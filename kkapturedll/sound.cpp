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
#include <malloc.h>
#include "videoencoder.h"
#include "util.h"
#include "video.h"
#include <psapi.h>
#define DIRECTSOUND_VERSION 0x0800
#include <dsound.h>

#pragma comment(lib,"dsound.lib")
#pragma comment(lib,"dxguid.lib")
#pragma comment(lib,"winmm.lib")
#pragma comment(lib,"psapi.lib")

// if waveOutGetPosition is called frequently in a single frame, assume the app is waiting for the
// current playback position to change and advance the time. this is the threshold for "frequent" calls.
static const int MAX_GETPOSITION_PER_FRAME = 1024;

// my own directsound fake!
class MyDirectSound8;
class MyDirectSoundBuffer8;

static MyDirectSoundBuffer8 *playBuffer = 0;

class LockOwner
{
  CRITICAL_SECTION &section;

public:
  LockOwner(CRITICAL_SECTION &cs)
    : section(cs)
  {
    EnterCriticalSection(&section);
  }

  ~LockOwner()
  {
    LeaveCriticalSection(&section);
  }
};

class MyDirectSound3DListener8 : public IDirectSound3DListener8
{
  int RefCount;

public:
  MyDirectSound3DListener8()
    : RefCount(1)
  {
  }

  // IUnknown methods
  virtual HRESULT __stdcall QueryInterface(REFIID iid,LPVOID *ptr)
  {
    if(iid == IID_IDirectSound3DListener || iid == IID_IDirectSound3DListener8)
    {
      *ptr = this;
      AddRef();
      return S_OK;
    }
    else
      return E_NOINTERFACE;
  }

  virtual ULONG __stdcall AddRef()
  {
    return ++RefCount;
  }

  virtual ULONG __stdcall Release()
  {
    ULONG ret = --RefCount;
    if(!RefCount)
      delete this;

    return ret;
  }

  // IDirectSound3DListener methods
  virtual HRESULT __stdcall GetAllParameters(LPDS3DLISTENER pListener)
  {
    return E_NOTIMPL;
  }

  virtual HRESULT __stdcall GetDistanceFactor(D3DVALUE* pflDistanceFactor)
  {
    return E_NOTIMPL;
  }

  virtual HRESULT __stdcall GetDopplerFactor(D3DVALUE* pflDistanceFactor)
  {
    return E_NOTIMPL;
  }

  virtual HRESULT __stdcall GetOrientation(D3DVECTOR* pvOrientFront, D3DVECTOR* pvOrientTop)
  {
    return E_NOTIMPL;
  }

  virtual HRESULT __stdcall GetPosition(D3DVECTOR* pvPosition)
  {
    return E_NOTIMPL;
  }

  virtual HRESULT __stdcall GetRolloffFactor(D3DVALUE* pflRolloffFactor)
  {
    return E_NOTIMPL;
  }

  virtual HRESULT __stdcall GetVelocity(D3DVECTOR* pvVelocity)
  {
    return E_NOTIMPL;
  }

  virtual HRESULT __stdcall SetAllParameters(LPCDS3DLISTENER pcListener,DWORD dwApply)
  {
    return E_NOTIMPL;
  }

  virtual HRESULT __stdcall SetDistanceFactor(D3DVALUE flDistanceFactor,DWORD dwApply)
  {
    return E_NOTIMPL;
  }

  virtual HRESULT __stdcall SetDopplerFactor(D3DVALUE flDopplerFactor,DWORD dwApply)
  {
    return E_NOTIMPL;
  }

  virtual HRESULT __stdcall SetOrientation(D3DVALUE xFront,D3DVALUE yFront,D3DVALUE zFront,D3DVALUE xTop,D3DVALUE yTop,D3DVALUE zTop,DWORD dwApply)
  {
    return E_NOTIMPL;
  }

  virtual HRESULT __stdcall SetPosition(D3DVALUE x,D3DVALUE y,D3DVALUE z,DWORD dwApply)
  {
    return E_NOTIMPL;
  }

  virtual HRESULT __stdcall SetRolloffFactor(D3DVALUE flRolloffFactor,DWORD dwApply)
  {
    return E_NOTIMPL;
  }

  virtual HRESULT __stdcall SetVelocity(D3DVALUE x,D3DVALUE y,D3DVALUE z,DWORD dwApply)
  {
    return E_NOTIMPL;
  }

  virtual HRESULT __stdcall CommitDeferredSettings()
  {
    return S_OK;
  }
};

class MyDirectSound3DBuffer8 : public IDirectSound3DBuffer8
{
  int RefCount;

public:
  MyDirectSound3DBuffer8()
    : RefCount(1)
  {
  }

  // IUnknown methods
  virtual HRESULT __stdcall QueryInterface(REFIID iid,LPVOID *ptr)
  {
    if(iid == IID_IDirectSound3DBuffer || iid == IID_IDirectSound3DListener8)
    {
      *ptr = this;
      AddRef();
      return S_OK;
    }
    else
      return E_NOINTERFACE;
  }

  virtual ULONG __stdcall AddRef()
  {
    return ++RefCount;
  }

  virtual ULONG __stdcall Release()
  {
    ULONG ret = --RefCount;
    if(!RefCount)
      delete this;

    return ret;
  }

  // IDirectSound3DBuffer methods
  virtual HRESULT __stdcall GetAllParameters(LPDS3DBUFFER pDs3dBuffer)
  {
    return E_NOTIMPL;
  }

  virtual HRESULT __stdcall GetConeAngles(LPDWORD pdwInsideConeAngle, LPDWORD pdwOutsideConeAngle)
  {
    return E_NOTIMPL;
  }

  virtual HRESULT __stdcall GetConeOrientation(D3DVECTOR *pvOrientation)
  {
    return E_NOTIMPL;
  }

  virtual HRESULT __stdcall GetConeOutsideVolume(LPLONG plConeOutsideVolume)
  {
    return E_NOTIMPL;
  }

  virtual HRESULT __stdcall GetMaxDistance(D3DVALUE *pflMaxDistance)
  {
    return E_NOTIMPL;
  }

  virtual HRESULT __stdcall GetMinDistance(D3DVALUE *pflMaxDistance)
  {
    return E_NOTIMPL;
  }

  virtual HRESULT __stdcall GetMode(LPDWORD pdwMode)
  {
    return E_NOTIMPL;
  }

  virtual HRESULT __stdcall GetPosition(D3DVECTOR *pvPosition)
  {
    return E_NOTIMPL;
  }

  virtual HRESULT __stdcall GetVelocity(D3DVECTOR *pvVelocity)
  {
    return E_NOTIMPL;
  }

  virtual HRESULT __stdcall SetAllParameters(LPCDS3DBUFFER pDs3dBuffer, DWORD dwApply)
  {
    return E_NOTIMPL;
  }

  virtual HRESULT __stdcall SetConeAngles(DWORD dwInsideConeAngle, DWORD dwOutsideConeAngle, DWORD dwApply)
  {
    return E_NOTIMPL;
  }

  virtual HRESULT __stdcall SetConeOrientation(D3DVALUE x, D3DVALUE y, D3DVALUE z, DWORD dwApply)
  {
    return E_NOTIMPL;
  }

  virtual HRESULT __stdcall SetConeOutsideVolume(LONG lConeOutsideVolume, DWORD dwApply)
  {
    return E_NOTIMPL;
  }

  virtual HRESULT __stdcall SetMaxDistance(D3DVALUE flMaxDistance, DWORD dwApply)
  {
    return E_NOTIMPL;
  }

  virtual HRESULT __stdcall SetMinDistance(D3DVALUE flMaxDistance, DWORD dwApply)
  {
    return E_NOTIMPL;
  }

  virtual HRESULT __stdcall SetMode(DWORD dwMode, DWORD dwApply)
  {
    return E_NOTIMPL;
  }
  
  virtual HRESULT __stdcall SetPosition(D3DVALUE x, D3DVALUE y, D3DVALUE z, DWORD dwApply)
  {
    return E_NOTIMPL;
  }

  virtual HRESULT __stdcall SetVelocity(D3DVALUE x, D3DVALUE y, D3DVALUE z, DWORD dwApply)
  {
    return E_NOTIMPL;
  }
};

