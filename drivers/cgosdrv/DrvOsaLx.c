/*---------------------------------------------------------------------------
 *
 * Copyright (c) 2015, congatec AG. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as 
 * published by the Free Software Foundation; either version 2 of 
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, 
 * but WITHOUT ANY WARRANTY; without even the implied warranty of 
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. 
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation, 
 * Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 * The full text of the license may also be found at:        
 * http://opensource.org/licenses/GPL-2.0
 *
 *---------------------------------------------------------------------------
 */ 

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

cgos_cdecl void OsaSleepms(void *ctx, unsigned int ms)
  {
  current->state=TASK_INTERRUPTIBLE;
  schedule_timeout((ms*HZ+999)/1000);
  }

//***************************************************************************

cgos_cdecl void *OsaMapAddress(unsigned int addr, unsigned int len)
  {
    return cgos_ioremap(addr,len);
  }

cgos_cdecl void OsaUnMapAddress(void *base, unsigned int len)
  {
    iounmap(base);
  }

//***************************************************************************

cgos_cdecl void *OsaMemAlloc(unsigned int len)
  {
  return __vmalloc(len, GFP_KERNEL, PAGE_KERNEL_EXEC);
  }

cgos_cdecl void OsaMemFree(void *p)
  {
  vfree(p);
  }

cgos_cdecl void OsaMemCpy(void *d, void *s, unsigned int len)
  {
  memcpy(d,s,len);
  }

cgos_cdecl void OsaMemSet(void *d, char val, unsigned int len)
  {
  memset(d,val,len);
  }

//***************************************************************************

