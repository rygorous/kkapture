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
#include "avi_videoencoder_dshow.h"
#include "video.h"
#include "audio_resample.h"
#include "videocapturetimer.h"

#if USE_DSHOW_AVI_WRITER

// okay, so why is this only enabled on a define?
// why you would want to enable it:
// - it allows kkapture to write avi files >2gb without indexing problems
//
// why you wouldn't want to enable it:
// - it requires directshow and the directshow base classes, which is a
//   pretty big bunch of code most people don't have installed and
//   probably don't *want* to install.
//
// if you want it enabled, copy over the base classes header files into
// the dshow_base/ subdirectory of the solution dir.
//
// you also need to make the stream base classes libraries (release/debug
// builds) available as strmbase_r.lib and strmbase_d.lib in the same
// directory.

#pragma comment(lib,"vfw32.lib")

#ifdef NDEBUG
#pragma comment(lib,"../dshow_base/strmbase_r.lib")
#else
#pragma comment(lib,"../dshow_base/strmbase_d.lib")
#endif

// ---- dshow filters (aaargh!!)

// {D4176A5A-B1CA-4c9e-A4A5-1BEDB9E281A8}
static const GUID CLSID_VideoSource = 
{ 0xd4176a5a, 0xb1ca, 0x4c9e, { 0xa4, 0xa5, 0x1b, 0xed, 0xb9, 0xe2, 0x81, 0xa8 } };
// {C8F8DD64-223D-4d2f-A240-FF437137CF1B}
static const GUID CLSID_AudioSource = 
{ 0xc8f8dd64, 0x223d, 0x4d2f, { 0xa2, 0x40, 0xff, 0x43, 0x71, 0x37, 0xcf, 0x1b } };

class VideoSourcePin : public CSourceStream
{
  enum { BufferCount = 4 };

  CSource *Filter;
  REFERENCE_TIME TimePerFrame;

  unsigned char *Buffers[BufferCount];
  CAMEvent BufferReadChange,BufferWriteChange;
  int BufferReadPtr,BufferWritePtr;

  int XRes,YRes;
  int Frames;
  bool Terminating;

public:
  VideoSourcePin(HRESULT *hr,CSource *filter,int fpsNum,int fpsDenom,int xRes,int yRes)
    : CSourceStream(NAME("kkapture video source"),hr,filter,L"Out"),
    Filter(filter),XRes(xRes),YRes(yRes)
  {
    // time per frame is in units of 100 nanoseconds
    TimePerFrame = ULongMulDiv(10*1000*1000,fpsDenom,fpsNum);
    Frames = 0;
    Terminating = false;

    // allocate buffers
    for(int i=0;i<BufferCount;i++)
      Buffers[i] = new unsigned char[xRes*yRes*3];

    BufferReadPtr = 0;
    BufferWritePtr = 0;
  }

  ~VideoSourcePin()
  {
    // free buffers
    for(int i=0;i<BufferCount;i++)
      delete[] Buffers[i];
  }

  HRESULT GetMediaType(CMediaType *mediaType)
  {
    CAutoLock autolock(Filter->pStateLock());
    
    CheckPointer(mediaType,E_POINTER);

    // allocate memory for the VIDEOINFOHEADER
    VIDEOINFOHEADER *vi = (VIDEOINFOHEADER *) mediaType->AllocFormatBuffer(SIZE_PREHEADER + sizeof(BITMAPINFOHEADER));
    if(!vi)
      return E_OUTOFMEMORY;

    ZeroMemory(vi,mediaType->cbFormat);
    vi->AvgTimePerFrame = TimePerFrame;

    // set stream format
    vi->bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    vi->bmiHeader.biWidth       = XRes;
    vi->bmiHeader.biHeight      = YRes;
    vi->bmiHeader.biPlanes      = 1;
    vi->bmiHeader.biBitCount    = 24;
    vi->bmiHeader.biCompression = BI_RGB;
    vi->bmiHeader.biSizeImage   = XRes * YRes * 3;

    // clear source&target rectangles
    SetRectEmpty(&vi->rcSource);
    SetRectEmpty(&vi->rcTarget);

    // setup media type
    mediaType->SetType(&MEDIATYPE_Video);
    mediaType->SetFormatType(&FORMAT_VideoInfo);
    mediaType->SetTemporalCompression(FALSE);

    const GUID subtypeGUID = GetBitmapSubtype(&vi->bmiHeader);
    mediaType->SetSubtype(&subtypeGUID);
    mediaType->SetSampleSize(vi->bmiHeader.biSizeImage);

    return S_OK;
  }

