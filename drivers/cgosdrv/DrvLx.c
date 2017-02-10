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
#include <linux/module.h>
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,33)
#include <linux/autoconf.h>
#else
#include <generated/autoconf.h>
#endif

#include <linux/fs.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/slab.h>

#include <asm/uaccess.h>
#include <linux/miscdevice.h>
#include "CgosIobd.h"

#define cgos_cdecl __attribute__((regparm(0)))
#include "DrvUla.h"

//***************************************************************************

#define DEVICE_NAME "cgos"

//***************************************************************************

// OS specific driver variables

typedef struct {
  void *hDriver;
  struct semaphore semIoCtl;
  void *devfs_handle;
  } OS_DRV_VARS;

OS_DRV_VARS osDrvVars;

static struct file_operations cgos_fops;
struct miscdevice cgos_device = {
  .minor = MISC_DYNAMIC_MINOR,
  .name = DEVICE_NAME,
  .fops = &cgos_fops
};

//***************************************************************************

int cgos_open(struct inode *_inode, struct file *f)
  {
//  MOD_INC_USE_COUNT;
  return 0;
  }

int cgos_release(struct inode *_inode, struct file *f)
  {
//  MOD_DEC_USE_COUNT;
  return 0;
  }

//***************************************************************************

#define return_ioctl(ret) { if (pbuf!=buf) kfree(pbuf); return ret; }

#if LINUX_VERSION_CODE > KERNEL_VERSION(2,6,35)
#define ioctl unlocked_ioctl
long cgos_ioctl(struct file *f, unsigned int command, unsigned long arg)
#else
int cgos_ioctl(struct inode *_inode, struct file *f, unsigned int command, unsigned long arg)
#endif
  {
  IOCTL_BUF_DESC iobd;
  unsigned char buf[512];
  unsigned char *pbuf=buf;
  unsigned int ret, rlen=0, maxlen;

  if (copy_from_user(&iobd,(IOCTL_BUF_DESC *)arg,sizeof(iobd)))
    return -EFAULT;

  maxlen=iobd.nInBufferSize>iobd.nOutBufferSize?iobd.nInBufferSize:iobd.nOutBufferSize;
  if (maxlen>sizeof(buf))
    pbuf=kmalloc(maxlen,GFP_KERNEL);
  if (!pbuf) return -ENOMEM;

  if (iobd.nInBufferSize) {
    if (!iobd.pInBuffer || copy_from_user(pbuf,iobd.pInBuffer,iobd.nInBufferSize))
      return_ioctl(-EFAULT);
    }

  down(&osDrvVars.semIoCtl);
  ret=UlaDeviceIoControl(osDrvVars.hDriver,command,pbuf,iobd.nInBufferSize,
     pbuf,iobd.nOutBufferSize,&rlen);
  up(&osDrvVars.semIoCtl);
  if (ret) return_ioctl(-EFAULT);

  if (rlen) {
    if (!iobd.pOutBuffer || copy_to_user(iobd.pOutBuffer,pbuf,rlen))
      return_ioctl(-EFAULT);
    }
  if (pbuf!=buf) kfree(pbuf);
  if (iobd.pBytesReturned)
    if (copy_to_user(iobd.pBytesReturned,&rlen,sizeof(unsigned int)))
      return -EFAULT;

  return 0;
  }

//***************************************************************************
static struct file_operations cgos_fops={
  owner: THIS_MODULE,
  ioctl: cgos_ioctl,
  open: cgos_open,
  release: cgos_release,
  };
//***************************************************************************

static int __init cgos_init(void)
  {

  int error=0;

  memset(&osDrvVars,0,sizeof(osDrvVars));
  sema_init(&osDrvVars.semIoCtl,1);
  error = misc_register(&cgos_device);

  if (!error)
    osDrvVars.hDriver=UlaOpenDriver(0);

  return error;
  }

//***************************************************************************

void cgos_exit(void)
  {
  UlaCloseDriver(osDrvVars.hDriver);
  misc_deregister(&cgos_device);
  }

//***************************************************************************

module_init(cgos_init);
module_exit(cgos_exit);

//***************************************************************************

MODULE_AUTHOR("congatec AG");
MODULE_DESCRIPTION("CGOS driver");
MODULE_LICENSE("GPL");

//***************************************************************************

