/*
  unicorn-spi-flash.c - flash interface over spi driver for FPGA UNICORN

  Copyright (c) 2011 Aldebaran robotics
  Ludovic SMAL <lsmal@aldebaran-robotics.com>
  Samuel MARTIN <s.martin49@gmail.com>

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

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/firmware.h>
#include <linux/device.h>
#include <linux/delay.h>

#include "unicorn.h"
#include "unicorn-spi-flash.h"

static DEFINE_MUTEX(update_lock);

#ifdef CONFIG_AL_UNICORN_SYSFS_FLASH
static int flash_access = 0;
#endif

#define SPI_FLASH_MAX_SECTOR 2048
#define FLASH_SECTOR_SIZE 4096

#define FLASH_RETRY 5

#define ATU_min(A,B) ((A)<(B)?(A):(B))

#ifdef CONFIG_AL_UNICORN_SYSFS_FLASH
#define FPGA_START_ADDRESS    0x100000
#define FPGA_SYNCHRO_ADDRESS  0x54
#define FPGA_SYNCHRO_WORD     0x665599AA
#endif /* CONFIG_AL_UNICORN_SYSFS_FLASH */

const int FPGA1[] = {0x03B960,0xFE008d,0xD4C10C};
const int FPGA2[] = {0x42F524,0xBB42CC,0x868042};
/* Functions to handle a winbond flash w25q64BV */

enum {
   FLASH_CMD_WRITE_ENABLE           = 0x06,
   FLASH_CMD_WRITE_DISABLE          = 0x04,
   FLASH_CMD_SECTOR_ERASE           = 0x20,
   FLASH_CMD_PAGE_PROGRAM           = 0x02,
   FLASH_CMD_READ_DATA              = 0x03,
   FLASH_CMD_READ_STATUS_1          = 0x05,
   FLASH_CMD_READ_STATUS_2          = 0x35,
   FLASH_CMD_WRITE_STATUS           = 0x01,
   FLASH_CMD_BLOCK_32K_ERASE        = 0x52,
   FLASH_CMD_BLOCK_64K_ERASE        = 0xd8,
   FLASH_CMD_DEVICE_ID              = 0xab,
   FLASH_CMD_MANUFACTURER_DEVICE_ID = 0x90,
   FLASH_CMD_JEDEC_ID               = 0x9f,
   FLASH_CMD_READ_UNIQUE_ID         = 0x4b,
   FALSH_CMD_CHIP_ERASE_1           = 0xc7,
   FALSH_CMD_CHIP_ERASE_2           = 0x60,
};

static int FlashControlBuild(struct unicorn_dev *dev,int code, int sector)
{
   static const struct {
      int code;
      int addr_cmd_length;
      int data_length;
      bool isRead;
   } codes[] = {
      { FLASH_CMD_WRITE_ENABLE,           1, 0, false },
      { FLASH_CMD_WRITE_DISABLE,          1, 0, false },
      { FLASH_CMD_READ_STATUS_1,          1, 1, true },
      { FLASH_CMD_READ_STATUS_2,          1, 1, true },
      { FLASH_CMD_WRITE_STATUS,           1, 2, false },
      { FLASH_CMD_PAGE_PROGRAM,           4, 4, false },
      { FLASH_CMD_SECTOR_ERASE,           4, 0, false },
      { FLASH_CMD_BLOCK_32K_ERASE,        4, 0, false },
      { FLASH_CMD_BLOCK_64K_ERASE,        4, 0, false },
      { 0xb9,                             1, 0, false },
      { 0xff,                             1, 1, false },
      { FALSH_CMD_CHIP_ERASE_1,           1, 0, false },
      { FALSH_CMD_CHIP_ERASE_2,           1, 0, false },
      { FLASH_CMD_READ_DATA,              4, 4, true },
      { FLASH_CMD_DEVICE_ID,              4, 1, true },
      { FLASH_CMD_MANUFACTURER_DEVICE_ID, 4, 2, true },
      { FLASH_CMD_JEDEC_ID,               1, 3, true },
      { FLASH_CMD_READ_UNIQUE_ID,         5, 8, true },

      { -1, 0, 0, true}
   };
   int idx;
   for (idx = 0; codes[idx].code >= 0; idx++) {
      if (codes[idx].code == code)
         break;
   }
   if (codes[idx].code < 0)
      return -EINVAL;

   dev->spi_flash->control = 0;

   dev->spi_flash->control |= (codes[idx].code<<SPI_FLASH_CONTROL_ID_CODE_POS);
   dev->spi_flash->control |= (codes[idx].addr_cmd_length<<SPI_FLASH_CONTROL_ADDR_LENGTH_POS);
   dev->spi_flash->control |= (codes[idx].data_length<<SPI_FLASH_CONTROL_DATA_LENGTH_POS);
   dev->spi_flash->control |= ((codes[idx].isRead ? SPI_FLASH_CONTROL_RNW_CMD_READ : SPI_FLASH_CONTROL_RNW_CMD_WRITE)<<SPI_FLASH_CONTROL_RNW_CMD_POS);
   dev->spi_flash->control |= (sector << SPI_FLASH_CONTROL_SECTOR_POS);

   return 0;
}

