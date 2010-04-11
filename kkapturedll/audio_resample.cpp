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

// note this is by no means a general audio resampler, it's only meant
// for the purposes of kkapture... so no really good resampling routines,
// only cubic spline (catmull-rom) interpolation.
//
// i'll gladly take any improved resampling code provided it uses the same
// (or a similar) interface.

#include "stdafx.h"
#include "audio_resample.h"

// ----

// evaluates Catmull-Rom spline with x_0,x_1,x_2,x_3 as given at time
// t (in [0,1]) - note index shift, x_0 should be x_-1.
static float catmullRom(float x0,float x1,float x2,float x3,float t)
{
  float a = 1.5f * (x1 - x2) + 0.5f * (x3 - x0);
  float b = x0 - 2.5f * x1 + 2.0f * x2 - 0.5f * x3;
  float c = 0.5f * (x2 - x0);
  float d = x1;

  return ((a*t+b)*t+c)*t+d;
}

// clamp to 16bit signed range
static int clamp(int x)
{
  return (x < -32768) ? -32768 : (x > 32767) ? 32767 : x;
}

static inline float getSample(const unsigned char *in)    { return (*in - 128.0f) * 256.0f; }
static inline float getSample(const short *in)            { return *in; }
static inline float getSample(const float *in)            { return *in * 32767.0f; }

// ----

// resampling inner loop, templated on the data type
template<class T> int AudioResampler::ResampleChan(T *in,short *out,int nInSamples,float *chanBuf,int stride,bool add,float amp)
{
  int outSamples = 0;
  float x0,x1,x2,x3;

  // get current state
  x0 = chanBuf[0];
  x1 = chanBuf[1];
  x2 = chanBuf[2];
  x3 = chanBuf[3];

  // resample
  while(nInSamples)
  {
    if(tFrac >= OutRate) // we need to advance by one input sample
    {
      x0 = x1;
      x1 = x2;
      x2 = x3;
      x3 = getSample(in);
      in += InChans;
      tFrac -= OutRate;
      nInSamples--;
    }
    else // generate output samples
    {
      // evaluate
      *out = (add ? *out : 0) + clamp((int) (amp * catmullRom(x0,x1,x2,x3,tFrac * invScale)));
      out += stride;
      outSamples++;

      // advance input time
      tFrac += dStep;
    }
  }

  // store current state
  chanBuf[0] = x0;
  chanBuf[1] = x1;
  chanBuf[2] = x2;
  chanBuf[3] = x3;

  return outSamples;
}

// initialize resampling
template<class T> int AudioResampler::ResampleInit(T *in,int nInSamples,float *chanBuf)
{
  int processed = 0;

  if(InitSamples == 0 && nInSamples > processed)
  {
    chanBuf[1] = getSample(in);
    in += InChans;
    processed++;
    InitSamples++;
  }

  if(InitSamples == 1 && nInSamples > processed)
  {
    chanBuf[2] = getSample(in);
    chanBuf[0] = 2*chanBuf[1] - chanBuf[2]; // symmetric extension
    in += InChans;
    processed++;
    InitSamples++;
  }

  if(InitSamples == 2 && nInSamples > processed)
  {
    chanBuf[3] = getSample(in);
    in += InChans;
    processed++;
    InitSamples++;
  }

  return processed;
}

// block processing
template<class T> int AudioResampler::ResampleBlock(T *in,short *out,int nInSamples)
{
  int count;

  // if we're not completely initialized, continue
  if(InitSamples < 3)
  {
    count = ResampleInit(in,nInSamples,ChannelBuf[0]);
    if(InChans == 2) // stereo
      ResampleInit(in+1,nInSamples,ChannelBuf[1]);

    in += count * InChans;
    nInSamples -= count;
  }

  // process the sample block
  count = 0;

  // set amplitude for stereo->mono downmix if necessary
  // should be sqrt(0.5) (equal power panning), but then i risk mixing two clamped
  // signals for extra distortion. the proper way to do this is use a higher dynamic
  // range mixing buffer (e.g. 32bit ints); maybe for the next iteration.
  float amp = (InChans == 2 && OutChans == 1) ? 0.5f : 1.0f;

  if(nInSamples)
  {
    int tOrig = tFrac;

    count = ResampleChan(in,out,nInSamples,ChannelBuf[0],OutChans,false,amp);
    if(InChans == 2) // stereo input
    {
      tFrac = tOrig;
      if(OutChans == 2) // also stereo output, can do this directly
        ResampleChan(in+1,out+1,nInSamples,ChannelBuf[1],OutChans,false,amp);
      else
        ResampleChan(in+1,out,nInSamples,ChannelBuf[1],OutChans,true,amp);
    }
    else if(InChans == 1 && OutChans == 2) // mono->stereo is easy
    {
      for(int i=0;i<count*2;i+=2)
        out[i+1] = out[i];
    }
  }

  return count;
}

