/* <LIC_AMD_STD>
 * Copyright (c) 2005 Advanced Micro Devices, Inc.
 * 
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 * 
 * The full GNU General Public License is included in this distribution in the
 * file called COPYING
 * </LIC_AMD_STD>  */
/* <CTL_AMD_STD>
 * </CTL_AMD_STD>  */
/* <DOC_AMD_STD>
 * Test application for the V4l2 geode driver. 
 * </DOC_AMD_STD>  */

   
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include "linux/videodev.h"

int fd_video = -1;

void open_devices()
{
   fd_video = open("/dev/video0", O_RDWR);
   if( fd_video < 0 ) perror("/dev/video0");
}

void close_devices()
{
   close(fd_video);
}

void set_video_fmt(int width, int height)
{
   struct v4l2_format fmt;
   fmt.fmt.pix.width = width;
   fmt.fmt.pix.height = height;
   fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_UYVY;
#ifndef V4L2_BUF_TYPE_CAPTURE
   fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
   fmt.fmt.pix.field = V4L2_FIELD_INTERLACED;
#else
   fmt.type = V4L2_BUF_TYPE_CAPTURE;
   fmt.fmt.pix.flags = V4L2_FMT_FLAG_INTERLACED;
#endif
   printf("%d: s_fmt capt\n",ioctl(fd_video, VIDIOC_S_FMT, &fmt));
}

void set_vout_window(int x, int y, int width, int height)
{
#ifndef V4L2_BUF_TYPE_CAPTURE
   struct v4l2_format fmt;
   fmt.type = V4L2_BUF_TYPE_VIDEO_OVERLAY;
   fmt.fmt.win.w.left = x;
   fmt.fmt.win.w.top = y;
   fmt.fmt.win.w.width = width;
   fmt.fmt.win.w.height = height;
   printf("%d: s_fmt win\n",ioctl(fd_video, VIDIOC_S_FMT, &fmt));
#else
   struct v4l2_window win;
   win.x = x;
   win.y = y;
   win.width = width;
   win.height = height;
   win.clipcount = 0;
   printf("%d: s_win\n",ioctl(fd_video, VIDIOC_S_WIN, &win));
#endif
}

void set_input(int inp)
{
   int input = inp;
   printf("%d: s_input\n",ioctl(fd_video, VIDIOC_S_INPUT,&input));
}

void capture_enable( int enable)
{
#ifndef V4L2_BUF_TYPE_CAPTURE
   int dummy = V4L2_BUF_TYPE_VIDEO_CAPTURE;
#else
   int dummy = V4L2_BUF_TYPE_CAPTURE;
#endif
   if(enable)
      printf("%d: streamon\n",ioctl(fd_video, VIDIOC_STREAMON, &dummy));
   else
      printf("%d: streamoff\n",ioctl(fd_video, VIDIOC_STREAMOFF, &dummy));
}

void preview(int enable)
{
#ifdef VIDIOC_OVERLAY
   printf("%d: overlay\n",ioctl(fd_video, VIDIOC_OVERLAY,&enable));
#else
   printf("%d: overlay\n",ioctl(fd_video, VIDIOC_PREVIEW,&enable));
#endif
}

void set_chan(int ch)
{
#ifdef VIDIOC_S_FREQ
   printf("%d: s_freq\n",ioctl(fd_video, VIDIOC_S_FREQ,&ch));
#endif
}

int main(int argc, char ** argv)
{
   int i;
   int x = 20;
   int y = 20;
   int enable;
   int input = 0;
   int width = 720;
   int height = 480;
   if( argc <= 1 || argc > 6 ) {
      printf("Usage: %s <input> <x> <y> <width> <height>\n",argv[0]);
      exit(1);
   }
   if( argc > 1 ) input = atoi(argv[1]);
   if( argc > 2 ) x = atoi(argv[2]);
   if( argc > 3 ) y = atoi(argv[3]);
   if( argc > 4 ) width = atoi(argv[4]);
   if( argc > 5 ) height = atoi(argv[5]);
   open_devices();
   printf("%s  %d,%d %dx%d\n", argv[0],x,y,width,height);
   set_input(input);
   set_vout_window(x,y,width,height);
   capture_enable(1);
   preview(1); // show video
   getchar();
   width = 400; height = 300;
   set_vout_window(x,y,width,height);
   preview(1); // show video
   getchar();
   capture_enable(0);
   getchar();
   preview(0); // terminate video
   close_devices();
   printf("Cleaning up\n");
}
