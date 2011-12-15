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
 * Header file for Linux devicd driver routines for Cimarron 
 * Jordan Crouse (jordan.crouse@amd.com)
 * </DOC_AMD_STD>  */

#ifndef CIMDEV_H_
#define CIMDEV_H_

#include "cim_mem.h"

#define CIM_RESERVE_MEM 0x01
#define CIM_FREE_MEM    0x02

typedef struct {

  /* These fields get populated by the client */

  char owner[10];
  char name[15];
  int flags;
  int size;

  /* These fields are populated by the device */

  unsigned long offset;
} cim_mem_req_t;

typedef struct {
  char owner[10];
  unsigned long offset;
} cim_mem_free_t;

#endif


