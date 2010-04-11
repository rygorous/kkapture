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

#include "d3d9.h"
#pragma comment (lib,"d3d9.lib")

DETOUR_TRAMPOLINE(IDirect3D9 * __stdcall Real_Direct3DCreate9(UINT SDKVersion), Direct3DCreate9);

typedef HRESULT (__stdcall *PD3D9_CreateDevice)(IDirect3D9 *d3d,UINT a0,UINT a1,DWORD a2,DWORD a3,D3DPRESENT_PARAMETERS *a4,IDirect3DDevice9 **a5);
typedef ULONG (__stdcall *PD3DDevice9_AddRef)(IDirect3DDevice9 *dev);
typedef ULONG (__stdcall *PD3DDevice9_Release)(IDirect3DDevice9 *dev);
typedef HRESULT (__stdcall *PD3DDevice9_Reset)(IDirect3DDevice9 *dev,D3DPRESENT_PARAMETERS *pp);
typedef HRESULT (__stdcall *PD3DDevice9_Present)(IDirect3DDevice9 *dev,DWORD a0,DWORD a1,DWORD a2,DWORD a3);

static PD3D9_CreateDevice Real_D3D9_CreateDevice = 0;
static PD3DDevice9_AddRef Real_D3DDevice9_AddRef = 0;
static PD3DDevice9_Release Real_D3DDevice9_Release = 0;
static PD3DDevice9_Reset Real_D3DDevice9_Reset = 0;
static PD3DDevice9_Present Real_D3DDevice9_Present = 0;

static IDirect3DTexture9 *captureTex = 0;
static IDirect3DSurface9 *captureSurf = 0;
static IDirect3DSurface9 *captureInbetween = 0;
static ULONG deviceRefCount = 0;
static bool multiSampleMode = false;
static bool firstCreate;

static void fixPresentParameters(D3DPRESENT_PARAMETERS *pp)
{
	pp->BackBufferCount = 1;

  if(pp->MultiSampleType == D3DMULTISAMPLE_NONE)
  {
		pp->SwapEffect = D3DSWAPEFFECT_COPY;
    multiSampleMode = false;
  }
  else
    multiSampleMode = true;

	// force back buffer format to something we can read
	D3DFORMAT fmt = pp->BackBufferFormat;
	if(fmt == D3DFMT_A2R10G10B10 || fmt == D3DFMT_A1R5G5B5 || fmt == D3DFMT_A8R8G8B8)
		pp->BackBufferFormat = D3DFMT_A8R8G8B8;
	else
		pp->BackBufferFormat = D3DFMT_X8R8G8B8;

  pp->PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
}

static void freeCaptureSurfaces()
{
  if(captureSurf)
  {
    captureSurf->Release();
    captureSurf = 0;
  }

  if(captureTex)
  {
    captureTex->Release();
    captureTex = 0;
  }

  if(captureInbetween)
  {
    captureInbetween->Release();
    captureInbetween = 0;
  }
}

static void createCaptureSurfaces(IDirect3DDevice9 *dev,D3DFORMAT format)
{
  if(SUCCEEDED(dev->CreateTexture(captureWidth,captureHeight,1,0,
    format,D3DPOOL_SYSTEMMEM,&captureTex,0)))
  {
    if(FAILED(captureTex->GetSurfaceLevel(0,&captureSurf)))
      printLog("video/d3d9: couldn't get capture surface.\n");

    if(FAILED(dev->CreateRenderTarget(captureWidth,captureHeight,format,
      D3DMULTISAMPLE_NONE,0,FALSE,&captureInbetween,0)))
      printLog("video/d3d9: couldn't create multisampling blit buffer\n");
  }
  else
    printLog("video/d3d9: couldn't create capture texture.\n");
}

template<class T> static void swap(T &a,T &b)
{
  T t = a;
  a = b;
  b = t;
}

static bool captureD3DFrame9(IDirect3DDevice9 *dev)
{
  if(!captureSurf)
    return false;

  videoNeedEncoder();

  IDirect3DSurface9 *back = 0, *captureSrc;
  bool error = true;

  VideoCaptureDataLock lock;

  dev->GetBackBuffer(0,0,D3DBACKBUFFER_TYPE_MONO,&back);
  if(back)
  {
    // if multisampling is used, we need another inbetween blit
    if(multiSampleMode)
    {
      if(FAILED(dev->StretchRect(back,0,captureInbetween,0,D3DTEXF_NONE)))
      {
        printLog("video: StretchRect failed!\n");
        return false;
      }

      captureSrc = captureInbetween;
    }
    else
      captureSrc = back;

    if(SUCCEEDED(dev->GetRenderTargetData(captureSrc,captureSurf)))
    {
      D3DLOCKED_RECT lr;

      if(SUCCEEDED(captureSurf->LockRect(&lr,0,D3DLOCK_READONLY)))
      {
        blitAndFlipBGRAToCaptureData((unsigned char *) lr.pBits,lr.Pitch);
        captureSurf->UnlockRect();
        error = false;
      }
      else
        printLog("video: lock+blit failed\n");
    }

    back->Release();
  }

  if(!error)
    encoder->WriteFrame(captureData);

  return !error;
}

