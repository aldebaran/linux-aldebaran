/*
   unicorn-video.c - V4L2 driver for FPGA UNICORN

   Copyright (c) 2010 Aldebaran robotics
   joseph pinkasfeld joseph.pinkasfeld@gmail.com
   Ludovic SMAL <lsmal@aldebaran-robotics.com>

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

#define __UNICORN_VIDEO_C

#include "unicorn-video.h"

#include <linux/videodev2.h>
#include "unicorn-mmap.h"
#include "unicorn-ioctlops.h"
#include "unicorn-vbuff.h"

static unsigned int video_nr[] = {[0 ... (UNICORN_MAXBOARDS - 1)] = UNSET };
module_param_array(video_nr, int, NULL, 0444);
MODULE_PARM_DESC(video_nr, "video device numbers");

module_param(video_debug, int, 0644);
MODULE_PARM_DESC(video_debug, "enable debug messages [video]");

unsigned int vid_limit = 48;
module_param(vid_limit, int, 0644);
MODULE_PARM_DESC(vid_limit, "capture memory limit in megabytes");

module_param(max_subdev_per_video_bus, int, 0444);
MODULE_PARM_DESC(max_subdev_per_video_bus, "maximal capture subdevice per physical video bus");

int unicorn_start_video_dma(struct unicorn_dev *dev,
    struct unicorn_buffer *buf,
    struct unicorn_fh *fh)
{
  unsigned long addr=0;
  int size = 0;

  dprintk_video(1, dev->name, "%s() channel:%d input:%d fps:%d\n", __func__, fh->channel, fh->input, dev->fps_limit[fh->channel]);

  dev->global_register->video[fh->channel].ctrl |= DMA_CONTROL_RESET;

  dev->interrupts_controller->irq.ctrl |=

    /* Init IT Channel 0 */
    IT_DMA_CHAN_0_TX_BUFF_0_END   |
    IT_DMA_CHAN_0_TX_BUFF_1_END   |
    IT_DMA_CHAN_0_ERROR           |
    IT_DMA_CHAN_0_FIFO_FULL_ERROR |
    IT_VIDEO_CHANNEL_0_OF_TRAME   |

    /* Init IT Channel 1 */
    IT_DMA_CHAN_1_TX_BUFF_0_END   |
    IT_DMA_CHAN_1_TX_BUFF_1_END   |
    IT_DMA_CHAN_1_ERROR           |
    IT_DMA_CHAN_1_FIFO_FULL_ERROR |
    IT_VIDEO_CHANNEL_1_OF_TRAME   |

    /* AHB32 Error */
    IT_ABH32_ERROR                |
    IT_ABH32_FIFO_RX_ERROR        |
    IT_ABH32_FIFO_TX_ERROR        |
    IT_RT;

  dev->global_register->video_in_reset |= 0x01 << fh->input;

  dev->global_register->video[fh->channel].ctrl &= ~VIDEO_CONTROL_INPUT_SEL_MASK;
  dev->global_register->video[fh->channel].ctrl |= (fh->input)<<VIDEO_CONTROL_INPUT_SEL_POS;

  //  dev->global_register->video[fh->channel].ctrl |= VIDEO_CONTROL_TIMESTAMP_INSERT;

  dev->global_register->video[fh->channel].nb_lines = fh->height;

  dev->global_register->video[fh->channel].nb_pixels = fh->width;

  if(dev->fps_limit[fh->channel]!=0)
  {
    dev->global_register->video[fh->channel].nb_us_inhibit = (1000/dev->fps_limit[fh->channel])*1000;
  }
  else
  {
    dev->global_register->video[fh->channel].nb_us_inhibit = 0;
  }

  dev->global_register->video_in_reset &= ~ (0x01 << fh->input) ;

  size = fh->width*fh->height*(fh->fmt->depth>>3)/UNICORN_DMA_BLOC_SIZE;
  dev->pcie_dma->dma[fh->channel].buff[0].size = size;
  dev->pcie_dma->dma[fh->channel].buff[1].size = 0;
  addr = videobuf_to_dma_contig(&buf->vb);
  dev->pcie_dma->dma[fh->channel].buff[0].addr = addr;
  dev->pcie_dma->dma[fh->channel].buff[1].addr = 0;

  dev->pcie_dma->dma[fh->channel].ctrl &= ~DMA_CONTROL_AUTO_START;
  dev->pcie_dma->dma[fh->channel].ctrl |= DMA_CONTROL_START;
  dev->global_register->video[fh->channel].ctrl |= VIDEO_CONTROL_ENABLE;

  dprintk_video(1, dev->name, "dma_0_start_add=%x dma_1_start_add=%x", (unsigned int)&dev->pcie_dma->dma[0], (unsigned int)&dev->pcie_dma->dma[1]);

  return 0;
}

