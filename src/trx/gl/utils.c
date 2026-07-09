#include <trx/gl/utils.h>

#include <trx/gl/gl_platform.h>

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

void TRX_GL_CheckErrorAt(
    const char *const file, const int line, const char *const func)
{
    for (GLenum err; (err = glGetError()) != GL_NO_ERROR;) {
        Log_Message(
            LOG_LEVEL_ERROR, file, line, func, "glGetError: (%s)",
            TRX_GL_GetErrorString(err));
    }
}
