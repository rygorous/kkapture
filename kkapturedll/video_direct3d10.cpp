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

#include "dxgi.h"
#include "d3d10.h"

typedef HRESULT (__stdcall *PCREATEDXGIFACTORY)(REFIID riid,void **ppFactory);
typedef HRESULT (__stdcall *PFACTORY_CREATESWAPCHAIN)(IUnknown *me,IUnknown *dev,DXGI_SWAP_CHAIN_DESC *desc,IDXGISwapChain **chain);
typedef HRESULT (__stdcall *PSWAPCHAIN_PRESENT)(IDXGISwapChain *me,UINT SyncInterval,UINT Flags);

static PCREATEDXGIFACTORY Real_CreateDXGIFactory = 0;
static PFACTORY_CREATESWAPCHAIN Real_Factory_CreateSwapChain = 0;
static PSWAPCHAIN_PRESENT Real_SwapChain_Present = 0;

static HRESULT __stdcall Mine_SwapChain_Present(IDXGISwapChain *me,UINT SyncInterval,UINT Flags)
{
  ID3D10Device *device = 0;
  ID3D10Texture2D *tex = 0, *captureTex = 0;

  if(params.CaptureVideo && SUCCEEDED(me->GetBuffer(0,IID_ID3D10Texture2D,(void**) &tex)))
  {
    D3D10_TEXTURE2D_DESC desc;
    tex->GetDevice(&device);
    tex->GetDesc(&desc);

    // re-creating the capture staging texture each frame is definitely not the most efficient
    // way to handle things, but it frees me of all kind of resource management trouble, so
    // here goes...
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.SampleDesc.Count = 1;
    desc.SampleDesc.Quality = 0;
    desc.Usage = D3D10_USAGE_STAGING;
    desc.BindFlags = 0;
    desc.CPUAccessFlags = D3D10_CPU_ACCESS_READ;
    desc.MiscFlags = 0;

    if(FAILED(device->CreateTexture2D(&desc,0,&captureTex)))
      printLog("video/d3d10: couldn't create staging texture for gpu->cpu download!\n");
    else
      setCaptureResolution(desc.Width,desc.Height);

    if(device)
      device->CopySubresourceRegion(captureTex,0,0,0,0,tex,0,0);

    D3D10_MAPPED_TEXTURE2D mapped;
    bool grabOk = false;

    if(captureTex && SUCCEEDED(captureTex->Map(0,D3D10_MAP_READ,0,&mapped)))
    {
      switch(desc.Format)
      {
      case DXGI_FORMAT_R8G8B8A8_UNORM:
        blitAndFlipRGBAToCaptureData((unsigned char *) mapped.pData,mapped.RowPitch);
        grabOk = true;
        break;

      default:
        printLog("video/d3d10: unsupported backbuffer format, can't grab pixels!\n");
        break;
      }

      captureTex->Unmap(0);
    }

    tex->Release();
    if(captureTex) captureTex->Release();
    if(device) device->Release();

    if(grabOk)
      encoder->WriteFrame(captureData);
  }

  HRESULT hr = Real_SwapChain_Present(me,0,Flags);

  nextFrame();
  return hr;
}

static HRESULT __stdcall Mine_Factory_CreateSwapChain(IUnknown *me,IUnknown *dev,DXGI_SWAP_CHAIN_DESC *desc,IDXGISwapChain **chain)
{
  HRESULT hr = Real_Factory_CreateSwapChain(me,dev,desc,chain);
  if(SUCCEEDED(hr))
  {
    printLog("video/d3d10: swap chain created.\n");
    Real_SwapChain_Present = (PSWAPCHAIN_PRESENT) DetourCOM(*chain,8,(PBYTE) Mine_SwapChain_Present);
  }

  return hr;
}

static HRESULT __stdcall Mine_CreateDXGIFactory(REFIID riid,void **ppFactory)
{
  HRESULT hr = Real_CreateDXGIFactory(riid,ppFactory);
  if(SUCCEEDED(hr) && riid == IID_IDXGIFactory)
  {
    IUnknown *factory = (IUnknown *) *ppFactory;
    Real_Factory_CreateSwapChain = (PFACTORY_CREATESWAPCHAIN) DetourCOM(factory,10,(PBYTE) Mine_Factory_CreateSwapChain);
  }

  return hr;
}

void initVideo_Direct3D10()
{
  HMODULE dxgi = LoadLibraryA("dxgi.dll");
  if(dxgi)
  {
    Real_CreateDXGIFactory = (PCREATEDXGIFACTORY) DetourFunction((PBYTE) GetProcAddress(dxgi,"CreateDXGIFactory"),(PBYTE) Mine_CreateDXGIFactory);
  }
}