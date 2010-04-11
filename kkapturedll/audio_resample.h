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
// only cubic spline (catmull-rom) interpolation

#ifndef __AUDIO_RESAMPLE_H__
#define __AUDIO_RESAMPLE_H__

struct tWAVEFORMATEX;

class AudioResampler
{
  bool Initialized;
  int InBits; // currently we only have 8/16 bit integer or 32bit float
  int InChans;
  int InRate,OutRate;
  bool Identical;
  
  int OutChans;

  int tFrac;
  int dStep;
  float invScale;
  int InitSamples;
  float ChannelBuf[2][4];

  template<class T> int ResampleChan(T *in,short *out,int nInSamples,float *chanBuf,int stride,bool add,float amp);
  template<class T> int ResampleInit(T *in,int nInSamples,float *chanBuf);
  template<class T> int ResampleBlock(T *in,short *out,int nInSamples);

public:
  AudioResampler();
  ~AudioResampler();

  bool Init(const tWAVEFORMATEX *srcFormat,const tWAVEFORMATEX *dstFormat);
  int MaxOutputSamples(int nInSamples) const;
  int Resample(const void *src,short *out,int nInSamples,bool last);
};

#endif