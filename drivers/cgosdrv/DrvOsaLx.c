// DrvOsaLx.c
// CGOS OS Abstraction Layer for Linux Kernel Drivers
// {G)U(2} 2005.06.02

//***************************************************************************

#include <linux/version.h>
#include <linux/slab.h>
#include <linux/sched.h>
//#include <linux/delay.h>
#include <asm/io.h>
#include <linux/vmalloc.h>
#include <asm/pgtable.h>

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,25)
#define cgos_ioremap ioremap
#else
#define cgos_ioremap ioremap_cache
#endif

#ifndef PAGE_KERNEL_EXEC
#define PAGE_KERNEL_EXEC PAGE_KERNEL
#endif

#define cgos_cdecl __attribute__((regparm(0)))
#include "DrvOsa.h"

//***************************************************************************

cgos_cdecl void OsaSleepms(void *ctx, unsigned long ms)
  {
  current->state=TASK_INTERRUPTIBLE;
  schedule_timeout((ms*HZ+999)/1000);
  }

//***************************************************************************

cgos_cdecl void *OsaMapAddress(unsigned long addr, unsigned long len)
  {
    return cgos_ioremap(addr,len);
  }

cgos_cdecl void OsaUnMapAddress(void *base, unsigned long len)
  {
    iounmap(base);
  }

//***************************************************************************

cgos_cdecl void *OsaMemAlloc(unsigned long len)
  {
  return __vmalloc(len, GFP_KERNEL, PAGE_KERNEL_EXEC);
  }

cgos_cdecl void OsaMemFree(void *p)
  {
  vfree(p);
  }

cgos_cdecl void OsaMemCpy(void *d, void *s, unsigned long len)
  {
  memcpy(d,s,len);
  }

cgos_cdecl void OsaMemSet(void *d, char val, unsigned long len)
  {
  memset(d,val,len);
  }

//***************************************************************************

