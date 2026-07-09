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
} TRX_GL_BUFFER;

void TRX_GL_Buffer_Init(TRX_GL_BUFFER *buf, GLenum target);
void TRX_GL_Buffer_Close(TRX_GL_BUFFER *buf);

void TRX_GL_Buffer_Bind(TRX_GL_BUFFER *buf);
void TRX_GL_Buffer_Data(
    TRX_GL_BUFFER *buf, GLsizei size, const void *data, GLenum usage);
void TRX_GL_Buffer_SubData(
    TRX_GL_BUFFER *buf, GLsizei offset, GLsizei size, const void *data);
void *TRX_GL_Buffer_Map(TRX_GL_BUFFER *buf, GLenum access);
void TRX_GL_Buffer_Unmap(TRX_GL_BUFFER *buf);
GLint TRX_GL_Buffer_Parameter(TRX_GL_BUFFER *buf, GLenum pname);
