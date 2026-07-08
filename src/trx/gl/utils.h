#pragma once

#include <trx/core/log.h>
#include <trx/gl/track.h>

#include <trx/gl/gl_platform.h>

void TRX_GL_CheckError_Impl(const char *file, int line);
#define TRX_GL_CheckError() TRX_GL_CheckError_Impl(__FILE__, __LINE__)
const char *TRX_GL_GetErrorString(GLenum err);
