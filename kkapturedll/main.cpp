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
#include "tchar.h"
#include <stdio.h>
#include <process.h>

#include "video.h"
#include "videoencoder.h"
#include "intercept.h"

VideoEncoder *encoder = 0;
int frameRateScaled = 1000, frameRateDenom = 100;
bool exitNextFrame = false;
ParameterBlock params;
void *hModule = 0;

static CRITICAL_SECTION shuttingDown;
static bool initialized = false;
static HHOOK hKeyHook = 0;
static HANDLE hHookThread = 0;
static DWORD HookThreadId = 0;

// ---- forwards

static void done();
static void init();

// ---- API hooks

DETOUR_TRAMPOLINE(void __stdcall Real_ExitProcess(UINT uExitCode), ExitProcess);

void __stdcall Mine_ExitProcess(UINT uExitCode)
{
  done();
  Real_ExitProcess(uExitCode);
}

LRESULT CALLBACK LLKeyboardHook(int code,WPARAM wParam,LPARAM lParam)
{
  bool wannaExit = false;
  KBDLLHOOKSTRUCT *hook = (KBDLLHOOKSTRUCT *) lParam;

  if(code == HC_ACTION && hook->vkCode == VK_CANCEL) // ctrl+break
    wannaExit = true;

  if(code == HC_ACTION && hook->vkCode == VK_RCONTROL) // right control => try "clean" exit
    exitNextFrame = true;

  LRESULT result = CallNextHookEx(hKeyHook,code,wParam,lParam);
  if(wannaExit)
  {
    printLog("main: ctrl+break pressed, stopping recording...\n");
    Mine_ExitProcess(0);
  }

  return result;
}

static void __cdecl HookThreadProc(void *arg)
{
  HookThreadId = GetCurrentThreadId();

  // install the hook
  hKeyHook = SetWindowsHookEx(WH_KEYBOARD_LL,LLKeyboardHook,(HINSTANCE) hModule,0);
  if(!hKeyHook)
    printLog("main: couldn't install keyboard hook\n");

  // message loop
  MSG msg;
  while(GetMessage(&msg,0,0,0))
  {
    TranslateMessage(&msg);
    DispatchMessage(&msg);
  }

  printLog("main: hook thread exiting\n");

  // remove the hook
  if(hKeyHook)
  {
    UnhookWindowsHookEx(hKeyHook);
    hKeyHook = 0;
  }
}

// ---- public interface

static void done()
{
  if(!initialized)
    return;

  EnterCriticalSection(&shuttingDown);

  if(initialized)
  {
    printLog("main: shutting down...\n");

    // shutdown hook thread
    PostThreadMessage(HookThreadId,WM_QUIT,0,0);

    VideoEncoder *realEncoder = encoder;
    encoder = 0;

    doneTiming();

    delete realEncoder;

    doneSound();
    doneVideo();    

    printLog("main: everything ok, closing log.\n");
    if(params.PowerDownAfterwards)
    {
      printLog("main: powering system down.\n");

      // get token for this process
      HANDLE hToken;
      if(OpenProcessToken(GetCurrentProcess(),TOKEN_ADJUST_PRIVILEGES|TOKEN_QUERY,&hToken))
      {
        // get shutdown privilege
        TOKEN_PRIVILEGES priv;
        LookupPrivilegeValue(0,SE_SHUTDOWN_NAME,&priv.Privileges[0].Luid);
        priv.PrivilegeCount = 1;
        priv.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;

        AdjustTokenPrivileges(hToken,FALSE,&priv,0,0,0);
        if(GetLastError() == ERROR_SUCCESS)
        {
          if(!ExitWindowsEx(EWX_POWEROFF|EWX_FORCE,SHTDN_REASON_MAJOR_OTHER|SHTDN_REASON_MINOR_OTHER|SHTDN_REASON_FLAG_PLANNED))
            printLog("main: shutdown command failed.\n");
        }
        else
          printLog("main: failed to acquire shutdown privileges.\n");

        CloseHandle(hToken);
      }
      else
        printLog("main: couldn't acquire process token, can't power off.\n");
    }

    closeLog();
    initialized = false;
  }

  LeaveCriticalSection(&shuttingDown);
  DeleteCriticalSection(&shuttingDown);
}

