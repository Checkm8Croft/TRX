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
} TRX_GL_SAMPLER;

void TRX_GL_Sampler_Init(TRX_GL_SAMPLER *sampler);
void TRX_GL_Sampler_Close(TRX_GL_SAMPLER *sampler);

void TRX_GL_Sampler_Bind(TRX_GL_SAMPLER *sampler, GLuint unit);
void TRX_GL_Sampler_Parameteri(
    TRX_GL_SAMPLER *sampler, GLenum pname, GLint param);
void TRX_GL_Sampler_Parameterf(
    TRX_GL_SAMPLER *sampler, GLenum pname, GLfloat param);
