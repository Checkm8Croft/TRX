#include <trx/gl/renderer.h>

#include <trx/core/log.h>
#include <trx/core/memory.h>
#include <trx/debug.h>
#include <trx/gl/buffer.h>
#include <trx/gl/context.h>
#include <trx/gl/enum.h>
#include <trx/gl/fbo.h>
#include <trx/gl/program.h>
#include <trx/gl/sampler.h>
#include <trx/gl/screenshot.h>
#include <trx/gl/texture.h>
#include <trx/gl/utils.h>
#include <trx/gl/vertex_array.h>

#include <trx/gl/gl_platform.h>
#include <SDL2/SDL_error.h>
#include <SDL2/SDL_video.h>
#include <stdint.h>

// Diagnostic only: set to 1 (or pass -DTRX_PERF_PROBE=1) to log a CPU vs GPU
// breakdown of the presented frames. Off by default; no effect on any
// platform while it is 0.
#ifndef TRX_PERF_PROBE
    #define TRX_PERF_PROBE 0
#endif

#if TRX_PERF_PROBE
    #include <SDL2/SDL_timer.h>
    #include <time.h>
#endif

// How many buffers the driver may be rotating through. Three covers the
// deepest chain a driver puts behind a window.
#define M_SWAPCHAIN_DEPTH 3

typedef struct {
    const TRX_GL_CONFIG *config;

    TRX_GL_FBO geometry_fbo;
    TRX_GL_FBO ui_fbo;
    // Holds the geometry framebuffer brought down to the scene resolution.
    // Only allocated while there is something to resolve.
    TRX_GL_FBO resolve_fbo;
    // The size the geometry framebuffer resolves to, recorded when it was
    // sized. The viewport moves ahead of the framebuffer, which is only
    // rebuilt once the frame it belongs to has been presented.
    int32_t scene_width;
    int32_t scene_height;
    // Whether resolve_fbo already holds this frame's scene.
    bool scene_resolved;

    // Full-screen quad resources for blitting FBOs to default framebuffer.
    TRX_GL_VERTEX_ARRAY vertex_array;
    TRX_GL_BUFFER buffer;
    TRX_GL_SAMPLER sampler;
    TRX_GL_PROGRAM program;
    GLint loc_dither_mode;
    GLint loc_supersample;
    GLint loc_palette_lut_size;
    TRX_GL_TEXTURE palette_lut;
    int32_t palette_lut_size;
    GLuint composite_fbo;
} M_CONTEXT;

#if TRX_PERF_PROBE
// Frames per report line.
    #define M_PROBE_REPORT_FRAMES 120
// One frame in this many is followed by glFinish() to measure how much GPU
// work is still pending once the CPU has submitted everything. Kept sparse
// because glFinish() itself stalls the pipeline.
    #define M_PROBE_FINISH_EVERY 15
// A frame longer than this missed a 60 Hz refresh.
    #define M_PROBE_SLOW_MS 17.5

typedef struct {
    bool primed;
    double last_end_ms;
    double last_cpu_ms;
    int32_t total_frames;
    int32_t frames;
    int32_t slow_frames;
    int32_t finish_samples;
    double interval_ms;
    double worst_ms;
    double cpu_ms;
    double composite_ms;
    double swap_ms;
    double finish_ms;
} M_PROBE;

static M_PROBE m_Probe;

static double M_ProbeNowMs(void)
{
    return (double)SDL_GetPerformanceCounter() * 1000.0
        / (double)SDL_GetPerformanceFrequency();
}

// CPU time actually burnt by the render thread, excluding any time it spent
// asleep or blocked (SDL_Delay, vsync, waiting on the GPU). Returns -1 where
// the platform offers no per-thread clock.
static double M_ProbeThreadCpuMs(void)
{
    #if defined(CLOCK_THREAD_CPUTIME_ID)
    struct timespec ts;
    if (clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts) == 0) {
        return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1e6;
    }
    #endif
    return -1.0;
}