class MyDirectSoundBuffer8 : public IDirectSoundBuffer8
{
  int RefCount;
  PBYTE Buffer;
  DWORD Flags;
  DWORD Bytes;
  WAVEFORMATEX *Format;
  DWORD Frequency;
  BOOL Playing,Looping;
  LONG Volume;
  LONG Panning;
  CRITICAL_SECTION BufferLock;
  DWORD PlayCursor;
  BOOL SkipAllowed;
  DWORD SamplesPlayed;
  DWORD GetPosThisFrame;
  int FirstFrame;

  DWORD NextFrameSize()
  {
    DWORD frame = getFrameTiming() - FirstFrame;
    DWORD samplePos = UMulDiv(frame,Frequency * frameRateDenom,frameRateScaled);
    DWORD bufferPos = samplePos * Format->nBlockAlign;
    DWORD nextSize = bufferPos - SamplesPlayed;

    return nextSize;
  }

  DWORD WriteCursor()
  {
    if(!Playing)
      return 0;

    return (PlayCursor + 128 * Format->nBlockAlign) % Bytes;
  }

public:
  MyDirectSoundBuffer8(DWORD flags,DWORD bufBytes,LPWAVEFORMATEX fmt)
    : RefCount(1)
  {
    Flags = flags;
    Buffer = new BYTE[bufBytes];
    Bytes = bufBytes;
    memset(Buffer,0,bufBytes);

    if(fmt)
      Format = CopyFormat(fmt);
    else
    {
      Format = new WAVEFORMATEX;
      Format->wFormatTag = WAVE_FORMAT_PCM;
      Format->nChannels = 2;
      Format->nSamplesPerSec = 44100;
      Format->nAvgBytesPerSec = 176400;
      Format->nBlockAlign = 4;
      Format->wBitsPerSample = 16;
      Format->cbSize = 0;
    }

    Frequency = Format->nSamplesPerSec;

    Playing = FALSE;
    Looping = FALSE;
    Volume = 0;
    Panning = 0;
    PlayCursor = 0;
    SkipAllowed = FALSE;
    SamplesPlayed = 0;
    GetPosThisFrame = 0;

    InitializeCriticalSection(&BufferLock);
  }

  ~MyDirectSoundBuffer8()
  {
    // synchronize access to bufferlock before deleting the section
    {
      LockOwner sync(BufferLock);
    }

    DeleteCriticalSection(&BufferLock);
    delete Format;
    delete[] Buffer;
  }

  // IUnknown methods
  virtual HRESULT __stdcall QueryInterface(REFIID iid,LPVOID *ptr)
  {
    if(iid == IID_IDirectSoundBuffer || iid == IID_IDirectSoundBuffer8)
    {
      *ptr = this;
      AddRef();
      return S_OK;
    }
    else if(iid == IID_IDirectSound3DListener || iid == IID_IDirectSound3DListener8)
    {
      *ptr = new MyDirectSound3DListener8;
      return S_OK;
    }
    else if(iid == IID_IDirectSound3DBuffer || iid == IID_IDirectSound3DBuffer8)
    {
      *ptr = new MyDirectSound3DBuffer8;
      return S_OK;
    }
    else
      return E_NOINTERFACE;
  }

  virtual ULONG __stdcall AddRef()
  {
    return ++RefCount;
  }

  virtual ULONG __stdcall Release()
  {
    ULONG ret = --RefCount;
    if(!RefCount)
      delete this;

    return ret;
  }

  // IDirectSoundBuffer methods
  virtual HRESULT __stdcall GetCaps(LPDSBCAPS caps)
  {
    if(caps && caps->dwSize == sizeof(DSBCAPS))
    {
      caps->dwFlags = Flags;
      caps->dwBufferBytes = Bytes;
      caps->dwUnlockTransferRate = 400 * 1024;
      caps->dwPlayCpuOverhead = 0;

      return S_OK;
    }
    else
    {
      printLog("dsound: DirectSoundBuffer::GetCaps - invalid size\n");
      return DSERR_INVALIDPARAM;
    }
  }

  virtual HRESULT __stdcall GetCurrentPosition(LPDWORD pdwCurrentPlayCursor,LPDWORD pdwCurrentWriteCursor)
  {
    LockOwner lock(BufferLock);

    if(++GetPosThisFrame >= MAX_GETPOSITION_PER_FRAME) // assume that the app is waiting for the playback position to change.
    {
      printLog("sound: app is hammering dsound GetCurrentPosition, advancing time (frame=%d)\n",getFrameTiming());
      GetPosThisFrame = 0;
      LeaveCriticalSection(&BufferLock);
      skipFrame();
      EnterCriticalSection(&BufferLock);
    }

    // skip some milliseconds of silence at start
    if(SkipAllowed)
    {
      DWORD maxskip = UMulDiv(Format->nSamplesPerSec,Format->nBlockAlign*params.SoundMaxSkip,1000);
      DWORD pp = PlayCursor;
      DWORD i;

      // find out whether the next maxskip bytes are zero
      for(i=0;i<maxskip;i++)
      {
        if(Buffer[pp])
          break;

        if(++pp == Bytes)
          pp = 0;
      }

      // yes they are, skip them
      if(i && i == maxskip)
        PlayCursor = pp;
      else
        SkipAllowed = FALSE;
    }

    if(!Playing) // not playing, report zeroes
    {
      if(pdwCurrentPlayCursor)
        *pdwCurrentPlayCursor = 0;

      if(pdwCurrentWriteCursor)
        *pdwCurrentWriteCursor = 0;
    }
    else // playing, so report current positions
    {
      //// the "track one" hack!
      //if(params.FairlightHack && !Looping && ++GetPosThisFrame > 128)
      //{
      //  Stop();
      //  PlayCursor = Bytes;
      //}

      if(pdwCurrentPlayCursor)
        *pdwCurrentPlayCursor = PlayCursor;

      if(pdwCurrentWriteCursor)
        *pdwCurrentWriteCursor = WriteCursor();
    }

    return S_OK;
  }

  virtual HRESULT __stdcall GetFormat(LPWAVEFORMATEX pwfxFormat, DWORD dwSizeAllocated, LPDWORD pdwSizeWritten)
  {
    int size = min(dwSizeAllocated,Format ? sizeof(WAVEFORMATEX) + Format->cbSize : 0);

    if(pdwSizeWritten)
      *pdwSizeWritten = size;

    if(pwfxFormat)
      memcpy(pwfxFormat,Format,size);

    return S_OK;
  }

  virtual HRESULT __stdcall GetVolume(LPLONG plVolume)
  {
    if(plVolume)
      *plVolume = Volume;

    return S_OK;
  }

  virtual HRESULT __stdcall GetPan(LPLONG plPan)
  {
    if(plPan)
      *plPan = Panning;

    return S_OK;
  }

  virtual HRESULT __stdcall GetFrequency(LPDWORD pdwFrequency)
  {
    if(pdwFrequency)
      *pdwFrequency = Frequency;

    return S_OK;
  }

  virtual HRESULT __stdcall GetStatus(LPDWORD pdwStatus)
  {
    if(pdwStatus)
    {
      *pdwStatus = 0;
      *pdwStatus |= Playing ? DSBSTATUS_PLAYING : 0;
      *pdwStatus |= Looping ? DSBSTATUS_LOOPING : 0;
    }

    return DS_OK;
  }

  virtual HRESULT __stdcall Initialize(LPDIRECTSOUND pDirectSound, LPCDSBUFFERDESC pcDSBufferDesc)
  {
    return S_OK;
  }

