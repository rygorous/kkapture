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
#include "bmp_videoencoder.h"

// internal data
struct BMPVideoEncoder::Internal
{
  BITMAPFILEHEADER bmfh;
  BITMAPINFOHEADER bmih;
  WAVEFORMATEX *wfx;
  FILE *wave;
};

BMPVideoEncoder::BMPVideoEncoder(const char *fileName)
{
  char drive[_MAX_DRIVE],dir[_MAX_DIR],fname[_MAX_FNAME],ext[_MAX_EXT];

  _splitpath(fileName,drive,dir,fname,ext);
  _makepath(prefix,drive,dir,fname,"");
  xRes = yRes = 0;
  
  intn = new Internal;
  intn->wave = 0;
  intn->wfx = 0;

  ZeroMemory(&intn->bmfh,sizeof(BITMAPFILEHEADER));
  ZeroMemory(&intn->bmih,sizeof(BITMAPINFOHEADER));

  intn->bmfh.bfType = 0x4d42;
  intn->bmfh.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);
  intn->bmfh.bfSize = intn->bmfh.bfOffBits;

  intn->bmih.biSize = sizeof(BITMAPINFOHEADER);
  intn->bmih.biPlanes = 1;
  intn->bmih.biBitCount = 24;
  intn->bmih.biCompression = BI_RGB;
}

BMPVideoEncoder::~BMPVideoEncoder()
{
  if(intn->wave)
  {
    // finish the wave file by writing overall and data chunk lengths
    long fileLen = ftell(intn->wave);

    long riffLen = fileLen - 8;
    fseek(intn->wave,4,SEEK_SET);
    fwrite(&riffLen,1,sizeof(long),intn->wave);

    long dataLen = fileLen - 44;
    fseek(intn->wave,40,SEEK_SET);
    fwrite(&dataLen,1,sizeof(long),intn->wave);

    fclose(intn->wave);
  }

  delete[] (unsigned char *) intn->wfx;
  delete intn;
}

void BMPVideoEncoder::SetSize(int _xRes,int _yRes)
{
  xRes = _xRes;
  yRes = _yRes;

  intn->bmfh.bfSize = xRes * yRes * 3 + intn->bmfh.bfOffBits;
  intn->bmih.biWidth = xRes;
  intn->bmih.biHeight = yRes;

  frame = 0;
}

void BMPVideoEncoder::WriteFrame(const unsigned char *buffer)
{
  if(xRes && yRes)
  {
    // create filename
    char filename[512];
    _snprintf_s(filename,sizeof(filename)/sizeof(*filename),"%s%04d.bmp",prefix,frame);

    // create file, write headers+image
    FILE *f = fopen(filename,"wb");
    fwrite(&intn->bmfh,sizeof(BITMAPFILEHEADER),1,f);
    fwrite(&intn->bmih,sizeof(BITMAPINFOHEADER),1,f);
    fwrite(buffer,xRes*yRes,3,f);
    fclose(f);

    // frame is finished.
    frame++;
  }
}

void BMPVideoEncoder::SetAudioFormat(const tWAVEFORMATEX *fmt)
{
  if(params.CaptureAudio && !intn->wave)
  {
    char filename[512];
    strcpy_s(filename,prefix);
    strcat_s(filename,".wav");

    delete intn->wfx;
    intn->wfx = CopyFormat(fmt);
    intn->wave = fopen(filename,"wb");
    
    if(intn->wave)
    {
      static unsigned char header[] = "RIFF\0\0\0\0WAVEfmt ";
      static unsigned char data[] = "data\0\0\0\0";

      fwrite(header,1,sizeof(header)-1,intn->wave);
      DWORD len = fmt->cbSize ? sizeof(WAVEFORMATEX)+fmt->cbSize : sizeof(WAVEFORMATEX)-2;
      fwrite(&len,1,sizeof(DWORD),intn->wave);
      fwrite(fmt,1,len,intn->wave);
      fwrite(data,1,sizeof(data)-1,intn->wave);
    }

    // fill already written frames with no sound
    unsigned char *buffer = new unsigned char[fmt->nBlockAlign * 1024];
    int sampleFill = MulDiv(frame,fmt->nSamplesPerSec*frameRateDenom,frameRateScaled);

    memset(buffer,0,fmt->nBlockAlign * 1024);
    for(int samplePos=0;samplePos<sampleFill;samplePos+=1024)
      WriteAudioFrame(buffer,min(sampleFill-samplePos,1024));
  }
}

WAVEFORMATEX *BMPVideoEncoder::GetAudioFormat()
{
  if(!intn->wave || !intn->wfx) return 0;
  return CopyFormat(intn->wfx);
}

void BMPVideoEncoder::WriteAudioFrame(const void *buffer,int samples)
{
  if(intn->wave)
    fwrite(buffer,intn->wfx->nBlockAlign,samples,intn->wave);
}