static int FlashWaitReady(struct unicorn_dev *dev)
{
  unsigned int i;
  for (i = 0; i < 100; i++) {
      if (!dev->spi_flash->status.busy) {
         return 0;
      }
      if (i > 10)
         msleep(1);
   }
   return -1;
}

static int FlashExecuteCommand(struct unicorn_dev *dev,
                               uint32_t *rlow, uint32_t *rhigh,
                               int code, int sector, bool has_w, uint32_t w)
{

   if (FlashWaitReady(dev))
      return -1;

   if (has_w)
   {
     dev->spi_flash->control &= ~SPI_FLASH_CONTROL_RNW_CMD_MASK;
     dev->spi_flash->control |= w  << SPI_FLASH_CONTROL_RNW_CMD_POS;
   }

   FlashControlBuild(dev, code, sector);


   if (code != FLASH_CMD_READ_DATA && code != FLASH_CMD_PAGE_PROGRAM) {
     dev->spi_flash->control &= ~SPI_FLASH_CONTROL_START_CMD_MASK;
     dev->spi_flash->control |= SPI_FLASH_CONTROL_START_CMD_START << SPI_FLASH_CONTROL_START_CMD_POS;
     if (FlashWaitReady(dev))
         return -1;
   }

   if (rlow)
      *rlow = dev->spi_flash->cmd_rdata_lsb;
   if (rhigh)
      *rhigh = dev->spi_flash->cmd_rdata_msb;

   return 0;
}

static int FlashReadSector(struct unicorn_dev *dev, void *buffer, int sector)
{
  const volatile uint32_t *src;
  uint32_t                *dst;
  int i;

  if (FlashExecuteCommand(dev, NULL, NULL, FLASH_CMD_READ_DATA, sector, false, 0))
  {
    dprintk(1, "unicorn-spi-flash", "FlashReadSector(): error execute command FLASH_CMD_READ_DATA\n");
    return -1;
  }


  src = (uint32_t*)dev->spi_flash->data;
  dst = buffer;
  for (i = 0; i < FLASH_SECTOR_SIZE / 4; i++)
    *dst++ = *src++;

  if (dev->spi_flash->status.error) {
    dprintk(1, "unicorn-spi-flash", "FlashReadSector(): flash status error\n");
    return -1;
  }

  return 0;
}

static int FlashWaitWriteCompleted(struct unicorn_dev *dev)
{
   uint32_t flags;
   int i;
   for (i = 0; i < 1000; i++) {
      if (FlashExecuteCommand(dev, &flags, NULL, FLASH_CMD_READ_STATUS_1, 0, false, 0))
         return -1;
      if ((flags & 0x01) == 0) {
         return 0;
      }

      if (i > 10)
         msleep(1);
   }

   printk(KERN_ERR  "FlashWaitWriteCompleted() error: wait:%d flags:%x\n",i,flags);
   return -1;
}

