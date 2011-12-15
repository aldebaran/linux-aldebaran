/*
    unicorn-fops.c - V4L2 driver for unicorn

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

#include "unicorn.h"

#ifdef CONFIG_AL_UNICORN_WIDTH_VIDEO_SUPPORT

#include "unicorn-video.h"
#include "unicorn-vbuff.h"
#include "unicorn-ioctlops.h"

extern struct videobuf_queue_ops unicorn_video_qops;

static int video_open(struct file *file)
{
  struct video_device *vdev = video_devdata(file);
  struct unicorn_dev *dev = video_drvdata(file);
  struct unicorn_fh *fh;
  enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

  mutex_lock(&dev->mutex);
  dprintk_video(1, dev->name, "open dev=%s type=%s\n", video_device_node_name(vdev),
    v4l2_type_names[type]);

  /* allocate + initialize per filehandle data */
  fh = kzalloc(sizeof(*fh), GFP_KERNEL);
  if (NULL == fh)
  {
    mutex_unlock(&dev->mutex);
    return -ENOMEM;
  }

  file->private_data = fh;
  fh->dev = dev;
  fh->type = type;
  fh->width = 160;
  fh->height = 120;

  fh->channel = vdev->index;
  fh->input = fh->channel;

  dev->global_register->video[fh->channel].ctrl &= ~VIDEO_CONTROL_INPUT_SEL_MASK;
  dev->global_register->video[fh->channel].ctrl |= (fh->input) << VIDEO_CONTROL_INPUT_SEL_POS;

  dev->pixel_formats[vdev->index] = V4L2_PIX_FMT_YUYV;
  dev->fps_limit[vdev->index] = 0; // No fps limit set
  fh->fmt = format_by_fourcc(V4L2_PIX_FMT_YUYV);

  v4l2_prio_open(&dev->prio, &fh->prio);
  videobuf_queue_dma_contig_init(&fh->vidq, &unicorn_video_qops,
             &dev->pci->dev, &dev->slock,
             V4L2_BUF_TYPE_VIDEO_CAPTURE,
             V4L2_FIELD_NONE,
             sizeof(struct unicorn_buffer), fh);

  dprintk_video(1, dev->name,  "post videobuf_queue_init()\n");
  mutex_unlock(&dev->mutex);

  return 0;
}

static int video_release(struct file *file)
{
  struct unicorn_fh *fh = file->private_data;
  struct unicorn_dev *dev = fh->dev;
  dprintk_video(1, dev->name,  "%s()%d\n", __func__,fh->channel);

  dev->global_register->video[fh->channel].ctrl &= ~VIDEO_CONTROL_ENABLE;
  dev->pcie_dma->dma[fh->channel].ctrl |= DMA_CONTROL_RESET;

  /* stop video capture */
  if (res_check(fh, 0x01<<fh->channel)) {
    videobuf_queue_cancel(&fh->vidq);
    res_free(dev, fh, 0x01<<fh->channel);
  }

  if (fh->vidq.read_buf) {
    buffer_release(&fh->vidq, fh->vidq.read_buf);
    kfree(fh->vidq.read_buf);
  }

  videobuf_mmap_free(&fh->vidq);

  v4l2_prio_close(&dev->prio, &fh->prio);
  file->private_data = NULL;
  kfree(fh);

  return 0;
}

static ssize_t video_read(struct file *file, char __user * data, size_t count,
        loff_t * ppos)
{
  struct unicorn_fh *fh = file->private_data;
  struct unicorn_dev *dev = fh->dev;
  dprintk_video(1, dev->name, "%s()\n", __func__);

  switch (fh->type) {
  case V4L2_BUF_TYPE_VIDEO_CAPTURE:
    if (res_locked(fh->dev, 0x01<<fh->channel))
      return -EBUSY;

    return videobuf_read_one(&fh->vidq, data, count, ppos,
           file->f_flags & O_NONBLOCK);

  default:
    BUG();
    return 0;
  }
}

static unsigned int video_poll(struct file *file,
             struct poll_table_struct *wait)
{
  struct unicorn_fh *fh = file->private_data;
  struct unicorn_buffer *buf;
  struct unicorn_dev *dev = fh->dev;
  dprintk_video(1, dev->name, "%s()\n", __func__);

  if (res_check(fh, 0x01 << fh->channel)) {
    /* streaming capture */
    if (list_empty(&fh->vidq.stream))
      return POLLERR;
    buf = list_entry(fh->vidq.stream.next,
         struct unicorn_buffer, vb.stream);
  } else {
    /* read() capture */
    buf = (struct unicorn_buffer *)fh->vidq.read_buf;
    if (NULL == buf)
      return POLLERR;
  }
  poll_wait(file, &buf->vb.done, wait);
  if (buf->vb.state == VIDEOBUF_DONE || buf->vb.state == VIDEOBUF_ERROR) {

    return POLLIN | POLLRDNORM;
  }

  return 0;
}

int video_mmap(struct file *file, struct vm_area_struct *vma)
{
  struct unicorn_fh *fh = file->private_data;
  struct unicorn_dev *dev = fh->dev;
  dprintk_video(1, dev->name, "%s()\n", __func__);

  return videobuf_mmap_mapper(&fh->vidq, vma);
}

const struct v4l2_file_operations video_fops = {
  .owner = THIS_MODULE,
  .open = video_open,     /* done */
  .release = video_release, /* done */
  .read = video_read,  /* done */
  .poll = video_poll,  /* done */
  .mmap = video_mmap, /* done */
  .ioctl = video_ioctl2, /* not found */
};
#endif
