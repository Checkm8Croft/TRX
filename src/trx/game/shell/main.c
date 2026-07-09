#include <trx/core/filesystem.h>
#include <trx/core/log.h>
#include <trx/core/memory.h>
#include <trx/core/strings.h>
#include <trx/core/utils.h>
#include <trx/game/shell.h>
#include <trx/game/shell/common.h>
#include <trx/game/shell/mod.h>
#include <trx/version.h>

#include <stdlib.h>
#include <string.h>

// No-op when SDL_MAIN_HANDLED is defined (every platform except iOS).
// On iOS, renames our main() below to SDL_main(), so that SDL's own
// real main() (in libSDL2main.a) becomes the C entry point, calls
// UIApplicationMain, and then invokes this function once its UIKit/
// CADisplayLink run loop is actually up and running.
#include <SDL2/SDL_main.h>

#if defined(TRX_TARGET_IOS)
// The .app bundle (TRX_PATH_TRX_DIR / SDL_GetBasePath) is read-only on iOS.
// User-writable content -- the games the player supplies themselves, plus
// saves/screenshots/cache -- must live in the sandbox's Documents
// directory instead, which is also reachable via Finder/Files thanks to
// UIFileSharingEnabled. TRXPath_Init already honours these env vars, so we
// only need to point them here and make sure the directories exist (the
// path-resolution fallback silently ignores TRX_GAMES_DIR if the directory
// doesn't exist yet).
static void M_SetupIOSDirs(void)
{
    const char *const home = getenv("HOME");
    if (home == nullptr) {
        return;
    }

    const struct {
        const char *env_name;
        const char *subdir;
    } dirs[] = {
        { "TRX_GAMES_DIR", "games" },
        { "TRX_SAVES_DIR", "saves" },
        { "TRX_SCREENSHOTS_DIR", "screenshots" },
        { "TRX_CACHE_DIR", "cache" },
        // TRX_CONFIG_DIR holds both read data (outfits.json5, base
        // strings) and write targets (TR1X.json5, console history), so it
        // needs to be writable too. This only affects TRX_DYNAMIC_PATH_
        // COMMON_CONFIG lookups; shaders resolve via %trx_dir%/cfg/shaders
        // directly and stay in the read-only bundle, unaffected by this.
        { "TRX_CONFIG_DIR", "cfg" },
    };

    for (size_t i = 0; i < sizeof(dirs) / sizeof(dirs[0]); i++) {
        char *const full_path =
            String_Format("%s/Documents/%s", home, dirs[i].subdir);
        if (!File_DirExists(full_path)) {
            File_CreateDirectory(full_path);
        }
        setenv(dirs[i].env_name, full_path, 1);
        Memory_FreePointer(&full_path);
    }
}
#endif

int main(int argc, char *argv[])
{
#if defined(TRX_TARGET_IOS)
    M_SetupIOSDirs();
#endif

    VECTOR *raw_args = Vector_Create(sizeof(const char *));
    for (int32_t i = 1; i < argc; i++) {
        char *const copied_arg = Memory_DupStr(argv[i]);
        Vector_Add(raw_args, &copied_arg);
    }

    TRXPath_Init(nullptr);

    // Initialize logging as early as possible -- in particular, before
    // Shell_ScanAvailableMods(), so that any warnings it emits (e.g. a mod
    // directory that failed to resolve) actually land in the log file
    // instead of being silently dropped because no log file was open yet.
#if defined(TRX_TARGET_IOS)
    // The app bundle (SDL_GetBasePath / TRX_PATH_TRX_DIR) is read-only on
    // iOS; write the log to the sandbox's writable Documents directory
    // instead, which is also reachable via Finder/Files (UIFileSharingEnabled).
    char *log_path = String_Format("%s/Documents/TRX.log", getenv("HOME"));
#else
    char *log_path = String_Format("%s/TRX.log", TRXPath_Get(TRX_PATH_TRX_DIR));
#endif
#if defined(TRX_TARGET_IOS)
    // LOG_LEVEL_MAX ("log everything", including LOG_DEBUG/LOG_INFO) was
    // causing a real, measurable performance problem on-device: several
    // per-frame call sites (e.g. TRX_GL_Context_SwitchToViewport, hit
    // multiple times per frame) log via LOG_INFO, and each call does a
    // synchronous write to the log file in the app's Documents directory.
    // Doing that 4+ times per frame at 60fps was very likely a major
    // contributor to the game feeling sluggish on-device. Only warnings
    // and errors are worth paying that cost for by default; DEBUG/INFO
    // are still available via Log_SetMinLevel() if needed for future
    // diagnosis.
    Log_Init(log_path, LOG_LEVEL_WARNING);
#else
    Log_Init(log_path, LOG_LEVEL_MAX);
#endif
    Memory_FreePointer(&log_path);

    LOG_INFO("Starting %s", g_TRXVersion);
#if defined(TRX_TARGET_IOS)
    LOG_INFO("Games directory: %s", TRXPath_Get(TRX_PATH_GAMES_DIR));
    LOG_INFO("Saves directory: %s", TRXPath_Get(TRX_PATH_SAVES_DIR));
    LOG_INFO("Config directory: %s", TRXPath_Get(TRX_PATH_CONFIG_DIR));
#endif

    Shell_ScanAvailableMods();
    SHELL_ARGS *args = Shell_ParseArgs(raw_args);
    if (args == nullptr) {
        return 0;
    }

    TRXPath_Init(args);
#if defined(TRX_TARGET_IOS)
    // See the Log_Init() call above: iOS has no CLI to pass --quiet, so
    // args->quiet is always false here, which would otherwise reset the
    // log level back to LOG_LEVEL_MAX and undefeat that fix.
    Log_SetMinLevel(LOG_LEVEL_WARNING);
#else
    Log_SetMinLevel(args->quiet ? LOG_LEVEL_WARNING : LOG_LEVEL_MAX);
#endif

    Shell_ValidateMods();
    if (args->startup.mod == nullptr || !args->startup.mod->is_valid) {
        args->startup.mod =
            Shell_SelectStartupMod(args->startup.engine_version);
        if (args->startup.mod != nullptr && args->startup.engine_version == 0) {
            args->startup.engine_version = args->startup.mod->engine_version;
        }
    }

    int32_t exit_code;
    bool restart;
    do {
        TRXPath_Init(args);
        restart = false;
        exit_code = Shell_Main(args);
        // Note: on a mod switch, Shell_Main has already freed args (via the
        // session) and reset m_Session to nullptr. Do not touch args after
        // this point in the restart branch.

        const char *const pending_mod = Shell_GetPendingMod();
        if (pending_mod != nullptr) {
            const SHELL_MOD *const mod = Shell_GetModByName(pending_mod);
            Shell_ClearPendingMod();
            if (mod != nullptr && mod->is_available) {
                LOG_INFO("Switching mod to: %s", mod->name);
                SHELL_ARGS *const next_args = Memory_Alloc(sizeof(SHELL_ARGS));
                *next_args = (SHELL_ARGS) {
                    .startup = {
                        .engine_version = mod->engine_version,
                        .mod = mod,
                        .level_request = { .num = -1 },
                        .save_to_load = -1,
                    },
                    .headless = Shell_GetPrevHeadless(),
                    .quiet = Shell_GetPrevQuiet(),
                };
                args = next_args;
                TRXPath_Init(args);
                restart = true;
            }
        }
    } while (restart);

    Shell_Terminate(exit_code);
    return exit_code;
}