  virtual HRESULT __stdcall Lock(DWORD dwOffset,DWORD dwBytes,LPVOID *ppvAudioPtr1,LPDWORD pdwAudioBytes1,LPVOID *ppvAudioPtr2,LPDWORD pdwAudioBytes2,DWORD dwFlags)
  {
    LockOwner lock(BufferLock);

    if(dwFlags & DSBLOCK_FROMWRITECURSOR)
      dwOffset = WriteCursor();

    if(dwFlags & DSBLOCK_ENTIREBUFFER)
      dwBytes = Bytes;

    if(dwOffset >= Bytes || dwBytes > Bytes)
    {
      LeaveCriticalSection(&BufferLock);
      return DSERR_INVALIDPARAM;
    }

    if(dwOffset + dwBytes <= Bytes) // no wrap
    {
      *ppvAudioPtr1 = (LPVOID) (Buffer + dwOffset);
      *pdwAudioBytes1 = dwBytes;
      if(ppvAudioPtr2)
      {
        *ppvAudioPtr2 = 0;
        *pdwAudioBytes2 = 0;
      }
    }
    else // wrap
    {
      *ppvAudioPtr1 = (LPVOID) (Buffer + dwOffset);
      *pdwAudioBytes1 = Bytes - dwOffset;
      if(ppvAudioPtr2)
      {
        *ppvAudioPtr2 = (LPVOID) Buffer;
        *pdwAudioBytes2 = dwBytes - *pdwAudioBytes1;
      }
    }

    return S_OK;
  }

  virtual HRESULT __stdcall Play(DWORD dwReserved1,DWORD dwPriority,DWORD dwFlags)
  {
    if(!Playing)
      SkipAllowed = TRUE;

    printLog("sound: play\n");

    Playing = TRUE;
    Looping = (dwFlags & DSBPLAY_LOOPING) ? TRUE : FALSE;

    if(!(Flags & DSBCAPS_PRIMARYBUFFER)/* && (!params.FairlightHack || Looping)*/)
    {
      encoder->SetAudioFormat(Format);
      playBuffer = this;
      FirstFrame = getFrameTiming();
    }

    return S_OK;
  }

  virtual HRESULT __stdcall SetCurrentPosition(DWORD dwNewPosition)
  {
    PlayCursor = dwNewPosition;
    return S_OK;
  }

  virtual HRESULT __stdcall SetFormat(LPCWAVEFORMATEX pcfxFormat)
  {
    delete Format;
    Format = CopyFormat(pcfxFormat);
    if(playBuffer==this)
      encoder->SetAudioFormat(Format);

    return S_OK;
  }

  virtual HRESULT __stdcall SetVolume(LONG lVolume)
  {
    Volume = lVolume;
    return S_OK;
  }

  virtual HRESULT __stdcall SetPan(LONG lPan)
  {
    Panning = lPan;
    return S_OK;
  }

  virtual HRESULT __stdcall SetFrequency(DWORD dwFrequency)
  {
    Frequency = dwFrequency;
    Format->nSamplesPerSec = dwFrequency;
    Format->nAvgBytesPerSec = Format->nBlockAlign * dwFrequency;
    if(playBuffer==this)
      encoder->SetAudioFormat(Format);

    return S_OK;
  }

  virtual HRESULT __stdcall Stop()
  {
    Playing = FALSE;
    if(playBuffer == this)
      playBuffer = 0;

    return S_OK;
  }

  virtual HRESULT __stdcall Unlock(LPVOID pvAudioPtr1,DWORD dwAudioBytes1,LPVOID pvAudioPtr2,DWORD dwAudioBytes2)
  {
    return S_OK;
  }

  virtual HRESULT __stdcall Restore()
  {
    return S_OK;
  }

  // IDirectSoundBuffer8 methods
  virtual HRESULT __stdcall SetFX(DWORD dwEffectsCount,LPDSEFFECTDESC pDSFXDesc,LPDWORD pdwResultCodes)
  {
    return E_NOTIMPL;
  }

  virtual HRESULT __stdcall AcquireResources(DWORD dwFlags,DWORD dwEffectsCount,LPDWORD pdwResultCodes)
  {
    return E_NOTIMPL;
  }

  virtual HRESULT __stdcall GetObjectInPath(REFGUID rguidObject,DWORD dwIndex,REFGUID rguidInterface,LPVOID *ppObject)
  {
    return E_NOTIMPL;
  }

  // ----
  void encodeLastFrameAudio()
  {
    LockOwner lock(BufferLock);

    // calculate number of samples processed since last frame, then encode
    DWORD frameSize = NextFrameSize();
    DWORD end = PlayCursor + frameSize;
    DWORD align = Format->nBlockAlign;

    if(end - PlayCursor > Bytes)
    {
      printLog("sound: more samples required per frame than buffer allows, increase frame rate\n");
      end = PlayCursor + Bytes;
    }

    if(end > Bytes) // wrap
    {
      encoder->WriteAudioFrame(Buffer + PlayCursor,(Bytes - PlayCursor) / align);
      encoder->WriteAudioFrame(Buffer,(end - Bytes) / align);
    }
    else // no wrap
      encoder->WriteAudioFrame(Buffer + PlayCursor,(end - PlayCursor) / align);

    PlayCursor = end % Bytes;
    SamplesPlayed += frameSize;
    GetPosThisFrame = 0;
  }
};

class MyDirectSound8 : public IDirectSound8
{
  int RefCount;

public:
  MyDirectSound8()
    : RefCount(1)
  {
    videoNeedEncoder();
  }

  // IUnknown methods
  virtual HRESULT __stdcall QueryInterface(REFIID iid,LPVOID *ptr)
  {
    printLog("sound: dsound queryinterface\n");
    return E_NOINTERFACE;
  }

  virtual ULONG __stdcall AddRef()
  {
    return ++RefCount;
  }

  virtual ULONG __stdcall Release()
  {
    ULONG ret = --RefCount;
    if(!RefCount)
      delete this;

    return ret;
  }

  // IDirectSound methods
  virtual HRESULT __stdcall CreateSoundBuffer(LPCDSBUFFERDESC desc,LPDIRECTSOUNDBUFFER *ppDSBuffer,LPUNKNOWN pUnkOuter)
  {
    if(desc && (desc->dwSize == sizeof(DSBUFFERDESC) || desc->dwSize == sizeof(DSBUFFERDESC1)))
    {
      if(desc->dwFlags & DSBCAPS_LOCHARDWARE) // we certainly don't do hw mixing.
        return DSERR_CONTROLUNAVAIL;

      *ppDSBuffer = new MyDirectSoundBuffer8(desc->dwFlags,desc->dwBufferBytes,desc->lpwfxFormat);
      printLog("sound: buffer created\n");
      return S_OK;
    }
    else
      return DSERR_INVALIDPARAM;
  }

  virtual HRESULT __stdcall GetCaps(LPDSCAPS pDSCaps)
  {
    if(pDSCaps && pDSCaps->dwSize == sizeof(DSCAPS))
    {
      ZeroMemory(pDSCaps,sizeof(DSCAPS));

      pDSCaps->dwSize = sizeof(DSCAPS);
      pDSCaps->dwFlags = DSCAPS_CONTINUOUSRATE
        | DSCAPS_PRIMARY16BIT | DSCAPS_PRIMARY8BIT
        | DSCAPS_PRIMARYMONO | DSCAPS_PRIMARYSTEREO
        | DSCAPS_SECONDARY16BIT | DSCAPS_SECONDARY8BIT
        | DSCAPS_SECONDARYMONO | DSCAPS_SECONDARYSTEREO;
      pDSCaps->dwMinSecondarySampleRate = 4000;
      pDSCaps->dwMaxSecondarySampleRate = 96000;
      pDSCaps->dwPrimaryBuffers = 1;

      return S_OK;
    }
    else
    {
      printLog("sound: DirectSound::GetCaps - invalid size\n");
      return DSERR_INVALIDPARAM;
    }
  }

  virtual HRESULT __stdcall DuplicateSoundBuffer(LPDIRECTSOUNDBUFFER pDSBufferOriginal,LPDIRECTSOUNDBUFFER *ppDSBufferDuplicate)
  {
    printLog("sound: attempting DuplicateSoundBuffer hack...\n");

    if(!ppDSBufferDuplicate)
      return E_INVALIDARG;

    // we don't mix, so simply return a handle to the same buffer and hope it works...
    pDSBufferOriginal->AddRef();
    *ppDSBufferDuplicate = pDSBufferOriginal;
    return S_OK;

    //printLog("sound: DuplicateSoundBuffer attempted (not implemented yet)\n");
    //return E_NOTIMPL;
  }

  virtual HRESULT __stdcall SetCooperativeLevel(HWND hwnd,DWORD dwLevel)
  {
    return S_OK;
  }

  virtual HRESULT __stdcall Compact()
  {
    return S_OK;
  }

