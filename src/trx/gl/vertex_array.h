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
} TRX_GL_VERTEX_ARRAY;

void TRX_GL_VertexArray_Init(TRX_GL_VERTEX_ARRAY *array);
void TRX_GL_VertexArray_Close(TRX_GL_VERTEX_ARRAY *array);
void TRX_GL_VertexArray_Bind(TRX_GL_VERTEX_ARRAY *array);
void TRX_GL_VertexArray_Attribute(
    TRX_GL_VERTEX_ARRAY *array, GLuint index, GLint size, GLenum type,
    GLboolean normalized, GLsizei stride, GLsizei offset);
void TRX_GL_VertexArray_IAttribute(
    TRX_GL_VERTEX_ARRAY *array, GLuint index, GLint size, GLenum type,
    GLsizei stride, GLsizei offset);
