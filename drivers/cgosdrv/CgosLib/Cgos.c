/*---------------------------------------------------------------------------
 *
 * Copyright (c) 2015, congatec AG. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the BSD 2-clause license which 
 * accompanies this distribution. 
 *
 * This program is distributed in the hope that it will be useful, 
 * but WITHOUT ANY WARRANTY; without even the implied warranty of 
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. 
 * See the BSD 2-clause license for more details.
 *
 * The full text of the license may be found at:        
 * http://opensource.org/licenses/BSD-2-Clause   
 *
 *---------------------------------------------------------------------------
 */
 
//***************************************************************************

#include "CgosLib.h"
#include "CgosBld.h"

//***************************************************************************

unsigned int dwLibInitCount=0;
void *hDriver=CGOS_INVALID_HANDLE;
unsigned int dwDrvVersion=0;

// __declspec(thread)
unsigned int dwLastError=0;
unsigned int *pdwLastError=&dwLastError;

//***************************************************************************

#define ErrFALSE ((*pdwLastError=~0), FALSE)

//***************************************************************************

/*
  The flags are:
    0: return TRUE on success, FALSE on failure
    1: return the first return value pret0
    2: reserved
    4: allow the returned number of bytes in pout to be smaller then lenout
    8: pin and pout contain the length in the first ulong
*/

unsigned int CallDrvPlain(unsigned int flags, unsigned int fct, unsigned int handle,
    unsigned int type, unsigned int par0, unsigned int par1,
    unsigned int par2, unsigned int par3, unsigned int *pret0, unsigned int *pret1)
  {
  CGOSIOCTLIN cin;
  CGOSIOCTLOUT cout;
  unsigned int cb;
  if (!dwLibInitCount) return ErrFALSE;
  cin.fct=fct;
  cin.handle=handle;
  cin.type=type;
  cin.pars[0]=par0;
  cin.pars[1]=par1;
  cin.pars[2]=par2;
  cin.pars[3]=par3;
  if (!OsaDeviceIoControl(hDriver,CGOS_IOCTL,&cin,sizeof(cin),&cout,sizeof(cout),&cb)) return ErrFALSE;
  if (cb<sizeof(CGOSIOCTLOUT)) return ErrFALSE;
  if (cout.status) return (*pdwLastError=cout.status), FALSE;
  if (pret0) *pret0=cout.rets[0];
  if (pret1) *pret1=cout.rets[1];
  if (flags&1) return cout.rets[0];
  return TRUE;
  }

//***************************************************************************

unsigned int CallDrvStruct(unsigned int flags, unsigned int fct, unsigned int handle,
    unsigned int type, unsigned int par0, unsigned int par1,
    unsigned int par2, unsigned int par3, unsigned int *pret0, unsigned int *pret1,
    void *pin, unsigned int lenin,
    void *pout, unsigned int lenout
    )
  {
  CGOSIOCTLIN *cin;
  CGOSIOCTLOUT *cout;
  unsigned int cb,lin,lout,ret=FALSE;
  unsigned char buf[512];
  unsigned char *pbuf=buf;

  if (!dwLibInitCount) return ErrFALSE;
  if (flags&8) {
    if (pin) lenin=*(unsigned int *)pin;
    if (pout) lenout=*(unsigned int *)pout;
    }
  if (lenin && !pin) return ErrFALSE;
  if (lenout && !pout) return ErrFALSE;
  lin=sizeof(CGOSIOCTLIN)+lenin;
  lout=sizeof(CGOSIOCTLOUT)+lenout;
  if (lin+lout>sizeof(buf))
    pbuf=OsaMemAlloc(lin+lout);
  if (!pbuf) return FALSE;
  cin=(CGOSIOCTLIN *)pbuf;
  cout=(CGOSIOCTLOUT *)(pbuf+lin);
  cin->fct=fct;
  cin->handle=handle;
  cin->type=type;
  cin->pars[0]=par0;
  cin->pars[1]=par1;
  cin->pars[2]=par2;
  cin->pars[3]=par3;
  if (pin) OsaMemCpy(cin+1,pin,lenin);
  if (OsaDeviceIoControl(hDriver,CGOS_IOCTL,cin,lin,cout,lout,&cb) &&
      cb>=sizeof(CGOSIOCTLOUT) &&
      ((flags&4) || cb>=lout)) {
    if (!cout->status) {
      if (pout) OsaMemCpy(pout,cout+1,lenout); // REVIEW this zeros the remainder of strings
      if (pret0) *pret0=cout->rets[0];
      if (pret1) *pret1=cout->rets[1];
      if (flags&1) return cout->rets[0];
      else ret=TRUE;
      }
    else *pdwLastError=cout->status;
    }
  else *pdwLastError=~0;
  if (pbuf!=buf) OsaMemFree(pbuf);
  return ret;
  }

