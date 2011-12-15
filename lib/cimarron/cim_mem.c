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
 * Linux memory management routines for Cimarron
 * William Morrow (william.morrow@amd.com)
 * </DOC_AMD_STD>  */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <asm/io.h>

#ifdef CONFIG_PROC_FS
#include <linux/proc_fs.h>
#endif

#include "build_num.h"

#include "cim_mem.h"
#include "cim/cim_parm.h"
#include "cim/cim_rtns.h"

/* cimarron related data */

unsigned char *cim_get_gp_ptr(void)   { return cim_gp_ptr; }
unsigned char *cim_get_fb_ptr(void)   { return cim_fb_ptr; }
unsigned char *cim_get_vg_ptr(void)   { return cim_vg_ptr; }
unsigned char *cim_get_vid_ptr(void)  { return cim_vid_ptr; }
unsigned char *cim_get_vip_ptr(void)  { return cim_vip_ptr; }
unsigned char *cim_get_cmd_base_ptr(void) { return cim_cmd_base_ptr; }
unsigned char *cim_get_cmd_ptr(void)  { return cim_cmd_ptr; }

/* initalization parameters for above */

unsigned long cim_gp_base = 0;
unsigned long cim_fb_base = 0;
unsigned long cim_fb_size = 0;
unsigned long cim_vg_base = 0;
unsigned long cim_vid_base = 0;
unsigned long cim_vip_base = 0;
unsigned long cim_cmd_size = 0;
unsigned long cim_cmd_base = 0;
unsigned long cim_scr_size = 0;
unsigned long cim_fb_active = 0;

unsigned long cim_get_gp_base(void)   { return cim_gp_base; }
unsigned long cim_get_fb_base(void)   { return cim_fb_base; }
unsigned long cim_get_fb_size(void)   { return cim_fb_size; }
unsigned long cim_get_vg_base(void)   { return cim_vg_base; }
unsigned long cim_get_vid_base(void)  { return cim_vid_base; }
unsigned long cim_get_vip_base(void)  { return cim_vip_base; }
unsigned long cim_get_cmd_size(void)  { return cim_cmd_size; }
unsigned long cim_get_cmd_base(void)  { return cim_cmd_base; }
unsigned long cim_get_scr_size(void)  { return cim_scr_size; }
unsigned long cim_get_fb_active(void) { return cim_fb_active; }

/* Size of the cmd buffer size - this is settable by the module param */
static int cmdbufsize = 0x200000;

#ifdef MODULE
module_param(cmdbufsize, int, 0);
MODULE_PARM_DESC(cmdbufsize, "Size of the GP command buffer (default=2MB)");
#endif

struct s_cim_mem {
	struct s_cim_mem *next, *prev;  /* Double linked list */
	unsigned long offset;   /* Offset of the block from the FB memory base */
	unsigned long size;     /* Size of the block in bytes */
	unsigned long flags;    /* Flags indicating ownership */
	struct file *file;      /* file owning volatile resource */
	char name[15];          /* Block identifier */
	char owner[10];         /* Owner of the block */
};

static struct s_cim_mem *cim_mem_head = 0;
static struct s_cim_mem *cim_mem_tail = 0;

static int cim_mem_init = 0;          /* initialization done */

#ifdef CONFIG_PROC_FS

static int
proc_cim_mem_read(char *page, char **start, off_t off, int count,
		  int *eof, void *data)
{
	int len = 0;
	struct s_cim_mem *lp = cim_mem_head;

	while(lp) {
		len += sprintf(page + len, "%-10s %-15s ",
			       lp->flags & CIM_F_FREE ? "_free_" : lp->owner,
			       lp->flags & CIM_F_FREE ? "_free_" : lp->name);

		len += sprintf(page + len, "0x%02lx 0x%08lx 0x%08lx\n",
			       lp->flags, lp->offset, lp->size);

		lp = lp->next;
	}

	return len;
}

#endif

