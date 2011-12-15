/* <LIC_AMD_STD>
 * Copyright (c) 2005 Advanced Micro Devices, Inc.
 * 
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 * 
 * The full GNU General Public License is included in this distribution in the
 * file called COPYING
 * </LIC_AMD_STD>  */
/* <CTL_AMD_STD>
 * </CTL_AMD_STD>  */
/* <DOC_AMD_STD>
 * </DOC_AMD_STD>  */
#ifndef VIDMEM_H_
#define VIDMEM_H_

#define VID_ALLOC_TOP 1
#define VID_ALLOC_BOTTOM 0

#define VID_F_FREE 1

#include <../lib/cimarron/cim_mem.h>

typedef struct s_vid_mem vid_mem;

struct s_vid_mem {
  vid_mem *next, *prev;
  unsigned long phys_addr;
  unsigned long size;
  unsigned long flags;
  char name[15];
};

unsigned long vid_mem_base(void);
unsigned long vid_mem_ofst(void);
unsigned long vid_mem_size(void);
unsigned long vid_mem_addr(void);
unsigned long vid_mem_avail(unsigned long blksz);
unsigned long vid_mem_largest(unsigned long *base);
unsigned long vid_mem_reserve(char *name,unsigned long base);

vid_mem *vid_mem_alloc(const char *name, unsigned long size, int flags);
void vid_mem_free(vid_mem *vp);
void vid_mem_free_phys_addr(unsigned long phys_addr);
int  vid_mem_init(void);
void vid_mem_exit(void);
int is_vid_mem(unsigned long adr);

#endif