//***************************************************************************

// portable wide character support for plain ANSI characters

char *W2A(const wchar_t *s, char *d, unsigned int cnt)
  {
  char *ret=d;
  if (!s || !d || !cnt) return NULL;
  while (cnt-- && *s) *d++=(char)*s++;
  if (cnt) *d=0;
  return ret;
  }

wchar_t *A2W(const char *s, wchar_t *d, unsigned int cnt)
  {
  wchar_t *ret=d;
  if (!s || !d || !cnt) return NULL;
  while (cnt-- && *s) *d++=(wchar_t)*s++;
  if (cnt) *d=0;
  return ret;
  }

unsigned int CgosStrLen(const char *s)
  {
  const char *ss=s;
  if (!s) return 0;
  while (*s++);
  return s-ss-1;
  }

//***************************************************************************

// Library

void CloseDrv(void)
  {
  if (hDriver!=CGOS_INVALID_HANDLE)
    OsaCloseDriver(hDriver);
  hDriver=CGOS_INVALID_HANDLE;
  }

cgosret_bool CgosLibUninitialize(void)
  {
  if (!dwLibInitCount) return FALSE;
  if (!--dwLibInitCount)
    CloseDrv();
  return TRUE;
  }

cgosret_bool CgosLibInitialize(void)
  {
  if (!dwLibInitCount) {
    hDriver=OsaOpenDriver();
    if (!hDriver) hDriver=CGOS_INVALID_HANDLE; // REVIEW
    if (hDriver==CGOS_INVALID_HANDLE) return FALSE;
    dwLibInitCount++;
    dwDrvVersion=CallDrvPlain(1,xCgosDrvGetVersion,0,0,0,0,0,0,NULL,NULL);
    if ((dwDrvVersion>>24)!=CGOS_DRIVER_MAJOR) {
      CgosLibUninitialize();
//      CloseDrv();
      return FALSE;
      }
    }
  return TRUE;
  }

cgosret_ulong CgosLibGetVersion(void)
  {
  return CgosLibVersion|CGOS_BUILD_NUMBER;
  }

cgosret_ulong CgosLibGetDrvVersion(void)
  {
  return dwDrvVersion;
  }

cgosret_bool CgosLibIsAvailable(void)
  {
  return dwLibInitCount!=0;
  }

cgosret_bool CgosLibInstall(unsigned int install)
  {
  return OsaInstallDriver(install);
  }

cgosret_ulong CgosLibGetLastError(void) // 1.2
  {
  return *pdwLastError;
  }

cgosret_bool CgosLibSetLastErrorAddress(unsigned int *pErrNo) // 1.2
  {
  pdwLastError=pErrNo?pErrNo:&dwLastError;
  *pdwLastError=0;
  return TRUE;
  }

//***************************************************************************

// Generic board

cgosret_bool CgosBoardClose(HCGOS hCgos)
  { return CallDrvPlain(0,xCgosBoardClose,hCgos,0,0,0,0,0,NULL,NULL); }

cgosret_ulong CgosBoardCount(unsigned int dwClass, unsigned int dwFlags)
  { return CallDrvPlain(1,xCgosBoardCount,0,0,dwClass,dwFlags,0,0,NULL,NULL); }

cgosret_bool CgosBoardOpen(unsigned int dwClass, unsigned int dwNum, unsigned int dwFlags, HCGOS *phCgos)
  { return CallDrvPlain(0,xCgosBoardOpen,0,dwNum,dwClass,dwFlags,0,0,phCgos,NULL); }

cgosret_bool CgosBoardOpenByNameA(const char *pszName, HCGOS *phCgos)
  { return CallDrvStruct(0,xCgosBoardOpenByNameA,0,0,0,0,0,0,phCgos,NULL,(void *)pszName,CgosStrLen(pszName),NULL,0); }

cgosret_bool CgosBoardGetNameA(HCGOS hCgos, char *pszName, unsigned int dwSize)
  { return CallDrvStruct(4,xCgosBoardGetNameA,hCgos,0,0,0,0,0,NULL,NULL,NULL,0,pszName,dwSize); }

cgosret_bool CgosBoardGetInfoA(HCGOS hCgos, CGOSBOARDINFOA *pBoardInfo)
  { return CallDrvStruct(8,xCgosBoardGetInfoA,hCgos,0,0,0,0,0,NULL,NULL,NULL,0,pBoardInfo,0); }

cgosret_bool CgosBoardGetBootCounter(HCGOS hCgos, unsigned int *pdwCount)
  { return CallDrvPlain(0,xCgosBoardGetBootCounter,hCgos,0,0,0,0,0,pdwCount,NULL); }

