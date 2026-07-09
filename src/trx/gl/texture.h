#pragma once

#include <trx/gl/gl_platform.h>
#ifdef __APPLE__
#include <TargetConditionals.h>
#if TARGET_OS_IPHONE
#include <OpenGLES/ES3/gl.h> 
#else
#include <OpenGL/gl3.h> 
#include <OpenGL/gl3ext.h>
#endif
#endif
typedef struct {
    bool initialized;
    GLuint id;
    GLenum target;
} TRX_GL_TEXTURE;

TRX_GL_TEXTURE *TRX_GL_Texture_Create(GLenum target);
void TRX_GL_Texture_Free(TRX_GL_TEXTURE *texture);

void TRX_GL_Texture_Init(TRX_GL_TEXTURE *texture, GLenum target);
void TRX_GL_Texture_Close(TRX_GL_TEXTURE *texture);
void TRX_GL_Texture_Bind(const TRX_GL_TEXTURE *texture);
void TRX_GL_Texture_Load(
    TRX_GL_TEXTURE *texture, const void *data, int width, int height,
    GLint internal_format, GLint format);
void TRX_GL_Texture_LoadFromBackBuffer(TRX_GL_TEXTURE *texture);
