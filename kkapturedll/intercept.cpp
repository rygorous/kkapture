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
#include "intercept.h"
#include "main.h"
#include <process.h>

#define COUNTOF(x) (sizeof(x)/sizeof(*x))

static bool InjectDLL(HANDLE hProcess,WCHAR *dllName)
{
  WCHAR name[2048]; // one page
  void *workArea;
  void *loadLibrary = GetProcAddress(GetModuleHandle(_T("kernel32.dll")),"LoadLibraryW");

  // Copy DLL name over
  int len = 0;
  while(dllName[len])
  {
    name[len] = dllName[len];
    if(++len == COUNTOF(name)) // DLL name is too long
      return false;
  }

  name[len] = 0; // zero-terminate

  // Write DLL name into process memory and generate LoadLibraryW call via CreateRemoteThread
  return (workArea = VirtualAllocEx(hProcess,0,4096,MEM_COMMIT,PAGE_READWRITE))
    && WriteProcessMemory(hProcess,workArea,name,sizeof(name),0)
    && CreateRemoteThread(hProcess,NULL,0,(LPTHREAD_START_ROUTINE) loadLibrary,workArea,0,0);
}

static void __cdecl WaitProcessThreadProc(void *arg)
{
  // just wait for the process to exit
  WaitForSingleObject((HANDLE) arg,INFINITE);
}

static BOOL (__stdcall *Real_CreateProcessA)(LPCSTR appName,LPSTR cmdLine,LPSECURITY_ATTRIBUTES processAttr,LPSECURITY_ATTRIBUTES threadAttr,BOOL inheritHandles,DWORD flags,LPVOID env,LPCSTR currentDir,LPSTARTUPINFOA startupInfo,LPPROCESS_INFORMATION processInfo) = CreateProcessA;
static BOOL (__stdcall *Real_CreateProcessW)(LPCWSTR appName,LPWSTR cmdLine,LPSECURITY_ATTRIBUTES processAttr,LPSECURITY_ATTRIBUTES threadAttr,BOOL inheritHandles,DWORD flags,LPVOID env,LPCWSTR currentDir,LPSTARTUPINFOW startupInfo,LPPROCESS_INFORMATION processInfo) = CreateProcessW;
static HWND (__stdcall *Real_CreateWindowExA)(DWORD dwExStyle,LPCSTR lpClassName,LPCSTR lpWindowName,DWORD dwStyle,int X,int Y,int nWidth,int nHeight,HWND hWndParent,HMENU hMenu,HINSTANCE hInstance,LPVOID lpParam) = CreateWindowExA;
static HWND (__stdcall *Real_CreateWindowExW)(DWORD dwExStyle,LPCWSTR lpClassName,LPCWSTR lpWindowName,DWORD dwStyle,int X,int Y,int nWidth,int nHeight,HWND hWndParent,HMENU hMenu,HINSTANCE hInstance,LPVOID lpParam) = CreateWindowExW;
static BOOL (__stdcall *Real_SetWindowPos)(HWND hWnd,HWND hWndInsertAfter,int X,int Y,int cx,int cy,UINT uFlags) = SetWindowPos;

static BOOL __stdcall Mine_CreateProcessA(LPCSTR appName,LPSTR cmdLine,LPSECURITY_ATTRIBUTES processAttr,
  LPSECURITY_ATTRIBUTES threadAttr,BOOL inheritHandles,DWORD flags,LPVOID env,LPCSTR currentDir,
  LPSTARTUPINFOA startupInfo,LPPROCESS_INFORMATION processInfo)
{
  PROCESS_INFORMATION localInfo;
  PROCESS_INFORMATION *pi = processInfo;

  // we always need a process info.
  if(!pi)
  {
    ZeroMemory(&localInfo,sizeof(localInfo));
    pi = &localInfo;
  }

  printLog("system: launching process \"%s\" with command line \"%s\"\n",appName,cmdLine);
  int err = CreateInstrumentedProcessA(appName,cmdLine,processAttr,threadAttr,TRUE,
    flags,env,currentDir,startupInfo,pi);

  if(err == ERR_OK)
  {
    // spawn the "wait for exit" thread for the called process
    // we need this because otherwise kkapture.exe might quit if this process decides to
    // exit soon afterwards, losing the parameter block in the process.
    _beginthread(WaitProcessThreadProc,65536,(void*)pi->hProcess);
  }

  return err == ERR_OK;
}

static BOOL __stdcall Mine_CreateProcessW(LPCWSTR appName,LPWSTR cmdLine,LPSECURITY_ATTRIBUTES processAttr,
  LPSECURITY_ATTRIBUTES threadAttr,BOOL inheritHandles,DWORD flags,LPVOID env,LPCWSTR currentDir,
  LPSTARTUPINFOW startupInfo,LPPROCESS_INFORMATION processInfo)
{
  PROCESS_INFORMATION localInfo;
  PROCESS_INFORMATION *pi = processInfo;

  // we always need a process info.
  if(!pi)
  {
    ZeroMemory(&localInfo,sizeof(localInfo));
    pi = &localInfo;
  }

  printLog("system: launching process \"%S\" with command line \"%S\"\n",appName,cmdLine);
  int err = CreateInstrumentedProcessW(appName,cmdLine,processAttr,threadAttr,TRUE,
    flags,env,currentDir,startupInfo,pi);

  if(err == ERR_OK)
  {
    // spawn the "wait for exit" thread for the called process
    // we need this because otherwise kkapture.exe might quit if this process decides to
    // exit soon afterwards, losing the parameter block in the process.
    _beginthread(WaitProcessThreadProc,65536,(void*)pi->hProcess);
  }

  return err == ERR_OK;
}

