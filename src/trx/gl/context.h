#pragma once

#include <trx/game/viewport.h>
#include <trx/gl/enum.h>
#include <trx/gl/gl_platform.h>
#include <trx/gl/renderer.h>
#ifdef __APPLE__
#include <TargetConditionals.h>
#if TARGET_OS_IPHONE
#include <OpenGLES/ES3/gl.h> 
#else
#include <OpenGL/gl3.h> 
#include <OpenGL/gl3ext.h>
#endif
#endif

#include <stdint.h>

bool TRX_GL_Context_Attach(void *window_handle);
void TRX_GL_Context_Detach(void);

void TRX_GL_Context_SetDisplayFilter(TEXTURE_FILTER filter);
bool TRX_GL_Context_GetWireframeMode(void);
void TRX_GL_Context_SetWireframeMode(bool enable);
void TRX_GL_Context_SetLineWidth(int32_t line_width);
void TRX_GL_Context_SetVSync(bool vsync);

void *TRX_GL_Context_GetWindowHandle(void);

// On iOS there is no real "framebuffer 0" -- SDL creates its own screen
// framebuffer with a driver-assigned id and keeps it bound as long as the
// app doesn't rebind away from it. This returns that id (captured once at
// context creation) so code that means "the screen" doesn't hardcode 0.
// On every other platform this simply returns 0.
GLuint TRX_GL_Context_GetMainFramebuffer(void);

// On iOS, SDL appears to defer the real layout/backing-store allocation
// of its screen framebuffer's renderbuffer(s) until the window actually
// becomes visible (hidden UIViews don't get a layoutSubviews pass). Call
// this once after the window-shown event to re-capture the main
// framebuffer id in case it changed; safe to call on every platform (a
// no-op off iOS).
void TRX_GL_Context_RefreshMainFramebuffer(void);

// The renderbuffer object (distinct from the FBO id above) that's bound
// as color attachment 0 of the main/drawable framebuffer. Per Apple/SDL's
// iOS requirements, this must be bound to the GL_RENDERBUFFER binding
// point at the moment SDL_GL_SwapWindow() is called -- SDL's own
// -[EAGLContext presentRenderbuffer:] call presents whatever happens to
// be bound there, it does not bind it itself. On every other platform
// this returns 0.
GLuint TRX_GL_Context_GetMainColorRenderbuffer(void);

void TRX_GL_Context_Clear(void);
void TRX_GL_Context_SwapBuffers(void);
void TRX_GL_Context_SetRendered(void);

void TRX_GL_Context_SwitchToViewport(VIEWPORT_SPACE space);

void TRX_GL_Context_ScheduleScreenshot(const char *path);
const char *TRX_GL_Context_GetScheduledScreenshotPath(void);
void TRX_GL_Context_ClearScheduledScreenshotPath(void);

TRX_GL_CONFIG *TRX_GL_Context_GetConfig(void);