static ULONG __stdcall Mine_D3DDevice9_AddRef(IDirect3DDevice9 *dev)
{
  Real_D3DDevice9_AddRef(dev);
  return ++deviceRefCount;
}

static ULONG __stdcall Mine_D3DDevice9_Release(IDirect3DDevice9 *dev)
{
  static bool inRelease = false;

  if(deviceRefCount && !inRelease)
  {
    if(--deviceRefCount == 0) // our surface is the last one
    {
      printLog("video/d3d9: Releasing...\n");
      inRelease = true;
      freeCaptureSurfaces();
      inRelease = false;
    }

    ULONG ref = Real_D3DDevice9_Release(dev);
    if(deviceRefCount)
      ref = deviceRefCount;

    return ref;
  }
  else
    return Real_D3DDevice9_Release(dev);
}

static HRESULT __stdcall Mine_D3DDevice9_Reset(IDirect3DDevice9 *dev,D3DPRESENT_PARAMETERS *pp)
{
  if(pp)
    fixPresentParameters(pp);

  deviceRefCount += 512; // to make sure nothing gets released wrongly

  freeCaptureSurfaces();
  HRESULT hr = Real_D3DDevice9_Reset(dev,pp);
  
  if(SUCCEEDED(hr))
  {
    printLog("video/d3d9: Reset successful.\n");

    setCaptureResolution(pp->BackBufferWidth,pp->BackBufferHeight);
    createCaptureSurfaces(dev,pp->BackBufferFormat);

    deviceRefCount -= 512;
    videoStartNextPart();
  }
  else
    deviceRefCount -= 512;

  return hr;
}

static HRESULT __stdcall Mine_D3DDevice9_Present(IDirect3DDevice9 *dev,DWORD a0,DWORD a1,DWORD a2,DWORD a3)
{
  HRESULT hr = Real_D3DDevice9_Present(dev,a0,a1,a2,a3);

  if(params.CaptureVideo)
  {
    if(!captureD3DFrame9(dev))
      printLog("video/d3d9: Frame capture failed! (frame %d)\n",getFrameTiming());
  }

  nextFrame();

  return hr;
}

static HRESULT __stdcall Mine_D3D9_CreateDevice(IDirect3D9 *d3d,UINT a0,UINT a1,
  DWORD a2,DWORD a3,D3DPRESENT_PARAMETERS *a4,IDirect3DDevice9 **a5)
{
  if(a4)
    fixPresentParameters(a4);

  HRESULT hr = Real_D3D9_CreateDevice(d3d,a0,a1,a2,a3,a4,a5);

  if(SUCCEEDED(hr) && *a5)
  {
    IDirect3DDevice9 *dev = *a5;

    if(!firstCreate)
      videoStartNextPart();
    else
      firstCreate = false;

    printLog("video/d3d9: CreateDevice successful.\n");

    setCaptureResolution(a4->BackBufferWidth,a4->BackBufferHeight);
    createCaptureSurfaces(dev,a4->BackBufferFormat);

    deviceRefCount = 1;

    if(!Real_D3DDevice9_AddRef)
      Real_D3DDevice9_AddRef = (PD3DDevice9_AddRef) DetourCOM(dev,1,(PBYTE) Mine_D3DDevice9_AddRef);

    if(!Real_D3DDevice9_Release)
      Real_D3DDevice9_Release = (PD3DDevice9_Release) DetourCOM(dev,2,(PBYTE) Mine_D3DDevice9_Release);

    if(!Real_D3DDevice9_Reset)
      Real_D3DDevice9_Reset = (PD3DDevice9_Reset) DetourCOM(dev,16,(PBYTE) Mine_D3DDevice9_Reset);

    if(!Real_D3DDevice9_Present)
      Real_D3DDevice9_Present = (PD3DDevice9_Present) DetourCOM(dev,17,(PBYTE) Mine_D3DDevice9_Present);

    graphicsInitTiming();
  }

  return hr;
}

static IDirect3D9 * __stdcall Mine_Direct3DCreate9(UINT SDKVersion)
{
  IDirect3D9 *d3d9 = Real_Direct3DCreate9(SDKVersion);

  if(d3d9)
  {
    printLog("video/d3d9: IDirect3D9 object created.\n");

    if(!Real_D3D9_CreateDevice)
      Real_D3D9_CreateDevice = (PD3D9_CreateDevice) DetourCOM(d3d9,16,(PBYTE) Mine_D3D9_CreateDevice);
  }

  return d3d9;
}

void initVideo_Direct3D9()
{
  firstCreate = true;
  DetourFunctionWithTrampoline((PBYTE) Real_Direct3DCreate9,(PBYTE) Mine_Direct3DCreate9);
}