static void init()
{
  bool error = true;
  HANDLE hMapping = OpenFileMapping(FILE_MAP_READ,FALSE,_T("__kkapture_parameter_block"));
  if(hMapping == 0) // no parameter block available.
    return;

  InitializeCriticalSection(&shuttingDown);

  // initialize params with all zero (ahem)
  initLog();
  printLog("main: initializing...\n");
  memset(&params,0,sizeof(params));

  // get file mapping containing capturing info
  ParameterBlock *block = (ParameterBlock *) MapViewOfFile(hMapping,FILE_MAP_READ,0,0,sizeof(ParameterBlock));
  if(block)
  {
    // correct version
    if(block->VersionTag == PARAMVERSION)
    {
      memcpy(&params,block,sizeof(params));
      error = false;
    }

    UnmapViewOfFile(block);
  }

  CloseHandle(hMapping);

  // if kkapture is being debugged, wait for the user to attach the debugger to this process
  if(params.IsDebugged)
  {
    // create message window
    HWND waiting = CreateWindowEx(0,"STATIC",
      "Please attach debugger now.",WS_POPUP|WS_DLGFRAME|SS_CENTER|SS_CENTERIMAGE,0,0,240,50,0,0,
      GetModuleHandle(0),0);
    SendMessage(waiting,WM_SETFONT,(WPARAM) GetStockObject(DEFAULT_GUI_FONT),TRUE);

    // center it
    RECT rcWork,rcDlg;
    SystemParametersInfo(SPI_GETWORKAREA,0,&rcWork,0);
    GetWindowRect(waiting,&rcDlg);
    SetWindowPos(waiting,0,(rcWork.left+rcWork.right-rcDlg.right+rcDlg.left)/2,
      (rcWork.top+rcWork.bottom-rcDlg.bottom+rcDlg.top)/2,-1,-1,SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);

    // show it and wait for user to attach debugger
    ShowWindow(waiting,SW_SHOW);

    while(!IsDebuggerPresent())
    {
      MSG msg;

      while(PeekMessage(&msg,0,0,0,PM_REMOVE))
      {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
      }
      
      Sleep(10);
    }

    // user has attached the debugger, bring window to foreground then destroy it
    SetForegroundWindow(waiting);
    ShowWindow(waiting,SW_HIDE);

    MessageBox(waiting,"Debugger attached, set any breakpoints etc. you need to and press OK.","kkapture",
      MB_ICONINFORMATION|MB_OK);

    DestroyWindow(waiting);
  }

  // rest of initialization code
  initTiming(true);
  initVideo();
  initSound();
  initProcessIntercept();
  printLog("main: all main components initialized.\n");

  if(error)
  {
    printLog("main: couldn't access parameter block or wrong version\n");

    frameRateScaled = 1000;
    frameRateDenom = 100;
    encoder = new DummyVideoEncoder;
  }
  else
  {
    printLog("main: reading parameter block...\n");

    frameRateScaled = params.FrameRateNum;
    frameRateDenom = params.FrameRateDenom;
    encoder = 0;
  }

  // install our hook so we get notified of process exit (hopefully)
  DetourFunctionWithTrampoline((PBYTE) Real_ExitProcess, (PBYTE) Mine_ExitProcess);
  hHookThread = (HANDLE) _beginthread(HookThreadProc,0,0);

  initialized = true;

  printLog("main: initialization done\n");
}

BOOL APIENTRY DllMain(HINSTANCE hModule, DWORD dwReason, PVOID lpReserved)
{
  if(dwReason == DLL_PROCESS_ATTACH)
  {
    ::hModule = hModule;
    init();
  }

  return TRUE;
}