  HRESULT DecideBufferSize(IMemAllocator *alloc,ALLOCATOR_PROPERTIES *request)
  {
    HRESULT hr;
    CAutoLock autolock(Filter->pStateLock());

    CheckPointer(alloc,E_POINTER);
    CheckPointer(request,E_POINTER);

    // setup buffer count+size
    VIDEOINFOHEADER *vi = (VIDEOINFOHEADER *) m_mt.Format();
    if(!request->cBuffers)
      request->cBuffers = 2;
    
    request->cbBuffer = vi->bmiHeader.biSizeImage;

    // setup allocator
    ALLOCATOR_PROPERTIES actual;
    hr = alloc->SetProperties(request,&actual);
    if(FAILED(hr))
      return hr;

    // if he doesn't want to give us enough memory, ditch him
    if(actual.cbBuffer < request->cbBuffer)
      return E_FAIL;

    return S_OK;
  }

  HRESULT FillBuffer(IMediaSample *sample)
  {
    CheckPointer(sample,E_POINTER);

    // synchronize
    unsigned char *sourceData = 0;
    do
    {
      if(Terminating)
      {
        BufferWriteChange.Set();
        return S_FALSE;
      }

      if(BufferReadPtr != BufferWritePtr)
      {
        sourceData = Buffers[BufferReadPtr];
        BufferReadPtr = (BufferReadPtr + 1) % BufferCount;
        BufferReadChange.Set();
      }
      else
        BufferWriteChange.Wait();
    }
    while(!sourceData);

    // copy over
    BYTE *dataPtr;
    VIDEOINFOHEADER *vi = (VIDEOINFOHEADER *) m_mt.Format();
    sample->GetPointer(&dataPtr);

    memcpy(dataPtr,sourceData,min(sample->GetSize(),(int) vi->bmiHeader.biSizeImage));

    // set time and sync point
    REFERENCE_TIME start,end;
    start = Frames * TimePerFrame;
    end = start + TimePerFrame;

    sample->SetTime(&start,&end);
    sample->SetSyncPoint(TRUE);

    // re-sync
    return S_OK;
  }

  // we don't implement quality control
  STDMETHODIMP Notify(IBaseFilter *self,Quality q)
  {
    return E_FAIL;
  }

  // use this to submit frames.
  void PushData(const unsigned char *buffer)
  {
    unsigned char *destBuffer = 0;

    do
    {
      int ReadPrev = (BufferReadPtr + BufferCount - 1) % BufferCount;

      if(ReadPrev != BufferWritePtr)
      {
        destBuffer = Buffers[BufferWritePtr];
        memcpy(destBuffer,buffer,XRes * YRes * 3);
        BufferWritePtr = (BufferWritePtr + 1) % BufferCount;
        BufferWriteChange.Set();
      }
      else
        BufferReadChange.Wait();
    }
    while(!destBuffer);
  }

  void Terminate()
  {
    Terminating = true;
    BufferWriteChange.Set();
  }
};

class VideoSourceFilter : public CSource
{
  VideoSourcePin *pin;

public:
  VideoSourceFilter(int fpsNum,int fpsDenom,int xRes,int yRes)
    : CSource(NAME("kkapture video source"),0,CLSID_VideoSource)
  {
    HRESULT hr;

    pin = new VideoSourcePin(&hr,this,fpsNum,fpsDenom,xRes,yRes);
  }

  ~VideoSourceFilter()
  {
    delete pin;
  }

  void PushData(const unsigned char *buffer)
  {
    pin->PushData(buffer);
  }

  void Terminate()
  {
    pin->Terminate();
  }
};

class AudioSourcePin : public CSourceStream
{
  enum { BufferCount = 4*4 };

  struct Buffer
  {
    BYTE *data;
  };

  CSource *Filter;

