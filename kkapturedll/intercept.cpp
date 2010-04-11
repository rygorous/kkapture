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

#define COUNTOF(x) (sizeof(x)/sizeof(*x))

static void *GetEntryPoint(HANDLE hProcess,void *baseAddr)
{
  IMAGE_DOS_HEADER doshdr;
  IMAGE_NT_HEADERS32 nthdr;
  DWORD read;
  BYTE *base = (BYTE *) baseAddr;

  if(!ReadProcessMemory(hProcess,base,&doshdr,sizeof(doshdr),&read) || read != sizeof(doshdr)
    || doshdr.e_magic != IMAGE_DOS_SIGNATURE)
    return 0;

  if(!ReadProcessMemory(hProcess,base + doshdr.e_lfanew,&nthdr,sizeof(nthdr),&read) || read != sizeof(nthdr))
    return 0;

  if(nthdr.Signature != IMAGE_NT_SIGNATURE
    || nthdr.FileHeader.Machine != IMAGE_FILE_MACHINE_I386
    || nthdr.OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC
    || !nthdr.OptionalHeader.AddressOfEntryPoint)
    return 0;

  return (void*) (base + nthdr.OptionalHeader.AddressOfEntryPoint);
}

static void *DetermineEntryPoint(HANDLE hProcess)
{
  // go through the virtual address range of the target process and try to find the executable
  // in there.

  MEMORY_BASIC_INFORMATION mbi;
  BYTE *current = (BYTE *) 0x10000; // first 64k are always reserved

  while(VirtualQueryEx(hProcess,current,&mbi,sizeof(mbi)) > 0)
  {
    // we only care about commited non-guard pages
    if(mbi.State == MEM_COMMIT && !(mbi.Protect & PAGE_GUARD))
    {
      // was an executable mapped starting here?
      void *entry = GetEntryPoint(hProcess,mbi.BaseAddress);
      if(entry)
        return entry;
    }

    current += mbi.RegionSize;
  }

  return 0; // nothing found
}

static bool PrepareInstrumentation(HANDLE hProcess,BYTE *workArea,WCHAR *dllName,void *entryPointPtr)
{
  BYTE origCode[24];
  struct bufferType
  {
    BYTE code[2048]; // code must be first field
    WCHAR data[1024];
  } buffer;
  BYTE jumpCode[5];

  DWORD offsWorkArea = (DWORD) workArea;
  BYTE *code = buffer.code;
  BYTE *loadLibrary = (BYTE *) GetProcAddress(GetModuleHandle(_T("kernel32.dll")),"LoadLibraryW");
  BYTE *entryPoint = (BYTE *) entryPointPtr;

  // Read original startup code
  DWORD amount = 0;
  memset(origCode,0xcc,sizeof(origCode));
  if(!ReadProcessMemory(hProcess,entryPoint,origCode,sizeof(origCode),&amount)
    && (amount == 0 || GetLastError() != 0x12b)) // 0x12b = request only partially completed
    return false;

  // Copy DLL name over
  int len = 0;
  while(dllName[len])
  {
    buffer.data[len] = dllName[len];
    if(++len == COUNTOF(buffer.data)) // DLL name is too long
      return false;
  }

  buffer.data[len] = 0; // zero-terminate

  // Generate Initialization hook
  code = DetourGenPushad(code);
  code = DetourGenPush(code,offsWorkArea + offsetof(bufferType,data));
  code = DetourGenCall(code,loadLibrary,workArea + (code - buffer.code));
  code = DetourGenPopad(code);

  // Copy startup code
  BYTE *sourcePtr = origCode;
  DWORD relPos;
  while((relPos = sourcePtr - origCode) < sizeof(jumpCode))
  {
    if(sourcePtr[0] == 0xe8 || sourcePtr[0] == 0xe9) // Yes, we can jump/call there too
    {
      if(sourcePtr[0] == 0xe8)
      {
        // turn a call into a push/jump sequence; this is necessary in case someone
        // decides to do computations based on the return address in the stack frame
        code = DetourGenPush(code,(UINT32) (entryPoint + relPos + 5));
        *code++ = 0xe9; // rest of flow continues with a jmp near
        sourcePtr++;
      }
      else // just copy the opcode
        *code++ = *sourcePtr++;

      // Copy target address, compensating for offset
      *((DWORD *) code) = *((DWORD *) sourcePtr)
        + entryPoint + relPos + 1             // add back original position
        - (workArea + (code - buffer.code));  // subtract new position

      code += 4;
      sourcePtr += 4;
    }
    else // not a jump/call, copy instruction
    {
      BYTE *oldPtr = sourcePtr;
      sourcePtr = DetourCopyInstruction(code,sourcePtr,0);
      code += sourcePtr - oldPtr;
    }
  }

  // Jump to rest
  code = DetourGenJmp(code,entryPoint + (sourcePtr - origCode),workArea + (code - buffer.code));

  // And prepare jump to init hook from original entry point
  DetourGenJmp(jumpCode,workArea,entryPoint);

  // Finally, write everything into process memory
  DWORD oldProtect;

  return VirtualProtectEx(hProcess,workArea,sizeof(buffer),PAGE_EXECUTE_READWRITE,&oldProtect)
    && WriteProcessMemory(hProcess,workArea,&buffer,sizeof(buffer),0)
    && VirtualProtectEx(hProcess,entryPoint,sizeof(jumpCode),PAGE_EXECUTE_READWRITE,&oldProtect)
    && WriteProcessMemory(hProcess,entryPoint,jumpCode,sizeof(jumpCode),0)
    && FlushInstructionCache(hProcess,entryPoint,sizeof(jumpCode)); 
}