cgosret_bool CgosBoardGetRunningTimeMeter(HCGOS hCgos, unsigned int *pdwCount)
  { return CallDrvPlain(0,xCgosBoardGetRunningTimeMeter,hCgos,0,0,0,0,0,pdwCount,NULL); }

cgosret_bool CgosBoardGetOption(HCGOS hCgos, unsigned int dwOption, unsigned int *pdwSetting)
  { return CallDrvPlain(0,xCgosBoardGetOption,hCgos,dwOption,0,0,0,0,pdwSetting,NULL); }

cgosret_bool CgosBoardSetOption(HCGOS hCgos, unsigned int dwOption, unsigned int dwSetting)
  { return CallDrvPlain(0,xCgosBoardSetOption,hCgos,dwOption,dwSetting,0,0,0,NULL,NULL); }

//***************************************************************************

// VGA (LCD)

cgosret_ulong CgosVgaCount(HCGOS hCgos)
  { return CallDrvPlain(1,xCgosVgaCount,hCgos,0,0,0,0,0,NULL,NULL); }

cgosret_bool CgosVgaGetContrast(HCGOS hCgos, unsigned int dwUnit, unsigned int *pdwSetting)
  { return CallDrvPlain(0,xCgosVgaGetContrast,hCgos,dwUnit,0,0,0,0,pdwSetting,NULL); }

cgosret_bool CgosVgaSetContrast(HCGOS hCgos, unsigned int dwUnit, unsigned int dwSetting)
  { return CallDrvPlain(0,xCgosVgaSetContrast,hCgos,dwUnit,dwSetting,0,0,0,NULL,NULL); }

cgosret_bool CgosVgaGetContrastEnable(HCGOS hCgos, unsigned int dwUnit, unsigned int *pdwSetting)
  { return CallDrvPlain(0,xCgosVgaGetContrastEnable,hCgos,dwUnit,0,0,0,0,pdwSetting,NULL); }

cgosret_bool CgosVgaSetContrastEnable(HCGOS hCgos, unsigned int dwUnit, unsigned int dwSetting)
  { return CallDrvPlain(0,xCgosVgaSetContrastEnable,hCgos,dwUnit,dwSetting,0,0,0,NULL,NULL); }

cgosret_bool CgosVgaGetBacklight(HCGOS hCgos, unsigned int dwUnit, unsigned int *pdwSetting)
  { return CallDrvPlain(0,xCgosVgaGetBacklight,hCgos,dwUnit,0,0,0,0,pdwSetting,NULL); }

cgosret_bool CgosVgaSetBacklight(HCGOS hCgos, unsigned int dwUnit, unsigned int dwSetting)
  { return CallDrvPlain(0,xCgosVgaSetBacklight,hCgos,dwUnit,dwSetting,0,0,0,NULL,NULL); }

cgosret_bool CgosVgaGetBacklightEnable(HCGOS hCgos, unsigned int dwUnit, unsigned int *pdwSetting)
  { return CallDrvPlain(0,xCgosVgaGetBacklightEnable,hCgos,dwUnit,0,0,0,0,pdwSetting,NULL); }

cgosret_bool CgosVgaSetBacklightEnable(HCGOS hCgos, unsigned int dwUnit, unsigned int dwSetting)
  { return CallDrvPlain(0,xCgosVgaSetBacklightEnable,hCgos,dwUnit,dwSetting,0,0,0,NULL,NULL); }

cgosret_bool CgosVgaEndDarkBoot(HCGOS hCgos, unsigned int dwReserved)
  { return CallDrvPlain(0,xCgosVgaEndDarkBoot,hCgos,0,dwReserved,0,0,0,NULL,NULL); }

cgosret_bool CgosVgaGetInfo(HCGOS hCgos, unsigned int dwUnit, CGOSVGAINFO *pInfo)
  { return CallDrvStruct(8,xCgosVgaGetInfo,hCgos,dwUnit,0,0,0,0,NULL,NULL,NULL,0,pInfo,0); }

//***************************************************************************

// Storage Areas

cgosret_ulong CgosStorageAreaCount(HCGOS hCgos, unsigned int dwUnit)
  { return CallDrvPlain(1,xCgosStorageAreaCount,hCgos,dwUnit,0,0,0,0,NULL,NULL); }

cgosret_ulong CgosStorageAreaType(HCGOS hCgos, unsigned int dwUnit)
  { return CallDrvPlain(1,xCgosStorageAreaType,hCgos,dwUnit,0,0,0,0,NULL,NULL); }

cgosret_ulong CgosStorageAreaSize(HCGOS hCgos, unsigned int dwUnit)
  { return CallDrvPlain(1,xCgosStorageAreaSize,hCgos,dwUnit,0,0,0,0,NULL,NULL); }