static void M_ProbeReport(const M_CONTEXT *const p)
{
    const double n = (double)m_Probe.frames;
    const double interval = m_Probe.interval_ms / n;
    const double cpu = m_Probe.cpu_ms / n;
    const double composite = m_Probe.composite_ms / n;
    const double swap = m_Probe.swap_ms / n;
    const double finish = m_Probe.finish_samples > 0
        ? m_Probe.finish_ms / (double)m_Probe.finish_samples
        : -1.0;
    const double cpu_ratio = cpu > 0.0 ? cpu / interval : -1.0;

    const char *hint;
    if (cpu_ratio > 0.85) {
        hint = "CPU-bound (render thread saturated)";
    } else if (finish > 0.3 * interval) {
        hint = "GPU-bound (GPU still busy after all work was submitted)";
    } else if (m_Probe.slow_frames * 10 > m_Probe.frames) {
        hint = "stalls without saturation (pacing/vsync/driver sync)";
    } else {
        hint = "not saturated (frame pacing is the limit)";
    }

    // Logged as a warning because the iOS log drops INFO and below.
    LOG_WARNING(
        "PERF %dx%d | frame %.2f ms (worst %.1f, slow %d/%d) | cpu %.2f ms "
        "(%.0f%%) | composite %.2f | swap %.2f | gpu-tail %.2f (n=%d) | "
        "hint: %s",
        p->geometry_fbo.width, p->geometry_fbo.height, interval,
        m_Probe.worst_ms, m_Probe.slow_frames, m_Probe.frames, cpu,
        cpu_ratio * 100.0, composite, swap, finish, m_Probe.finish_samples,
        hint);

    m_Probe.frames = 0;
    m_Probe.slow_frames = 0;
    m_Probe.finish_samples = 0;
    m_Probe.interval_ms = 0.0;
    m_Probe.worst_ms = 0.0;
    m_Probe.cpu_ms = 0.0;
    m_Probe.composite_ms = 0.0;
    m_Probe.swap_ms = 0.0;
    m_Probe.finish_ms = 0.0;
}

static void M_ProbeFrameDone(
    const M_CONTEXT *const p, const double composite_start,
    const double composite_end, const double finish_ms,
    const double swap_start)
{
    const double swap_end = M_ProbeNowMs();
    const double cpu_now = M_ProbeThreadCpuMs();

    if (m_Probe.primed) {
        const double interval = swap_end - m_Probe.last_end_ms;
        m_Probe.interval_ms += interval;
        if (interval > m_Probe.worst_ms) {
            m_Probe.worst_ms = interval;
        }
        if (interval > M_PROBE_SLOW_MS) {
            m_Probe.slow_frames++;
        }
        if (cpu_now >= 0.0 && m_Probe.last_cpu_ms >= 0.0) {
            m_Probe.cpu_ms += cpu_now - m_Probe.last_cpu_ms;
        }
        m_Probe.composite_ms += composite_end - composite_start;
        m_Probe.swap_ms += swap_end - swap_start;
        if (finish_ms >= 0.0) {
            m_Probe.finish_ms += finish_ms;
            m_Probe.finish_samples++;
        }
        m_Probe.frames++;
    }

    m_Probe.primed = true;
    m_Probe.last_end_ms = swap_end;
    m_Probe.last_cpu_ms = cpu_now;

    if (m_Probe.frames >= M_PROBE_REPORT_FRAMES) {
        M_ProbeReport(p);
    }
}
#endif

static void M_Blit(const M_CONTEXT *const p, const TRX_GL_FBO *const fbo)
{
    TRX_GL_Texture_Bind(&fbo->texture);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    TRX_GL_CheckError();
}

