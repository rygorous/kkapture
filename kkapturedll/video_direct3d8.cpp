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

#include "d3d8.h"
#pragma comment(lib,"d3d8.lib")

DETOUR_TRAMPOLINE(IDirect3D8 * __stdcall Real_Direct3DCreate8(UINT SDKVersion), Direct3DCreate8);

typedef HRESULT (__stdcall *PD3D8_CreateDevice)(IDirect3D8 *d3d,UINT a0,UINT a1,DWORD a2,DWORD a3,D3DPRESENT_PARAMETERS *a4,IDirect3DDevice8 **a5);
typedef ULONG (__stdcall *PD3DDevice8_AddRef)(IDirect3DDevice8 *dev);
typedef ULONG (__stdcall *PD3DDevice8_Release)(IDirect3DDevice8 *dev);
typedef HRESULT (__stdcall *PD3DDevice8_Reset)(IDirect3DDevice8 *dev,D3DPRESENT_PARAMETERS *pp);
typedef HRESULT (__stdcall *PD3DDevice8_Present)(IDirect3DDevice8 *dev,DWORD a0,DWORD a1,DWORD a2,DWORD a3);

static PD3D8_CreateDevice Real_D3D8_CreateDevice = 0;
static PD3DDevice8_AddRef Real_D3DDevice8_AddRef = 0;
static PD3DDevice8_Release Real_D3DDevice8_Release = 0;
static PD3DDevice8_Reset Real_D3DDevice8_Reset = 0;
static PD3DDevice8_Present Real_D3DDevice8_Present = 0;

static IDirect3DSurface8 *captureSurf = 0;
static ULONG deviceRefCount = 0;
static bool firstCreate;

static void fixPresentParameters(D3DPRESENT_PARAMETERS *pp)
{
  pp->BackBufferCount = 1;
  if(pp->MultiSampleType == D3DMULTISAMPLE_NONE)
    pp->SwapEffect = D3DSWAPEFFECT_COPY;

  // force back buffer format to something we can read
  D3DFORMAT fmt = pp->BackBufferFormat;
  if(fmt == D3DFMT_A1R5G5B5 || fmt == D3DFMT_A8R8G8B8)
    pp->BackBufferFormat = D3DFMT_A8R8G8B8;
  else
    pp->BackBufferFormat = D3DFMT_X8R8G8B8;
  
  pp->FullScreen_PresentationInterval = 0;
}

static void getBackBufferSize(D3DPRESENT_PARAMETERS *pp,HWND hFocusWindow,int &width,int &height)
{
  HWND hWnd = pp->hDeviceWindow;
  if(!hWnd)
    hWnd = hFocusWindow;

  RECT rc;
  GetClientRect(hWnd,&rc);

  width = pp->BackBufferWidth ? pp->BackBufferWidth : rc.right - rc.left;
  height = pp->BackBufferHeight ? pp->BackBufferHeight : rc.bottom - rc.top;
}

static bool captureD3DFrame8(IDirect3DDevice8 *dev)
{
  if(!captureSurf)
    return false;

  videoNeedEncoder();

  IDirect3DSurface8 *back = 0;
  bool error = true;

  VideoCaptureDataLock lock;

  dev->GetBackBuffer(0,D3DBACKBUFFER_TYPE_MONO,&back);
  if(back)
  {
    if(SUCCEEDED(dev->CopyRects(back,0,0,captureSurf,0)))
    {
      D3DLOCKED_RECT lr;

      if(SUCCEEDED(captureSurf->LockRect(&lr,0,D3DLOCK_READONLY)))
      {
        blitAndFlipBGRAToCaptureData((unsigned char *) lr.pBits,lr.Pitch);
        captureSurf->UnlockRect();
        error = false;
      }
    }

    back->Release();
  }

  if(!error)
    encoder->WriteFrame(captureData);

  return !error;
}

static ULONG __stdcall Mine_D3DDevice8_AddRef(IDirect3DDevice8 *dev)
{
  Real_D3DDevice8_AddRef(dev);
  return ++deviceRefCount;
}

static ULONG __stdcall Mine_D3DDevice8_Release(IDirect3DDevice8 *dev)
{
  static bool inRelease = false;
  
  if(deviceRefCount && !inRelease)
  {
    if(--deviceRefCount == 0) // our surface is the last one
    {
      printLog("video/d3d8: Releasing...\n");
      inRelease = true;

      if(captureSurf)
      {
        captureSurf->Release();
        captureSurf = 0;
      }

      inRelease = false;
    }

    ULONG ref = Real_D3DDevice8_Release(dev);
    if(deviceRefCount)
      ref = deviceRefCount;

    return ref;
  }
  else
    return Real_D3DDevice8_Release(dev);
}

