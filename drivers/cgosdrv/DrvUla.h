// DrvUla.h
// Driver upper layer function declarations
// {G)U(2} 2005.01.19

//***************************************************************************

#ifndef _DRVULA_H_
#define _DRVULA_H_

//***************************************************************************

#ifndef cgos_cdecl
#define cgos_cdecl
#endif

//***************************************************************************

cgos_cdecl void *UlaOpenDriver(unsigned long reserved);
cgos_cdecl void UlaCloseDriver(void *hDriver);
cgos_cdecl unsigned long UlaGetBoardCount(void *hDriver);
cgos_cdecl unsigned char *UlaGetBoardName(void *hDriver, unsigned long Index);
cgos_cdecl unsigned int UlaDeviceIoControl(void *hDriver, unsigned long dwIoControlCode,
    void *pInBuffer, unsigned long nInBufferSize,
    void *pOutBuffer, unsigned long nOutBufferSize,
    unsigned long *pBytesReturned);

//***************************************************************************

#endif