static int FlashEraseGeneric(struct unicorn_dev *dev, int cmd, int sector)
{
  int verify = -1;
  int i,count,try;
  uint8_t *verifyspace = kmalloc(FLASH_SECTOR_SIZE*16,GFP_KERNEL);
  uint32_t    *src;

  for(try=0;try<FLASH_RETRY;try++)
  {
    if (FlashExecuteCommand(dev, NULL, NULL, FLASH_CMD_WRITE_ENABLE, 0, false, 0))
      goto err;
    if (FlashExecuteCommand(dev, NULL, NULL, cmd, sector, false, 0))
      goto err;
    if(FlashWaitWriteCompleted(dev))
      goto err;

    switch (cmd)
    {
      case FLASH_CMD_BLOCK_64K_ERASE:
        count = 16;
        break;
      case FLASH_CMD_BLOCK_32K_ERASE:
        count = 8;
        break;
      case FLASH_CMD_SECTOR_ERASE:
        count = 1;
        break;
      default:
        goto err;
    }

    // Verify erase
    for(i=0;i<count;i++)
    {
     if(FlashReadSector(dev, (void*)&verifyspace[i*FLASH_SECTOR_SIZE], sector+i)<0)
       goto err;
    }

    src=(void*)verifyspace;
    verify=0;
    for(i=0;i<(count*FLASH_SECTOR_SIZE)/4;i++)
    {
      if(*src++!=0xFFFFFFFF)
      {
        verify=-1;
        break;
      }
    }

    if(verify==0)
    {
      kfree(verifyspace);
      return 0;
    }

    dprintk(1, "unicorn-spi-flash", " erase verifyFlash(): try %d sector:0x%x verify:%d\n",try,sector,verify);

  }

err:
  kfree(verifyspace);

  printk(KERN_ERR "unicorn: Error during erasing flash sector:0x%x\n",sector);

  return -1;
}

static int FlashWriteSector(struct unicorn_dev *dev, const void *buffer, int sector)
{
   const uint32_t    *src;
   volatile uint32_t *dst;
   uint8_t *verifyspace = kmalloc(FLASH_SECTOR_SIZE,GFP_KERNEL);
   int verify = -1;
   int i,try=0;

   if(!verifyspace)
        return -1;

   // check if there are something to write
   src = buffer;
   verify = 0;
   for(i=0; i < FLASH_SECTOR_SIZE / 4; i++)
   {
     if(*src++!=0xFFFFFFFF)
     {
       verify=-1;
       break;
     }
   }

   if(verify==0)
   {
     kfree(verifyspace);
     return 0;
   }

   for(try=0;try<FLASH_RETRY;try++)
   {
     src = buffer;
     dst = (uint32_t*)dev->spi_flash->data;

     for (i = 0; i < FLASH_SECTOR_SIZE / 4; i++)
     {
        if (FlashExecuteCommand(dev, NULL, NULL, FLASH_CMD_WRITE_ENABLE, 0, false, 0))
          goto err;
        if (FlashExecuteCommand(dev, NULL, NULL, FLASH_CMD_PAGE_PROGRAM, sector, false, 0))
          goto err;

        *dst++ = *src++;

        if (dev->spi_flash->status.error)
          goto err;

        if (FlashWaitWriteCompleted(dev))
          goto err;
     }

     // verify write
     if(FlashReadSector(dev,verifyspace,sector)>=0)
     {

       src = buffer;
       dst = (uint32_t*)dev->spi_flash->data;
       verify = 0;
       for(i=0;i<FLASH_SECTOR_SIZE/4;i++)
       {
         if(*dst++ != *src++)
         {
           verify = -1;
           break;
         }
       }

       if(verify==0)
       {
         kfree(verifyspace);
         return 0;
       }

       dprintk(1, "unicorn-spi-flash", " write verifyFlash(): try %d sector:0x%x verify:%d\n",try,sector,verify);

     }
     else
       goto err;

   }

err:
  kfree(verifyspace);

  return -1;
}

int readFlash(struct unicorn_dev *dev, void *buffer, size_t size,
                               size_t offset)
{

  uint8_t *workspace = kmalloc(FLASH_SECTOR_SIZE,GFP_KERNEL);
  if (!workspace)
      return -1;

   while (size > 0) {
      const int sector = offset / FLASH_SECTOR_SIZE;
      const int skip   = offset % FLASH_SECTOR_SIZE;
      const int copy   = ATU_min(FLASH_SECTOR_SIZE - skip, size);

      if (FlashReadSector(dev, workspace, sector))
         break;

      memcpy(buffer, &workspace[skip], copy);

      buffer += copy;
      offset += copy;
      size   -= copy;

   }

   kfree(workspace);

   if (size > 0)
      return -1;
   return 0;
}

