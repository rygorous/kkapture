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
#include "videocapturetimer.h"
#include "video.h"
#include <malloc.h>
#include <process.h>

#pragma comment(lib, "winmm.lib")

// events
static HANDLE nextFrameEvent = 0;
static HANDLE resyncEvent = 0;
static HANDLE noOneWaiting = 0;

static HANDLE stuckTimer = 0;
static HANDLE stuckThread = 0;
static HANDLE endStuckEvent = 0;

static LONGLONG perfFrequency = 0;
static volatile LONG resyncCounter = 0;
static volatile LONG waitCounter = 0;

// trampolines
static BOOL (__stdcall *Real_QueryPerformanceFrequency)(LARGE_INTEGER *lpFrequency) = QueryPerformanceFrequency;
static BOOL (__stdcall *Real_QueryPerformanceCounter)(LARGE_INTEGER *lpPerformaceCount) = QueryPerformanceCounter;
static DWORD (__stdcall *Real_GetTickCount)() = GetTickCount;
static DWORD (__stdcall *Real_timeGetTime)() = timeGetTime;
static MMRESULT (__stdcall *Real_timeGetSystemTime)(MMTIME *pmmt,UINT cbmmt) = timeGetSystemTime;
VOID (__stdcall *Real_Sleep)(DWORD dwMilliseconds) = Sleep;
DWORD (__stdcall *Real_WaitForSingleObject)(HANDLE hHandle,DWORD dwMilliseconds) = WaitForSingleObject;
static DWORD (__stdcall *Real_WaitForMultipleObjects)(DWORD nCount,CONST HANDLE *lpHandles,BOOL bWaitAll,DWORD dwMilliseconds) = WaitForMultipleObjects;
static DWORD (__stdcall *Real_MsgWaitForMultipleObjects)(DWORD nCount,CONST HANDLE *lpHandles,BOOL bWaitAll,DWORD dwMilliseconds,DWORD dwWakeMask) = MsgWaitForMultipleObjects;
static void (__stdcall *Real_GetSystemTimeAsFileTime)(FILETIME *time) = GetSystemTimeAsFileTime;
static void (__stdcall *Real_GetSystemTime)(SYSTEMTIME *time) = GetSystemTime;
static MMRESULT (__stdcall *Real_timeSetEvent)(UINT uDelay,UINT uResolution,LPTIMECALLBACK fptc,DWORD_PTR dwUser,UINT fuEvent) = timeSetEvent;
static MMRESULT (__stdcall *Real_timeKillEvent)(UINT uTimerID) = timeKillEvent;
static UINT_PTR (__stdcall *Real_SetTimer)(HWND hWnd,UINT_PTR uIDEvent,UINT uElapse,TIMERPROC lpTimerFunc) = SetTimer;

// if timer functions are called frequently in a single frame, assume the app is waiting for the
// current time to change and advance it. this is the threshold for "frequent" calls.
static const LONG MAX_TIMERQUERY_PER_FRAME = 256*1024;

// timer seeds
static bool TimersSeeded = false;
static CRITICAL_SECTION TimerSeedLock;
static LARGE_INTEGER firstTimeQPC;
static DWORD firstTimeTGT;
static FILETIME firstTimeGSTAFT;
static volatile LONG timerHammeringCounter = 0;

static void seedAllTimers()
{
  if(!TimersSeeded)
  {
    EnterCriticalSection(&TimerSeedLock);

    if(!TimersSeeded)
    {
      Real_QueryPerformanceCounter(&firstTimeQPC);
      firstTimeTGT = Real_timeGetTime();
      Real_GetSystemTimeAsFileTime(&firstTimeGSTAFT);

      // never actually mark timers as seeded in the first frame (i.e. before anything
      // was presented) - you get problems with config dialogs etc. otherweise
      TimersSeeded = getFrameTiming() != 0;
    }

    LeaveCriticalSection(&TimerSeedLock);
  }
}

static int getFrameTimingAndSeed()
{
  if(params.FrequentTimerCheck && InterlockedIncrement(&timerHammeringCounter) == MAX_TIMERQUERY_PER_FRAME)
  {
    printLog("timing: application is hammering timer calls, advancing time. (frame = %d)\n",getFrameTiming());
    skipFrame();
  }

  int frame = getFrameTiming();
  seedAllTimers();
  return frame;
}

// actual timing functions

BOOL __stdcall Mine_QueryPerformanceFrequency(LARGE_INTEGER *lpFrequency)
{
  if(lpFrequency)
    lpFrequency->QuadPart = perfFrequency;

  return TRUE;
}