static void M_BindQuadState(M_CONTEXT *const p)
{
#if !defined(TRX_TARGET_IOS)
    // GLES has no polygon mode concept -- GL_FILL is the only mode it
    // ever renders in, so there's nothing to (re)set here.
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
#endif
    TRX_GL_Program_Bind(&p->program);
    TRX_GL_Buffer_Bind(&p->buffer);
    TRX_GL_VertexArray_Bind(&p->vertex_array);
    TRX_GL_Sampler_Bind(&p->sampler, 0);
    glActiveTexture(GL_TEXTURE0);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    TRX_GL_CheckError();
}

// Multisampling and supersampling are alternatives, so a scene rasterized at
// more than the scene resolution is rasterized with one sample per pixel.
static int32_t M_GetGeometrySamples(const M_CONTEXT *const p)
{
    const VIEWPORT_RECT game = Viewport_GetRect(VIEWPORT_GAME);
    const VIEWPORT_RECT scene = Viewport_GetRect(VIEWPORT_SCENE);
    return game.width > scene.width ? 1 : p->config->multisampling_factor;
}

static void M_SizeGeometryFbo(M_CONTEXT *const p)
{
    const VIEWPORT_RECT game = Viewport_GetRect(VIEWPORT_GAME);
    const VIEWPORT_RECT scene = Viewport_GetRect(VIEWPORT_SCENE);
    TRX_GL_FBO_ResizeIfNeeded(
        &p->geometry_fbo, game.width, game.height, M_GetGeometrySamples(p));
    p->scene_width = scene.width;
    p->scene_height = scene.height;
}

// Bring the geometry framebuffer down to the scene resolution the composite
// pass expects: averaging the samples of a multisampled buffer, or box-
// filtering a supersampled one down to the pixel grid the player is meant to
// see. Returns the geometry framebuffer itself when neither is in play,
// leaving that path exactly as it was.
static const TRX_GL_FBO *M_ResolveScene(M_CONTEXT *const p)
{
    const int32_t width = p->scene_width;
    const int32_t height = p->scene_height;
    const int32_t factor = width > 0 ? p->geometry_fbo.width / width : 1;
    if (factor <= 1 && p->geometry_fbo.samples <= 1) {
        if (p->resolve_fbo.fbo != 0) {
            TRX_GL_FBO_Close(&p->resolve_fbo);
        }
        return &p->geometry_fbo;
    }

    if (p->scene_resolved) {
        return &p->resolve_fbo;
    }

    if (p->resolve_fbo.fbo == 0) {
        TRX_GL_FBO_Init(
            &p->resolve_fbo, width, height, 1, GL_RGBA8, GL_RGBA, false);
    } else {
        TRX_GL_FBO_ResizeIfNeeded(&p->resolve_fbo, width, height, 1);
    }

    if (p->geometry_fbo.samples > 1) {
        glBindFramebuffer(GL_READ_FRAMEBUFFER, p->geometry_fbo.fbo);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, p->resolve_fbo.fbo);
        glBlitFramebuffer(
            0, 0, width, height, 0, 0, width, height, GL_COLOR_BUFFER_BIT,
            GL_NEAREST);
        TRX_GL_CheckError();
    } else {
        M_BindQuadState(p);
        TRX_GL_FBO_Bind(&p->resolve_fbo);
        glViewport(0, 0, width, height);
        TRX_GL_Program_Uniform1i(&p->program, p->loc_supersample, factor);
        TRX_GL_Program_Uniform1i(
            &p->program, p->loc_dither_mode, DITHER_MODE_DISABLED);
        TRX_GL_Program_Uniform1i(&p->program, p->loc_palette_lut_size, 0);
        M_Blit(p, &p->geometry_fbo);
        TRX_GL_Program_Uniform1i(&p->program, p->loc_supersample, 1);
    }

    p->scene_resolved = true;
    return &p->resolve_fbo;
}