int unicorn_recover_fifo_full_error(struct unicorn_dev *dev,
    int channel)
{
  dprintk_video(1,dev->name,"%s() channel:%d",__func__, channel);

  spin_lock(&dev->slock);

  dev->fifo_full_error |= 1<<channel;
  dev->global_register->video[channel].ctrl &= ~VIDEO_CONTROL_ENABLE;
  dev->global_register->video_in_reset  |=  (0x01 << (
        (dev->global_register->video[channel].ctrl & VIDEO_CONTROL_INPUT_SEL_MASK)>>VIDEO_CONTROL_INPUT_SEL_POS));
  dev->pcie_dma->dma[channel].ctrl |= DMA_CONTROL_RESET;

  spin_unlock(&dev->slock);

  return 0;
}

int unicorn_continue_video_dma(struct unicorn_dev *dev,
    struct unicorn_buffer *buf,
    struct unicorn_fh *fh,
    int buff_index)
{
  unsigned long addr=0;
  int size=0;
  dprintk_video(1, dev->name, "%s() channel %d buf %d \n", __func__, fh->channel,buff_index);
  size = fh->width*fh->height*(fh->fmt->depth>>3)/UNICORN_DMA_BLOC_SIZE;
  dev->pcie_dma->dma[fh->channel].buff[buff_index].size = size;
  addr = videobuf_to_dma_contig(&buf->vb);
  dev->pcie_dma->dma[fh->channel].buff[buff_index].addr = addr;

  return 0;
}

int unicorn_video_dma_flipflop_buf(struct unicorn_dev *dev,
    struct unicorn_buffer *buf,
    struct unicorn_fh *fh)
{
  unsigned long addr=0;
  int size=0;

  dprintk_video(1, dev->name, "%s() channel %d buff[0].size=%d buff[1].size=%d\n", __func__, fh->channel,
      dev->pcie_dma->dma[fh->channel].buff[0].size,
      dev->pcie_dma->dma[fh->channel].buff[1].size);

  if(dev->pcie_dma->dma[fh->channel].buff[0].size == 0)
  {
    size = fh->width*fh->height*(fh->fmt->depth>>3)/UNICORN_DMA_BLOC_SIZE;
    dev->pcie_dma->dma[fh->channel].ctrl |= DMA_CONTROL_AUTO_START;
    dev->pcie_dma->dma[fh->channel].buff[0].size = size;
    addr = videobuf_to_dma_contig(&buf->vb);
    dev->pcie_dma->dma[fh->channel].buff[0].addr = addr;
    return 1;

  }

  if(dev->pcie_dma->dma[fh->channel].buff[1].size == 0)
  {
    size = fh->width*fh->height*(fh->fmt->depth>>3)/UNICORN_DMA_BLOC_SIZE;
    dev->pcie_dma->dma[fh->channel].ctrl |= DMA_CONTROL_AUTO_START;
    dev->pcie_dma->dma[fh->channel].buff[1].size = size;
    addr = videobuf_to_dma_contig(&buf->vb);
    dev->pcie_dma->dma[fh->channel].buff[1].addr = addr;
    return 1;

  }

  return 0;
}

int unicorn_video_change_fps(struct unicorn_dev *dev, struct unicorn_fh *fh)
{
  int channel = fh->channel;

  spin_lock(&dev->slock);

  dev->fifo_full_error |= 1<<channel;
  dev->global_register->video[channel].ctrl &= ~VIDEO_CONTROL_ENABLE;
  dev->global_register->video_in_reset  |=  (0x01 << (
        (dev->global_register->video[channel].ctrl & VIDEO_CONTROL_INPUT_SEL_MASK)>>VIDEO_CONTROL_INPUT_SEL_POS));
  dev->pcie_dma->dma[channel].ctrl |= DMA_CONTROL_RESET;

  spin_unlock(&dev->slock);

  return 0;
}

struct video_device *unicorn_vdev_init(struct unicorn_dev *dev,
    struct pci_dev *pci,
    struct video_device *template,
    char *type)
{
  struct video_device *vfd;
  dprintk_video(1, dev->name, "%s() %d\n", __func__,template->index);

  vfd = video_device_alloc();
  if (NULL == vfd) {
    return NULL;
  }
  *vfd = *template;
  vfd->v4l2_dev = &dev->v4l2_dev;
  vfd->release = video_device_release;
  snprintf(vfd->name, sizeof(vfd->name), "%s %s ", dev->name, type);
  video_set_drvdata(vfd, dev);
  return vfd;
}