cgosret_ulong CgosStorageAreaBlockSize(HCGOS hCgos, unsigned int dwUnit)
  { return CallDrvPlain(1,xCgosStorageAreaSize,hCgos,dwUnit,0,0,0,0,NULL,NULL); }

cgosret_bool CgosStorageAreaRead(HCGOS hCgos, unsigned int dwUnit, unsigned int dwOffset, unsigned char *pBytes, unsigned int dwLen)
  { return CallDrvStruct(0,xCgosStorageAreaRead,hCgos,dwUnit,dwOffset,dwLen,0,0,NULL,NULL,NULL,0,pBytes,dwLen); }

cgosret_bool CgosStorageAreaWrite(HCGOS hCgos, unsigned int dwUnit, unsigned int dwOffset, unsigned char *pBytes, unsigned int dwLen)
  { return CallDrvStruct(0,xCgosStorageAreaWrite,hCgos,dwUnit,dwOffset,dwLen,0,0,NULL,NULL,pBytes,dwLen,NULL,0); }

cgosret_bool CgosStorageAreaErase(HCGOS hCgos, unsigned int dwUnit, unsigned int dwOffset, unsigned int dwLen)
  { return CallDrvPlain(0,xCgosStorageAreaErase,hCgos,dwUnit,dwOffset,dwLen,0,0,NULL,NULL); }

cgosret_bool CgosStorageAreaEraseStatus(HCGOS hCgos, unsigned int dwUnit, unsigned int dwOffset, unsigned int dwLen, unsigned int *lpStatus)
  { return CallDrvPlain(0,xCgosStorageAreaEraseStatus,hCgos,dwUnit,dwOffset,dwLen,0,0,lpStatus,NULL); }

cgosret_bool CgosStorageAreaLock(HCGOS hCgos, unsigned int dwUnit, unsigned int dwFlags, unsigned char *pBytes, unsigned int dwLen) // 1.2
  { return CallDrvStruct(0,xCgosStorageAreaLock,hCgos,dwUnit,dwFlags,dwLen,0,0,NULL,NULL,pBytes,dwLen,NULL,0); }

cgosret_bool CgosStorageAreaUnlock(HCGOS hCgos, unsigned int dwUnit, unsigned int dwFlags, unsigned char *pBytes, unsigned int dwLen) // 1.2
  { return CallDrvStruct(0,xCgosStorageAreaUnlock,hCgos,dwUnit,dwFlags,dwLen,0,0,NULL,NULL,pBytes,dwLen,NULL,0); }

cgosret_bool CgosStorageAreaIsLocked(HCGOS hCgos, unsigned int dwUnit, unsigned int dwFlags) // 1.2
  { return CallDrvPlain(1,xCgosStorageAreaIsLocked,hCgos,dwUnit,dwFlags,0,0,0,NULL,NULL); }

//***************************************************************************

// I2C Bus

cgosret_ulong CgosI2CCount(HCGOS hCgos)
  { return CallDrvPlain(1,xCgosI2CCount,hCgos,0,0,0,0,0,NULL,NULL); }

cgosret_bool CgosI2CIsAvailable(HCGOS hCgos, unsigned int dwUnit)
  { return CallDrvPlain(0,xCgosI2CIsAvailable,hCgos,dwUnit,0,0,0,0,NULL,NULL); }

cgosret_ulong CgosI2CType(HCGOS hCgos, unsigned int dwUnit)
  { return CallDrvPlain(1,xCgosI2CType,hCgos,dwUnit,0,0,0,0,NULL,NULL); }

cgosret_bool CgosI2CRead(HCGOS hCgos, unsigned int dwUnit, unsigned char bAddr, unsigned char *pBytes, unsigned int dwLen)
  { return CallDrvStruct(0,xCgosI2CRead,hCgos,dwUnit,bAddr,0,0,0,NULL,NULL,NULL,0,pBytes,dwLen); }

cgosret_bool CgosI2CWrite(HCGOS hCgos, unsigned int dwUnit, unsigned char bAddr, unsigned char *pBytes, unsigned int dwLen)
  { return CallDrvStruct(0,xCgosI2CWrite,hCgos,dwUnit,bAddr,0,0,0,NULL,NULL,pBytes,dwLen,NULL,0); }

cgosret_bool CgosI2CReadRegister(HCGOS hCgos, unsigned int dwUnit, unsigned char bAddr, unsigned short wReg, unsigned char *pDataByte)
  {
  unsigned int ret;
  if (!pDataByte) return FALSE;
  if (!CallDrvPlain(0,xCgosI2CReadRegister,hCgos,dwUnit,bAddr,wReg,0,0,&ret,NULL)) return FALSE;
  *pDataByte=(unsigned char)ret;
  return TRUE;
  }

