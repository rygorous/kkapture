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

#ifndef __VIDEOENCODER_H__
#define __VIDEOENCODER_H__

class VideoEncoder
{
public:
  virtual ~VideoEncoder();

  virtual void SetSize(int xRes,int yRes) = 0;
  virtual void WriteFrame(const unsigned char *buffer) = 0;

  virtual void SetAudioFormat(const struct tWAVEFORMATEX *fmt) = 0;
  virtual tWAVEFORMATEX *GetAudioFormat() = 0;
  virtual void WriteAudioFrame(const void *buffer,int samples) = 0;
};

class DummyVideoEncoder : public VideoEncoder
{
public:
  DummyVideoEncoder();
  virtual ~DummyVideoEncoder();

  virtual void SetSize(int xRes,int yRes);
  virtual void WriteFrame(const unsigned char *buffer);

  virtual void SetAudioFormat(const struct tWAVEFORMATEX *fmt);
  virtual tWAVEFORMATEX *GetAudioFormat();
  virtual void WriteAudioFrame(const void *buffer,int samples);
};

#endif