void unicorn_video_unregister(struct unicorn_dev *dev, int chan_num)
{
  struct unicorn_fh *fh = dev->vidq[chan_num].fh;
  int index=0;

  // Free FileHandler
  {
    for (index=0; index < VIDEO_MAX_FRAME; ++index) {
      video_mmap_mapper_free(fh, index);
    }
    kfree(fh);
    dev->vidq[chan_num].fh = NULL;
  }

  if (dev->video_dev[chan_num]) {
    if (video_is_registered(dev->video_dev[chan_num])) {
      video_unregister_device(dev->video_dev[chan_num]);
    }
    else {
      video_device_release(dev->video_dev[chan_num]);
    }

    dev->video_dev[chan_num] = NULL;
    dprintk(0, dev->name, "%s() device %d released!\n", __func__, chan_num);
  }
}

void unicorn_video_timeout(unsigned long data)
{
  struct unicorn_timeout_data *timeout_data = (struct unicorn_timeout_data *)data;
  struct unicorn_dev *dev = timeout_data->dev;
  struct unicorn_dmaqueue *q = timeout_data->vidq;
  struct unicorn_buffer *buf;
  unsigned long flags;
  volatile unsigned long addr=0, addr1=0;
  int size = 0;
  dprintk_video(1, dev->name, "%s() channel:%d fifo_error:0x%x nb_timeout_fifo_full_error:0x%x\n", __func__, q->fh->channel,dev->fifo_full_error,
      dev->nb_timeout_fifo_full_error);

  spin_lock_irqsave(&dev->slock, flags);


  if(dev->fifo_full_error & (1<<q->fh->channel))
  {
    if( GET_TIMEOUT(dev->nb_timeout_fifo_full_error,q->fh->channel) < TIMEOUT_RETRY)
    {
      dev->nb_timeout_fifo_full_error = INC_TIMEOUT(dev->nb_timeout_fifo_full_error,q->fh->channel);
      mod_timer(&q->timeout, jiffies + BUFFER_TIMEOUT);

      if(dev->fps_limit[q->fh->channel]!=0)
      {
        dev->global_register->video[q->fh->channel].nb_us_inhibit = (1000/dev->fps_limit[q->fh->channel])*1000;
      }
      else
      {
        dev->global_register->video[q->fh->channel].nb_us_inhibit = 0;
      }

      dev->global_register->video_in_reset &= ~ (0x01 << q->fh->input) ;

      size = q->fh->width*q->fh->height*(q->fh->fmt->depth>>3)/UNICORN_DMA_BLOC_SIZE;

      dev->pcie_dma->dma[q->fh->channel].buff[0].size = size;
      dev->pcie_dma->dma[q->fh->channel].buff[1].size = size;

      addr = dev->pcie_dma->dma[q->fh->channel].buff[0].addr;
      addr1 = dev->pcie_dma->dma[q->fh->channel].buff[1].addr;

      dev->pcie_dma->dma[q->fh->channel].buff[0].addr = addr;
      dev->pcie_dma->dma[q->fh->channel].buff[1].addr = addr1;


      dev->pcie_dma->dma[q->fh->channel].ctrl |= DMA_CONTROL_AUTO_START;
      dev->pcie_dma->dma[q->fh->channel].ctrl |= DMA_CONTROL_START;
      dev->global_register->video[q->fh->channel].ctrl |= VIDEO_CONTROL_ENABLE;

      spin_unlock_irqrestore(&dev->slock, flags);
      return;
    }
    else
    {
      dev->fifo_full_error &= ~(1<<q->fh->channel);
      dev->nb_timeout_fifo_full_error = RESET_TIMEOUT(dev->nb_timeout_fifo_full_error,q->fh->channel);
    }
  }



  while (!list_empty(&q->active)) {
    buf = list_entry(q->active.next, struct unicorn_buffer, vb.queue);
    list_del(&buf->vb.queue);

    buf->vb.state = VIDEOBUF_ERROR;
    wake_up(&buf->vb.done);
  }
  while (!list_empty(&q->queued)) {
    buf = list_entry(q->queued.next, struct unicorn_buffer, vb.queue);
    list_del(&buf->vb.queue);

    buf->vb.state = VIDEOBUF_ERROR;
    wake_up(&buf->vb.done);
  }

  spin_unlock_irqrestore(&dev->slock, flags);
}

int unicorn_video_register(struct unicorn_dev *dev, int chan_num,
    struct video_device *video_template)
{
  int err;
  /* init video dma queues */
  spin_lock_init(&dev->slock);
  dev->timeout_data[chan_num].dev = dev;

  dev->timeout_data[chan_num].video =(struct video_in *) &dev->global_register->video[chan_num];
  dev->timeout_data[chan_num].dma = (struct dma_wr *) &dev->pcie_dma->dma[chan_num];
  dev->timeout_data[chan_num].vidq = &dev->vidq[chan_num];

