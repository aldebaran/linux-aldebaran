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

#ifndef _CGOSIOBD_H_
#define _CGOSIOBD_H_

//***************************************************************************

typedef struct {
  void *pInBuffer;
  unsigned int nInBufferSize;
  void *pOutBuffer;
  unsigned int nOutBufferSize;
  unsigned int *pBytesReturned;
  } IOCTL_BUF_DESC;

//***************************************************************************

#endif

