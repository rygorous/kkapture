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

#include "ddraw.h"
#pragma comment(lib,"ddraw.lib")

// DON'T LOOK, this is by far the messiest source in this project...

DETOUR_TRAMPOLINE(HRESULT __stdcall Real_DirectDrawCreate(GUID *lpGUID,LPDIRECTDRAW *lplpDD,IUnknown *pUnkOuter), DirectDrawCreate);
DETOUR_TRAMPOLINE(HRESULT __stdcall Real_DirectDrawCreateEx(GUID *lpGUID,LPVOID *lplpDD,REFIID iid,IUnknown *pUnkOuter), DirectDrawCreateEx);

typedef HRESULT (__stdcall *PQueryInterface)(IUnknown *dd,REFIID iid,LPVOID *ppObj);
typedef HRESULT (__stdcall *PDDraw_CreateSurface)(IUnknown *dd,LPDDSURFACEDESC ddsd,LPDIRECTDRAWSURFACE *surf,IUnknown *pUnkOuter);
typedef HRESULT (__stdcall *PDDrawSurface_Blt)(IUnknown *dds,LPRECT destrect,IUnknown *src,LPRECT srcrect,DWORD dwFlags,LPDDBLTFX fx);
typedef HRESULT (__stdcall *PDDrawSurface_Flip)(IUnknown *dds,IUnknown *surf,DWORD flags);
typedef HRESULT (__stdcall *PDDrawSurface_Unlock)(IUnknown *dds,void *ptr);

static PQueryInterface Real_DDraw_QueryInterface = 0;
static PDDraw_CreateSurface Real_DDraw_CreateSurface = 0;
static PQueryInterface Real_DDrawSurface_QueryInterface = 0;
static PDDrawSurface_Blt Real_DDrawSurface_Blt = 0;
static PDDrawSurface_Flip Real_DDrawSurface_Flip = 0;
static PDDrawSurface_Unlock Real_DDrawSurface_Unlock = 0;

static PQueryInterface Real_DDraw2_QueryInterface = 0;
static PDDraw_CreateSurface Real_DDraw2_CreateSurface = 0;
static PQueryInterface Real_DDrawSurface2_QueryInterface = 0;
static PDDrawSurface_Blt Real_DDrawSurface2_Blt = 0;
static PDDrawSurface_Flip Real_DDrawSurface2_Flip = 0;
static PDDrawSurface_Unlock Real_DDrawSurface2_Unlock = 0;

static PQueryInterface Real_DDrawSurface3_QueryInterface = 0;
static PDDrawSurface_Blt Real_DDrawSurface3_Blt = 0;
static PDDrawSurface_Flip Real_DDrawSurface3_Flip = 0;
static PDDrawSurface_Unlock Real_DDrawSurface3_Unlock = 0;

static PQueryInterface Real_DDraw4_QueryInterface = 0;
static PDDraw_CreateSurface Real_DDraw4_CreateSurface = 0;
static PQueryInterface Real_DDrawSurface4_QueryInterface = 0;
static PDDrawSurface_Blt Real_DDrawSurface4_Blt = 0;
static PDDrawSurface_Flip Real_DDrawSurface4_Flip = 0;
static PDDrawSurface_Unlock Real_DDrawSurface4_Unlock = 0;

static PQueryInterface Real_DDraw7_QueryInterface = 0;
static PDDraw_CreateSurface Real_DDraw7_CreateSurface = 0;
static PQueryInterface Real_DDrawSurface7_QueryInterface = 0;
static PDDrawSurface_Blt Real_DDrawSurface7_Blt = 0;
static PDDrawSurface_Flip Real_DDrawSurface7_Flip = 0;
static PDDrawSurface_Unlock Real_DDrawSurface7_Unlock = 0;

static IUnknown *PrimaryDDraw = 0;
static IUnknown *PrimarySurface = 0;
static int PrimarySurfaceVersion = 0;

static ULONG DDrawRefCount = 0;

// ---- blit surface handling