  virtual HRESULT __stdcall GetSpeakerConfig(LPDWORD pdwSpeakerConfig)
  {
    *pdwSpeakerConfig = DSSPEAKER_STEREO;
    return S_OK;
  }

  virtual HRESULT __stdcall SetSpeakerConfig(DWORD dwSpeakerConfig)
  {
    printLog("sound: dsound setspeakerconfig\n");
    return E_NOTIMPL;
  }

  virtual HRESULT __stdcall Initialize(LPCGUID pcGuidDevice)
  {
    return S_OK;
  }

  // IDirectSound8 methods
  virtual HRESULT __stdcall VerifyCertification(LPDWORD pdwCertified)
  {
    printLog("sound: dsound verifycertification\n");
    return E_NOTIMPL;
  }
};

// trampolines
DETOUR_TRAMPOLINE(HRESULT __stdcall Real_DirectSoundCreate(LPCGUID lpcGuidDevice,LPDIRECTSOUND *ppDS,LPUNKNOWN pUnkOuter), DirectSoundCreate);
DETOUR_TRAMPOLINE(HRESULT __stdcall Real_DirectSoundCreate8(LPCGUID lpcGuidDevice,LPDIRECTSOUND8 *ppDS8,LPUNKNOWN pUnkOuter), DirectSoundCreate8);

DETOUR_TRAMPOLINE(HRESULT __stdcall Real_CoCreateInstance(REFCLSID rclsid,LPUNKNOWN pUnkOuter,DWORD dwClsContext,REFIID riid,LPVOID *ppv), CoCreateInstance);

HRESULT __stdcall Mine_DirectSoundCreate(LPCGUID lpcGuidDevice,LPDIRECTSOUND8 *ppDS8,LPUNKNOWN pUnkOuter)
{
  printLog("sound: emulating DirectSound\n");
  *ppDS8 = new MyDirectSound8;
  return S_OK;
}

HRESULT __stdcall Mine_DirectSoundCreate8(LPCGUID lpcGuidDevice,LPDIRECTSOUND8 *ppDS8,LPUNKNOWN pUnkOuter)
{
  printLog("sound: emulating DirectSound 8\n");
  *ppDS8 = new MyDirectSound8;
  return S_OK;
}

HRESULT __stdcall Mine_CoCreateInstance(REFCLSID rclsid,LPUNKNOWN pUnkOuter,DWORD dwClsContext,REFIID riid,LPVOID *ppv)
{
  IUnknown **ptr = (IUnknown **) ppv;

  if(rclsid == CLSID_DirectSound && riid == IID_IDirectSound)
  {
    printLog("sound: emulating DirectSound (created via CoCreateInstance)\n");
    *ptr = new MyDirectSound8;
    return S_OK;
  }
  else if(rclsid == CLSID_DirectSound8 && riid == IID_IDirectSound8)
  {
    printLog("sound: emulating DirectSound 8 (created via CoCreateInstance)\n");
    *ptr = new MyDirectSound8;
    return S_OK;
  }
  else
    return Real_CoCreateInstance(rclsid,pUnkOuter,dwClsContext,riid,ppv);
}

// --- now, waveout

class WaveOutImpl;
static WaveOutImpl *currentWaveOut = 0;

class WaveOutImpl
{
  char MagicCookie[16];
  WAVEFORMATEX *Format;
  DWORD_PTR Callback;
  DWORD_PTR CallbackInstance;
  DWORD OpenFlags;
  WAVEHDR *Head,*Current,*Tail;
  bool Paused,InLoop;
  int FirstFrame;
  int FirstWriteFrame;
  bool FrameInitialized;
  DWORD CurrentBufferPos;
  DWORD CurrentSamplePos;
  int GetPositionCounter;

  void callbackMessage(UINT uMsg,DWORD dwParam1,DWORD dwParam2)
  {
    switch(OpenFlags & CALLBACK_TYPEMASK)
    {
    case CALLBACK_EVENT:
      SetEvent((HANDLE) Callback);
      break;

    case CALLBACK_FUNCTION:
      ((PDRVCALLBACK) Callback)((HDRVR) this,uMsg,CallbackInstance,dwParam1,dwParam2);
      break;

    case CALLBACK_THREAD:
      PostThreadMessage((DWORD) Callback,uMsg,(WPARAM) this,(LPARAM) dwParam1);
      break;

    case CALLBACK_WINDOW:
      PostMessage((HWND) Callback,uMsg,(WPARAM) this,(LPARAM) dwParam1);
      break;
    }
  }

  void doneBuffer()
  {
    if(Head && Head == Current)
    {
      // mark current buffer as done and advance
      Current->dwFlags = (Current->dwFlags & ~WHDR_INQUEUE) | WHDR_DONE;
      callbackMessage(WOM_DONE,(DWORD) Current,0);

      Current = Current->lpNext;
      Head = Current;
      if(!Head)
        Tail = 0;
    }
    else
      printLog("sound: inconsistent state in waveOut (this is a kkapture bug)\n");
  }

  void advanceBuffer()
  {
    // loops need seperate processing
    if(InLoop && (Current->dwFlags & WHDR_ENDLOOP))
    {
      Current = Head;
      if(!--Current->dwLoops)
        InLoop = false;
      return;
    }

    // current buffer is done, mark it as done and advance
    if(!InLoop)
      doneBuffer();
    else
      Current = Current->lpNext;

    processCurrentBuffer();
  }

  void processCurrentBuffer()
  {
    // process beginloop flag because it may cause a state change
    if(Current && (Current->dwFlags & WHDR_BEGINLOOP) && Current->dwLoops)
      InLoop = true;
  }

public:
  WaveOutImpl(const WAVEFORMATEX *fmt,DWORD_PTR cb,DWORD_PTR cbInstance,DWORD fdwOpen)
  {
    videoNeedEncoder();

    Format = CopyFormat(fmt);
    Callback = cb;
    CallbackInstance = cbInstance;
    OpenFlags = fdwOpen;

    Head = Current = Tail = 0;
    CurrentBufferPos = 0;

    Paused = false;
    InLoop = false;
    FirstFrame = 0;
    FirstWriteFrame = 0;
    FrameInitialized = false;
    GetPositionCounter = 0;

    CurrentSamplePos = 0;
    memcpy(MagicCookie,"kkapture.waveout",16);

    callbackMessage(WOM_OPEN,0,0);
  }

  ~WaveOutImpl()
  {
    delete Format;
    callbackMessage(WOM_CLOSE,0,0);
  }

  bool amIReal() const
  {
    bool result = false;

    __try
    {
      result = memcmp(MagicCookie,"kkapture.waveout",16) == 0;
    }
    __except(EXCEPTION_EXECUTE_HANDLER)
    {
    }

    return result;
  }

  MMRESULT prepareHeader(WAVEHDR *hdr,UINT size)
  {
    if(!hdr || size != sizeof(WAVEHDR))
      return MMSYSERR_INVALPARAM;

    hdr->dwFlags |= WHDR_PREPARED;
    return MMSYSERR_NOERROR;
  }

  MMRESULT unprepareHeader(WAVEHDR *hdr,UINT size)
  {
    if(!hdr || size != sizeof(WAVEHDR))
      return MMSYSERR_INVALPARAM;

    if(hdr->dwFlags & WHDR_INQUEUE)
      return WAVERR_STILLPLAYING;

    hdr->dwFlags &= ~WHDR_PREPARED;
    return MMSYSERR_NOERROR;
  }

  MMRESULT write(WAVEHDR *hdr,UINT size)
  {
    if(!hdr || size != sizeof(WAVEHDR))
      return MMSYSERR_INVALPARAM;

    if(!(hdr->dwFlags & WHDR_PREPARED))
      return WAVERR_UNPREPARED;

    if(hdr->dwFlags & WHDR_INQUEUE)
      return MMSYSERR_NOERROR;

    // enqueue
    if(!FrameInitialized) // officially start playback!
    {
      FirstFrame = getFrameTiming();
      FirstWriteFrame = FirstFrame;
      FrameInitialized = true;
      encoder->SetAudioFormat(Format);
      currentWaveOut = this;
    }

    hdr->lpNext = 0;
    hdr->dwFlags = (hdr->dwFlags | WHDR_INQUEUE) & ~WHDR_DONE;

    if(Tail)
    {
      Tail->lpNext = hdr;
      Tail = hdr;
    }
    else
    {
      Head = Current = Tail = hdr;
      processCurrentBuffer();
    }

    return MMSYSERR_NOERROR;
  }

