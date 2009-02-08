/* kkapture: intrusive demo video capturing.
 * Copyright (c) 2005-2006 Fabian "ryg/farbrausch" Giesen.
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

// read 8-bit unsigned samples
float getSample(unsigned char *in)
{
  return (*in - 128.0f) * 256.0f;
}

// read 16-bit signed samples
float getSample(short *in)
{
  return *in;
}

// ----

// resampling main function for 16bit input data
template<class T> int AudioResampler::ResampleChan(T *in,short *out,int nInSamples,float *chanBuf)
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
      *out = clamp((int) catmullRom(x0,x1,x2,x3,tFrac * invScale));
      out += 2;
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

  if(nInSamples)
  {
    int tOrig = tFrac;

    count = ResampleChan(in,out,nInSamples,ChannelBuf[0]);
    if(InChans == 2) // stereo input
    {
      tFrac = tOrig;
      ResampleChan(in+1,out+1,nInSamples,ChannelBuf[1]);
    }
    else // plain channel doubling
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

bool AudioResampler::Init(const tWAVEFORMATEX *srcFormat,const tWAVEFORMATEX *dstFormat)
{
  // some sanity checks: first of all, we only support PCM formats
  if(srcFormat->wFormatTag != WAVE_FORMAT_PCM || dstFormat->wFormatTag != WAVE_FORMAT_PCM)
    return false;

  // only 8 or 16 bit input and only 16 bit output
  if((srcFormat->wBitsPerSample != 8 && srcFormat->wBitsPerSample != 16)
    || dstFormat->wBitsPerSample != 16)
    return false;

  // only mono or stereo input and stereo output
  if((srcFormat->nChannels != 1 && srcFormat->nChannels != 2)
    || dstFormat->nChannels != 2)
    return false;

  // reasonable sample rate bounds
  if(srcFormat->nSamplesPerSec < 4000 || srcFormat->nSamplesPerSec > 48000
    || dstFormat->nSamplesPerSec < 4000 || dstFormat->nSamplesPerSec > 48000)
    return false;

  // plus some consistency checks
  if(srcFormat->nBlockAlign != srcFormat->nChannels * srcFormat->wBitsPerSample / 8)
    return false;

  if(dstFormat->nBlockAlign != dstFormat->nChannels * dstFormat->wBitsPerSample / 8)
    return false;

  if(srcFormat->cbSize != 0 || dstFormat->cbSize != 0)
    return false;

  // everything seems to be ok, set up conversion...
  Initialized = true;
  In8Bit = srcFormat->wBitsPerSample == 8;
  InChans = srcFormat->nChannels;
  InRate = srcFormat->nSamplesPerSec;
  OutRate = dstFormat->nSamplesPerSec;

  Identical = !In8Bit && InChans == 2 && InRate == OutRate;
  if(!Identical)
    printLog("audio_resample: converting from %d Hz %d bits %s to %d Hz %d bits %s\n",
    InRate,In8Bit ? 8 : 16,InChans == 1 ? "mono" : "stereo",
    OutRate,16,"stereo");

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
  unsigned char zero[3*2*2]; // enough space for 3 16bit stereo samples

  memset(zero,0,sizeof(zero));

  // in identical resampling mode, just copy
  if(Identical)
  {
    memcpy(out,src,nInSamples * 4);
    return nInSamples;
  }

  // else we have some more work to do
  if(In8Bit)
  {
    count = ResampleBlock((unsigned char *) src,out,nInSamples);
    if(last)
      count += ResampleBlock((unsigned char *) zero,out + count*2,3);
  }
  else
  {
    count = ResampleBlock((short *) src,out,nInSamples);
    if(last)
      count += ResampleBlock((short *) zero,out + count*2,3);
  }

  return count;
}