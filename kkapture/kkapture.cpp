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
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <tchar.h>
#include "../kkapturedll/main.h"
#include "../kkapturedll/intercept.h"
#include "resource.h"

#pragma comment(lib,"vfw32.lib")
#pragma comment(lib,"msacm32.lib")

#define COUNTOF(x) (sizeof(x)*sizeof*(x))

static const TCHAR RegistryKeyName[] = _T("Software\\Farbrausch\\kkapture");
static const int MAX_ARGS = _MAX_PATH*2;

// global vars for parameter passing (yes this sucks...)
static TCHAR ExeName[_MAX_PATH];
static TCHAR Arguments[MAX_ARGS];
static ParameterBlock Params;

// ---- some dialog helpers

static BOOL ErrorMsg(const TCHAR *msg,HWND hWnd=0)
{
  MessageBox(hWnd,msg,_T(".kkapture"),MB_ICONERROR|MB_OK);
  return FALSE; // so you can conveniently "return ErrorMsg(...)"
}

static BOOL EnableDlgItem(HWND hWnd,int id,BOOL bEnable)
{
  HWND hCtrlWnd = GetDlgItem(hWnd,id);
  return EnableWindow(hCtrlWnd,bEnable);
}

static void SetVideoCodecInfo(HWND hWndDlg,HIC codec)
{
  TCHAR buffer[256];

  if(codec)
  {
    ICINFO info;
    ZeroMemory(&info,sizeof(info));
    ICGetInfo(codec,&info,sizeof(info));

    _sntprintf(buffer,sizeof(buffer)/sizeof(*buffer),_T("Video codec: %S"),info.szDescription);
    buffer[255] = 0;
  }
  else
    _tcscpy(buffer,_T("Video codec: (uncompressed)"));

  SetDlgItemText(hWndDlg,IDC_VIDEOCODEC,buffer);
}

static DWORD RegQueryDWord(HKEY hk,LPCTSTR name,DWORD defValue)
{
  DWORD value,typeCode,size=sizeof(DWORD);

  if(!hk || !RegQueryValueEx(hk,name,0,&typeCode,(LPBYTE) &value,&size) == ERROR_SUCCESS
    || typeCode != REG_DWORD && size != sizeof(DWORD))
    value = defValue;

  return value;
}

static void RegSetDWord(HKEY hk,LPCTSTR name,DWORD value)
{
  RegSetValueEx(hk,name,0,REG_DWORD,(LPBYTE) &value,sizeof(DWORD));
}

static void LoadSettingsFromRegistry()
{
  HKEY hk = 0;

  if(RegOpenKeyEx(HKEY_CURRENT_USER,RegistryKeyName,0,KEY_READ,&hk) != ERROR_SUCCESS)
    hk = 0;

  Params.FrameRateNum = RegQueryDWord(hk,_T("FrameRate"),6000);
  Params.FrameRateDenom = RegQueryDWord(hk,_T("FrameRateDenom"),100);
  Params.Encoder = (EncoderType) RegQueryDWord(hk,_T("VideoEncoder"),AVIEncoderVFW);
  Params.VideoCodec = RegQueryDWord(hk,_T("AVIVideoCodec"),mmioFOURCC('D','I','B',' '));
  Params.VideoQuality = RegQueryDWord(hk,_T("AVIVideoQuality"),ICQUALITY_DEFAULT);
  Params.NewIntercept = TRUE; // always use new interception now.
  Params.SoundsysInterception = RegQueryDWord(hk,_T("SoundsysInterception"),1);
  Params.EnableAutoSkip = RegQueryDWord(hk,_T("EnableAutoSkip"),0);
  Params.FirstFrameTimeout = RegQueryDWord(hk,_T("FirstFrameTimeout"),1000);
  Params.FrameTimeout = RegQueryDWord(hk,_T("FrameTimeout"),500);
  Params.UseEncoderThread = RegQueryDWord(hk,_T("UseEncoderThread"),0);
  Params.EnableGDICapture = RegQueryDWord(hk,_T("EnableGDICapture"),0);

  if(hk)
    RegCloseKey(hk);
}