BOOL __stdcall Mine_QueryPerformanceCounter(LARGE_INTEGER *lpCounter)
{
  int frame = getFrameTimingAndSeed();
  if(lpCounter)
    lpCounter->QuadPart = firstTimeQPC.QuadPart + ULongMulDiv(perfFrequency,frame*frameRateDenom,frameRateScaled);

  return TRUE;
}

DWORD __stdcall Mine_GetTickCount()
{
  // before the first frame is finished, time still progresses normally
  int frame = getFrameTimingAndSeed();
  return firstTimeTGT + UMulDiv(frame,1000*frameRateDenom,frameRateScaled);
}

DWORD __stdcall Mine_timeGetTime()
{
  int frame = getFrameTimingAndSeed();
  return firstTimeTGT + UMulDiv(frame,1000*frameRateDenom,frameRateScaled);
}

MMRESULT __stdcall Mine_timeGetSystemTime(MMTIME *pmmt,UINT cbmmt)
{
  return Real_timeGetSystemTime(pmmt,cbmmt);
}

void __stdcall Mine_GetSystemTimeAsFileTime(FILETIME *time)
{
  int frame = getFrameTimingAndSeed();

  LONGLONG baseTime = *((LONGLONG *) &firstTimeGSTAFT);
  LONGLONG elapsedSince = ULongMulDiv(10000000,frame * frameRateDenom,frameRateScaled);
  LONGLONG finalTime = baseTime + elapsedSince;

  *((LONGLONG *) time) = finalTime;
}

void __stdcall Mine_GetSystemTime(SYSTEMTIME *time)
{
  FILETIME filetime;

  Mine_GetSystemTimeAsFileTime(&filetime);
  FileTimeToSystemTime(&filetime,time);
}

// --- event timers

static const int MaxEventTimers = 256;

struct EventTimerDef
{
  UINT Delay;
  LPTIMECALLBACK Callback;
  DWORD_PTR User;
  UINT Flags;
  int Counter;
  bool Dead;
  bool Used;
};

static EventTimerDef EventTimer[MaxEventTimers];
static CRITICAL_SECTION TimerAllocLock;

static MMRESULT __stdcall Mine_timeSetEvent(UINT uDelay,UINT uResolution,LPTIMECALLBACK fptc,DWORD_PTR dwUser,UINT fuEvent)
{
  if(!uDelay) // don't even try to create a timer without delay
    return 0;

  EnterCriticalSection(&TimerAllocLock);

  // try to find a free slot
  int i;
  for(i=0;i<MaxEventTimers;i++)
    if(!EventTimer[i].Used)
      break;

  if(i == MaxEventTimers) // no free slot found
  {
    printLog("timing: OUT OF EVENT TIMER SLOTS.\n");
    LeaveCriticalSection(&TimerAllocLock);
    return 0;
  }

  // fill out timer struct
  EventTimerDef *timer = &EventTimer[i];
  timer->Delay = uDelay;
  timer->Counter = uDelay;
  timer->Callback = fptc;
  timer->User = dwUser;
  timer->Flags = fuEvent;
  timer->Dead = false;
  timer->Used = true;

  LeaveCriticalSection(&TimerAllocLock);
  return i + 1;
}

static MMRESULT __stdcall Mine_timeKillEvent(UINT uTimerID)
{
  if(!uTimerID || uTimerID > MaxEventTimers)
    return MMSYSERR_INVALPARAM;

  EventTimerDef *timer = &EventTimer[uTimerID-1];
  if(!timer->Used)
    return MMSYSERR_INVALPARAM;

  timer->Used = false;
  return TIMERR_NOERROR;
}

static void FireTimer(UINT index)
{
  EventTimerDef *timer = &EventTimer[index];

  if(timer->Flags & TIME_CALLBACK_EVENT_SET)
    SetEvent((HANDLE) timer->Callback);
  else if(timer->Flags & TIME_CALLBACK_EVENT_PULSE)
    PulseEvent((HANDLE) timer->Callback);
  else // function
    timer->Callback(index+1,0,timer->User,0,0);
}

static void ProcessEventTimers(int TimeElapsed)
{
  EnterCriticalSection(&TimerAllocLock);

  for(int i=0;i<MaxEventTimers;i++)
  {
    EventTimerDef *timer = &EventTimer[i];
    
    if(timer->Used && !timer->Dead)
    {
      timer->Counter -= TimeElapsed;
      while(timer->Counter <= 0)
      {
        FireTimer(i);
        if(timer->Flags & TIME_PERIODIC)
          timer->Counter += timer->Delay;
        else
        {
          timer->Dead = true;
          break;
        }
      }
    }
  }

  LeaveCriticalSection(&TimerAllocLock);
}

