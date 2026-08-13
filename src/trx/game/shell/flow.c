#include <trx/config.h>
#include <trx/config/registry.h>
#include <trx/core/log.h>
#include <trx/core/memory.h>
#include <trx/core/strings.h>
#include <trx/core/subsystem.h>
#include <trx/debug.h>
#include <trx/game/catalog/manager.h>
#include <trx/game/clock.h>
#include <trx/game/game_flow.h>
#include <trx/game/game_strings/manager.h>
#include <trx/game/lua.h>
#include <trx/game/output.h>
#include <trx/game/replay/test_recorder.h>
#include <trx/game/replay/test_replay.h>
#include <trx/game/savegame.h>
#include <trx/game/shell.h>
#include <trx/game/shell/platform.h>
#include <trx/game/shell/session.h>
#include <trx/game/shell/state.h>
#include <trx/game/stats.h>
#include <trx/gl/context.h>
#include <trx/version.h>

#include <SDL2/SDL.h>
#include <stdio.h>

static SHELL_SESSION *m_Session = nullptr;
static SDL_Window *m_Window = nullptr;
static char *m_PendingMod = nullptr;

// Flags preserved across mod switches (needed to rebuild args in main()).
static bool m_PrevHeadless = false;
static bool m_PrevQuiet = false;

// Given back before the config module goes down, so a mod switch does not
// leave a copy behind.
static int32_t m_ConfigListener = -1;

static void M_CreateGameWindow(void)
{
    if (m_Window != nullptr) {
        return; // Window persists across mod switches
    }
#if defined(TRX_TARGET_IOS)
    // Without ALLOW_HIGHDPI, SDL creates the backing CAEAGLLayer at a
    // fixed contentsScale of 1.0 regardless of the device's actual Retina
    // scale factor, which can desync the color/depth-stencil renderbuffer
    // dimensions SDL allocates internally from what the layer actually
    // provides -- producing a silent GL error at presentation time.
    //
    // SDL_WINDOW_FULLSCREEN_DESKTOP must be passed here, at creation time:
    // SDL's UIKit backend sizes the CAEAGLLayer (and therefore the actual
    // GL renderbuffer storage) from the window creation flags. Calling
    // SDL_SetWindowFullscreen() afterwards (in Shell_SyncToWindow) does
    // NOT resize that layer on iOS -- there's no real windowing system to
    // reflow, so the call is effectively a no-op for sizing purposes. Not
    // setting this flag here left the renderbuffer permanently sized to
    // the leftover desktop "windowed" config default (e.g. 480x320) while
    // the rest of the engine, once corrected to use the real display
    // size, issued glViewport calls far larger than that backing storage
    // -- producing a black screen and a GL_INVALID_OPERATION loop at
    // present time.
    const uint32_t window_flags = SDL_WINDOW_HIDDEN | SDL_WINDOW_RESIZABLE
        | SDL_WINDOW_OPENGL | SDL_WINDOW_ALLOW_HIGHDPI
        | SDL_WINDOW_FULLSCREEN_DESKTOP;
#else
    const uint32_t window_flags =
        SDL_WINDOW_HIDDEN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_OPENGL;
#endif
    m_Window = SDL_CreateWindow(
        "TRX", g_Config.window.x, g_Config.window.y, g_Config.window.width,
        g_Config.window.height, window_flags);

    if (m_Window == nullptr) {
        Shell_ExitSystemFmt("Failed to create SDL window: %s", SDL_GetError());
    }
    Shell_EnableThemeSupport(m_Window);
}

static void M_ExitUnsupportedGraphics(void)
{
    char *driver = TRX_GL_Context_DescribeDriver(m_Window);

#ifdef _WIN32
    const char *const hint =
        " Where the card is too old for that, installing "
        "Mesa3D lets TRX draw the game without it.";
#else
    const char *const hint = "";
#endif

    char *message = String_Format(
        "TRX needs OpenGL 3.3 to draw the game, and the graphics driver on "
        "this computer does not offer it.\n"
        "\n"
        "Graphics driver: %s\n"
        "\n"
        "Installing the latest drivers for the graphics card usually helps.%s",
        driver != nullptr ? driver : "unknown", hint);

    Shell_ExitSystem(message);

    Memory_FreePointer(&message);
    Memory_FreePointer(&driver);
}