static void M_UpdateFBOSizes(TRX_GL_RENDERER *renderer)
{
    M_CONTEXT *const p = renderer->priv;

    M_SizeGeometryFbo(p);
    const VIEWPORT_RECT rect = Viewport_GetRect(VIEWPORT_UI);
    TRX_GL_FBO_ResizeIfNeeded(&p->ui_fbo, rect.width, rect.height, 1);
}

// Draws the scene and the UI over it into the given framebuffer. Resolving the
// scene binds framebuffers of its own, so the destination is bound here rather
// than by the caller.
static void M_Composite(
    M_CONTEXT *const p, const GLuint dst_fbo, const VIEWPORT_RECT rect)
{
    const GLuint filter = p->config->display_filter == TEXTURE_FILTER_BILINEAR
        ? GL_LINEAR
        : GL_NEAREST;

    const TRX_GL_FBO *const scene_fbo = M_ResolveScene(p);

    M_BindQuadState(p);
    glBindFramebuffer(GL_FRAMEBUFFER, dst_fbo);

    TRX_GL_Sampler_Parameteri(&p->sampler, GL_TEXTURE_MAG_FILTER, filter);
    TRX_GL_Sampler_Parameteri(&p->sampler, GL_TEXTURE_MIN_FILTER, filter);

    TRX_GL_Program_Uniform1i(
        &p->program, p->loc_dither_mode, p->config->dither_mode);
    TRX_GL_Program_Uniform1i(
        &p->program, p->loc_palette_lut_size, p->palette_lut_size);
    if (p->palette_lut_size > 0) {
        glActiveTexture(GL_TEXTURE1);
        TRX_GL_Texture_Bind(&p->palette_lut);
        glActiveTexture(GL_TEXTURE0);
    }

    glViewport(rect.x, rect.y, rect.width, rect.height);
    TRX_GL_CheckError();

    // Composite scene FBO (opaque)
    M_Blit(p, scene_fbo);

    // Composite UI FBO (with premultiplied alpha blending)
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    M_Blit(p, &p->ui_fbo);
    glDisable(GL_BLEND);
}

static void M_Render(TRX_GL_RENDERER *renderer)
{
    ASSERT(renderer != nullptr);
    M_CONTEXT *const p = renderer->priv;
    ASSERT(p != nullptr);

    // Clear every frame because resizing may give us dirty buffers.
    glBindFramebuffer(GL_FRAMEBUFFER, TRX_GL_Context_GetMainFramebuffer());
    TRX_GL_Context_SwitchToViewport(VIEWPORT_WINDOW);
    TRX_GL_Context_Clear();

    // TRX_GL_Context_GetMainFramebuffer() is 0 everywhere except iOS, where
    // there is no real default framebuffer 0: SDL creates an actual
    // framebuffer object for the window. Targeting literal 0 there leaves
    // the final composite pass drawing into a nonexistent framebuffer
    // (GL_INVALID_FRAMEBUFFER_OPERATION on every subsequent draw).
    M_Composite(
        p, TRX_GL_Context_GetMainFramebuffer(),
        Viewport_GetRect(VIEWPORT_TARGET));

    if (TRX_GL_Context_GetScheduledScreenshotPath() != nullptr) {
        TRX_GL_Context_SwitchToViewport(VIEWPORT_TARGET);
        SHOULD(TRX_GL_Screenshot_CaptureToFile(
            TRX_GL_Context_GetScheduledScreenshotPath()));
        TRX_GL_Context_ClearScheduledScreenshotPath();
    }
}