// --- user32 timers

static UINT_PTR __stdcall Mine_SetTimer(HWND hWnd,UINT_PTR nIDEvent,UINT uElapse,TIMERPROC lpTimerFunc)
{
  return Real_SetTimer(hWnd,nIDEvent,1,lpTimerFunc);
}

// --- wait scheduling

static void IncrementWaiting()
{
  InterlockedIncrement(&waitCounter);
}

static void DecrementWaiting()
{
  if(InterlockedDecrement(&waitCounter) == 0)
    SetEvent(noOneWaiting);
}

// --- everything that sleeps may accidentially take more than one frame.
// this causes soundsystems to bug, so we have to fix it here.

// NOTES: "Wonder" by Sunflower uses Sleep(30) in its audio handling thread to keep time with
//        the sound card. This must be propely maintained or the audio will stutter or run way
//        too fast and then graphics skip out of confusion. The demo sound library supports
//        DirectSound and will use this sense of timing for the audio buffer. If you select the
//        waveOut* support instead, it sets up one WAVEHDR buffer which it then sets the
//        WHDR_BEGINLOOP|WHDR_ENDLOOP flags apparently to mimic DirectSound looping buffer
//        behavior with waveOutWrite and such. This method of audio output is good enough that
//        it happens to work but you might hear erratic audio errors every now and then.
//        On a Windows 7 system, the audio is OK but glitches out about once every 1-2 minutes.
//        Windows 11 doesn't handle it well at all. You get the proper sound but it's constantly
//        a bit glitchy. For this reason, this is one of the few demos where you want to explicitly
//        clear the "Make sleep last one frame" option because setting it throws audio timing way
//        off.

VOID __stdcall Mine_Sleep(DWORD dwMilliseconds)
{
  // Bugfix: Standard Windows common controls will stall a lot if we enforce Sleep() policy here at all times,
  //         at least in Windows 11, where the dropdown control will call Sleep(1) when you click the down arrow.
  if(dwMilliseconds >= 2)
  {
	if(params.MakeSleepsLastOneFrame || getFrameTiming() == 0) {
		int stopAfterFrame = -1;
		DWORD pt,ct,dt;

		if (dwMilliseconds == INFINITE) dwMilliseconds = 30000;

		pt = Real_GetTickCount();
		while (dwMilliseconds != 0 && !exitNextFrame) {
			Real_WaitForSingleObject(resyncEvent,INFINITE);

			if (stopAfterFrame == -1)
				stopAfterFrame = getFrameTiming() + params.Microframes - 1;
			else if (getFrameTiming() > stopAfterFrame)
				break;

			IncrementWaiting();
			Real_WaitForSingleObject(nextFrameEvent,dwMilliseconds);
			DecrementWaiting();

			ct = Real_GetTickCount();
			dt = ct - pt;
			pt = ct;

			if (dwMilliseconds >= dt)
				dwMilliseconds -= dt;
			else
				dwMilliseconds = 0;
		}
	}
	else {
		// Fiddling with this for more accuracy seems to cause more problems, do what normal KKapture does for now
		Real_WaitForSingleObject(resyncEvent,INFINITE);
		IncrementWaiting();
		Real_WaitForSingleObject(nextFrameEvent,params.SleepTimeout);
		DecrementWaiting();
	}
  }
  else
    Real_Sleep(dwMilliseconds);
}

DWORD __stdcall Mine_WaitForSingleObject(HANDLE hHandle,DWORD dwMilliseconds)
{
  if(dwMilliseconds <= 0x7fffffff)
  {
    Real_WaitForSingleObject(resyncEvent,INFINITE);
    IncrementWaiting();

    HANDLE handles[] = { hHandle, nextFrameEvent };
    DWORD result = Real_WaitForMultipleObjects(2,handles,FALSE,dwMilliseconds);

    DecrementWaiting();

    if(result == WAIT_OBJECT_0+1)
      result = WAIT_TIMEOUT;

    return result;
  }
  else
    return Real_WaitForSingleObject(hHandle,dwMilliseconds);
}

