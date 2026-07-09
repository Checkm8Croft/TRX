#pragma once

#include <trx/core/log.h>
#include <trx/gl/track.h>
#ifdef __APPLE__
#include <TargetConditionals.h>
#if TARGET_OS_IPHONE
#include <OpenGLES/ES3/gl.h> 
#else
#include <OpenGL/gl3.h> 
#include <OpenGL/gl3ext.h>
#endif
#endif
#include <trx/gl/gl_platform.h>

void TRX_GL_CheckError_Impl(const char *file, int line);
#define TRX_GL_CheckError() TRX_GL_CheckError_Impl(__FILE__, __LINE__)
const char *TRX_GL_GetErrorString(GLenum err);