static int FinishCreateInstrumentedProcess(LPPROCESS_INFORMATION pi,WCHAR *dllPath,DWORD flags)
{
  // inject our DLL into the target process
  if(InjectDLL(pi->hProcess,dllPath))
  {
    // we're done with our evil machinations, so let the process run
    if(!(flags & CREATE_SUSPENDED))
      ResumeThread(pi->hThread);

    return ERR_OK;
  }
  else
  {
    TerminateProcess(pi->hProcess,0);
    return ERR_INSTRUMENTATION_FAILED;
  }
}

int CreateInstrumentedProcessA(LPCSTR appName,LPSTR cmdLine,
  LPSECURITY_ATTRIBUTES processAttr,LPSECURITY_ATTRIBUTES threadAttr,BOOL inheritHandles,DWORD flags,
  LPVOID env,LPCSTR currentDir,LPSTARTUPINFOA startupInfo,LPPROCESS_INFORMATION pi)
{
  // get dll path
  WCHAR dllPath[_MAX_PATH];
  GetModuleFileNameW((HMODULE) hModule,dllPath,_MAX_PATH);

  // actual process creation
  if(Real_CreateProcessA(appName,cmdLine,processAttr,threadAttr,inheritHandles,flags|CREATE_SUSPENDED,
    env,currentDir,startupInfo,pi))
  {
    return FinishCreateInstrumentedProcess(pi,dllPath,flags);
  }
  else
  {
    if(pi) pi->hProcess = INVALID_HANDLE_VALUE;
    return ERR_COULDNT_EXECUTE;
  }
}


int CreateInstrumentedProcessW(LPCWSTR appName,LPWSTR cmdLine,
  LPSECURITY_ATTRIBUTES processAttr,LPSECURITY_ATTRIBUTES threadAttr,BOOL inheritHandles,DWORD flags,
  LPVOID env,LPCWSTR currentDir,LPSTARTUPINFOW startupInfo,LPPROCESS_INFORMATION pi)
{
  // get dll path
  WCHAR dllPath[_MAX_PATH];
  GetModuleFileNameW((HMODULE) hModule,dllPath,_MAX_PATH);

  // actual process creation
  if(Real_CreateProcessW(appName,cmdLine,processAttr,threadAttr,inheritHandles,flags|CREATE_SUSPENDED,
    env,currentDir,startupInfo,pi))
  {
    return FinishCreateInstrumentedProcess(pi,dllPath,flags);
  }
  else
  {
    if(pi) pi->hProcess = INVALID_HANDLE_VALUE;
    return ERR_COULDNT_EXECUTE;
  }
}

static bool ShouldPreventTopmost(DWORD dwExStyle,DWORD dwStyle,HWND hWndParent) {
	if (hWndParent != NULL && hWndParent != GetDesktopWindow())
		return false;

	return true;
}

static HWND __stdcall Mine_CreateWindowExA(DWORD dwExStyle,LPCSTR lpClassName,LPCSTR lpWindowName,DWORD dwStyle,int X,int Y,int nWidth,int nHeight,HWND hWndParent,HMENU hMenu,HINSTANCE hInstance,LPVOID lpParam) {
	if (params.PreventTopmostWindow && ShouldPreventTopmost(dwExStyle,dwStyle,hWndParent)) dwExStyle &= ~WS_EX_TOPMOST;
	return Real_CreateWindowExA(dwExStyle,lpClassName,lpWindowName,dwStyle,X,Y,nWidth,nHeight,hWndParent,hMenu,hInstance,lpParam);
}

static HWND __stdcall Mine_CreateWindowExW(DWORD dwExStyle,LPCWSTR lpClassName,LPCWSTR lpWindowName,DWORD dwStyle,int X,int Y,int nWidth,int nHeight,HWND hWndParent,HMENU hMenu,HINSTANCE hInstance,LPVOID lpParam) {
	if (params.PreventTopmostWindow && ShouldPreventTopmost(dwExStyle,dwStyle,hWndParent)) dwExStyle &= ~WS_EX_TOPMOST;
	return Real_CreateWindowExW(dwExStyle,lpClassName,lpWindowName,dwStyle,X,Y,nWidth,nHeight,hWndParent,hMenu,hInstance,lpParam);
}

static BOOL __stdcall Mine_SetWindowPos(HWND hWnd,HWND hWndInsertAfter,int X,int Y,int cx,int cy,UINT uFlags) {
	if (params.PreventTopmostWindow) {
		if (hWndInsertAfter == HWND_TOPMOST) hWndInsertAfter = HWND_TOP;
	}

	return Real_SetWindowPos(hWnd,hWndInsertAfter,X,Y,cx,cy,uFlags);
}

void initProcessIntercept()
{
  HookFunction(&Real_CreateProcessA,Mine_CreateProcessA);
  HookFunction(&Real_CreateProcessW,Mine_CreateProcessW);
  HookFunction(&Real_CreateWindowExA,Mine_CreateWindowExA);
  HookFunction(&Real_CreateWindowExW,Mine_CreateWindowExW);
  HookFunction(&Real_SetWindowPos,Mine_SetWindowPos);
}