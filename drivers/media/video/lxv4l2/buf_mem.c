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
#define LINUX_2_6
#include <linux/autoconf.h>
#if defined(MODULE) && defined(CONFIG_MODVERSIONS)
#ifndef MODVERSIONS
#define MODVERSIONS
#endif
#ifdef LINUX_2_6
#include <config/modversions.h>
#else
#include <linux/modversions.h>
#endif
#endif

#include <linux/proc_fs.h>

#include <linux/types.h>
#include <linux/version.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/init.h>
#include <linux/poll.h>
#include <linux/pci.h>
#ifndef LINUX_2_6
#include <linux/wrapper.h>
#endif
#include <linux/mm.h>
#ifdef LINUX_2_6
#include <linux/page-flags.h>
#ifdef io_remap_page_range
#define remap_page_range io_remap_page_range
#endif
#endif
#include <linux/videodev.h>

#include "v4l.h"
#include "vid_mem.h"
#include "buf_mem.h"

unsigned long
phys_bfrsz(unsigned long size)
{
   return (size+PAGE_SIZE-1) & ~(PAGE_SIZE-1);
}

static int
get_vbuf(char *nm,int index,unsigned long size,io_buf *bp)
{
   vid_mem *vp;
   memset(bp,0,sizeof(*bp));
   bp->index = index;
   INIT_LIST_HEAD(&bp->bfrq);
   vp = vid_mem_alloc(nm, size, 0);
   if( vp == NULL ) return -ENOMEM;
   bp->vmem = vp;
   bp->phys_addr = vp->phys_addr;
   bp->offset = vp->phys_addr - vid_mem_base();
   bp->bfrp = (unsigned char *)(vid_mem_addr() + bp->offset);
   return 0;
}

static void
ret_vbuf(io_buf *bp)
{
   if( bp->vmem != NULL ) {
      vid_mem_free(bp->vmem);
      bp->vmem = NULL;
   }
   bp->bfrp = NULL;
}

int
mmap_bufs(io_queue *io,unsigned long offset,unsigned long start,
   unsigned long size, struct vm_area_struct *vma)
{
   int i, mbfrs;
   unsigned long bsz;
   io_buf *bp;
   bp = &io->bfrs[0];
   mbfrs = io->mbfrs;
   for( bsz=0,i=mbfrs; --i>=0; ++bp )
      if( bp->offset == offset ) break;
#ifdef REV2
#ifndef LINUX_2_6
   /* this bit of twisted logic is to overcome the fact that
      at 2.4.25 the offset is missing from the interface design.
      to have many programs work at all, we have to guess offset. */
   if( i < 0 || (bp->flags&V4L2_BUF_FLAG_MAPPED) != 0 ) {
      bp = &io->bfrs[0];
      for( bsz=0,i=mbfrs; --i>=0; ++bp )
         if( (bp->flags&V4L2_BUF_FLAG_MAPPED) == 0 ) break;
   }
#endif
#endif
   if( i < 0 )
      return -EINVAL;
   bsz = (i+1)*io->size;
   if( size > bsz )
      return -EINVAL;

   for( ; i>=0 && size>0; --i,++bp ) {
      bsz = size>io->size ? io->size : size;
#ifdef LINUX_2_6
      if( remap_pfn_range(vma,start,bp->phys_addr >> PAGE_SHIFT,bsz,PAGE_SHARED) != 0 )
         return -EAGAIN;
#else
      if( remap_page_range(start,bp->phys_addr,bsz,PAGE_SHARED) != 0 )
         return -EAGAIN;
#endif
      bp->start = start;
      bp->flags |= V4L2_BUF_FLAG_MAPPED;
      start += io->size;
      size -= io->size;
   }
   return 0;
}

static void
del_buffers(io_queue *io)
{
   int i, m;
   io_buf *iop;
   m = io->mbfrs;
   for( i=0; i<m; ++i ) {
      iop = &io->bfrs[i];
      ret_vbuf(iop);
   }
   io->mbfrs = 0;
}

int
new_buffer(io_queue *io,io_buf **pbp)
{
   int ret;
   char name[16];
   int idx = io->mbfrs++;
   io_buf *bp = &io->bfrs[idx];
   snprintf(&name[0],sizeof(name)-1,"bfr%02d",idx);
   ret = get_vbuf(&name[0],idx,io->size,bp);
   *pbp = ret == 0 ? bp : NULL;
   return ret;
}

void
del_io_queue(io_queue **pio)
{
   io_queue *io = *pio;
   if( io == NULL ) return;
   del_buffers(io);
   kfree(io);
   *pio = NULL;
}

int
new_vio_queue(int io_type,int nbfrs,int size,io_queue **pio)
{
   io_queue *io;
   int ret = 0;
   size_t sz = sizeof(*io)-sizeof(io->bfrs) + nbfrs*sizeof(io->bfrs[0]);
   io = (io_queue *)kmalloc(sz,GFP_KERNEL);
   if( (*pio=io) == NULL ) return -ENOMEM;
   memset(io,0,sz);
   io->io_type = io_type;
   io->nbfrs = nbfrs;
   io->size = size;
   INIT_LIST_HEAD(&io->rd_qbuf);
   INIT_LIST_HEAD(&io->rd_dqbuf);
   INIT_LIST_HEAD(&io->wr_qbuf);
   INIT_LIST_HEAD(&io->wr_dqbuf);
   spin_lock_init(&io->lock);
   return ret;
}