  int SampleRate;
  int BufferSize;
  CAMEvent BufferReadChange,BufferWriteChange;
  Buffer *Buffers;
  volatile LONG BufferReadPtr,BufferWritePtr;
  BYTE *PartialBuffer;
  int PartialBufferPos;
  LONGLONG SamplesWritten;
  bool Terminating;

public:
  AudioSourcePin(HRESULT *hr,CSource *filter,int sampleRate)
    : CSourceStream(NAME("kkapture audio source"),hr,filter,L"Out"),
    Filter(filter),SampleRate(sampleRate)
  {
    Terminating = false;
    //BufferSize = 16*1024; // use 16k chunks
    BufferSize = 4*1024;

    // allocate buffers
    Buffers = new Buffer[BufferCount];
    for(int i=0;i<BufferCount;i++)
    {
      Buffers[i].data = (BYTE *) malloc(BufferSize);
    }

    BufferReadPtr = 0;
    BufferWritePtr = 0;
    PartialBuffer = (BYTE *) malloc(BufferSize);
    PartialBufferPos = 0;

    SamplesWritten = 0;
  }

  ~AudioSourcePin()
  {
    for(int i=0;i<BufferCount;i++)
      free(Buffers[i].data);
  }

  HRESULT GetMediaType(CMediaType *mediaType)
  {
    CAutoLock autolock(Filter->pStateLock());
    
    CheckPointer(mediaType,E_POINTER);

    WAVEFORMATEX *wfx = (WAVEFORMATEX *) mediaType->AllocFormatBuffer(sizeof(WAVEFORMATEX));
    if(!wfx)
      return E_OUTOFMEMORY;
    
    wfx->wFormatTag = WAVE_FORMAT_PCM;
    wfx->nChannels = 2;
    wfx->nSamplesPerSec = 44100;
    wfx->nAvgBytesPerSec = 44100 * 4;
    wfx->nBlockAlign = 4;
    wfx->wBitsPerSample = 16;
    wfx->cbSize = 0;

    return CreateAudioMediaType(wfx,mediaType,FALSE);
  }

  HRESULT DecideBufferSize(IMemAllocator *alloc,ALLOCATOR_PROPERTIES *request)
  {
    HRESULT hr;
    CAutoLock autolock(Filter->pStateLock());

    CheckPointer(alloc,E_POINTER);
    CheckPointer(request,E_POINTER);

    // setup buffer count+size
    WAVEFORMATEX *wfx = (WAVEFORMATEX *) m_mt.Format();
    request->cbBuffer = BufferSize;
    request->cBuffers = BufferCount;

    // setup allocator
    ALLOCATOR_PROPERTIES actual;
    hr = alloc->SetProperties(request,&actual);
    if(FAILED(hr))
      return hr;

    if(actual.cbBuffer < request->cbBuffer)
      return E_FAIL;

    return S_OK;
  }

  HRESULT FillBuffer(IMediaSample *sample)
  {
    CheckPointer(sample,E_POINTER);

    // synchronize and try to get data
    Buffer *sourceBuffer = 0;
    do
    {
      if(Terminating)
      {
        BufferWriteChange.Set();
        return S_FALSE;
      }

      if(BufferReadPtr != BufferWritePtr)
      {
        sourceBuffer = &Buffers[BufferReadPtr];
        BufferReadPtr = (BufferReadPtr + 1) % BufferCount;
        BufferReadChange.Set();
      }
      else
        BufferWriteChange.Wait();
    }
    while(!sourceBuffer);

    // copy over
    BYTE *destPtr = 0;
    WAVEFORMATEX *wfx = (WAVEFORMATEX *) m_mt.Format();
    sample->SetActualDataLength(BufferSize);
    sample->GetPointer(&destPtr);
    memcpy(destPtr,sourceBuffer->data,BufferSize);

    // set time
    REFERENCE_TIME start,end;
    bool firstFrame = SamplesWritten == 0;

    start = ULongMulDiv(SamplesWritten,10000000,SampleRate);
    SamplesWritten += BufferSize / wfx->nBlockAlign;
    end = ULongMulDiv(SamplesWritten,10000000,SampleRate);

    sample->SetTime(&start,&end);
    sample->SetSyncPoint(firstFrame);

    return S_OK;
  }

  STDMETHODIMP Notify(IBaseFilter *self,Quality q)
  {
    return E_FAIL;
  }

