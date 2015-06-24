/*
   unicorn-mmap.c - V4L2 driver for unicorn

   Copyright (c) 2010 Aldebaran robotics
   Corentin Le Molgat <clemolgat@aldebaran-robotics.com>

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
   */

#include "unicorn-mmap.h"

#ifdef  CONFIG_AL_UNICORN_WIDTH_VIDEO_SUPPORT

struct videobuf_dma_contig_memory {
  u32 magic;
  void *vaddr;
  dma_addr_t dma_handle;
  unsigned long size;
  int is_userptr;
};

#define MAGIC_DC_MEM 0x0733ac61
#define MAGIC_CHECK(is, should)               \
  if (unlikely((is) != (should))) {           \
    pr_err("magic mismatch: %x expected %x\n", (is), (should)); \
    BUG();                  \
  }

static void video_vma_open(struct vm_area_struct *vma)
{
  struct videobuf_mapping *map = vma->vm_private_data;

  dprintk_video(1, "unicorn-mmap", "open mmap %p [count=%u,vma=%08lx-%08lx]\n",
      map, map->count, vma->vm_start, vma->vm_end);

  map->count++;
}

static void video_vma_close(struct vm_area_struct *vma)
{
  struct videobuf_mapping *map = vma->vm_private_data;
  struct videobuf_queue *q = map->q;
  int i;

  dprintk_video(1, "unicorn-mmap", "close mmap %p [count=%u,vma=%08lx-%08lx]\n",
      map, map->count, vma->vm_start, vma->vm_end);

  map->count--;
  if (0 == map->count) {
    struct videobuf_dma_contig_memory *mem;

    dprintk_video(1, "unicorn-mmap", "munmap %p q=%p\n", map, q);
    mutex_lock(&q->vb_lock);

    /* We need first to cancel streams, before unmapping */
    if (q->streaming)
      videobuf_queue_cancel(q);

    for (i = 0; i < VIDEO_MAX_FRAME; i++) {
      if (NULL == q->bufs[i])
        continue;
      if (q->bufs[i]->map != map)
        continue;

      mem = q->bufs[i]->priv;
      if (mem) {
        /* This callback is called only if kernel has allocated memory and this memory is mmapped.
           In this case, memory should be freed, in order to do memory unmap.*/
        MAGIC_CHECK(mem->magic, MAGIC_DC_MEM);
        mem->vaddr = NULL;
        mem->dma_handle = 0;
        mem->size = 0;
      }

      q->bufs[i]->map   = NULL;
      q->bufs[i]->baddr = 0;
    }
    kfree(map);
    mutex_unlock(&q->vb_lock);
  }
}

const struct vm_operations_struct video_vma_ops = {
  .open  = video_vma_open,
  .close = video_vma_close,
};

int video_mmap_mapper_alloc(
    struct unicorn_fh *fh,
    unsigned int index,
    struct vm_area_struct *vma)
{
  struct videobuf_mapping *map;
  struct videobuf_dma_contig_memory *mem;
  unsigned long size;
  int retval;
  struct videobuf_queue *q = &fh->vidq;
  mutex_lock(&q->vb_lock);

  /* Build map struct */
  map = kzalloc(sizeof(struct videobuf_mapping), GFP_KERNEL);
  if (!map)
    return -ENOMEM;
  map->q = q;

  /* Link map to video_queue buffer */
  q->bufs[index]->map = map;
  q->bufs[index]->baddr = vma->vm_start;

  mem = q->bufs[index]->priv;
  BUG_ON(!mem);
  MAGIC_CHECK(mem->magic, MAGIC_DC_MEM);

  mem->size = PAGE_ALIGN(q->bufs[index]->bsize);

  // Try to use previously allocated buffer.
  if (fh->dma_mem[index]) {
    dprintk_video(1, "unicorn-mmap", "use previously allocated coherent buffer %d\n", index);
    mem->vaddr = fh->dma_mem[index]->vaddr;
    mem->dma_handle = fh->dma_mem[index]->dma_handle;
  }
  else {
    dprintk_video(1, "unicorn-mmap", "allocate new coherent buffer for buffer %d ...\n", index);
    mem->vaddr = dma_alloc_coherent(q->dev, mem->size, &mem->dma_handle, GFP_KERNEL);
    if (!mem->vaddr) {
      dprintk_video(1, "unicorn-mmap", "DMA alloc coherent of size %ld failed\n",  mem->size);
      kfree(map);
      q->bufs[index] = NULL;
      mutex_unlock(&q->vb_lock);
      return -ENOMEM;
    }

    // Copy dma alloc coherent for freeing it in unicorn_video_unregister
    fh->dma_mem[index] =  kzalloc(sizeof(*mem), GFP_KERNEL);
    fh->dma_mem[index]->magic = mem->magic;
    fh->dma_mem[index]->vaddr = mem->vaddr;
    fh->dma_mem[index]->dma_handle = mem->dma_handle;
    fh->dma_mem[index]->size = mem->size;
    dprintk_video(1, "unicorn-mmap", "DMA Coherent allocation is at %p (%ld)\n", mem->vaddr, mem->size);
  }

  /* Try to remap memory */
  size = vma->vm_end - vma->vm_start;
  size = (size < mem->size) ? size : mem->size;

  vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);
  retval = remap_pfn_range(vma, vma->vm_start, mem->dma_handle >> PAGE_SHIFT, size, vma->vm_page_prot);
  if (retval) {
    dprintk_video(1, "unicorn-mmap", "remap failed with error %d. ", retval);
    kfree(fh->dma_mem[index]);
    fh->dma_mem[index] = NULL;
    dma_free_coherent(q->dev, mem->size, mem->vaddr, mem->dma_handle);
    q->bufs[index] = NULL;
    kfree(map);
    mutex_unlock(&q->vb_lock);
    return -ENOMEM;
  }

  vma->vm_ops          = &video_vma_ops;
  vma->vm_flags       |= VM_DONTEXPAND;
  vma->vm_private_data = map;

  dprintk_video(1, "unicorn-mmap", "mmap %p: q=%p %08lx-%08lx (%lx); pgoff %08lx buf %d\n",
      map, q, vma->vm_start, vma->vm_end, (long int) fh->vidq.bufs[index]->bsize,
      vma->vm_pgoff, index);

  video_vma_open(vma);
  mutex_unlock(&q->vb_lock);
  return 0;
}

int video_mmap_mapper_free(
    struct unicorn_fh *fh,
    unsigned int index)
{
  struct videobuf_dma_contig_memory *mem = fh->dma_mem[index];
  if (mem)
  {
    /* This callback is called only if kernel has allocated mmapped memory.
       In this case, memory should be freed.*/
    MAGIC_CHECK(mem->magic, MAGIC_DC_MEM);

    /* vfree is not atomic - can't be called with IRQ's disabled */
    dprintk_video(1, "unicorn-mmap", "buf[%d] freeing %p\n", index, mem->vaddr);
    dma_free_coherent(fh->vidq.dev, mem->size, mem->vaddr, mem->dma_handle);
    kfree(mem);
    fh->dma_mem[index] = NULL;
  }

  return 0;
}

#endif
