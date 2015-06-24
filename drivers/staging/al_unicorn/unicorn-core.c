/*
  unicorn.c - V4L2 driver for FPGA UNICORN

  Copyright (c) 2010 - 2011 Aldebaran robotics
  joseph pinkasfeld joseph.pinkasfeld@gmail.com
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

#include <linux/module.h>							/* Needed by all modules */
#include <linux/kernel.h>							/* Needed for KERN_INFO */
#include <linux/init.h>								/* Needed for the macros */

#define __UNICORN_CORE_C

#include "unicorn.h"
#include "unicorn-i2c.h"

#ifdef  CONFIG_AL_UNICORN_WIDTH_VIDEO_SUPPORT
#include "unicorn-fops.h"
#include "unicorn-ioctlops.h"
#include "unicorn-video.h"
#endif

#define DRIVER_AUTHOR 					"Joseph Pinkasfeld <joseph.pinkasfeld@gmail.com>;Ludovic SMAL <lsmal@aldebaran-robotics.com>"
#define DRIVER_DESC   					"Aldebaran Robotics Unicorn dual channel video acquisition"

static unsigned int unicorn_devcount = 0;
static struct unicorn_dev unicorn;

module_param(unicorn_debug, int, 0644);
MODULE_PARM_DESC(unicorn_debug, "enable debug messages [Unicorn core]");

module_param(unicorn_version, int, 0444);
MODULE_PARM_DESC(unicorn_version, "show fpga version major|minor|revision|bugfix");

#ifdef  CONFIG_AL_UNICORN_WIDTH_VIDEO_SUPPORT
void unicorn_video_wakeup(struct unicorn_dev *dev, struct unicorn_dmaqueue *q, int channel, int buff_index)
{
  struct unicorn_buffer *buf;
  dprintk(1, dev->name, "%s() list_empty(&q->queued)=%d list_empty(&q->active)=%d\n", __func__,
		  list_empty(&q->queued),
		  list_empty(&q->active));

  spin_lock(&dev->slock);

  // reset fifo_full_error
  dev->fifo_full_error &= ~(1<<channel);
  // reset fifo_full first trial
  dev->nb_timeout_fifo_full_error = RESET_TIMEOUT(dev->nb_timeout_fifo_full_error,q->fh->channel);

  if(!list_empty(&q->active))
  {
    buf = list_entry(q->active.next, struct unicorn_buffer, vb.queue);
    do_gettimeofday(&buf->vb.ts);
    buf->vb.state = VIDEOBUF_DONE;
    list_del(&buf->vb.queue);
    wake_up(&buf->vb.done);

    /* reset dma size to mark it free for flip_flop function */
    dev->pcie_dma->dma[channel].buff[buff_index].size=0;

  }

  if (!list_empty(&q->queued))
  {
	  buf = list_entry(q->queued.next , struct unicorn_buffer, vb.queue);
	  list_move_tail(&buf->vb.queue, &q->active);
	  buf->vb.state = VIDEOBUF_ACTIVE;
	  buf->count = q->count++;
	  unicorn_continue_video_dma(dev, buf, q->fh, buff_index);
	  mod_timer(&q->timeout, jiffies + BUFFER_TIMEOUT);
  }
  else
  {
	  if(list_empty(&q->active))
	  {
		dev->global_register->video[channel].ctrl &= ~VIDEO_CONTROL_ENABLE;
		dev->pcie_dma->dma[channel].ctrl |= DMA_CONTROL_RESET;
		del_timer(&q->timeout);
	  }
  }
  spin_unlock(&dev->slock);
}
#endif

