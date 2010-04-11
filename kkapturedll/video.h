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

#ifndef __VIDEO_H__
#define __VIDEO_H__

class VideoEncoder;

class VideoCaptureDataLock
{
public:
  VideoCaptureDataLock();
  ~VideoCaptureDataLock();
};

// encoder
VideoEncoder *createVideoEncoder(const char *filename);
void videoStartNextPart(bool autoSize=true);

void videoNeedEncoder();

// setup
void initVideo();
void doneVideo();

// init functions
void initVideo_OpenGL();
void initVideo_Direct3D8();
void initVideo_Direct3D9();
void initVideo_Direct3D10();
void initVideo_DirectDraw();
void initVideo_GDI();

// helpers
void setCaptureResolution(int width,int height);
void nextFrame();
void skipFrame();

// capture helpers
void blitAndFlipBGRAToCaptureData(unsigned char *source,unsigned pitch);
void blitAndFlipRGBAToCaptureData(unsigned char *source,unsigned pitch);

extern int captureWidth, captureHeight;
extern unsigned char *captureData;

// generic blitter class. always outputs 24bit pixels, BGR byte order.
class GenericBlitter
{
  unsigned char RTab[256],GTab[256],BTab[256];
  int RShift,GShift,BShift;
  int RMask,GMask,BMask;
  int BytesPerPixel;
  bool Paletted;

  // the inner loops for different bytes per source pixel
  void Blit1ByteSrc(unsigned char *src,unsigned char *dst,int count);
  void Blit2ByteSrc(unsigned char *src,unsigned char *dst,int count);
  void Blit3ByteSrc(unsigned char *src,unsigned char *dst,int count);
  void Blit4ByteSrc(unsigned char *src,unsigned char *dst,int count);

public:
  GenericBlitter();

  void SetInvalidFormat();
  void SetRGBFormat(int bits,unsigned redMask,unsigned greenMask,unsigned blueMask);
  void SetPalettedFormat(int bits);
  void SetPalette(const struct tagPALETTEENTRY *palette,int nEntries);

  bool IsPaletted() const;

  void BlitOneLine(unsigned char *src,unsigned char *dst,int nPixels);
};

#endif