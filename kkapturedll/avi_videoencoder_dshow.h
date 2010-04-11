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

#pragma once

#ifndef __AVI_VIDEOENCODER_DSHOW_H__
#define __AVI_VIDEOENCODER_DSHOW_H__

#include "videoencoder.h"

#if USE_DSHOW_AVI_WRITER

class AVIVideoEncoderDShow : public VideoEncoder
{
  struct Internal;
  
  int xRes,yRes;
  int frame;
  int audioSample,audioBytesSample;
  int fpsNum,fpsDenom;
  Internal *d;

  void Cleanup();
  void StartEncode();
  void StartAudioEncode(const tWAVEFORMATEX *fmt);

public:
  AVIVideoEncoderDShow(const char *name,int fpsNum,int fpsDenom,unsigned long codec,unsigned quality);
  virtual ~AVIVideoEncoderDShow();

  virtual void SetSize(int xRes,int yRes);
  virtual void WriteFrame(const unsigned char *buffer);

  virtual void SetAudioFormat(const tWAVEFORMATEX *fmt);
  virtual tWAVEFORMATEX *GetAudioFormat();
  virtual void WriteAudioFrame(const void *buffer,int samples);
};

#endif

#endif