// The framebuffers keep the frame that was just presented until the next one
// draws over them, so that frame can be composited a second time for a
// snapshot. Output_BeginScene is what prepares them for the next frame.
static void M_SwapBuffers(TRX_GL_RENDERER *const renderer)
{
    M_CONTEXT *const p = renderer->priv;

#if TRX_PERF_PROBE
    const double probe_composite_start = M_ProbeNowMs();
#endif
    M_Render(renderer);
#if TRX_PERF_PROBE
    const double probe_composite_end = M_ProbeNowMs();
    double probe_finish_ms = -1.0;
    if (m_Probe.total_frames++ % M_PROBE_FINISH_EVERY == 0) {
        glFinish();
        probe_finish_ms = M_ProbeNowMs() - probe_composite_end;
    }
#endif
#if defined(TRX_TARGET_IOS)
    // Per Apple/SDL's iOS requirements (see SDL2/README-ios), the drawable
    // Renderbuffer must be bound to the GL_RENDERBUFFER binding point at
    // the moment SDL_GL_SwapWindow() is called: SDL's internal
    // -[EAGLContext presentRenderbuffer:] call presents whatever
    // renderbuffer object happens to already be bound there -- it does
    // not bind the correct one itself. Our own rendering exclusively uses
    // texture-attached FBOs (see gl/fbo.c), so we never otherwise touch
    // the GL_RENDERBUFFER binding point, and it's left stale/unbound,
    // causing SwapWindow to try to present an invalid renderbuffer every
    // frame (GL_INVALID_OPERATION).
    glBindRenderbuffer(
        GL_RENDERBUFFER, TRX_GL_Context_GetMainColorRenderbuffer());
#endif
#if TRX_PERF_PROBE
    const double probe_swap_start = M_ProbeNowMs();
#endif
    SDL_ClearError();
    SDL_GL_SwapWindow(TRX_GL_Context_GetWindowHandle());
#if TRX_PERF_PROBE
    M_ProbeFrameDone(
        p, probe_composite_start, probe_composite_end, probe_finish_ms,
        probe_swap_start);
#endif
#if defined(TRX_TARGET_IOS)
    // SDL_GL_SwapWindow() returns void but may internally call
    // SDL_SetError(); surface it directly instead of guessing.
    const char *const sdl_err = SDL_GetError();
    if (sdl_err != nullptr && sdl_err[0] != '\0') {
        LOG_ERROR("SDL_GL_SwapWindow: %s", sdl_err);
    }
#endif
    // Kept as a permanent check (not just leftover bisection debugging):
    // this is the exact checkpoint that caught the GL_INVALID_OPERATION
    // present-time bug, so it's worth continuing to catch regressions here.
    TRX_GL_CheckError();
    M_UpdateFBOSizes(renderer);

    TRX_GL_FBO_Bind(&p->geometry_fbo);
    TRX_GL_Context_SwitchToViewport(VIEWPORT_GAME);
}