cgosret_bool CgosI2CWriteRegister(HCGOS hCgos, unsigned int dwUnit, unsigned char bAddr, unsigned short wReg, unsigned char bData)
  { return CallDrvPlain(0,xCgosI2CWriteRegister,hCgos,dwUnit,bAddr,wReg,bData,0,NULL,NULL); }

cgosret_bool CgosI2CWriteReadCombined(HCGOS hCgos, unsigned int dwUnit, unsigned char bAddr, unsigned char *pBytesWrite, unsigned int dwLenWrite, unsigned char *pBytesRead, unsigned int dwLenRead)
  { return CallDrvStruct(0,xCgosI2CWriteReadCombined,hCgos,dwUnit,bAddr,0,0,0,NULL,NULL,pBytesWrite,dwLenWrite,pBytesRead,dwLenRead); }

cgosret_bool CgosI2CGetMaxFrequency(HCGOS hCgos, unsigned int dwUnit, unsigned int *pdwSetting) // 1.3
  { return CallDrvPlain(0,xCgosI2CGetMaxFrequency,hCgos,dwUnit,0,0,0,0,pdwSetting,NULL); }

cgosret_bool CgosI2CGetFrequency(HCGOS hCgos, unsigned int dwUnit, unsigned int *pdwSetting) // 1.3
  { return CallDrvPlain(0,xCgosI2CGetFrequency,hCgos,dwUnit,0,0,0,0,pdwSetting,NULL); }

cgosret_bool CgosI2CSetFrequency(HCGOS hCgos, unsigned int dwUnit, unsigned int dwSetting) // 1.3
  { return CallDrvPlain(0,xCgosI2CSetFrequency,hCgos,dwUnit,dwSetting,0,0,0,NULL,NULL); }

//***************************************************************************

// General purpose IO

cgosret_ulong CgosIOCount(HCGOS hCgos)
  { return CallDrvPlain(1,xCgosIOCount,hCgos,0,0,0,0,0,NULL,NULL); }

cgosret_bool CgosIOIsAvailable(HCGOS hCgos, unsigned int dwUnit)
  { return CallDrvPlain(0,xCgosIOIsAvailable,hCgos,dwUnit,0,0,0,0,NULL,NULL); }

cgosret_bool CgosIORead(HCGOS hCgos, unsigned int dwUnit, unsigned int *pdwData)
  { return CallDrvPlain(0,xCgosIORead,hCgos,dwUnit,0,0,0,0,pdwData,NULL); }

cgosret_bool CgosIOWrite(HCGOS hCgos, unsigned int dwUnit, unsigned int dwData)
  { return CallDrvPlain(0,xCgosIOWrite,hCgos,dwUnit,dwData,0,0,0,NULL,NULL); }

cgosret_bool CgosIOXorAndXor(HCGOS hCgos, unsigned int dwUnit, unsigned int dwXorMask1, unsigned int dwAndMask, unsigned int dwXorMask2)
  { return CallDrvPlain(0,xCgosIOXorAndXor,hCgos,dwUnit,dwXorMask1,dwAndMask,dwXorMask2,0,NULL,NULL); }

cgosret_bool CgosIOGetDirection(HCGOS hCgos, unsigned int dwUnit, unsigned int *pdwData)
  { return CallDrvPlain(0,xCgosIOGetDirection,hCgos,dwUnit,0,0,0,0,pdwData,NULL); }

cgosret_bool CgosIOSetDirection(HCGOS hCgos, unsigned int dwUnit, unsigned int dwData)
  { return CallDrvPlain(0,xCgosIOSetDirection,hCgos,dwUnit,dwData,0,0,0,NULL,NULL); }

cgosret_bool CgosIOGetDirectionCaps(HCGOS hCgos, unsigned int dwUnit, unsigned int *pdwInputs, unsigned int *pdwOutputs)
  { return CallDrvPlain(0,xCgosIOGetDirectionCaps,hCgos,dwUnit,0,0,0,0,pdwInputs,pdwOutputs); }

cgosret_bool CgosIOGetNameA(HCGOS hCgos, unsigned int dwUnit, char *pszName, unsigned int dwSize)
  { return CallDrvStruct(4,xCgosIOGetNameA,hCgos,dwUnit,0,0,0,0,NULL,NULL,NULL,0,pszName,dwSize); }

//***************************************************************************

// Watchdog

cgosret_ulong CgosWDogCount(HCGOS hCgos)
  { return CallDrvPlain(1,xCgosWDogCount,hCgos,0,0,0,0,0,NULL,NULL); }

cgosret_bool CgosWDogIsAvailable(HCGOS hCgos, unsigned int dwUnit)
  { return CallDrvPlain(0,xCgosWDogIsAvailable,hCgos,dwUnit,0,0,0,0,NULL,NULL); }