// ----

AudioResampler::AudioResampler()
{
  Initialized = false;
}

AudioResampler::~AudioResampler()
{
}

static bool supportedSourceFormat(const tWAVEFORMATEX *f)
{
  bool ok = false;

  switch(f->wFormatTag)
  {
  case WAVE_FORMAT_PCM:
    ok = (f->wBitsPerSample == 8 || f->wBitsPerSample == 16);
    break;

  case WAVE_FORMAT_IEEE_FLOAT:
    ok = (f->wBitsPerSample == 32);
    break;
  }

  if(!ok)
    printLog("audio_resample: only 8/16 bit PCM or 32bit float wave data supported as source format.\n");
  else if(f->nChannels < 1 || f->nChannels > 2)
  {
    ok = false;
    printLog("audio_resample: only mono or stereo data, please.\n");
  }
  else if(f->nSamplesPerSec < 4000 || f->nSamplesPerSec > 256000)
  {
    printLog("audio_resample: unsupported source sample rate (must be between 4kHz and 256kHz)\n");
    ok = false;
  }

  return ok;
}

static bool supportedDestinationFormat(const tWAVEFORMATEX *f)
{
  bool ok = (f->wFormatTag == WAVE_FORMAT_PCM && f->wBitsPerSample == 16);
  if(!ok)
    printLog("audio_resample: only 16bit PCM wave data supported as destination format.\n");
  else if(f->nChannels < 1 || f->nChannels > 2)
  {
    ok = false;
    printLog("audio_resample: only mono or stereo data, please.\n");
  }
  else if(f->nSamplesPerSec < 4000 || f->nSamplesPerSec > 256000)
  {
    printLog("audio_resample: unsupported destination sample rate (must be between 4kHz and 256kHz)\n");
    ok = false;
  }

  return ok;
}

bool AudioResampler::Init(const tWAVEFORMATEX *srcFormat,const tWAVEFORMATEX *dstFormat)
{
  // some sanity checks for the supported format types
  if(!supportedSourceFormat(srcFormat)
    || !supportedDestinationFormat(dstFormat))
    return false;

  // plus some consistency checks
  if(srcFormat->nBlockAlign != srcFormat->nChannels * srcFormat->wBitsPerSample / 8)
  {
    printLog("audio_resample: source block align looks wrong\n");
    return false;
  }

  if(dstFormat->nBlockAlign != dstFormat->nChannels * dstFormat->wBitsPerSample / 8)
  {
    printLog("audio_resample: destination block align looks wrong\n");
    return false;
  }

  if(srcFormat->cbSize != 0 || dstFormat->cbSize != 0)
  {
    printLog("audio_resample: junk in format descriptor.\n");
    return false;
  }

  // everything seems to be ok, set up conversion...
  Initialized = true;
  InBits = srcFormat->wBitsPerSample;
  InChans = srcFormat->nChannels;
  InRate = srcFormat->nSamplesPerSec;
  OutRate = dstFormat->nSamplesPerSec;
  OutChans = dstFormat->nChannels;

  Identical = InBits == 16 && InChans == OutChans && InRate == OutRate;
  if(!Identical)
    printLog("audio_resample: converting from %d Hz %d bits %s to %d Hz %d bits %s\n",
    InRate,InBits,InChans == 1 ? "mono" : "stereo",
    OutRate,16,OutChans == 1 ? "mono" : "stereo");

  // setup resampling state
  tFrac = 0;
  dStep = InRate;
  invScale = 1.0f / OutRate;
  InitSamples = 0;

  // all ok.
  return true;
}

// upper bound for maximum number of output samples generated in one step
int AudioResampler::MaxOutputSamples(int nInSamples) const
{
  // 3 additional input samples for last sample (catmull-rom tail),
  // 1 for safety
  return MulDiv(nInSamples+3+1,OutRate,InRate);
}

// main resampling function
int AudioResampler::Resample(const void *src,short *out,int nInSamples,bool last)
{
  int count;
  unsigned char zero[3*2*4]; // enough space for 3 32bit stereo samples

  memset(zero,0,sizeof(zero));

  // in identical resampling mode, just copy
  if(Identical)
  {
    memcpy(out,src,nInSamples * OutChans * 2);
    return nInSamples;
  }

  // else we have some more work to do
  switch(InBits)
  {
  case 8:
    count = ResampleBlock((unsigned char *) src,out,nInSamples);
    if(last)
      count += ResampleBlock((unsigned char *) zero,out + count*2,3);
    break;

  case 16:
    count = ResampleBlock((short *) src,out,nInSamples);
    if(last)
      count += ResampleBlock((short *) zero,out + count*2,3);
    break;

  case 32:
    count = ResampleBlock((float *) src,out,nInSamples);
    if(last)
      count += ResampleBlock((float *) zero,out + count*2,3);
    break;
  }

  return count;
}