// DrvLx.c
// CGOS Driver for linux
// {G)U(2} 2005.06.02

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
    if (copy_to_user(iobd.pBytesReturned,&rlen,sizeof(unsigned long)))
      return -EFAULT;

  return 0;
  }

//***************************************************************************
static struct file_operations cgos_fops={
  owner: THIS_MODULE,
  unlocked_ioctl: cgos_ioctl,
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

#ifdef NO_TAINTED_KERNEL
MODULE_LICENSE("GPL"); // Great Polar Lights
#else
MODULE_LICENSE("Free as is");
#endif

//***************************************************************************