static HRESULT __stdcall Mine_D3DDevice8_Reset(IDirect3DDevice8 *dev,D3DPRESENT_PARAMETERS *pp)
{
  if(pp)
    fixPresentParameters(pp);

  deviceRefCount += 512;

  if(captureSurf)
  {
    captureSurf->Release();
    captureSurf = 0;
  }

  HRESULT hr = Real_D3DDevice8_Reset(dev,pp);
  if(SUCCEEDED(hr))
  {
    printLog("video/d3d8: Reset successful.\n");

    D3DDEVICE_CREATION_PARAMETERS param;
    dev->GetCreationParameters(&param);

    int width,height;
    getBackBufferSize(pp,param.hFocusWindow,width,height);
    
    setCaptureResolution(width,height);
    if(FAILED(dev->CreateImageSurface(captureWidth,captureHeight,pp->BackBufferFormat,&captureSurf)))
      printLog("video/d3d8: couldn't create capture surface (%d,%d,%d).\n",captureWidth,captureHeight,pp->BackBufferFormat);

    deviceRefCount -= 512;
    videoStartNextPart();
  }
  else
    deviceRefCount -= 512;

  return hr;
}

static HRESULT __stdcall Mine_D3DDevice8_Present(IDirect3DDevice8 *dev,DWORD a0,DWORD a1,DWORD a2,DWORD a3)
{
  HRESULT hr = Real_D3DDevice8_Present(dev,a0,a1,a2,a3);

  if(params.CaptureVideo)
  {
    if(!captureD3DFrame8(dev))
      printLog("video/d3d8: Frame capture failed! (frame %d)\n",getFrameTiming());
  }

  nextFrame();
  return hr;
}

static HRESULT __stdcall Mine_D3D8_CreateDevice(IDirect3D8 *d3d,UINT a0,UINT a1,
  DWORD a2,DWORD a3,D3DPRESENT_PARAMETERS *a4,IDirect3DDevice8 **a5)
{
  if(a4)
    fixPresentParameters(a4);

  HRESULT hr = Real_D3D8_CreateDevice(d3d,a0,a1,a2,a3,a4,a5);

  if(SUCCEEDED(hr) && *a5)
  {
    IDirect3DDevice8 *dev = *a5;

    printLog("video/d3d8: CreateDevice successful.\n");

    if(!firstCreate)
      videoStartNextPart();
    else
      firstCreate = false;

    int width,height;
    getBackBufferSize(a4,(HWND) a2,width,height);

    setCaptureResolution(width,height);
    if(FAILED(dev->CreateImageSurface(captureWidth,captureHeight,a4->BackBufferFormat,&captureSurf)))
      printLog("video/d3d8: couldn't create capture surface (%d,%d,%d).\n",captureWidth,captureHeight,a4->BackBufferFormat);
      //printLog("video/d3d8: couldn't create capture surface.\n");

    if(!Real_D3DDevice8_AddRef)
      Real_D3DDevice8_AddRef = (PD3DDevice8_AddRef) DetourCOM(dev,1,(PBYTE) Mine_D3DDevice8_AddRef);

    if(!Real_D3DDevice8_Release)
      Real_D3DDevice8_Release = (PD3DDevice8_Release) DetourCOM(dev,2,(PBYTE) Mine_D3DDevice8_Release);

    if(!Real_D3DDevice8_Reset)
      Real_D3DDevice8_Reset = (PD3DDevice8_Reset) DetourCOM(dev,14,(PBYTE) Mine_D3DDevice8_Reset);

    if(!Real_D3DDevice8_Present)
      Real_D3DDevice8_Present = (PD3DDevice8_Present) DetourCOM(dev,15,(PBYTE) Mine_D3DDevice8_Present);

    deviceRefCount = 1;
    graphicsInitTiming();
  }

  return hr;
}

static IDirect3D8 * __stdcall Mine_Direct3DCreate8(UINT SDKVersion)
{
  IDirect3D8 *d3d8 = Real_Direct3DCreate8(SDKVersion);

  if(d3d8)
  {
    if(!Real_D3D8_CreateDevice)
      Real_D3D8_CreateDevice = (PD3D8_CreateDevice) DetourCOM(d3d8,15,(PBYTE) Mine_D3D8_CreateDevice);

    printLog("video/d3d8: IDirect3D8 object created.\n");
  }

  return d3d8;
}

void initVideo_Direct3D8()
{
  firstCreate = true;
  DetourFunctionWithTrampoline((PBYTE) Real_Direct3DCreate8,(PBYTE) Mine_Direct3DCreate8);
}