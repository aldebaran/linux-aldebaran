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

#ifndef _CGOSPRIV_H_
#define _CGOSPRIV_H_

#ifdef __cplusplus
extern "C" {
#endif

//***************************************************************************
//
// CgbcGetInfo
//

typedef struct {
  unsigned long size;
  unsigned long type;
  unsigned long flags;
  unsigned long flashSize;
  unsigned long eepromSize;
  unsigned long ramSize;
  unsigned long firmwareRevision;
  } CGOSBCINFO, CGEB_BC_INFO;

// BC (sub) type flags

#define CGEB_BC_TYPE_NONE            0x00000000
#define CGEB_BC_TYPE_UNKNOWN         0x00010000
#define CGEB_BC_TYPE_EMU             0x00020000
#define CGEB_BC_TYPE_ATMEL           0x00030000
#define CGEB_BC_TYPE_ATMEL_ATMEGA    0x00030001
#define CGEB_BC_TYPE_ATMEL_ATMEGA48  0x00030002
#define CGEB_BC_TYPE_ATMEL_ATMEGA88  0x00030003
#define CGEB_BC_TYPE_ATMEL_ATMEGA168 0x00030004
#define CGEB_BC_TYPE_PIC             0x00040000

// BC flags

#define CGEB_BC_FLAG_I2C   0x00000001
#define CGEB_BC_FLAG_WD    0x00000002
#define CGEB_BC_FLAG_EEP   0x00000004

// CgbcSetControl flags

#define CGEB_BC_CONTROL_SS 0
#define CGEB_BC_CONTROL_RESET 1

#ifndef NOCGOSAPI

//***************************************************************************

//
// Board Controller functions
//

cgosret_bool CgosCgbcGetInfo(HCGOS hCgos, unsigned long dwType, CGOSBCINFO *pInfo);
cgosret_bool CgosCgbcSetControl(HCGOS hCgos, unsigned long dwLine, unsigned long dwSetting);
cgosret_bool CgosCgbcReadWrite(HCGOS hCgos, unsigned char bDataByte, unsigned char *pDataByte,
  unsigned long dwClockDelay, unsigned long dwByteDelay);
cgosret_bool CgosCgbcHandleCommand(HCGOS hCgos, unsigned char *pBytesWrite, unsigned long dwLenWrite,
  unsigned char *pBytesRead, unsigned long dwLenRead, unsigned long *pdwStatus);

//***************************************************************************

//
// Backdoor functions
//

cgosret_bool CgosCgeb(HCGOS hCgos, unsigned char *pBytes, unsigned long dwLen);
CGOSDLLAPI unsigned char *CGOSAPI CgosCgebTransAddr(HCGOS hCgos, unsigned char *pBytes);
cgosret_bool CGOSAPI CgosCgebDbgLevel(HCGOS hCgos, unsigned long dwLevel);

//***************************************************************************

//
// Reserved functions
//

cgosret_bool CgosBoardGetOption(HCGOS hCgos, unsigned long dwOption, unsigned long *pdwSetting);
cgosret_bool CgosBoardSetOption(HCGOS hCgos, unsigned long dwOption, unsigned long dwSetting);
cgosret_bool CgosBoardGetBootErrorLog(HCGOS hCgos, unsigned long dwType, unsigned long *pdwLogType, unsigned char *pBytes, unsigned long *pdwLen);
cgosret_bool CgosVgaEndDarkBoot(HCGOS hCgos, unsigned long dwReserved);

//***************************************************************************

//
// Obsolete functions
//

cgosret_ulong CgosWDogGetTriggerCount(HCGOS hCgos, unsigned long dwType);
cgosret_bool CgosWDogSetTriggerCount(HCGOS hCgos, unsigned long dwType, unsigned long cnt);

//***************************************************************************

#endif // NOCGOSAPI

#ifdef __cplusplus
}
#endif

#endif // _CGOSPRIV_H_

