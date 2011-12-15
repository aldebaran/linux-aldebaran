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
#include <linux/module.h>

#ifdef CONFIG_PROC_FS
#include <linux/proc_fs.h>
#endif

#include "v4l.h"

#define VIDMEM_SZ 0x400000
#define _x(s) _s(s)
#define _s(s) #s

static int vidbufsize = VIDMEM_SZ;

#ifdef MODULE
module_param(vidbufsize, int, 0644);
MODULE_PARM_DESC(vidbufsize, "Size of the video buffer (default=" _x(VIDMEM_SZ) ")");
#endif

static vid_mem *vm_head = 0;
static vid_mem *vm_tail = 0;

static unsigned long vm_base = 0;
static unsigned long vm_ofst = 0;
static unsigned long vm_addr = 0;
static unsigned long vm_size = 0;

unsigned long vid_mem_base(void) { return vm_base; }
unsigned long vid_mem_ofst(void) { return vm_ofst; }
unsigned long vid_mem_addr(void) { return vm_addr; }
unsigned long vid_mem_size(void) { return vm_size; }

#ifdef CONFIG_PROC_FS

static int
proc_vm_read(char *page, char **start, off_t off, int count,
                  int *eof, void *data)
{
   int len = 0;
   vid_mem *vp = vm_head;
   while( vp != NULL ) {
      len += sprintf(page+len,"%-15s {",
         vp->flags & VID_F_FREE ? "_free_" : vp->name);
      len += sprintf(page+len,"flags 0x%02lx phys_addr 0x%08lx size 0x%08lx}\n",
         vp->flags, vp->phys_addr, vp->size);
      vp = vp->next;
   }
   return len;
}

#endif

vid_mem *
vid_mem_alloc(const char *name, unsigned long size, int flags)
{
   vid_mem *vp, *rp;
   if( (flags&VID_ALLOC_TOP) == 0 ) {
      for( rp=vm_head; rp!=NULL; rp=rp->next ) {
         if( (rp->flags&VID_F_FREE) == 0 ) continue;
         if( rp->size >= size ) break;
      }
   }
   else {
      for( rp=vm_tail; rp!=NULL; rp=rp->prev ) {
         if( (rp->flags&VID_F_FREE) == 0 ) continue;
         if( rp->size >= size ) break;
      }
   }
   if( rp == NULL ) goto xit;

   if( rp->size != size ) {
      vp = rp;
      rp = kmalloc(sizeof(*rp), GFP_KERNEL);
      if( rp == NULL ) goto xit;
      rp->flags = 0;
      rp->size = size;
      if( (flags&VID_ALLOC_TOP) != 0 ) {
         rp->phys_addr = vp->phys_addr + (vp->size-size);
         rp->next = vp->next;
         rp->prev = vp;
         *(vp->next ? &vp->next->prev : &vm_tail) = rp;
         vp->size -= size;
         vp->next = rp;
      }
      else {
         rp->phys_addr = vp->phys_addr;
         rp->next = vp;
         rp->prev = vp->prev;
         *(vp->prev ? &vp->prev->next : &vm_head) = rp;
         vp->phys_addr += size;
         vp->size -= size;
         vp->prev = rp;
      }
   }

   strncpy(&rp->name[0],&name[0],sizeof(rp->name)-1);
   rp->name[sizeof(rp->name)-1] = 0;
   rp->flags &= ~VID_F_FREE;
xit:
   return rp;
}

void
vid_mem_free(vid_mem *bp)
{
   vid_mem *vp;

   if( (vp=bp->prev) != NULL &&
       (vp->flags&VID_F_FREE) != 0 &&
       (vp->phys_addr+vp->size) == bp->phys_addr ) {
      vp->size += bp->size;
      vp->next = bp->next;
      *(bp->next ? &bp->next->prev : &vm_tail) = vp;
      kfree(bp);
      bp = vp;
   }

   if( (vp=bp->next) != NULL &&
       (vp->flags&VID_F_FREE) != 0 &&
       (bp->phys_addr+bp->size) == vp->phys_addr ) {
      bp->size += vp->size;
      bp->next = vp->next;
      *(vp->next ? &vp->next->prev : &vm_tail) = bp;
      kfree(vp);
   }

   bp->flags = VID_F_FREE;
   bp->name[0] = 0;
}

void
vid_mem_free_phys_addr(unsigned long phys_addr)
{
   vid_mem *vp;
   for( vp=vm_head; vp!=NULL && vp->phys_addr!=phys_addr; vp=vp->next );
   if( vp != NULL )
      vid_mem_free(vp);
}

int
vid_mem_init(void)
{
   unsigned long phys_addr;
   vid_mem *vp;
   if( vm_size != 0 ) return 0;
   phys_addr = cim_get_memory(v4l_name(),"video",vidbufsize,0);
   if( phys_addr == 0 ) {
     printk("cant allocate video memory\n");
     return  -ENODEV;
   }
   vp = kmalloc(sizeof(*vp), GFP_KERNEL);
   if( vp == NULL ) return -ENOMEM;
   memset(vp,0,sizeof(*vp));
   vp->phys_addr = phys_addr;
   vp->size = vidbufsize;
   vp->flags = VID_F_FREE;
   vm_head = vm_tail = vp;
#ifdef CONFIG_PROC_FS
   if( lx_dev != NULL && lx_dev->proc != NULL )
      create_proc_read_entry("map", 0, lx_dev->proc, proc_vm_read, NULL);
#endif
   vm_size = vidbufsize;
   vm_base = phys_addr;
   vm_ofst = phys_addr - cim_get_fb_base();
   vm_addr = (unsigned long)(cim_get_fb_ptr() + vm_ofst);
   return 0;
}

void
vid_mem_exit(void)
{
   vid_mem *vp, *np;
   for( vp=vm_head; vp!=NULL; vp=np )
       kfree((np=vp->next, vp));
   vm_head = vm_tail = NULL;
   cim_free_memory(v4l_name(),vm_base);
   vm_size = vm_base = vm_ofst = vm_addr = 0;
}

unsigned long
vid_mem_avail(unsigned long blksz)
{
   unsigned long blks;
   unsigned long avail = 0;
   vid_mem *vp;
   if( blksz == 0 ) return 0;
   for( vp=vm_head; vp!=NULL; vp=vp->next ) {
      if( (vp->flags&VID_F_FREE) == 0 ) continue;
      blks = vp->size / blksz;
      avail += blks;
   }
   return avail;
}

int
is_vid_addr(unsigned long adr)
{
   return adr>=vm_base && adr<(vm_base+vm_size) ? 1 : 0;
}