static struct s_cim_mem *
cim_alloc(const char *owner, const char *name, struct file *file,
	  unsigned long size, int policy, int flags) {

	struct s_cim_mem *ptr =
		(policy == CIM_ALLOC_BOTTOM) ? cim_mem_head : cim_mem_tail;

	struct s_cim_mem *ret = 0;

	/* Traverse the list looking for a free block */
	while(ptr) {

		if (!(ptr->flags & CIM_F_FREE) || size > ptr->size) {
			ptr = (policy == CIM_ALLOC_BOTTOM) ? ptr->next : ptr->prev;
			continue;
		}

		if (ptr->size == size)  {
			ret = ptr;
		}
		else {
			ret = kmalloc(sizeof(struct s_cim_mem), GFP_KERNEL);
			if (!ret) break;

			ret->size = size;

			if (policy == CIM_ALLOC_TOP) {
				ret->offset = ptr->offset + (ptr->size - size);
				ret->next = ptr->next;
				ret->prev = ptr;

				if (ptr->next)
					ptr->next->prev = ret;
				else
					cim_mem_tail = ret;

				ptr->size -= size;
				ptr->next = ret;
			}
			else {  /* CIM_ALLOC_BOTTOM */
				ret->offset = ptr->offset;
				ret->next = ptr;
				ret->prev = ptr->prev;
				if (ptr->prev)
					ptr->prev->next = ret;
				else
					cim_mem_head = ret;

				ptr->offset += size;
				ptr->size -= size;

				ptr->prev = ret;
			}
		}

		break;
	}

	if (ret) {
		ret->flags = flags;
		strncpy(ret->name, name, sizeof(ret->name) - 1);
		strncpy(ret->owner, owner, sizeof(ret->owner) - 1);

		ret->name[sizeof(ret->name) - 1] = 0;
		ret->owner[sizeof(ret->owner) - 1] = 0;
		ret->file = file;
	}

	return ret;
}

static void cim_free(struct s_cim_mem *block) {

	struct s_cim_mem *ret = block;
	struct s_cim_mem *prev, *next;

	prev = block->prev;

	/* Check to see if we can merge the previous block */

	if (prev && prev->flags & CIM_F_FREE) {
		ret = prev;
		ret->size += block->size;
		ret->next = block->next;

		if (block->next) block->next->prev = ret;
		else cim_mem_tail = ret;

		kfree(block);
	}

	next = ret->next;

	/* Merge the next block in, if we can */

	if (next && next->flags & CIM_F_FREE) {
		ret->size += next->size;
		ret->next = next->next;

		if (next->next) next->next->prev = ret;
		else cim_mem_tail = ret;

		kfree(next);
	}

	ret->flags = CIM_F_FREE;

	ret->name[0] = 0;
	ret->owner[0] = 0;
	ret->file = 0;
}

int
cim_init_memory(void)
{
	unsigned long cpu_version = 0, companion = 0;
	struct proc_dir_entry *procdir = 0;
	INIT_BASE_ADDRESSES cim_base_addr;

	if( cim_mem_init != 0 ) return 0;     /* Already set up */

	/* Allocate the inital free block */
	cim_mem_head = cim_mem_tail = kmalloc(sizeof(struct s_cim_mem), GFP_KERNEL);
	if (!cim_mem_head)
		return -ENOMEM;

	if( init_detect_cpu(&cpu_version, &companion) != CIM_STATUS_OK )
		return -ENODEV;

	init_read_base_addresses(&cim_base_addr);

	/* publicize the init phys addresses used for virtual mappings */
	cim_gp_base = cim_base_addr.gp_register_base;
	cim_fb_base = cim_base_addr.framebuffer_base;
	cim_fb_size = cim_base_addr.framebuffer_size;
	cim_vg_base = cim_base_addr.vg_register_base;
	cim_vid_base = cim_base_addr.df_register_base;
	cim_vip_base = cim_base_addr.vip_register_base;
	cim_cmd_size = cmdbufsize;
	cim_scr_size = CIM_SCRATCH_SIZE;
	cim_fb_active = cim_fb_size - cim_cmd_size - cim_scr_size;

	/* initialize cimarron */
	cim_gp_ptr = (unsigned char *)ioremap(cim_gp_base,0x4000);
	cim_fb_ptr = (unsigned char *)ioremap(cim_fb_base,cim_fb_size);
	cim_vg_ptr = (unsigned char *)ioremap(cim_vg_base,0x4000);
	cim_vid_ptr = (unsigned char *)ioremap(cim_vid_base,0x4000);
	cim_vip_ptr = (unsigned char *)ioremap(cim_vip_base,0x4000);

	if( !cim_gp_ptr || !cim_fb_ptr || !cim_vg_ptr ||
	    !cim_vid_ptr || !cim_vip_ptr ) return -ENODEV;

	gp_set_frame_buffer_base(cim_fb_base,cim_fb_size-cim_cmd_size);
	cim_cmd_base_ptr = cim_fb_ptr + cim_fb_size - cim_cmd_size;
	cim_cmd_base = cim_fb_base + cim_fb_size - cim_cmd_size;
	gp_set_command_buffer_base(cim_cmd_base,0,cim_cmd_size);

	/* Initalize the heap memory */

	cim_mem_head->next = cim_mem_head->prev = NULL;

	cim_mem_head->offset = cim_base_addr.framebuffer_base;
	cim_mem_head->size = cim_base_addr.framebuffer_size;
	cim_mem_head->name[0] = 0;
	cim_mem_head->owner[0] = 0;
	cim_mem_head->file = 0;

	cim_mem_head->flags = CIM_F_FREE;  /* Mark the whole block as free */

	/* Now, allocate the space for the GP block */
	cim_alloc("cimarron", "gp_cmd_buffer", 0, cim_cmd_size,
		  CIM_ALLOC_TOP, CIM_F_CMDBUF);
	cim_alloc("cimarron", "gp_scr_buffer", 0, cim_scr_size,
		  CIM_ALLOC_TOP, CIM_F_PRIVATE);

#ifdef CONFIG_PROC_FS
	procdir = proc_mkdir("driver/cimarron", 0);
	create_proc_read_entry ("map", 0, procdir, proc_cim_mem_read, NULL);
#endif

	cim_mem_init = 1;
	return 0;
}