  MMRESULT pause()
  {
    Paused = true;
    return MMSYSERR_NOERROR;
  }

  MMRESULT restart()
  {
    Paused = FALSE;
    return MMSYSERR_NOERROR;
  }

  MMRESULT reset()
  {
    while(Head)
      doneBuffer();

    CurrentBufferPos = 0;
    CurrentSamplePos = 0;

    Paused = false;
    InLoop = false;
    FirstFrame = 0;
    FirstWriteFrame = 0;
    FrameInitialized = false;

    return MMSYSERR_NOERROR;
  }

  MMRESULT message(UINT uMsg,DWORD dwParam1,DWORD dwParam2)
  {
    return 0;
  }

  MMRESULT getPosition(MMTIME *mmt,UINT size)
  {
    if(++GetPositionCounter >= MAX_GETPOSITION_PER_FRAME) // assume that the app is waiting for the waveout position to change.
    {
      printLog("sound: app is hammering waveOutGetPosition, advancing time (frame=%d)\n",getFrameTiming());
      GetPositionCounter = 0;
      skipFrame();
    }

    if(!mmt || size < sizeof(MMTIME))
    {
      printLog("sound: invalid param to waveOutGetPosition");
      return MMSYSERR_INVALPARAM;
    }

    if(size > sizeof(MMTIME))
    {
      static bool warnedAboutMMTime = false;
      if(!warnedAboutMMTime)
      {
        printLog("sound: MMTIME structure passed to waveOutGetPosition is too large, ignoring extra fields (will only report this once).\n");
        warnedAboutMMTime = true;
      }
    }

    if(mmt->wType != TIME_BYTES && mmt->wType != TIME_SAMPLES && mmt->wType != TIME_MS)
    {
      printLog("sound: unsupported timecode format, defaulting to bytes.\n");
      mmt->wType = TIME_BYTES;
      return MMSYSERR_INVALPARAM;
    }

    // current frame (offset corrected)
    int relFrame = getFrameTiming() - FirstFrame;
    DWORD now = UMulDiv(relFrame,Format->nSamplesPerSec * frameRateDenom,frameRateScaled);

    // calc time in requested format
    switch(mmt->wType)
    {
    case TIME_BYTES:
    case TIME_MS:
      // yes, TIME_MS seems to return *bytes*. WHATEVER.
      mmt->u.cb = now * Format->nBlockAlign;
      break;

    case TIME_SAMPLES:
      mmt->u.sample = now;
      break;
    }

    return MMSYSERR_NOERROR;
  }

  // ----
  void encodeNoAudio(DWORD sampleCount)
  {
    // no new/delete, we do not know from where this might be called
    void *buffer = _alloca(256 * Format->nBlockAlign);
    memset(buffer,0,256 * Format->nBlockAlign);

    while(sampleCount)
    {
      int sampleBlock = min(sampleCount,256);
      encoder->WriteAudioFrame(buffer,sampleBlock);
      sampleCount -= sampleBlock;
    }
  }

  void processFrame()
  {
    GetPositionCounter = 0;

    // calculate number of samples to write
    int frame = getFrameTiming() - FirstWriteFrame;
    int align = Format->nBlockAlign;

    DWORD sampleNew = UMulDiv(frame,Format->nSamplesPerSec * frameRateDenom,frameRateScaled);
    DWORD sampleCount = sampleNew - CurrentSamplePos;

    if(!Current || Paused) // write one frame of no audio
    {
      encodeNoAudio(sampleCount);

      // also fix write frame timing
      FirstFrame++;
    }
    else // we have audio playing, so consume buffers as long as possible
    {
      while(sampleCount && Current)
      {
        int smps = min(sampleCount,(Current->dwBufferLength - CurrentBufferPos) / align);
        if(smps)
          encoder->WriteAudioFrame((PBYTE) Current->lpData + CurrentBufferPos,smps);

        sampleCount -= smps;
        CurrentBufferPos += smps * align;
        if(CurrentBufferPos >= Current->dwBufferLength) // buffer done
        {
          advanceBuffer();
          CurrentBufferPos = 0;
        }
      }

      if(sampleCount && !Current) // ran out of audio data
        encodeNoAudio(sampleCount);
    }

    CurrentSamplePos = sampleNew;
  }
};

DETOUR_TRAMPOLINE(MMRESULT __stdcall Real_waveOutOpen(LPHWAVEOUT phwo,UINT uDeviceID,LPCWAVEFORMATEX pwfx,DWORD_PTR dwCallback,DWORD_PTR dwInstance,DWORD fdwOpen), waveOutOpen);
DETOUR_TRAMPOLINE(MMRESULT __stdcall Real_waveOutClose(HWAVEOUT hwo), waveOutClose);
DETOUR_TRAMPOLINE(MMRESULT __stdcall Real_waveOutPrepareHeader(HWAVEOUT hwo,LPWAVEHDR pwh,UINT cbwh), waveOutPrepareHeader);
DETOUR_TRAMPOLINE(MMRESULT __stdcall Real_waveOutUnprepareHeader(HWAVEOUT hwo,LPWAVEHDR pwh,UINT cbwh), waveOutUnprepareHeader);
DETOUR_TRAMPOLINE(MMRESULT __stdcall Real_waveOutWrite(HWAVEOUT hwo,LPWAVEHDR pwh,UINT cbwh), waveOutWrite);
DETOUR_TRAMPOLINE(MMRESULT __stdcall Real_waveOutPause(HWAVEOUT hwo), waveOutPause);
DETOUR_TRAMPOLINE(MMRESULT __stdcall Real_waveOutRestart(HWAVEOUT hwo), waveOutRestart);
DETOUR_TRAMPOLINE(MMRESULT __stdcall Real_waveOutReset(HWAVEOUT hwo), waveOutReset);
DETOUR_TRAMPOLINE(MMRESULT __stdcall Real_waveOutMessage(HWAVEOUT hwo,UINT uMsg,DWORD_PTR dw1,DWORD_PTR dw2), waveOutMessage);
DETOUR_TRAMPOLINE(MMRESULT __stdcall Real_waveOutGetPosition(HWAVEOUT hwo,LPMMTIME pmmt,UINT cbmmt), waveOutGetPosition);
DETOUR_TRAMPOLINE(MMRESULT __stdcall Real_waveOutGetDevCaps(UINT_PTR uDeviceId,LPWAVEOUTCAPS pwo,UINT cbwoc), waveOutGetDevCaps);
DETOUR_TRAMPOLINE(MMRESULT __stdcall Real_waveOutGetNumDevs(), waveOutGetNumDevs);

static WaveOutImpl *waveOutLast = 0;

static WaveOutImpl *GetWaveOutImpl(HWAVEOUT hwo)
{
  if(hwo)
    return (WaveOutImpl *) hwo;

  return waveOutLast;
}

MMRESULT __stdcall Mine_waveOutOpen(LPHWAVEOUT phwo,UINT uDeviceID,LPCWAVEFORMATEX pwfx,DWORD_PTR dwCallback,DWORD_PTR dwInstance,DWORD fdwOpen)
{
  TRACE(("waveOutOpen(%p,%u,%p,%p,%p,%u)\n",phwo,uDeviceID,pwfx,dwCallback,dwInstance,fdwOpen));

  if(phwo)
  {
    printLog("sound: waveOutOpen %08x (%d hz, %d bits, %d channels)\n",
      uDeviceID,pwfx->nSamplesPerSec,pwfx->wBitsPerSample,pwfx->nChannels);

    WaveOutImpl *impl = new WaveOutImpl(pwfx,dwCallback,dwInstance,fdwOpen);
    waveOutLast = impl;
    *phwo = (HWAVEOUT) impl;

    return MMSYSERR_NOERROR;
  }
  else
  {
    if(fdwOpen & WAVE_FORMAT_QUERY)
      return MMSYSERR_NOERROR;
    else
      return MMSYSERR_INVALPARAM;
  }
}