/* IRQ handler */
static irqreturn_t unicorn_irq_handler(int irq, void *dev_id)
{
  struct unicorn_dev *dev;

#ifdef  CONFIG_AL_UNICORN_WIDTH_VIDEO_SUPPORT
  int i=0;
#endif

  dev = (struct unicorn_dev *)dev_id;
  dev->pending = dev->interrupts_controller->irq.pending;
  dprintk(1, dev->name, "%s() 0x%X\n", __func__,dev->pending);


  if (dev->pending)
  {

#ifdef  CONFIG_AL_UNICORN_WIDTH_VIDEO_SUPPORT
  for(i=0;i<MAX_VID_CHANNEL_NUM;i++)
  {
    if ((dev->pending>>(i*4)) & IT_DMA_CHAN_0_TX_BUFF_0_END)
    {
      dprintk(1, dev->name, "%s() IT_DMA_CHAN_%d_TX_BUFF_0_END\n", __func__,i);
      unicorn_video_wakeup(dev,  &dev->vidq[i],i,0);
    }
    if ((dev->pending>>(i*4)) & IT_DMA_CHAN_0_TX_BUFF_1_END)
    {
      dprintk(1, dev->name, "%s() IT_DMA_CHAN_%d_TX_BUFF_1_END\n", __func__,i);
      unicorn_video_wakeup(dev,  &dev->vidq[i],i,1);
    }
    if ((dev->pending>>(i*4)) & IT_DMA_CHAN_0_FIFO_FULL_ERROR)
    {
      printk(KERN_ERR "%s() IT_DMA_CHAN_%d_FIFO_FULL_ERROR\n", __func__,i);
      unicorn_recover_fifo_full_error(dev,i);
    }
    if ((dev->pending>>(i*4)) & IT_DMA_CHAN_0_ERROR)
    {
      printk(KERN_ERR "%s() IT_DMA_CHAN_%d_ERROR\n", __func__,i);
      dev->global_register->video[i].ctrl &= ~VIDEO_CONTROL_ENABLE;
      dev->pcie_dma->dma[i].ctrl |= DMA_CONTROL_RESET;
    }
  }
  if(dev->pending & IT_VIDEO_CHANNEL_0_OF_TRAME)
  {
    dprintk(1, dev->name, "%s() IT_VIDEO_CHANNEL_0_OF_TRAME\n", __func__);
  }
  if(dev->pending & IT_VIDEO_CHANNEL_1_OF_TRAME)
  {
    dprintk(1, dev->name, "%s() IT_VIDEO_CHANNEL_1_OF_TRAME\n", __func__);
  }
#endif

  if ((dev->pending & IT_ABH32_ERROR)||
    (dev->pending & IT_ABH32_FIFO_RX_ERROR)||
    (dev->pending & IT_ABH32_FIFO_TX_ERROR))
  {
    printk(KERN_ERR "UNICORN %s() error IRQ \n", __func__);
  }
  if (dev->pending & IT_I2C_WRITE_FIFO_ERROR)
  {
    printk(KERN_ERR "UNICORN %s() IT_I2C_WRITE_FIFO_ERROR \n", __func__);
  }
  if (dev->pending & IT_I2C_READ_FIFO_ERROR)
  {
    printk(KERN_ERR "UNICORN %s() IT_I2C_READ_FIFO_ERROR \n", __func__);
  }
  if (dev->pending & IT_I2C_TRANSFER_END)
  {
    dev->i2c_eof = 1;
     //dprintk(1, "UNICORN %s() IT_I2C_TRANSFER_END \n", __func__);
  }
  if (dev->pending & IT_I2C_ACCES_ERROR)
  {
      dev->i2c_error = 1;
      dprintk(1, dev->name, "UNICORN %s() IT_I2C_ACCES_ERROR \n", __func__);
  }
 /* if (dev->interrupts_controller->irq.pending)
  {
    printk(KERN_ERR "%s() irq still pending 0x%X unknown state \n", __func__,dev->pending);
  }*/

  dev->interrupt_queue_flag = 1;
  return IRQ_HANDLED;
  }
  else
  {
  return IRQ_NONE;
  }
}

#ifdef  CONFIG_AL_UNICORN_WIDTH_VIDEO_SUPPORT
struct video_device unicorn_video_template0 = {
  .name = "unicorn-video0",
  .fops = &video_fops,
  .ioctl_ops = &video_ioctl_ops,
  .index = 0,
};

struct video_device unicorn_video_template1 = {
  .name = "unicorn-video1",
  .fops = &video_fops,
  .ioctl_ops = &video_ioctl_ops,
  .index = 1,
};
#endif

/**
 * Unmap the BAR regions that had been mapped earlier using map_bars()
 */
static void unmap_bars(struct unicorn_dev *dev)
{
  if (dev->fpga_regs) {
    /* unmap BAR */
    pci_iounmap(dev->fpga_regs, dev->pci);
    dev->fpga_regs = NULL;
  }
}

/**
 * Map the device memory regions into kernel virtual address space after
 * verifying their sizes respect the minimum sizes needed, given by the
 * bar_min_len[] array.
 */
static int map_bars(struct unicorn_dev *dev)
{
  int ret;
  unsigned long bar_start = pci_resource_start(dev->pci, 0);
  unsigned long bar_end = pci_resource_end(dev->pci, 0);
  unsigned long bar_length = bar_end - bar_start + 1;
  dev->fpga_regs = NULL;

  /* map the device memory or IO region into kernel virtual
         * address space */
  dev->fpga_regs = pci_iomap(dev->pci, 0, bar_length);
  if (!dev->fpga_regs) {
    printk(KERN_DEBUG "Could not map BAR #%d.\n", 0);
    ret = -1;
    goto fail;
  }

  ret = 0;
  goto success;
  fail:
    /* unmap any BARs that we did map */
    unmap_bars(dev);
  success:
    return ret;
}

