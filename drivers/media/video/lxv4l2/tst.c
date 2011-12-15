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
 * </DOC_AMD_STD>  */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <malloc.h>
#include <signal.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/ioctl.h>

#define LINUX_2_6

//#define REQBUFS
//#define PLANAR

#ifdef LINUX_2_6
#define REV2
#endif
#include <linux/videodev.h>

#define DEV_VIDEO "/dev/video0"
#define DEV_VIDEO2 "/dev/video1"
#define BFRSZ 0x100000

#define IOCTLF(fd,fn,arg) IoCtl(fd,#fn,fn,arg,1)
#define IOCTL(fd,fn,arg) IoCtl(fd,#fn,fn,arg,0)
//#define printf(s...) do { } while(0)

int last_error = 0;

int IoCtl(int fd,char *cp,unsigned int fn,void *arg,int fatal)
{
   char msg[128];
   int ret;
   printf("%d:%s\n",fd,cp);
   ret = ioctl(fd,fn,arg);
   if( ret != 0 ) {
      last_error = errno;
      sprintf(&msg[0],"ioctl(%d,%08x,%p)",fd,fn,arg);
      perror(&msg[0]);
      if( fatal == 0 ) return ret;
      exit(1);
   }
   return 0;
}

int done = 0;
void sigint(int n)
{
   done = 1;
}

/*
 * YCbCr is defined per CCIR 601-1, except that Cb and Cr are
 * normalized to the range 0..MAXJSAMPLE rather than -0.5 .. 0.5.
 * The conversion equations to be implemented are therefore
 *        R = Y                + 1.40200 * Cr
 *        G = Y - 0.34414 * Cb - 0.71414 * Cr
 *        B = Y + 1.77200 * Cb
 * where Cb and Cr represent the incoming values less CENTERJSAMPLE.
 * (These numbers are derived from TIFF 6.0 section 21, dated 3-June-92.)
 */

#define SCALEBITS 16
#define ONE_HALF  ((int) 1 << (SCALEBITS-1))
#define FIX(x)          ((int) ((x) * (1L<<SCALEBITS) + 0.5))
#define MAXJSAMPLE      255
#define CENTERJSAMPLE   128
#define RIGHT_SHIFT(x,shft) ((x) >> (shft))

/*
 * Initialize tables for YCC->RGB colorspace conversion.
 */

int Crrtab[MAXJSAMPLE+1];
int Cbbtab[MAXJSAMPLE+1];
int Crgtab[MAXJSAMPLE+1];
int Cbgtab[MAXJSAMPLE+1];
int xrange_limit[3*(MAXJSAMPLE+1)];
int *range_limit;

static void
build_ycc_rgb_table(void)
{
  int i, x;
  for( i=0,x=-CENTERJSAMPLE; i<=MAXJSAMPLE; ++i,++x ) {
    Crrtab[i] = (int) RIGHT_SHIFT(FIX(1.40200)*x + ONE_HALF, SCALEBITS);
    Cbbtab[i] = (int) RIGHT_SHIFT(FIX(1.77200)*x + ONE_HALF, SCALEBITS);
    Crgtab[i] = (-FIX(0.71414)) * x;
    Cbgtab[i] = (-FIX(0.34414)) * x + ONE_HALF;
  }
  for( i=0; i<=MAXJSAMPLE; ++i )
     xrange_limit[i] = 0;
  for( x=0; i<=(MAXJSAMPLE+1)+MAXJSAMPLE; ++i,++x )
     xrange_limit[i] = x;
  for( ; i<=2*(MAXJSAMPLE+1)+MAXJSAMPLE; ++i )
     xrange_limit[i] = MAXJSAMPLE;
  range_limit = &xrange_limit[MAXJSAMPLE+1];
}

#define RGB_RED 0
#define RGB_GREEN 1
#define RGB_BLUE 2
#define RGB_PIXELSIZE 3

static int planar =
#ifdef PLANAR
   1;
#else
   0;
#endif