cgosret_bool CgosWDogTrigger(HCGOS hCgos, unsigned int dwUnit)
  { return CallDrvPlain(0,xCgosWDogTrigger,hCgos,dwUnit,0,0,0,0,NULL,NULL); }

cgosret_ulong CgosWDogGetTriggerCount(HCGOS hCgos, unsigned int dwUnit)
  { return CallDrvPlain(1,xCgosWDogGetTriggerCount,hCgos,dwUnit,0,0,0,0,NULL,NULL); }

cgosret_bool CgosWDogSetTriggerCount(HCGOS hCgos, unsigned int dwUnit, unsigned int cnt)
  { return CallDrvPlain(0,xCgosWDogSetTriggerCount,hCgos,dwUnit,cnt,0,0,0,NULL,NULL); }

cgosret_bool CgosWDogGetConfigStruct(HCGOS hCgos, unsigned int dwUnit, CGOSWDCONFIG *pConfig)
  { return CallDrvStruct(8,xCgosWDogGetConfigStruct,hCgos,dwUnit,0,0,0,0,NULL,NULL,NULL,0,pConfig,0); }

cgosret_bool CgosWDogSetConfigStruct(HCGOS hCgos, unsigned int dwUnit, CGOSWDCONFIG *pConfig)
  { return CallDrvStruct(8,xCgosWDogSetConfigStruct,hCgos,dwUnit,0,0,0,0,NULL,NULL,pConfig,0,NULL,0); }

cgosret_bool CgosWDogSetConfig(HCGOS hCgos, unsigned int dwUnit, unsigned int timeout, unsigned int delay, unsigned int mode)
  { return CallDrvPlain(0,xCgosWDogSetConfig,hCgos,dwUnit,timeout,delay,mode,0,NULL,NULL); }

cgosret_bool CgosWDogDisable(HCGOS hCgos, unsigned int dwUnit)
  { return CallDrvPlain(0,xCgosWDogDisable,hCgos,dwUnit,0,0,0,0,NULL,NULL); }

cgosret_bool CgosWDogGetInfo(HCGOS hCgos, unsigned int dwUnit, CGOSWDINFO *pInfo)
  { return CallDrvStruct(8,xCgosWDogGetInfo,hCgos,dwUnit,0,0,0,0,NULL,NULL,NULL,0,pInfo,0); }

//***************************************************************************

// CPU Performance

cgosret_bool CgosPerformanceGetCurrent(HCGOS hCgos, unsigned int dwUnit, unsigned int *pdwSetting)
  { return CallDrvPlain(1,xCgosPerformanceGetCurrent,hCgos,dwUnit,0,0,0,0,pdwSetting,NULL); }

cgosret_bool CgosPerformanceSetCurrent(HCGOS hCgos, unsigned int dwUnit, unsigned int dwSetting)
  { return CallDrvPlain(0,xCgosPerformanceSetCurrent,hCgos,dwUnit,dwSetting,0,0,0,NULL,NULL); }

cgosret_bool CgosPerformanceGetPolicyCaps(HCGOS hCgos, unsigned int dwUnit, unsigned int *pdwSetting)
  { return CallDrvPlain(0,xCgosPerformanceGetPolicyCaps,hCgos,dwUnit,0,0,0,0,pdwSetting,NULL); }

cgosret_bool CgosPerformanceGetPolicy(HCGOS hCgos, unsigned int dwUnit, unsigned int *pdwSetting)
  { return CallDrvPlain(0,xCgosPerformanceGetPolicy,hCgos,dwUnit,0,0,0,0,pdwSetting,NULL); }

cgosret_bool CgosPerformanceSetPolicy(HCGOS hCgos, unsigned int dwUnit, unsigned int dwSetting)
  { return CallDrvPlain(0,xCgosPerformanceSetPolicy,hCgos,dwUnit,dwSetting,0,0,0,NULL,NULL); }


//cgosret_bool CgosPerformanceGetReduced(HCGOS hCgos, unsigned int dwUnit, unsigned int *pdwSetting);
//cgosret_bool CgosPerformanceSetReduced(HCGOS hCgos, unsigned int dwUnit, unsigned int dwSetting);

//***************************************************************************

// Temperature

cgosret_ulong CgosTemperatureCount(HCGOS hCgos)
  { return CallDrvPlain(1,xCgosTemperatureCount,hCgos,0,0,0,0,0,NULL,NULL); }

cgosret_bool CgosTemperatureGetInfo(HCGOS hCgos, unsigned int dwUnit, CGOSTEMPERATUREINFO *pInfo)
  { return CallDrvStruct(8,xCgosTemperatureGetInfo,hCgos,dwUnit,0,0,0,0,NULL,NULL,NULL,0,pInfo,0); }