static int unicorn_dev_setup(void)
{
  int ret = 0;
  mutex_init(&unicorn.mutex);
  mutex_init(&unicorn.i2c_mutex);
  sprintf(unicorn.name, DRV_NAME);

  unicorn.nr = ++unicorn_devcount;

  ret = pci_enable_device(unicorn.pci);
  if (ret)
  {
    printk(KERN_INFO "Cannot enable PCI device\n");
    return -1;
  }
  pci_set_master(unicorn.pci);

  ret = pci_request_regions(unicorn.pci, DRV_NAME);
  if(ret)
  {
    printk(KERN_ERR "Cannot request pci region\n");
    goto fail_disable;
  }
  ret = map_bars(&unicorn);

  unicorn.pcie_dma = (struct abh32_pcie_dma *)(unicorn.fpga_regs + AHB32_PCIE_DMA);
  unicorn.global_register = (struct abh32_global_registers *)(unicorn.fpga_regs + AHB32_GLOBAL_REGISTERS);
  unicorn.interrupts_controller = (struct abh32_interrupts_controller *)(unicorn.fpga_regs + AHB32_INTERRUPTS_CTRL);
  unicorn.i2c_master = (struct abh32_i2c_master *)(unicorn.fpga_regs + AHB32_I2C_MASTER);
  unicorn.spi_flash = (struct abh32_spi_flash *)(unicorn.fpga_regs + AHB32_SPI_FLASH);

  printk(KERN_INFO "unicorn : bugfix:%d revision:%d major:%d minor:%d\n"
         ,unicorn.global_register->general.version.bugfix
         ,unicorn.global_register->general.version.revision
         ,unicorn.global_register->general.version.major
         ,unicorn.global_register->general.version.minor);

  /*
  if(unicorn.global_register->general.version.major < VERSION_MAJOR_MIN )
  {
    printk(KERN_ERR "unicorn : wrong major version (min=%d) \n",VERSION_MAJOR_MIN);
    goto fail_unmap;
  }
  else if(unicorn.global_register->general.version.major == VERSION_MAJOR_NO_VERSION )
  {
    printk(KERN_ERR "unicorn : wrong major version (min=%d) \n",VERSION_MAJOR_MIN);
    goto fail_unmap;

  }
  else if(unicorn.global_register->general.version.major == VERSION_MAJOR_MIN )
  {
    if(unicorn.global_register->general.version.minor < VERSION_MINOR_MIN )
    {
      printk(KERN_ERR "unicorn : wrong minor version (min=%d)\n",VERSION_MINOR_MIN);
      goto fail_unmap;
    }
  }
*/

  unicorn_version = ((unicorn.global_register->general.version.major & 0xFF) << 24)   |
                    ((unicorn.global_register->general.version.minor & 0xFF) << 16)   |
                    ((unicorn.global_register->general.version.revision & 0xFF) << 8) |
                    (unicorn.global_register->general.version.bugfix & 0xFF);

  return 0;
 /* fail_unmap :
    unmap_bars(&unicorn);
    pci_release_regions(unicorn.pci);*/
  fail_disable :
    pci_disable_device(unicorn.pci);
  return -1;
}

static int unicorn_interrupt_init(void)
{
  int ret = 0;

  unicorn.interrupts_controller->irq.ctrl = 0;

  /* Interrupts : */
  if (pci_enable_msi(unicorn.pci))
  {
    printk(KERN_INFO "unicorn : failed to activate MSI interrupts !\n");
    return -1;
  }

  ret = request_irq(unicorn.pci->irq, unicorn_irq_handler, 0, DRV_NAME, &unicorn);
  if (ret)
  {
    printk(KERN_INFO "unicorn : can't get IRQ %d, err = %d\n", unicorn.pci->irq, ret);
    return -1;
  }

  return 0;
}

static int unicorn_pci_probe(struct pci_dev *pci_dev,
                  const struct pci_device_id *pci_id)
{
  int err=0;

  /* PCIe identification : */
  if (pci_id->vendor != PEGD_VENDOR_ID)
  {
    printk(KERN_INFO "unicorn_probe, vendor != PEGD_VENDOR_ID !\n");
    return -1;
  }

  unicorn.pci = pci_dev;

#ifdef  CONFIG_AL_UNICORN_WIDTH_VIDEO_SUPPORT
  err = v4l2_device_register(&pci_dev->dev, &unicorn.v4l2_dev);
  if (err < 0)
  {
    printk(KERN_INFO "fail to register V4L2 device\n");
    return err;
  }
#endif

  err =  unicorn_dev_setup();
  if(err < 0)
  {
    goto fail_unregister_device;
  }
  err = unicorn_interrupt_init();
  if(err < 0)
  {
    goto fail_disable_device;
  }

  err = unicorn_init_i2c(&unicorn);
  if(err < 0)
  {
    goto fail_irq_disable;
  }

#ifdef  CONFIG_AL_UNICORN_WIDTH_VIDEO_SUPPORT
  err = unicorn_init_video(&unicorn);
  if(err < 0)
  {
    goto fail_i2c_remove;
  }
#endif

  return 0;

#ifdef  CONFIG_AL_UNICORN_WIDTH_VIDEO_SUPPORT
fail_i2c_remove :
#endif
  unicorn_i2c_remove(&unicorn);
fail_irq_disable :
  free_irq(unicorn.pci->irq, &unicorn);

if (unicorn.pci->msi_enabled)
  {
    pci_disable_msi(pci_dev);
  }

fail_disable_device :
  if(unicorn.fpga_regs)
  {
    unmap_bars(&unicorn);
  }
  pci_disable_device(unicorn.pci);
  pci_release_regions(unicorn.pci);

fail_unregister_device :

#ifdef  CONFIG_AL_UNICORN_WIDTH_VIDEO_SUPPORT
  v4l2_device_unregister(&unicorn.v4l2_dev);
#endif

  return err;
}

