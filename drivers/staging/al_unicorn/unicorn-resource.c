/*
    unicorn-resource.c - V4L2 driver for unicorn

    Copyright (c) 2010 Aldebaran robotics
    Corentin Le Molgat <clemolgat@aldebaran-robotics.com>

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

#include "unicorn-resource.h"

#ifdef  CONFIG_AL_UNICORN_WIDTH_VIDEO_SUPPORT

int res_check(struct unicorn_fh *fh, unsigned int bit)
{
  return fh->resources & bit;
}

int res_locked(struct unicorn_dev *dev, unsigned int bit)
{
  return dev->resources & bit;
}

int res_get(struct unicorn_dev *dev, struct unicorn_fh *fh, unsigned int bit)
{
  dprintk_video(1, dev->name, "%s()\n", __func__);
  if (fh->resources & bit)
    /* have it already allocated */
    return 1;

  /* is it free? */
  mutex_lock(&dev->mutex);
  if (dev->resources & bit)
  {
    /* no, someone else uses it */
    mutex_unlock(&dev->mutex);
    return 0;
  }
  /* it's free, grab it */
  fh->resources |= bit;
  dev->resources |= bit;
  dprintk_video(1, dev->name, "res: get %d\n", bit);
  mutex_unlock(&dev->mutex);
  return 1;
}

void res_free(struct unicorn_dev *dev, struct unicorn_fh *fh, unsigned int bits)
{
  BUG_ON((fh->resources & bits) != bits);
  dprintk_video(1, dev->name, "%s()\n", __func__);

  mutex_lock(&dev->mutex);
  fh->resources &= ~bits;
  dev->resources &= ~bits;
  dprintk_video(1, dev->name, "res: put %d\n", bits);
  mutex_unlock(&dev->mutex);
}

int get_resource(struct unicorn_fh *fh, int resource)
{
  switch (fh->type) {
  case V4L2_BUF_TYPE_VIDEO_CAPTURE:
    return resource;
  default:
    BUG();
    return 0;
  }
}

#endif