  void PushData(const void *buffer,int bytes)
  {
    const BYTE *bufInPtr = (const BYTE *) buffer;

    while(bytes)
    {
      // first, fill up current partial buffer
      int countBytes = min(bytes,BufferSize - PartialBufferPos);
      memcpy(PartialBuffer + PartialBufferPos,bufInPtr,countBytes);
      bufInPtr += countBytes;
      PartialBufferPos += countBytes;
      bytes -= countBytes;

      // when we have filled up one buffer, try to submit it
      if(PartialBufferPos == BufferSize)
      {
        Buffer *destBuffer = 0;

        do
        {
          int ReadPrev = BufferReadPtr - 1;
          if(ReadPrev < 0)
            ReadPrev += BufferCount;

          if(ReadPrev != BufferWritePtr)
          {
            destBuffer = &Buffers[BufferWritePtr];
            BYTE *t = PartialBuffer;
            PartialBuffer = destBuffer->data;
            destBuffer->data = t;

            BufferWritePtr = (BufferWritePtr + 1 < BufferCount) ? BufferWritePtr + 1 : 0;
            BufferWriteChange.Set();
          }
          else
            BufferReadChange.Wait();
        }
        while(!destBuffer);

        PartialBufferPos = 0;
      }
    }
  }

  void FeedZeroFrames(int frames,int fpsNum,int fpsDenom)
  {
    unsigned char buffer[2048];
    memset(buffer,0,sizeof(buffer));

    WAVEFORMATEX *fmt = (WAVEFORMATEX *) m_mt.Format();
    DWORD fillBytes = UMulDiv(frames,fmt->nAvgBytesPerSec * fpsDenom,fpsNum);

    while(fillBytes)
    {
      int count = min(fillBytes,2048);
      PushData(buffer,count);
      fillBytes -= count;
    }
  }

  void Terminate()
  {
    Terminating = true;
    BufferWriteChange.Set();
  }
};

class AudioSourceFilter : public CSource
{
  AudioSourcePin *pin;

public:
  AudioSourceFilter(int sampleRate)
    : CSource(NAME("kkapture audio source"),0,CLSID_AudioSource)
  {
    HRESULT hr;

    pin = new AudioSourcePin(&hr,this,sampleRate);
  }

  ~AudioSourceFilter()
  {
    delete pin;
  }

  void PushData(const void *data,int countBytes)
  {
    pin->PushData(data,countBytes);
  }

  void FeedZeroFrames(int frames,int fpsNum,int fpsDenom)
  {
    pin->FeedZeroFrames(frames,fpsNum,fpsDenom);
  }

  void Terminate()
  {
    pin->Terminate();
  }
};

template<class T> static void safeRelease(T *&ptr)
{
  if(ptr)
  {
    ptr->Release();
    ptr = 0;
  }
}

// creates a filter from a FOURCC codec id.
// searching by name seems to be a TOTALLY weird and stupid idea,
// but it's the only way I managed to map from fourcc to a filter...
// aaarrgh!
static IBaseFilter *FilterFromFourCC(DWORD codec)
{
  // try to find the driver name for the given fourcc
  ICINFO info;
  HIC ic = ICOpen(ICTYPE_VIDEO,codec,ICMODE_QUERY);
  if(!ic || !ICGetInfo(ic,&info,sizeof(info)))
  {
    if(ic)
      ICClose(ic);

    return 0;
  }

  // search for the filter with the system device enumerator
  IBaseFilter *result = 0;
  ICreateDevEnum *devEnum = 0;
  if(SUCCEEDED(CoCreateInstance(CLSID_SystemDeviceEnum,0,
    CLSCTX_INPROC_SERVER,IID_ICreateDevEnum,(void **) &devEnum)))
  {
    IEnumMoniker *enumerator = 0;

    if(SUCCEEDED(devEnum->CreateClassEnumerator(CLSID_VideoCompressorCategory,
      &enumerator,CDEF_DEVMON_CMGR_DEVICE)))
    {
      IMoniker *moniker = 0;
      while(enumerator->Next(1,&moniker,0) == S_OK)
      {
        IPropertyBag *bag = 0;
        if(SUCCEEDED(moniker->BindToStorage(0,0,IID_IPropertyBag,(void **) &bag)))
        {
          VARIANT var;
          VariantInit(&var);
          if(SUCCEEDED(bag->Read(L"FriendlyName",&var,0)))
          {
            if(!wcscmp(info.szDescription,var.bstrVal) && !result) // first match!
            {
              if(FAILED(moniker->BindToObject(0,0,IID_IBaseFilter,(void **) &result)))
                result = 0;
            }
          }
          VariantClear(&var);

          bag->Release();
        }

        moniker->Release();
      }

      enumerator->Release();
    }

    devEnum->Release();
  }

  return result;
}

