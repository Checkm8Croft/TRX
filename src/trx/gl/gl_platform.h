#pragma once

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
#else
    #include <trx/gl/gl_platform.h>
#endif