static IDirectDrawSurface* GetBlitSurface()
{
  IDirectDrawSurface* blitSurface;

  if(PrimarySurfaceVersion < 4)
  {
    IDirectDrawSurface *surf = (IDirectDrawSurface *) PrimarySurface;

    DDSURFACEDESC ddsd;
    ZeroMemory(&ddsd,sizeof(ddsd));
    ddsd.dwSize = sizeof(ddsd);
    if(FAILED(surf->GetSurfaceDesc(&ddsd)))
      printLog("video: can't get surface desc\n");
    else
    {
      ddsd.dwWidth = captureWidth;
      ddsd.dwHeight = captureHeight;

      ddsd.dwFlags = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT;
      ddsd.ddsCaps.dwCaps = DDSCAPS_OFFSCREENPLAIN | DDSCAPS_SYSTEMMEMORY;
      IDirectDraw *dd = (IDirectDraw *) PrimaryDDraw;
      if(FAILED(dd->CreateSurface(&ddsd,&blitSurface,0)))
        printLog("video: could not create blit target\n");
    }
  }
  else
  {
    IDirectDrawSurface4 *srf4 = (IDirectDrawSurface4 *) PrimarySurface;

    DDSURFACEDESC2 ddsd;
    ZeroMemory(&ddsd,sizeof(ddsd));
    ddsd.dwSize = sizeof(ddsd);
    if(FAILED(srf4->GetSurfaceDesc(&ddsd)))
      printLog("video: can't get surface desc\n");
    else
    {
      ddsd.dwWidth = captureWidth;
      ddsd.dwHeight = captureHeight;

      ddsd.dwFlags = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT;
      ddsd.ddsCaps.dwCaps = DDSCAPS_OFFSCREENPLAIN | DDSCAPS_SYSTEMMEMORY;
      ddsd.ddsCaps.dwCaps2 = 0;
      IDirectDraw4 *dd = (IDirectDraw4 *) PrimaryDDraw;
      if(FAILED(dd->CreateSurface(&ddsd,(IDirectDrawSurface4 **) &blitSurface,0)))
        printLog("video: could not create blit target\n");
    }
  }

  return blitSurface;
}

// ---- blitter

// this is not meant to be as fast as possible; instead I try to keep it
// generic and short because blitting itself is not going to be a performance
// bottleneck anyway.
class DDrawBlitter : public GenericBlitter
{
  void SetFormat(LPDDPIXELFORMAT fmt)
  {
    if(fmt->dwFlags & DDPF_PALETTEINDEXED8)
      SetPalettedFormat(8);
    else if(fmt->dwFlags & DDPF_RGB)
      SetRGBFormat(fmt->dwRGBBitCount,fmt->dwRBitMask,fmt->dwGBitMask,fmt->dwBBitMask);
    else
      SetInvalidFormat();
  }
  
  void UpdatePalette()
  {
    if(IsPaletted() && PrimarySurface)
    {
      IDirectDrawPalette *pal = 0;
      ((IDirectDrawSurface *) PrimarySurface)->GetPalette(&pal);

      if(pal)
      {
        PALETTEENTRY entries[256];
        pal->GetEntries(0,0,256,entries);
        SetPalette(entries,256);
        pal->Release();
      }
      else
        printLog("video: couldn't get palette!\n");
    }
  }

public:
  bool BlitSurfaceToCapture(IDirectDrawSurface *surf,int version)
  {
    DDPIXELFORMAT fmt;
    ZeroMemory(&fmt,sizeof(fmt));
    fmt.dwSize = sizeof(fmt);

    IDirectDrawSurface* blitSurface = GetBlitSurface();
    if(!blitSurface)
      return false;

    if(FAILED(surf->GetPixelFormat(&fmt)))
    {
      printLog("video: can't get pixel format\n");
      return false;
    }

    SetFormat(&fmt);
    if(IsPaletted())
      UpdatePalette();

    // blit backbuffer to our readback surface
    RECT rc;
    rc.left = 0;
    rc.top = 0;
    rc.right = captureWidth;
    rc.bottom = captureHeight;

    if(FAILED(blitSurface->Blt(&rc,surf,&rc,DDBLT_WAIT,0)))
    {
      printLog("video: blit failed\n");
      return false;
    }

    LPBYTE surface;
    DWORD pitch;

    if(version < 4)
    {
      DDSURFACEDESC ddsd;
      ZeroMemory(&ddsd,sizeof(ddsd));
      ddsd.dwSize = sizeof(ddsd);

      if(FAILED(blitSurface->Lock(0,&ddsd,DDLOCK_WAIT,0)))
      {
        printLog("video: can't lock surface\n");
        return false;
      }

      surface = (LPBYTE) ddsd.lpSurface;
      pitch = ddsd.lPitch;
    }
    else
    {
      IDirectDrawSurface4 *srf4 = (IDirectDrawSurface4 *) blitSurface;

      DDSURFACEDESC2 ddsd;
      ZeroMemory(&ddsd,sizeof(ddsd));
      ddsd.dwSize = sizeof(ddsd);

      if(FAILED(srf4->Lock(0,&ddsd,DDLOCK_WAIT,0)))
      {
        printLog("video: can't lock surface\n");
        return false;
      }

      surface = (LPBYTE) ddsd.lpSurface;
      pitch = ddsd.lPitch;
    }

    // blit individual lines
    for(int y=0;y<captureHeight;y++)
    {
      unsigned char *src = surface + (captureHeight-1-y) * pitch;
      unsigned char *dst = captureData + y * captureWidth * 3;

      BlitOneLine(src,dst,captureWidth);
    }

    blitSurface->Unlock(surface);
    blitSurface->Release();

    return true;
  }
};