int writeFlash(struct unicorn_dev *dev,
                                const void *buffer, size_t size,
                                size_t offset)
{
   uint8_t *workspace = kmalloc(FLASH_SECTOR_SIZE,GFP_KERNEL);
   const int  skip      = offset % FLASH_SECTOR_SIZE;
   const int  copy      = ATU_min(FLASH_SECTOR_SIZE - skip, size);
   const bool isPartial = copy < FLASH_SECTOR_SIZE;

   if(!workspace)
      return -1;

   while (size > 0) {
      int sector = offset / FLASH_SECTOR_SIZE;

      if (size >= 8 * FLASH_SECTOR_SIZE && (offset % (8 * FLASH_SECTOR_SIZE)) == 0) {
         /* Larger erase are faster (a lot) */
         int count;
         int erase;
         if (size >= 16 * FLASH_SECTOR_SIZE && (offset % (16 * FLASH_SECTOR_SIZE)) == 0) {
            count = 16;
            erase = FLASH_CMD_BLOCK_64K_ERASE;
         } else {
            count = 8;
            erase = FLASH_CMD_BLOCK_32K_ERASE;
         }
         if (FlashEraseGeneric(dev, erase, sector))
            break;
         for (; count > 0; count--, sector++) {
            if (buffer && FlashWriteSector(dev, buffer, sector))
               break;

            if (buffer)
               buffer += FLASH_SECTOR_SIZE;
            offset += FLASH_SECTOR_SIZE;
            size   -= FLASH_SECTOR_SIZE;

         }
      } else {

         if (isPartial) {
            if (FlashReadSector(dev, workspace, sector))
               break;
            if (buffer)
               memcpy(&workspace[skip], buffer, copy);
         }
         if (FlashEraseGeneric(dev, FLASH_CMD_SECTOR_ERASE, sector))
            break;
         if ((isPartial || buffer) && FlashWriteSector(dev, isPartial ? workspace : buffer, sector))
            break;
         if (buffer)
            buffer += copy;
         offset += copy;
         size   -= copy;

      }
   }

   kfree(workspace);

   if (size > 0)
      return -1;

   return 0;
}

#ifdef CONFIG_AL_UNICORN_SYSFS_FLASH
static ssize_t flash_access_show(struct class *unicorn_class, struct class_attribute *attr, char *buf)
{

  return sprintf(buf, "%d\n",flash_access);

}

static ssize_t flash_access_store(struct class *unicorn_class,
		struct class_attribute *attr, const char *buf, size_t count)
{
  int tbl[3];
  int i=0,j=0;
  if(count==sizeof(FPGA1)-2)
  {
	  for(i=0; i<count; i+=3)
	  {
		  tbl[j]=buf[i]<<16 | buf[i+1]<<8 | buf[i+2];
		  j++;
	  }
	  for(i=0;i<3;i++)
	  {
		  if( (tbl[i]^FPGA2[i]) != FPGA1[i])
		  {
			  goto err;
		  }
	  }
  	  flash_access^=1;
	  return count;
  }

err:
  return -EPERM;
 }

static struct class_attribute class_attr_flash_access = {
        .attr = {.name = "flash_access", .mode = 0644},
        .show = flash_access_show,
        .store = flash_access_store,
};

static ssize_t flash_data_read(struct file *f, struct kobject *kobj,
		struct bin_attribute *bin_attr, char *buffer, loff_t offset,
		size_t count)
{
   size_t ret_count=0;
   struct unicorn_dev *dev = (struct unicorn_dev *)bin_attr->private;

   mutex_lock(&update_lock);

   offset += FPGA_START_ADDRESS;

   if(flash_access==0)
     ret_count = -EPERM;
   else
   {
     if(offset<(SPI_FLASH_MAX_SECTOR*FLASH_SECTOR_SIZE))
     {
       if(readFlash(dev,buffer,count,offset)>=0)
       {
         dprintk(1, "unicorn-spi-flash", "flash_data_read(): buffer:0x%p off:0x%llx size:0x%x\n",buffer,offset,count);
         ret_count = count;
       }
       else
       {
         printk(KERN_ERR "unicorn: Error during reading flash off:0x%llx size:0x%x\n",offset,count);
         ret_count = -EIO;
       }
     }
     else
     {
       printk(KERN_ERR "unicorn: Error during reading flash out of bound off:0x%llx size:0x%x\n",offset,count);
       ret_count = -EIO;
     }
   }

   mutex_unlock(&update_lock);

   return ret_count;
}

