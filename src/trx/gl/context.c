#if !defined(__APPLE__) || (defined(TARGET_OS_IPHONE) && !TARGET_OS_IPHONE)
#include <GL/glew.h>
#endif
#include <trx/gl/context.h>
#include <trx/core/log.h>
#include <trx/core/memory.h>
#include <trx/core/strings.h>
#include <trx/game/shell.h>
#include <trx/game/viewport.h>
#include <trx/gl/renderer.h>
#include <trx/gl/screenshot.h>
#include <trx/gl/utils.h>

#include <trx/gl/gl_platform.h>
#include <SDL2/SDL_video.h>
#include <string.h>
#ifdef __APPLE__
// Garantisce che GLvoid sia semplicemente trattato come void standard
#ifndef GLvoid
#define GLvoid void
#endif

// Su macOS le funzioni di callback non richiedono convenzioni di chiamata Windows (come __stdcall)
#ifndef GLAPIENTRY
#define GLAPIENTRY
#endif
#endif
#ifndef GL_DEBUG_SEVERITY_NOTIFICATION
#define GL_DEBUG_SEVERITY_NOTIFICATION 0x826B
#endif

typedef struct {
    SDL_GLContext context;
    SDL_Window *window_handle;
    VIEWPORT_SPACE space;
#if defined(TRX_TARGET_IOS)
    GLuint main_framebuffer;
    GLuint main_color_renderbuffer;
#endif

    TRX_GL_CONFIG config;

    // Size of the SDL window.
    int32_t window_width;
    int32_t window_height;

    char *scheduled_screenshot_path;
    TRX_GL_RENDERER *renderer;
} TRX_GL_CONTEXT;

extern RGBA_F Output_GetFogColor(void);

static TRX_GL_CONTEXT m_Context = {};

static bool M_IsExtensionSupported(const char *name)
{
    int number_of_extensions;

    glGetIntegerv(GL_NUM_EXTENSIONS, &number_of_extensions);
    TRX_GL_CheckError();

    for (int i = 0; i < number_of_extensions; i++) {
        const char *gl_ext = (const char *)glGetStringi(GL_EXTENSIONS, i);
        TRX_GL_CheckError();

        if (gl_ext && !strcmp(gl_ext, name)) {
            return true;
        }
    }
    return false;
}

#if !defined(TRX_TARGET_IOS)
// KHR_debug (glDebugMessageCallback and friends) is not exposed by the
// iOS GLES3 headers; this whole callback is desktop-only.
static GLvoid GLAPIENTRY M_GLDebug(
    const GLenum source, const GLenum type, const GLuint id,
    const GLenum severity, const GLsizei length, const GLchar *const message,
    const void *const user_param)
{
    if (severity == GL_DEBUG_SEVERITY_NOTIFICATION) {
        return;
    }
    size_t len = strlen(message);
    if (len > 0 && message[len - 1] == '\n') {
        len--;
    }
    LOG_INFO("%d %*s", source, len, message);
}
#endif

void TRX_GL_Context_SwitchToViewport(const VIEWPORT_SPACE space)
{
    const VIEWPORT_RECT rect = Viewport_GetRect(space);
    m_Context.space = space;
#if defined(TRX_TARGET_IOS)
    // TEMP DIAGNOSTIC: log currently bound framebuffer + rect just before
    // the call that's erroring, to find the real cause instead of guessing.
    GLint current_fbo = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &current_fbo);
    LOG_INFO(
        "SwitchToViewport space=%d rect=(%d,%d,%d,%d) bound_fbo=%d "
        "main_fbo=%u",
        space, rect.x, rect.y, rect.width, rect.height, current_fbo,
        TRX_GL_Context_GetMainFramebuffer());
#endif
    glViewport(rect.x, rect.y, rect.width, rect.height);
    TRX_GL_CheckError();
}

VIEWPORT_SPACE TRX_GL_Context_GetViewport(void)
{
    return m_Context.space;
}