DWORD __stdcall Mine_WaitForMultipleObjects(DWORD nCount,CONST HANDLE *lpHandles,BOOL bWaitAll,DWORD dwMilliseconds)
{
  // infinite waits are always passed through
  if(dwMilliseconds >= 0x7fffffff)
    return Real_WaitForMultipleObjects(nCount,lpHandles,bWaitAll,dwMilliseconds);
  else
  {
    // waitalls are harder to fake, so we just clamp them to timeout 0.
    if(bWaitAll)
      return Real_WaitForMultipleObjects(nCount,lpHandles,TRUE,0);
    else
    {
      // we can't use new/delete, this might be called from a context
      // where they don't work (such as after clib deinit)
      HANDLE *handles = (HANDLE *) _alloca((nCount + 1) * sizeof(HANDLE));
      memcpy(handles,lpHandles,nCount*sizeof(HANDLE));
      handles[nCount] = nextFrameEvent;

      Real_WaitForSingleObject(resyncEvent,INFINITE);
      IncrementWaiting();
  
      DWORD result = Real_WaitForMultipleObjects(nCount+1,handles,FALSE,dwMilliseconds);
      if(result == WAIT_OBJECT_0+nCount)
        result = WAIT_TIMEOUT;

      DecrementWaiting();
  
      return result;
    }
  }
}

DWORD __stdcall Mine_MsgWaitForMultipleObjects(DWORD nCount,CONST HANDLE *lpHandles,BOOL bWaitAll,DWORD dwMilliseconds,DWORD dwWakeMask)
{
  // infinite waits are always passed through
  if(dwMilliseconds >= 0x7fffffff)
    return Real_MsgWaitForMultipleObjects(nCount,lpHandles,bWaitAll,dwMilliseconds,dwWakeMask);
  else
  {
    // waitalls are harder to fake, so we just clamp them to timeout 0.
    if(bWaitAll)
      return Real_MsgWaitForMultipleObjects(nCount,lpHandles,TRUE,0,dwWakeMask);
    else
    {
      // we can't use new/delete, this might be called from a context
      // where they don't work (such as after clib deinit)
      HANDLE *handles = (HANDLE *) _alloca((nCount + 1) * sizeof(HANDLE));
      memcpy(handles,lpHandles,nCount*sizeof(HANDLE));
      handles[nCount] = nextFrameEvent;

      Real_WaitForSingleObject(resyncEvent,INFINITE);
      IncrementWaiting();
  
      DWORD result = Real_MsgWaitForMultipleObjects(nCount+1,handles,FALSE,dwMilliseconds,dwWakeMask);
      if(result == WAIT_OBJECT_0+nCount)
        result = WAIT_TIMEOUT;

      DecrementWaiting();
  
      return result;
    }
  }
}

static int currentFrame = 0;
static int realStartTime = 0;

static unsigned int __stdcall stuckThreadProc(void *arg)
{
  HANDLE handles[2];

  handles[0] = endStuckEvent;
  handles[1] = stuckTimer;

  while(Real_WaitForMultipleObjects(2,handles,FALSE,INFINITE) != WAIT_OBJECT_0)
  {
    printLog("timing: frame %d timed out, advancing time manually...\n", currentFrame);
    skipFrame();
    //nextFrame();
  }

  return 0;
}

void initTiming(bool interceptAnything)
{
  timeBeginPeriod(1);

  nextFrameEvent = CreateEvent(0,TRUE,FALSE,0);
  resyncEvent = CreateEvent(0,TRUE,TRUE,0);
  noOneWaiting = CreateEvent(0,TRUE,FALSE,0);
  stuckTimer = CreateWaitableTimer(0,TRUE,0);
  endStuckEvent = CreateEvent(0,TRUE,FALSE,0);

  memset(EventTimer,0,sizeof(EventTimer));
  InitializeCriticalSection(&TimerAllocLock);
  InitializeCriticalSection(&TimerSeedLock);

  LARGE_INTEGER freq;
  QueryPerformanceFrequency(&freq);
  perfFrequency = freq.QuadPart;

  if(interceptAnything)
  {
    HookFunction(&Real_QueryPerformanceFrequency, Mine_QueryPerformanceFrequency);
    HookFunction(&Real_QueryPerformanceCounter, Mine_QueryPerformanceCounter);
    HookFunction(&Real_GetTickCount, Mine_GetTickCount);
    HookFunction(&Real_timeGetTime, Mine_timeGetTime);
    HookFunction(&Real_Sleep, Mine_Sleep);
    HookFunction(&Real_WaitForSingleObject, Mine_WaitForSingleObject);
    HookFunction(&Real_WaitForMultipleObjects, Mine_WaitForMultipleObjects);
    HookFunction(&Real_MsgWaitForMultipleObjects, Mine_MsgWaitForMultipleObjects);
    HookFunction(&Real_GetSystemTimeAsFileTime, Mine_GetSystemTimeAsFileTime);
    HookFunction(&Real_GetSystemTime, Mine_GetSystemTime);
    HookFunction(&Real_timeSetEvent, Mine_timeSetEvent);
    HookFunction(&Real_timeKillEvent, Mine_timeKillEvent);
    HookFunction(&Real_SetTimer, Mine_SetTimer);
  }

  stuckThread = (HANDLE) _beginthreadex(0,0,stuckThreadProc,0,0,0);
}

