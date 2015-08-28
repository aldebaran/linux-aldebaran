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

#ifndef _LIBOSA_H_
#define _LIBOSA_H_

//***************************************************************************

#define CGOS_INVALID_HANDLE ((void *)-1)

//***************************************************************************

unsigned int OsaInstallDriver(unsigned int install);
void *OsaOpenDriver(void);
void OsaCloseDriver(void *hDriver);
unsigned int OsaDeviceIoControl(void *hDriver, unsigned long dwIoControlCode,
    void *pInBuffer, unsigned long nInBufferSize,
    void *pOutBuffer, unsigned long nOutBufferSize,
    unsigned long *pBytesReturned);

//***************************************************************************

void *OsaMemAlloc(unsigned long len);
void OsaMemFree(void *p);
void OsaMemCpy(void *d, void *s, unsigned long len);
void OsaMemSet(void *d, char val, unsigned long len);

//***************************************************************************

#endif