void
ycc_rgb_convert(unsigned char *input_buf, int rows, int cols,
                unsigned char *output_buf)
{
  int y1, y2, cb, cr, col, row, red, green, blue;
  unsigned char *yp, *up, *vp, *us, *vs;
  yp = &input_buf[0];
  us = up = yp + rows*cols;
  vs = vp = up + rows*cols/4;

  row = 0;
  while( --rows >= 0 ) {
    if( planar == 0 ) {
      if( (row&1) == 0 ) { us = up;  vs = vp; } else { up = us;  vp = vs; }
    }
    col = cols;
    while( col > 0 ) {
      if( planar == 0 ) {
        cb = *yp++; y1 = *yp++; cr = *yp++; y2 = *yp++;
      }
      else {
        cb = *up++; y1 = *yp++; cr = *vp++; y2 = *yp++;
      }
      red = y1 + Crrtab[cr];
      output_buf[RGB_RED] = range_limit[red];
      green = y1 + ((int) RIGHT_SHIFT(Cbgtab[cb] + Crgtab[cr], SCALEBITS));
      output_buf[RGB_GREEN] = range_limit[green];
      blue = y1 + Cbbtab[cb];
      output_buf[RGB_BLUE] = range_limit[blue];
      output_buf += RGB_PIXELSIZE;
      red = y2 + Crrtab[cr];
      output_buf[RGB_RED] = range_limit[red];
      output_buf[RGB_RED] =   range_limit[red];
      green = y2 + ((int) RIGHT_SHIFT(Cbgtab[cb] + Crgtab[cr], SCALEBITS));
      output_buf[RGB_GREEN] = range_limit[green];
      blue = y2 + Cbbtab[cb];
      output_buf[RGB_BLUE] =  range_limit[blue];
      output_buf += RGB_PIXELSIZE;
      input_buf += 4;
      col -= 2;
    }
    ++row;
  }
}

void writePpm(FILE *fp,char *name,unsigned char *pm, int w,int h)
{
   int i, n;
   fprintf(fp,"P6\n");
   if( name != NULL )
     fprintf(fp,"# %s\n",name);
   fprintf(fp,"%d %d\n",w,h);
   fprintf(fp,"255\n");

   for( n=w*h; --n>=0; ) {
      putc(*pm++,fp);
      putc(*pm++,fp);
      putc(*pm++,fp);
   }
}

int writepict(char *fn,unsigned char *bfr,int width,int height)
{
   FILE *fp;
   unsigned char *rgb = malloc(3*width*height);
   if( rgb == NULL ) {
      perror("malloc");
      return -1;
   }
   build_ycc_rgb_table();
   ycc_rgb_convert(bfr,height,width,&rgb[0]);
   fp = fopen(fn,"w");
   if( fp != NULL ) {
      writePpm(fp,"capture",&rgb[0],width,height);
      fclose(fp);
   }
   else
      perror(fn);
   free(rgb);
   return 0;
}