static void SaveSettingsToRegistry()
{
  HKEY hk;

  if(RegCreateKeyEx(HKEY_CURRENT_USER,RegistryKeyName,0,0,0,KEY_ALL_ACCESS,0,&hk,0) == ERROR_SUCCESS)
  {
    RegSetDWord(hk,_T("FrameRate"),Params.FrameRateNum);
    RegSetDWord(hk,_T("FrameRateDenom"),Params.FrameRateDenom);
    RegSetDWord(hk,_T("VideoEncoder"),Params.Encoder);
    RegSetDWord(hk,_T("AVIVideoCodec"),Params.VideoCodec);
    RegSetDWord(hk,_T("AVIVideoQuality"),Params.VideoQuality);
    RegSetDWord(hk,_T("NewIntercept"),Params.NewIntercept);
    RegSetDWord(hk,_T("SoundsysInterception"),Params.SoundsysInterception);
    RegSetDWord(hk,_T("EnableAutoSkip"),Params.EnableAutoSkip);
    RegSetDWord(hk,_T("FirstFrameTimeout"),Params.FirstFrameTimeout);
    RegSetDWord(hk,_T("FrameTimeout"),Params.FrameTimeout);
    RegSetDWord(hk,_T("UseEncoderThread"),Params.UseEncoderThread);
    RegCloseKey(hk);
  }
}

static int IntPow(int a,int b)
{
  int x = 1;
  while(b-- > 0)
    x *= a;

  return x;
}

// log10(x) if x is a nonnegative power of 10, -1 otherwise
static int GetPowerOf10(int x)
{
  int power = 0;
  while((x % 10) == 0)
  {
    power++;
    x /= 10;
  }

  return (x == 1) ? power : -1;
}

static void FormatRational(TCHAR *buffer,int nChars,int num,int denom)
{
  int d10 = GetPowerOf10(denom);
  if(d10 != -1) // decimal power
    _sntprintf(buffer,nChars,"%d.%0*d",num/denom,d10,num%denom);
  else // general rational
    _sntprintf(buffer,nChars,"%d/%d",num,denom);

  buffer[nChars-1] = 0;
}

static bool ParsePositiveRational(const TCHAR *buffer,int &num,int &denom)
{
  TCHAR *end;
  long intPart = _tcstol(buffer,&end,10);
  if(intPart < 0 || end == buffer)
    return false;

  if(*end == 0)
  {
    num = intPart;
    denom = 1;
    return num > 0;
  }
  else if(*end == '.') // decimal notation
  {
    end++;
    int digits = 0;
    while(end[digits] >= '0' && end[digits] <= '9')
      digits++;

    if(!digits || end[digits] != 0) // invalid character in digit string
      return false;

    denom = IntPow(10,digits);
    long fracPart = _tcstol(end,&end,10);
    if(intPart > (LONG_MAX - fracPart) / denom) // overflow
      return false;

    num = intPart * denom + fracPart;
    return true;
  }
  else if(*end == '/') // rational number
  {
    const TCHAR *rest = end + 1;
    num = intPart;
    denom = _tcstol(rest,&end,10);
    return (denom > 0) && (end != rest) && *end == 0;
  }
  else // invalid character
    return false;
}