MMRESULT __stdcall Mine_waveOutClose(HWAVEOUT hwo)
{
  TRACE(("waveOutClose(%p)\n",hwo));

  printLog("sound: waveOutClose %08x\n",hwo);

  WaveOutImpl *impl = GetWaveOutImpl(hwo);
  if(impl)
  {
    delete impl;
    if(impl == waveOutLast)
      waveOutLast = 0;

    return MMSYSERR_NOERROR;
  }
  else
    return MMSYSERR_INVALHANDLE;
}

MMRESULT __stdcall Mine_waveOutPrepareHeader(HWAVEOUT hwo,LPWAVEHDR pwh,UINT cbwh)
{
  TRACE(("waveOutPrepareHeader(%p,%p,%u)\n",hwo,pwh,cbwh));
  WaveOutImpl *impl = GetWaveOutImpl(hwo);
  return impl ? impl->prepareHeader(pwh,cbwh) : MMSYSERR_INVALHANDLE;
}

MMRESULT __stdcall Mine_waveOutUnprepareHeader(HWAVEOUT hwo,LPWAVEHDR pwh,UINT cbwh)
{
  TRACE(("waveOutUnprepareHeader(%p,%p,%u)\n",hwo,pwh,cbwh));
  WaveOutImpl *impl = GetWaveOutImpl(hwo);
  return impl ? impl->unprepareHeader(pwh,cbwh) : MMSYSERR_INVALHANDLE;
}

MMRESULT __stdcall Mine_waveOutWrite(HWAVEOUT hwo,LPWAVEHDR pwh,UINT cbwh)
{
  TRACE(("waveOutWrite(%p,%p,%u)\n",hwo,pwh,cbwh));
  WaveOutImpl *impl = GetWaveOutImpl(hwo);
  return impl ? impl->write(pwh,cbwh) : MMSYSERR_INVALHANDLE;
}

MMRESULT __stdcall Mine_waveOutPause(HWAVEOUT hwo)
{
  TRACE(("waveOutPause(%p)\n",hwo));
  WaveOutImpl *impl = GetWaveOutImpl(hwo);
  return impl ? impl->pause() : MMSYSERR_INVALHANDLE;
}

MMRESULT __stdcall Mine_waveOutRestart(HWAVEOUT hwo)
{
  TRACE(("waveOutRestart(%p)\n",hwo));
  WaveOutImpl *impl = GetWaveOutImpl(hwo);
  return impl ? impl->restart() : MMSYSERR_INVALHANDLE;
}

MMRESULT __stdcall Mine_waveOutReset(HWAVEOUT hwo)
{
  TRACE(("waveOutReset(%p)\n",hwo));
  WaveOutImpl *impl = GetWaveOutImpl(hwo);
  return impl ? impl->reset() : MMSYSERR_INVALHANDLE;
}

MMRESULT __stdcall Mine_waveOutMessage(HWAVEOUT hwo,UINT uMsg,DWORD_PTR dw1,DWORD_PTR dw2)
{
  TRACE(("waveOutMessage(%p,%u,%p,%p)\n",hwo,uMsg,dw1,dw2));
  WaveOutImpl *impl = GetWaveOutImpl(hwo);
  return impl ? impl->message(uMsg,(DWORD) dw1,(DWORD) dw2) : MMSYSERR_INVALHANDLE;
}

MMRESULT __stdcall Mine_waveOutGetPosition(HWAVEOUT hwo,LPMMTIME pmmt,UINT cbmmt)
{
  TRACE(("waveOutGetPosition(%p,%p,%u)\n",hwo,pmmt,cbmmt));
  WaveOutImpl *impl = GetWaveOutImpl(hwo);
  return impl ? impl->getPosition(pmmt,cbmmt) : MMSYSERR_INVALHANDLE;
}

MMRESULT __stdcall Mine_waveOutGetDevCaps(UINT_PTR uDeviceID,LPWAVEOUTCAPS pwoc,UINT cbwoc)
{
  static const char deviceName[] = ".kkapture Audio";
  WaveOutImpl *impl;

  TRACE(("waveOutGetDevCaps(%p,%p,%u)\n",uDeviceID,pwoc,cbwoc));

  if(uDeviceID == WAVE_MAPPER || uDeviceID == 0)
    impl = waveOutLast;
  else if(uDeviceID < 0x10000)
    return MMSYSERR_BADDEVICEID;
  else
  {
    impl = (WaveOutImpl *) uDeviceID;
    if(!impl->amIReal())
      return MMSYSERR_NODRIVER;
  }

  if(cbwoc < sizeof(WAVEOUTCAPS))
    return MMSYSERR_INVALPARAM;

  pwoc->wMid = MM_MICROSOFT;
  pwoc->wPid = (uDeviceID == WAVE_MAPPER) ? MM_WAVE_MAPPER : MM_MSFT_GENERIC_WAVEOUT;
  pwoc->vDriverVersion = 0x100;
  memcpy(pwoc->szPname,deviceName,sizeof(deviceName));
  pwoc->dwFormats = WAVE_FORMAT_1M08 | WAVE_FORMAT_1M16 | WAVE_FORMAT_1S08 | WAVE_FORMAT_1S16
    | WAVE_FORMAT_2M08 | WAVE_FORMAT_2M16 | WAVE_FORMAT_2S08 | WAVE_FORMAT_2S16
    | WAVE_FORMAT_4M08 | WAVE_FORMAT_4M16 | WAVE_FORMAT_4S08 | WAVE_FORMAT_4S16;
  pwoc->wChannels = 2;
  pwoc->wReserved1 = 0;
  pwoc->dwSupport = WAVECAPS_PLAYBACKRATE | WAVECAPS_SAMPLEACCURATE;

  return MMSYSERR_NOERROR;
}

UINT __stdcall Mine_waveOutGetNumDevs()
{
  TRACE(("waveOutGetNumDevs()\n"));
  return 1;
}

// ---- BASS

struct BASS_INFO
{
  DWORD flags;      // device capabilities (DSCAPS_xxx flags)
  DWORD hwsize;     // size of total device hardware memory
  DWORD hwfree;     // size of free device hardware memory
  DWORD freesam;    // number of free sample slots in the hardware
  DWORD free3d;     // number of free 3D sample slots in the hardware
  DWORD minrate;    // min sample rate supported by the hardware
  DWORD maxrate;    // max sample rate supported by the hardware
  BOOL eax;         // device supports EAX? (always FALSE if BASS_DEVICE_3D was not used)
  DWORD minbuf;     // recommended minimum buffer length in ms (requires BASS_DEVICE_LATENCY)
  DWORD dsver;      // DirectSound version
  DWORD latency;    // delay (in ms) before start of playback (requires BASS_DEVICE_LATENCY)
  DWORD initflags;  // BASS_Init "flags" parameter
  DWORD speakers;   // number of speakers available
  DWORD freq;       // current output rate (Vista/OSX only)
};

typedef BOOL (__stdcall *PBASS_INIT)(int device,DWORD freq,DWORD flags,HWND win,GUID *clsid);
typedef BOOL (__stdcall *PBASS_GETINFO)(BASS_INFO *info);

static PBASS_INIT Real_BASS_Init = 0;
static PBASS_GETINFO Real_BASS_GetInfo = 0;

static BOOL __stdcall Mine_BASS_Init(int device,DWORD freq,DWORD flags,HWND win,GUID *clsid)
{
  // for BASS, all we need to do is make sure that the BASS_DEVICE_LATENCY flag is cleared.
  return Real_BASS_Init(device,freq,flags & ~256,win,clsid);
}

static BOOL __stdcall Mine_BASS_GetInfo(BASS_INFO *info)
{
  BOOL res = Real_BASS_GetInfo(info);
  if(info) info->latency = 0;
  return res;
}

static void initSoundsysBASS()
{
  HMODULE bassDll = LoadLibraryA("bass.dll");
  if(bassDll)
  {
    PBASS_INIT init = (PBASS_INIT) GetProcAddress(bassDll,"BASS_Init");
    PBASS_GETINFO getinfo = (PBASS_GETINFO) GetProcAddress(bassDll,"BASS_GetInfo");

    if(init && getinfo)
    {
      printLog("sound/bass: bass.dll found, BASS support enabled.\n");
      Real_BASS_Init = (PBASS_INIT) DetourFunction((PBYTE) init,(PBYTE) Mine_BASS_Init);
      Real_BASS_GetInfo = (PBASS_GETINFO) DetourFunction((PBYTE) getinfo,(PBYTE) Mine_BASS_GetInfo);
    }
  }
}