  INIT_LIST_HEAD(&dev->vidq[chan_num].active);
  INIT_LIST_HEAD(&dev->vidq[chan_num].queued);
  dev->vidq[chan_num].timeout.function = unicorn_video_timeout;
  dev->vidq[chan_num].timeout.data =
    (unsigned long)&dev->timeout_data[chan_num];
  init_timer(&dev->vidq[chan_num].timeout);

  /* register v4l devices */
  dev->video_dev[chan_num] =
    unicorn_vdev_init(dev, dev->pci, video_template, "video");
  err =
    video_register_device(dev->video_dev[chan_num], VFL_TYPE_GRABBER,
        video_nr[dev->nr]);

  if (err < 0) {
    unicorn_video_unregister(dev, chan_num);
    return err;
  }


  // Initialize FileHandler
  {
    struct unicorn_fh *fh;
    fh = kzalloc(sizeof(*fh), GFP_KERNEL);
    if (NULL == fh) {
      return -ENOMEM;
    }
    fh->dev = dev;
    fh->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fh->width = MIN_WIDTH;
    fh->height = MIN_HEIGHT;

    fh->channel = chan_num;
    fh->input = fh->channel;
    fh->fmt = format_by_fourcc(V4L2_PIX_FMT_YUYV);

    dev->vidq[chan_num].fh = fh;
  }
  return 0;
}

static int unicorn_probe_camera(struct unicorn_dev *dev, struct v4l2_subdev **v4l2_subdev,
    struct camera_to_probe_t *cam_to_probe)
{
  *v4l2_subdev = v4l2_i2c_new_subdev(&dev->v4l2_dev,
      cam_to_probe->i2c_adapter,
      cam_to_probe->name,
      cam_to_probe->i2c_addr, NULL);

  if (!*v4l2_subdev) {
    dprintk_video(1, dev->name, "i2c subdev not found (%s @ 0x%p).\n",
      cam_to_probe->name, cam_to_probe->i2c_adapter);
    return 0;
  }

  return 1;
}

/* Camera subdevs supported in Aldebaran's robots
*/
static struct {
  char name[32];
  int  i2c_addr;
  int  found_on_i2c_adapter[MAX_I2C_ADAPTER];
}probed_subdevs[] = {
  {  .name     = "ov5640",
    .i2c_addr = 0x3c,
    .found_on_i2c_adapter = {0}
  }
  ,{ .name     = "mt9m114",
    .i2c_addr = 0x48,
    .found_on_i2c_adapter = {0}
  }
  ,{ .name     = "mt9m114",
    .i2c_addr = 0x5d,
    .found_on_i2c_adapter = {0}
  }
};


static int unicorn_attach_camera(struct unicorn_dev *dev)
{
  int i=0, try_i=0;

  dev->sensor[MIRE_VIDEO_INPUT][0] = NULL;

  // i2c_adapter[MAX_I2C_ADAPTER -1] is the multicast i2c device
  for(i=0; i<(MAX_I2C_ADAPTER-1); i++)
  {
    int nb_cam_on_vid_bus = 0;
    for (try_i=0; try_i<3; try_i++)
    {
      int k;
      for(k=0; k<(sizeof(probed_subdevs)/sizeof(probed_subdevs[0])); ++k)
      {
        struct camera_to_probe_t cam_to_prob = {
          .name        = probed_subdevs[k].name,
          .i2c_addr    = probed_subdevs[k].i2c_addr,
          .i2c_adapter = dev->i2c_adapter[i]
        };

        if(nb_cam_on_vid_bus >= max_subdev_per_video_bus)
          break;

        if(probed_subdevs[k].found_on_i2c_adapter[i])
          continue;

        if(unicorn_probe_camera(dev, &dev->sensor[i][nb_cam_on_vid_bus], &cam_to_prob))
        {
          probed_subdevs[k].found_on_i2c_adapter[i] = 1;
          nb_cam_on_vid_bus++;
        }
      }

      if(nb_cam_on_vid_bus >= max_subdev_per_video_bus)
        break;
    }
  }
  return 0;
}

int unicorn_init_video(struct unicorn_dev *dev)
{
  int i=0;
  struct video_device *video_template[] = {
    &unicorn_video_template0,
    &unicorn_video_template1,
  };

  unicorn_attach_camera(dev);

  for (i = 0; i < VID_CHANNEL_NUM; i++) {
    if (unicorn_video_register(dev, i, video_template[i]) < 0) {
      printk(KERN_ERR
          "%s() Failed to register video channel %d\n",
          __func__, i);
      return -1;
    }
    dev->global_register->video[i].ctrl &= ~VIDEO_CONTROL_ENABLE;
    dev->pcie_dma->dma[i].ctrl |= DMA_CONTROL_RESET;
  }
  dev->global_register->video_in_reset = RESET_ALL_VIDEO_INPUT;
  return 0;
}

