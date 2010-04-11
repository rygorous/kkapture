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

static void captureGDIFrame(unsigned char *buffer, int xres, int yres, CONST BITMAPINFO *bmi)
{
  setCaptureResolution(xres,yres);
  if(!captureData || !encoder)
    return;

  GenericBlitter blit;
  int srcPitch = ((bmi->bmiHeader.biBitCount + 7) / 8) * bmi->bmiHeader.biWidth;
  int dstPitch = captureWidth * 3;

  printLog("video: captureGDIFrame xres=%d yres=%d srcPitch=%d dstPitch=%d\n",
    xres,yres,srcPitch,dstPitch);

  if(bmi->bmiHeader.biCompression == BI_RGB)
  {
    switch(bmi->bmiHeader.biBitCount)
    {
    case 32:  blit.SetRGBFormat(32,0xff0000,0x00ff00,0x0000ff); break;
    case 24:  blit.SetRGBFormat(24,0xff0000,0x00ff00,0x0000ff); break;
    case 16:  blit.SetRGBFormat(16,0x7c00,0x3e0,0x01f); break;
    case 8:
      blit.SetPalettedFormat(8);
      blit.SetPalette((PALETTEENTRY*) bmi->bmiColors,bmi->bmiHeader.biClrUsed);
      break;
    default:
      return; // unsupported format
    }
  }
  else if(bmi->bmiHeader.biCompression == BI_BITFIELDS)
  {
    const DWORD *masks = (const DWORD*) bmi->bmiColors;

    switch(bmi->bmiHeader.biBitCount)
    {
    case 32:  blit.SetRGBFormat(32,masks[0],masks[1],masks[2]); break;
    case 16:  blit.SetRGBFormat(16,masks[0],masks[1],masks[2]); break;
    default:  return; // unsupported format
    }
  }
  else
    return; // unsupported format

  bool flip = bmi->bmiHeader.biHeight < 0;
  for(int y=0;y<yres;y++)
  {
    unsigned char *dst = captureData + (flip ? (yres-1-y) : y) * dstPitch;
    unsigned char *src = buffer + y * srcPitch;

    blit.BlitOneLine(src,dst,xres);
  }

  // encode
  encoder->WriteFrame(captureData);
}

  // heuristic: if it's smaller than a certain size, we're not interested.
  // hacky, but otherwise we get *tons* of GDI traffic in some cases.
static bool isInterestingSize(int xRes,int yRes)
{
  return xRes >= 160 && yRes >= 120;
}

static void processDIBits(unsigned char *buffer,int xres,int yres,int xsrc,int ysrc,const BITMAPINFO *bmi)
{
  if(isInterestingSize(xres,yres))
  {
    if(params.CaptureVideo)
    {
      int bytesPerPix = (bmi->bmiHeader.biBitCount + 7) / 8;
      int srcPitch = bytesPerPix * bmi->bmiHeader.biWidth;

      buffer += ysrc * srcPitch + xsrc * bytesPerPix;
      captureGDIFrame(buffer,xres,yres,bmi);
    }

    nextFrame();
  }
}

static void captureSourceBits(HDC hdc,int xSrc,int ySrc,int xRes,int yRes)
{
  // need to create a temp bitmap so we can get the HBITMAP of the real data
  HBITMAP tempBitmap = CreateCompatibleBitmap(hdc,1,1);
  HBITMAP srcBitmap = SelectBitmap(hdc,tempBitmap);

  // get bitmap info
  BITMAPINFO bmi;
  memset(&bmi,0,sizeof(bmi));
  bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);

  GetDIBits(hdc,srcBitmap,0,0,0,&bmi,DIB_RGB_COLORS);

  printLog("video/gdi: %dx%d pixels, depth %d\n",bmi.bmiHeader.biWidth,
    bmi.bmiHeader.biHeight,bmi.bmiHeader.biBitCount);

  // allocate a temp bitmap buffer and get the bits themselves
  // then let processDIBits handle it.
  unsigned char *temp = new unsigned char[bmi.bmiHeader.biSizeImage];
  GetDIBits(hdc,srcBitmap,0,abs(bmi.bmiHeader.biHeight),temp,&bmi,DIB_RGB_COLORS);
  processDIBits(temp,xRes,yRes,xSrc,ySrc,&bmi);
  delete[] temp;

  // restore old bitmap and destroy temp bitmap
  SelectBitmap(hdc,srcBitmap);
  DeleteBitmap(tempBitmap);
}

// trampolines