cgosret_bool CgosTemperatureGetCurrent(HCGOS hCgos, unsigned int dwUnit, unsigned int *pdwSetting, unsigned int *pdwStatus)
  { return CallDrvPlain(0,xCgosTemperatureGetCurrent,hCgos,dwUnit,0,0,0,0,pdwSetting,pdwStatus); }

cgosret_bool CgosTemperatureSetLimits(HCGOS hCgos, unsigned int dwUnit, CGOSTEMPERATUREINFO *pInfo)
  { return CallDrvPlain(0,xCgosTemperatureSetLimits,hCgos,dwUnit,0,0,0,0,NULL,NULL); }

//***************************************************************************

// Fan

cgosret_ulong CgosFanCount(HCGOS hCgos)
  { return CallDrvPlain(1,xCgosFanCount,hCgos,0,0,0,0,0,NULL,NULL); }

cgosret_bool CgosFanGetInfo(HCGOS hCgos, unsigned int dwUnit, CGOSFANINFO *pInfo)
  { return CallDrvStruct(8,xCgosFanGetInfo,hCgos,dwUnit,0,0,0,0,NULL,NULL,NULL,0,pInfo,0); }

cgosret_bool CgosFanGetCurrent(HCGOS hCgos, unsigned int dwUnit, unsigned int *pdwSetting, unsigned int *pdwStatus)
  { return CallDrvPlain(0,xCgosFanGetCurrent,hCgos,dwUnit,0,0,0,0,pdwSetting,pdwStatus); }

cgosret_bool CgosFanSetLimits(HCGOS hCgos, unsigned int dwUnit, CGOSFANINFO *pInfo)
  { return CallDrvStruct(8,xCgosFanSetLimits,hCgos,dwUnit,0,0,0,0,NULL,NULL,pInfo,0,NULL,0); }

//***************************************************************************

// Voltage

cgosret_ulong CgosVoltageCount(HCGOS hCgos)
  { return CallDrvPlain(1,xCgosVoltageCount,hCgos,0,0,0,0,0,NULL,NULL); }

cgosret_bool CgosVoltageGetInfo(HCGOS hCgos, unsigned int dwUnit, CGOSVOLTAGEINFO *pInfo)
  { return CallDrvStruct(8,xCgosVoltageGetInfo,hCgos,dwUnit,0,0,0,0,NULL,NULL,NULL,0,pInfo,0); }

cgosret_bool CgosVoltageGetCurrent(HCGOS hCgos, unsigned int dwUnit, unsigned int *pdwSetting, unsigned int *pdwStatus)
  { return CallDrvPlain(0,xCgosVoltageGetCurrent,hCgos,dwUnit,0,0,0,0,pdwSetting,pdwStatus); }

cgosret_bool CgosVoltageSetLimits(HCGOS hCgos, unsigned int dwUnit, CGOSVOLTAGEINFO *pInfo)
  { return CallDrvStruct(8,xCgosVoltageSetLimits,hCgos,dwUnit,0,0,0,0,NULL,NULL,pInfo,0,NULL,0); }

//***************************************************************************

cgosret_bool CgosBoardGetBootErrorLog(HCGOS hCgos, unsigned int dwUnit, unsigned int *pdwLogType, unsigned char *pBytes, unsigned int *pdwLen)
  { return CallDrvStruct(0,xCgosBoardGetBootErrorLog,hCgos,dwUnit,0,0,0,0,pdwLen,pdwLogType,NULL,0,pBytes,pdwLen?*pdwLen:0); }

//***************************************************************************

//
// Board Controller functions
//

cgosret_bool CgosCgbcGetInfo(HCGOS hCgos, unsigned int dwUnit, CGOSBCINFO *pInfo)
  { return CallDrvStruct(8,xCgosCgbcSetControl,hCgos,dwUnit,0,0,0,0,NULL,NULL,NULL,0,pInfo,0); }

cgosret_bool CgosCgbcSetControl(HCGOS hCgos, unsigned int dwLine, unsigned int dwSetting)
  { return CallDrvPlain(0,xCgosCgbcSetControl,hCgos,0,dwLine,dwSetting,0,0,NULL,NULL); }

cgosret_bool CgosCgbcReadWrite(HCGOS hCgos, unsigned char bDataByte, unsigned char *pDataByte, unsigned int dwClockDelay, unsigned int dwByteDelay)
  {
  unsigned int ret;
  if (!pDataByte) return FALSE;
  if (!CallDrvPlain(0,xCgosCgbcReadWrite,hCgos,0,bDataByte,dwClockDelay|(dwByteDelay<<8),dwByteDelay,0,&ret,NULL)) return FALSE;
  *pDataByte=(unsigned char)ret;
  return TRUE;
  }

