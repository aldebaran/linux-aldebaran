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
#include <linux/export.h>
#include <linux/kernel.h>
#include <linux/slab.h>

#include <asm/uaccess.h>
#include <linux/miscdevice.h>
#include "CgosIobd.h"

//MODGWE #define cgos_cdecl __attribute__((regparm(0)))
#define cgos_cdecl 	//MODGWE

#include "DrvUla.h"
#include "DrvOsHdr.h"

//***************************************************************************

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,18)
#include <linux/devfs_fs_kernel.h>
#else
static inline int devfs_mk_cdev(dev_t dev, umode_t mode, const char *fmt, ...)
  {
  return 0;
  }
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,5,0)
#define MAJOR_DEV_NO 99
static int cgos_major=MAJOR_DEV_NO;
#else
typedef void *devfs_handle_t;
#endif

//***************************************************************************

#define DEVICE_NAME "cgos"

//***************************************************************************

// OS specific driver variables

typedef struct {
  void *hDriver;
  struct semaphore semIoCtl;
  devfs_handle_t devfs_handle;
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
  unsigned long ret, rlen=0, maxlen;

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

  ret = cgos_issue_request(command & ~0UL,
      (unsigned long*)pbuf, iobd.nInBufferSize,
      (unsigned long*)pbuf, iobd.nOutBufferSize, &rlen);

  if (ret) return_ioctl(-EFAULT);

  if (rlen) {
    if (!iobd.pOutBuffer || copy_to_user(iobd.pOutBuffer,pbuf,rlen))
      return_ioctl(-EFAULT);
    }
  if (pbuf!=buf) kfree(pbuf);
  if (iobd.pBytesReturned)
    if (copy_to_user(iobd.pBytesReturned,&rlen,sizeof(unsigned long)))
      return -EFAULT;

  return 0;
  }

unsigned long cgos_issue_request(unsigned long command,
    unsigned long *ibuf, unsigned long isize,
    unsigned long *obuf, unsigned long osize, unsigned long *olen)
{
  unsigned long ret;
  down(&osDrvVars.semIoCtl);
  ret=UlaDeviceIoControl(osDrvVars.hDriver, command,
      ibuf, isize, obuf, osize, olen);
  up(&osDrvVars.semIoCtl);
  return ret;
}
EXPORT_SYMBOL(cgos_issue_request);

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

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,5,0)

  // Kernel Version 2.4
#if defined(CONFIG_DEVFS_FS)

  error=devfs_register_chrdev(cgos_major,DEVICE_NAME,&cgos_fops);
  if (error<0) return error;

  if (error>0) {
    // we got a dynamic major number
    cgos_major=error;
    error=0;
    }

  osDrvVars.devfs_handle=devfs_register(NULL,DEVICE_NAME,
        DEVFS_FL_NONE,cgos_major,0,S_IFCHR|S_IXUGO,&cgos_fops,NULL);

  if (osDrvVars.devfs_handle<0) return -EACCES;

#else
  if (register_chrdev(cgos_major,DEVICE_NAME,&cgos_fops)<0) return -EACCES;
#endif

#else

  // Kernel Version 2.6
  error = misc_register(&cgos_device);

#endif

  if (!error)
    osDrvVars.hDriver=UlaOpenDriver(0);

  return error;
  }

//***************************************************************************

void cgos_exit(void)
  {
  UlaCloseDriver(osDrvVars.hDriver);

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,5,0)
#if defined(CONFIG_DEVFS_FS)
  // Kernel Version 2.4 and devfs
  devfs_unregister(osDrvVars.devfs_handle);
  devfs_unregister_chrdev(cgos_major,DEVICE_NAME);
#else
  devfs_remove(DEVICE_NAME);
  unregister_chrdev(cgos_major,DEVICE_NAME);
#endif
#else

  misc_deregister(&cgos_device);

#endif
  }

//***************************************************************************

module_init(cgos_init);
module_exit(cgos_exit);

//***************************************************************************

MODULE_AUTHOR("congatec AG");
MODULE_DESCRIPTION("CGOS driver");

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,5,0)
MODULE_PARM(cgos_major,"i");
#endif

// These lines are just present to allow you to suppress the tainted kernel
// messages when the module is loaded and they imply absolutely nothing else.

MODULE_LICENSE("GPL"); // Great Polar Lights

//***************************************************************************

