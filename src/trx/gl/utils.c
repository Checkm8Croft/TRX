#include <trx/gl/utils.h>

#include <trx/gl/gl_platform.h>

#include <string.h>

// GLES has no fixed-function matrix stacks, so its headers don't define
// these error codes, but TRX_GL_GetErrorString() still names them.
#ifndef GL_STACK_UNDERFLOW
#define GL_STACK_UNDERFLOW 0x0504
#endif

#ifndef GL_STACK_OVERFLOW
#define GL_STACK_OVERFLOW 0x0503
#endif

const char *TRX_GL_GetErrorString(GLenum err)
{
    switch (err) {
    case GL_NO_ERROR:
        return "GL_NO_ERROR";
    case GL_INVALID_ENUM:
        return "GL_INVALID_ENUM";
    case GL_INVALID_VALUE:
        return "GL_INVALID_VALUE";
    case GL_INVALID_OPERATION:
        return "GL_INVALID_OPERATION";
    case GL_INVALID_FRAMEBUFFER_OPERATION:
        return "GL_INVALID_FRAMEBUFFER_OPERATION";
    case GL_OUT_OF_MEMORY:
        return "GL_OUT_OF_MEMORY";
    case GL_STACK_UNDERFLOW:
        return "GL_STACK_UNDERFLOW";
    case GL_STACK_OVERFLOW:
        return "GL_STACK_OVERFLOW";
    default:
        return "UNKNOWN";
    }
}

#if defined(TRX_GL_PROBE_COUNTING)
TRX_GL_PROBE_COUNTERS g_TRX_GL_Probe;

// Where the thread blocks. Between two consecutive TRX_GL_CheckError() calls
// the wall time minus the CPU time is time spent waiting, most likely on the
// driver and so the GPU. It is charged to the pair of call sites, which
// brackets the code that waited even when that code makes no check of its
// own.
    #define M_SITE_MAX 192
    #define M_SITE_MIN_MS 0.3
    #define M_SITE_LOGGED 6

typedef struct {
    const char *from_file;
    int32_t from_line;
    const char *file;
    int32_t line;
    double blocked_ms;
    uint32_t hits;
} M_SITE;

static M_SITE m_Sites[M_SITE_MAX];
static int32_t m_SiteCount;
static const char *m_PrevFile;
static int32_t m_PrevLine;
static double m_PrevWall;
static double m_PrevCpu;

static void M_ProbeSiteEnter(const char *const file, const int32_t line)
{
    const double wall = TRX_GL_PROBE_WALL_MS();
    const double cpu = TRX_GL_PROBE_CPU_MS();
    if (m_PrevFile != nullptr) {
        const double blocked = (wall - m_PrevWall) - (cpu - m_PrevCpu);
        if (blocked >= M_SITE_MIN_MS) {
            M_SITE *site = nullptr;
            for (int32_t i = 0; i < m_SiteCount; i++) {
                M_SITE *const s = &m_Sites[i];
                if (s->line == line && s->from_line == m_PrevLine
                    && s->file == file && s->from_file == m_PrevFile) {
                    site = s;
                    break;
                }
            }
            if (site == nullptr && m_SiteCount < M_SITE_MAX) {
                site = &m_Sites[m_SiteCount++];
                *site = (M_SITE) {
                    .from_file = m_PrevFile,
                    .from_line = m_PrevLine,
                    .file = file,
                    .line = line,
                };
            }
            if (site != nullptr) {
                site->blocked_ms += blocked;
                site->hits++;
            }
        }
    }
}

static void M_ProbeSiteExit(const char *const file, const int32_t line)
{
    m_PrevFile = file;
    m_PrevLine = line;
    m_PrevWall = TRX_GL_PROBE_WALL_MS();
    m_PrevCpu = TRX_GL_PROBE_CPU_MS();
}

static const char *M_BaseName(const char *const path)
{
    const char *const slash = strrchr(path, '/');
    return slash != nullptr ? slash + 1 : path;
}

void TRX_GL_Probe_LogBlockedSites(const double frames)
{
    for (int32_t rank = 0; rank < M_SITE_LOGGED; rank++) {
        M_SITE *best = nullptr;
        for (int32_t i = 0; i < m_SiteCount; i++) {
            if (m_Sites[i].blocked_ms > 0.0
                && (best == nullptr
                    || m_Sites[i].blocked_ms > best->blocked_ms)) {
                best = &m_Sites[i];
            }
        }
        if (best == nullptr) {
            break;
        }
        LOG_WARNING(
            "PERF-BLOCK %.2f ms/frame (%.2f hits/frame) between %s:%d and "
            "%s:%d",
            best->blocked_ms / frames, (double)best->hits / frames,
            M_BaseName(best->from_file), best->from_line,
            M_BaseName(best->file), best->line);
        best->blocked_ms = 0.0;
    }
    m_SiteCount = 0;
}
#endif

void TRX_GL_CheckErrorAt(
    const char *const file, const int line, const char *const func)
{
#if defined(TRX_GL_PROBE_COUNTING)
    // Nearly every GL call is followed by one of these, so it doubles as a
    // count of the GL calls made.
    g_TRX_GL_Probe.gl_calls++;
    M_ProbeSiteEnter(file, line);
#endif
    for (GLenum err; (err = glGetError()) != GL_NO_ERROR;) {
        Log_Message(
            LOG_LEVEL_ERROR, file, line, func, "glGetError: (%s)",
            TRX_GL_GetErrorString(err));
    }
#if defined(TRX_GL_PROBE_COUNTING)
    M_ProbeSiteExit(file, line);
#endif
}
