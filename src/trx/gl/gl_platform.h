#pragma once


#include <trx/gl/probe.h>

// Single switch point between desktop OpenGL (via GLEW) and iOS OpenGLES.
// All engine code should include this header instead of <GL/glew.h> or
// <OpenGLES/ES3/gl.h> directly.

#if defined(TRX_TARGET_IOS)
    #include <OpenGLES/ES3/gl.h>
    #include <OpenGLES/ES3/glext.h>

    // GLES has no double-precision depth clear; alias to the float variant.
    #define glClearDepth(d) glClearDepthf((GLfloat)(d))

    // GLES only exposes the 4-component integer vertex attribute setter.
    // Per the GL spec, the 1-component form is equivalent to calling the
    // 4-component form with y=0, z=0, w=1.
    #define glVertexAttribI1ui(index, x) \
        glVertexAttribI4ui((index), (x), 0, 0, 1)

    // GLES has no glMapBuffer and therefore no legacy access-mode enum
    // either. These are fixed, stable Khronos registry values (used only
    // internally by TRX_GL_Buffer_Map's access translation, never passed
    // to a real GLES entry point), kept so portable call sites can stay
    // identical across platforms.
    #define GL_READ_ONLY  0x88B8
    #define GL_WRITE_ONLY 0x88B9
    #define GL_READ_WRITE 0x88BA

    // GLES has no single-target glDrawBuffer; callers should use
    // TRX_GL_DrawSingleColorBuffer() (see gl/utils.h) instead.

    // GLES has no glPolygonMode/wireframe fill mode. Wireframe rendering is
    // stubbed out on this platform (see gl/context.c).

    // GLES has no glDrawElementsBaseVertex. Callers must pre-offset indices
    // at upload time and use glDrawElements instead (see mesh_batcher).

    // Diagnostic only: with TRX_PERF_PROBE the draw calls and uploads of each
    // frame are counted and timed (see gl/probe.h), to tell a driver/CPU-bound
    // frame from a GPU-bound one. Each macro calls the real function of the
    // same name; a macro isn't expanded again inside its own body.
    #if defined(TRX_GL_PROBE_COUNTING)
        #define glDrawElements(mode, count, type, indices)                     \
            (g_TRX_GL_Probe.draws++,                                           \
             g_TRX_GL_Probe.draw_indices += (uint64_t)(count),                 \
             TRX_GL_PROBE_TIMED(                                               \
                 draw_ms, glDrawElements(mode, count, type, indices)))
        #define glDrawArrays(mode, first, count)                               \
            (g_TRX_GL_Probe.draws++,                                           \
             g_TRX_GL_Probe.draw_indices += (uint64_t)(count),                 \
             TRX_GL_PROBE_TIMED(draw_ms, glDrawArrays(mode, first, count)))
        #define glBufferData(target, size, data, usage)                        \
            (g_TRX_GL_Probe.buffer_uploads++,                                  \
             g_TRX_GL_Probe.buffer_bytes += (uint64_t)(size),                  \
             TRX_GL_PROBE_TIMED(                                               \
                 upload_ms, glBufferData(target, size, data, usage)))
        #define glBufferSubData(target, offset, size, data)                    \
            (g_TRX_GL_Probe.buffer_uploads++,                                  \
             g_TRX_GL_Probe.buffer_bytes += (uint64_t)(size),                  \
             TRX_GL_PROBE_TIMED(                                               \
                 upload_ms, glBufferSubData(target, offset, size, data)))
        #define glTexImage2D(                                                  \
            target, level, ifmt, width, height, border, fmt, type, px)         \
            (g_TRX_GL_Probe.tex_uploads++,                                     \
             g_TRX_GL_Probe.tex_pixels += (uint64_t)(width) * (height),        \
             TRX_GL_PROBE_TIMED(                                               \
                 upload_ms,                                                    \
                 glTexImage2D(                                                 \
                     target, level, ifmt, width, height, border, fmt, type,    \
                     px)))
        #define glTexSubImage2D(                                               \
            target, level, x, y, width, height, fmt, type, px)                 \
            (g_TRX_GL_Probe.tex_uploads++,                                     \
             g_TRX_GL_Probe.tex_pixels += (uint64_t)(width) * (height),        \
             TRX_GL_PROBE_TIMED(                                               \
                 upload_ms,                                                    \
                 glTexSubImage2D(                                              \
                     target, level, x, y, width, height, fmt, type, px)))
        #define glTexImage3D(                                                  \
            target, level, ifmt, width, height, depth, border, fmt, type, px)  \
            (g_TRX_GL_Probe.tex_uploads++,                                     \
             g_TRX_GL_Probe.tex_pixels +=                                      \
                 (uint64_t)(width) * (height) * (depth),                       \
             TRX_GL_PROBE_TIMED(                                               \
                 upload_ms,                                                    \
                 glTexImage3D(                                                 \
                     target, level, ifmt, width, height, depth, border, fmt,   \
                     type, px)))
        #define glTexSubImage3D(                                               \
            target, level, x, y, z, width, height, depth, fmt, type, px)       \
            (g_TRX_GL_Probe.tex_uploads++,                                     \
             g_TRX_GL_Probe.tex_pixels +=                                      \
                 (uint64_t)(width) * (height) * (depth),                       \
             TRX_GL_PROBE_TIMED(                                               \
                 upload_ms,                                                    \
                 glTexSubImage3D(                                              \
                     target, level, x, y, z, width, height, depth, fmt, type,  \
                     px)))
        // Mapping is where a driver waits for the GPU if the range is still in
        // use, so time it too. These return a value, hence the statement
        // expressions.
        #define glMapBufferRange(target, offset, length, access)               \
            ({                                                                 \
                const double trx_probe_t0_ = TRX_GL_PROBE_WALL_MS();           \
                void *const trx_probe_r_ =                                     \
                    glMapBufferRange(target, offset, length, access);          \
                g_TRX_GL_Probe.maps++;                                         \
                g_TRX_GL_Probe.map_ms +=                                       \
                    TRX_GL_PROBE_WALL_MS() - trx_probe_t0_;                    \
                trx_probe_r_;                                                  \
            })
        #define glUnmapBuffer(target)                                          \
            ({                                                                 \
                const double trx_probe_t0_ = TRX_GL_PROBE_WALL_MS();           \
                const GLboolean trx_probe_r_ = glUnmapBuffer(target);          \
                g_TRX_GL_Probe.map_ms +=                                       \
                    TRX_GL_PROBE_WALL_MS() - trx_probe_t0_;                    \
                trx_probe_r_;                                                  \
            })
    #endif
#else
    #include <GL/glew.h>
#endif
