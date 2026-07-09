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


typedef struct OUTPUT_SHADER OUTPUT_SHADER;

OUTPUT_SHADER *Output_Shader_Create(const char *path);
void Output_Shader_Free(OUTPUT_SHADER *shader);
void Output_Shader_Bind(const OUTPUT_SHADER *shader);

GLint Output_Shader_LookupUniform(
    const OUTPUT_SHADER *shader, const char *name);

bool Output_Shader_TryLookupUniform(
    const OUTPUT_SHADER *shader, const char *name, GLint *out_location);