bool TRX_GL_Context_Attach(void *window_handle)
{
    const char *shading_ver;

    if (m_Context.window_handle) {
        LOG_ERROR("Context already attached");
        return false;
    }

    LOG_INFO("Attaching to window %p", window_handle);
    m_Context.context = SDL_GL_CreateContext(window_handle);
    if (m_Context.context == nullptr) {
        LOG_ERROR("Can't create OpenGL context: %s", SDL_GetError());
        return false;
    }

    m_Context.config.line_width = 1;
    m_Context.config.enable_wireframe = false;
#if defined(TRX_TARGET_IOS)
    // With SDL_WINDOW_ALLOW_HIGHDPI, SDL_GetWindowSize returns logical
    // points, not the actual pixel dimensions of the backing renderbuffer
    // on Retina devices; use the real drawable size instead so viewports
    // and FBOs are sized in pixels, matching what actually gets rendered.
    SDL_GL_GetDrawableSize(
        window_handle, &m_Context.window_width, &m_Context.window_height);
#else
    SDL_GetWindowSize(
        window_handle, &m_Context.window_width, &m_Context.window_height);
#endif

    m_Context.window_handle = window_handle;

    if (SDL_GL_MakeCurrent(m_Context.window_handle, m_Context.context)) {
        Shell_ExitSystemFmt(
            "Can't activate OpenGL context: %s", SDL_GetError());
    }

#if defined(TRX_TARGET_IOS)
    // SDL's iOS backend creates its own screen framebuffer here and keeps
    // it bound; this is the only point where we're guaranteed it's still
    // the active one, so capture its id now for later use (see
    // TRX_GL_Context_GetMainFramebuffer).
    {
        GLint main_fbo = 0;
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &main_fbo);
        m_Context.main_framebuffer = (GLuint)main_fbo;

        GLint color_rb = 0;
        glGetFramebufferAttachmentParameteriv(
            GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
            GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME, &color_rb);
        m_Context.main_color_renderbuffer = (GLuint)color_rb;
    }
#endif

#if !defined(TRX_TARGET_IOS)
    // GLES entry points are linked directly against OpenGLES.framework and
    // require no runtime extension loader.
    const GLenum err = glewInit();
    if (err != GLEW_OK) {
        if (err != 4) {
            Shell_ExitSystemFmt(
                "Can't initialize GLEW for OpenGL extension loading: %d", err);
        }
        // https://github.com/nigels-com/glew/issues/417
        LOG_WARNING("GLEW failed to init: %d", err);
    }
#endif

    LOG_INFO("OpenGL vendor string:   %s", glGetString(GL_VENDOR));
    LOG_INFO("OpenGL renderer string: %s", glGetString(GL_RENDERER));
    LOG_INFO("OpenGL version string:  %s", glGetString(GL_VERSION));

    shading_ver = (const char *)glGetString(GL_SHADING_LANGUAGE_VERSION);
    if (shading_ver != nullptr) {
        LOG_INFO("Shading version string: %s", shading_ver);
    } else {
        TRX_GL_CheckError();
    }

    glClearColor(0, 0, 0, 0);
    glClearDepth(1);
    TRX_GL_CheckError();

    // VSync defaults to on unless user disabled it in runtime json
    SDL_GL_SetSwapInterval(1);

#if DEBUG && !defined(TRX_TARGET_IOS)
    // KHR_debug / glDebugMessageCallback is not exposed by the iOS GLES3
    // headers; skip on that platform.
    if (glDebugMessageCallback != nullptr) {
        glDebugMessageCallback(M_GLDebug, nullptr);
    }
    glEnable(GL_DEBUG_OUTPUT);
#endif

    m_Context.renderer = &g_TRX_GL_Renderer;
    if (m_Context.renderer->init != nullptr) {
        m_Context.renderer->init(m_Context.renderer, &m_Context.config);
    }

    return true;
}

char *TRX_GL_Context_DescribeDriver(void *const window_handle)
{
    SDL_GL_ResetAttributes();
    SDL_GLContext context = SDL_GL_CreateContext(window_handle);
    if (context == nullptr) {
        LOG_ERROR("Can't create fallback OpenGL context: %s", SDL_GetError());
        return nullptr;
    }

    char *result = nullptr;
    if (SDL_GL_MakeCurrent(window_handle, context) == 0) {
        const char *const renderer = (const char *)glGetString(GL_RENDERER);
        const char *const version = (const char *)glGetString(GL_VERSION);
        if (renderer != nullptr && version != nullptr) {
            result = String_Format("%s (OpenGL %s)", renderer, version);
        } else if (version != nullptr) {
            result = String_Format("OpenGL %s", version);
        }
        SDL_GL_MakeCurrent(window_handle, nullptr);
    }

    SDL_GL_DeleteContext(context);
    return result;
}