void doneTiming()
{
  // terminate "stuck" thread
  SetEvent(endStuckEvent);
  Real_WaitForSingleObject(stuckThread,500);
  CloseHandle(stuckThread);
  CloseHandle(stuckTimer);

  // make sure all currently active waits are finished
  ResetEvent(resyncEvent);
  SetEvent(nextFrameEvent);
  if(waitCounter)
    ResetEvent(noOneWaiting);
  else
    SetEvent(noOneWaiting);

  while(Real_WaitForSingleObject(noOneWaiting,5) == WAIT_TIMEOUT)
    if(!waitCounter)
      break;

  // these functions depend on critical sections that we're about to delete.
  UnhookFunction(&Real_timeSetEvent);
  UnhookFunction(&Real_timeKillEvent);
  UnhookFunction(&Real_SetTimer);
  EnterCriticalSection(&TimerAllocLock);
  LeaveCriticalSection(&TimerAllocLock);
  DeleteCriticalSection(&TimerAllocLock);
  
  EnterCriticalSection(&TimerSeedLock);
  TimersSeeded = true;
  LeaveCriticalSection(&TimerSeedLock);
  DeleteCriticalSection(&TimerSeedLock);

  // we have to remove those, because code we call on deinitilization (especially directshow related)
  // might be using them.
  UnhookFunction(&Real_Sleep);
  UnhookFunction(&Real_WaitForSingleObject);
  UnhookFunction(&Real_WaitForMultipleObjects);
  UnhookFunction(&Real_MsgWaitForMultipleObjects);

  CloseHandle(nextFrameEvent);
  CloseHandle(resyncEvent);
  CloseHandle(noOneWaiting);
  CloseHandle(endStuckEvent);

  int runTime = Real_timeGetTime() - realStartTime;
  timeEndPeriod(1);

  if(runTime)
  {
    int rate = MulDiv(currentFrame,100*1000,runTime);
    printLog("timing: %d.%02d frames per second on average\n",rate/100,rate%100);
  }
}

void graphicsInitTiming()
{
  if(params.EnableAutoSkip)
  {
    LARGE_INTEGER due;
    due.QuadPart = -10*1000*__int64(params.FirstFrameTimeout);
    SetWaitableTimer(stuckTimer,&due,0,0,0,FALSE);
  }
}

void resetTiming()
{
  currentFrame = 0;
}

void nextFrameTiming()
{
  ResetEvent(resyncEvent);
  SetEvent(nextFrameEvent);
  if(waitCounter)
    ResetEvent(noOneWaiting);
  else
    SetEvent(noOneWaiting);

  while(Real_WaitForSingleObject(noOneWaiting,5) == WAIT_TIMEOUT)
    if(!waitCounter)
      break;

  ResetEvent(nextFrameEvent);
  SetEvent(resyncEvent);

  DWORD oldFrameTime = UMulDiv(currentFrame,1000*frameRateDenom,frameRateScaled);
  DWORD newFrameTime = UMulDiv(currentFrame+1,1000*frameRateDenom,frameRateScaled);
  ProcessEventTimers(newFrameTime - oldFrameTime);

  if(!currentFrame)
    realStartTime = Real_timeGetTime();

  if(params.EnableAutoSkip)
  {
    LARGE_INTEGER due;
    due.QuadPart = -10*1000*__int64(params.FrameTimeout);
    SetWaitableTimer(stuckTimer,&due,0,0,0,FALSE);
  }

  if(exitNextFrame)
  {
    printLog("main: clean exit requested, doing my best...\n");
    ExitProcess(0);
  }

  // make sure there's always a message in the queue for next frame
  // (some old hjb intros stop when there's no new messages)
  PostMessage(GetForegroundWindow(),WM_NULL,0,0);
  currentFrame++;
  timerHammeringCounter = 0;
}

int getFrameTiming()
{
  return currentFrame;
}