// internal data
struct AVIVideoEncoderDShow::Internal
{
  bool formatSet;
  DWORD codec;
  ICaptureGraphBuilder2 *build;
  IGraphBuilder *graph;
  IBaseFilter *mux;
  IBaseFilter *vid_compress;
  VideoSourceFilter *video;
  AudioSourceFilter *audio;
  IMediaControl *control;
  
  WAVEFORMATEX *wfx;

  bool audioOk;
  AudioResampler resampler;
  int resampleSize;
  short *resampleBuf;

  WCHAR wideName[256];

  void SetName(const char *name)
  {
    MultiByteToWideChar(CP_ACP,0,name,-1,wideName,256);
    wideName[255] = 0;    
  }

  void NeedBuild()
  {
    if(build)
      return;

    // give us a capture graph builder
    CoInitialize(0);
    CoCreateInstance(CLSID_CaptureGraphBuilder2,0,CLSCTX_INPROC_SERVER,
      IID_ICaptureGraphBuilder2,(void **) &build);

    if(build)
    {
      HRESULT hr;

      // rendering section first
      hr = build->SetOutputFileName(&MEDIASUBTYPE_Avi,wideName,&mux,0);

      // setup full interleaving on mux
      if(mux)
      {
        IConfigInterleaving *ileave = 0;
        mux->QueryInterface(IID_IConfigInterleaving,(void **) &ileave);

        if(ileave)
        {
          ileave->put_Mode(INTERLEAVE_FULL);
          ileave->Release();
        }

        printLog("avi_dshow: setup interleave\n");
      }
      else
        printLog("avi_dshow: no mux! <fn %ws>\n",wideName);

      // we'll need a general graph builder for the rest
      hr = build->GetFiltergraph(&graph);
      if(FAILED(hr))
        printLog("avi_dshow: getfiltergraph failed\n");
    }
  }
};

void AVIVideoEncoderDShow::Cleanup()
{
  printLog("avi_dshow: cleanup\n");

  if(d->control)
  {
    if(d->video)
      d->video->Terminate();

    if(d->audio)
      d->audio->Terminate();

    d->control->Stop();
    safeRelease(d->control);
  }

  safeRelease(d->graph);
  safeRelease(d->build);
  safeRelease(d->mux);
  safeRelease(d->vid_compress);
  safeRelease(d->video);
  safeRelease(d->audio);

  delete[] d->resampleBuf;
  delete[] (unsigned char*) d->wfx;
}

void AVIVideoEncoderDShow::StartEncode()
{
  d->NeedBuild();

  if(d->graph)
  {
    HRESULT hr;

    // add the video source
    d->video = new VideoSourceFilter(fpsNum,fpsDenom,xRes,yRes);
    hr = d->graph->AddFilter(d->video,L"kkapture video source");
    if(FAILED(hr))
      printLog("avi_dshow: couldn't add filter\n");
    else
      printLog("avi_dshow: video source filter added\n");

    d->video->AddRef(); // addfilter apparently does NOT addref

    // add the video compressor
    d->vid_compress = FilterFromFourCC(d->codec);
    if(d->vid_compress)
    {
      hr = d->graph->AddFilter(d->vid_compress,L"kkapture encoder");
      if(FAILED(hr))
        printLog("avi_dshow: couldn't add compressor\n");
      else
        printLog("avi_dshow: added compressor\n");

      d->vid_compress->AddRef();
    }
    else
      printLog("avi_dshow: couldn't find compressor\n");

    // setup rendering through video compressor to mux
    hr = d->build->RenderStream(0,0,(IBaseFilter *) d->video,d->vid_compress,d->mux);
    if(FAILED(hr))
      printLog("avi_dshow: RenderStream to set up connections failed\n");
    else
      printLog("avi_dshow: video source filter connected\n");

    if(params.CaptureAudio)
    {
      // add the audio source
      d->audio = new AudioSourceFilter(44100);
      hr = d->graph->AddFilter(d->audio,L"kkapture audio source");
      if(FAILED(hr))
        printLog("avi_dshow: couldn't add audio source filter\n");
      else
        printLog("avi_dshow: audio source filter added\n");

      d->audio->AddRef();

      // setup audio rendering
      hr = d->build->RenderStream(0,0,(IBaseFilter *) d->audio,0,d->mux);
      if(FAILED(hr))
        printLog("avi_dshow: RenderStream to set up audio connections failed\n");
      else
        printLog("avi_dshow: audio source filter connected\n");
    }

    d->formatSet = true;

    // try to let it render
    hr = d->graph->QueryInterface(IID_IMediaControl,(void **) &d->control);
    if(d->control)
    {
      printLog("avi_dshow: starting graph\n");
      d->control->Run();
      printLog("avi_dshow: graph running\n");
    }
  }
}