int main(int ac, char **av)
{

   struct {
      void *start;
      unsigned long offset;
      size_t length;
   } *bfrs;
   int i, n, nbfrs, last_bfr, flag, nbfrz, zn, fd, fd2, enable;
#ifndef LINUX_2_6
   unsigned long vidbfr, bfrsz, bsz;
#endif
   unsigned long offset, ovly_phys_addr;
   unsigned char *bp;
   unsigned char *bfr;
   unsigned char **bfrz;
   char fn[128];
   int einp, eout, eidx;
   struct v4l2_input inp0;
   struct v4l2_output out0;
   struct v4l2_requestbuffers req0;
   struct v4l2_capability cap0;
   struct video_capability cap1;
   struct v4l2_format fmt0;
   struct v4l2_format fmt1;
   struct v4l2_buffer bfr0;
#ifdef REV2
   struct v4l2_format fmt2;
   v4l2_std_id esid0;
#else
   struct v4l2_enumstd estd;
   struct v4l2_window win0;
#endif
   struct v4l2_standard std0;
   struct v4l2_standard *stdp;
   struct video_buffer vbfr0;
   struct v4l2_framebuffer fbfr0;

   fd = open(DEV_VIDEO,O_RDWR+O_NONBLOCK);
   if( fd < 0 ) {
      perror(DEV_VIDEO);
      exit(1);
   }
   fd2 = open(DEV_VIDEO2,O_RDWR+O_NONBLOCK);
   if( fd < 0 ) {
      perror(DEV_VIDEO2);
      exit(1);
   }

   bfr = (unsigned char *)malloc(BFRSZ);
   if( bfr == NULL ) {
      perror("malloc");
      exit(1);
   }

   nbfrz = ac > 2 ? atoi(av[2]) : 1;
   bfrz = (unsigned char **)malloc(nbfrz*sizeof(char*));
   if( bfrz == NULL ) {
      perror("malloc");
      exit(1);
   }

   IOCTL(fd,VIDIOC_G_INPUT,&einp);
   printf(" current input id = %d\n",einp);

   inp0.index = 0;
   while( IOCTL(fd,VIDIOC_ENUMINPUT,&inp0) == 0 ) {
      if( inp0.index == eidx )
         printf ("Current input source: %s\n",&inp0.name[0]);
      printf(" index            - %d\n",inp0.index);
      printf(" name[32]         - %s\n",&inp0.name[0]);
      printf(" type             - %d\n",inp0.type);
#ifdef REV2
      printf(" audioset         - %#x\n",inp0.audioset);
      printf(" tuner            - %d\n",inp0.tuner);
      printf(" std              - %016llx\n",inp0.std);
      printf(" status           - %#x\n",inp0.status);
#else
      printf(" capability       - %d\n",inp0.capability);
      printf(" assoc_audio      - %d\n",inp0.assoc_audio);
#endif
      ++inp0.index;
   }

#ifdef REV2
   IOCTL(fd,VIDIOC_G_STD,&esid0);
   printf(" current standard id = %016llx\n",esid0);

   std0.index = 0;
   while( IOCTL(fd,VIDIOC_ENUMSTD,&std0) == 0 ) {
      if( (std0.id & esid0) != 0 )
         printf ("Current video standard: %s\n",&std0.name[0]);
      printf(" id               - %016llx\n",&std0.id);
      printf(" name[24]         - %s\n",&std0.name[0]);
      printf(" numerator        - %d\n",std0.frameperiod.numerator);
      printf(" denominator      - %d\n",std0.frameperiod.denominator);
      printf(" framelines       - %d\n",std0.framelines);
      ++std0.index;
   }
#else
   IOCTL(fd,VIDIOC_G_STD,&std0);
   printf(" name[24]         - %s\n",&std0.name[0]);
   printf(" numerator        - %d\n",std0.framerate.numerator);
   printf(" denominator      - %d\n",std0.framerate.denominator);
   printf(" framelines       - %d\n",std0.framelines);
   printf(" colorstandard    - %d\n",std0.colorstandard);
   switch( std0.colorstandard ) {
   case V4L2_COLOR_STD_PAL:
      printf(" colorsubcarrier pal - %d\n",std0.colorstandard_data.pal.colorsubcarrier);
      break;
   case V4L2_COLOR_STD_NTSC:
      printf(" colorsubcarrier ntsc - %d\n",std0.colorstandard_data.ntsc.colorsubcarrier);
      break;
   case V4L2_COLOR_STD_SECAM:
      printf(" colorsubcarrier secam - %d/%d\n",
         std0.colorstandard_data.secam.f0b,std0.colorstandard_data.secam.f0r);
      break;
   }
   printf(" transmission     - %d\n",std0.transmission);
#endif

   memset(&cap0,0,sizeof(cap0));
   IOCTLF(fd,VIDIOC_QUERYCAP,&cap0);
#ifdef REV2
   printf(" driver[16]       - %s\n",&cap0.driver[0]);
   printf(" card[32]         - %s\n",&cap0.card[0]);
   printf(" bus_info[32]     - %s\n",&cap0.bus_info[0]);
   printf(" version          - %d\n",cap0.version);
   printf(" capabilities     - %#x\n",cap0.capabilities);
#else
   printf(" name[32]         - %s\n",&cap0.name[0]);
   printf(" type             - %d\n",cap0.type);
   printf(" inputs           - %d\n",cap0.inputs);
   printf(" outputs          - %d\n",cap0.outputs);
   printf(" audios           - %d\n",cap0.audios);
   printf(" maxwidth         - %d\n",cap0.maxwidth);
   printf(" maxheight        - %d\n",cap0.maxheight);
   printf(" minwidth         - %d\n",cap0.minwidth);
   printf(" minheight        - %d\n",cap0.minheight);
   printf(" maxframerate     - %d\n",cap0.maxframerate);
   printf(" flags            - %#x\n",cap0.flags);
#endif

#if 0
   memset(&cap1,0,sizeof(cap1));
   IOCTLF(fd,VIDIOCGCAP,&cap1);
   printf(" name[32]         - %s\n",&cap1.name[0]);
   printf(" type             - %d\n",cap1.type);
   printf(" channels         - %d\n",cap1.channels);
   printf(" audios           - %d\n",cap1.audios);
   printf(" maxwidth         - %d\n",cap1.maxwidth);
   printf(" maxheight        - %d\n",cap1.maxheight);
   printf(" minwidth         - %d\n",cap1.minwidth);
   printf(" minheight        - %d\n",cap1.minheight);

   IOCTLF(fd,VIDIOCGFBUF,&vbfr0);
   printf(" base             - %08lx\n",(unsigned long)vbfr0.base);
   printf(" height           - %d\n",vbfr0.height);
   printf(" width            - %d\n",vbfr0.width);
   printf(" depth            - %d\n",vbfr0.depth);
   printf(" bytesperline     - %d\n",vbfr0.bytesperline);
#endif
   memset(&fmt0,0,sizeof(fmt0));
   fmt0.type =
#ifdef REV2
      V4L2_BUF_TYPE_VIDEO_CAPTURE;
#else
      VID_TYPE_CAPTURE;
#endif
   IOCTL(fd,VIDIOC_G_FMT,&fmt0);
   printf(" type             - 0x%x\n",fmt0.type);
   printf(" pix.width        - %d\n",fmt0.fmt.pix.width);
   printf(" pix.height       - %d\n",fmt0.fmt.pix.height);
#ifndef REV2
   printf(" pix.depth        - %d\n",fmt0.fmt.pix.depth);
#endif
   printf(" pix.pixelformat  - 0x%x(%s)\n",
      fmt0.fmt.pix.pixelformat,(char*)&fmt0.fmt.pix.pixelformat);
#ifdef REV2
   printf(" pix.field        - 0x%x\n",fmt0.fmt.pix.field);
#else
   printf(" pix.flags        - 0x%x\n",fmt0.fmt.pix.flags);
#endif
   printf(" pix.bytesperline - %d\n",fmt0.fmt.pix.bytesperline);
   printf(" pix.sizeimage    - %d\n",fmt0.fmt.pix.sizeimage);
#ifdef REV2
   printf(" pix.colorspace   - 0x%x\n",fmt0.fmt.pix.colorspace);
#endif
   printf(" pix.priv         - %d\n",fmt0.fmt.pix.priv);

#ifdef NO_HD
   fmt0.fmt.pix.width = 1280;
   fmt0.fmt.pix.height = 720;
//   fmt0.fmt.pix.width = 1920;
//   fmt0.fmt.pix.height = 1080;
#endif

   memset(&fmt1,0,sizeof(fmt1));
   fmt1.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
   fmt1.fmt.pix.field = V4L2_FIELD_NONE;
   fmt1.fmt.pix.width = 640;
   fmt1.fmt.pix.height = 480;
   fmt1.fmt.pix.pixelformat = 0x56595559;

   IOCTL(fd,VIDIOC_S_FMT,&fmt1);
   IOCTL(fd2,VIDIOC_S_FMT,&fmt1);
   printf(" type             - 0x%x\n",fmt1.type);
   printf(" pix.width        - %d\n",fmt1.fmt.pix.width);
   printf(" pix.height       - %d\n",fmt1.fmt.pix.height);
#ifndef REV2
   printf(" pix.depth        - %d\n",fmt1.fmt.pix.depth);
#endif
   printf(" pix.pixelformat  - 0x%x(%s)\n",
      fmt1.fmt.pix.pixelformat,(char*)&fmt1.fmt.pix.pixelformat);
#ifdef REV2
   printf(" pix.field        - 0x%x\n",fmt1.fmt.pix.field);
#else
   printf(" pix.flags        - 0x%x\n",fmt1.fmt.pix.flags);
#endif
   printf(" pix.bytesperline - %d\n",fmt1.fmt.pix.bytesperline);
   printf(" pix.sizeimage    - %d\n",fmt1.fmt.pix.sizeimage);
   printf(" pix.priv         - %d\n",fmt1.fmt.pix.priv);

   n = fmt1.fmt.pix.sizeimage;
   for( i=0; i<nbfrz; ++i ) {
      bfrz[i] = malloc(n);
      if( bfrz[i] == NULL ) {
         perror("mallocz");
         exit(1);
      }
      memset(bfrz[i],0,n);
   }

#ifdef REV2
   memset(&fmt2,0,sizeof(fmt2));
   fmt2.type = V4L2_BUF_TYPE_VIDEO_OVERLAY;
   fmt2.fmt.win.w.top = 30;
   fmt2.fmt.win.w.left = 20;
   fmt2.fmt.win.w.width = fmt0.fmt.pix.width;
   fmt2.fmt.win.w.height = fmt0.fmt.pix.height;
   IOCTL(fd,VIDIOC_S_FMT,&fmt2);
   printf(" type               - 0x%x\n",fmt2.type);
   printf(" win.w.left         - %d\n",fmt2.fmt.win.w.left);
   printf(" win.w.top          - %d\n",fmt2.fmt.win.w.top);
   printf(" win.w.width        - %d\n",fmt2.fmt.win.w.width);
   printf(" win.w.height       - %d\n",fmt2.fmt.win.w.height);
#else
   memset(&win0,0,sizeof(win0));
   win0.x = 30;
   win0.y = 20;
   win0.width = fmt0.fmt.pix.width;
   win0.height = fmt0.fmt.pix.height;
   IOCTL(fd,VIDIOC_S_WIN,&win0);
   printf(" win.x              - %d\n",win0.x);
   printf(" win.y              - %d\n",win0.y);
   printf(" win.w.width        - %d\n",win0.width);
   printf(" win.w.height       - %d\n",win0.height);
#endif

#ifdef REQBUFS
   memset(&req0,0,sizeof(req0));
   req0.type =
#ifdef REV2
      V4L2_BUF_TYPE_VIDEO_CAPTURE;
   req0.memory = V4L2_MEMORY_MMAP;
#else
      V4L2_BUF_TYPE_CAPTURE;
#endif
   req0.count = 1000;
   IOCTL(fd,VIDIOC_REQBUFS,&req0);
   printf(" type             - 0x%x\n",req0.type);
   printf(" count            - %d\n",req0.count);
#ifdef LINUX_2_6
   printf(" memory           - %d\n",req0.memory);
#else
   bfrsz = 0;
#endif
   nbfrs = req0.count;
   if( nbfrs <= 0 ) exit(1);

   last_bfr = -1;

   bfrs = malloc(nbfrs*sizeof(*bfrs));
   for( i=0; i<nbfrs; ++i ) {
      memset(&bfr0,0,sizeof(bfr0));
      bfr0.type = req0.type;
      bfr0.index = i;
      IOCTL(fd, VIDIOC_QUERYBUF, &bfr0);
#ifdef REV2
      offset = bfr0.m.offset;
#else
      offset = bfr0.offset;
#endif
      bfrs[i].offset = offset;
#ifdef LINUX_2_6
      bfrs[i].length = bfr0.length; /* remember for munmap() */
      bfrs[i].start = mmap (NULL, bfr0.length, PROT_READ | PROT_WRITE, /* required */
                                   MAP_SHARED, fd, offset);
      if( bfrs[i].start == MAP_FAILED ) {
         perror ("mmap");  exit(1);
      }
#else
      bfrs[i].length = bfr0.length; /* remember for munmap() */
      bfrs[i].start = (void *)offset;
      bsz = offset+bfr0.length;
      if( bfrsz < bsz ) bfrsz = bsz;
#endif
      if( last_bfr >= 0 ) {
         bfr0.index = last_bfr;
         bfr0.type = req0.type;
         IOCTL(fd,VIDIOC_QBUF,&bfr0);
      }
      last_bfr = i;
   }

   if( nbfrs == 1 ) {
      bfr0.index = 0;
      bfr0.type = req0.type;
      IOCTL(fd, VIDIOC_QUERYBUF, &bfr0);
      bfr0.index = 0;
      bfr0.type = req0.type;
      IOCTL(fd,VIDIOC_QBUF,&bfr0);
   }

#ifndef LINUX_2_6
   vidbfr = (unsigned long)mmap (NULL, bfrsz, PROT_READ | PROT_WRITE, /* required */
                                 MAP_SHARED, fd, 0);
   if( (void*)vidbfr == MAP_FAILED ) {
      perror ("mmap");  exit(1);
   }

   printf(" vidbfr = %08lx\n",(unsigned long)vidbfr);

   for( i=0; i<nbfrs; ++i ) {
      bfrs[i].start += vidbfr;
      printf(" bfr[%d] = %08lx\n",i,(unsigned long)bfrs[i].start);
   }
#endif
   getc(stdin);

   flag = 1;
   IOCTLF(fd,VIDIOC_STREAMON,&flag);
   flag = 1;
#ifdef REV2
   IOCTLF(fd,VIDIOC_OVERLAY,&flag);
#else
   IOCTLF(fd,VIDIOC_PREVIEW,&flag);
#endif
/* REQBUFS */
#endif
   memset(&fbfr0,0,sizeof(fbfr0));
   IOCTLF(fd,VIDIOC_G_FBUF,&fbfr0);
   ovly_phys_addr = *(unsigned long *)&fbfr0.base;

   i = n = zn = 0;
   signal(SIGINT,sigint);
   while( done == 0 || zn < nbfrz ) {
#ifndef REQBUFS
      do { n = read(fd,bfr,BFRSZ); } while( n < 0 && errno == EAGAIN );
      printf(" read - %d\n",n);
      if( done != 0 )
         memcpy(bfrz[zn++],bfr,fmt0.fmt.pix.sizeimage);
#else
      do { n = IOCTL(fd,VIDIOC_DQBUF,&bfr0); } while( n < 0 && errno == EAGAIN );
      if( n < 0 && last_error != EINTR ) {
         perror("VIDIOC_DQBUF"); exit(1);
      }
      if( done != 0 && zn >= nbfrz ) break;
      i = bfr0.index;
      printf(" dqbuf - %d\n",i);

      if( done != 0 ) {
         printf("grab %d %08lx\n",zn,*(unsigned long *)bfrs[i].start);
         memcpy(bfrz[zn++],bfrs[i].start,fmt1.fmt.pix.sizeimage);
      }
      fbfr0.fmt.pixelformat = fmt1.fmt.pix.pixelformat;
      //*(void**)&fbfr0.base = (void*)((unsigned long)vbfr0.base + bfrs[i].offset);
      *(void**)&fbfr0.base = (void*)(ovly_phys_addr + bfrs[i].offset);
      printf(" base = %08lx\n",*(void**)&fbfr0.base);
      IOCTLF(fd,VIDIOC_S_FBUF,&fbfr0);

      bfr0.index = last_bfr;
#ifndef REV2
      bfr0.type = V4L2_BUF_TYPE_CAPTURE;
#else
      bfr0.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
#endif

      IOCTLF(fd,VIDIOC_QBUF,&bfr0);
      last_bfr = i;
#endif
   }
   signal(SIGINT,SIG_DFL);

   flag = 1;
   IOCTLF(fd,VIDIOC_STREAMOFF,&flag);

   if( ac > 1 ) {
      for( i=0; i<nbfrz; ++i ) {
         sprintf(&fn[0],"%s%02d",av[1],i);
         printf(" write - %s, %dx%d\n",&fn[0],fmt1.fmt.pix.width,fmt1.fmt.pix.height);
         writepict(&fn[0],bfrz[i],fmt1.fmt.pix.width,fmt1.fmt.pix.height);
         free(bfrz[i]);
      }
   }

#ifdef REQBUFS
   flag = 0;
#ifdef REV2
   IOCTLF(fd,VIDIOC_OVERLAY,&flag);
#else
   IOCTLF(fd,VIDIOC_PREVIEW,&flag);
#endif

#ifdef LINUX_2_6
   for( i=0; i<nbfrs; ++i )
      munmap(bfrs[i].start,bfrs[i].length);
#else
   munmap((void *)vidbfr,bfrsz);
#endif

   free(bfrs);
#endif

   close(fd);
   return 0;
}


