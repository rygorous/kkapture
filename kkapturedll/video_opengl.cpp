/* kkapture: intrusive demo video capturing.
 * Copyright (c) 2005-2010 Fabian "ryg/farbrausch" Giesen.
 *
 * This program is free software; you can redistribute and/or modify it under
 * the terms of the Artistic License, Version 2.0beta5, or (at your opinion)
 * any later version; all distributions of this program should contain this
 * license in a file named "LICENSE.txt".
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT UNLESS REQUIRED BY
 * LAW OR AGREED TO IN WRITING WILL ANY COPYRIGHT HOLDER OR CONTRIBUTOR
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY,
 * OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT
 * OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "stdafx.h"
#include "video.h"
#include "videoencoder.h"
#include "videocapturetimer.h"

#include <gl/gl.h>
#pragma comment(lib,"opengl32.lib")

// we use the swap interval extension to disable waiting for vblank (which
// only makes kkapturing slower) and the FBO extension to support correct
// kkapturing on demos that use them and perform SwapBuffers while a FBO
// is bound.

#define GL_FRAMEBUFFER_EXT                     0x8D40
#define GL_FRAMEBUFFER_BINDING_EXT             0x8CA6

typedef BOOL (__stdcall *PWGLSWAPINTERVALEXT)(int interval);
typedef int (_stdcall *PWGLGETSWAPINTERVALEXT)();
typedef void (__stdcall *PGLBINDFRAMEBUFFEREXT)(GLenum target,GLuint renderbuffer);

static PWGLSWAPINTERVALEXT wglSwapIntervalEXT = 0;
static PWGLGETSWAPINTERVALEXT wglGetSwapIntervalEXT = 0;
static PGLBINDFRAMEBUFFEREXT glBindFramebufferEXT = 0;
static bool swapIntervalChecked = false;
static bool fboChecked = false;
static GLenum useReadBuffer = GL_FRONT;

// we keep track of active HDC/HGLRC pairs so kkapture can map HDCs passed to
// SwapBuffers to rendering contexts (kkapture may need to temporarily switch
// rendering context to perform readback).
stdext::hash_map<HDC,HGLRC> rcFromDC;

// ---- gl framegrabbing code

static void captureGLFrame()
{
  HDC hdc = wglGetCurrentDC();
  HWND wnd = WindowFromDC(hdc);

  videoNeedEncoder();

  if(wnd)
  {
    RECT rc;
    GetClientRect(wnd,&rc);
    setCaptureResolution(rc.right-rc.left,rc.bottom-rc.top);
  }

  VideoCaptureDataLock lock;

  if(captureData && encoder && params.CaptureVideo)
  {
    // use immediate blits if possible
    if(wglGetSwapIntervalEXT && wglSwapIntervalEXT && wglGetSwapIntervalEXT() > 0)
      wglSwapIntervalEXT(0);

    // prepare for readback
    GLuint oldFrameBuffer;
    GLenum oldReadBuffer;

    if(glBindFramebufferEXT)
    {
      glGetIntegerv(GL_FRAMEBUFFER_BINDING_EXT,(GLint *) &oldFrameBuffer);
      glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,0);
    }
    
    glGetIntegerv(GL_READ_BUFFER,(GLint *) &oldReadBuffer);
    glReadBuffer(useReadBuffer);

    // actual readback
    glReadPixels(0,0,captureWidth,captureHeight,GL_BGR_EXT,GL_UNSIGNED_BYTE,captureData);

    // restore bindings
    if(glBindFramebufferEXT)
      glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,oldFrameBuffer);
    glReadBuffer(oldReadBuffer);

    // encode
    encoder->WriteFrame(captureData);
  }
}

// trampolines

// newer wingdi.h versions don't declare this anymore
extern "C" BOOL __stdcall wglSwapBuffers(HDC hdc);

static LONG (__stdcall *Real_ChangeDisplaySettings)(LPDEVMODE lpDevMode,DWORD dwFlags) = ChangeDisplaySettingsA;
static LONG (__stdcall *Real_ChangeDisplaySettingsEx)(LPCTSTR lpszDeviceName,LPDEVMODE lpDevMode,HWND hwnd,DWORD dwflags,LPVOID lParam) = ChangeDisplaySettingsExA;
static HGLRC (__stdcall *Real_wglCreateContext)(HDC hdc) = wglCreateContext;
static HGLRC (__stdcall *Real_wglCreateLayerContext)(HDC hdc,int iLayerPlane) = wglCreateLayerContext;
static BOOL (__stdcall *Real_wglMakeCurrent)(HDC hdc,HGLRC hglrc) = wglMakeCurrent;
static BOOL (__stdcall *Real_wglSwapBuffers)(HDC hdc) = wglSwapBuffers;
static BOOL (__stdcall *Real_wglSwapLayerBuffers)(HDC hdc,UINT fuPlanes) = wglSwapLayerBuffers;

BOOL __stdcall Mine_wglMakeCurrent(HDC hdc,HGLRC hglrc);

// context handling

static void prepareSwapOnDC(HDC hdc,HDC &oldDC,HGLRC &oldRC)
{
  oldDC = wglGetCurrentDC();
  oldRC = wglGetCurrentContext();

  if(oldDC != hdc)
  {
    HGLRC rc = rcFromDC[hdc];
    if(rc)
      Mine_wglMakeCurrent(hdc,rc);
    else
      printLog("video/opengl: SwapBuffers called on DC with no active rendering context. This is potentially bad.\n");
  }
}

static void finishSwapOnDC(HDC hdc,const HDC &oldDC,const HGLRC &oldRC)
{
  if(oldDC != hdc)
    Mine_wglMakeCurrent(oldDC,oldRC);
}

// ---- hooked API functions follow

static LONG __stdcall Mine_ChangeDisplaySettingsEx(LPCTSTR lpszDeviceName,LPDEVMODE lpDevMode,HWND hwnd,DWORD dwflags,LPVOID lParam)
{
  if (params.ExtraScreenMode) {
    setCaptureResolution(params.ExtraScreenWidth, params.ExtraScreenHeight);
    return DISP_CHANGE_SUCCESSFUL;
  } else {
    LONG result = Real_ChangeDisplaySettingsEx(lpszDeviceName,lpDevMode,hwnd,dwflags,lParam);
    if(result == DISP_CHANGE_SUCCESSFUL && lpDevMode &&
      (lpDevMode->dmFields & (DM_PELSWIDTH | DM_PELSHEIGHT)) == (DM_PELSWIDTH | DM_PELSHEIGHT))
      setCaptureResolution(lpDevMode->dmPelsWidth,lpDevMode->dmPelsHeight);
    return result;
  }
}

static HGLRC __stdcall Mine_wglCreateContext(HDC hdc)
{
  HGLRC result = Real_wglCreateContext(hdc);
  if(result)
    graphicsInitTiming();

  return result;
}

static HGLRC __stdcall Mine_wglCreateLayerContext(HDC hdc,int iLayerPlane)
{
  HGLRC result = Real_wglCreateLayerContext(hdc,iLayerPlane);
  if(result)
    graphicsInitTiming();

  return result;
}

static BOOL __stdcall Mine_wglMakeCurrent(HDC hdc,HGLRC hglrc)
{
  BOOL result = Real_wglMakeCurrent(hdc,hglrc);

  if(result)
  {
    rcFromDC[hdc] = hglrc;

    // determine read buffer to use
    PIXELFORMATDESCRIPTOR pfd;
    if(DescribePixelFormat(hdc,GetPixelFormat(hdc),sizeof(pfd),&pfd))
      useReadBuffer = (pfd.dwFlags & PFD_DOUBLEBUFFER) ? GL_BACK : GL_FRONT;

    // determine whether swap interval extension is supported
    if(!swapIntervalChecked && (!wglSwapIntervalEXT || !wglGetSwapIntervalEXT))
    {
      wglSwapIntervalEXT = (PWGLSWAPINTERVALEXT) wglGetProcAddress("wglSwapIntervalEXT");
      wglGetSwapIntervalEXT = (PWGLGETSWAPINTERVALEXT) wglGetProcAddress("wglGetSwapIntervalEXT");
      if(wglSwapIntervalEXT)
        printLog("video/opengl: wglSwapIntervalEXT supported\n");

      swapIntervalChecked = true;
    }

    // determine whether framebuffer objects are supported
    if(!fboChecked && !glBindFramebufferEXT)
    {
      glBindFramebufferEXT = (PGLBINDFRAMEBUFFEREXT) wglGetProcAddress("glBindFramebufferEXT");
      if(glBindFramebufferEXT)
        printLog("video/opengl: FBOs supported\n");

      fboChecked = true;
    }
  }

  return result;
}

static BOOL __stdcall Mine_wglSwapBuffers(HDC hdc)
{
  HDC olddc;
  HGLRC oldrc;

  prepareSwapOnDC(hdc,olddc,oldrc);
  captureGLFrame();
  nextFrame();
  finishSwapOnDC(hdc,olddc,oldrc);

  return Real_wglSwapBuffers(hdc);
}

static BOOL __stdcall Mine_wglSwapLayerBuffers(HDC hdc,UINT fuPlanes)
{
  if(fuPlanes & WGL_SWAP_MAIN_PLANE)
  {
    HDC olddc;
    HGLRC oldrc;

    prepareSwapOnDC(hdc,olddc,oldrc);
    captureGLFrame();
    nextFrame();
    finishSwapOnDC(hdc,olddc,oldrc);
  }

  return Real_wglSwapLayerBuffers(hdc,fuPlanes);
}

void initVideo_OpenGL()
{
  HookFunction(&Real_ChangeDisplaySettingsEx,Mine_ChangeDisplaySettingsEx);
  HookFunction(&Real_wglCreateContext,Mine_wglCreateContext);
  HookFunction(&Real_wglCreateLayerContext,Mine_wglCreateLayerContext);
  HookFunction(&Real_wglMakeCurrent,Mine_wglMakeCurrent);
  HookFunction(&Real_wglSwapBuffers,Mine_wglSwapBuffers);
  HookFunction(&Real_wglSwapLayerBuffers,Mine_wglSwapLayerBuffers);
}