void AVIVideoEncoderDShow::StartAudioEncode(const tWAVEFORMATEX *fmt)
{
  WAVEFORMATEX out;

  // (hardcoded) output format...
  out.wFormatTag = WAVE_FORMAT_PCM;
  out.nChannels = 2;
  out.nSamplesPerSec = 44100;
  out.nAvgBytesPerSec = 44100 * 4;
  out.nBlockAlign = 4;
  out.wBitsPerSample = 16;
  out.cbSize = 0;

  // prepare resample
  d->audioOk = d->resampler.Init(fmt,&out);

  if(d->audio && d->audioOk)
    d->audio->FeedZeroFrames(frame,fpsNum,fpsDenom);
}

AVIVideoEncoderDShow::AVIVideoEncoderDShow(const char *name,int _fpsNum,int _fpsDenom,unsigned long codec,unsigned quality)
{
  xRes = yRes = 0;
  frame = 0;
  audioSample = 0;
  fpsNum = _fpsNum;
  fpsDenom = _fpsDenom;

  d = new Internal;
  d->formatSet = false;
  d->codec = codec;
  d->build = 0;
  d->graph = 0;
  d->mux = 0;
  d->video = 0;
  d->audio = 0;
  d->vid_compress = 0;
  d->control = 0;
  d->wfx = 0;

  d->audioOk = false;
  d->resampleBuf = 0;
  d->resampleSize = 0;

  d->SetName(name);
}

AVIVideoEncoderDShow::~AVIVideoEncoderDShow()
{
  Cleanup();
  delete d;
}

void AVIVideoEncoderDShow::SetSize(int _xRes,int _yRes)
{
  xRes = _xRes;
  yRes = _yRes;
}

void AVIVideoEncoderDShow::WriteFrame(const unsigned char *buffer)
{
  // encode the frame
  if(!frame && !d->formatSet && xRes && yRes && params.CaptureVideo)
    StartEncode();

  if(d->formatSet && d->video)
  {
    d->video->PushData(buffer);
    frame++;
  }
}

void AVIVideoEncoderDShow::SetAudioFormat(const tWAVEFORMATEX *fmt)
{
  if(params.CaptureAudio)
  {
    printLog("avi_dshow: audio format %d Hz, %d bits/sample, %d channels\n",
      fmt->nSamplesPerSec,fmt->wBitsPerSample,fmt->nChannels);

    delete d->wfx;
    d->wfx = CopyFormat(fmt);
    StartAudioEncode(d->wfx);
  }
}

tWAVEFORMATEX *AVIVideoEncoderDShow::GetAudioFormat()
{
  return CopyFormat(d->wfx);
}

void AVIVideoEncoderDShow::WriteAudioFrame(const void *buffer,int samples)
{
  if(d->audio && d->audioOk)
  {
    int needSize = d->resampler.MaxOutputSamples(samples);
    if(needSize > d->resampleSize)
    {
      delete[] d->resampleBuf;
      d->resampleBuf = new short[needSize * 2]; // 2 samples (stereo)
      d->resampleSize = needSize;
    }

    // perform resampling
    int outSamples = d->resampler.Resample(buffer,d->resampleBuf,samples,false);
    if(outSamples)
      d->audio->PushData(d->resampleBuf,outSamples*4);
  }
}

#endif