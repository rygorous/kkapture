/* kkapture: intrusive demo video capturing.
 * Copyright (c) 2005-2006 Fabian "ryg/farbrausch" Giesen.
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

// ---- logging functions

static FILE *logFile = 0;

void initLog()
{
  logFile = fopen("kkapturelog.txt","w");
}

void closeLog()
{
  if(logFile)
  {
    fclose(logFile);
    logFile = 0;
  }
}

void printLog(const char *format,...)
{
  if(logFile)
  {
    va_list arg;
    va_start(arg,format);
    vfprintf(logFile,format,arg);
    va_end(arg);

    fflush(logFile);
  }
}

// ---- vtable patching

PBYTE DetourCOM(IUnknown *obj,int vtableOffs,PBYTE newFunction)
{
  PBYTE **vtblPtr = (PBYTE **) obj;
  PBYTE *vtbl = *vtblPtr;
  PBYTE target = vtbl[vtableOffs];
  
  return DetourFunction(target,newFunction);
}

// ---- long integer arithmetic

DWORD UMulDiv(DWORD a,DWORD b,DWORD c)
{
  __asm
  {
    mov   eax, [a];
    mul   [b];
    div   [c];
    mov   [a], eax;
  }

  return a;
}

ULONGLONG ULongMulDiv(ULONGLONG a,DWORD b,DWORD c)
{
  __asm
  {
    mov   eax, dword ptr [a+4];
    mul   [b];
    mov   ecx, edx;             // ecx=top 32 bits
    mov   ebx, eax;             // ebx=middle 32 bits
    mov   eax, dword ptr [a];
    mul   [b];
    add   ebx, edx;

    // now eax=lower 32 bits, ebx=middle 32 bits, ecx=top 32 bits
    mov   edx, ecx;
    xchg  eax, ebx;
    div   [c];                  // high divide
    mov   dword ptr [a+4], eax;
    mov   eax, ebx;
    div   [c];                  // low divide
    mov   dword ptr [a], eax;
  }

  return a;
}