static DDrawBlitter Blitter;

// ---- stuff common to all versions

static void PatchDDrawInterface(IUnknown *dd,int version);
static void PatchDDrawSurface(IUnknown *surf,int version);

static HRESULT DDrawQueryInterface(HRESULT hr,REFIID iid,LPVOID *ppObject)
{
  if(FAILED(hr) || !ppObject || !*ppObject)
    return hr;

  IUnknown *iface = (IUnknown *) *ppObject;
  if(iid == IID_IDirectDraw)
    PatchDDrawInterface(iface,1);
  else if(iid == IID_IDirectDraw2)
    PatchDDrawInterface(iface,2);
  else if(iid == IID_IDirectDraw4)
    PatchDDrawInterface(iface,4);
  else if(iid == IID_IDirectDraw7)
    PatchDDrawInterface(iface,7);

  return hr;
}

static HRESULT DDrawSurfQueryInterface(HRESULT hr,REFIID iid,LPVOID *ppObject)
{
  if(FAILED(hr) || !ppObject || !*ppObject)
    return hr;

  IUnknown *iface = (IUnknown *) *ppObject;
  if(iid == IID_IDirectDrawSurface)
    PatchDDrawSurface(iface,1);
  else if(iid == IID_IDirectDrawSurface2)
    PatchDDrawSurface(iface,2);
  else if(iid == IID_IDirectDrawSurface3)
    PatchDDrawSurface(iface,3);
  else if(iid == IID_IDirectDrawSurface4)
    PatchDDrawSurface(iface,4);
  else if(iid == IID_IDirectDrawSurface7)
    PatchDDrawSurface(iface,7);

  return hr;
}

static void PrimarySurfaceCreated(IUnknown *ddraw,IUnknown *srfp,int ver)
{
  PrimaryDDraw = ddraw;
  PrimarySurface = srfp;
  PrimarySurfaceVersion = ver;

  if(PrimarySurfaceVersion < 4)
  {
    IDirectDrawSurface *surf = (IDirectDrawSurface *) PrimarySurface;

    DDSURFACEDESC ddsd;
    ZeroMemory(&ddsd,sizeof(ddsd));
    ddsd.dwSize = sizeof(ddsd);
    if(FAILED(surf->GetSurfaceDesc(&ddsd)))
      printLog("video: can't get surface desc\n");
    else
    {
      if(captureWidth != ddsd.dwWidth || captureHeight != ddsd.dwHeight)
        setCaptureResolution(ddsd.dwWidth,ddsd.dwHeight);
    }
  }
  else
  {
    IDirectDrawSurface4 *srf4 = (IDirectDrawSurface4 *) PrimarySurface;

    DDSURFACEDESC2 ddsd;
    ZeroMemory(&ddsd,sizeof(ddsd));
    ddsd.dwSize = sizeof(ddsd);
    if(FAILED(srf4->GetSurfaceDesc(&ddsd)))
      printLog("video: can't get surface desc\n");
    else
    {
      if(captureWidth != ddsd.dwWidth || captureHeight != ddsd.dwHeight)
        setCaptureResolution(ddsd.dwWidth,ddsd.dwHeight);
    }
  }

  graphicsInitTiming();
}

