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
 * Cimarron device driver code
 * Jordan Crouse (jordan.crouse@amd.com)
 * </DOC_AMD_STD>  */

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <asm/uaccess.h>

#include "cim_dev.h"

#include "cim/cim_parm.h"
#include "cim/cim_rtns.h"

static int
cimdev_ioctl(struct inode *inode, struct file *filp, unsigned int cmd,
	     unsigned long arg) {
  
  switch(cmd) {
    
  case CIM_RESERVE_MEM: {
    cim_mem_req_t res;
    int err;

    if ((err = copy_from_user(&res, (void *) arg, sizeof(res))))
      return err;
    
    res.offset = cim_fget_memory(res.owner, res.name, filp,
				 res.size, res.flags);
    
    return copy_to_user((void *) arg, &res, sizeof(res));
  }
  
  case CIM_FREE_MEM: {
    cim_mem_free_t res;
    int err;
    
    if ((err = copy_from_user(&res, (void *) arg, sizeof(res))))
      return err;

    cim_free_memory(res.owner, res.offset);
    return 0;
  }
  }

  return -EINVAL;
}

static int
cimdev_release(struct inode *inode, struct file *filp) {
  cim_release_memory(filp);
  return 0;
}

static struct file_operations cimdev_fops = {
        owner:          THIS_MODULE,
	release:	cimdev_release,
        ioctl:          cimdev_ioctl,
};

#define CIMDEV_MINOR_DEV 156

static struct miscdevice cimdev_device = {
        CIMDEV_MINOR_DEV,
        "cimarron",
        &cimdev_fops
};

int cim_init_dev(void) {
  return misc_register(&cimdev_device);
}

void cim_exit_dev(void) {
  misc_deregister(&cimdev_device);
}