static void unicorn_pci_remove(struct pci_dev *pci_dev)
{
#ifdef  CONFIG_AL_UNICORN_WIDTH_VIDEO_SUPPORT
  struct v4l2_device *v4l2_dev = pci_get_drvdata(pci_dev);
  int i;
#endif



#ifdef  CONFIG_AL_UNICORN_WIDTH_VIDEO_SUPPORT
  for (i = 0; i < VID_CHANNEL_NUM; i++)
  {
    unicorn_video_unregister(&unicorn, i);
  }
#endif

  unicorn_i2c_remove(&unicorn);

#ifdef  CONFIG_AL_UNICORN_WIDTH_VIDEO_SUPPORT
  if (unicorn.fpga_regs)
  {
    unicorn.global_register->video[0].ctrl &= ~VIDEO_CONTROL_ENABLE;
    unicorn.global_register->video[1].ctrl &= ~VIDEO_CONTROL_ENABLE;
    unicorn.global_register->video_in_reset = RESET_ALL_VIDEO_INPUT;
    unicorn.pcie_dma->dma[0].ctrl |= DMA_CONTROL_RESET;
    unicorn.pcie_dma->dma[1].ctrl |= DMA_CONTROL_RESET;
  }
#endif

  free_irq(pci_dev->irq, &unicorn);

  if (pci_dev->msi_enabled)
  {
    pci_disable_msi(pci_dev);
  }

  if(unicorn.fpga_regs)
  {
    unmap_bars(&unicorn);
  }

  pci_disable_device(unicorn.pci);

  pci_release_regions(unicorn.pci);

#ifdef  CONFIG_AL_UNICORN_WIDTH_VIDEO_SUPPORT
  v4l2_device_unregister(v4l2_dev);
#endif
}


static struct pci_device_id unicorn_pci_tbl[] = {
  {
   .vendor = PCI_VENDOR_ID_XILINX,
   .device = PEGD_DEVICE_ID,
   .subvendor = PEGD_SUBVENDOR_ID,
   .subdevice = PEGD_SUBDEVICE_ID,
   },
  {
   /* --- end of list --- */
   }
};

MODULE_DEVICE_TABLE(pci, unicorn_pci_tbl);

struct pci_driver __refdata unicorn_pci_driver =
{
   name:      PEGD_DEVICE_NAME,
   id_table:  unicorn_pci_tbl,
   probe:     unicorn_pci_probe,
   remove:    unicorn_pci_remove
};



static int __init unicorn_init(void)
{
  int err = 0;

  if(max_subdev_per_video_bus > MAX_SUBDEV_PER_VIDEO_BUS)
    max_subdev_per_video_bus = MAX_SUBDEV_PER_VIDEO_BUS;

/* Clear unicorn structure : */
  memset(&unicorn, 0, sizeof(unicorn));

/* PCI bus initialisation : */
  if (pci_register_driver(&unicorn_pci_driver))
  {
    printk(KERN_INFO "error : pci_register_driver(&unicorn_pci_driver) != 0 !\n");
    return -1;
  }

#ifdef CONFIG_AL_UNICORN_SYSFS_FLASH
  unicorn.class_unicorn=class_create(THIS_MODULE,"unicorn");
  err = unicorn_spi_flash_init(unicorn.class_unicorn,&unicorn);
#endif

  return err;
}

static void __exit unicorn_exit(void)
{
  pci_unregister_driver(&unicorn_pci_driver);

#ifdef CONFIG_AL_UNICORN_SYSFS_FLASH
  class_destroy(unicorn.class_unicorn);
#endif
  printk(KERN_INFO "UNICORN module unregistered\n");
}

module_init(unicorn_init);
module_exit(unicorn_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_SUPPORTED_DEVICE(PEGD_DEVICE_NAME);

/* unicorn.c end */