// ---- FMOD 3.xx

typedef int (__stdcall *PFSOUND_STREAM_PLAY)(int channel,void *stream);
typedef int (__stdcall *PFSOUND_STREAM_PLAYEX)(int channel,void *stream,void *dspunit,signed char paused);
typedef signed char (__stdcall *PFSOUND_STREAM_STOP)(void *stream);
typedef int (__stdcall *PFSOUND_STREAM_GETTIME)(void *stream);
typedef int (__stdcall *PFSOUND_PLAYSOUND)(int channel,void *sample);
typedef int (__stdcall *PFSOUND_GETFREQUENCY)(int channel);
typedef unsigned int (__stdcall *PFSOUND_GETCURRENTPOSITION)(int channel);

static PFSOUND_STREAM_PLAY Real_FSOUND_Stream_Play = 0;
static PFSOUND_STREAM_PLAYEX Real_FSOUND_Stream_PlayEx = 0;
static PFSOUND_STREAM_STOP Real_FSOUND_Stream_Stop = 0;
static PFSOUND_STREAM_GETTIME Real_FSOUND_Stream_GetTime = 0;
static PFSOUND_PLAYSOUND Real_FSOUND_PlaySound = 0;
static PFSOUND_GETFREQUENCY Real_FSOUND_GetFrequency = 0;
static PFSOUND_GETCURRENTPOSITION Real_FSOUND_GetCurrentPosition = 0;

static void *FMODStart = 0, *FMODEnd = 0;

#define CalledFromFMOD() (_ReturnAddress() >= FMODStart && _ReturnAddress() < FMODEnd) // needs to be a macro

struct FMODStreamDesc
{
  void *stream;
  int channel;
  int firstFrame;
  int frequency;
};

static const int FMODNumStreams = 16; // max # of active (playing) streams supported
static FMODStreamDesc FMODStreams[FMODNumStreams];

static void RegisterFMODStream(void *stream,int channel)
{
  if(channel == -1) // error from fmod
    return;

  // find a free stream desc
  int index = 0;
  while(index<FMODNumStreams && FMODStreams[index].stream)
    index++;

  if(index==FMODNumStreams)
  {
    printLog("sound/fmod: more than %d streams playing, ran out of handles.\n",FMODNumStreams);
    return;
  }

  FMODStreams[index].stream = stream;
  FMODStreams[index].channel = channel;
  FMODStreams[index].firstFrame = getFrameTiming();
  FMODStreams[index].frequency = Real_FSOUND_GetFrequency(channel);
  printLog("sound/fmod: stream playing on channel %08x with frequency %d Hz.\n",channel,FMODStreams[index].frequency);
}

static FMODStreamDesc *FMODStreamFromChannel(int chan)
{
  for(int i=0;i<FMODNumStreams;i++)
    if(FMODStreams[i].stream && FMODStreams[i].channel == chan)
      return &FMODStreams[i];

  return 0;
}

static FMODStreamDesc *FMODStreamFromPtr(void *ptr)
{
  for(int i=0;i<FMODNumStreams;i++)
    if(FMODStreams[i].stream == ptr)
      return &FMODStreams[i];

  return 0;
}

static int __stdcall Mine_FSOUND_Stream_Play(int channel,void *stream)
{
  printLog("sound/fmod: StreamPlay\n");
  int realChan = Real_FSOUND_Stream_Play(channel,stream);
  RegisterFMODStream(stream,realChan);

  return realChan;
}

static int __stdcall Mine_FSOUND_Stream_PlayEx(int channel,void *stream,void *dspunit,signed char paused)
{
  printLog("sound/fmod: StreamPlayEx(%08x,%p,%p,%d)\n",channel,stream,dspunit,paused);
  int realChan = Real_FSOUND_Stream_PlayEx(channel,stream,dspunit,paused);
  RegisterFMODStream(stream,realChan);

  return realChan;
}

static int __stdcall Mine_FSOUND_PlaySound(int channel,void *sample)
{
  printLog("sound/fmod: PlaySound(%08x,%p)\n",channel,sample);
  int realChan = Real_FSOUND_PlaySound(channel,sample);
  RegisterFMODStream(sample,realChan);

  return realChan;
}

static signed char __stdcall Mine_FSOUND_Stream_Stop(void *stream)
{
  printLog("sound/fmod: StreamStop(%p)\n",stream);
  FMODStreamDesc *desc = FMODStreamFromPtr(stream);
  if(desc)
    desc->stream = 0;

  return Real_FSOUND_Stream_Stop(stream);
}

static int __stdcall Mine_FSOUND_Stream_GetTime(void *stream)
{
  FMODStreamDesc *desc = FMODStreamFromPtr(stream);
  if(desc)
  {
    unsigned int time = UMulDiv(getFrameTiming() - desc->firstFrame,1000*frameRateDenom,frameRateScaled);
    return time;
  }

  return Real_FSOUND_Stream_GetTime(stream);
}

static unsigned int __stdcall Mine_FSOUND_GetCurrentPosition(int channel)
{
  FMODStreamDesc *desc = FMODStreamFromChannel(channel);
  if(!CalledFromFMOD() && desc)
  {
    // this is a known stream playing; return time based on frame timing.
    // (to work around FMODs stupidly coarse timer resolution)
    unsigned int time = UMulDiv(getFrameTiming() - desc->firstFrame,desc->frequency*frameRateDenom,frameRateScaled);
    return time;
  }

  return Real_FSOUND_GetCurrentPosition(channel);
}

static void initSoundsysFMOD3()
{
  HMODULE fmodDll = LoadLibraryA("fmod.dll");
  if(fmodDll)
  {
    if(GetProcAddress(fmodDll,"_FSOUND_Stream_Play@8"))
    {
      MODULEINFO moduleInfo;
      GetModuleInformation(GetCurrentProcess(),fmodDll,&moduleInfo,sizeof(moduleInfo));

      FMODStart = moduleInfo.lpBaseOfDll;
      FMODEnd = (void*) ((BYTE*) moduleInfo.lpBaseOfDll + moduleInfo.SizeOfImage);

      printLog("sound/FMOD3: fmod.dll found, FMOD support enabled.\n");
      Real_FSOUND_Stream_Play = (PFSOUND_STREAM_PLAY) DetourFunction((PBYTE) GetProcAddress(fmodDll,"_FSOUND_Stream_Play@8"),(PBYTE) Mine_FSOUND_Stream_Play);
      Real_FSOUND_Stream_PlayEx = (PFSOUND_STREAM_PLAYEX) DetourFunction((PBYTE) GetProcAddress(fmodDll,"_FSOUND_Stream_PlayEx@16"),(PBYTE) Mine_FSOUND_Stream_PlayEx);
      Real_FSOUND_Stream_Stop = (PFSOUND_STREAM_STOP) DetourFunction((PBYTE) GetProcAddress(fmodDll,"_FSOUND_Stream_Stop@4"),(PBYTE) Mine_FSOUND_Stream_Stop);
      Real_FSOUND_Stream_GetTime = (PFSOUND_STREAM_GETTIME) DetourFunction((PBYTE) GetProcAddress(fmodDll,"_FSOUND_Stream_GetTime@4"),(PBYTE) Mine_FSOUND_Stream_GetTime);
      Real_FSOUND_PlaySound = (PFSOUND_PLAYSOUND) DetourFunction((PBYTE) GetProcAddress(fmodDll,"_FSOUND_PlaySound@8"),(PBYTE) Mine_FSOUND_PlaySound);
      Real_FSOUND_GetFrequency = (PFSOUND_GETFREQUENCY) GetProcAddress(fmodDll,"_FSOUND_GetFrequency@4");
      Real_FSOUND_GetCurrentPosition = (PFSOUND_GETCURRENTPOSITION) DetourFunction((PBYTE) GetProcAddress(fmodDll,"_FSOUND_GetCurrentPosition@4"),(PBYTE) Mine_FSOUND_GetCurrentPosition);
    }
  }
}

// ---- FMODEx 4.xx

