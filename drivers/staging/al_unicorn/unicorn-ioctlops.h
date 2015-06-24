/*
    unicorn-ioctlops.h - V4L2 driver for unicorn

    Copyright (c) 2013 Aldebaran robotics
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

#ifndef UNICORN_IOCTLOPS_H
#define UNICORN_IOCTLOPS_H

#ifdef  CONFIG_AL_UNICORN_WIDTH_VIDEO_SUPPORT
#include <media/v4l2-ioctl.h>
#include "unicorn.h"

extern const struct v4l2_ioctl_ops video_ioctl_ops;
#endif

#endif
