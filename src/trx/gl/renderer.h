#pragma once

#include <trx/gl/config.h>
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

typedef struct TRX_GL_Renderer {
    void (*init)(struct TRX_GL_Renderer *renderer, const TRX_GL_CONFIG *config);
    void (*shutdown)(struct TRX_GL_Renderer *renderer);
    void (*swap_buffers)(struct TRX_GL_Renderer *renderer);
    void *priv;
} TRX_GL_RENDERER;

extern TRX_GL_RENDERER g_TRX_GL_Renderer;

// Bind the geometry framebuffer for rendering the 3D scene.
void TRX_GL_Renderer_BindGeometryFbo(void);

// Bind the UI framebuffer for rendering the UI overlay.
void TRX_GL_Renderer_BindUiFbo(void);

// Get the GL object id of the geometry framebuffer (3D scene only, no UI).
GLuint TRX_GL_Renderer_GetGeometryFboId(void);