static ssize_t flash_data_write(struct file *f, struct kobject *kobj,
		struct bin_attribute *bin_attr, char *buffer, loff_t offset,
		size_t count)
{
  size_t ret_count=0;
  struct unicorn_dev *dev = (struct unicorn_dev *)bin_attr->private;

  mutex_lock(&update_lock);

  offset += FPGA_START_ADDRESS;

  if(flash_access==0)
    ret_count = -EPERM;
  else
  {
    if(offset<(SPI_FLASH_MAX_SECTOR*FLASH_SECTOR_SIZE))
    {
      if(writeFlash(dev,buffer,count,offset)>=0)
      {
        dprintk(1, "unicorn-spi-flash", "flash_data_write(): buffer:0x%p off:0x%llx count:0x%x\n",buffer,offset,count);
        ret_count = count;
      }
      else
      {
        printk(KERN_ERR "unicorn: Error during writting flash off:0x%llx size:0x%x\n",offset,count);
        ret_count = -EIO;
      }
    }
    else
    {
      printk(KERN_ERR "unicorn: Error during writting flash out of bound off:0x%llx size:0x%x\n",offset,count);
      ret_count = -EIO;
    }
  }

  mutex_unlock(&update_lock);

  return ret_count;
}

static struct bin_attribute unicorn_attr_flash_data = {
        .attr = {.name = "flash_data", .mode = 0644},
        .size = 0,
        .read = flash_data_read,
        .write = flash_data_write,
};

#ifdef CONFIG_AL_UNICORN_WRITE_BOOTLOADER
static ssize_t bootloader_data_read(struct file *f, struct kobject *kobj,
	struct bin_attribute *bin_attr, char *buffer, loff_t offset, size_t count)
{
   size_t ret_count=0;
   struct unicorn_dev *dev = (struct unicorn_dev *)bin_attr->private;

   mutex_lock(&update_lock);

   if(flash_access==0)
     ret_count = -EPERM;
   else
   {
     if(offset<FPGA_START_ADDRESS)
     {
       if(readFlash(dev,buffer,count-1,offset)>=0)
       {
         dprintk(1, "unicorn-spi-flash", "bootloader_data_read(): buffer:0x%p off:0x%llx size:0x%x\n",buffer,offset,count);
         ret_count = count;
       }
       else
       {
         printk(KERN_ERR "unicorn: Error during reading bootloader off:0x%llx size:0x%x\n",offset,count);
         ret_count = -EIO;
       }
     }
     else
     {
       printk(KERN_ERR "unicorn: Error during reading bootloader out of bound off:0x%llx size:0x%x\n",offset,count);
       ret_count = -EIO;
     }
   }

   mutex_unlock(&update_lock);

   return ret_count;
}

static ssize_t bootloader_data_write(struct file *f, struct kobject *kobj,
		struct bin_attribute *bin_attr, char *buffer, loff_t offset, size_t count)
{
  size_t ret_count=0;
  struct unicorn_dev *dev = (struct unicorn_dev *)bin_attr->private;

  mutex_lock(&update_lock);

  if(flash_access==0)
    ret_count = -EPERM;
  else
  {
    if(offset<FPGA_START_ADDRESS)
    {
      if(writeFlash(dev,buffer,count,offset)>=0)
      {
        dprintk(1, "unicorn-spi-flash", "bootloader_data_write(): buffer:0x%p off:0x%llx count:0x%x\n",buffer,offset,count);
        ret_count = count;
      }
      else
      {
        printk(KERN_ERR "unicorn: Error during writting bootloader off:0x%llx size:0x%x\n",offset,count);
        ret_count = -EIO;
      }
    }
    else
    {
      printk(KERN_ERR "unicorn: Error during writting bootloader out of bound off:0x%llx size:0x%x\n",offset,count);
      ret_count = -EIO;
    }
  }

  mutex_unlock(&update_lock);

