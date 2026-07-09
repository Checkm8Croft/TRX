#pragma once

#include <trx/gl/gl_platform.h>
#include <stdint.h>
#ifdef __APPLE__
#include <TargetConditionals.h>
#if TARGET_OS_IPHONE
#include <OpenGLES/ES3/gl.h> 
#else
#include <OpenGL/gl3.h> 
#include <OpenGL/gl3ext.h>
#endif
#endif

bool TRX_GL_Screenshot_CaptureToFile(const char *path);

void TRX_GL_Screenshot_CaptureToBuffer(
    uint8_t *out_buffer, GLint *out_width, GLint *out_height, GLint depth,
    GLenum format, GLenum type, bool vflip);
