// DrvOsa.h
// CGOS OS Abstraction Layer
// {G)U(2} 2005.01.21

//***************************************************************************

#ifndef _DRVOSA_H_
#define _DRVOSA_H_

//***************************************************************************

#ifndef cgos_cdecl
#define cgos_cdecl
#endif

//***************************************************************************

cgos_cdecl void OsaSleepms(void *ctx, unsigned long ms);
cgos_cdecl void OsaWaitus(void *ctx, unsigned long us);

//***************************************************************************

cgos_cdecl void *OsaMapAddress(unsigned long addr, unsigned long len);
cgos_cdecl void OsaUnMapAddress(void *base, unsigned long len);

//***************************************************************************

cgos_cdecl void *OsaMemAlloc(unsigned long len);
cgos_cdecl void OsaMemFree(void *p);
cgos_cdecl void OsaMemCpy(void *d, void *s, unsigned long len);
cgos_cdecl void OsaMemSet(void *d, char val, unsigned long len);

//***************************************************************************

#endif