static void ImplementFlip(IUnknown *surf,int version)
{
  IDirectDrawSurface *back = 0;

  videoNeedEncoder();

  if(params.CaptureVideo)
  {
    if(version < 4)
    {
      DDSCAPS caps;
      ZeroMemory(&caps,sizeof(caps));

      caps.dwCaps = DDSCAPS_BACKBUFFER;
      ((IDirectDrawSurface *) surf)->GetAttachedSurface(&caps,&back);
    }
    else
    {
      DDSCAPS2 caps;
      ZeroMemory(&caps,sizeof(caps));

      caps.dwCaps = DDSCAPS_BACKBUFFER;
      ((IDirectDrawSurface4 *) surf)->GetAttachedSurface(&caps,(IDirectDrawSurface4 **) &back);
    }

    if(back)
    {
      VideoCaptureDataLock lock;

      if(Blitter.BlitSurfaceToCapture(back,version))
        encoder->WriteFrame(captureData);

      back->Release();
    }
  }

  nextFrame();
}

static void ImplementBltToPrimary(IUnknown *surf,int version)
{
  if(!surf)
    return;

  videoNeedEncoder();

  if(params.CaptureVideo)
  {
    if(version < 4)
    {
      DDSURFACEDESC ddsd;
      ZeroMemory(&ddsd,sizeof(ddsd));

      ddsd.dwSize = sizeof(ddsd);
      if(SUCCEEDED(((IDirectDrawSurface *) surf)->GetSurfaceDesc(&ddsd)))
        setCaptureResolution(ddsd.dwWidth,ddsd.dwHeight);
      else
        printLog("video/ddraw: couldn't get blit source surface desc\n");
    }
    else
    {
      DDSURFACEDESC2 ddsd;
      ZeroMemory(&ddsd,sizeof(ddsd));
      
      ddsd.dwSize = sizeof(ddsd);
      if(SUCCEEDED(((IDirectDrawSurface4 *) surf)->GetSurfaceDesc(&ddsd)))
        setCaptureResolution(ddsd.dwWidth,ddsd.dwHeight);
      else
        printLog("video/ddraw: couldn't get blit source surface desc\n");
    }

    VideoCaptureDataLock lock;

    if(Blitter.BlitSurfaceToCapture((IDirectDrawSurface *) surf,version))
      encoder->WriteFrame(captureData);
  }

  nextFrame();
}

// ---- directdraw 1

static HRESULT __stdcall Mine_DDraw_QueryInterface(IUnknown *dd,REFIID iid,LPVOID *ppObj)
{
  return DDrawQueryInterface(Real_DDraw_QueryInterface(dd,iid,ppObj),iid,ppObj);
}

static HRESULT __stdcall Mine_DDraw_CreateSurface(IDirectDraw *dd,LPDDSURFACEDESC ddsd,LPDIRECTDRAWSURFACE *pSurf,IUnknown *pUnkOuter)
{
  HRESULT hr = Real_DDraw_CreateSurface(dd,ddsd,pSurf,pUnkOuter);
  if(SUCCEEDED(hr))
  {
    PatchDDrawSurface(*pSurf,1);
    if(ddsd->ddsCaps.dwCaps & DDSCAPS_PRIMARYSURFACE)
      PrimarySurfaceCreated(dd,*pSurf,1);
  }

  return hr;
}

static HRESULT __stdcall Mine_DDrawSurface_QueryInterface(IUnknown *dd,REFIID iid,LPVOID *ppObj)
{
  return DDrawSurfQueryInterface(Real_DDrawSurface_QueryInterface(dd,iid,ppObj),iid,ppObj);
}

static HRESULT __stdcall Mine_DDrawSurface_Blt(IUnknown *me,LPRECT dstr,IUnknown *src,LPRECT srcr,DWORD flags,LPDDBLTFX fx)
{
  HRESULT hr = Real_DDrawSurface_Blt(me,dstr,src,srcr,flags,fx);

  if(PrimarySurfaceVersion == 1 && me == PrimarySurface)
    ImplementBltToPrimary(src,1);

  return hr;
}

static HRESULT __stdcall Mine_DDrawSurface_Flip(IUnknown *me,IUnknown *other,DWORD flags)
{
  if(PrimarySurfaceVersion == 1)
    ImplementFlip(me,1);

  return Real_DDrawSurface_Flip(me,other,flags);
}