  return ret_count;
}

static struct bin_attribute unicorn_attr_bootloader_data = {
        .attr = {.name = "bootloader_data", .mode = 0600},
        .size = 0,
        .read = bootloader_data_read,
        .write = bootloader_data_write,
};

#endif /* CONFIG_AL_UNICORN_WRITE_BOOTLOADER */

static ssize_t flash_synchro_read(struct file *f, struct kobject *kobj, struct bin_attribute *bin_attr,
    char *buffer, loff_t offset, size_t count)
{
  return -EPERM;
}

static ssize_t flash_synchro_write(struct file *f, struct kobject *kobj, struct bin_attribute *bin_attr,
    char *buffer, loff_t offset, size_t count)
{
  size_t ret_count=0;
  struct unicorn_dev *dev = (struct unicorn_dev *)bin_attr->private;
  int sector;
  volatile uint32_t *dst;

  mutex_lock(&update_lock);

  offset += FPGA_START_ADDRESS;
  if(flash_access==0)
  {
	  printk(KERN_ERR "unicorn: no write access to write fpga synchro dword\n");
      ret_count = -EPERM;
  }
  else
  {
	sector = offset / FLASH_SECTOR_SIZE;
	dst = (uint32_t*)dev->spi_flash->data + (FPGA_SYNCHRO_ADDRESS/4);

	if (FlashExecuteCommand(dev, NULL, NULL, FLASH_CMD_WRITE_ENABLE, 0, false, 0))
	{
		ret_count = -EIO;
		goto err;
	}

	if (FlashExecuteCommand(dev, NULL, NULL, FLASH_CMD_PAGE_PROGRAM, sector, false, 0))
	{
		ret_count = -EIO;
		goto err;
	}

	*dst++ = FPGA_SYNCHRO_WORD;

	if (dev->spi_flash->status.error)
	{
		ret_count = -EIO;
		goto err;
	}

	if (FlashWaitWriteCompleted(dev))
	{
		ret_count = -EIO;
		goto err;
	}

	dprintk(1, "unicorn-spi-flash", "flash_synchro_write: off:0x%llx count:%x\n",offset,count);
	ret_count = count;
  }

 err:
  mutex_unlock(&update_lock);
  return ret_count;
}

static struct bin_attribute unicorn_attr_flash_synchro = {
        .attr = {.name = "flash_synchro", .mode = 0644},
        .size = 0,
        .read = flash_synchro_read,
        .write = flash_synchro_write,
};

#endif /* CONFIG_AL_UNICORN_SYSFS_FLASH */

int unicorn_spi_flash_init(struct class *unicorn_class, struct unicorn_dev *dev)
{
  int error = 0;

#ifdef CONFIG_AL_UNICORN_SYSFS_FLASH

  struct kset *kset  = (struct kset*) unicorn_class->p;

  error = class_create_file(unicorn_class, &class_attr_flash_access);
  if (error)
  {
    printk(KERN_ERR "%s: class_create_file class_attr_flash_access failed\n",
                   __func__);
  }

  unicorn_attr_flash_data.private=dev;
  error = sysfs_create_bin_file(&kset->kobj, &unicorn_attr_flash_data);
  if (error)
  {
    printk(KERN_ERR "%s: sysfs_create_bin_file unicorn_attr_flash_data failed\n",
                   __func__);
  }

  unicorn_attr_flash_synchro.private=dev;
  error = sysfs_create_bin_file(&kset->kobj, &unicorn_attr_flash_synchro);
  if (error)
  {
    printk(KERN_ERR "%s: sysfs_create_bin_file unicorn_attr_flash_synchro failed\n",
                   __func__);
  }

#ifdef CONFIG_AL_UNICORN_WRITE_BOOTLOADER

  unicorn_attr_bootloader_data.private=dev;
  error = sysfs_create_bin_file(&kset->kobj, &unicorn_attr_bootloader_data);
  if (error)
  {
     printk(KERN_ERR "%s: sysfs_create_bin_file unicorn_attr_bootloader_data failed\n",
                    __func__);
  }
#endif /* CONFIG_AL_UNICORN_WRITE_BOOTLOADER */

#endif /* CONFIG_AL_UNICORN_SYSFS_FLASH */

  return error;
}