static void M_CreateGLContext(void)
{
    if (TRX_GL_Context_GetWindowHandle() != nullptr) {
        return; // GL context persists across mod switches
    }
#if defined(TRX_TARGET_IOS)
    // iOS only speaks GLES, not desktop GL core profile. TRX's shaders
    // target GLSL 330 core / GLSL ES 300, both mapped from GLES 3.0.
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    SDL_GL_SetAttribute(
        SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
#else
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(
        SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
#endif
    if (!TRX_GL_Context_Attach(m_Window)) {
        M_ExitUnsupportedGraphics();
    }
}

static void M_ShowWindow(void)
{
    Shell_SyncToWindow();
    SDL_ShowWindow(m_Window);
    SDL_RaiseWindow(m_Window);
    Shell_RefreshRendererViewport();
}

static void M_HandleConfigChange(const EVENT *const event, void *const data)
{
    Shell_HandleConfigChange(event->data);
}

static void M_SetupSDL(void)
{
    SDL_version compiled;
    SDL_VERSION(&compiled);
    LOG_INFO(
        "SDL version: %d.%d.%d", compiled.major, compiled.minor,
        compiled.patch);
#if defined(TRX_TARGET_IOS)
    // With SDL_MAIN_HANDLED, SDL_Init refuses to run on platforms where it
    // normally supplies its own main() (Windows/WinRT/iOS) unless the app
    // explicitly confirms it is handling entry-point setup itself.
    SDL_SetMainReady();
#endif
    if (SDL_Init(SDL_INIT_EVENTS | SDL_INIT_VIDEO) < 0) {
        Shell_ExitSystemFmt("Cannot initialize SDL: %s", SDL_GetError());
    }
}

static void M_SetupGL(void)
{
    // Setup minimum properties of GL context
    SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
}

static void M_LoadCatalog(
    const CATALOG_CONTEXT context, const char *const filename,
    const bool allow_duplicates)
{
    const char *const path =
        TRXPath_Resolve(TRX_DYNAMIC_PATH_CATALOG, filename);
    if (!Catalog_Load(context, path, allow_duplicates)) {
        Shell_ExitSystemFmt("Failed to load catalogs from %s", path);
    }
}

static void M_InitModules(void)
{
    Shell_SetupHiDPI();
    Shell_SetupLibAV();
    M_SetupSDL();
    M_SetupGL();

    // Some of the subsystems read the clock or the video state as they come
    // up, so the platform stands first.
    Subsystem_InitAll();

    LUA_Init();

    const SHELL_ARGS *const args = Shell_GetArgs();
    if (args != nullptr && args->startup.dump_lua_api) {
        LUA_DumpAPI();
        exit(0);
    }
}

static void M_ShutdownModules(void)
{
    if (m_ConfigListener >= 0) {
        Config_UnsubscribeChanges(m_ConfigListener);
        m_ConfigListener = -1;
    }

    if (TestReplay_IsOpened()) {
        TestReplay_Close();
    }
    if (TestRecorder_IsOpened()) {
        TestRecorder_Close();
    }

    // The Lua bridges are subscribed to the modules they wrap and unsubscribe
    // here, so the modules have to still be standing.
    LUA_Shutdown();

    Subsystem_ShutdownAll();
}

static void M_PrepareSystem(void)
{
    SHELL_SESSION *const s = m_Session;
    ASSERT(s != nullptr);
    const char *const test_replay_path = s->args->test_replay_path;

    if (s->args->test_record_path != nullptr
        && s->args->test_replay_path != nullptr) {
        Shell_ExitSystem("Cannot use both --test-record and --test-replay");
    }

    if (test_replay_path != nullptr) {
        // Allow inferring engine version from outer args for replays lacking
        // embedded info (created with the old directory layout).
        g_TRVersion = s->args->startup.engine_version;
        SHELL_ARGS *const tmp_args = TestReplay_Open(test_replay_path);
        if (tmp_args != nullptr) {
            tmp_args->headless = s->args->headless;
            tmp_args->debug_render_performance =
                s->args->debug_render_performance;
            ShellSession_UseArgs(s, tmp_args);
        }
    } else if (s->args->headless) {
        Shell_ExitSystem("--headless can only be used with --test-replay");
    }

    g_TRVersion = s->args->startup.engine_version;
    LOG_INFO("Engine version: %d", g_TRVersion);
    LOG_INFO(
        "Mod: %s",
        s->args->startup.mod != nullptr ? s->args->startup.mod->name : nullptr);
    if (s->args->startup.engine_version <= 0
        || s->args->startup.mod == nullptr) {
        Shell_ExitSystem("No playable mods available.");
    }
    if (s->args->startup.mod->mod_type != MOD_DIRECT_LEVEL
        && test_replay_path == nullptr) {
        ShellState_RememberLastPlayedMod(s->args->startup.mod->name);
    }

    Config_RegisterBuiltInOptions();

    TRXPath_Init(s->args);

    // The catalogs name the objects, samples and music the subsystem loads
    // look themselves up in.
    M_LoadCatalog(CATALOG_OBJECTS, "catalog_objects.csv", false);
    M_LoadCatalog(CATALOG_MUSIC, "catalog_music.csv", false);
    M_LoadCatalog(CATALOG_SAMPLES, "catalog_samples.csv", true);
    M_LoadCatalog(CATALOG_LARA_STATES, "catalog_lara_states.csv", false);
    M_LoadCatalog(CATALOG_LARA_ANIMS, "catalog_lara_anims.csv", false);
    M_LoadCatalog(CATALOG_ITEM_ACTIONS, "catalog_item_actions.csv", false);
    Subsystem_LoadAll();

    if (test_replay_path != nullptr) {
        TestReplay_Start();
    } else {
        char *engine_config_path =
            TRXPath_ExpandVars("%config_dir%/TR%tr_version%X.json5");
        if (engine_config_path == nullptr) {
            Shell_ExitSystem("Failed to resolve engine config path");
        }
        Config_Read(
            engine_config_path, Shell_GetGameFlowPath(s->args->startup.mod));
        Memory_FreePointer(&engine_config_path);

        if (s->args->test_record_path != nullptr) {
            TestRecorder_Open(
                s->args->test_record_path, s->args->original_args);
        }
    }
    m_ConfigListener = Config_SubscribeChanges(M_HandleConfigChange, nullptr);

    Subsystem_ApplyConfigAll();
}

void Shell_RequestModSwitch(const char *const mod_name)
{
    Memory_FreePointer(&m_PendingMod);
    m_PendingMod = Memory_DupStr(mod_name);
}

const char *Shell_GetPendingMod(void)
{
    return m_PendingMod;
}

void Shell_ClearPendingMod(void)
{
    Memory_FreePointer(&m_PendingMod);
}

bool Shell_GetPrevHeadless(void)
{
    return m_PrevHeadless;
}

bool Shell_GetPrevQuiet(void)
{
    return m_PrevQuiet;
}

const SHELL_ARGS *Shell_GetArgs(void)
{
    ASSERT(m_Session != nullptr);
    return m_Session->args;
}

void Shell_SetHeadless(const bool headless)
{
    ASSERT(m_Session != nullptr);
    SHELL_ARGS *const args = (SHELL_ARGS *)m_Session->args;
    if (args->headless == headless) {
        return;
    }

#if defined(TRX_TARGET_IOS)
    // iOS has no desktop-style resizable/positionable window -- the app is
    // always fullscreen. Force this regardless of what the config file
    // says (it may carry a leftover desktop "windowed" size), so
    // Shell_GetCurrentSize() always resolves to the real display size
    // instead of a small windowed rect. See Shell_SyncToWindow() in
    // game/shell/config.c for the corresponding iOS-specific bypass.
    g_Config.window.is_fullscreen = true;
#endif

    Clock_SetSimSpeed(Clock_GetSpeedMultiplier());
    if (!s->args->headless) {
        Sound_Init();
        Music_Init();
        Sound_SetMasterVolume(g_Config.audio.sound_volume);
        Music_SetVolume(g_Config.audio.music_volume);
    } else {
    args->headless = headless;
    // The clock counts frames either way; only the pacing changes here.
    if (headless) {
        Clock_DisableWait();
    } else {
        Clock_EnableWait();
        Clock_SyncTick();
    }
}

SDL_Window *Shell_GetWindow(void)
{
    return m_Window;
}

int32_t Shell_Main(const SHELL_ARGS *const args)
{
    ASSERT(m_Session == nullptr);
    m_Session = ShellSession_Create();

    SHELL_SESSION *const s = m_Session;
    ShellSession_UseArgs(s, args);

    LOG_INFO("Game directory: %s", TRXPath_Get(TRX_PATH_TRX_DIR));

    M_InitModules();
    M_PrepareSystem();
    if (s->args->startup.mod == nullptr) {
        Shell_ExitSystem("No --mod specified.");
        return 1;
    }
    TRXPath_Init(s->args);
    M_CreateGameWindow();
    M_CreateGLContext();
    Output_Init();
    if (!s->args->headless) {
        M_ShowWindow();
    }

    GF_Init();
    GF_LoadFromFile(Shell_GetGameFlowPath(s->args->startup.mod));

    GameStringManager_LoadForMod(s->args->startup.mod);

    Savegame_Init();
    SG_Manager_ScanSavedGames();

    LUA_RunGameScript();

    // The settings a recording carries are the ones that exist by now, the
    // game's own among them.
    if (TestReplay_IsOpened()) {
        TestReplay_ApplyDeferredConfig();
    }
    if (TestRecorder_IsOpened()) {
        TestRecorder_WriteConfig();
    }

    Stats_CalculateMaxStats();
    GF_RunUntilExit(GF_DoFrontendSequence());

    if (m_PendingMod != nullptr) {
        if (TestReplay_IsOpened()) {
            TestReplay_Close();
        }
        if (TestRecorder_IsOpened()) {
            TestRecorder_Close();
        }
        // Save flags needed to rebuild args in main() before freeing the
        // session (which owns and will free the args struct).
        m_PrevHeadless = s->args->headless;
        m_PrevQuiet = s->args->quiet;
        M_ShutdownModules();
        ShellSession_Free(m_Session);
        m_Session = nullptr;
        return 0;
    }

    const int32_t replay_exit_code = TestReplay_GetExitCodeOverride();
    return replay_exit_code >= 0 ? replay_exit_code : 0;
}

void Shell_Shutdown(void)
{
    M_ShutdownModules();
    TRX_GL_Context_Detach();
    Log_Shutdown();
    if (m_Session != nullptr) {
        ShellSession_Free(m_Session);
        m_Session = nullptr;
    }
}