cgosret_bool CgosCgbcHandleCommand(HCGOS hCgos, unsigned char *pBytesWrite, unsigned int dwLenWrite, unsigned char *pBytesRead, unsigned int dwLenRead, unsigned int *pdwStatus)
  { return CallDrvStruct(0,xCgosCgbcHandleCommand,hCgos,0,dwLenWrite,dwLenRead,0,0,pdwStatus,NULL,pBytesWrite,dwLenWrite,pBytesRead,dwLenRead); }

//***************************************************************************

// CGEB Backdoor

cgosret_bool CgosCgeb(HCGOS hCgos, unsigned char *pBytes, unsigned int dwLen)
  { return CallDrvStruct(4,xCgosCgeb,hCgos,0,0,0,0,0,NULL,NULL,pBytes,dwLen,pBytes,dwLen); }

CGOSDLLAPI unsigned char *CGOSAPI CgosCgebTransAddr(HCGOS hCgos, unsigned char *pBytes)
  { return (unsigned char *)CallDrvPlain(1,xCgosCgebTransAddr,hCgos,0,(unsigned int)pBytes,0,0,0,NULL,NULL); }

cgosret_bool CGOSAPI CgosCgebDbgLevel(HCGOS hCgos, unsigned int dwLevel)
  { return CallDrvPlain(1,xCgosCgebDbgLevel,hCgos,0,dwLevel,0,0,0,NULL,NULL); }

//***************************************************************************
//***************************************************************************

// Unicode support

cgosret_bool CgosBoardOpenByNameW(const wchar_t *pszName, HCGOS *phCgos)
  {
  char s[CGOS_BOARD_MAX_SIZE_ID_STRING];
  return CgosBoardOpenByNameA(W2A(pszName,s,sizeof(s)),phCgos);
  }

cgosret_bool CgosBoardGetNameW(HCGOS hCgos, wchar_t *pszName, unsigned int dwSize)
  {
  char s[CGOS_BOARD_MAX_SIZE_ID_STRING];
  if (!CgosBoardGetNameA(hCgos,s,sizeof(s)))
    return FALSE;
  A2W(s,pszName,dwSize);
  return TRUE;
  }

cgosret_bool CgosBoardGetInfoW(HCGOS hCgos, CGOSBOARDINFOW *pBoardInfo)
  {
  CGOSBOARDINFOA bi;
  if (pBoardInfo->dwSize<sizeof(CGOSBOARDINFOW)) return FALSE;
  OsaMemSet(&bi,0,sizeof(bi));
  bi.dwSize=sizeof(bi);
  if (!CgosBoardGetInfoA(hCgos,&bi))
    return FALSE;
  pBoardInfo->dwFlags=bi.dwFlags;
  A2W(bi.szBoard,pBoardInfo->szBoard,CGOS_BOARD_MAX_SIZE_ID_STRING);
  A2W(bi.szBoardSub,pBoardInfo->szBoardSub,CGOS_BOARD_MAX_SIZE_ID_STRING);
  A2W(bi.szManufacturer,pBoardInfo->szManufacturer,CGOS_BOARD_MAX_SIZE_ID_STRING);
  pBoardInfo->stManufacturingDate=bi.stManufacturingDate;
  pBoardInfo->stLastRepairDate=bi.stLastRepairDate;
  A2W(bi.szSerialNumber,pBoardInfo->szSerialNumber,CGOS_BOARD_MAX_SIZE_SERIAL_STRING);
  pBoardInfo->wProductRevision=bi.wProductRevision;
  pBoardInfo->wSystemBiosRevision=bi.wSystemBiosRevision;
  pBoardInfo->wBiosInterfaceRevision=bi.wBiosInterfaceRevision;
  pBoardInfo->wBiosInterfaceBuildRevision=bi.wBiosInterfaceBuildRevision;
  pBoardInfo->dwClasses=bi.dwClasses;
  pBoardInfo->dwPrimaryClass=bi.dwPrimaryClass;
  pBoardInfo->dwRepairCounter=bi.dwRepairCounter;
  pBoardInfo->dwManufacturer=bi.dwManufacturer;
  A2W(bi.szPartNumber,pBoardInfo->szPartNumber,CGOS_BOARD_MAX_SIZE_PART_STRING);
  A2W(bi.szEAN,pBoardInfo->szEAN,CGOS_BOARD_MAX_SIZE_EAN_STRING);
  return TRUE;
  }

cgosret_bool CgosIOGetNameW(HCGOS hCgos, unsigned int dwUnit, wchar_t *pszName, unsigned int dwSize)
  {
  char s[CGOS_BOARD_MAX_SIZE_ID_STRING];
  if (!CgosIOGetNameA(hCgos,dwUnit,s,sizeof(s)))
    return FALSE;
  A2W(s,pszName,dwSize);
  return TRUE;
  }

//***************************************************************************
