/* <LIC_AMD_STD>
 * Copyright (c) 2003-2005 Advanced Micro Devices, Inc.
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
 * Header file for Linux memory management routines for Cimarron 
 * Jordan Crouse (jordan.crouse@amd.com)
 * </DOC_AMD_STD>  */

#ifndef CIMMEM_H_
#define CIMMEM_H_

#define CIM_SCRATCH_SIZE 0x100000

/* Allocation schemes */

#define CIM_ALLOC_TOP 0
#define CIM_ALLOC_BOTTOM 1

/* Block flags */
#define CIM_F_FREE    0x01  /* This block is marked as free */
#define CIM_F_CMDBUF  0x02  /* GP command buffer flag */
#define CIM_F_PRIVATE 0x04  /* This block is reserved only for its owner */
#define CIM_F_PUBLIC  0x08  /* This block can be used by the world, not just */
                            /* the owner */

#ifdef __KERNEL__
#include <linux/fs.h>

typedef struct {
  int match;
  char name[15];
  char owner[10];
  int flags;
} cim_mem_find_t;

#define CIM_MATCH_OWNER 0x1
#define CIM_MATCH_NAME  0x2
#define CIM_MATCH_FLAGS 0x4

int cim_init_memory(void);
unsigned long cim_get_memory(char *, char *, int, int);
unsigned long cim_fget_memory(char *, char *, struct file *, int, int);
unsigned long cim_find_memory(cim_mem_find_t *, int *);

void cim_free_memory(char *, unsigned long);
void cim_release_memory(struct file *);

unsigned long cim_get_gp_base(void);
unsigned long cim_get_fb_base(void);
unsigned long cim_get_fb_size(void);
unsigned long cim_get_vg_base(void);
unsigned long cim_get_vid_base(void);
unsigned long cim_get_vip_base(void);
unsigned long cim_get_cmd_size(void);
unsigned long cim_get_cmd_base(void);
unsigned long cim_get_scr_size(void);
unsigned long cim_get_fb_active(void);

unsigned char *cim_get_gp_ptr(void);
unsigned char *cim_get_fb_ptr(void);
unsigned char *cim_get_vg_ptr(void);
unsigned char *cim_get_vid_ptr(void);
unsigned char *cim_get_vip_ptr(void);
unsigned char *cim_get_cmd_base_ptr(void);
unsigned char *cim_get_cmd_ptr(void);

#endif
#endif
