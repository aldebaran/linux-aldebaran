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

#include <stdlib.h>
//#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <unistd.h>
#include "CgosIobd.h"
#include "LibOsa.h"

//***************************************************************************

unsigned int OsaDeviceIoControl(void *hDriver, unsigned int dwIoControlCode,
    void *pInBuffer, unsigned int nInBufferSize, void *pOutBuffer,
    unsigned int nOutBufferSize, unsigned int *pBytesReturned)
  {
  IOCTL_BUF_DESC iobd;
  iobd.pInBuffer=pInBuffer;
  iobd.nInBufferSize=nInBufferSize;
  iobd.pOutBuffer=pOutBuffer;
  iobd.nOutBufferSize=nOutBufferSize;
  iobd.pBytesReturned=pBytesReturned;
  return (ioctl((int)hDriver,dwIoControlCode,&iobd)==0);
  }

//***************************************************************************

void OsaCloseDriver(void *hDriver)
  {
  close((int)hDriver);
  }

//***************************************************************************

void *OsaOpenDriver(void)
  {
  return (void *)open("/dev/cgos",O_WRONLY);
  }

//***************************************************************************

unsigned int OsaInstallDriver(unsigned int install)
  {
  int status;
  if (!install) return 0;
  if (!fork())
    exit(execlp("modprobe","modprobe","cgosdrv",NULL)==-1);
  wait(&status);
//  if (!status) {
//    mknod("/dev/cgos",(00644|S_IFCHR),99<<8);
//    chmod("/dev/cgos",00666);
//    }
  return !status;
  }

//***************************************************************************
