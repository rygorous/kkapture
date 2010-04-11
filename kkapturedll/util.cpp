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

// ---- logging functions

static FILE *logFile = 0;

void initLog()
{
  TCHAR logname[_MAX_PATH+32];
  GetModuleFileName(GetModuleHandle(0),logname,_MAX_PATH);
  _tcscat_s(logname,_T(".kklog.txt"));

  logFile = _tfopen(logname,_T("w"));
}

void closeLog()
{
  if(logFile)
  {
    fclose(logFile);
    logFile = 0;
  }
}

void printLog(const char *format,...)
{
  if(logFile)
  {
    va_list arg;
    va_start(arg,format);
    vfprintf(logFile,format,arg);
    va_end(arg);

    fflush(logFile);
  }
}

void printLogHex(void *buffer,int size)
{
  static const char hex2ch[] = "0123456789ABCDEF";

  unsigned char *p = (unsigned char*) buffer;
  char linebuf[128]; // yes, this is enough.

  // process in lines of 16 bytes each
  while(size)
  {
    int bytes = min(size,16);
    memset(linebuf,' ',sizeof(linebuf));
    linebuf[16*3] = '|';

    // prepare hexdump of one line
    for(int i=0;i<bytes;i++)
    {
      int byte = p[i];

      linebuf[i*3+0] = hex2ch[byte >> 4];
      linebuf[i*3+1] = hex2ch[byte & 15];
      linebuf[16*3+2+i] = (byte >= 32 && byte <= 126) ? byte : '.';
    }

    // finish and print it
    linebuf[16*3+2+bytes] = 0;
    printLog("%s\n",linebuf);

    p += bytes;
    size -= bytes;
  }
}

// ---- vtable patching

PBYTE DetourCOM(IUnknown *obj,int vtableOffs,PBYTE newFunction)
{
  PBYTE **vtblPtr = (PBYTE **) obj;
  PBYTE *vtbl = *vtblPtr;
  PBYTE target = vtbl[vtableOffs];
  
  return DetourFunction(target,newFunction);
}

// ---- long integer arithmetic

DWORD UMulDiv(DWORD a,DWORD b,DWORD c)
{
  __asm
  {
    mov   eax, [a];
    mul   [b];
    div   [c];
    mov   [a], eax;
  }

  return a;
}

ULONGLONG ULongMulDiv(ULONGLONG a,DWORD b,DWORD c)
{
  __asm
  {
    mov   eax, dword ptr [a+4];
    mul   [b];
    mov   ecx, edx;             // ecx=top 32 bits
    mov   ebx, eax;             // ebx=middle 32 bits
    mov   eax, dword ptr [a];
    mul   [b];
    add   ebx, edx;

    // now eax=lower 32 bits, ebx=middle 32 bits, ecx=top 32 bits
    mov   edx, ecx;
    xchg  eax, ebx;
    div   [c];                  // high divide
    mov   dword ptr [a+4], eax;
    mov   eax, ebx;
    div   [c];                  // low divide
    mov   dword ptr [a], eax;
  }

  return a;
}

// ---- really misc stuff

void *MakeCopy(const void *src,int size)
{
  unsigned char *buffer = new unsigned char[size];
  memcpy(buffer,src,size);
  return buffer;
}

WAVEFORMATEX *CopyFormat(const WAVEFORMATEX *src)
{
  if(!src)
    return 0;

  // try to simplify audio format if possible
  if(src->wFormatTag == WAVE_FORMAT_EXTENSIBLE)
  {
    static unsigned char WaveFormatTag[8] = { 0x80,0x00,0x00,0xaa,0x00,0x38,0x9b,0x71 };

    WAVEFORMATEXTENSIBLE *wfe = (WAVEFORMATEXTENSIBLE *) src;
    if(wfe->Samples.wValidBitsPerSample == wfe->Format.wBitsPerSample
      && wfe->dwChannelMask == (1 << wfe->Format.nChannels)-1
      && wfe->SubFormat.Data1 < 0x10000
      && wfe->SubFormat.Data2 == 0x0000 && wfe->SubFormat.Data3 == 0x0010
      && memcmp(wfe->SubFormat.Data4,WaveFormatTag,8)==0)
    {
      // just drop the WAVEFORMATEXTENSIBLE part entirely
      WAVEFORMATEX *out = new WAVEFORMATEX;
      memcpy(out,src,sizeof(WAVEFORMATEX));
      out->wFormatTag = (WORD) wfe->SubFormat.Data1;
      out->cbSize = 0;
      return out;
    }
  }

  int size = sizeof(WAVEFORMATEX)+src->cbSize;
  if(src->wFormatTag == WAVE_FORMAT_PCM) // a common bug is leaving cbSize uninitialized. work around it.
    size = sizeof(WAVEFORMATEX);

  // general case
  unsigned char *buffer = new unsigned char[size];
  memcpy(buffer,src,size);

  // modify the format, if necessary.
  WAVEFORMATEX *outFmt = (WAVEFORMATEX*) buffer;
  if(src->wFormatTag == WAVE_FORMAT_PCM) // workaround part two
    outFmt->cbSize = 0;

  return outFmt;
}

WAVEFORMATEX *BounceFormat(const WAVEFORMATEX *src)
{
  WAVEFORMATEX *out = CopyFormat(src);

  // downmix 32bit float data to 16bit pcm since the former is mostly unsupported
  if(out && out->wFormatTag == WAVE_FORMAT_IEEE_FLOAT && out->wBitsPerSample == 32)
  {
    printLog("audio: downmixing 32bit float audio to 16bit pcm\n");

    out->wFormatTag = WAVE_FORMAT_PCM;
    out->wBitsPerSample = 16;
    out->nBlockAlign = out->nChannels * 2;
    out->nAvgBytesPerSec = out->nSamplesPerSec * out->nBlockAlign;
  }

  return out;
}