static HRESULT __stdcall Mine_DDrawSurface_Unlock(IUnknown *me,void *ptr)
{
  HRESULT hr = Real_DDrawSurface_Unlock(me,ptr);
  if(SUCCEEDED(hr) && PrimarySurfaceVersion == 1 && me == PrimarySurface)
    ImplementBltToPrimary(me,1);

  return hr;
}

// ---- directdraw 2

static HRESULT __stdcall Mine_DDraw2_QueryInterface(IUnknown *dd,REFIID iid,LPVOID *ppObj)
{
  return DDrawQueryInterface(Real_DDraw2_QueryInterface(dd,iid,ppObj),iid,ppObj);
}

static HRESULT __stdcall Mine_DDraw2_CreateSurface(IDirectDraw2 *dd,LPDDSURFACEDESC ddsd,LPDIRECTDRAWSURFACE *pSurf,IUnknown *pUnkOuter)
{
  HRESULT hr = Real_DDraw2_CreateSurface(dd,ddsd,pSurf,pUnkOuter);
  if(SUCCEEDED(hr))
  {
    PatchDDrawSurface(*pSurf,1);
    if(ddsd->ddsCaps.dwCaps & DDSCAPS_PRIMARYSURFACE)
      PrimarySurfaceCreated(dd,*pSurf,1);
  }

  return hr;
}

static HRESULT __stdcall Mine_DDrawSurface2_QueryInterface(IUnknown *dd,REFIID iid,LPVOID *ppObj)
{
  return DDrawSurfQueryInterface(Real_DDrawSurface2_QueryInterface(dd,iid,ppObj),iid,ppObj);
}

static HRESULT __stdcall Mine_DDrawSurface2_Blt(IUnknown *me,LPRECT dstr,IUnknown *src,LPRECT srcr,DWORD flags,LPDDBLTFX fx)
{
  HRESULT hr = Real_DDrawSurface2_Blt(me,dstr,src,srcr,flags,fx);

  if(PrimarySurfaceVersion == 2 && me == PrimarySurface)
    ImplementBltToPrimary(src,2);

  return hr;
}

static HRESULT __stdcall Mine_DDrawSurface2_Flip(IUnknown *me,IUnknown *other,DWORD flags)
{
  if(PrimarySurfaceVersion == 2)
    ImplementFlip(me,2);

  return Real_DDrawSurface2_Flip(me,other,flags | DDFLIP_NOVSYNC);
}

static HRESULT __stdcall Mine_DDrawSurface2_Unlock(IUnknown *me,void *ptr)
{
  HRESULT hr = Real_DDrawSurface2_Unlock(me,ptr);
  if(SUCCEEDED(hr) && PrimarySurfaceVersion == 2 && me == PrimarySurface)
    ImplementBltToPrimary(me,2);

  return hr;
}

// ---- directdraw 3

static HRESULT __stdcall Mine_DDrawSurface3_QueryInterface(IUnknown *dd,REFIID iid,LPVOID *ppObj)
{
  return DDrawSurfQueryInterface(Real_DDrawSurface3_QueryInterface(dd,iid,ppObj),iid,ppObj);
}

static HRESULT __stdcall Mine_DDrawSurface3_Blt(IUnknown *me,LPRECT dstr,IUnknown *src,LPRECT srcr,DWORD flags,LPDDBLTFX fx)
{
  HRESULT hr = Real_DDrawSurface3_Blt(me,dstr,src,srcr,flags,fx);

  if(PrimarySurfaceVersion == 3 && me == PrimarySurface)
    ImplementBltToPrimary(src,3);

  return hr;
}

static HRESULT __stdcall Mine_DDrawSurface3_Flip(IUnknown *me,IUnknown *other,DWORD flags)
{
  if(PrimarySurfaceVersion == 3)
    ImplementFlip(me,3);

  return Real_DDrawSurface3_Flip(me,other,flags | DDFLIP_NOVSYNC);
}

static HRESULT __stdcall Mine_DDrawSurface3_Unlock(IUnknown *me,void *ptr)
{
  HRESULT hr = Real_DDrawSurface3_Unlock(me,ptr);
  if(SUCCEEDED(hr) && PrimarySurfaceVersion == 3 && me == PrimarySurface)
    ImplementBltToPrimary(me,3);

  return hr;
}

// ---- directdraw 4

