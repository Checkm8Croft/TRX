#pragma once

// Diagnostic only: set TRX_PERF_PROBE to 1 (or pass -DTRX_PERF_PROBE=1) to log
// a breakdown of where the frames go (see gl/renderer.c). Off by default, and
// then none of this exists.
#ifndef TRX_PERF_PROBE
    #define TRX_PERF_PROBE 0
#endif

// The counters and timers are only built for iOS, where GLEW doesn't already
// wrap the GL functions that gl_platform.h wraps for counting.
#if TRX_PERF_PROBE && defined(TRX_TARGET_IOS)
    #define TRX_GL_PROBE_COUNTING 1

    #include <stdint.h>
    #include <time.h>

enum {
    TRX_PROBE_PHASE_WAIT, // Clock_WaitTick(): the pacing sleep
    TRX_PROBE_PHASE_CONTROL, // game logic and event pumping
    TRX_PROBE_PHASE_DRAW, // building and submitting the scene
    TRX_PROBE_PHASE_FLIP, // glFlush, composite and present
    TRX_PROBE_PHASE_COUNT,
};

typedef struct {
    uint32_t draws;
    uint64_t draw_indices;
    uint32_t buffer_uploads;
    uint64_t buffer_bytes;
    uint32_t tex_uploads;
    uint64_t tex_pixels;
    uint32_t gl_calls;
    // Wall time spent inside the GL upload and draw calls themselves: a call
    // that has to wait for the GPU shows up here and not as CPU time.
    double t0;
    double upload_ms;
    double draw_ms;
    uint32_t maps;
    double map_ms;
    double phase_wall_ms[TRX_PROBE_PHASE_COUNT];
    double phase_cpu_ms[TRX_PROBE_PHASE_COUNT];
} TRX_GL_PROBE_COUNTERS;

extern TRX_GL_PROBE_COUNTERS g_TRX_GL_Probe;

// Logs the places in the GL call sequence where the thread spent the most
// time blocked (wall time minus CPU time) since the last call, then resets.
void TRX_GL_Probe_LogBlockedSites(double frames);

static inline double TRX_GL_Probe_ClockMs(const clockid_t id)
{
    struct timespec ts;
    clock_gettime(id, &ts);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1.0e6;
}

    #define TRX_GL_PROBE_WALL_MS() TRX_GL_Probe_ClockMs(CLOCK_MONOTONIC_RAW)
    // CPU time the calling thread actually burnt; time asleep or blocked
    // doesn't count.
    #define TRX_GL_PROBE_CPU_MS() TRX_GL_Probe_ClockMs(CLOCK_THREAD_CPUTIME_ID)

    // Brackets a stretch of the frame loop. BEGIN declares two locals, so it
    // goes where a declaration is allowed, and each phase once per scope.
    #define TRX_PROBE_BEGIN(phase)                                             \
        const double probe_wall_##phase = TRX_GL_PROBE_WALL_MS();              \
        const double probe_cpu_##phase = TRX_GL_PROBE_CPU_MS()
    #define TRX_PROBE_END(phase)                                               \
        (g_TRX_GL_Probe.phase_wall_ms[TRX_PROBE_PHASE_##phase] +=              \
         TRX_GL_PROBE_WALL_MS() - probe_wall_##phase,                          \
         g_TRX_GL_Probe.phase_cpu_ms[TRX_PROBE_PHASE_##phase] +=               \
         TRX_GL_PROBE_CPU_MS() - probe_cpu_##phase)

    // Runs a GL call and adds its wall time to the given counter field.
    #define TRX_GL_PROBE_TIMED(field, call)                                    \
        (g_TRX_GL_Probe.t0 = TRX_GL_PROBE_WALL_MS(), call,                     \
         g_TRX_GL_Probe.field += TRX_GL_PROBE_WALL_MS() - g_TRX_GL_Probe.t0)
#else
    #define TRX_PROBE_BEGIN(phase) ((void)0)
    #define TRX_PROBE_END(phase) ((void)0)
#endif