static RESULT M_Init(
    TRX_GL_RENDERER *const renderer, const TRX_GL_CONFIG *const config)
{
    ASSERT(renderer != nullptr);
    renderer->priv = (M_CONTEXT *)Memory_Alloc(sizeof(M_CONTEXT));
    M_CONTEXT *const p = renderer->priv;
    ASSERT(p != nullptr);

    p->config = config;

#if !defined(TRX_TARGET_IOS)
    // GL_MULTISAMPLE doesn't exist in GLES -- multisample resolve is
    // implicit/always-on there when a multisample renderbuffer is used
    // (see gl/fbo.c, where multisampling is disabled entirely on iOS).
    glEnable(GL_MULTISAMPLE);
    TRX_GL_CheckError();
#endif

    TRX_GL_Buffer_Init(&p->buffer, GL_ARRAY_BUFFER);
    TRX_GL_Buffer_Bind(&p->buffer);
    const GLfloat verts[] = {
        0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f, 1.0f,
    };
    TRX_GL_Buffer_Data(&p->buffer, sizeof(verts), verts, GL_STATIC_DRAW);

    TRX_GL_VertexArray_Init(&p->vertex_array);
    TRX_GL_VertexArray_Bind(&p->vertex_array);
    TRX_GL_VertexArray_Attribute(
        &p->vertex_array, 0, 2, GL_FLOAT, GL_FALSE, 0, 0);

    TRX_GL_Sampler_Init(&p->sampler);
    TRX_GL_Sampler_Bind(&p->sampler, 0);
    TRX_GL_Sampler_Parameteri(&p->sampler, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    TRX_GL_Sampler_Parameteri(&p->sampler, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    TRX_GL_Sampler_Parameteri(&p->sampler, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    TRX_GL_Sampler_Parameteri(&p->sampler, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    MUST(TRX_GL_Program_Init(&p->program));
    MUST(TRX_GL_Program_AttachShader(
        &p->program, GL_VERTEX_SHADER, "fbo.glsl", nullptr));
    MUST(TRX_GL_Program_AttachShader(
        &p->program, GL_FRAGMENT_SHADER, "fbo.glsl", nullptr));
    TRX_GL_Program_FragmentData(&p->program, "outColor");
    MUST(TRX_GL_Program_Link(&p->program));
    TRX_GL_Program_Bind(&p->program);
    TRX_GL_Program_Uniform1i(
        &p->program, TRX_GL_Program_UniformLocation(&p->program, "uTex0"), 0);
    p->loc_dither_mode =
        TRX_GL_Program_UniformLocation(&p->program, "uDitherMode");
    p->loc_supersample =
        TRX_GL_Program_UniformLocation(&p->program, "uSupersample");
    TRX_GL_Program_Uniform1i(&p->program, p->loc_supersample, 1);
    TRX_GL_Program_Uniform1i(
        &p->program,
        TRX_GL_Program_UniformLocation(&p->program, "uTexPaletteLut"), 1);
    p->loc_palette_lut_size =
        TRX_GL_Program_UniformLocation(&p->program, "uPaletteLutSize");
    TRX_GL_Texture_Init(&p->palette_lut, GL_TEXTURE_3D);

    VIEWPORT_RECT rect;
    rect = Viewport_GetRect(VIEWPORT_GAME);
    TRX_GL_FBO_Init(
        &p->geometry_fbo, rect.width, rect.height, M_GetGeometrySamples(p),
        GL_RGBA8, GL_RGBA, true);
    p->scene_width = Viewport_GetWidth(VIEWPORT_SCENE);
    p->scene_height = Viewport_GetHeight(VIEWPORT_SCENE);

    rect = Viewport_GetRect(VIEWPORT_UI);
    TRX_GL_FBO_Init(
        &p->ui_fbo, rect.width, rect.height, 1, GL_RGBA8, GL_RGBA, false);
    return OK;
}

static void M_Shutdown(TRX_GL_RENDERER *renderer)
{
    LOG_INFO("");

    ASSERT(renderer != nullptr);
    M_CONTEXT *const p = renderer->priv;
    ASSERT(p != nullptr);

    TRX_GL_FBO_Close(&p->geometry_fbo);
    TRX_GL_FBO_Close(&p->ui_fbo);
    TRX_GL_FBO_Close(&p->resolve_fbo);
    if (p->composite_fbo != 0) {
        glDeleteFramebuffers(1, &p->composite_fbo);
        p->composite_fbo = 0;
    }
    TRX_GL_Texture_Close(&p->palette_lut);
    TRX_GL_Program_Close(&p->program);
    TRX_GL_Sampler_Close(&p->sampler);
    TRX_GL_Buffer_Close(&p->buffer);
    TRX_GL_VertexArray_Close(&p->vertex_array);

    Memory_FreePointer(&renderer->priv);
}

TRX_GL_RENDERER g_TRX_GL_Renderer = {
    .swap_buffers = &M_SwapBuffers,
    .init = &M_Init,
    .shutdown = &M_Shutdown,
};

void TRX_GL_Renderer_BindGeometryFbo(void)
{
    M_CONTEXT *const p = (M_CONTEXT *)g_TRX_GL_Renderer.priv;
    // Anything about to draw the scene again invalidates what was resolved
    // from it.
    TRX_GL_Renderer_InvalidateScene();
    TRX_GL_FBO_Bind(&p->geometry_fbo);
}

void TRX_GL_Renderer_InvalidateScene(void)
{
    M_CONTEXT *const p = (M_CONTEXT *)g_TRX_GL_Renderer.priv;
    p->scene_resolved = false;
}

void TRX_GL_Renderer_BindUiFbo(void)
{
    M_CONTEXT *const p = (M_CONTEXT *)g_TRX_GL_Renderer.priv;
    TRX_GL_FBO_Bind(&p->ui_fbo);
}

GLuint TRX_GL_Renderer_GetGeometryFboId(void)
{
    M_CONTEXT *const p = (M_CONTEXT *)g_TRX_GL_Renderer.priv;
    return p->geometry_fbo.fbo;
}

void TRX_GL_Renderer_SyncFboSizes(void)
{
    M_UpdateFBOSizes(&g_TRX_GL_Renderer);
}

void TRX_GL_Renderer_BlankWindow(void)
{
    M_CONTEXT *const p = (M_CONTEXT *)g_TRX_GL_Renderer.priv;
    SDL_Window *const window = TRX_GL_Context_GetWindowHandle();
    if (p == nullptr || window == nullptr) {
        return;
    }

    const int32_t swap_interval = SDL_GL_GetSwapInterval();
    SDL_GL_SetSwapInterval(0);
    for (int32_t i = 0; i < M_SWAPCHAIN_DEPTH; i++) {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        TRX_GL_Context_SwitchToViewport(VIEWPORT_WINDOW);
        TRX_GL_Context_Clear();
        SDL_GL_SwapWindow(window);
    }
    SDL_GL_SetSwapInterval(swap_interval);

    TRX_GL_FBO_Bind(&p->geometry_fbo);
    TRX_GL_Context_SwitchToViewport(VIEWPORT_GAME);
}

void TRX_GL_Renderer_CompositeToTexture(
    const TRX_GL_TEXTURE *const texture, const int32_t width,
    const int32_t height)
{
    M_CONTEXT *const p = (M_CONTEXT *)g_TRX_GL_Renderer.priv;
    if (texture == nullptr || !texture->initialized || width <= 0
        || height <= 0) {
        return;
    }

    if (p->composite_fbo == 0) {
        glGenFramebuffers(1, &p->composite_fbo);
    }

    GLint prev_fbo = 0;
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &prev_fbo);

    glBindFramebuffer(GL_FRAMEBUFFER, p->composite_fbo);
    glFramebufferTexture2D(
        GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture->id, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE) {
        M_Composite(
            p, p->composite_fbo,
            (VIEWPORT_RECT) { .width = width, .height = height });
    } else {
        LOG_ERROR("cannot draw into the given texture");
    }
    glFramebufferTexture2D(
        GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, 0, 0);

    glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)prev_fbo);
    TRX_GL_Context_SwitchToViewport(TRX_GL_Context_GetViewport());
    TRX_GL_CheckError();
}

GLuint TRX_GL_Renderer_ResolveSceneFbo(void)
{
    M_CONTEXT *const p = (M_CONTEXT *)g_TRX_GL_Renderer.priv;
    GLint prev_fbo = 0;
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &prev_fbo);
    const TRX_GL_FBO *const fbo = M_ResolveScene(p);
    glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)prev_fbo);
    TRX_GL_Context_SwitchToViewport(TRX_GL_Context_GetViewport());
    return fbo->fbo;
}

void TRX_GL_Renderer_SetPaletteLut(const void *const rgba, const int32_t size)
{
    M_CONTEXT *const p = (M_CONTEXT *)g_TRX_GL_Renderer.priv;
    if (p == nullptr) {
        return;
    }
    p->palette_lut_size = rgba == nullptr ? 0 : size;
    if (p->palette_lut_size > 0) {
        TRX_GL_Texture_Load3D(&p->palette_lut, rgba, size, GL_RGBA8, GL_RGBA);
    }
}