static HRESULT __stdcall Mine_DDraw4_QueryInterface(IUnknown *dd,REFIID iid,LPVOID *ppObj)
{
  return DDrawQueryInterface(Real_DDraw4_QueryInterface(dd,iid,ppObj),iid,ppObj);
}

static HRESULT __stdcall Mine_DDraw4_CreateSurface(IDirectDraw4 *dd,LPDDSURFACEDESC2 ddsd,LPDIRECTDRAWSURFACE4 *pSurf,IUnknown *pUnkOuter)
{
  HRESULT hr = Real_DDraw4_CreateSurface(dd,(LPDDSURFACEDESC) ddsd,(LPDIRECTDRAWSURFACE *) pSurf,pUnkOuter);
  if(SUCCEEDED(hr))
  {
    PatchDDrawSurface(*pSurf,4);
    if(ddsd->ddsCaps.dwCaps & DDSCAPS_PRIMARYSURFACE)
      PrimarySurfaceCreated(dd,*pSurf,4);
  }

  return hr;
}

static HRESULT __stdcall Mine_DDrawSurface4_QueryInterface(IUnknown *dd,REFIID iid,LPVOID *ppObj)
{
  return DDrawSurfQueryInterface(Real_DDrawSurface4_QueryInterface(dd,iid,ppObj),iid,ppObj);
}

static HRESULT __stdcall Mine_DDrawSurface4_Blt(IUnknown *me,LPRECT dstr,IUnknown *src,LPRECT srcr,DWORD flags,LPDDBLTFX fx)
{
  HRESULT hr = Real_DDrawSurface4_Blt(me,dstr,src,srcr,flags,fx);

  if(PrimarySurfaceVersion == 4 && me == PrimarySurface)
    ImplementBltToPrimary(src,4);

  return hr;
}

static HRESULT __stdcall Mine_DDrawSurface4_Flip(IUnknown *me,IUnknown *other,DWORD flags)
{
  if(PrimarySurfaceVersion == 4)
    ImplementFlip(me,4);

  return Real_DDrawSurface4_Flip(me,other,flags | DDFLIP_NOVSYNC);
}

static HRESULT __stdcall Mine_DDrawSurface4_Unlock(IUnknown *me,void *ptr)
{
  HRESULT hr = Real_DDrawSurface4_Unlock(me,ptr);
  if(SUCCEEDED(hr) && PrimarySurfaceVersion == 4 && me == PrimarySurface)
    ImplementBltToPrimary(me,4);

  return hr;
}

// ---- directdraw 7

static HRESULT __stdcall Mine_DDraw7_QueryInterface(IUnknown *dd,REFIID iid,LPVOID *ppObj)
{
  return DDrawQueryInterface(Real_DDraw7_QueryInterface(dd,iid,ppObj),iid,ppObj);
}

static HRESULT __stdcall Mine_DDraw7_CreateSurface(IDirectDraw7 *dd,LPDDSURFACEDESC2 ddsd,LPDIRECTDRAWSURFACE7 *pSurf,IUnknown *pUnkOuter)
{
  HRESULT hr = Real_DDraw7_CreateSurface(dd,(LPDDSURFACEDESC) ddsd,(LPDIRECTDRAWSURFACE *) pSurf,pUnkOuter);
  if(SUCCEEDED(hr))
  {
    PatchDDrawSurface(*pSurf,7);
    if(ddsd->ddsCaps.dwCaps & DDSCAPS_PRIMARYSURFACE)
      PrimarySurfaceCreated(dd,*pSurf,7);
  }

  return hr;
}

static HRESULT __stdcall Mine_DDrawSurface7_QueryInterface(IUnknown *dd,REFIID iid,LPVOID *ppObj)
{
  return DDrawSurfQueryInterface(Real_DDrawSurface7_QueryInterface(dd,iid,ppObj),iid,ppObj);
}

static HRESULT __stdcall Mine_DDrawSurface7_Blt(IUnknown *me,LPRECT dstr,IUnknown *src,LPRECT srcr,DWORD flags,LPDDBLTFX fx)
{
  HRESULT hr = Real_DDrawSurface7_Blt(me,dstr,src,srcr,flags,fx);

  if(PrimarySurfaceVersion == 7 && me == PrimarySurface)
    ImplementBltToPrimary(src,7);

  return hr;
}