static INT_PTR CALLBACK MainDialogProc(HWND hWndDlg,UINT uMsg,WPARAM wParam,LPARAM lParam)
{
  switch(uMsg)
  {
  case WM_INITDIALOG:
    {
      // center this window
      RECT rcWork,rcDlg;
      SystemParametersInfo(SPI_GETWORKAREA,0,&rcWork,0);
      GetWindowRect(hWndDlg,&rcDlg);
      SetWindowPos(hWndDlg,0,(rcWork.left+rcWork.right-rcDlg.right+rcDlg.left)/2,
        (rcWork.top+rcWork.bottom-rcDlg.bottom+rcDlg.top)/2,-1,-1,SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);

      // load settings from registry (or get default settings)
      LoadSettingsFromRegistry();

      // set gui values
      TCHAR buffer[32];
      FormatRational(buffer,32,Params.FrameRateNum,Params.FrameRateDenom);
      //_stprintf(buffer,"%d.%02d",Params.FrameRate/100,Params.FrameRate%100);
      SetDlgItemText(hWndDlg,IDC_FRAMERATE,buffer);

      _stprintf(buffer,"%d.%02d",Params.FirstFrameTimeout/1000,(Params.FirstFrameTimeout/10)%100);
      SetDlgItemText(hWndDlg,IDC_FIRSTFRAMETIMEOUT,buffer);

      _stprintf(buffer,"%d.%02d",Params.FrameTimeout/1000,(Params.FrameTimeout/10)%100);
      SetDlgItemText(hWndDlg,IDC_OTHERFRAMETIMEOUT,buffer);

      SendDlgItemMessage(hWndDlg,IDC_ENCODER,CB_ADDSTRING,0,(LPARAM) ".BMP/.WAV writer");
      SendDlgItemMessage(hWndDlg,IDC_ENCODER,CB_ADDSTRING,0,(LPARAM) ".AVI (VfW, segmented)");
      SendDlgItemMessage(hWndDlg,IDC_ENCODER,CB_ADDSTRING,0,(LPARAM) ".AVI (DirectShow, *unstable*)");
      SendDlgItemMessage(hWndDlg,IDC_ENCODER,CB_SETCURSEL,Params.Encoder - 1,0);

      EnableDlgItem(hWndDlg,IDC_VIDEOCODEC,Params.Encoder != BMPEncoder);
      EnableDlgItem(hWndDlg,IDC_VCPICK,Params.Encoder != BMPEncoder);

      if(Params.EnableAutoSkip)
        CheckDlgButton(hWndDlg,IDC_AUTOSKIP,BST_CHECKED);
      else
      {
        EnableDlgItem(hWndDlg,IDC_FIRSTFRAMETIMEOUT,FALSE);
        EnableDlgItem(hWndDlg,IDC_OTHERFRAMETIMEOUT,FALSE);
      }

      CheckDlgButton(hWndDlg,IDC_SOUNDSYS,Params.SoundsysInterception ? BST_CHECKED : BST_UNCHECKED);
      CheckDlgButton(hWndDlg,IDC_ENCODERTHREAD,Params.UseEncoderThread ? BST_CHECKED : BST_UNCHECKED);
      CheckDlgButton(hWndDlg,IDC_CAPTUREGDI,Params.EnableGDICapture ? BST_CHECKED : BST_UNCHECKED);

      HIC codec = ICOpen(ICTYPE_VIDEO,Params.VideoCodec,ICMODE_QUERY);
      SetVideoCodecInfo(hWndDlg,codec);
      ICClose(codec);

      // gui stuff not read from registry
      CheckDlgButton(hWndDlg,IDC_VCAPTURE,BST_CHECKED);
      CheckDlgButton(hWndDlg,IDC_ACAPTURE,BST_CHECKED);
      CheckDlgButton(hWndDlg,IDC_SLEEPLAST,BST_CHECKED);
    }
    return TRUE;

  case WM_COMMAND:
    switch(LOWORD(wParam))
    {
    case IDCANCEL:
      EndDialog(hWndDlg,0);
      return TRUE;

    case IDOK:
      {
        TCHAR frameRateStr[64];
        TCHAR firstFrameTimeout[64];
        TCHAR otherFrameTimeout[64];

        Params.VersionTag = PARAMVERSION;

        // get values
        GetDlgItemText(hWndDlg,IDC_DEMO,ExeName,_MAX_PATH);
        GetDlgItemText(hWndDlg,IDC_ARGUMENTS,Arguments,MAX_ARGS);
        GetDlgItemText(hWndDlg,IDC_TARGET,Params.FileName,_MAX_PATH);
        GetDlgItemText(hWndDlg,IDC_FRAMERATE,frameRateStr,sizeof(frameRateStr)/sizeof(*frameRateStr));
        GetDlgItemText(hWndDlg,IDC_FIRSTFRAMETIMEOUT,firstFrameTimeout,sizeof(firstFrameTimeout)/sizeof(*firstFrameTimeout));
        GetDlgItemText(hWndDlg,IDC_OTHERFRAMETIMEOUT,otherFrameTimeout,sizeof(otherFrameTimeout)/sizeof(*otherFrameTimeout));

        BOOL autoSkip = IsDlgButtonChecked(hWndDlg,IDC_AUTOSKIP) == BST_CHECKED;

        // validate everything and fill out parameter block
        HANDLE hFile = CreateFile(ExeName,GENERIC_READ,0,0,OPEN_EXISTING,0,0);
        if(hFile == INVALID_HANDLE_VALUE)
          return !ErrorMsg(_T("You need to specify a valid executable in the 'demo' field."),hWndDlg);
        else
          CloseHandle(hFile);

        int frameRateNum,frameRateDenom;
        if(!ParsePositiveRational(frameRateStr,frameRateNum,frameRateDenom)
          || frameRateNum < 0 || frameRateNum / frameRateDenom >= 1000)
          return !ErrorMsg(_T("Please enter a valid frame rate between 0 and 1000 "
            "(either as decimal or rational)."),hWndDlg);

        if(autoSkip)
        {
          double fft = atof(firstFrameTimeout);
          if(fft <= 0.0 || fft >= 3600.0)
            return !ErrorMsg(_T("'Initial frame timeout' must be between 0 and 3600 seconds."),hWndDlg);

          double oft = atof(otherFrameTimeout);
          if(oft <= 0.0 || oft >= 3600.0)
            return !ErrorMsg(_T("'Other frames timeout' must be between 0 and 3600 seconds."),hWndDlg);

          Params.FirstFrameTimeout = DWORD(fft*1000);
          Params.FrameTimeout = DWORD(oft*1000);
        }

        Params.FrameRateNum = frameRateNum;
        Params.FrameRateDenom = frameRateDenom;
        Params.Encoder = (EncoderType) (1 + SendDlgItemMessage(hWndDlg,IDC_ENCODER,CB_GETCURSEL,0,0));

        Params.CaptureVideo = IsDlgButtonChecked(hWndDlg,IDC_VCAPTURE) == BST_CHECKED;
        Params.CaptureAudio = IsDlgButtonChecked(hWndDlg,IDC_ACAPTURE) == BST_CHECKED;
        Params.SoundMaxSkip = IsDlgButtonChecked(hWndDlg,IDC_SKIPSILENCE) == BST_CHECKED ? 10 : 0;
        Params.MakeSleepsLastOneFrame = IsDlgButtonChecked(hWndDlg,IDC_SLEEPLAST) == BST_CHECKED;
        Params.SleepTimeout = 2500; // yeah, this should be configurable
        Params.NewIntercept = TRUE; // this doesn't seem to cause *any* problems, while the old interception did.
        Params.SoundsysInterception = IsDlgButtonChecked(hWndDlg,IDC_SOUNDSYS) == BST_CHECKED;
        Params.EnableAutoSkip = autoSkip;
        Params.PowerDownAfterwards = IsDlgButtonChecked(hWndDlg,IDC_POWERDOWN) == BST_CHECKED;
        Params.UseEncoderThread = IsDlgButtonChecked(hWndDlg,IDC_ENCODERTHREAD) == BST_CHECKED;
        Params.EnableGDICapture = IsDlgButtonChecked(hWndDlg,IDC_CAPTUREGDI) == BST_CHECKED;

        // save settings for next time
        SaveSettingsToRegistry();

        // that's it.
        EndDialog(hWndDlg,1);
      }
      return TRUE;

    case IDC_BDEMO:
      {
        OPENFILENAME ofn;
        TCHAR filename[_MAX_PATH];

        GetDlgItemText(hWndDlg,IDC_DEMO,filename,_MAX_PATH);

        ZeroMemory(&ofn,sizeof(ofn));
        ofn.lStructSize   = sizeof(ofn);
        ofn.hwndOwner     = hWndDlg;
        ofn.hInstance     = GetModuleHandle(0);
        ofn.lpstrFilter   = _T("Executable files (*.exe)\0*.exe\0");
        ofn.nFilterIndex  = 1;
        ofn.lpstrFile     = filename;
        ofn.nMaxFile      = sizeof(filename)/sizeof(*filename);
        ofn.Flags         = OFN_FILEMUSTEXIST|OFN_HIDEREADONLY;
        if(GetOpenFileName(&ofn))
        {
          SetDlgItemText(hWndDlg,IDC_DEMO,ofn.lpstrFile);

          // also set demo .avi file name if not yet set
          TCHAR drive[_MAX_DRIVE],dir[_MAX_DIR],fname[_MAX_FNAME],ext[_MAX_EXT],path[_MAX_PATH];
          int nChars = GetDlgItemText(hWndDlg,IDC_TARGET,fname,_MAX_FNAME);
          if(!nChars)
          {
            _tsplitpath(ofn.lpstrFile,drive,dir,fname,ext);
            _tmakepath(path,drive,dir,fname,_T(".avi"));
            SetDlgItemText(hWndDlg,IDC_TARGET,path);
          }
        }
      }
      return TRUE;

    case IDC_BTARGET:
      {
        OPENFILENAME ofn;
        TCHAR filename[_MAX_PATH];

        GetDlgItemText(hWndDlg,IDC_TARGET,filename,_MAX_PATH);

        ZeroMemory(&ofn,sizeof(ofn));
        ofn.lStructSize   = sizeof(ofn);
        ofn.hwndOwner     = hWndDlg;
        ofn.hInstance     = GetModuleHandle(0);
        ofn.lpstrFilter   = _T("AVI files (*.avi)\0*.avi\0");
        ofn.nFilterIndex  = 1;
        ofn.lpstrFile     = filename;
        ofn.nMaxFile      = sizeof(filename)/sizeof(*filename);
        ofn.Flags         = OFN_HIDEREADONLY|OFN_PATHMUSTEXIST|OFN_OVERWRITEPROMPT;
        if(GetSaveFileName(&ofn))
          SetDlgItemText(hWndDlg,IDC_TARGET,ofn.lpstrFile);
      }
      return TRUE;

    case IDC_ENCODER:
      if(HIWORD(wParam) == CBN_SELCHANGE)
      {
        LRESULT selection = SendDlgItemMessage(hWndDlg,IDC_ENCODER,CB_GETCURSEL,0,0);
        BOOL allowCodecSelect = selection != 0;

        EnableDlgItem(hWndDlg,IDC_VIDEOCODEC,allowCodecSelect);
        EnableDlgItem(hWndDlg,IDC_VCPICK,allowCodecSelect);
      }
      return TRUE;

    case IDC_VCPICK:
      {
        COMPVARS cv;
        ZeroMemory(&cv,sizeof(cv));

        cv.cbSize = sizeof(cv);
        cv.dwFlags = ICMF_COMPVARS_VALID;
        cv.fccType = ICTYPE_VIDEO;
        cv.fccHandler = Params.VideoCodec;
        cv.lQ = Params.VideoQuality;
        if(ICCompressorChoose(hWndDlg,0,0,0,&cv,0))
        {
          Params.VideoCodec = cv.fccHandler;
          Params.VideoQuality = cv.lQ;
          SetVideoCodecInfo(hWndDlg,cv.hic);

          if(cv.cbState <= sizeof(Params.CodecSpecificData))
          {
            Params.CodecDataSize = cv.cbState;
            memcpy(Params.CodecSpecificData, cv.lpState, cv.cbState);
          }
          else
            Params.CodecDataSize = 0;

          ICCompressorFree(&cv);
        }
      }
      return TRUE;

    case IDC_AUTOSKIP:
      {
        BOOL enable = IsDlgButtonChecked(hWndDlg,IDC_AUTOSKIP) == BST_CHECKED;
        EnableDlgItem(hWndDlg,IDC_FIRSTFRAMETIMEOUT,enable);
        EnableDlgItem(hWndDlg,IDC_OTHERFRAMETIMEOUT,enable);
      }
      return TRUE;
    }
    break;
  }

  return FALSE;
}

