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

#ifndef __UTIL_H__
#define __UTIL_H__

// ---- logging functions
void initLog();
void closeLog();
void printLog(const char *format,...);
void printLogHex(void *buffer,int size);

// ---- logging used to debug kkapture
//#define TRACE(x) printLog x
#define TRACE(x)

// ---- lock holder for critical sections
class Lock
{
  CRITICAL_SECTION* CS;

public:
  Lock(CRITICAL_SECTION *cs) : CS(cs)   { EnterCriticalSection(CS); }
  Lock(CRITICAL_SECTION &cs) : CS(&cs)  { EnterCriticalSection(CS); }

  ~Lock()                               { LeaveCriticalSection(CS); }
};

// ---- vtable patching
PBYTE DetourCOM(IUnknown *obj,int vtableOffs,PBYTE newFunction);

// ---- long integer arithmetic
// multiply two 32-bit numbers, yielding a 64-bit temporary result,
// then divide by another 32-bit number
DWORD UMulDiv(DWORD a,DWORD b,DWORD c);

// multiply a 64-bit number by a 32-bit number, yielding a 96-bit
// temporary result, then divide by a 32-bit number
ULONGLONG ULongMulDiv(ULONGLONG a,DWORD b,DWORD c);

// ---- really misc stuff
void *MakeCopy(const void *src,int size); // copy allocated as unsigned char[]

// Copies and simplifies a WAVEFORMATEX
struct tWAVEFORMATEX *CopyFormat(const tWAVEFORMATEX *src);

// Get downmix (bounce) format for a source WAVEFORMATEX
struct tWAVEFORMATEX *BounceFormat(const tWAVEFORMATEX *src);

#endif