DETOUR_TRAMPOLINE(int __stdcall Real_SetDIBitsToDevice(HDC hdc, 
          int XDest, int YDest, DWORD dwWidth,  DWORD dwHeight,
          int XSrc, int YSrc, UINT uStartScan, UINT cScanLines,
          CONST VOID *lpvBits, CONST BITMAPINFO *lpbmi, UINT fuColorUse), SetDIBitsToDevice);
DETOUR_TRAMPOLINE(int __stdcall Real_StretchDIBits(HDC hdc,
          int XDest, int YDest, int nDestWidth, int nDestHeight,
          int XSrc, int YSrc, int nSrcWidth, int nSrcHeight,
          CONST VOID *lpvBits, CONST BITMAPINFO *lpbmi, UINT iUsage, DWORD dwRop), StretchDIBits);
DETOUR_TRAMPOLINE(BOOL __stdcall Real_BitBlt(HDC hdcDest,
          int XDest, int YDest, int nDestWidth, int nDestHeight,
          HDC hdcSrc,
          int XSrc, int YSrc, DWORD dwRop), BitBlt);
DETOUR_TRAMPOLINE(BOOL __stdcall Real_StretchBlt(HDC hdcDest,
          int XDest, int YDest, int nDestWidth, int nDestHeight,
          HDC hdcSrc,
          int XSrc, int YSrc, int nSrcWidth, int nSrcHeight,DWORD dwRop), StretchBlt);

static int __stdcall Mine_SetDIBitsToDevice(HDC hdc, int xo, int yo, DWORD xres, DWORD yres, 
        int c, int d, UINT e, UINT f, CONST VOID *buffer, CONST BITMAPINFO *bmi, UINT flag)
{
  int ret = Real_SetDIBitsToDevice(hdc,xo,yo,xres,yres,c,d,e,f,buffer,bmi,flag);
  if(ret != 0)
    processDIBits((unsigned char*)buffer,xres,yres,0,0,bmi);
  return ret;
}

static int __stdcall Mine_StretchDIBits(HDC hdc, int xd, int yd, int ndw, int ndh,
        int xsrc, int ysrc, int xres, int yres, CONST VOID *buffer, CONST BITMAPINFO *bmi, UINT iUsage, DWORD dwRop)
{
  int ret = Real_StretchDIBits(hdc,xd,yd,ndw,ndh,xsrc,ysrc,xres,yres,buffer,bmi,iUsage,dwRop);
  if(ret != GDI_ERROR && dwRop == SRCCOPY) // if it uses other ROPs, it's not something we're interested in
    processDIBits((unsigned char*)buffer,xres,yres,xsrc,ysrc,bmi);

  return ret;
}

static BOOL __stdcall Mine_BitBlt(HDC hdcDest,
        int XDest, int YDest, int nDestWidth, int nDestHeight,
        HDC hdcSrc,
        int XSrc, int YSrc, DWORD dwRop)
{
  BOOL ok = Real_BitBlt(hdcDest,XDest,YDest,nDestWidth,nDestHeight,hdcSrc,XSrc,YSrc,dwRop);
  if(dwRop == SRCCOPY && isInterestingSize(nDestWidth,nDestHeight))
    captureSourceBits(hdcSrc,XSrc,YSrc,nDestWidth,nDestHeight);

  return ok;
}


BOOL __stdcall Mine_StretchBlt(HDC hdcDest,
        int XDest, int YDest, int nDestWidth, int nDestHeight,
        HDC hdcSrc,
        int XSrc, int YSrc, int nSrcWidth, int nSrcHeight, DWORD dwRop)
{
  BOOL ok = Real_StretchBlt(hdcDest,XDest,YDest,nDestWidth,nDestHeight,hdcSrc,XSrc,YSrc,nSrcWidth,nSrcHeight,dwRop);
  if(ok && dwRop == SRCCOPY && isInterestingSize(nSrcWidth,nSrcHeight))
    captureSourceBits(hdcSrc,XSrc,YSrc,nSrcWidth,nSrcHeight);

  return ok;
}

void initVideo_GDI()
{
  if(params.EnableGDICapture)
  {
    DetourFunctionWithTrampoline((PBYTE) Real_SetDIBitsToDevice,(PBYTE) Mine_SetDIBitsToDevice);
    DetourFunctionWithTrampoline((PBYTE) Real_StretchDIBits,(PBYTE) Mine_StretchDIBits);
    DetourFunctionWithTrampoline((PBYTE) Real_BitBlt,(PBYTE) Mine_BitBlt);
    DetourFunctionWithTrampoline((PBYTE) Real_StretchBlt,(PBYTE) Mine_StretchBlt);
  }
}