// ----

int WINAPI WinMain(HINSTANCE hInstance,HINSTANCE hPrevInstance,LPSTR lpCmdLine,int nCmdShow)
{
  if(DialogBox(hInstance,MAKEINTRESOURCE(IDD_MAINWIN),0,MainDialogProc))
  {
    Params.IsDebugged = IsDebuggerPresent() != 0;

    // create file mapping object with parameter block
    HANDLE hParamMapping = CreateFileMapping(INVALID_HANDLE_VALUE,0,PAGE_READWRITE,
      0,sizeof(ParameterBlock),_T("__kkapture_parameter_block"));

    // copy parameters
    LPVOID ParamBlock = MapViewOfFile(hParamMapping,FILE_MAP_WRITE,0,0,sizeof(ParameterBlock));
    if(ParamBlock)
    {
      memcpy(ParamBlock,&Params,sizeof(ParameterBlock));
      UnmapViewOfFile(ParamBlock);
    }

    // prepare command line
    TCHAR commandLine[_MAX_PATH+MAX_ARGS+4];
    _tcscpy(commandLine,_T("\""));
    _tcscat(commandLine,ExeName);
    _tcscat(commandLine,_T("\" "));
    _tcscat(commandLine,Arguments);

    // create process
	  STARTUPINFOA si;
	  PROCESS_INFORMATION pi;

    ZeroMemory(&si,sizeof(si));
    ZeroMemory(&pi,sizeof(pi));
    si.cb = sizeof(si);

    // change to directory that contains executable
    TCHAR exepath[_MAX_PATH];
    TCHAR drive[_MAX_DRIVE],dir[_MAX_DIR],fname[_MAX_FNAME],ext[_MAX_EXT];

    _tsplitpath(ExeName,drive,dir,fname,ext);
    _tmakepath(exepath,drive,dir,_T(""),_T(""));
    SetCurrentDirectory(exepath);

    // actually start the target
    int err = CreateInstrumentedProcessA(Params.NewIntercept,ExeName,commandLine,0,0,TRUE,
      CREATE_DEFAULT_ERROR_MODE,0,0,&si,&pi);
    switch(err)
    {
    case ERR_OK:
      // wait for target process to finish
      WaitForSingleObject(pi.hProcess,INFINITE);
      break;

    case ERR_INSTRUMENTATION_FAILED:
      ErrorMsg(_T("Startup instrumentation failed"));
      break;

    case ERR_COULDNT_FIND_ENTRY_POINT:
      ErrorMsg(_T("Couldn't determine entry point!"));
      break;

    case ERR_COULDNT_EXECUTE:
      ErrorMsg(_T("Couldn't execute target process"));
      break;
    }

    // cleanup
    CloseHandle(pi.hProcess);
    CloseHandle(hParamMapping);
  }

  return 0;
}