unsigned long
cim_fget_memory(char *owner, char *entity, struct file *file, int size, int flags)
{
	struct s_cim_mem *lp;
	int policy;

	/* Policy is 0 if it should come from the top, or 1 if it should
	   come from the bottom of memory */

	/* Public memory space should be allocated from the bottom, anything
	   else from the top */

	policy = (flags & CIM_F_PUBLIC) ? CIM_ALLOC_BOTTOM : CIM_ALLOC_TOP;

	if (size <= 0) return 0;

	for(lp = cim_mem_head; lp; lp = lp->next) {
		if (!strcmp(lp->owner, owner) && !strcmp(lp->name, entity))
			break;
	}

	if (!lp) {
		if (!(lp = cim_alloc(owner, entity, file, size, policy, flags)))
			return 0;
	}

	return lp->offset;
}

unsigned long
cim_get_memory(char *owner, char *entity, int size, int flags)
{
	return cim_fget_memory(owner,entity,0,size,flags);
}

unsigned long
cim_find_memory(cim_mem_find_t *find, int *size) {
	struct s_cim_mem *lp = 0;

	if (!find)
		return *size=0;

	for( lp = cim_mem_head; lp; lp = lp->next) {

		if (find->match & CIM_MATCH_OWNER)
			if (!lp->owner || strcmp(lp->owner, find->owner)) continue;

		if (find->match & CIM_MATCH_NAME)
			if (!lp->name || strcmp(lp->name, find->name)) continue;

		if (find->match & CIM_MATCH_FLAGS)
			if (!(lp->flags & find->flags)) continue;

		break;
	}

	if (size)
		*size = lp ? lp->size : 0;

	return lp ? lp->offset : 0;
}

void
cim_free_memory(char *owner, unsigned long offset)
{
	struct s_cim_mem *lp;

	for(lp = cim_mem_head; lp; lp = lp->next) {
		if (lp->offset == offset && !strcmp(lp->owner, owner)) {
			cim_free(lp);
			return;
		}
	}
}

void
cim_release_memory(struct file *file)
{
	struct s_cim_mem *lp;
	for(;;) {
		for( lp=cim_mem_head; lp!=NULL && lp->file!=file; lp=lp->next );
		if( lp == NULL ) break;
		cim_free(lp);
	}
}

void
cim_exit_memory(void)
{
	struct s_cim_mem *lp = cim_mem_head, *np = 0;

#ifdef CONFIG_PROC_FS
	remove_proc_entry ("driver/cimarron", NULL);
#endif

	for( ; lp!=NULL; lp=np ) kfree((np=lp->next, lp));

	cim_mem_head = cim_mem_tail = 0;
	cim_mem_init = 0;
}

EXPORT_SYMBOL(cim_get_memory);
EXPORT_SYMBOL(cim_free_memory);
EXPORT_SYMBOL(cim_find_memory);

