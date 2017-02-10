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
  unsigned int size;
  unsigned int type;
  unsigned int flags;
  unsigned int flashSize;
  unsigned int eepromSize;
  unsigned int ramSize;
  unsigned int firmwareRevision;
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

cgosret_bool CgosCgbcGetInfo(HCGOS hCgos, unsigned int dwType, CGOSBCINFO *pInfo);
cgosret_bool CgosCgbcSetControl(HCGOS hCgos, unsigned int dwLine, unsigned int dwSetting);
cgosret_bool CgosCgbcReadWrite(HCGOS hCgos, unsigned char bDataByte, unsigned char *pDataByte,
  unsigned int dwClockDelay, unsigned int dwByteDelay);
cgosret_bool CgosCgbcHandleCommand(HCGOS hCgos, unsigned char *pBytesWrite, unsigned int dwLenWrite,
  unsigned char *pBytesRead, unsigned int dwLenRead, unsigned int *pdwStatus);

//***************************************************************************

//
// Backdoor functions
//

cgosret_bool CgosCgeb(HCGOS hCgos, unsigned char *pBytes, unsigned int dwLen);
CGOSDLLAPI unsigned char *CGOSAPI CgosCgebTransAddr(HCGOS hCgos, unsigned char *pBytes);
cgosret_bool CGOSAPI CgosCgebDbgLevel(HCGOS hCgos, unsigned int dwLevel);

//***************************************************************************

//
// Reserved functions
//

cgosret_bool CgosBoardGetOption(HCGOS hCgos, unsigned int dwOption, unsigned int *pdwSetting);
cgosret_bool CgosBoardSetOption(HCGOS hCgos, unsigned int dwOption, unsigned int dwSetting);
cgosret_bool CgosBoardGetBootErrorLog(HCGOS hCgos, unsigned int dwType, unsigned int *pdwLogType, unsigned char *pBytes, unsigned int *pdwLen);
cgosret_bool CgosVgaEndDarkBoot(HCGOS hCgos, unsigned int dwReserved);

//***************************************************************************

//
// Obsolete functions
//

cgosret_ulong CgosWDogGetTriggerCount(HCGOS hCgos, unsigned int dwType);
cgosret_bool CgosWDogSetTriggerCount(HCGOS hCgos, unsigned int dwType, unsigned int cnt);

//***************************************************************************

#endif // NOCGOSAPI

#ifdef __cplusplus
}
#endif

#endif // _CGOSPRIV_H_