static HRESULT __stdcall Mine_DDrawSurface7_Flip(IUnknown *me,IUnknown *other,DWORD flags)
{
  if(PrimarySurfaceVersion == 7)
    ImplementFlip(me,7);

  return Real_DDrawSurface7_Flip(me,other,flags | DDFLIP_NOVSYNC);
}

static HRESULT __stdcall Mine_DDrawSurface7_Unlock(IUnknown *me,void *ptr)
{
  HRESULT hr = Real_DDrawSurface7_Unlock(me,ptr);
  if(SUCCEEDED(hr) && PrimarySurfaceVersion == 7 && me == PrimarySurface)
    ImplementBltToPrimary(me,7);

  return hr;
}

// ---- again, common stuff

static void PatchDDrawInterface(IUnknown *dd,int version)
{
  switch(version)
  {
  case 1:
    if(!Real_DDraw_QueryInterface)
      Real_DDraw_QueryInterface = (PQueryInterface) DetourCOM(dd,0,(PBYTE) Mine_DDraw_QueryInterface);

    if(!Real_DDraw_CreateSurface)
      Real_DDraw_CreateSurface = (PDDraw_CreateSurface) DetourCOM(dd,6,(PBYTE) Mine_DDraw_CreateSurface);
    break;
    
  case 2:
    if(!Real_DDraw2_QueryInterface)
      Real_DDraw2_QueryInterface = (PQueryInterface) DetourCOM(dd,0,(PBYTE) Mine_DDraw2_QueryInterface);

    if(!Real_DDraw2_CreateSurface)
      Real_DDraw2_CreateSurface = (PDDraw_CreateSurface) DetourCOM(dd,6,(PBYTE) Mine_DDraw2_CreateSurface);
    break;

  case 4:
    if(!Real_DDraw4_QueryInterface)
      Real_DDraw4_QueryInterface = (PQueryInterface) DetourCOM(dd,0,(PBYTE) Mine_DDraw4_QueryInterface);

    if(!Real_DDraw4_CreateSurface)
      Real_DDraw4_CreateSurface = (PDDraw_CreateSurface) DetourCOM(dd,6,(PBYTE) Mine_DDraw4_CreateSurface);
    break;

  case 7:
    if(!Real_DDraw7_QueryInterface)
      Real_DDraw7_QueryInterface = (PQueryInterface) DetourCOM(dd,0,(PBYTE) Mine_DDraw7_QueryInterface);

    if(!Real_DDraw7_CreateSurface)
      Real_DDraw7_CreateSurface = (PDDraw_CreateSurface) DetourCOM(dd,6,(PBYTE) Mine_DDraw7_CreateSurface);
    break;
  }
}

