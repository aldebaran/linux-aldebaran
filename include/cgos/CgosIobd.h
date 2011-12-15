// CgosIobd.h
// CGOS IO Control Buffer Descriptor
// {G)U(2} 2005.06.02

//***************************************************************************

#ifndef _CGOSIOBD_H_
#define _CGOSIOBD_H_

//***************************************************************************

typedef struct {
  void *pInBuffer;
  unsigned long nInBufferSize;
  void *pOutBuffer;
  unsigned long nOutBufferSize;
  unsigned long *pBytesReturned;
  } IOCTL_BUF_DESC;

//***************************************************************************

#endif