typedef int (__stdcall *PSYSTEM_INIT)(void *sys,int maxchan,int flags,void *extradriverdata);
typedef int (__stdcall *PSYSTEM_SETOUTPUT)(void *sys,int output);
typedef int (__stdcall *PSYSTEM_PLAYSOUND)(void *sys,int index,void *sound,bool paused,void **channel);
typedef int (__stdcall *PCHANNEL_GETFREQUENCY)(void *chan,float *freq);
typedef int (__stdcall *PCHANNEL_GETPOSITION)(void *chan,unsigned *position,int posType);

static PSYSTEM_INIT Real_System_init;
static PSYSTEM_SETOUTPUT Real_System_setOutput;
static PSYSTEM_PLAYSOUND Real_System_playSound;
static PCHANNEL_GETFREQUENCY Real_Channel_getFrequency;
static PCHANNEL_GETPOSITION Real_Channel_getPosition;

static void *FMODExStart = 0, *FMODExEnd = 0;

#define CalledFromFMODEx() (_ReturnAddress() >= FMODExStart && _ReturnAddress() < FMODExEnd) // needs to be a macro

struct FMODExSoundDesc
{
  void *sound;
  void *channel;
  int firstFrame;
  float frequency;
};

static const int FMODExNumSounds = 16; // max # of active (playing) sounds supported
static FMODExSoundDesc FMODExSounds[FMODExNumSounds];

static int __stdcall Mine_System_init(void *sys,int maxchan,int flags,void *extradriverdata)
{
  // force output type to be dsound (mainly so i don't have to write a windows audio session implementation for vista)
  Real_System_setOutput(sys,6); // 6=DirectSound
  return Real_System_init(sys,maxchan,flags,extradriverdata);
}

static int __stdcall Mine_System_playSound(void *sys,int index,void *sound,bool paused,void **channel)
{
  void *chan;
  printLog("sound/fmodex: playSound\n");
  int result = Real_System_playSound(sys,index,sound,paused,&chan);
  if(result == 0) // FMOD_OK
  {
    // find a free sound desc
    int index = 0;
    while(index<FMODExNumSounds && FMODExSounds[index].sound)
      index++;

    if(index==FMODExNumSounds)
      printLog("sound/fmodex: more than %d sounds playing, ran out of handles.\n",FMODExNumSounds);
    else
    {
      FMODExSounds[index].sound = sound;
      FMODExSounds[index].channel = chan;
      FMODExSounds[index].firstFrame = getFrameTiming();
      Real_Channel_getFrequency(chan,&FMODExSounds[index].frequency);
    }
  }

  if(channel)
    *channel = chan;

  return result;
}

static FMODExSoundDesc *FMODExSoundFromChannel(void *chan)
{
  for(int i=0;i<FMODExNumSounds;i++)
    if(FMODExSounds[i].sound && FMODExSounds[i].channel == chan)
      return &FMODExSounds[i];

  return 0;
}

static int __stdcall Mine_Channel_getPosition(void *channel,unsigned *position,int posType)
{
  FMODExSoundDesc *desc = FMODExSoundFromChannel(channel);
  if(desc && !CalledFromFMODEx())
  {
    int timeBase;

    switch(posType)
    {
    case 1:   timeBase = 1000; break;                   // FMOD_TIMEUNIT_MS
    case 2:   timeBase = (int) desc->frequency; break;  // FMOD_TIMEUNIT_PCM
    default:  timeBase = -1; break;
    }

    if(timeBase != -1)
    {
      *position = UMulDiv(getFrameTiming() - desc->firstFrame,timeBase*frameRateDenom,frameRateScaled);
      return 0; // FMOD_OK
    }
    else
      printLog("sound/fmodex: unsupported timebase used in Channel::getPosition, forwarding to fmod timer.\n");
  }

  return Real_Channel_getPosition(channel,position,posType);
}

static void initSoundsysFMODEx()
{
  HMODULE fmodDll = LoadLibraryA("fmodex.dll");
  if(fmodDll)
  {
    if(GetProcAddress(fmodDll,"FMOD_System_Init"))
    {
      MODULEINFO moduleInfo;
      GetModuleInformation(GetCurrentProcess(),fmodDll,&moduleInfo,sizeof(moduleInfo));

      FMODExStart = moduleInfo.lpBaseOfDll;
      FMODExEnd = (void*) ((BYTE*) moduleInfo.lpBaseOfDll + moduleInfo.SizeOfImage);

      printLog("sound/fmodex: fmodex.dll found, FMODEx support enabled.\n");

      Real_System_init = (PSYSTEM_INIT) DetourFunction((PBYTE) GetProcAddress(fmodDll,"?init@System@FMOD@@QAG?AW4FMOD_RESULT@@HIPAX@Z"),(PBYTE) Mine_System_init);
      Real_System_setOutput = (PSYSTEM_SETOUTPUT) GetProcAddress(fmodDll,"?setOutput@System@FMOD@@QAG?AW4FMOD_RESULT@@W4FMOD_OUTPUTTYPE@@@Z");
      Real_System_playSound = (PSYSTEM_PLAYSOUND) DetourFunction((PBYTE) GetProcAddress(fmodDll,"?playSound@System@FMOD@@QAG?AW4FMOD_RESULT@@W4FMOD_CHANNELINDEX@@PAVSound@2@_NPAPAVChannel@2@@Z"),(PBYTE) Mine_System_playSound);
      Real_Channel_getFrequency = (PCHANNEL_GETFREQUENCY) GetProcAddress(fmodDll,"?getFrequency@Channel@FMOD@@QAG?AW4FMOD_RESULT@@PAM@Z");
      Real_Channel_getPosition = (PCHANNEL_GETPOSITION) DetourFunction((PBYTE) GetProcAddress(fmodDll,"?getPosition@Channel@FMOD@@QAG?AW4FMOD_RESULT@@PAII@Z"),(PBYTE) Mine_Channel_getPosition);
    }
  }
}

// ----

void initSound()
{
  DetourFunctionWithTrampoline((PBYTE) Real_DirectSoundCreate,(PBYTE) Mine_DirectSoundCreate);
  DetourFunctionWithTrampoline((PBYTE) Real_DirectSoundCreate8,(PBYTE) Mine_DirectSoundCreate8);

  DetourFunctionWithTrampoline((PBYTE) Real_CoCreateInstance,(PBYTE) Mine_CoCreateInstance);

  DetourFunctionWithTrampoline((PBYTE) Real_waveOutOpen,(PBYTE) Mine_waveOutOpen);
  DetourFunctionWithTrampoline((PBYTE) Real_waveOutClose,(PBYTE) Mine_waveOutClose);
  DetourFunctionWithTrampoline((PBYTE) Real_waveOutPrepareHeader,(PBYTE) Mine_waveOutPrepareHeader);
  DetourFunctionWithTrampoline((PBYTE) Real_waveOutUnprepareHeader,(PBYTE) Mine_waveOutUnprepareHeader);
  DetourFunctionWithTrampoline((PBYTE) Real_waveOutWrite,(PBYTE) Mine_waveOutWrite);
  DetourFunctionWithTrampoline((PBYTE) Real_waveOutPause,(PBYTE) Mine_waveOutPause);
  DetourFunctionWithTrampoline((PBYTE) Real_waveOutReset,(PBYTE) Mine_waveOutReset);
  DetourFunctionWithTrampoline((PBYTE) Real_waveOutRestart,(PBYTE) Mine_waveOutRestart);
  DetourFunctionWithTrampoline((PBYTE) Real_waveOutMessage,(PBYTE) Mine_waveOutMessage);
  DetourFunctionWithTrampoline((PBYTE) Real_waveOutGetPosition,(PBYTE) Mine_waveOutGetPosition);
  DetourFunctionWithTrampoline((PBYTE) Real_waveOutGetDevCaps, (PBYTE) Mine_waveOutGetDevCaps);
  DetourFunctionWithTrampoline((PBYTE) Real_waveOutGetNumDevs, (PBYTE) Mine_waveOutGetNumDevs);

  if(params.SoundsysInterception)
  {
    initSoundsysBASS();
    initSoundsysFMOD3();
    initSoundsysFMODEx();
  }
}

void doneSound()
{
}

void nextFrameSound()
{
  if(playBuffer)
    playBuffer->encodeLastFrameAudio();
  else if(currentWaveOut)
    currentWaveOut->processFrame();
}