static void PatchDDrawSurface(IUnknown *dd,int version)
{
  switch(version)
  {
  case 1:
    if(!Real_DDrawSurface_QueryInterface)
      Real_DDrawSurface_QueryInterface = (PQueryInterface) DetourCOM(dd,0,(PBYTE) Mine_DDrawSurface_QueryInterface);

    if(!Real_DDrawSurface_Blt)
      Real_DDrawSurface_Blt = (PDDrawSurface_Blt) DetourCOM(dd,5,(PBYTE) Mine_DDrawSurface_Blt);

    if(!Real_DDrawSurface_Flip)
      Real_DDrawSurface_Flip = (PDDrawSurface_Flip) DetourCOM(dd,11,(PBYTE) Mine_DDrawSurface_Flip);

    if(!Real_DDrawSurface_Unlock)
      Real_DDrawSurface_Unlock = (PDDrawSurface_Unlock) DetourCOM(dd,32,(PBYTE) Mine_DDrawSurface_Unlock);
    break;

  case 2:
    if(!Real_DDrawSurface2_QueryInterface)
      Real_DDrawSurface2_QueryInterface = (PQueryInterface) DetourCOM(dd,0,(PBYTE) Mine_DDrawSurface2_QueryInterface);

    if(!Real_DDrawSurface2_Blt)
      Real_DDrawSurface2_Blt = (PDDrawSurface_Blt) DetourCOM(dd,5,(PBYTE) Mine_DDrawSurface2_Blt);

    if(!Real_DDrawSurface2_Flip)
      Real_DDrawSurface2_Flip = (PDDrawSurface_Flip) DetourCOM(dd,11,(PBYTE) Mine_DDrawSurface2_Flip);

    if(!Real_DDrawSurface2_Unlock)
      Real_DDrawSurface2_Unlock = (PDDrawSurface_Unlock) DetourCOM(dd,32,(PBYTE) Mine_DDrawSurface2_Unlock);
    break;

  case 3:
    if(!Real_DDrawSurface3_QueryInterface)
      Real_DDrawSurface3_QueryInterface = (PQueryInterface) DetourCOM(dd,0,(PBYTE) Mine_DDrawSurface3_QueryInterface);

    if(!Real_DDrawSurface3_Blt)
      Real_DDrawSurface3_Blt = (PDDrawSurface_Blt) DetourCOM(dd,5,(PBYTE) Mine_DDrawSurface3_Blt);

    if(!Real_DDrawSurface3_Flip)
      Real_DDrawSurface3_Flip = (PDDrawSurface_Flip) DetourCOM(dd,11,(PBYTE) Mine_DDrawSurface3_Flip);

    if(!Real_DDrawSurface3_Unlock)
      Real_DDrawSurface3_Unlock = (PDDrawSurface_Unlock) DetourCOM(dd,32,(PBYTE) Mine_DDrawSurface3_Unlock);
    break;

  case 4:
    if(!Real_DDrawSurface4_QueryInterface)
      Real_DDrawSurface4_QueryInterface = (PQueryInterface) DetourCOM(dd,0,(PBYTE) Mine_DDrawSurface4_QueryInterface);

    if(!Real_DDrawSurface4_Blt)
      Real_DDrawSurface4_Blt = (PDDrawSurface_Blt) DetourCOM(dd,5,(PBYTE) Mine_DDrawSurface4_Blt);

    if(!Real_DDrawSurface4_Flip)
      Real_DDrawSurface4_Flip = (PDDrawSurface_Flip) DetourCOM(dd,11,(PBYTE) Mine_DDrawSurface4_Flip);

    if(!Real_DDrawSurface4_Unlock)
      Real_DDrawSurface4_Unlock = (PDDrawSurface_Unlock) DetourCOM(dd,32,(PBYTE) Mine_DDrawSurface4_Unlock);
    break;

  case 7:
    if(!Real_DDrawSurface7_QueryInterface)
      Real_DDrawSurface7_QueryInterface = (PQueryInterface) DetourCOM(dd,0,(PBYTE) Mine_DDrawSurface7_QueryInterface);

    if(!Real_DDrawSurface7_Blt)
      Real_DDrawSurface7_Blt = (PDDrawSurface_Blt) DetourCOM(dd,5,(PBYTE) Mine_DDrawSurface7_Blt);

    if(!Real_DDrawSurface7_Flip)
      Real_DDrawSurface7_Flip = (PDDrawSurface_Flip) DetourCOM(dd,11,(PBYTE) Mine_DDrawSurface7_Flip);

    if(!Real_DDrawSurface7_Unlock)
      Real_DDrawSurface7_Unlock = (PDDrawSurface_Unlock) DetourCOM(dd,32,(PBYTE) Mine_DDrawSurface7_Unlock);
    break;
  }
}

HRESULT __stdcall Mine_DirectDrawCreate(GUID *lpGUID,LPDIRECTDRAW *lplpDD,IUnknown *pUnkOuter)
{
  HRESULT hr = Real_DirectDrawCreate(lpGUID,lplpDD,pUnkOuter);
  if(SUCCEEDED(hr))
  {
    printLog("video: DirectDrawCreate successful\n");
    PatchDDrawInterface(*lplpDD,1);
  }

  return hr;
}

HRESULT __stdcall Mine_DirectDrawCreateEx(GUID *lpGUID,LPVOID *lplpDD,REFIID iid,IUnknown *pUnkOuter)
{
  HRESULT hr = Real_DirectDrawCreateEx(lpGUID,lplpDD,iid,pUnkOuter);
  if(SUCCEEDED(hr))
  {
    printLog("video: DirectDrawCreateEx successful\n");
    DDrawQueryInterface(hr,iid,lplpDD);
  }

  return hr;
}

void initVideo_DirectDraw()
{
  DetourFunctionWithTrampoline((PBYTE) Real_DirectDrawCreate,(PBYTE) Mine_DirectDrawCreate);
  DetourFunctionWithTrampoline((PBYTE) Real_DirectDrawCreateEx,(PBYTE) Mine_DirectDrawCreateEx);
}