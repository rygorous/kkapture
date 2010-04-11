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
#include "mt_proxy_videoencoder.h"
#include <process.h>

static const int MAX_QUEUED = 8; // max num of commands in queue

enum QueueCommand
{
  QC_SETSIZE,
  QC_WRITEFRAME,
  QC_SETAUDIOFORMAT,
  QC_WRITEAUDIOFRAME,
};

struct QueueEntry
{
  QueueEntry *next;
  QueueCommand cmd;
  const void *ptr;
  int para1, para2;
};

struct MTProxyVideoEncoder::Internal
{
  QueueEntry *queue_head;
  QueueEntry *queue_tail;
  CRITICAL_SECTION queue_lock;
  HANDLE queue_semaphore;
  HANDLE queue_exit;
  HANDLE queue_thread;
  WAVEFORMATEX *wfx;
  int xRes, yRes;
  VideoEncoder *actualEncoder;

  void QueueAppend(QueueCommand cmd, const void *ptr, int para1, int para2)
  {
    // create queue entry
    QueueEntry* entry = new QueueEntry;
    entry->next = 0;
    entry->cmd = cmd;
    entry->ptr = ptr;
    entry->para1 = para1;
    entry->para2 = para2;

    // wait till we can submit something again
    if(Real_WaitForSingleObject(queue_semaphore, 15*1000) == WAIT_TIMEOUT)
    {
      printLog("mt_proxy_encoder: couldn't add to queue within 15 seconds, giving up and dropping frame.\n");
      delete entry;
      return;
    }

    // actually append to list
    Lock lock(queue_lock);
    if(queue_tail)
    {
      queue_tail->next = entry;
      queue_tail = entry;
    }
    else
      queue_head = queue_tail = entry;
  }

  // you need to free the QueueEntry afterwards!
  QueueEntry *QueuePop()
  {
    // pop entry off front of list
    QueueEntry *entry;
    {
      Lock lock(queue_lock);
      
      entry = queue_head;
      if(queue_head)
        queue_head = queue_head->next;
      
      if(!queue_head)
        queue_tail = 0;
    }

    // if we popped something, increase semaphore count
    if(entry)
      ReleaseSemaphore(queue_semaphore, 1, 0);

    return entry;
  }

  void QueueFree(QueueEntry *entry)
  {
    unsigned char* entryData = (unsigned char *) entry->ptr;

    delete[] entryData;
    delete entry;
  }
};

unsigned MTProxyVideoEncoder::QueueRunner(void *para)
{
  Internal *d = (Internal *) para;
  
  printLog("mt_proxy_encoder: queue runner started.\n");
  while(Real_WaitForSingleObject(d->queue_exit,10) != WAIT_OBJECT_0)
  {
    while(QueueEntry *entry=d->QueuePop())
    {
      switch(entry->cmd)
      {
      case QC_SETSIZE:
        d->actualEncoder->SetSize(entry->para1,entry->para2);
        break;

      case QC_WRITEFRAME:
        d->actualEncoder->WriteFrame((const unsigned char *) entry->ptr);
        break;

      case QC_SETAUDIOFORMAT:
        d->actualEncoder->SetAudioFormat((const tWAVEFORMATEX *) entry->ptr);
        break;

      case QC_WRITEAUDIOFRAME:
        d->actualEncoder->WriteAudioFrame(entry->ptr,entry->para1);
        break;
      }

      d->QueueFree(entry);
    }
  }

  printLog("mt_proxy_encoder: draining queue and exiting queue runner.\n");
  while(QueueEntry *entry=d->QueuePop())
    d->QueueFree(entry);

  return 0;
}

MTProxyVideoEncoder::MTProxyVideoEncoder(VideoEncoder *actual)
{
  d = new Internal;
  
  InitializeCriticalSection(&d->queue_lock);
  d->queue_head = d->queue_tail = 0;
  d->queue_semaphore = CreateSemaphore(0, MAX_QUEUED, MAX_QUEUED, 0);
  d->queue_exit = CreateEvent(0,FALSE,FALSE,0);
  d->actualEncoder = actual;
  d->xRes = d->yRes = 0;

  d->wfx = 0;

  // kick off queue runner
  d->queue_thread = (HANDLE) _beginthreadex(0, 0, QueueRunner, d, 0, 0);
}

MTProxyVideoEncoder::~MTProxyVideoEncoder()
{
  // shut down queue runner thread
  SetEvent(d->queue_exit);
  Real_WaitForSingleObject(d->queue_thread, INFINITE);

  // finally, clean up the rest
  CloseHandle(d->queue_semaphore);
  CloseHandle(d->queue_exit);
  DeleteCriticalSection(&d->queue_lock);

  delete[] (unsigned char*) d->wfx;
  delete d->actualEncoder;
  delete d;
}

void MTProxyVideoEncoder::SetSize(int xRes,int yRes)
{
  d->QueueAppend(QC_SETSIZE, 0, xRes, yRes);
  d->xRes = xRes;
  d->yRes = yRes;
}

void MTProxyVideoEncoder::WriteFrame(const unsigned char *buffer)
{
  if(d->xRes && d->yRes)
    d->QueueAppend(QC_WRITEFRAME,MakeCopy(buffer,d->xRes*d->yRes*3),0,0);
}

void MTProxyVideoEncoder::SetAudioFormat(const tWAVEFORMATEX *fmt)
{
  d->QueueAppend(QC_SETAUDIOFORMAT,CopyFormat(fmt),0,0);
  
  delete d->wfx;
  d->wfx = CopyFormat(fmt);
}

tWAVEFORMATEX *MTProxyVideoEncoder::GetAudioFormat()
{
  if(!d->wfx) return 0;
  return CopyFormat(d->wfx);
}

void MTProxyVideoEncoder::WriteAudioFrame(const void *buffer, int samples)
{
  d->QueueAppend(QC_WRITEAUDIOFRAME,
    MakeCopy(buffer,samples*d->wfx->nBlockAlign),samples,0);
}