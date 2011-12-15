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
#ifndef _BUFMEM_H_
#define _BUFMEM_H_

extern unsigned long cim_fb_base;
extern unsigned char *cim_fb_ptr;

typedef struct s_io_buf io_buf;

enum {
   io_none,
   io_queued,
   io_flipped,
};

struct s_io_buf {
   struct list_head bfrq;
   int type, index, memory, flags;
   unsigned long sequence;
   jiffiez_t jiffies;
   unsigned long start;
   vid_mem *vmem;
   unsigned long phys_addr;
   unsigned long offset;
   unsigned char *bfrp;
};

typedef struct s_io_queue io_queue;

struct s_io_queue {
   int io_type;
   spinlock_t lock;
   struct list_head rd_qbuf, rd_dqbuf;
   struct list_head wr_qbuf, wr_dqbuf;
   unsigned long sequence;
   unsigned long size;
   io_buf *stream_bfr;
   int count, offset;
   int nbfrs, mbfrs;
   io_buf bfrs[1];
};

unsigned long phys_bfrsz(unsigned long size);
int mmap_bufs(io_queue *qp,unsigned long offset,unsigned long start,
   unsigned long size,struct vm_area_struct *vma);
void del_io_queue(io_queue **pqp);
int new_buffer(io_queue *io,io_buf **pbp);
int new_vio_queue(int io_type,int nbfrs,int size,io_queue **pio);

#endif
