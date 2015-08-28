/*
    unicorn-vbuff.c - V4L2 driver for FPGA UNICORN

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

#include "unicorn-video.h"
#include "linux/delay.h"

extern unsigned int vid_limit;

void unicorn_free_buffer(struct videobuf_queue *q, struct unicorn_buffer *buf)
{
  struct unicorn_fh *fh = q->priv_data;
  struct unicorn_dev *dev = fh->dev;
  dprintk_video(1, dev->name, "free buffer for video device %d\n", fh->channel);

  BUG_ON(in_interrupt());
  videobuf_waiton(q, &buf->vb, 0, 0);
  videobuf_dma_contig_free(q, &buf->vb);

  buf->vb.state = VIDEOBUF_NEEDS_INIT;
}

static void buffer_queue(struct videobuf_queue *vq, struct videobuf_buffer *vb)
{
  struct unicorn_buffer *buf =
      container_of(vb, struct unicorn_buffer, vb);
  struct unicorn_buffer *prev;
  struct unicorn_fh *fh = vq->priv_data;
  struct unicorn_dev *dev = fh->dev;
  struct unicorn_dmaqueue *q = &dev->vidq[fh->channel];

  if (!list_empty(&q->queued)) {
    list_add_tail(&buf->vb.queue, &q->queued);
    buf->vb.state = VIDEOBUF_QUEUED;
    dprintk_video(1, dev->name, "[%p/%d] buffer_queue - append to queued\n", buf, buf->vb.i);

  } else if (list_empty(&q->active)) {
    list_add_tail(&buf->vb.queue, &q->active);
    buf->vb.state = VIDEOBUF_ACTIVE;
    q->fh=fh;
    unicorn_start_video_dma(dev, buf, fh);
    buf->count = q->count++;
    mod_timer(&q->timeout, jiffies + BUFFER_TIMEOUT);
    dprintk_video(1, dev->name, "[%p/%d] buffer_queue - first active, buf cnt = %d, q->count = %d\n",
      buf, buf->vb.i, buf->count, q->count);
  } else {
    prev =
        list_entry(q->active.prev, struct unicorn_buffer, vb.queue);
    if (prev->vb.width == buf->vb.width
        && prev->vb.height == buf->vb.height
        && prev->fmt == buf->fmt) {
      if(unicorn_video_dma_flipflop_buf(dev, buf, fh))
      {
        list_add_tail(&buf->vb.queue, &q->active);
        buf->vb.state = VIDEOBUF_ACTIVE;
        buf->count = q->count++;
        mod_timer(&q->timeout, jiffies + BUFFER_TIMEOUT);
        dprintk_video(1, dev->name, "[%p/%d] buffer_queue - append to active, buf->count=%d\n",
                buf, buf->vb.i, buf->count);
      } else {
        list_add_tail(&buf->vb.queue, &q->queued);
        buf->vb.state = VIDEOBUF_QUEUED;
        dprintk_video(1, dev->name, "[%p/%d] buffer_queue - first queued\n", buf,
          buf->vb.i);
      }
    }
  }
  if (list_empty(&q->active)) {
    dprintk_video(1, dev->name, "active queue empty!\n");
  }
}


static int buffer_setup(struct videobuf_queue *q, unsigned int *count,
     unsigned int *size)
{
  struct unicorn_fh *fh = q->priv_data;
  struct unicorn_dev *dev = fh->dev;
  dprintk_video(1, dev->name, "%s()\n", __func__);

  *size = MAX_DEPTH * MAX_WIDTH * MAX_HEIGHT >> 3;

  if (0 == *count)
    *count = 32;

  while (*size * *count > vid_limit * 1024 * 1024)
    (*count)--;

  return 0;
}

static int buffer_prepare(struct videobuf_queue *q, struct videobuf_buffer *vb,
       enum v4l2_field field)
{
  struct unicorn_fh *fh = q->priv_data;
  struct unicorn_dev *dev = fh->dev;
  struct unicorn_buffer *buf = container_of(vb, struct unicorn_buffer, vb);
  int rc;

  BUG_ON(NULL == fh->fmt);
  if (fh->width < MIN_WIDTH || fh->width > MAX_WIDTH ||
      fh->height < MIN_HEIGHT || fh->height > MAX_HEIGHT)
    return -EINVAL;

  buf->vb.size = (fh->width * fh->height * fh->fmt->depth) >> 3;

  if (0 != buf->vb.baddr && buf->vb.bsize < buf->vb.size)
    return -EINVAL;

  if (buf->fmt != fh->fmt ||
      buf->vb.width != fh->width ||
      buf->vb.height != fh->height || buf->vb.field != field) {
    buf->fmt = fh->fmt;
    buf->vb.width = fh->width;
    buf->vb.height = fh->height;
    buf->vb.field = field;
  }

  if (VIDEOBUF_NEEDS_INIT == buf->vb.state) {
    dprintk_video(2, dev->name, "%s() iolock on video device %d\n", __func__, fh->channel);
    rc = videobuf_iolock(q, &buf->vb, NULL);
    if (0 != rc) {
      printk(KERN_DEBUG "videobuf_iolock failed!\n");
      goto fail;
    }
  }

  dprintk_video(2, dev->name, "[%p/%d] buffer_prep - %dx%d %dbpp \"%s\" \n",
    buf, buf->vb.i, fh->width, fh->height, fh->fmt->depth,
    fh->fmt->name);

  buf->vb.state = VIDEOBUF_PREPARED;

  return 0;

fail:
  unicorn_free_buffer(q, buf);
  return rc;
}

void buffer_release(struct videobuf_queue *q, struct videobuf_buffer *vb)
{
  struct unicorn_buffer *buf = container_of(vb, struct unicorn_buffer, vb);
  unicorn_free_buffer(q, buf);
}

const struct videobuf_queue_ops unicorn_video_qops = {
  .buf_setup = buffer_setup,
  .buf_prepare = buffer_prepare,
  .buf_queue = buffer_queue,
  .buf_release = buffer_release,
};