static void __cdecl WaitProcessThreadProc(void *arg)
{
  // just wait for the process to exit
  WaitForSingleObject((HANDLE) arg,INFINITE);
}

DETOUR_TRAMPOLINE(BOOL __stdcall Real_CreateProcessA(LPCSTR appName,LPSTR cmdLine,LPSECURITY_ATTRIBUTES processAttr,LPSECURITY_ATTRIBUTES threadAttr,BOOL inheritHandles,DWORD flags,LPVOID env,LPCSTR currentDir,LPSTARTUPINFOA startupInfo,LPPROCESS_INFORMATION processInfo), CreateProcessA);
DETOUR_TRAMPOLINE(BOOL __stdcall Real_CreateProcessW(LPCWSTR appName,LPWSTR cmdLine,LPSECURITY_ATTRIBUTES processAttr,LPSECURITY_ATTRIBUTES threadAttr,BOOL inheritHandles,DWORD flags,LPVOID env,LPCWSTR currentDir,LPSTARTUPINFOW startupInfo,LPPROCESS_INFORMATION processInfo), CreateProcessW);

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
  int err = CreateInstrumentedProcessA(TRUE,appName,cmdLine,processAttr,threadAttr,TRUE,
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
  int err = CreateInstrumentedProcessW(TRUE,appName,cmdLine,processAttr,threadAttr,TRUE,
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
  if(void *entryPoint = DetermineEntryPoint(pi->hProcess))
  {
    // get some memory in the target processes' space for us to work with
    void *workMem = VirtualAllocEx(pi->hProcess,0,4096,MEM_COMMIT,PAGE_EXECUTE_READWRITE);

    // do all the mean initialization faking code here
    if(PrepareInstrumentation(pi->hProcess,(BYTE *) workMem,dllPath,entryPoint))
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
  else
  {
    ResumeThread(pi->hThread);
    TerminateProcess(pi->hProcess,0);
    return ERR_COULDNT_FIND_ENTRY_POINT;
  }
}

int CreateInstrumentedProcessA(BOOL newIntercept,LPCSTR appName,LPSTR cmdLine,
  LPSECURITY_ATTRIBUTES processAttr,LPSECURITY_ATTRIBUTES threadAttr,BOOL inheritHandles,DWORD flags,
  LPVOID env,LPCSTR currentDir,LPSTARTUPINFOA startupInfo,LPPROCESS_INFORMATION pi)
{
  // actual process creation
  if(!newIntercept)
  {
    // get dll path
    TCHAR dllPath[_MAX_PATH];
    GetModuleFileNameA((HMODULE) hModule,dllPath,_MAX_PATH);

    if(DetourCreateProcessWithDllA(appName,cmdLine,processAttr,threadAttr,inheritHandles,flags,env,
      currentDir,startupInfo,pi,dllPath,0))
      return ERR_OK;
    else
      return ERR_COULDNT_EXECUTE;
  }
  else
  {
    // get dll path
    WCHAR dllPath[_MAX_PATH];
    GetModuleFileNameW((HMODULE) hModule,dllPath,_MAX_PATH);

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
}


int CreateInstrumentedProcessW(BOOL newIntercept,LPCWSTR appName,LPWSTR cmdLine,
  LPSECURITY_ATTRIBUTES processAttr,LPSECURITY_ATTRIBUTES threadAttr,BOOL inheritHandles,DWORD flags,
  LPVOID env,LPCWSTR currentDir,LPSTARTUPINFOW startupInfo,LPPROCESS_INFORMATION pi)
{
  // get dll path
  WCHAR dllPath[_MAX_PATH];
  GetModuleFileNameW((HMODULE) hModule,dllPath,_MAX_PATH);

  // actual process creation
  if(!newIntercept)
  {
    if(DetourCreateProcessWithDllW(appName,cmdLine,processAttr,threadAttr,inheritHandles,flags,env,
      currentDir,startupInfo,pi,dllPath,0))
      return ERR_OK;
    else
      return ERR_COULDNT_EXECUTE;
  }
  else
  {
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
}

void initProcessIntercept()
{
  DetourFunctionWithTrampoline((PBYTE) Real_CreateProcessA, (PBYTE) Mine_CreateProcessA);
  DetourFunctionWithTrampoline((PBYTE) Real_CreateProcessW, (PBYTE) Mine_CreateProcessW);
}