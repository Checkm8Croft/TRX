#include <trx/av/audio.h>
#include <trx/config.h>
#include <trx/core/log.h>
#include <trx/core/memory.h>
#include <trx/debug.h>
#include <trx/game/shell.h>

#ifdef _WIN32
    #include <objbase.h>
    #include <windows.h>
#endif

#include <SDL2/SDL.h>
#include <SDL2/SDL_messagebox.h>
#include <libavcodec/version.h>
#include <libavutil/log.h>
#include <stdio.h>

static bool m_IsExiting = false;
static bool m_IsFocused = true;

static void M_ShowFatalError(
    const char *const log_message, const char *const dialog_message)
{
    LOG_ERROR("%s", log_message);
    SDL_Window *const window = Shell_GetWindow();
    SDL_ShowSimpleMessageBox(
        SDL_MESSAGEBOX_ERROR, "Tomb Raider Error", dialog_message, window);
    Shell_Terminate(1);
}

const char *Shell_GetConfigDir(void)
{
    return TRXPath_Get(TRX_PATH_CONFIG_DIR);
}

const char *Shell_GetCacheDir(void)
{
    return TRXPath_Get(TRX_PATH_CACHE_DIR);
}

void Shell_Terminate(int32_t exit_code)
{
    Shell_Shutdown();

    SDL_Window *const window = Shell_GetWindow();
    if (window != nullptr) {
        SDL_DestroyWindow(window);
    }
    if (Audio_ShouldSkipSDLQuitAudio()) {
        const Uint32 inited = SDL_WasInit(0);
        const Uint32 quit_flags = inited & ~SDL_INIT_AUDIO;
        if (quit_flags != 0) {
            SDL_QuitSubSystem(quit_flags);
        }
    } else {
        SDL_Quit();
    }
    exit(exit_code);
}

void Shell_ExitSystem(const char *message)
{
    M_ShowFatalError(message, message);
    Shell_Shutdown();
}

void Shell_ExitSystemEx(
    const char *const log_message, const char *const dialog_message)
{
    M_ShowFatalError(log_message, dialog_message);
    Shell_Shutdown();
}

void Shell_ExitSystemFmt(const char *fmt, ...)
{
    va_list va;
    va_start(va, fmt);
    int32_t size = vsnprintf(nullptr, 0, fmt, va) + 1;
    char *message = Memory_Alloc(size);
    va_end(va);

    va_start(va, fmt);
    vsnprintf(message, size, fmt, va);
    va_end(va);

    Shell_ExitSystem(message);

    Memory_FreePointer(&message);
}

bool Shell_IsFullscreen(void)
{
    SDL_Window *const window = Shell_GetWindow();
    ASSERT(window != nullptr);
    const Uint32 flags = SDL_GetWindowFlags(window);
    return (flags & SDL_WINDOW_FULLSCREEN_DESKTOP) != 0;
}

SHELL_SIZE Shell_GetCurrentSize(void)
{
#if defined(TRX_TARGET_IOS)
    // SDL's UIKit backend doesn't toggle SDL_WINDOW_FULLSCREEN_DESKTOP or
    // adjust SDL_GetWindowSize() the way desktop backends do when
    // SDL_SetWindowFullscreen() is called -- there's no real windowing
    // system to change, so the flag Shell_IsFullscreen() checks never
    // actually gets set, and SDL_GetWindowSize() keeps returning whatever
    // logical size the window was created with (a leftover desktop config
    // default). iOS is always effectively fullscreen, so always use the
    // real display size directly instead of trusting that flag.
    //
    // Also, unlike Shell_GetCurrentDisplaySize() (SDL_GetCurrentDisplayMode,
    // which reports size in *points*), this value feeds straight into
    // glViewport() via Viewport_Reset()/Viewport_GetRect(). The actual
    // renderbuffer storage backing the window is allocated in *pixels*
    // (SDL_WINDOW_ALLOW_HIGHDPI scales it by the Retina factor), so
    // using points here left glViewport() only covering a fraction of the
    // real backbuffer -- the rendered content was correct, just confined
    // to a small corner of the screen. SDL_GL_GetDrawableSize() reports
    // the actual pixel dimensions of that backing store, matching what
    // glViewport() needs.
    if (Shell_GetArgs()->headless) {
        return Shell_GetDefaultSize();
    }
    SDL_Window *const window = Shell_GetWindow();
    if (window == nullptr) {
        return Shell_GetCurrentDisplaySize();
    }
    SHELL_SIZE result;
    SDL_GL_GetDrawableSize(window, &result.w, &result.h);
    return result;
#else
    return Shell_IsFullscreen() ? Shell_GetCurrentDisplaySize()
                                : Shell_GetWindowSize();
#endif
}

SHELL_SIZE Shell_GetDefaultSize(void)
{
    return (SHELL_SIZE) { SHELL_HEADLESS_WIDTH, SHELL_HEADLESS_HEIGHT };
}

SHELL_SIZE Shell_GetWindowSize(void)
{
    if (Shell_GetArgs()->headless) {
        return Shell_GetDefaultSize();
    }
    SDL_Window *const window = Shell_GetWindow();
    SHELL_SIZE result = { .w = -1, .h = -1 };
    if (window != nullptr) {
        SDL_GetWindowSize(window, &result.w, &result.h);
    }
    return result;
}

SHELL_SIZE Shell_GetCurrentDisplaySize(void)
{
    if (Shell_GetArgs()->headless) {
        return Shell_GetDefaultSize();
    }
    int32_t display_idx = 0;
    SDL_Window *const window = Shell_GetWindow();
    if (window != nullptr) {
        display_idx = SDL_GetWindowDisplayIndex(window);
    }
    SDL_DisplayMode dm;
    const int32_t rc = SDL_GetCurrentDisplayMode(display_idx, &dm);
    LOG_INFO(
        "DEBUG bisect: SDL_GetCurrentDisplayMode(idx=%d) rc=%d dm=%dx%d "
        "window=%p SDL_GetError=%s",
        display_idx, rc, rc == 0 ? dm.w : -1, rc == 0 ? dm.h : -1,
        (void *)window, SDL_GetError());
    if (rc == 0) {
        return (SHELL_SIZE) { .w = dm.w, .h = dm.h };
    }
    return (SHELL_SIZE) { .w = -1, .h = -1 };
}

void Shell_ScheduleExit(void)
{
    m_IsExiting = true;
}

bool Shell_IsExiting(void)
{
    return m_IsExiting;
}

void Shell_SetIsFocused(const bool is_focused)
{
    m_IsFocused = is_focused;
}

bool Shell_IsFocused(void)
{
    return m_IsFocused;
}