void TRX_GL_Context_Detach(void)
{
    if (!m_Context.window_handle) {
        return;
    }

    if (m_Context.renderer != nullptr
        && m_Context.renderer->shutdown != nullptr) {
        m_Context.renderer->shutdown(m_Context.renderer);
    }

    SDL_GL_MakeCurrent(nullptr, nullptr);

    if (m_Context.context != nullptr) {
        SDL_GL_DeleteContext(m_Context.context);
        m_Context.context = nullptr;
    }
    m_Context.window_handle = nullptr;
}

void TRX_GL_Context_SetDisplayFilter(const TEXTURE_FILTER filter)
{
    m_Context.config.display_filter = filter;
}

void TRX_GL_Context_SetMultisamplingFactor(const int32_t factor)
{
    m_Context.config.multisampling_factor = factor;
}

void TRX_GL_Context_SetDithering(const bool enable)
{
    m_Context.config.enable_dithering = enable;
}

bool TRX_GL_Context_GetWireframeMode(void)
{
    return m_Context.config.enable_wireframe;
}

void TRX_GL_Context_SetWireframeMode(const bool enable)
{
    m_Context.config.enable_wireframe = enable;
}

void TRX_GL_Context_SetLineWidth(const int32_t line_width)
{
    m_Context.config.line_width = line_width;
}

void TRX_GL_Context_SetVSync(bool vsync)
{
    SDL_GL_SetSwapInterval(vsync);
}

void *TRX_GL_Context_GetWindowHandle(void)
{
    return m_Context.window_handle;
}

GLuint TRX_GL_Context_GetMainFramebuffer(void)
{
#if defined(TRX_TARGET_IOS)
    return m_Context.main_framebuffer;
#else
    return 0;
#endif
}

GLuint TRX_GL_Context_GetMainColorRenderbuffer(void)
{
#if defined(TRX_TARGET_IOS)
    return m_Context.main_color_renderbuffer;
#else
    return 0;
#endif
}

void TRX_GL_Context_RefreshMainFramebuffer(void)
{
#if defined(TRX_TARGET_IOS)
    GLint main_fbo = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &main_fbo);
    m_Context.main_framebuffer = (GLuint)main_fbo;

    GLint color_rb = 0;
    glGetFramebufferAttachmentParameteriv(
        GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
        GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME, &color_rb);
    m_Context.main_color_renderbuffer = (GLuint)color_rb;
#endif
}

void TRX_GL_Context_Clear(void)
{
    const RGBA_F white = { 1.0f, 1.0f, 1.0f, 0.0f };
    const RGBA_F fog = Output_GetFogColor();
    const RGBA_F black = { 0.0f, 0.0f, 0.0f, 0.0f };
    const RGBA_F color =
        m_Context.space == VIEWPORT_GAME && m_Context.config.enable_wireframe
        ? white
        : m_Context.space == VIEWPORT_GAME ? fog
                                           : black;
    glClearBufferfv(GL_COLOR, 0, &color.r);
}

void TRX_GL_Context_SwapBuffers(void)
{
    glFlush();
    TRX_GL_CheckError();

    if (m_Context.renderer != nullptr
        && m_Context.renderer->swap_buffers != nullptr) {
        m_Context.renderer->swap_buffers(m_Context.renderer);
    }
}

void TRX_GL_Context_ScheduleScreenshot(const char *path)
{
    Memory_FreePointer(&m_Context.scheduled_screenshot_path);
    m_Context.scheduled_screenshot_path = Memory_DupStr(path);
}

const char *TRX_GL_Context_GetScheduledScreenshotPath(void)
{
    return m_Context.scheduled_screenshot_path;
}

void TRX_GL_Context_ClearScheduledScreenshotPath(void)
{
    Memory_FreePointer(&m_Context.scheduled_screenshot_path);
}

TRX_GL_CONFIG *TRX_GL_Context_GetConfig(void)
{
    return &m_Context.config;
}
