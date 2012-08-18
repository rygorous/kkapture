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

#ifndef __MAIN_H__
#define __MAIN_H__

class VideoEncoder;

// configuration options

// writing AVIs using DShow (please read top of avi_videoencoder_dshow.cpp)
#define USE_DSHOW_AVI_WRITER      1

// global variables
extern VideoEncoder *encoder;
extern int frameRateScaled,frameRateDenom;
extern bool exitNextFrame;
extern void *hModule;

// parameter block submitted by main app
static const int PARAMVERSION = 6;

enum EncoderType
{
  DummyEncoder,
  BMPEncoder,
  AVIEncoderVFW,
  AVIEncoderDShow,
};

struct ParameterBlock
{
  unsigned VersionTag;
  TCHAR FileName[_MAX_PATH];
  int FrameRateNum,FrameRateDenom;
  EncoderType Encoder;
  DWORD VideoCodec;
  DWORD VideoQuality;

  BOOL CaptureVideo;
  BOOL CaptureAudio;

  DWORD SoundMaxSkip; // in milliseconds
  BOOL MakeSleepsLastOneFrame;
  DWORD SleepTimeout;
  BOOL NewIntercept;
  BOOL SoundsysInterception;

  BOOL EnableAutoSkip;
  DWORD FirstFrameTimeout;
  DWORD FrameTimeout;

  BOOL IsDebugged;
  BOOL PowerDownAfterwards;
  BOOL UseEncoderThread;
  BOOL EnableGDICapture;
  BOOL FrequentTimerCheck;
  BOOL VirtualFramebuffer;

  BOOL ExtraScreenMode;
  DWORD ExtraScreenWidth;
  DWORD ExtraScreenHeight;

  DWORD CodecDataSize;
  UCHAR CodecSpecificData[16384];
};

extern ParameterBlock params;

#endif