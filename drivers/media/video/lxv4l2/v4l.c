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
#include "v4l.h"
#include <linux/autoconf.h>
#if defined(MODULE) && defined(CONFIG_MODVERSIONS)
#ifndef MODVERSIONS
#define MODVERSIONS
#endif
#ifdef LINUX_2_6
#include <config/modversions.h>
#else
#include <linux/modversions.h>
#endif
#endif
#include <linux/module.h>
#include <linux/wait.h>
#include <linux/fs.h>
#include <linux/proc_fs.h>
#include <linux/init.h>
#include <linux/pm.h>
#include <linux/delay.h>

#include "ov7670.h"
/* LX3 video for linux driver
 *  This driver implements much of the v4l and v4l2 video device interface.
 *  It is an understatement to say that there are many situations
 *   where it was not clear what the correct thing to do was.  Hopefully
 *   it works for your application.  It appears to me that the principle
 *   v4l design was to capture overlay data directly into the active graphics.
 *   While direct capture is possible, it is not the best way to capture
 *   and overlay data, and is not how this hardware was designed to work.
 *   Since there are old applications which want this style of design,
 *   it has been included... but it creates a context sensitive interface.
 *   That is to say, if you run certain functions first - other functions
 *   may behave differently.  This is most apparent with S_FBUF.
 *  It is possible to allocate capture buffers and pass them directly to
 *   the overlay queue.  This can be used to display capture data directly.
 *   Note that the interrupt handler will not pass capture buffers until
 *   new buffers are avaiable.  This is bacause if no buffers are avaiable
 *   when it is time to cycle capture buffers, it either has to stop the
 *   capture or rewrite the current buffers.  Since these buffers are active
 *   until new buffers are available, they cant be handed to the user  This
 *   means that there must be enough video memory to allocate at least two
 *   sets of buffers to run a capture.  to capture and overlay - it takes
 *   at least three sets.  One as the current active capture, one for the
 *   next latched capture buffer, and one for the active overlay buffer.
 *  autoflipping operation (capture data is automatically overlayed without
 *   user intervention) is selected if no buffers have been allocated, the
 *   frame buffer capture base address and the frame buffer overlay base address
 *   have not been set - and the overlay is started.  In this case, a buffer
 *   pool is built and the overlay format/colorspace is set to the capture
 *   format/colorspace.  The buffer type is set to io_flipped, which causes
 *   interrupts to cycle the buffers automatically instead of queuing them.
 *  This driver implements sequential io to both capture and overlay.  For
 *   overlay stream data, timestamps are invented and applied to match the
 *   data rate selected by ovly num/denom.  Both capture and overlay data
 *   rates can be set/varied with S_STD and S_PARM.  Note that both
 *   capt/ovly max_bfrs can be overridden with S_PARM.  Capture buffers
 *   can be downsampled by setting a max rate utilizing S_PARM.
 *
 * The module parameters:
 *  capt_max_bfrs - capture max buffers allowed
 *  ovly_max_bfrs - overlay max buffers allowed
 *  capt_formats  - capture enabled formats bit mask
 *  ovly_formats  - overlay enabled formats bit mask
 *  irq           - interrupt to use
 *  debug         - debug level 0-9
 *  plain_sensor  - 7670 for ov7670 sensor
 */

/* if to handle power management events */
/* #define PM_EVENTS */

//not available in 2.6.29
//static int I2C_DECODER_ID = I2C_DRIVERID_TVMIXER;
static int I2C_DECODER_ID = I2C_DRIVERID_TVAUDIO;

static int I2C_TUNER_ID   = I2C_DRIVERID_TUNER;
#ifdef TDA9851
static int I2C_AUDIO_ID   = I2C_DRIVERID_TVAUDIO;
#else
static int I2C_AUDIO_ID   = I2C_DRIVERID_MSP3400;
#endif
static int I2C_OV7670_ID  = I2C_DRIVERID_OV7670;

static int capt_max_bfrs = -1;  /* capture max buffers */
static int ovly_max_bfrs = -1;  /* overlay max buffers */
static int capt_formats  = ~0;  /* capture enabled formats mask */
static int ovly_formats  = ~0;  /* overlay enabled formats mask */
static int plain_sensor  = 0;   /* using a plain I2C sensor */

/* dev has at least one, user has at least one */
#define V4L_MIN_BFRS 2

static struct s_v4l_std_data {
   unsigned long id;  /* v4l2 std id */
   char *name;        /* std name */
   int num;           /* frame period numerator */
   int denom;         /* frame period denominator */
   int lines;         /* total frame lines */
} v4l_std_data[STD_IDS_SZ] = {
   [STD_SD_NTSC]  = { V4L2_STD_NTSC,   "SD_NTSC", 1001, 30000,  525 },
   [STD_SD_PAL]   = { V4L2_STD_PAL,    "ST_PAL",     1,    25,  625 },
   [STD_HD_480P]  = { V4L2_STD_UNK101, "HD_480",  1001, 60000,  625 },
   [STD_HD_525P]  = { V4L2_STD_UNK102, "HD_525P", 1001, 60000,  525 },
   [STD_HD_720P]  = { V4L2_STD_UNK103, "HD_720P", 1001, 60000,  750 },
   [STD_HD_1080I] = { V4L2_STD_UNK104, "HD_1080I",   1,    60, 1125 },
   [STD_HD_VGA]   = { V4L2_STD_UNK105, "HD_VGA",     1,    30,  625 },
   [STD_HD_QVGA]  = { V4L2_STD_UNK106, "HD_QVGA",    1,    30,  625 },
};

V4LDevice *lx_dev = NULL;

/** v4l_name - driver name
 *  returns device name
 */
char *
v4l_name(void)
{
   return &lx_dev->name[0];
}

#ifdef REV2
/** v4l_dev_release - decr refcnt and release if zero
 *   free V4LDevice on module deletion
 */
static void
v4l_dev_release(struct video_device *dev)
{
   V4LDevice *pp = video_get_drvdata(dev);
   if( pp != NULL && --pp->refcnt == 0 ) {
      lx_exit(pp);
      vid_mem_exit();
      kfree(pp);
   }
}
#endif

/** v4l_num_channels - valid input source count
 *  returns 1 if no decoder attached
 *  returns last enuminput index+1 if decoder attached (max 6)
 */
int
v4l_num_channels(void)
{
   int i, ret;
   if( lx_dev == 0 ) return 0;
   if( lx_dev->tv_decoder == NULL ) return 1;
   for( i=0; i<6; i++ ) {
      struct v4l2_input v_input;
      v_input.index = i;
      ret = v4l_component_ioctl(lx_dev->tv_decoder,VIDIOC_ENUMINPUT,&v_input);
      if( ret != 0 ) break;
   }
   return i;
}

/** v4l_num_audios - valid audio source count
 *  returns 0 if no audio codec attached
 *  reutrns 1 if audio codec attached
 */
int
v4l_num_audios(void)
{
   if( lx_dev == 0 || lx_dev->tv_audio == NULL ) return 0;
   return 1;
}

/** v4l_std_norm - return v4l norm
 *  return v4l norm given std_index if mapping exists
 */
int v4l_std_norm(int idx)
{
   int norm = 0;
   switch( idx ) {
   case STD_SD_NTSC:  norm = VIDEO_MODE_NTSC;  break;
   case STD_SD_PAL:   norm = VIDEO_MODE_PAL;   break;
   }
   return norm;
}

/** v4l_norm_std - return std id
 *  returns std_index given norm if mapping exists
 */
long long v4l_norm_std(int norm)
{
   long long std = 0;
   switch( norm ) {
   case VIDEO_MODE_NTSC:  std = V4L2_STD_NTSC;   break;
   case VIDEO_MODE_PAL:   std = V4L2_STD_PAL;    break;
   }
   return std;
}

/** v4l_standard_index - return std index
 *  return std_index given v4l2 std id
 */
int
v4l_standard_index(long long sid)
{
   int idx = -1;
   switch( sid ) {
   case V4L2_STD_NTSC:   idx = STD_SD_NTSC;  break;
   case V4L2_STD_PAL:    idx = STD_SD_PAL;   break;
   case V4L2_STD_UNK101: idx = STD_HD_480P;  break;
   case V4L2_STD_UNK102: idx = STD_HD_525P;  break;
   case V4L2_STD_UNK103: idx = STD_HD_720P;  break;
   case V4L2_STD_UNK104: idx = STD_HD_1080I; break;
   case V4L2_STD_UNK105: idx = STD_HD_VGA;   break;
   case V4L2_STD_UNK106: idx = STD_HD_QVGA;  break;
   }
   return idx;
}

/** v4l_standard_id - return std id
 *  return v4l2 std_id given std_index
 */
long long
v4l_standard_id(int idx)
{
   long long sid = 0;
   switch( idx ) {
   case STD_SD_NTSC:  sid = V4L2_STD_NTSC;    break;
   case STD_SD_PAL:   sid = V4L2_STD_PAL;     break;
   case STD_HD_480P:  sid = V4L2_STD_UNK101;  break;
   case STD_HD_525P:  sid = V4L2_STD_UNK102;  break;
   case STD_HD_720P:  sid = V4L2_STD_UNK103;  break;
   case STD_HD_1080I: sid = V4L2_STD_UNK104;  break;
   case STD_HD_VGA:   sid = V4L2_STD_UNK105;  break;
   case STD_HD_QVGA:  sid = V4L2_STD_UNK106;  break;
   }
   return sid;
}

/** v4l_pixelformat_to_palette - translate to palette
 *  return v4l palette given v4l2 pixelformat
 */
int
v4l_pixelformat_to_palette(int pixelformat)
{
   int palette = 0;
   switch( pixelformat ) {
   case V4L2_PIX_FMT_GREY:    palette = VIDEO_PALETTE_GREY;    break;
   case V4L2_PIX_FMT_RGB555:  palette = VIDEO_PALETTE_RGB555;  break;
   case V4L2_PIX_FMT_RGB565:  palette = VIDEO_PALETTE_RGB565;  break;
   case V4L2_PIX_FMT_BGR24:   palette = VIDEO_PALETTE_RGB24;   break;
   case V4L2_PIX_FMT_BGR32:   palette = VIDEO_PALETTE_RGB32;   break;
   case V4L2_PIX_FMT_YUYV:    palette = VIDEO_PALETTE_YUYV;    break;
   case V4L2_PIX_FMT_UYVY:    palette = VIDEO_PALETTE_UYVY;    break;
   case V4L2_PIX_FMT_YUV420:  palette = VIDEO_PALETTE_YUV420P; break;
   case V4L2_PIX_FMT_YUV422P: palette = VIDEO_PALETTE_YUV422P; break;
   case V4L2_PIX_FMT_YUV411P: palette = VIDEO_PALETTE_YUV411P; break;
   }
   return palette;
}

/** v4l_palette_to_pixelformat - translate to pixelformat
 *  return v4l2 pixelformat given v4l palette
 */
int
v4l_palette_to_pixelformat(int palette)
{
   int pixelformat = 0;
   switch( palette ) {
   case VIDEO_PALETTE_GREY:    pixelformat = V4L2_PIX_FMT_GREY;    break;
   case VIDEO_PALETTE_RGB555:  pixelformat = V4L2_PIX_FMT_RGB555;  break;
   case VIDEO_PALETTE_RGB565:  pixelformat = V4L2_PIX_FMT_RGB565;  break;
   case VIDEO_PALETTE_RGB24:   pixelformat = V4L2_PIX_FMT_BGR24;   break;
   case VIDEO_PALETTE_RGB32:   pixelformat = V4L2_PIX_FMT_BGR32;   break;
   case VIDEO_PALETTE_YUYV:
   case VIDEO_PALETTE_YUV422:  pixelformat = V4L2_PIX_FMT_YUYV;    break;
   case VIDEO_PALETTE_UYVY:    pixelformat = V4L2_PIX_FMT_UYVY;    break;
   case VIDEO_PALETTE_YUV420P: pixelformat = V4L2_PIX_FMT_YUV420;  break;
   case VIDEO_PALETTE_YUV422P: pixelformat = V4L2_PIX_FMT_YUV422P; break;
   case VIDEO_PALETTE_YUV411:
   case VIDEO_PALETTE_YUV411P: pixelformat = V4L2_PIX_FMT_YUV411P; break;
   }
   return pixelformat;
}

/** v4l_fmt_is_planar - test format planar
 *  return 1 if pixelformat is planar
 */
int
v4l_fmt_is_planar(unsigned int fmt)
{
   int is_planar = 0;
   switch( fmt ) {
   case V4L2_PIX_FMT_RGB565:
   case V4L2_PIX_FMT_YUYV:
   case V4L2_PIX_FMT_UYVY:
      break;
   case V4L2_PIX_FMT_YVU420:
   case V4L2_PIX_FMT_YUV420:
   case V4L2_PIX_FMT_YUV422P:
      is_planar = 1;
      break;
   default:
      return -1;
   }
   return is_planar;
}

/** v4l_fmt_is_420 - test format 420
 *  return 1 if pixelformat is 420 downsampled
 */
int v4l_fmt_is_420(int fmt)
{
   int is_420 = 0;
   switch( fmt ) {
   case V4L2_PIX_FMT_RGB565:
   case V4L2_PIX_FMT_YUYV:
   case V4L2_PIX_FMT_UYVY:
   case V4L2_PIX_FMT_YUV422P:
      break;
   case V4L2_PIX_FMT_YVU420:
   case V4L2_PIX_FMT_YUV420:
      is_420 = 1;
      break;
   default:
      return -1;
   }
   return is_420;
}

/** v4l_fmt_is_laced - test format laced
 *  return 1 if pixelformat implies interlaced input
 */
int v4l_fld_is_laced(int field)
{
   int is_laced = 0;
   switch( field ) {
   case V4L2_FIELD_SEQ_TB:         /* both fields sequential into one buffer, top-bottom order */
   case V4L2_FIELD_SEQ_BT:         /* same as above + bottom-top order */
   case V4L2_FIELD_INTERLACED:     /* both fields interlaced */
   case V4L2_FIELD_TOP:            /* top field only */
   case V4L2_FIELD_BOTTOM:         /* bottom field only */
   case V4L2_FIELD_ALTERNATE:      /* both fields alternating into separate buffers */
      is_laced = 1;
      break;
   case V4L2_FIELD_NONE:           /* counts as progressive */
      break;
   default:
      return -1;
   }
   return is_laced;
}

/** v4l_flds_per_bfr - return field count
 *  return 2 if two fields present for given pixelformat
 *  return 1 if one fields present for given pixelformat
 *  return 0 if progressive (not interlaced)
 */
int v4l_flds_per_bfr(int field)
{
   int n = 0;
   switch( field ) {
   case V4L2_FIELD_SEQ_TB:         /* both fields sequential into one buffer, top-bottom order */
   case V4L2_FIELD_SEQ_BT:         /* same as above + bottom-top order */
   case V4L2_FIELD_INTERLACED:     /* both fields interlaced */
      n = 2;
      break;
   case V4L2_FIELD_TOP:            /* top field only */
   case V4L2_FIELD_BOTTOM:         /* bottom field only */
   case V4L2_FIELD_ALTERNATE:      /* both fields alternating into separate buffers */
      n = 1;
      break;
   case V4L2_FIELD_NONE:           /* counts as progressive */
      break;
   default:
      return -1;
   }
   return n;
}

/** v4l_vid_ypitch - return y plane pitch
 *  returns y linesize for all formats
 *    note: if not planar format, then y plane lines contain uv data
 */
int v4l_vid_ypitch(unsigned int pixelformat,int width)
{
   int y_pitch;
   switch( pixelformat ) {
   case V4L2_PIX_FMT_RGB565:
      y_pitch = width*2;
      break;
   case V4L2_PIX_FMT_YUYV:
   case V4L2_PIX_FMT_UYVY:
      y_pitch = ((width+1)/2)*4;
      break;
   case V4L2_PIX_FMT_YVU420:
   case V4L2_PIX_FMT_YUV420:
   case V4L2_PIX_FMT_YUV422P:
      y_pitch = ((width+1)/2)*2;
      break;
   default:
      return -1;
   }
   return y_pitch;
}

/** v4l_vid_uvpitch - return u/v plane pitch
 *  returns uv linesize for all formats
 *    note: if not planar format, then returns zero
 */
int v4l_vid_uvpitch(unsigned int pixelformat,int width)
{
   int uv_pitch;
   switch( pixelformat ) {
   case V4L2_PIX_FMT_RGB565:
   case V4L2_PIX_FMT_YUYV:
   case V4L2_PIX_FMT_UYVY:
      uv_pitch = 0;
      break;
   case V4L2_PIX_FMT_YVU420:
   case V4L2_PIX_FMT_YUV420:
   case V4L2_PIX_FMT_YUV422P:
      uv_pitch = (width+1)/2;
      break;
   default:
      return -1;
   }
   return uv_pitch;
}

/** v4l_vid_byte_depth - return y/u/v plane depth
 *  returns byte depth approx (y_imgsz+2*uv_imgsz)/(width*height)
 */
int v4l_vid_byte_depth(unsigned int pixelformat)
{
   int depth;
   switch( pixelformat ) {
   case V4L2_PIX_FMT_RGB565:
   case V4L2_PIX_FMT_YUYV:
   case V4L2_PIX_FMT_UYVY:
   case V4L2_PIX_FMT_YUV422P:
      depth = 4;
      break;
   case V4L2_PIX_FMT_YVU420:
   case V4L2_PIX_FMT_YUV420:
      depth = 3;
      break;
   default:
      return -1;
   }
   return depth;
}

/* v4l_vid_colorspace_def - return colorspace or default
 * returns given colorspace if known, or default if not
 */
int v4l_vid_colorspace_def(int colorspace,int def)
{
   if( colorspace == 0 )
      colorspace = def;
   switch( colorspace ) {          /* enum v4l2_colorspace */
   case V4L2_COLORSPACE_SMPTE170M: /* ITU-R 601 -- broadcast NTSC/PAL */
   case V4L2_COLORSPACE_REC709:    /* hdtv */
      break;
   default:
      colorspace = V4L2_COLORSPACE_SMPTE170M;
      break;
   }
   return colorspace;
}

/* v4l_vid_field_def - return field or default
 * returns given field if known, or default if not
 */
int v4l_vid_field_def(int field,int def)
{
   if( field == V4L2_FIELD_ANY )
      field = def;
   switch( field ) {           /* enum v4l2_field */
   case V4L2_FIELD_NONE:       /* counts for progressive */
   case V4L2_FIELD_TOP:        /* top field only */
   case V4L2_FIELD_BOTTOM:     /* bottom field only */
   case V4L2_FIELD_INTERLACED: /* both fields interlaced */
   case V4L2_FIELD_SEQ_TB:     /* both fields sequential into one buffer, top-bottom order */
   case V4L2_FIELD_SEQ_BT:     /* same as above + bottom-top order */
   case V4L2_FIELD_ALTERNATE:  /* both fields alternating into separate buffers */
      break;
   default:
      field = V4L2_FIELD_INTERLACED;
      break;
   }
   return field;
}

#ifdef REV2
/* v4l_vid_set_field - set rev2 field */
void v4l_vid_set_field(struct v4l2_pix_format *fmt,int field)
{
   fmt->field = field;
}

/* v4l_vid_set_colorspace - set rev2 colorspace */
void v4l_vid_set_colorspace(struct v4l2_pix_format *fmt,int colorspace)
{
   fmt->colorspace = colorspace;
}

/* v4l_vid_field - return rev2 field */
int v4l_vid_field(struct v4l2_pix_format *fmt)
{
   return fmt->field;
}

/* v4l_vid_colorspace - return rev2 colorspace */
int v4l_vid_colorspace(struct v4l2_pix_format *fmt)
{
   return fmt->colorspace;
}
#else
/* v4l_vid_set_field - set rev1 field */
void v4l_vid_set_field(struct v4l2_pix_format *fmt,int field)
{
   int flags = 0;
   switch( field ) {
   case V4L2_FIELD_NONE:
      break;
   case V4L2_FIELD_TOP:
      flags |= V4L2_FMT_FLAG_TOPFIELD;
      break;
   case V4L2_FIELD_BOTTOM:
      flags |= V4L2_FMT_FLAG_BOTFIELD;
      break;
   case V4L2_FIELD_INTERLACED:
      flags |= V4L2_FMT_FLAG_INTERLACED;
      break;
   case V4L2_FIELD_SEQ_TB:
      flags |= V4L2_FMT_FLAG_INTERLACED|V4L2_FMT_FLAG_TOPFIELD;
      break;
   case V4L2_FIELD_SEQ_BT:
      flags |= V4L2_FMT_FLAG_INTERLACED|V4L2_FMT_FLAG_BOTFIELD;
      break;
   case V4L2_FIELD_ALTERNATE:
      flags |= V4L2_FMT_FLAG_TOPFIELD|V4L2_FMT_FLAG_BOTFIELD;
      break;
   }
   fmt->flags &= ~V4L2_FMT_FLAG_FIELD_field;
   fmt->flags |= flags;
}

/* v4l_vid_set_colorspace - set rev1 colorspace */
void v4l_vid_set_colorspace(struct v4l2_pix_format *fmt,int colorspace)
{
   int flags = 0;
   switch( colorspace ) {          /* enum v4l2_colorspace */
   case V4L2_COLORSPACE_SMPTE170M: /* ITU-R 601 -- broadcast NTSC/PAL */
      flags |= V4L2_FMT_CS_601YUV;
      break;
   case V4L2_COLORSPACE_REC709:    /* hdtv */
      break;
   }
   fmt->flags &= ~V4L2_FMT_CS_field;
   fmt->flags |= flags;
}

/* v4l_vid_field - return rev1 field */
int v4l_vid_field(struct v4l2_pix_format *fmt)
{
   int flags = (fmt->flags & V4L2_FMT_FLAG_FIELD_field);
   int field = V4L2_FIELD_ANY;
   switch( flags ) {
   case V4L2_FMT_FLAG_NOT_INTERLACED:
      field = V4L2_FIELD_NONE;
      break;
   case V4L2_FMT_FLAG_TOPFIELD:
      field = V4L2_FIELD_TOP;
      break;
   case V4L2_FMT_FLAG_BOTFIELD:
      field = V4L2_FIELD_BOTTOM;
      break;
   case V4L2_FMT_FLAG_INTERLACED|V4L2_FMT_FLAG_TOPFIELD|V4L2_FMT_FLAG_BOTFIELD:
   case V4L2_FMT_FLAG_INTERLACED:
      field = V4L2_FIELD_INTERLACED;
      break;
   case V4L2_FMT_FLAG_INTERLACED|V4L2_FMT_FLAG_TOPFIELD:
      field = V4L2_FIELD_SEQ_TB;
      break;
   case V4L2_FMT_FLAG_INTERLACED|V4L2_FMT_FLAG_BOTFIELD:
      field = V4L2_FIELD_SEQ_BT;
      break;
   case V4L2_FMT_FLAG_TOPFIELD|V4L2_FMT_FLAG_BOTFIELD:
      field = V4L2_FIELD_ALTERNATE;
      break;
   }
   return field;
}

/* v4l_vid_colorspace - return rev1 colorspace */
int v4l_vid_colorspace(struct v4l2_pix_format *fmt)
{
   int flags = fmt->flags;
   int colorspace;
   if( (flags&V4L2_FMT_CS_601YUV) != 0 )
      colorspace = V4L2_COLORSPACE_SMPTE170M;
   else
      colorspace = V4L2_COLORSPACE_REC709;
   return colorspace;
}
#endif

/* v4l_capt_bfr_limit - return max_bfrs based on field def */
int
v4l_capt_bfr_limit(VidDevice *dp)
{
   int limit = dp->capt_max_bfrs;
   int field = v4l_vid_field(&dp->capt);
   field = v4l_vid_field_def(field,dp->capt_dft_field);
   if( v4l_fld_is_laced(field) != 0 && v4l_flds_per_bfr(field) == 1 )
      limit *= 2;
   return limit;
}
/* v4l_ovly_bfr_limit - return max_bfrs based on field def */
int
v4l_ovly_bfr_limit(VidDevice *dp)
{
   int limit = dp->ovly_max_bfrs;
   int field = v4l_vid_field(&dp->capt);
   field = v4l_vid_field_def(field,dp->capt_dft_field);
#ifdef REV2
   field = v4l_vid_field_def(dp->ovly.field,field);
#endif
   if( v4l_fld_is_laced(field) != 0 && v4l_flds_per_bfr(field) == 1 )
      limit *= 2;
   return limit;
}

/* v4l_set_input - select active input source
 * return zero if decoder input source selectable
 *   or validate input zero selection
 */
int
v4l_set_input(VidDevice *dp, int input)
{
   int last, ret;
   struct i2c_client *dcdr;
   if( (dcdr=lx_dev->tv_decoder) != NULL ) {
      if( (ret=v4l_component_ioctl(dcdr,VIDIOC_G_INPUT,&last)) == 0 ) return ret;
      if( last == input ) return 0;
      if( (ret=v4l_component_ioctl(dcdr,VIDIOC_S_INPUT,&input)) != 0 ) return ret;
   }
   else if( input != 0 ) {
      DMSG(1,"v4l_set_input: no decoder, only input 0\n");
      return -EINVAL;
   }
   return 0;
}

/* v4l_set_std_index - select active standard
 * return zero if decoder standard selectable
 *   update rate,port and field device data
 */
int
v4l_set_std_index(VidDevice *dp, int idx)
{
   int ret, field;
   struct i2c_client *dcdr;
   struct s_v4l_std_data *sp;
   if( idx < 0 || idx >= STD_IDS_SZ ) return -EINVAL;
   if( (dcdr=lx_dev->tv_decoder) != NULL ) {
#ifdef REV2
      long long sid;
      if( (ret=v4l_component_ioctl(dcdr,VIDIOC_G_STD,&sid)) != 0 ) return ret;
      if( v4l_standard_index(sid) == idx ) return 0;
      sid = v4l_standard_id(idx);
      if( (ret=v4l_component_ioctl(dcdr,VIDIOC_S_STD,&sid)) != 0 ) return ret;
#else
      struct v4l2_standard cstd, nstd;
      if( (ret=v4l_component_ioctl(dcdr,VIDIOC_G_STD,&cstd)) != 0 ) return ret;
      if( (ret=v4l_standard(idx,&nstd)) != 0 ) return ret;
      if( strncmp(&cstd.name[0],&nstd.name[0],sizeof(cstd.name)) == 0 ) return 0;
      if( (ret=v4l_component_ioctl(dcdr,VIDIOC_S_STD,&nstd)) != 0 ) return ret;
#endif
   }
   dp->std_index = idx;
   sp = &v4l_std_data[idx];
   dp->std_num = sp->num;
   dp->std_denom = sp->denom;
   dp->capt_num = dp->std_num * HZ;
   dp->capt_denom = dp->std_denom;
   dp->ovly_num = dp->std_num * HZ;
   dp->ovly_denom = dp->std_denom;
   dp->capt_progressive = lx_capt_std_progressive(dp,idx);
   dp->capt_vip_version = lx_capt_std_vip_version(dp,idx);
   dp->capt_port_size = lx_capt_std_port_size(dp,idx);
   field = dp->capt_progressive != 0 ?  V4L2_FIELD_NONE : V4L2_FIELD_INTERLACED;
   dp->capt_dft_field = field;
   v4l_vid_set_field(&dp->capt,field);
#ifdef REV2
   dp->ovly.field = field;
#endif
   return 0;
}

/* v4l_set_norm - select active standard (by norm) */
int
v4l_set_norm(VidDevice *dp, int norm)
{
   long long std = v4l_norm_std(norm);
   return v4l_set_std_index(dp,v4l_standard_index(std));
}

/* v4l_get_control - return control value
 *  returns control value from either decoder
 *   or builtin controls (via gamma ram)
 */
int
v4l_get_control(VidDevice *dp,int id,int *value)
{
   int val = 0;
   int ret = 0;
   if( lx_dev->tv_decoder == NULL ) {
      switch( id ) {
      case V4L2_CID_BRIGHTNESS:
         val = dp->ovly_brightness;
         break;
      case V4L2_CID_CONTRAST:
         val = dp->ovly_contrast;
         break;
      case V4L2_CID_BLACK_LEVEL:
         val = dp->ovly_blackLvl;
         break;
      case V4L2_CID_RED_BALANCE:
         val = dp->ovly_redGain;
         break;
      case V4L2_CID_BLUE_BALANCE:
         val = dp->ovly_blueGain;
         break;
      default:
         ret = -EINVAL;
         break;
      }
   }
   else {
      struct v4l2_control ctl;
      ctl.id = id;
      ret = v4l_component_ioctl(lx_dev->tv_decoder,VIDIOC_G_CTRL,&ctl);
      if( ret == 0 ) val = ctl.value;
   }
   if( ret == 0 )
      *value = val;
   return ret;
}

/* v4l_set_control - apply control value
 *  sets control value either with decoder
 *   or builtin controls (via gamma ram)
 */
int
v4l_set_control(VidDevice *dp,int id,int value)
{
   int ret = 0;
   if( lx_dev->tv_decoder == NULL ) {
      switch( id ) {
      case V4L2_CID_BRIGHTNESS:
         dp->ovly_brightness = value;
         break;
      case V4L2_CID_CONTRAST:
         dp->ovly_contrast = value;
         break;
      case V4L2_CID_BLACK_LEVEL:
         dp->ovly_blackLvl = value;
         break;
      case V4L2_CID_RED_BALANCE:
         dp->ovly_redGain = value;
         break;
      case V4L2_CID_BLUE_BALANCE:
         dp->ovly_blueGain = value;
         break;
      default:
         ret = -EINVAL;
         break;
      }
      if( ret == 0 )
         lx_ovly_palette(dp);
   }
   else {
      struct v4l2_control ctl;
      ctl.id = id;
      ctl.value = value;
      ret = v4l_component_ioctl(lx_dev->tv_decoder,VIDIOC_S_CTRL,&ctl);
   }
   return ret;
}

/** v4l_qbfr - insert buffer into queue
 * add a buffer to the end of a specified queue.
 *  if queuing capture buffer, unstall if stalled waiting on buffer
 */
void
v4l_qbfr(VidDevice *dp,struct list_head *lp,io_buf *bp,int capt)
{
   unsigned long flags;
   FilePriv *fp;
   io_queue *io;

   // Bri. tests to avoid panic !
   if (dp == NULL) return;
   fp = &dp->fp; //is !=NULL if dp!=NULL
   io = fp->io;
   if (io == NULL) return;
   if (bp == NULL) return;

   spin_lock_irqsave(&io->lock, flags);
   list_move_tail(&bp->bfrq,lp);
   bp->sequence = io->sequence++;
   bp->flags &= ~V4L2_BUF_FLAG_DONE;
   bp->flags |= V4L2_BUF_FLAG_QUEUED;
   if( capt != 0 && dp->capt_stalled != 0 )
      v4l_capt_unstall(dp);
   spin_unlock_irqrestore(&io->lock, flags);
}

/** v4l_qbfrs - insert all buffers into queue
 * move all buffers into a specified queue.
 */
void
v4l_qbfrs(VidDevice *dp,struct list_head *lp)
{
   int i;  io_buf *bp;
   FilePriv *fp;
   io_queue *io;

   // Bri. tests to avoid panic !
   if (dp == NULL) return;
   fp = &dp->fp; //is !=NULL if dp!=NULL
   io = fp->io;
   if (io == NULL) return;
   bp = &io->bfrs[0];
   if (bp == NULL) return;

   for( i=io->mbfrs; --i>=0; ++bp ) {
      v4l_qbfr(dp,lp,bp,0);
   }
}

/** v4l_dq1bfr - remove buffer from queue
 * remove a buffer from the front of a specified queue.
 *  return NULL if queue is empty.
 */
io_buf *
v4l_dq1bfr(VidDevice *dp,struct list_head *lp)
{
   unsigned long flags;
   FilePriv *fp;
   io_queue *io;
   io_buf *bp = NULL;

   // Bri. tests to avoid panic !
   if (dp == NULL) return NULL;
   fp = &dp->fp;
   io = fp->io;
   if (io == NULL) return NULL;

   spin_lock_irqsave(&io->lock, flags);
   if( ! list_empty(lp) ) {
      bp = list_entry(lp->next,io_buf,bfrq);
      list_del_init(&bp->bfrq);
   }
   spin_unlock_irqrestore(&io->lock,flags);
   return bp;
}

/** v4l_dqbfr - remove buffer from queue (or wait)
 * remove a buffer from the front of a specified queue.
 *  if the queue is empty, either wait if blocking, or
 *   return EAGAIN(=11) if nonblocking.  If interrupted while
 *   waiting blocked, return ERESTARTSYS(=512).
 */
int
v4l_dqbfr(VidDevice *dp,struct list_head *lp,io_buf **pbp,int nonblock,wait_queue_head_t *wait)
{
   io_buf *bp = NULL;
   int ret = 0;

   while( ret == 0 ) {
      if( (bp=v4l_dq1bfr(dp,lp)) != NULL )
        break;

      if( nonblock != 0 )
      {
      	ret = -EAGAIN;
      	break;
      }
      spin_unlock(&dp->lock);
#ifdef REV2
      ret = wait_event_interruptible(*wait,list_empty(lp)==0);
      DMSG(5,"v4l_dqbfr\n");
#else
      interruptible_sleep_on(wait);
      ret = signal_pending(current) != 0 ? -ERESTARTSYS : 0;
#endif
      spin_lock(&dp->lock);
   }

   *pbp = bp;
   return ret;
}

/** v4l_ovly_frame_jiffy - return next buffer expiration time
 *  return jiffy number of next overlay buffer based on
 *   its sequence number, and the current display rate.
 */
jiffiez_t
v4l_ovly_frame_jiffy(VidDevice *dp)
{
   int field;
   long long numer = dp->ovly_num;
   long denom = dp->ovly_denom;
   if( denom == 0 ) denom = 1;
#ifdef REV2
   field = dp->ovly.field;
#else
   /* no where else to look */
   field = v4l_vid_field(&dp->capt);
#endif
   if( lx_capt_std_progressive(dp,dp->std_index) == 0 &&
       (field == V4L2_FIELD_ALTERNATE || field == V4L2_FIELD_NONE) )
      denom *= 2;
   numer *= dp->ovly_jiffy_sequence;
   do_div(numer,denom);
   return dp->ovly_jiffy_start + numer;
}

/** v4l_stream_copy - transmit user data to/from video buffers
 *  move up to count bytes of data -to- (wr==0, read) or -from-
 *  (wr!=0, write) user space buffer bfr.  The data will be
 *  trasmitted to/from the next queued device buffer.  Which
 *  device depends on -wr-.  Overlay data is output, and capture
 *  data is input. If there are no waiting buffers and the io
 *  is not nonblocking, the user process is blocked.  If the io
 *  blocking and there are no buffers, the process is blocked
 *  until buffers are made available.  The data is handled as a
 *  continuous stream.  If the user buffer ends before the
 *  transfer is complete, the next call will resume from where
 *  the last transfer stopped.  Multiple device buffers may be
 *  as a result of one call.
 */
long v4l_stream_copy(VidDevice *dp,unsigned long imgsz, int wr,
   char *bfr, unsigned long count,int nonblock)
{
   long n, ret;
   io_buf *bp;
   char *cp;
   FilePriv *fp;
   io_queue *io;
   wait_queue_head_t *wait;
   struct list_head *qbuf, *dqbuf;

   //Bri. tests to avoid panic !
   if (dp == NULL) return -EINVAL;
   fp = &dp->fp; //is !=NULL if dp!=NULL
   io = fp->io;
   if (io == NULL) return -EINVAL;

   if( wr != 0 ) {
      qbuf = &io->wr_qbuf;
      dqbuf = &io->wr_dqbuf;
      wait = &dp->ovly_wait;
   }
   else {
      qbuf = &io->rd_qbuf;
      dqbuf = &io->rd_dqbuf;
      wait = &dp->capt_wait;
   }
   spin_lock(&dp->lock);
   ret = 0;
   while( count > 0 ) {
      if( (bp=io->stream_bfr) == NULL ) {
         n = v4l_dqbfr(dp,dqbuf,&bp,nonblock,wait);
         if( n != 0 ) {
            if( ret == 0 ) ret = n;
            break;
         }
         io->stream_bfr = bp;
         io->offset = 0;
      }
      cp = bp->bfrp + io->offset;
      n = imgsz - io->offset;
      if( count < n ) n = count;
      DMSG(5,"jiffies_start %u\n", (unsigned)jiffies);
      if( wr != 0 ) {
         if( copy_from_user(cp,(void __user *)bfr,n) ) {
            DMSG(1,"SEGV during seq write %p<=%p %ld\n",cp,bfr,n);
            if( ret == 0 ) ret = -EFAULT;
            break;
         }
      }
      else {

	#ifdef AL_PERF_CALCULATION
	unsigned t1, t2;
	t1 = (unsigned)jiffies;
    #endif //AL_PERF_CALCULATION

#ifdef FASTUSERCOPY
         if (access_ok(VERIFY_WRITE, bfr, n))
            memcpy(bfr,cp,n);
         else
            ret = -EFAULT;
#else
         long remaining;
         if( (remaining = copy_to_user((void __user *)bfr,cp,n)) > 0 ) {
            DMSG(1,"SEGV during seq read %p<=%p %ld %ld\n",bfr,cp,n,remaining);
            if( ret == 0 ) ret = -EFAULT;
            break;
         }
#endif
    #ifdef AL_PERF_CALCULATION
	t2 = (unsigned)jiffies;
	DMSG(1,"copy_to_user tick: %u HZ:%d n:%ld\n", t2-t1, HZ, n);
    #endif //AL_PERF_CALCULATION

      }
      DMSG(5,"jiffies_stop  %u\n", (unsigned)jiffies);
      io->offset += n;
      count -= n;
      bfr += n;
      ret += n;
      if( io->offset >= imgsz ) {
         if( wr != 0 )
            bp->jiffies = v4l_ovly_frame_jiffy(dp);
         v4l_qbfr(dp,qbuf,bp,wr==0);
         io->stream_bfr = NULL;
         break;
      }
   }

   spin_unlock(&dp->lock);
   return ret;
}

/* v4l_set_capt_time - update buffer timestamp
 *  set buffer timestamp to time based on jiffy offset
 */
void v4l_set_capt_time(VidDevice *dp,struct v4l2_buffer *bfr,
   struct timeval *start,jiffiez_t jiffy_offset)
{
   struct timespec ts;
   unsigned long secs, usecs;
   jiffies_to_timespec(jiffy_offset,&ts);
   usecs = start->tv_usec + ts.tv_nsec/1000;
   secs = start->tv_sec + ts.tv_sec + (usecs/1000000);
   usecs %= 1000000;
#ifdef REV2
   bfr->timestamp.tv_sec = secs;
   bfr->timestamp.tv_usec = usecs;
#else
   bfr->timestamp = (secs*1000000000ll) + usecs*10 + ((ts.tv_nsec%1000)/100);
#endif
}

/* v4l_set_capt_timecode - update buffer timestamp
 *  set buffer timecode to time based on jiffy offset
 *  type flags are set based on current capture std
 */
void v4l_set_capt_timecode(VidDevice *dp,struct v4l2_timecode *tc,
   jiffiez_t jiffy_offset)
{
   int type, rate;
   unsigned long long jo;
   rate = lx_capt_max_framerate(dp,dp->std_index);
   switch( rate ) {
   case 24:  type = V4L2_TC_TYPE_24FPS;  break;
   case 25:  type = V4L2_TC_TYPE_25FPS;  break;
   case 30:  type = V4L2_TC_TYPE_30FPS;  break;
   case 50:  type = V4L2_TC_TYPE_50FPS;  break;
   case 60:  type = V4L2_TC_TYPE_60FPS;  break;
   default:  type = 0;                   break;
   }
   tc->type = type | V4L2_TC_FLAG_COLORFRAME;
   jo = jiffy_offset;
   tc->frames = (do_div(jo,HZ) * rate) / HZ;
   tc->seconds = do_div(jo,60);
   tc->minutes = do_div(jo,60);
   tc->hours = jo;
}

/** v4l_get_ovly_time - determine overlay presentation time jiffy
 *  return system jiffy for presentation of given buffer (vio timestamp)
 */
jiffiez_t
v4l_get_ovly_time(VidDevice *dp,struct v4l2_buffer *bfr,
   struct timeval *start,jiffiez_t jiffy_offset)
{
   struct timespec ts;
   unsigned long secs, usecs, borrow;
#ifdef REV2
   secs = bfr->timestamp.tv_sec;
   usecs = bfr->timestamp.tv_usec;
#else
   { long long ts = bfr->timestamp;
     usecs = do_div(ts,1000000000);
     secs = ts;  usecs /= 1000;
   }
#endif
   secs -= start->tv_sec;
   if( usecs < start->tv_usec ) {
      borrow = (start->tv_usec-usecs+1000000-1)/1000000;
      secs -= borrow;  usecs += 1000000*borrow;
   }
   usecs -= start->tv_usec;
   ts.tv_sec = secs;
   ts.tv_nsec = usecs*1000;
   return jiffy_offset + timespec_to_jiffies(&ts);
}

/** v4l_vid_buffer - update buffer user data
 *  update buffer fields to specfied buffer contents and size
 */
void v4l_vid_buffer(VidDevice *dp,struct v4l2_buffer *bfr,io_buf *bp,
   int used,int size,int flags)
{
   memset(bfr,0,sizeof(*bfr));
   bfr->type = bp->type;
   bfr->bytesused = used;
   bfr->index = bp->index;
   bfr->sequence = bp->sequence;
   bfr->flags = bp->flags | flags;
   bfr->length = size;
#ifdef REV2
   bfr->memory = bp->memory;
   switch( bp->memory ) {
   case V4L2_MEMORY_OVERLAY:  bfr->m.offset = bp->phys_addr-cim_get_fb_base();  break;
   case V4L2_MEMORY_MMAP:     bfr->m.offset = bp->offset;  break;
   case V4L2_MEMORY_USERPTR:  bfr->m.userptr = bp->start;  break;
   }
#else
   bfr->offset = bp->offset;
#endif
}

/** v4l_alloc_bfrs - allocate and possibly queue video memory buffers
 *  video memory buffers are allocated (as many as possible, up to spcified limit).
 *  the allocated buffers may be queued to overlay/capture queues, depending
 *  on the direction of the qbfrs varaible (-1 read,1 write, 0 no queuing).
 */
int v4l_alloc_bfrs(VidDevice *dp,int io_type,int max,int imgsz,int qbfrs)
{
   int ret = 0;
   io_buf *bp;
   struct list_head *qp;
   int i, count;
   int limit, capt_limit, ovly_limit;
   unsigned long size;
   FilePriv *fp = &dp->fp;
   io_queue *io = fp->io;
   size = phys_bfrsz(imgsz);
   count = v4l_max_buffers(size,dp);
   DMSG(5,"v4l_alloc_bfrs: v4l_max_buffers allows %d buffers and max is %d\n", count, max);
   if( count > max ) count = max;
   if( io == NULL ) {
      capt_limit = v4l_capt_bfr_limit(dp);
      ovly_limit = v4l_ovly_bfr_limit(dp);
      DMSG(5,"v4l_alloc_bfrs: capt_limit: %d ovly_limit: %d\n", capt_limit, ovly_limit);
      limit = capt_limit + ovly_limit;
      /* not limited to max because only count bfrs are queued.
         a second set of (limit-max) bfrs can be used for ovly
         if max bfrs are allocated for capt */
      if( (ret=new_vio_queue(io_type,limit,size,&io)) != 0 ) return ret;
      DMSG(5,"v4l_alloc_bfrs: limit: %d\n", limit);
      fp->io = io;
   }
   else if( io->io_type != io_type ) {
      DMSG(1,"v4l_alloc_bfrs: io type mismatch %d/%d\n",io->io_type,io_type);
      return -EINVAL;
   }
   else if( (limit=io->nbfrs-io->mbfrs) == 0 )
      return -ENOMEM;
   else {
   	  DMSG(5,"v4l_alloc_bfrs: count: %d limit: %d\n", count, limit);
      if( count > limit ) count = limit;
      if( count == 0 ) return -ENOMEM;
   }
   qp = qbfrs == 0 ? NULL :
        qbfrs > 0 ? &io->wr_dqbuf : &io->rd_qbuf;
   DMSG(2,"v4l_alloc_bfrs: allocating %d buffers\n",count);
   for( i=count; --i>=0; ) {
      if( (ret=new_buffer(io,&bp)) != 0 ) return ret;
      if( qp != NULL ) v4l_qbfr(dp,qp,bp,0);
   }
   return ret;
}

/** v4l_set_std_data - build v4l2 record
 * return v4l2 std record based in selected std_index and specified
 *  field data in arugment list.
 */
static void
v4l_set_std_data(struct v4l2_standard *std,int index,
   unsigned long id,char *name,int num,int denom,int lines)
{
      strncpy(&std->name[0],name,sizeof(std->name));
      std->framelines = lines;
#ifdef REV2
      std->index = index;
      std->id = id;
      std->frameperiod.numerator = num;
      std->frameperiod.denominator = denom;
#else
      std->framerate.numerator = num;
      std->framerate.denominator = denom;
      switch( id ) {
      default:
      case V4L2_STD_NTSC:
         std->colorstandard = V4L2_COLOR_STD_NTSC;
         std->colorstandard_data.ntsc.colorsubcarrier = V4L2_COLOR_SUBC_NTSC;
         break;
      case V4L2_STD_PAL:
         std->colorstandard = V4L2_COLOR_STD_PAL;
         std->colorstandard_data.pal.colorsubcarrier = V4L2_COLOR_SUBC_PAL;
         break;
      case V4L2_STD_SECAM:
         std->colorstandard = V4L2_COLOR_STD_SECAM;
         std->colorstandard_data.secam.f0b = V4L2_COLOR_SUBC_SECAMB;
         std->colorstandard_data.secam.f0r = V4L2_COLOR_SUBC_SECAMR;
         break;
      }
#endif
}

/** v4l_standard - return standard data by std_index */
int
v4l_standard(int index, struct v4l2_standard *std)
{
   struct s_v4l_std_data *sp;
   memset(std,0,sizeof(*std));
   if( index < 0 || index >= STD_IDS_SZ )
      return -EINVAL;
   sp = &v4l_std_data[index];
   v4l_set_std_data(std,index,sp->id,sp->name,sp->num,sp->denom,sp->lines);
   return 0;
}

/** v4l_max_buffers - return max number of available buffers
 * return number of buffers which can be generated with the
 *  available pool of video memory, for a given buffer size
 *  the count is limited to VIP_MAX_BUFFERS
 */
int v4l_max_buffers(unsigned long size,VidDevice *dp)
{
   int count = vid_mem_avail(size);
   DMSG(5,"v4l_max_buffers: count: %d VIP_MAX_BUFFERS: %d\n", count, VIP_MAX_BUFFERS);
   if( count > VIP_MAX_BUFFERS ) count = VIP_MAX_BUFFERS;
   return count;
}

/** v4l_default_capt_format - build a default format record for the capture device
 * returns a v4l2_pix_format record consistent with the V4L_DEFAULT_PIX_FMT
 */
void v4l_default_capt_format(VidDevice *dp,struct v4l2_pix_format *fmt)
{
   int colorspace, field, y_pitch, uv_pitch;
   lx_capt_std_geom(dp,dp->std_index,&fmt->width,&fmt->height);
   fmt->pixelformat = V4L_DEFAULT_PIX_FMT;
   y_pitch = lx_capt_ypitch(dp,dp->capt.pixelformat,fmt->width);
   uv_pitch = lx_capt_uvpitch(dp,dp->capt.pixelformat,fmt->width);
   if( v4l_fmt_is_420(dp->capt.pixelformat) > 0 )
      uv_pitch /= 2;
   fmt->bytesperline = y_pitch + 2*uv_pitch;
   fmt->sizeimage = fmt->bytesperline*fmt->height;
   field = V4L2_FIELD_INTERLACED;
   v4l_vid_set_field(fmt,field);
   colorspace = dp->std_index >= STD_HD_480P ?
      V4L2_COLORSPACE_SMPTE170M : V4L2_COLORSPACE_REC709;
   v4l_vid_set_colorspace(fmt,colorspace);
   fmt->priv = 0;
}

/** v4l_default_ovly_format - build a default format record for the overlay device
 * returns a v4l2_pix_format record consistent with the V4L_DEFAULT_PIX_FMT
 */
void v4l_default_ovly_format(VidDevice *dp,struct v4l2_window *win)
{
   memset(win,0,sizeof(*win));
#ifdef REV2
   win->w.top = win->w.left = 0;
   lx_capt_std_geom(dp,dp->std_index,&win->w.width,&win->w.height);
   win->field = V4L2_FIELD_INTERLACED;
#else
   win->x = win->y = 0;
   lx_capt_std_geom(dp,dp->std_index,&win->width,&win->height);
#endif
}

/** v4l_check_flipping - determine if auto-flipping to be used
 */
int v4l_check_flipping(VidDevice *dp)
{
   int i, nbfrs, mbfrs, capt_limit, ovly_limit;
   unsigned int ovly_pixelformat;
   unsigned long imgsz;
   int ret = 0;
   FilePriv *fp = &dp->fp;
   io_queue *io = fp->io;
   if( io == NULL && dp->capt_addr_is_set == 0 && dp->ovly_addr_is_set == 0 ) {
      ovly_pixelformat = dp->capt.pixelformat;
      imgsz = dp->capt.sizeimage;
      capt_limit = v4l_capt_bfr_limit(dp);
      ovly_limit = v4l_ovly_bfr_limit(dp);
      DMSG(5,"v4l_check_flipping: capt_limit: %d ovly_limit: %d\n", capt_limit, ovly_limit);
      nbfrs = capt_limit + ovly_limit;
      if( (ret=v4l_alloc_bfrs(dp,io_flipped,nbfrs,imgsz,-1)) == 0 ) {
         io = fp->io;
         mbfrs = io->mbfrs;
         for( i=0; i<mbfrs; ++i )
            memset(io->bfrs[i].bfrp,0x80,imgsz);
         if( lx_ovly_hw_fmt(dp,ovly_pixelformat) >= 0 ) {
            dp->ovly_pixelformat = ovly_pixelformat;
            dp->ovly_colorspace = v4l_vid_colorspace(&dp->capt);
            lx_ovly_set_offsets(dp);
            lx_ovly_set_bfr(dp,io->bfrs[0].phys_addr);
         }
         else {
            DMSG(1,"VIDIOC_OVERLAY: capt pixformat not supported by ovly\n");
            ret = -EINVAL;
         }
      }
   }
   return ret;
}

/* v4l_capt_start - start the capture */
int
v4l_capt_start(VidDevice *dp)
{
   unsigned long flags;
   FilePriv *fp;
   io_queue *io;
   int ret = 0;

   // Bri. test : avoid panic !
   if (dp == NULL) return -EINVAL;
   fp = &dp->fp; //is !=NULL if dp!=NULL
   io = fp->io;
   //no test on io;

   spin_lock_irqsave(&io->lock, flags);
   if( dp->capt_state != capt_state_run ) {
      if( dp->capt_addr_is_set != 0 || io != NULL ) {
         DMSG(4,"capt started, iobfrs %d\n",io!=NULL? io->mbfrs : -1);
         if( io != NULL )
            io->sequence = 0;
         dp->capt_field = 0;
         dp->capt_frame = 0;
         dp->capt_stalled = 0;
         dp->capt_skipped = 0;
         dp->capt_dropped = 0;
         dp->capt_sequence = 0;
         dp->capt_jiffy_sequence = 0;
         do_gettimeofday(&dp->capt_start_time);
         dp->capt_jiffy_start = dp->capt_start_jiffy = jiffiez;
         if( (ret=lx_capt_start(dp)) == 0 )
         {
            dp->capt_state = capt_state_run;
            DMSG(5,"capt_state_run\n");
         }
         else
            DMSG(5,"capt_state still not in run mode\n");
      }
      else {
         DMSG(1,"capt not started, no bfrs\n");
         ret = -EINVAL;
      }
   }
   spin_unlock_irqrestore(&io->lock, flags);
   return ret;
}

/* v4l_capt_unstall - continue a stalled capture
 *  a stalled capture occurs when buffers are demanded in the interrupt
 *  handler, and there are no queued buffers available.  This routine is
 *  called by interfaces which requeue buffers when a stall has occured.
 *  io->lock must be locked before call !
 */
int
v4l_capt_unstall(VidDevice *dp)
{
//   unsigned long flags;
   int ret = 0;
   FilePriv *fp;
   io_queue *io;

   //Bri. tests to avoid panic !
   if (dp == NULL) return -EINVAL;
   fp = &dp->fp; //is !=NULL if dp!=NULL
   io = fp->io;
   if (io == NULL) return -EINVAL;

// Bri. :  already locked by the caller (v4l_capt_qbfr)
//   spin_lock_irqsave(&io->lock, flags);
   if( dp->capt_state == capt_state_run ) {
       //if( io != NULL ) {
       if( dp->capt_stalled != 0 )
           DMSG(4,"capt resumed\n");
       ret = lx_capt_resume(dp,io);
       // }
   }
//   spin_unlock_irqrestore(&io->lock, flags);
   return ret;
}

/** v4l_capt_requebfr - unleak a buffer (pair)
 *  requeue buffers which have been dequeued, but not finished.
 */
static void
v4l_capt_requebfr(VidDevice *dp,struct list_head *qp,io_buf **pop,io_buf **pep)
{
   io_buf *op, *ep;
   if( (op=*pop) != NULL ) {
      if( dp->capt_toggle == capt_toggle_both ) {
         if( (ep=*pep) != NULL ) {
            list_move(&ep->bfrq,qp);
         }
      }
      list_move(&op->bfrq,qp);
      *pop = *pep = NULL;
   }
}

/** v4l_capt_requebfrs - unleak active buffers (pairs)
 *  when a capture is stopped, the intermediate buffers are requeued
 *   to recover the memory.  Active pointers are nilled to prevent
 *   conflicting allocations on restart.
 */
static void
v4l_capt_requebfrs(VidDevice *dp)
{
   struct list_head *qp;
   FilePriv *fp;
   io_queue *io;

   //Bri. tests to avoid panic !
   if (dp == NULL) return;
   fp = &dp->fp; //is !=NULL if dp!=NULL
   io = fp->io;
   if (io == NULL) return;

   qp = &io->rd_qbuf;
   v4l_capt_requebfr(dp,qp,&dp->capt_onxt,&dp->capt_enxt);
   v4l_capt_requebfr(dp,qp,&dp->capt_olch,&dp->capt_elch);
   v4l_capt_requebfr(dp,qp,&dp->capt_obfr,&dp->capt_ebfr);
}

/** v4l_capt_stop - discontinue capture */
void
v4l_capt_stop(VidDevice *dp)
{
   unsigned long flags;
   FilePriv *fp;
   io_queue *io;

   // Bri. test : avoids panic !
   if (dp == NULL) return;
   fp = &dp->fp; //is !=NULL if dp!=NULL
   io = fp->io;
   if (io == NULL) return;

   spin_lock_irqsave(&io->lock, flags);
   if( dp->capt_state != capt_state_idle ) {
      if( dp->capt_addr_is_set != 0 || io != NULL ) {
         DMSG(4,"capt stopped\n");
         lx_capt_stop(dp);
         dp->capt_state = capt_state_idle;
         dp->capt_stalled = 0;
         v4l_capt_requebfrs(dp);
      }
   }
   spin_unlock_irqrestore(&io->lock, flags);
}

/* v4l_ovly_imagesize - return overlay image size (in bytes) */
int
v4l_ovly_imagesize(VidDevice *dp)
{
#ifdef REV2
   int width = dp->ovly.w.width;
   int height = dp->ovly.w.height;
#else
   int width = dp->ovly.width;
   int height = dp->ovly.height;
#endif
   int y_pitch = lx_ovly_ypitch(dp,width);
   int uv_pitch = lx_ovly_uvpitch(dp,width);
   if( v4l_fmt_is_420(dp->ovly_pixelformat) > 0 )
      uv_pitch /= 2;
   return (y_pitch+2*uv_pitch)*height;
}

/* v4l_ovly_post_bfr - dequeue and post next buffer (if possible) */
static int
v4l_ovly_post_bfr(VidDevice *dp)
{
   int ret = 0;
   FilePriv *fp = &dp->fp;
   io_queue *io = fp->io;
   io_buf *bp = v4l_dq1bfr(dp,&io->wr_qbuf);
   if( bp != NULL ) {
      if( dp->ovly_bfr != NULL ) {
         v4l_qbfr(dp,&io->wr_dqbuf,dp->ovly_bfr,0);
         if( waitqueue_active(&dp->ovly_wait) != 0 )
             wake_up_interruptible(&dp->ovly_wait);
      }
      dp->ovly_bfr = bp;
      lx_ovly_set_bfr(dp,bp->phys_addr);
   }
   else {
      ++dp->ovly_dropped;
      ret = -EAGAIN;
   }
   return ret;
}

/** v4l_ovly_stop_frame_timer - stop overlay frame timer
 *   overlay buffers are no longer posted by timer callback
 *   frame timer is deactivated.
 */
void
v4l_ovly_stop_frame_timer(VidDevice *dp)
{
   if( dp->ovly_timer_active != 0 ) {
      del_timer(&dp->ovly_timer);
      dp->ovly_timer_active = 0;
   }
}

/** v4l_ovly_start_frame_timer - start overlay frame timer
 *   overlay buffers are posted by timer callback
 *   frame timer is activated.  timer expiration determined
 *   by presentation time in next queued buffer.  Timer marked
 *   inactive if no buffers queued for presentation.
 */
void
v4l_ovly_start_frame_timer(VidDevice *dp)
{
   struct list_head *lp;
   FilePriv *fp = &dp->fp;
   io_queue *io;  io_buf *bp;
   if( (io=fp->io) != NULL && list_empty(lp=&io->wr_qbuf) == 0 ) {
      bp = list_entry(lp->next,io_buf,bfrq);
      dp->ovly_timer.expires = bp->jiffies;
      add_timer(&dp->ovly_timer);
   }
   else
      dp->ovly_timer_active = 0;
}

/** v4l_ovly_frame_timer - timer callback
 *  buffer sequence incrmented (and possibly rolled over).
 *  next buffer posted if possible.
 */
void
v4l_ovly_frame_timer(unsigned long data)
{
   unsigned long flags;
   VidDevice *dp = (VidDevice *)data;
   FilePriv *fp;
   io_queue *io;

   // Bri. test : avoid panic !
   if (dp == NULL) return;
   fp = &dp->fp; //is !=NULL if dp!=NULL
   io = fp->io;
   if (io == NULL) return;

   spin_lock_irqsave(&io->lock, flags);
   if( dp->ovly_timer_active != 0 && dp->ovly_state == ovly_state_run ) {
      ++dp->ovly_jiffy_sequence;
      v4l_ovly_post_bfr(dp);
      v4l_ovly_start_frame_timer(dp);
      ++dp->ovly_sequence;
      ++dp->ovly_frames;
   }
   spin_unlock_irqrestore(&io->lock, flags);
}

/** v4l_ovly_start - turn on the overlay */
int
v4l_ovly_start(VidDevice *dp)
{
   unsigned long flags;
   FilePriv *fp;
   io_queue *io;
   int ret = 0;

   // Bri. test : avoid panic !
   if (dp == NULL) return -EINVAL;
   fp = &dp->fp; //is !=NULL if dp!=NULL
   io = fp->io;
   if (io == NULL) return -EINVAL;

   spin_lock_irqsave(&io->lock, flags);
   if( dp->ovly_state != ovly_state_run ) {
      if( dp->ovly_addr_is_set != 0 || io != NULL ) {
         DMSG(4,"ovly started\n");
         if( io != NULL )
            io->sequence = 0;
         dp->ovly_frames = 0;
         dp->ovly_dropped = 0;
         dp->ovly_sequence = 0;
         dp->ovly_jiffy_sequence = 1;
         do_gettimeofday(&dp->ovly_start_time);
         dp->ovly_jiffy_start = dp->ovly_start_jiffy = jiffiez;
         if( (ret=lx_ovly_start(dp)) == 0 )
            dp->ovly_state = ovly_state_run;
      }
   }
   spin_unlock_irqrestore(&io->lock, flags);
   return ret;
}

/** v4l_ovly_requebfrs - unleak overlay buffer
 *  requeue buffers which have been dequeued, and are still active
 *  requeued pointers are nilled.
 */
static void
v4l_ovly_requebfrs(VidDevice *dp)
{
   io_buf *bp;
   struct list_head *qp;
   FilePriv *fp = &dp->fp;
   io_queue *io = fp->io;
   if( io == NULL ) return;
   qp = &io->wr_qbuf;
   if( (bp=dp->ovly_bfr) != NULL ) {
      list_move(&bp->bfrq,qp);
      dp->ovly_bfr = NULL;
   }
}

/** v4l_ovly_stop - stop the overlay device */
void
v4l_ovly_stop(VidDevice *dp)
{
   unsigned long flags;
   FilePriv *fp;
   io_queue *io;

   // Bri. test : avoids panic !
   if (dp == NULL) return;
   fp = &dp->fp; //is !=NULL if dp!=NULL
   io = fp->io;
   if (io == NULL) return;

   spin_lock_irqsave(&io->lock, flags);
   v4l_ovly_stop_frame_timer(dp);
   if( dp->ovly_state != ovly_state_idle ) {
      if( dp->ovly_addr_is_set != 0 || io != NULL ) {
         DMSG(4,"ovly stopped\n");
         lx_ovly_stop(dp);
         dp->ovly_state = ovly_state_idle;
         v4l_ovly_requebfrs(dp);
      }
   }
   spin_unlock_irqrestore(&io->lock, flags);
}

/** v4l_component_ioctl - try the request in the specified client handler
 *  returns ENODEV if there is no client handler for request
 */
int v4l_component_ioctl(struct i2c_client * client, unsigned int fn, void * arg)
{
   struct packed_ioctl_t {
     int cmd;
     void *arg;
   } p;
   if( client == NULL ) return -ENODEV;
   p.cmd = fn;  p.arg = arg;
   return client->driver->command(client,V4L2_COMPONENT_COMMAND,&p);
}

/** vid_open - open video device */
#ifdef LINUX_2_6
static int

vid_open(struct file *file)
{
 struct video_device *v = video_devdata(file);

 int flags = file->f_flags;
#else
static int
vid_open(struct video_device *v, int flags)
{
#endif


   V4LDevice *pp = (V4LDevice *)video_get_drvdata(v);
   VidDevice *dp = &pp->vid;
   FilePriv *fp = &dp->fp;
   int ret = 0;
   DMSG(2," %#x %s %d\n",flags,&v->name[0],fp->open_files);

	 // dcoz. Release image buffers if they happen to be non-NULL.
   // Patch to FS#1161.
   if (fp->io != NULL) {
     DMSG(0, " Warning, io size > 0  : %d. Force-delete buffers.\n",
       (int)fp->io->size);
     del_io_queue(&fp->io);
   }

   if( fp->open_files == 0 ) {
      fp->type = ft_vid;
      fp->file = NULL;
      dp->capt_obfr = dp->capt_ebfr = NULL;
      dp->capt_olch = dp->capt_elch = NULL;
      dp->capt_onxt = dp->capt_enxt = NULL;
      dp->ovly_obfr = dp->ovly_ebfr = NULL;
      dp->fb_is_set = 0;
      dp->ovly_src.is_set = 0;
      dp->ovly_dst.is_set = 0;
      dp->capt_addr_is_set = 0;
      dp->ovly_addr_is_set = 0;
      dp->capt_num = dp->std_num * HZ;
      dp->capt_denom = dp->std_denom;
      dp->ovly_num = dp->std_num * HZ;
      dp->ovly_denom = dp->std_denom;
   }

   ++fp->open_files;
   return ret;
}

/* v4l2_open - rev1 open video device */
#ifndef REV2
static int
v4l2_open(struct v4l2_device *v2, int flags, void **idptr)
{
    struct video_device *v = (struct video_device *)v2->priv;
    int ret = vid_open(v,flags);
    if( ret == 0 ) *idptr = v;
    return ret;
}
#endif

/** vid_stats - log closing io statistics */
static void
vid_stats(VidDevice *dp)
{
   if( dp->capt_sequence > 0 ) {
      DMSG(1," %d capt sequence\n",dp->capt_sequence);
      DMSG(1," %d capt dropped\n",dp->capt_dropped);
   }
   if( dp->ovly_sequence > 0 ) {
      DMSG(1," %d ovly sequence\n",dp->ovly_sequence);
      DMSG(1," %d ovly dropped\n",dp->ovly_dropped);
   }
}

/** vid_close - close video device
 *  video operations are terminated and resources released
 */
#ifdef LINUX_2_6
static int
vid_close(struct file *file)
{
   struct video_device *v = video_devdata(file);
#else
static void
vid_close(struct video_device *v)
{
#endif
  V4LDevice *pp = (V4LDevice *)video_get_drvdata(v);
   VidDevice *dp = &pp->vid;
   FilePriv *fp = &dp->fp;
   io_queue *io = fp->io;
   DMSG(2," %s %d\n",&v->name[0],fp->open_files);

   if( --fp->open_files == 0
#ifdef LINUX_2_6
       || fp->file == file
#endif
      ) {
      v4l_capt_stop(dp);
      v4l_ovly_stop(dp);
      if( io != NULL )
         del_io_queue(&fp->io);
      vid_stats(dp);
      fp->type = ft_none;
      fp->file = NULL;
   }
#ifdef LINUX_2_6
   return 0;
#endif
}

/** v4l2_close - rev2 close video device */
#ifndef REV2
static void
v4l2_close(void *id)
{
   struct video_device *v = (struct video_device *)id;
   vid_close(v);
}
#endif

/** vid_write - sequential write overlay
 *  if the stream is idle, the buffers pool is constructed and
 *   the overlay is started.  User data is streamed to overlay
 *   buffers and posted by the timer handler.  The update timeing
 *   depends on the ovly_num/denom data rate.  The buffers are
 *   complete when imgsz bytes have been transmitted.
 */
#ifdef LINUX_2_6
static ssize_t
vid_write(struct file *file, const char __user * buf, size_t count, loff_t *ppos)
{
   struct video_device *v = video_devdata(file);
   int nonblock = O_NONBLOCK & file->f_flags;
#else
static long
vid_write(struct video_device *v, const char *buf, unsigned long count, int nonblock)
{
#endif
   long ret;  int ovly_limit;
   char *bfr = (char *)buf;
   V4LDevice *pp = (V4LDevice *)video_get_drvdata(v);
   VidDevice *dp = &pp->vid;
   int imgsz = v4l_ovly_imagesize(dp);
   DMSG(2,"ovly %lu %d %d\n",(unsigned long)count,nonblock,imgsz);
   if( !access_ok(VERIFY_READ, bfr, count) ) return -EFAULT;
   if( dp->ovly_state == ovly_state_idle ) {
      ovly_limit = v4l_ovly_bfr_limit(dp);
      if( (ret=v4l_alloc_bfrs(dp,io_queued,ovly_limit,imgsz,1)) != 0 ) return ret;
      if( (ret=v4l_ovly_start(dp)) != 0 ) return ret;
   }
   if( (ret=v4l_stream_copy(dp,imgsz,1,bfr,count,nonblock)) < 0 ) return ret;
   if( dp->ovly_timer_active == 0 ) {
      dp->ovly_timer_active = 1;
      v4l_ovly_start_frame_timer(dp);
   }
   return ret;
}

/** v4l2_write - rev1 sequential write overlay */
#ifndef REV2
static long
v4l2_write(void *id, const char *buf, unsigned long count, int nonblock)
{
   struct video_device *v = (struct video_device *)id;
   return vid_write(v,buf,count,nonblock);
}
#endif

/** vid_read - sequential read capture
 *  if the stream is idle, the buffers pool is constructed and
 *   the capture is started.  Capture data is streamed to user
 *   buffers as it arrives from the interrupt handler.  Frames
 *   can be downsampled by setting a max rate utilizing capt num/denom.
 */
#ifdef LINUX_2_6
static ssize_t
vid_read(struct file *file, char *bfr, size_t count, loff_t * ppos)
{
   struct video_device *v = video_devdata(file);
   int nonblock = O_NONBLOCK & file->f_flags;
#else
static long
vid_read(struct video_device *v, char *bfr, unsigned long count, int nonblock)
{
#endif
   long ret;  int capt_limit;
   V4LDevice *pp = (V4LDevice *)video_get_drvdata(v);
   VidDevice *dp = &pp->vid;
   unsigned long imgsz = dp->capt.sizeimage;
   DMSG(2,"capt %lu %d %lu\n",(unsigned long)count,nonblock,imgsz);
   if( !access_ok(VERIFY_WRITE, bfr, count) ) return -EFAULT;
   if( dp->capt_state == capt_state_idle ) {
      DMSG(5,"capt_state_idle\n");
      capt_limit = v4l_capt_bfr_limit(dp);
      if( (ret=v4l_alloc_bfrs(dp,io_queued,capt_limit,imgsz,-1)) != 0 ) return ret;
      if( (ret=v4l_capt_start(dp)) != 0 ) return ret;
   }
   return v4l_stream_copy(dp,imgsz,0,bfr,count,nonblock);
}

/** v4l2_read - rev1 sequential read capture */
#ifndef REV2
static long
v4l2_read(void *id, char *bfr, unsigned long count, int nonblock)
{
   struct video_device *v = (struct video_device *)id;
   return vid_read(v,bfr,count,nonblock);
}
#endif

#define SWITCH(v) switch(v) {
#define CASE(nm) break; } case nm: DMSG(2, #nm "\n"); {
#define DEFAULT(s...) default: { DMSG(2,s)
#define HCTIWS } }

/** vid_ioctl - video device ioctl
 *  provides most of the operational functionallity of the interface.
 *   Implementations includes most of the v4l and v4l2 design.  The
 *   overlay S_FMT function has 2 buffer types which are outside of
 *   the standard design.  They specify the src/dest rectangles for
 *   the overlay.  If there are componenets attached when the driver
 *   initializes, this causes many functions to forward the operation
 *   to the attached driver.
 */
#ifdef LINUX_2_6
static int
vid_ioctl(struct file *file, unsigned int cmd,
             unsigned long ul_arg)
{
   void *arg = (void *)ul_arg;
   struct video_device *v = video_devdata(file);
#else
int
vid_ioctl(struct video_device *v, unsigned int cmd, void *arg)
{
#endif
   int ret = 0;
   V4LDevice *pp = (V4LDevice *)video_get_drvdata(v);
   VidDevice *dp = &pp->vid;
   FilePriv *fp = &dp->fp;
   io_queue *io = fp->io;

   spin_lock(&dp->lock);

   SWITCH( cmd )
   DEFAULT(" unknown %08x\n",cmd);
      ret = -EINVAL;

   CASE(VIDIOCGCAP) /* get capability */
      struct video_capability *cap = arg;
      memset(cap,0,sizeof(*cap));
      strncpy(&cap->name[0],v4l_name(),sizeof(cap->name));
      cap->type = fp->vd.vfl_type;
      cap->channels = v4l_num_channels();
      cap->audios = v4l_num_audios();
      lx_capt_max_rect(dp,&cap->maxwidth,&cap->maxheight);
      lx_capt_min_rect(dp,&cap->minwidth,&cap->minheight);

   CASE(VIDIOCGFBUF) /* get frame buffer */
      struct video_buffer *fb = arg;
      fb->base = (void *)dp->fb_phys_addr;
      fb->width = dp->fb_width;
      fb->height = dp->fb_height;
      fb->depth = dp->fb_depth;
      fb->bytesperline = dp->fb_pitch;

   CASE(VIDIOCSFBUF) /* set frame buffer */
      struct video_buffer *fb = arg;
      DMSG(3," base=0x%lx, %dx%d, %d, %d\n",(unsigned long)fb->base,
         fb->width,fb->height,fb->depth,fb->bytesperline);
      v4l_capt_stop(dp);
      dp->fb_phys_addr = (unsigned long)fb->base;
      dp->fb_width = fb->width;
      dp->fb_height = fb->height;
      dp->fb_depth = fb->depth;
      dp->fb_pitch = fb->bytesperline;
      if( dp->fb_pitch == 0 )
         dp->fb_pitch = dp->fb_width*((dp->fb_depth+7)/8);
      dp->fb_is_set = 1;
      dp->capt_addr_is_set = 0;
      dp->ovly_addr_is_set = 0;

   CASE(VIDIOCGWIN) /* get ovly dst window */
      struct video_window *win = arg;
      memset(win,0,sizeof(*win));
      win->x = dp->ovly_dst.x;
      win->y = dp->ovly_dst.y;
      win->width = dp->ovly_dst.w;
      win->height = dp->ovly_dst.h;
      if( lx_ovly_has_chomakey() ) {
         win->chromakey = dp->ovly.chromakey;
         if( dp->ovly_keymode != 0 )
            win->flags |= VIDEO_WINDOW_CHROMAKEY;
      }
      win->clipcount = 0;

   CASE(VIDIOCSWIN) /* set ovly src window, or set capt phys addr */
      struct video_window *win = arg;
      DMSG(3," %d,%d, %dx%d, clipcount=%d\n",
         win->x, win->y, win->width, win->height, win->clipcount);
      ret = lx_ovly_set_src_rect(dp,0,0,win->width,win->height);
      if( ret != 0 ) break;
      if( lx_ovly_has_chomakey() ) {
         if( (win->flags&VIDEO_WINDOW_CHROMAKEY) != 0 ) {
            dp->ovly_keymode = 1;
            dp->ovly.chromakey = win->chromakey;
            lx_ovly_chromakey(dp,win->chromakey,0xffffff,1);
         }
         else {
            dp->ovly_keymode = 0;
            dp->ovly.chromakey = 0;
            lx_ovly_chromakey(dp,0,0,0);
         }
      }
      if( dp->fb_is_set != 0 ) {
         int bpp = (dp->fb_depth+7)/8;
         dp->capt_phys_addr = dp->fb_phys_addr + win->y*dp->fb_pitch + win->x*bpp;
         dp->capt_addr_is_set = 1;
         lx_ovly_set_bfr(dp,dp->capt_phys_addr);
         dp->ovly_addr_is_set = 1;
      }

   CASE(VIDIOCCAPTURE) /* enable/disable capture */
      int enabled = *(int *)arg;
      DMSG(3, "  enabled - %d\n",enabled);
      if( enabled != 0 ) {
         if( dp->fb_is_set == 0 || dp->capt_addr_is_set == 0 ) {
            DMSG(1," VIDIOCCAPTURE: capt fb/geom rect not set\n");
            ret = -EINVAL;  break;
         }
         v4l_capt_start(dp);
      }
      else
         v4l_capt_stop(dp);

   CASE(VIDIOCGCHAN) /* return channel source data */
      struct video_channel *chan = arg;
      int channel = chan->channel;
      DMSG(3, "  channel=%d\n",channel);
      memset(chan,0,sizeof(*chan));
      if( channel < 0 || channel > v4l_num_channels() ) {
         DMSG(1, "  VIDIOCGCHAN: bad channel %d\n", channel);
         ret = -EINVAL;  break;
      }
      if( pp->tv_decoder != NULL ) {
         ret = v4l_component_ioctl(lx_dev->tv_decoder,cmd,arg);
         if( ret != 0 ) break;
      }
      else if( channel == 0 ) {
         strcpy(&chan->name[0],"inp0");
      }
      else {
         DMSG(1, "VIDIOCGCHAN: no decoder, only chan 0\n");
         ret = -EINVAL;  break;
      }
      chan->tuners = lx_dev->tv_tuner != NULL ? 1 : 0;
      chan->flags = 0;
      chan->type = VIDEO_TYPE_CAMERA;
      chan->norm = v4l_std_norm(dp->std_index);
      chan->channel = channel;

   CASE(VIDIOCSCHAN) /* set channel source data */
      struct video_channel *chan = arg;
      int channel = chan->channel;
      int norm = chan->norm;
      DMSG(3, " channel=%d, norm=%d\n", channel, norm);
      if( (ret=v4l_set_input(dp,channel)) != 0 ) break;
      if( (ret=v4l_set_norm(dp,norm)) != 0 ) break;

   CASE(VIDIOCGPICT) /* return video control data */
      struct video_picture *pict = arg;
      memset(pict,0,sizeof(*pict));
      pict->hue = dp->capt_hue;
      pict->brightness = dp->capt_brightness;
      pict->contrast = dp->capt_contrast;
      pict->colour = dp->capt_saturation;
      pict->palette = v4l_pixelformat_to_palette(dp->ovly_pixelformat);

   CASE(VIDIOCSPICT) /* set video control data */
      int pixelformat;
      struct video_picture *pict = arg;
      DMSG(3, " bri=%d, hue=%d, col=%d, con=%d, dep=%d, pal=%d\n",
         pict->brightness, pict->hue, pict->colour, pict->contrast,
         pict->depth, pict->palette);
      pixelformat = v4l_palette_to_pixelformat(pict->palette);
      if( lx_ovly_is_supported_format(dp,pixelformat) == 0 ||
          lx_ovly_is_supported_depth(dp,pict->depth) == 0 ) {
         DMSG(1, "VIDIOCSPICT: bad ovly palette/depth %d/%d\n",pict->palette,pict->depth);
         ret = -EINVAL;  break;
      }
      dp->ovly_pixelformat = pixelformat;
      if( dp->capt_hue != pict->hue ) {
         dp->capt_hue = pict->hue;
         v4l_set_control(dp,V4L2_CID_HUE,dp->capt_hue);
      }
      if( dp->capt_contrast != pict->contrast ) {
         dp->capt_contrast = pict->contrast;
         v4l_set_control(dp,V4L2_CID_CONTRAST,dp->capt_contrast);
      }
      if( dp->capt_saturation != pict->colour ) {
         dp->capt_saturation = pict->colour;
         v4l_set_control(dp,V4L2_CID_SATURATION,dp->capt_saturation);
      }
      if( dp->capt_brightness != pict->brightness ) {
         dp->capt_brightness = pict->brightness;
         v4l_set_control(dp,V4L2_CID_BRIGHTNESS,dp->capt_brightness);
      }

   CASE(VIDIOCGTUNER)   ret = -ENOIOCTLCMD; /* get tuner information  */
   CASE(VIDIOCSTUNER)   ret = -ENOIOCTLCMD; /* select a tuner input  */
   CASE(VIDIOCGFREQ)    ret = -ENOIOCTLCMD; /* get frequency  */
   CASE(VIDIOCSFREQ)    ret = -ENOIOCTLCMD; /* set frequency  */
   CASE(VIDIOCGAUDIO)   ret = -ENOIOCTLCMD; /* get audio properties/controls  */
   CASE(VIDIOCSAUDIO)   ret = -ENOIOCTLCMD; /* set audio controls  */

   CASE(VIDIOCMCAPTURE) /* queue capture buffer */
      io_buf *bp;  int format;
      struct video_mmap *map = arg;
      int index = map->frame;
      DMSG(3,"  index = %d\n",index);
      if( dp->capt.width != map->width ||
          dp->capt.height != map->height ) {
         DMSG(1, "VIDIOCMCAPTURE: bad width/height %d/%d\n",map->width,map->height);
         ret = -EINVAL;  break;
      }
      format = v4l_palette_to_pixelformat(map->format);
      if( format != dp->capt.pixelformat ) {
         DMSG(1, "VIDIOCMCAPTURE: bad format %d %4.4s %4.4s\n",map->format,
            (char*)&format,(char*)&dp->capt.pixelformat);
         ret = -EINVAL;  break;
      }
      if( io == NULL ) {
         DMSG(1, "VIDIOCMCAPTURE: no VIDIOCGMBUF bufs\n");
         ret = -EINVAL;  break;
      }
      if( index < 0 || index >= io->mbfrs ) {
         DMSG(1, "VIDIOCMCAPTURE: bad index %d\n",index);
         ret = -EINVAL;  break;
      }
      bp = &io->bfrs[index];
      v4l_qbfr(dp,&io->rd_qbuf,bp,1);
      if( dp->capt_state != capt_state_run )
         ret = v4l_capt_start(dp);

   CASE(VIDIOCGMBUF) /* allocate capture buffers */
      io_buf *bp;
      int i, bfrsz, imgsz, count, capt_limit;
      struct video_mbuf *mbuf = arg;
      memset(mbuf,0,sizeof(*mbuf));
      if( io != NULL ) {
         del_io_queue(&fp->io);
         io = NULL;
      }
      imgsz = dp->capt.sizeimage;
      capt_limit = v4l_capt_bfr_limit(dp);
      ret = v4l_alloc_bfrs(dp,io_queued,capt_limit,imgsz,0);
      if( ret != 0 ) break;
      io = fp->io;
      count = io->mbfrs;
      if( count <= 0 ) { ret = -ENOMEM;  break; }
      bfrsz = io->size;
      mbuf->frames = count;
      mbuf->size = bfrsz*count;
      for( i=0,bp=&io->bfrs[0]; i<count; ++bp,++i ) {
         mbuf->offsets[i] = i*bfrsz;
         bp->type =
#ifndef REV2
            V4L2_BUF_TYPE_CAPTURE;
#else
            V4L2_BUF_TYPE_VIDEO_CAPTURE;
#endif
      }

   CASE(VIDIOCSYNC) /* deque capture buffer (blocking io) */
      int flags;  io_buf *bp;
      int index = *(int *)arg;
      DMSG(3,"  index = %d\n",index);
      if( io == NULL ) {
         DMSG(1, "VIDIOCSYNC: no VIDIOCGMBUF bufs\n");
         ret = -EINVAL;  break;
      }
      if( index < 0 || index >= io->mbfrs ) {
         DMSG(1, "VIDIOCSYNC: bad index %d\n",index);
         ret = -EINVAL;  break;
      }
      if( dp->capt_state != capt_state_run ) {
         DMSG(1, "VIDIOCSYNC: not capturing\n");
         ret = -EINVAL;  break;
      }
      bp = &io->bfrs[index];
      flags = bp->flags;
      if( (flags&V4L2_BUF_FLAG_QUEUED) == 0 && (flags&V4L2_BUF_FLAG_DONE) == 0 ) {
         DMSG(1,"VIDIOCSYNC: inactive buffer %d\n",index);
         ret = -EINVAL;  break;
      }

      while( (ret=v4l_dqbfr(dp,&io->rd_dqbuf,&bp,0,&dp->capt_wait)) == 0 &&
              bp->index!=index ) {
         v4l_qbfr(dp,&io->rd_qbuf,bp,1);
      }

      if( dp->capt_state == capt_state_run &&
          list_empty(&io->rd_qbuf) != 0 && list_empty(&io->rd_dqbuf) != 0 &&
          dp->capt_obfr == NULL && dp->capt_onxt == NULL )
         v4l_capt_stop(dp);

#ifdef REV2
   CASE(VIDIOCGVBIFMT)  ret = -ENOIOCTLCMD; /* query VBI data capture format */
   CASE(VIDIOCSVBIFMT)  ret = -ENOIOCTLCMD;
#endif

   /* CASE(VIDIOCGCAPTURE)  ret = -ENOIOCTLCMD; not supported */
   /* CASE(VIDIOCSCAPTURE)  ret = -ENOIOCTLCMD;  not supported */

   CASE(VIDIOC_G_AUDIO)  /* return audio device data */
      ret = v4l_component_ioctl(pp->tv_audio,cmd,arg);

   CASE(VIDIOC_S_AUDIO)  /* set audio device data */
      ret = v4l_component_ioctl(pp->tv_audio,cmd,arg);

   CASE(VIDIOC_ENUMSTD)  /* enumerate video standards */
#ifdef REV2
      struct v4l2_standard *std = arg;
      DMSG(3, " std->index %d\n",std->index);
      ret = v4l_standard(std->index,std);
#else
      struct v4l2_enumstd *std = arg;
      DMSG(3, " std->index %d\n",std->index);
      ret = v4l_standard(std->index,&std->std);
      std->inputs = std->outputs = 0;
#endif

   CASE(VIDIOC_G_STD) /* get current video standard */
#ifdef REV2
      v4l2_std_id *sid = arg;
      *sid = v4l_standard_id(dp->std_index);
      //*((long long *)arg) = 12345; // PATCH to keep the writing access to the page where arg is. 
      //*((v4l2_std_id *)arg) = v4l_standard_id(dp->std_index);
#else
      struct v4l2_standard *std = arg;
      ret = v4l_standard(dp->std_index,std);
#endif

   CASE(VIDIOC_S_STD) /* set current video standard */
      unsigned long bpl, imgsz;
      int width, height, y_pitch, uv_pitch, idx;
#ifdef REV2
      struct v4l2_standard std;
      v4l2_std_id *sid = arg;
      idx = v4l_standard_index(*sid);
      if( idx < 0 ) {
         DMSG(1,"unknown std %016llx\n",*sid);
         ret = -EINVAL;  break;
      }
#else
      int v4l2_std;
      struct v4l2_standard *sp = arg;
      struct v4l2_standard std = *sp;
      for( idx=0; (ret=v4l_standard(idx,&std))==0; ++idx ) {
         if( strncmp(&std.name[0],&sp->name[0],sizeof(std.name)) == 0 ) break;
#if 0
         /* this causes problems with the hi-definition modes */
         v4l2_std = v4l2_video_std_confirm(&std);
         if( v4l2_std < 0 ) continue;
         if( strncmp(&std.name[0],&sp->name[0],sizeof(std.name)) == 0 ) break;
         if( v4l_standard_id(idx) == v4l2_std ) break;
#endif
      }
      if( ret != 0 ) {
         DMSG(1,"unknown std '%s'\n",&sp->name[0]);
         break;
      }
#endif
      lx_capt_std_geom(dp,idx,&width,&height);
      y_pitch = lx_capt_ypitch(dp,dp->capt.pixelformat,width);
      uv_pitch = lx_capt_uvpitch(dp,dp->capt.pixelformat,width);
      if( v4l_fmt_is_420(dp->capt.pixelformat) > 0 )
        uv_pitch /= 2;
      bpl = y_pitch+2*uv_pitch;
      imgsz = bpl*height;
      if( dp->capt_state != capt_state_idle ||
          (io != NULL && io->size < imgsz) ) {
         ret = -EBUSY;  break;
      }
      if( (ret=v4l_standard(idx,&std)) != 0 ) break;
      if( (ret=v4l_set_std_index(dp,idx)) != 0 ) break;
      dp->capt.width = width;
      dp->capt.height = height;
      dp->capt.bytesperline = bpl;
      dp->capt.sizeimage = imgsz;
      DMSG(1,"std %d %dx%d bpl %d sz %d prgsv %d ver %d ps %d\n",
        dp->std_index,dp->capt.width,dp->capt.height,dp->capt.bytesperline,
        dp->capt.sizeimage,dp->capt_progressive,dp->capt_vip_version,dp->capt_port_size);

   CASE(VIDIOC_ENUMINPUT) /* enumerate input sources */
      struct v4l2_input *inp = arg;
      int index = inp->index;
      DMSG(3," index %d\n",index);
      if( pp->tv_decoder != NULL )
         ret = v4l_component_ioctl(lx_dev->tv_decoder,cmd,arg);
      else if( index == 0 ) {
         memset(inp,0,sizeof(*inp));
         strncpy(&inp->name[0],"inp0",sizeof(inp->name));
      }
      else
         ret = -EINVAL;
      inp->index = index;

   CASE(VIDIOC_G_INPUT) /* get current input source */
      int *inp = arg;
      if( pp->tv_decoder == NULL )
         *inp = 0;
      else
         ret = v4l_component_ioctl(lx_dev->tv_decoder,cmd,arg);

   CASE(VIDIOC_S_INPUT) /* set current input source */
      int *inp = arg;
      DMSG(3," input %d\n",*inp);
      if( pp->tv_decoder != NULL )
         ret = v4l_component_ioctl(lx_dev->tv_decoder,cmd,arg);

   CASE(VIDIOC_ENUMOUTPUT) ret = -EINVAL; /* not implemented yet */
   CASE(VIDIOC_G_OUTPUT) ret = -EINVAL; /* not implemented yet */
   CASE(VIDIOC_S_OUTPUT) ret = -EINVAL; /* not implemented yet */

#ifdef REV2
   CASE(VIDIOC_CROPCAP) /* return cropping bounding rectangles */
      struct v4l2_cropcap *cap = arg;
      int type = cap->type;
      memset(cap,0,sizeof(cap));
      cap->type = type;
      switch( type ) {
      case V4L2_BUF_TYPE_VIDEO_CAPTURE:
         lx_capt_max_rect(dp,&cap->bounds.width,&cap->bounds.height);
         lx_capt_max_rect(dp,&cap->defrect.width,&cap->defrect.height);
         break;
      case V4L2_BUF_TYPE_VIDEO_OVERLAY:
         lx_ovly_max_rect(dp,&cap->bounds.width,&cap->bounds.height);
         lx_ovly_max_rect(dp,&cap->defrect.width,&cap->defrect.height);
         break;
      default:
         ret = -EINVAL;
         break;
      }
      if( ret != 0 ) break;
      cap->pixelaspect.numerator = 1;
      cap->pixelaspect.denominator = 1;

   CASE(VIDIOC_G_CROP) /* return cropping rectangle */
      struct v4l2_crop *crop = arg;
      struct v4l2_rect *r = &crop->c;
      int type = crop->type;
      memset(crop,0,sizeof(crop));
      crop->type = type;
      switch( type ) {
      case V4L2_BUF_TYPE_VIDEO_CAPTURE:
         r->left = 0;
         r->top = 0;
         r->width = dp->capt.width;
         r->height = dp->capt.height;
         break;
      case V4L2_BUF_TYPE_VIDEO_OVERLAY:
         r->left = dp->ovly_src.x;
         r->top = dp->ovly_src.y;
         r->width = dp->ovly_src.w;
         r->height = dp->ovly_src.h;
         break;
      default:
         ret = -EINVAL;
         break;
      }
      if( ret != 0 ) break;

   CASE(VIDIOC_S_CROP) /* set cropping rectangle */
      struct v4l2_crop *crop = arg;
      struct v4l2_rect *r = &crop->c;
      int type = crop->type;
      crop->type = type;
      switch( type ) {
      case V4L2_BUF_TYPE_VIDEO_CAPTURE:
         break;
      case V4L2_BUF_TYPE_VIDEO_OVERLAY:
         ret = lx_ovly_set_src_rect(dp,r->left,r->top,r->width,r->height);
         break;
      default:
         ret = -EINVAL;
         break;
      }
      if( ret != 0 ) break;
#endif

   CASE(VIDIOC_QUERYCTRL) /* query video control */
      if (ov7670_i2c_client != NULL) {
         spin_unlock(&dp->lock);
         ret = ov7670_command(ov7670_i2c_client, cmd, arg);
         spin_lock(&dp->lock);
      }
      else if( lx_dev->tv_decoder == NULL ) {
         struct v4l2_queryctrl *ctrl = arg;
         int id = ctrl->id;
         DMSG(3,"  id %d\n",id);
         memset(ctrl,0,sizeof(*ctrl));
         ctrl->id = id;
         switch( id ) {
         case V4L2_CID_BRIGHTNESS:
            strcpy(ctrl->name, "Brightness");
            ctrl->default_value = V4L_DEFAULT_BRIGHTNESS*256;
            break;
         case V4L2_CID_CONTRAST:
            strcpy(ctrl->name, "Contrast");
            ctrl->default_value = V4L_DEFAULT_CONTRAST*256;
            break;
         case V4L2_CID_BLACK_LEVEL:
            strcpy(ctrl->name, "BlackLvl");
            ctrl->default_value = V4L_DEFAULT_BLACKLVL*256;
            break;
         case V4L2_CID_RED_BALANCE:
            strcpy(ctrl->name, "RedGain");
            ctrl->default_value = 32768;
            break;
         case V4L2_CID_BLUE_BALANCE:
            strcpy(ctrl->name, "BlueGain");
            ctrl->default_value = 32768;
            break;
         default:
            ret = -EINVAL;
            break;
         }
         if( ret != 0 ) break;
         ctrl->minimum = 0;
         ctrl->maximum = 65535;
         ctrl->step = 1;
         ctrl->type = V4L2_CTRL_TYPE_INTEGER;
      }
      else
         ret = v4l_component_ioctl(lx_dev->tv_decoder,cmd,arg);

   CASE(VIDIOC_G_CTRL) /* get video control value */
      if (ov7670_i2c_client != NULL) {
         spin_unlock(&dp->lock);
         ret = ov7670_command(ov7670_i2c_client, cmd, arg);
         spin_lock(&dp->lock);
      }
      else {
         printk(KERN_ERR "lx: No OV7670 camera attached\n");
         ret = -EINVAL;
      }
      break;

#if 0
      struct v4l2_control *ctrl = arg;
      int id = ctrl->id;
      ctrl->id = id;
      ret = v4l_get_control(dp,id,&ctrl->value);
      DMSG(3,"  id %d value %d\n",id,ctrl->value);
#endif

   CASE(VIDIOC_S_CTRL) /* set video control value */
      if (ov7670_i2c_client != NULL) {
         spin_unlock(&dp->lock);
         ret = ov7670_command(ov7670_i2c_client, cmd, arg);
         spin_lock(&dp->lock);
      }
      else {
         printk(KERN_ERR "lx: No OV7670 camera attached\n");
         ret = -EINVAL;
      }
      break;
#if 0
      struct v4l2_control *ctrl = arg;
      int id = ctrl->id;
      int value = ctrl->value;
      DMSG(3,"  id %d value %d\n",id,value);
      ret = v4l_set_control(dp,id,value);
#endif

   CASE(VIDIOC_G_TUNER) /* get tuner data */
      ret = v4l_component_ioctl(lx_dev->tv_tuner,cmd,arg);

#ifdef VIDIOC_G_FREQ
   CASE(VIDIOC_G_FREQ) /* get tuner frequency parameters */
      ret = v4l_component_ioctl(lx_dev->tv_tuner,cmd,arg);
#endif

#ifdef VIDIOC_S_FREQ
   CASE(VIDIOC_S_FREQ) /* set tuner frequency parameters */
      ret = v4l_component_ioctl(lx_dev->tv_tuner,cmd,arg);
#endif

   CASE(VIDIOC_STREAMON) /* start capture */
      int type = *(int *)arg;
      DMSG(3, "VIDIOC_STREAMON  type %d\n",type);
      switch( type ) {
#ifdef REV2
      case V4L2_BUF_TYPE_VIDEO_CAPTURE:
#else
      case V4L2_BUF_TYPE_CAPTURE:
#endif
         if( (ret=v4l_check_flipping(dp)) != 0 )
         {
            DMSG(3, "VIDIOC_STREAMON: error v4l_check_flipping returned %d\n",ret);
            break;
         }
         else
            DMSG(5,"VIDIOC_STREAMON: v4l_check_flipping ok (returned %d)\n",ret);

         spin_unlock(&dp->lock);
         ret = v4l_capt_start(dp);
         spin_lock(&dp->lock);
         break;
      default:
         DMSG(1,"VIDIOC_STREAMON: bad stream type %d\n",type);
         ret = -EINVAL;
         break;
      }

   CASE(VIDIOC_STREAMOFF) /* stop capture */
      int type = *(int *)arg;
      DMSG(3, "  type %d\n",type);
      switch( type ) {
#ifdef REV2
      case V4L2_BUF_TYPE_VIDEO_CAPTURE:
#else
      case V4L2_BUF_TYPE_CAPTURE:
#endif
         v4l_capt_stop(dp);
         break;
      default:
         DMSG(1,"VIDIOC_STREAMOFF: bad stream type %d\n",type);
         ret = -EINVAL;
         break;
      }

#ifndef REV2
   CASE(VIDIOC_G_WIN) /* return ovly dst window */
      struct v4l2_window *win = arg;
      memset(win,0,sizeof(win));
      win->x = dp->ovly_dst.x;
      win->y = dp->ovly_dst.y;
      win->width = dp->ovly_dst.w;
      win->height = dp->ovly_dst.h;
      win->chromakey = dp->ovly.chromakey;

   CASE(VIDIOC_S_WIN) /* set ovly dst window */
      struct v4l2_window *win = arg;
      DMSG(3,"  win %d,%d %dx%d %08x\n",
         win->x,win->y,win->width,win->height,win->chromakey);
      ret = lx_ovly_set_dst_rect(dp,win->x,win->y,win->width,win->height);
      if( ret != 0 ) break;
      if( lx_ovly_has_chomakey() ) {
         dp->ovly.chromakey = win->chromakey;
         if( dp->ovly_keymode != 0 )
            lx_ovly_chromakey(dp,win->chromakey,0xffffff,1);
      }
#endif

#ifdef REV2
   CASE(VIDIOC_QUERYCAP) /* query video device capabilities */
      struct v4l2_capability *cap = arg;
      strncpy(&cap->driver[0],&pp->name[0],sizeof(cap->driver));
      cap->driver[sizeof(cap->driver)-1] = 0;
      strncpy(&cap->card[0],v4l_name(),sizeof(cap->card));
      cap->card[sizeof(cap->card)-1] = 0;
#ifdef LINUX_2_6
      strncpy(&cap->bus_info[0],pci_name(pp->pci),sizeof(cap->bus_info));
#else
      strncpy(&cap->bus_info[0],&pp->pci->name[0],sizeof(cap->bus_info));
#endif
      cap->bus_info[sizeof(cap->bus_info)-1] = 0;
#define cat(a,b) a##b
#define hex(n) cat(0x,n)
      cap->version = KERNEL_VERSION((hex(_MAJOR)<<8)|hex(_MINOR),hex(_BL),hex(_BLREV));
#undef hex
#undef cat
      cap->driver[sizeof(cap->driver)-1] = 0;
      cap->capabilities = V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_VIDEO_OVERLAY |
         V4L2_CAP_READWRITE | V4L2_CAP_STREAMING;
      if( pp->tv_audio != NULL ) cap->capabilities |= V4L2_CAP_AUDIO;
      if( pp->tv_tuner != NULL ) cap->capabilities |= V4L2_CAP_TUNER;
      if( pp->tv_decoder != NULL ) cap->capabilities |= V4L2_CAP_RDS_CAPTURE;

/* REV2 */
#else
   CASE(VIDIOC_QUERYCAP) /* query video device capabilities */
      struct v4l2_capability *cap = arg;
      memset(cap,0,sizeof(*cap));
      strncpy(&cap->name[0],v4l_name(),sizeof(cap->name));
      cap->name[sizeof(cap->name)-1] = 0;
      cap->type = V4L2_TYPE_CAPTURE;
      cap->inputs = v4l_num_channels();
      cap->outputs = 0;
      cap->audios = v4l_num_audios();
      lx_capt_max_rect(dp,&cap->maxwidth,&cap->maxheight);
      lx_capt_min_rect(dp,&cap->minwidth,&cap->minheight);
      cap->maxframerate = lx_capt_max_framerate(dp,dp->std_index);
      cap->flags = V4L2_FLAG_READ | V4L2_FLAG_WRITE | V4L2_FLAG_SELECT |
         V4L2_FLAG_STREAMING | V4L2_FLAG_PREVIEW;
      if( pp->tv_tuner != NULL ) cap->flags |= V4L2_FLAG_TUNER;
      if( pp->tv_decoder != NULL ) cap->flags |= V4L2_FLAG_DATA_SERVICE;
#endif

   CASE(VIDIOC_G_PARM) /* get buffering parameters */
#if 0
      if (ov7670_i2c_client != NULL) {
         spin_unlock(&dp->lock);
         ret = ov7670_command(ov7670_i2c_client, cmd, arg);
         spin_lock(&dp->lock);
      }
      else {
         printk(KERN_ERR "lx: No OV7670 camera attached\n");
         ret = -EINVAL;
      }
      break;
#endif
#if 1
      struct v4l2_streamparm *sp = arg;
      int denom;
      int type = sp->type;
      DMSG(3, "VIDIOC_G_PARM:  type %d\n",type);
      memset(sp,0,sizeof(*sp));
      sp->type = type;
      switch( type ) {
#ifdef REV2
      case V4L2_BUF_TYPE_VIDEO_CAPTURE:
#else
      case V4L2_BUF_TYPE_CAPTURE:
#endif
      {
         struct v4l2_captureparm *cap = &sp->parm.capture;
         if( (denom=dp->capt_denom) == 0 ) denom = 1;
         cap->capability = V4L2_CAP_TIMEPERFRAME;
#ifdef REV2
         cap->timeperframe.numerator = dp->capt_num / HZ;
         cap->timeperframe.denominator = dp->capt_denom;
         cap->readbuffers = dp->capt_max_bfrs;
#else
         { long long num = 10000000LL*dp->capt_num;
           long den = denom*HZ;
           do_div(num,den);
           cap->timeperframe = num;
         }
#endif
         break; }
#ifdef REV2
      case V4L2_BUF_TYPE_VIDEO_OUTPUT:
      case V4L2_BUF_TYPE_VIDEO_OVERLAY:
#else
      case V4L2_BUF_TYPE_VIDEOOUT:
#endif
      {
         struct v4l2_outputparm *out = &sp->parm.output;
         if( (denom=dp->ovly_denom) == 0 ) denom = 1;
         out->capability = V4L2_CAP_TIMEPERFRAME;
#ifdef REV2
         out->timeperframe.numerator = dp->ovly_num / HZ;
         out->timeperframe.denominator = dp->ovly_denom;
         out->writebuffers = dp->ovly_max_bfrs;
#else
         { long long num = 10000000LL*dp->ovly_num;
           long den = denom*HZ;
           do_div(num,den);
           out->timeperframe = num;
         }
#endif
         break; }
      default:
         ret = -EINVAL;
         break;
      }
#endif

   CASE(VIDIOC_S_PARM) /* set buffering parameters */
#if 1
      struct v4l2_streamparm *sp = arg;
      int type = sp->type;
      DMSG(3, "VIDIOC_S_PARM:  type %d\n",type);
#endif

#if 0
      if (ov7670_i2c_client != NULL) {
         spin_unlock(&dp->lock);
         ret = ov7670_command(ov7670_i2c_client, cmd, arg);
         spin_lock(&dp->lock);
      }
      else {
         printk(KERN_ERR "lx: No OV7670 camera attached\n");
         ret = -EINVAL;
      }
#endif

#if 1
      switch( type ) {
#ifdef REV2
      case V4L2_BUF_TYPE_VIDEO_CAPTURE:
#else
      case V4L2_BUF_TYPE_CAPTURE:
#endif
      {
         struct v4l2_captureparm *cap = &sp->parm.capture;
         if( (cap->capability&V4L2_CAP_TIMEPERFRAME) != 0 ) {
#ifdef REV2
            dp->capt_num = cap->timeperframe.numerator * HZ;
            dp->capt_denom = cap->timeperframe.denominator;
#else
            dp->capt_num = cap->timeperframe * HZ;
            dp->capt_denom = 10000000;
#endif
            dp->capt_jiffy_start = jiffiez;
            dp->capt_jiffy_sequence = 1;
            DMSG(2, "changing timeperframe to %d/%d\n", dp->capt_num, dp->capt_denom);
         }
#ifdef REV2
#if 0
         dp->capt_max_bfrs = cap->readbuffers;
#endif
#endif
         break; }
#ifdef REV2
      case V4L2_BUF_TYPE_VIDEO_OVERLAY:
      case V4L2_BUF_TYPE_VIDEO_OUTPUT:
#else
      case V4L2_BUF_TYPE_VIDEOOUT:
#endif
      {
         struct v4l2_outputparm *out = &sp->parm.output;
         if( (out->capability&V4L2_CAP_TIMEPERFRAME) != 0 ) {
#ifdef REV2
            dp->ovly_num = out->timeperframe.numerator * HZ;
            dp->ovly_denom = out->timeperframe.denominator;
#else
            dp->ovly_num = out->timeperframe * HZ;
            dp->ovly_denom = 10000000;
#endif
            dp->ovly_jiffy_start = jiffiez;
            dp->ovly_jiffy_sequence = 1;
         }
#ifdef REV2
         dp->ovly_max_bfrs = out->writebuffers;
#endif
         break; }
      default:
         ret = -EINVAL;
         break;
      }
#endif
      break;

#ifndef REV2
   CASE(VIDIOC_ENUM_PIXFMT) /* enumerate capture formats */
      struct v4l2_fmtdesc *fmt = arg;
      ret = lx_capt_get_format(dp,fmt->index,fmt);

   CASE(VIDIOC_ENUM_FBUFFMT) /* enumerate overlay formats */
      struct v4l2_fmtdesc *fmt = arg;
      ret = lx_ovly_get_format(dp,fmt->index,fmt);
#else

   CASE(VIDIOC_ENUM_FMT) /* enumerate capture/overlay formats */
      struct v4l2_fmtdesc *fmt = arg;
      DMSG(3, "  type %d index %d\n",fmt->type,fmt->index);
      switch( fmt->type ) {
      case V4L2_BUF_TYPE_VIDEO_CAPTURE:
         ret = lx_capt_get_format(dp,fmt->index,fmt);
         break;
      case V4L2_BUF_TYPE_VIDEO_OVERLAY:
         ret = lx_ovly_get_format(dp,fmt->index,fmt);
         break;
      default:
         ret = -EINVAL;
      }
#endif

   CASE(VIDIOC_G_FMT) /* return current capture/overlay format */
      struct v4l2_format *fmt = arg;
      int type = fmt->type;
      DMSG(3, "  type %08x\n",type);
      memset(fmt,0,sizeof(*fmt));
      fmt->type = type;
      switch( type ) {
#ifdef REV2
      case V4L2_BUF_TYPE_VIDEO_CAPTURE:
#else
      case V4L2_BUF_TYPE_CAPTURE:
#endif
         fmt->fmt.pix = dp->capt;
         break;
#ifdef REV2
      case V4L2_BUF_TYPE_VIDEO_OVERLAY: {
         int field;
         struct v4l2_rect *s = &fmt->fmt.win.w;
         Rect *r;
         /* yot another problem with the spec */
         r = dp->fb_is_set!=0 ? &dp->ovly_src : &dp->ovly_dst;
         s->left = r->x;   s->top = r->y;
         s->width = r->w;  s->height = r->h;
         /* default is capt format, if not in spec */
         field = v4l_vid_field(&dp->capt);
         field = v4l_vid_field_def(field,dp->capt_dft_field);
#ifdef REV2
         field = v4l_vid_field_def(dp->ovly.field,field);
#endif
         fmt->fmt.win.field = field;
         fmt->fmt.win.chromakey = dp->ovly.chromakey;
         break; }

      case 0x100: { /* return src window */
         struct v4l2_rect *s = &fmt->fmt.win.w;
         Rect *r = &dp->ovly_src;
         s->left = r->x;   s->top = r->y;
         s->width = r->w;  s->height = r->h;
         break; }

      case 0x101: { /* return dst window */
         struct v4l2_rect *s = &fmt->fmt.win.w;
         Rect *r = &dp->ovly_dst;
         s->left = r->x;   s->top = r->y;
         s->width = r->w;  s->height = r->h;
         break; }
#endif
      default:
         DMSG(1,"VIDIOC_G_FMT: bad fmt type\n");
         ret = -EINVAL;
         break;
      }

#ifdef REV2
   CASE(VIDIOC_TRY_FMT) /* test validity of capture/overlay format */
      struct v4l2_format *fmt = arg;
      int type = fmt->type;
      int pixelformat = fmt->fmt.pix.pixelformat;
      DMSG(3, "  type %d pixelformat %08x\n",type,pixelformat);
      switch( type ) {

#ifdef REV2
      case V4L2_BUF_TYPE_VIDEO_CAPTURE:
#else
      case V4L2_BUF_TYPE_CAPTURE:
#endif
      {
         lx_capt_validate_format(dp,&fmt->fmt.pix);
         break; }

#ifdef REV2
      case V4L2_BUF_TYPE_VIDEO_OVERLAY:
      {
         lx_ovly_validate_window(dp,dp->ovly_pixelformat,&fmt->fmt.win);
         break; }
#endif

      default:
         DMSG(1,"VIDIOC_TRY_FMT: bad fmt type %d\n",type);
         ret = -EINVAL;
         break;
      }
#endif

   CASE(VIDIOC_S_FMT) /* apply capture/overlay format */
      struct v4l2_format *fmt = arg;
      int type = fmt->type;
      DMSG(3,"VIDIOC_S_FMT:  type %d\n",type);
      switch( type ) {

#ifdef REV2
      case V4L2_BUF_TYPE_VIDEO_CAPTURE:
#ifdef LINUX_2_6
         if( fp->file != NULL && fp->file != file ) {
            ret = -EBUSY;  break;
         }
#endif
#else
      case V4L2_BUF_TYPE_CAPTURE:
#endif
      if (ov7670_i2c_client != NULL) {
         spin_unlock(&dp->lock);
         ret = ov7670_command(ov7670_i2c_client, cmd, arg);
         spin_lock(&dp->lock);
      }
      else
         printk(KERN_ERR "lx: No OV7670 camera attached\n");

      // From here it is not possible anymore to write in fmt->fmt.pix.pixelformat
      //   without the patch. This address can only be read (new kernel trouble)

      if (ret < 0) {
         DMSG(2, "Error setting the camera parameters\n");
         break;
      }
      {
         int ovly_width, ovly_height, field;
         struct v4l2_pix_format *pix = &fmt->fmt.pix;
         unsigned int pixelformat = pix->pixelformat;
         DMSG(1," S_FMT capt %4.4s %dx%d bpl %d imgsz %d\n",(char*)&pix->pixelformat,
           pix->width, pix->height, pix->bytesperline, pix->sizeimage);
         if( dp->capt_state != capt_state_idle ) {
            ret = -EBUSY;  break;
         }
         if( lx_capt_is_supported_format(dp,pix->pixelformat) == 0 ) {
            DMSG(1,"unsupported format %4.4s\n",(char*)&pix->pixelformat);
            ret = -EINVAL;  break;
         }
         field = v4l_vid_field(pix);
         field = v4l_vid_field_def(field,dp->capt_dft_field);
         if( lx_capt_is_field_valid(dp,pix->pixelformat,field) == 0 ) {
            DMSG(1,"unsupported format/field combination %4.4s/%d\n",
                    (char*)&pix->pixelformat,field);
            ret = -EINVAL;  break;
         }
         if( io != NULL && io->size < pix->sizeimage ) {
            ret = -ENOMEM;  break;
         }
         if( lx_dev->tv_decoder != NULL ) {
            ret = v4l_component_ioctl(lx_dev->tv_decoder,VIDIOC_S_FMT,arg);
            if( ret != 0 ) break;
         }
	// sometimes error "BUG: unable to handle kernel paging request at xxxx" because it's not
	//   possible anymore to write in fmt->fmt.pix.pixelformat since we passed ov7670_command() above
         pix->pixelformat = pixelformat;
         lx_capt_validate_format(dp,pix);
         dp->capt.pixelformat = pix->pixelformat;
         dp->capt.width = pix->width;
         dp->capt.height = pix->height;
         dp->capt.bytesperline = pix->bytesperline;
         dp->capt.sizeimage = pix->sizeimage;
         dp->capt.priv = 0;
         /* bad idea, ovly side affected by capture */
         ovly_width = 2*dp->capt.bytesperline/v4l_vid_byte_depth(pix->pixelformat);
         ovly_height = dp->capt.sizeimage/dp->capt.bytesperline;
#ifdef REV2
         dp->capt.field = pix->field;
         dp->capt.colorspace = pix->colorspace;
         dp->ovly.w.top = dp->ovly.w.left = 0;
         dp->ovly.w.width = ovly_width;
         dp->ovly.w.height = ovly_height;
         dp->ovly.field = pix->field;
#else
         dp->capt.depth = (8*v4l_vid_byte_depth(pix->pixelformat))/2;
         dp->capt.flags = pix->flags;
         dp->ovly.width = ovly_width;
         dp->ovly.height = ovly_height;
#endif
         dp->ovly_src.is_set = 0;
         dp->ovly_src.x = dp->ovly_src.y = 0;
         dp->ovly_src.w = ovly_width;
         dp->ovly_src.h = ovly_height;
         dp->ovly_dst.is_set = 0;
         dp->ovly_dst.x = dp->ovly_dst.y = 0;
         dp->ovly_dst.w = ovly_width;
         dp->ovly_dst.h = ovly_height;
         field = v4l_vid_field(&dp->capt);
         dp->ovly_bobbing = 0;
         if( dp->capt_progressive == 0 ) {
            if( field == V4L2_FIELD_NONE || field == V4L2_FIELD_ALTERNATE )
               dp->ovly_bobbing = 1;
         }
         *pix = dp->capt;
         DMSG(1," S_FMT capt %4.4s %dx%d bpl %d imgsz %d\n",(char*)&pix->pixelformat,
           pix->width, pix->height, pix->bytesperline, pix->sizeimage);
         break; }

#ifdef REV2
      case V4L2_BUF_TYPE_VIDEO_OVERLAY: {
         int bpp;
         unsigned long ovly_phys_addr;
         struct v4l2_window *win = &fmt->fmt.win;
         struct v4l2_rect *w = &win->w;
         lx_ovly_validate_window(dp,dp->ovly_pixelformat,win);
         /* The v4l2 spec is vague on whether overlay s_fmt sets the
            buffer geometry, or sets the graphics geometry.
            Most applications seem to use the latter. */
         if( dp->fb_is_set != 0 ) {
            ret = lx_ovly_set_src_rect(dp,0,0,w->width,w->height);
            if( ret != 0 ) break;
            bpp = (dp->fb_depth+7)/8;
            ovly_phys_addr = dp->fb_phys_addr + w->top*dp->fb_pitch + w->left*bpp;
            lx_ovly_set_bfr(dp,ovly_phys_addr);
            dp->ovly_addr_is_set = 1;
         }
         else {
            ret = lx_ovly_set_dst_rect(dp,w->left,w->top,w->width,w->height);
            if( ret != 0 ) break;
         }
         if( lx_ovly_has_chomakey() != 0 && dp->ovly_keymode != 0 )
            lx_ovly_chromakey(dp,win->chromakey,0xffffff,1);
         dp->ovly.field = win->field;
         *win = dp->ovly;
         DMSG(1,"VIDIOC_S_FMT: set ovly fmt %d,%d %dx%d\n",win->w.left,win->w.top,win->w.width,win->w.height);
         break; }

      case 0x100: { /* set ovly src window */
         struct v4l2_window *win = &fmt->fmt.win;
         struct v4l2_rect *w = &win->w;
         ret = lx_ovly_set_src_rect(dp,w->left,w->top,w->width,w->height);
         DMSG(1,"set ovly src %d,%d %dx%d\n",w->left,w->top,w->width,w->height);
         break; }

      case 0x101: { /* set ovly dst window */
         struct v4l2_window *win = &fmt->fmt.win;
         struct v4l2_rect *w = &win->w;
         ret = lx_ovly_set_dst_rect(dp,w->left,w->top,w->width,w->height);
         DMSG(1,"set ovly dst %d,%d %dx%d\n",w->left,w->top,w->width,w->height);
         break; }

      case 0x102: { /* set ovly buffer geometry */
         struct v4l2_window *win = &fmt->fmt.win;
         struct v4l2_rect *w = &win->w;
         dp->ovly.w = *w;
         DMSG(1,"set ovly geom %d,%d %dx%d\n",w->left,w->top,w->width,w->height);
         break; }
#endif

      default:
         DMSG(1,"VIDIOC_S_FMT: bad fmt type %d\n",type);
         ret = -EINVAL;
         break;
      }

   CASE(VIDIOC_REQBUFS) /* request video buffer memory */
      io_buf *bp;
      int i, imgsz, limit;
      struct v4l2_requestbuffers *req = arg;
      int type = req->type;
      int count = req->count;
#ifdef REV2
      int memory = req->memory;
#endif
      DMSG(3, "VIDIOC_REQBUFS:  type %d req->count %d\n",type,count);
      memset(req,0,sizeof(*req));
      req->type = type;
#ifdef REV2
      req->memory = memory;
#endif
      if( fp->io != NULL ) {
         del_io_queue(&fp->io);
         io = NULL;
      }
      if( count <= 0 ) break;

      switch( type ) {
#ifdef REV2
      case V4L2_BUF_TYPE_VIDEO_CAPTURE:
         imgsz = dp->capt.sizeimage;
         switch( req->memory ) {
         case V4L2_MEMORY_MMAP:
            DMSG(5,"V4L2_BUF_TYPE_VIDEO_CAPTURE: V4L2_MEMORY_MMAP mode selected\n");//@streaming
         case V4L2_MEMORY_USERPTR:
            break;
         default:
            ret = -EINVAL;
            break;
         }
         if( ret != 0 ) break;
#else
      case V4L2_BUF_TYPE_CAPTURE:
         imgsz = dp->capt.sizeimage;
#endif
         limit = v4l_capt_bfr_limit(dp);
         if( count > limit ) count = limit;
         break;
#ifdef REV2
      case V4L2_BUF_TYPE_VIDEO_OVERLAY:
         limit = v4l_ovly_bfr_limit(dp);
         if( count > limit ) count = limit;
         imgsz = v4l_ovly_imagesize(dp);
         break;
#endif
      default:
         imgsz = 0;
         DMSG(1,"VIDIOC_REQBUFS: bad buf type %d\n",type);
         ret = -EINVAL;
         break;
      }
      if( ret != 0 ) break;

      ret = v4l_alloc_bfrs(dp,io_queued,count,imgsz,0);
      if( ret != 0 ) break;
      io = fp->io;
      count = io->mbfrs;

      for( i=count,bp=&io->bfrs[0]; --i>=0; ++bp ) {
         bp->type = type;
#ifdef REV2
         bp->memory = memory;
#endif
      }
      req->count = count;

   CASE(VIDIOC_QUERYBUF) /* query buffer parameters */
      io_buf *bp;
      struct v4l2_buffer *bfr = arg;
      int index = bfr->index;
      int type = bfr->type;
      DMSG(3, "VIDIOC_QUERYBUF: index %d type %d\n",index,type);
      switch( type ) {
#ifdef REV2
      case V4L2_BUF_TYPE_VIDEO_CAPTURE:
#else
      case V4L2_BUF_TYPE_CAPTURE:
#endif
         if( index < 0 || io == NULL || index >= io->mbfrs ) {
            DMSG(1,"VIDIOC_QUERYBUF: bad buf index %d\n",index);
            ret = -EINVAL;  break;
         }
         memset(bfr,0,sizeof(*bfr));
         bfr->index = index;
         bfr->type = type;
         bp = &io->bfrs[index];
         v4l_vid_buffer(dp,bfr,bp,dp->capt.sizeimage,io->size,0);
         break;
#ifdef REV2
      case V4L2_BUF_TYPE_VIDEO_OVERLAY:
         if( index < 0 || io == NULL || index >= io->mbfrs ) {
            DMSG(1,"VIDIOC_QUERYBUF case V4L2_BUF_TYPE_VIDEO_OVERLAY: bad buf index %d\n",index);
            ret = -EINVAL;  break;
         }
         memset(bfr,0,sizeof(*bfr));
         bfr->index = index;
         bfr->type = type;
         bp = &io->bfrs[index];
         v4l_vid_buffer(dp,bfr,bp,v4l_ovly_imagesize(dp),io->size,0);
         break;
#endif
      default:
         ret = -EINVAL;
         break;
      }

   CASE(VIDIOC_G_FBUF) /* get frame buffer parameters */
      int field;
      struct v4l2_framebuffer *fb = arg;
      memset(fb,0,sizeof(*fb));
      fb->fmt.pixelformat = dp->ovly_pixelformat;
      field = v4l_vid_field(&dp->capt);
      field = v4l_vid_field_def(field,dp->capt_dft_field);
#ifdef REV2
      field = v4l_vid_field_def(dp->ovly.field,field);
#endif
      v4l_vid_set_field(&fb->fmt,field);
      v4l_vid_set_colorspace(&fb->fmt,dp->ovly_colorspace);
      if( dp->fb_is_set != 0 ) {
         *(void **)&fb->base = (void *) dp->fb_phys_addr;
         fb->fmt.width = dp->fb_width;
         fb->fmt.height = dp->fb_height;
         fb->fmt.bytesperline = dp->fb_pitch;
      }
      else {
         int y_pitch, uv_pitch, width, height, w, h;
#ifdef REV2
         width = dp->ovly.w.width;
         height = dp->ovly.w.height;
#else
         width = dp->ovly.width;
         height = dp->ovly.height;
#endif
         lx_ovly_min_rect(dp,&w,&h);
         if( width < w ) width = w;
         if( height < h ) height = h;
         y_pitch = lx_ovly_ypitch(dp,width);
         uv_pitch = lx_ovly_uvpitch(dp,width);
         if( v4l_fmt_is_420(dp->ovly_pixelformat) > 0 )
            uv_pitch /= 2; /* mostly bogus */
         *(void **)&fb->base = (void *) vid_mem_base();
         fb->fmt.width = width;
         fb->fmt.height = height;
         fb->fmt.bytesperline = y_pitch + 2*uv_pitch;
      }
      fb->fmt.sizeimage = fb->fmt.bytesperline * fb->fmt.height;
      fb->flags = V4L2_FBUF_FLAG_OVERLAY;
      fb->capability = 0;
      if( lx_ovly_has_chomakey() ) {
         fb->capability |= V4L2_FBUF_CAP_CHROMAKEY;
         if( dp->ovly_keymode != 0 )
            fb->flags |= V4L2_FBUF_FLAG_CHROMAKEY;
      }
#ifndef REV2
      fb->fmt.depth = 8*(fb->fmt.bytesperline/fb->fmt.height);
      if( lx_ovly_has_clipping() ) fb->capability |= V4L2_FBUF_CAP_CLIPPING;
      if( lx_ovly_has_scaleup() )  fb->capability |= V4L2_FBUF_CAP_SCALEUP;
      if( lx_ovly_has_scaledn() )  fb->capability |= V4L2_FBUF_CAP_SCALEDOWN;
#endif

   CASE(VIDIOC_S_FBUF) /* set frame buffer parameters */
      unsigned int orig_pixelformat = dp->ovly_pixelformat;
      struct v4l2_framebuffer *fb = arg;
      unsigned long base = (unsigned long)*(void **)&fb->base;
      unsigned int pixelformat = fb->fmt.pixelformat;
      int capt_colorspace = v4l_vid_colorspace(&dp->capt);
      int colorspace = v4l_vid_colorspace(&fb->fmt);
      DMSG(3, "  base %08lx (%4.4s)\n",base,(char*)&pixelformat);
      if( lx_ovly_is_supported_format(dp,pixelformat) == 0 ) {
         DMSG(1, "VIDIOC_S_FBUF: bad ovly pixelformat (%4.4s)\n",(char*)&pixelformat);
         ret = -EINVAL;  break;
      }
      dp->ovly_pixelformat = pixelformat;
      if( base != 0 ) {
         ret = lx_ovly_set_src_rect(dp,0,0,fb->fmt.width,fb->fmt.height);
         if( ret != 0 ) { dp->ovly_pixelformat = orig_pixelformat;  break;  }
         dp->fb_width = fb->fmt.width;
         dp->fb_height = fb->fmt.height;
         dp->fb_pitch = fb->fmt.bytesperline;
         dp->fb_phys_addr = base;
         dp->fb_is_set = 1;
         lx_ovly_set_bfr(dp,base);
         dp->ovly_addr_is_set = 1;
      }
      else {
         dp->fb_is_set = 0;
         dp->ovly_addr_is_set = 0;
      }
      dp->ovly_colorspace = v4l_vid_colorspace_def(colorspace,capt_colorspace);
      if( lx_ovly_has_chomakey() != 0 ) {
         if( (fb->flags&V4L2_FBUF_FLAG_CHROMAKEY) != 0 ) {
            if( dp->ovly_keymode == 0 ) {
               dp->ovly_keymode = 1;
               lx_ovly_chromakey(dp,dp->ovly.chromakey,0xffffff,1);
            }
         }
         else if( dp->ovly_keymode != 0 ) {
            dp->ovly_keymode = 0;
            lx_ovly_chromakey(dp,0,0,0);
         }
      }

   CASE(VIDIOCGUNIT) /* return device unit numbers */
      struct video_unit *vup = arg;
      vup->video = pp->vid.fp.vd.minor;
      vup->vbi = pp->vbi.fp.vd.minor;
      vup->radio = VIDEO_NO_UNIT;
      vup->audio = VIDEO_NO_UNIT;
      vup->teletext = VIDEO_NO_UNIT;

   CASE(VIDIOC_QBUF) /* queue video memory buffer */
      io_buf *bp;
      struct v4l2_buffer *bfr = arg;
      int index = bfr->index;
      int type = bfr->type;
      DMSG(3, "VIDIOC_QBUF: type %d index %d\n",type,index);
      switch( type ) {
#ifdef REV2
      case V4L2_BUF_TYPE_VIDEO_CAPTURE:
#else
      case V4L2_BUF_TYPE_CAPTURE:
#endif
         if( index < 0 || io == NULL || index >= io->mbfrs ) {
            DMSG(1,"VIDIOC_QBUF: bad buf index %d\n",index);
            ret = -EINVAL;  break;
         }
         if( io->io_type != io_queued ) {
            DMSG(1,"VIDIOC_QBUF: mixed io types\n");
            ret = -EINVAL;  break;
         }
         bp = &io->bfrs[index];
         v4l_qbfr(dp,&io->rd_qbuf,bp,1);
         if(dp==NULL)
            DMSG(1,"VIDIOC_QBUF: dp null\n");
         else if((&dp->fp)->io == NULL)
            DMSG(1,"VIDIOC_QBUF: io null\n");
         if(bp == NULL)
            DMSG(1,"VIDIOC_QBUF: bp null\n");
         break;
#ifdef REV2
      case V4L2_BUF_TYPE_VIDEO_OVERLAY:
         if( index < 0 || io == NULL || index >= io->mbfrs ) {
            DMSG(1,"VIDIOC_QBUF: bad buf index %d\n",index);
            ret = -EINVAL;  break;
         }
         if( io->io_type != io_queued ) {
            DMSG(1,"VIDIOC_QBUF: mixed io types\n");
            ret = -EINVAL;  break;
         }
         bp = &io->bfrs[index];
         if( dp->ovly_state == ovly_state_run ) {
            int qmt = list_empty(&io->wr_qbuf);
            struct timeval *start = &dp->ovly_start_time;
            jiffiez_t jiffy = dp->ovly_start_jiffy;
            bp->jiffies = (bfr->flags&V4L2_BUF_FLAG_TIMECODE) != 0 ?
               v4l_get_ovly_time(dp,bfr,start,jiffy) :
               v4l_ovly_frame_jiffy(dp);
            v4l_qbfr(dp,&io->wr_qbuf,bp,0);
#ifndef NO_TIMESTAMPS
            if( bfr->timestamp.tv_sec != 0 || bfr->timestamp.tv_usec != 0 ||
                (bfr->flags&V4L2_BUF_FLAG_TIMECODE) == 0 || qmt == 0 ) {
               if( dp->ovly_timer_active == 0 ) {
                  dp->ovly_timer_active = 1;
                  v4l_ovly_start_frame_timer(dp);
               }
            }
            else
#endif
               v4l_ovly_post_bfr(dp);
         }
         else
            v4l_qbfr(dp,&io->wr_dqbuf,bp,0);
         break;
#endif
      default:
         DMSG(1,"VIDIOC_QBUF: bad buf type %d\n",type);
         ret = -EINVAL;
         break;
      }

   CASE(VIDIOC_DQBUF) /* dequeue video memory buffer */
      io_buf *bp;
      jiffiez_t jiffy;
      struct v4l2_buffer *bfr = arg;
#ifdef LINUX_2_6
      int nonblock = file->f_flags & O_NONBLOCK;
#else
      int nonblock = 0;
#endif
      int type = bfr->type;
      DMSG(3,"VIDIOC_DQBUF  type %d\n",type);
      switch( type ) {
#ifdef REV2
      case V4L2_BUF_TYPE_VIDEO_CAPTURE:
#else
      case V4L2_BUF_TYPE_CAPTURE:
#endif
         if( io == NULL ) {
            DMSG(1,"VIDIOC_DQBUF: no buffers queed\n");
            ret = -EINVAL;  break;
         }
         if( (ret=v4l_dqbfr(dp,&io->rd_dqbuf,&bp,nonblock,&dp->capt_wait)) != 0 )
         {
           DMSG(5,"VIDIOC_DQBUF: v4l_dqbfr returned %d (-EAGAIN = -11 / -ERESTARTSYS = -512)\n", ret);
           if( ret == -EAGAIN ) ret = 0;
           break;
         }
         v4l_vid_buffer(dp,bfr,bp,dp->capt.sizeimage,io->size,V4L2_BUF_FLAG_TIMECODE);
         jiffy = bp->jiffies - dp->capt_start_jiffy;
         v4l_set_capt_time(dp,bfr,&dp->capt_start_time,jiffy);
         v4l_set_capt_timecode(dp,&bfr->timecode,jiffy);
         DMSG(4,"  dqbuf %d\n",bp->index);
         break;
#ifdef REV2
      case V4L2_BUF_TYPE_VIDEO_OVERLAY:
         if( io == NULL ) {
            DMSG(1,"VIDIOC_DQBUF: no bfrs q'd\n");
            ret = -EINVAL;  break;
         }
         if( (ret=v4l_dqbfr(dp,&io->wr_dqbuf,&bp,nonblock,&dp->ovly_wait)) != 0 ) break;
         v4l_vid_buffer(dp,bfr,bp,v4l_ovly_imagesize(dp),io->size,V4L2_BUF_FLAG_TIMECODE);
         v4l_set_capt_time(dp,bfr,&dp->ovly_start_time,jiffies-dp->ovly_start_jiffy);
         DMSG(4,"VIDIOC_DQBUF:  dqbuf %d\n",bp->index);
         break;
#endif
      default:
         DMSG(1,"VIDIOC_DQBUF: bad buf type %d\n",type);
         ret = -EINVAL;
         break;
      }

#ifdef REV2
   CASE(VIDIOC_OVERLAY) /* enable/disable overlay */
#else
   CASE(VIDIOC_PREVIEW) /* enable/disable overlay */
#endif
      int *enable = arg;
      if( *enable != 0 ) {
         if( dp->ovly_src.is_set == 0 ) {
            ret = lx_ovly_set_src_rect(dp,0,0,dp->capt.width,dp->capt.height);
            if( ret != 0 ) break;
         }
         if( dp->ovly_dst.is_set == 0 ) {
            ret = lx_ovly_set_dst_rect(dp,0,0,dp->capt.width,dp->capt.height);
            if( ret != 0 ) break;
         }
         if( (ret=v4l_check_flipping(dp)) != 0 ) break;
         if( (io=fp->io) == NULL && dp->ovly_addr_is_set == 0 ) {
            DMSG(1,"VIDIOC_OVERLAY: no overlay buffer\n");
            ret = -EINVAL;  break;
         }
         if( io != NULL && dp->ovly_state == ovly_state_idle ) {
            if( io->io_type == io_flipped ) {
               v4l_qbfrs(dp,&io->rd_qbuf);
               if( (ret=v4l_capt_start(dp)) != 0 ) break;
            }
            else if( v4l_ovly_post_bfr(dp) != 0 )
               lx_ovly_set_bfr(dp,io->bfrs[0].phys_addr);
         }
         if( (ret=v4l_ovly_start(dp)) != 0 ) break;
      }
      else {
         v4l_ovly_stop(dp);
         if( io != NULL && io->io_type == io_flipped ) {
            v4l_capt_stop(dp);
            del_io_queue(&fp->io);
         }
         vid_stats(dp);
      }

   CASE(VIDIOC_ALPHA_WINDOW) /* enable/disable/configure alpha window */
      struct v4l2_alpha_window *awin = arg;
      ret = lx_set_alpha_window(awin);

   HCTIWS

   spin_unlock(&dp->lock);

   DMSG(4,"VIDIOC_ALPHA_WINDOW:  ret = %d\n",ret);
   return ret;
}

#undef SWITCH
#undef CASE
#undef DEFAULT
#undef HCTIWS

/** v4l2_ioctl - rev1 video ioctl */
#ifndef REV2
static int
v4l2_ioctl(void *id, unsigned int cmd, void *arg)
{
   struct video_device *v = (struct video_device *)id;
   return vid_ioctl(v,cmd,arg);
}
#endif

/** vid_mmap - map video memory into user space
 *  must be possible to map buffers individually or as
 *  congolmerate buffers.
 */

#ifdef LINUX_2_6
static int
vid_mmap(struct file *file, struct vm_area_struct *vma)
{
   struct video_device *v = video_devdata(file);
   unsigned long offset = vma->vm_pgoff * PAGE_SIZE;
   unsigned long start = vma->vm_start;
   unsigned long size  = vma->vm_end - vma->vm_start;
#else
static int
vid_mmap(struct video_device *v, const char *adr, unsigned long size)
{
   unsigned long offset  = 0;
   unsigned long start = (unsigned long)adr;
   struct vm_area_struct *vma = NULL;
#endif
   V4LDevice *pp = (V4LDevice *)video_get_drvdata(v);
   VidDevice *dp = &pp->vid;
   FilePriv *fp = &dp->fp;
   io_queue *io = fp->io;
   if( size == 0 ) {
      DMSG(1, "vid_mmap zero size\n");
      return -EINVAL;
   }
   DMSG(3, "vid_mmap start %08lx size %08lx offset %08lx\n",start,size,offset);
   if( io == NULL ) {
      int ret, imgsz, bfrsz, count;
      int limit, capt_limit, ovly_limit;
      imgsz = dp->capt.sizeimage;
      capt_limit = v4l_capt_bfr_limit(dp);
      ovly_limit = v4l_ovly_bfr_limit(dp);
      limit = capt_limit + ovly_limit;
      ret = v4l_alloc_bfrs(dp,io_queued,limit,imgsz,0);
      if( ret != 0 ) return ret;
      io = fp->io;
      count = io->mbfrs;
      bfrsz = io->size;
      if( size > bfrsz*count ) {
         DMSG(1, "vid_mmap size too big\n");
         del_io_queue(&fp->io);
         return -EINVAL;
      }
   }
   return mmap_bufs(io,offset,start,size,vma);
}

/** v4l2_mmap - rev1 mmap */
#ifndef REV2
static int
v4l2_mmap(void *id, struct vm_area_struct *vma)
{
   struct video_device *v = (struct video_device *)id;
   unsigned long start = vma->vm_start;
   unsigned long size  = vma->vm_end - vma->vm_start;
   return vid_mmap(v,(const char *)start,size);
}
#endif

/** vid_poll - poll device
 *  returns blocked/ready status of devices
 */
#ifdef LINUX_2_6
static unsigned int
vid_poll(struct file *file, poll_table *wait)
{
   struct video_device *v = video_devdata(file);
#else
static unsigned int
vid_poll(struct video_device *v, struct file *file, poll_table *wait)
{
#endif
   unsigned int mask = 0;
   V4LDevice *pp = (V4LDevice *)video_get_drvdata(v);
   VidDevice *dp = &pp->vid;
   FilePriv *fp = &dp->fp;
   io_queue *io = fp->io;
   DMSG(3,"\n");

   if( io == NULL ) return POLLNVAL;

   if( dp->capt_state == capt_state_run )
      poll_wait(file,&dp->capt_wait,wait);
   //if( dp->ovly_state == ovly_state_run )
   //   poll_wait(file,&dp->ovly_wait,wait);

   switch( io->io_type ) {
   case io_queued:
      if( list_empty(&io->rd_dqbuf) == 0 )
         mask |= POLLIN | POLLRDNORM;
      if( list_empty(&io->wr_dqbuf) == 0 )
         mask |= POLLOUT | POLLRDNORM;
      break;
   }
   return mask;
}

/** v4l2_poll - rev1 poll */
#ifndef REV2
static int
v4l2_poll(void *id, struct file *file, poll_table *wait)
{
   struct video_device *v = (struct video_device *)id;
   return vid_poll(v,file,wait);
}
#endif

#ifndef VID_HARDWARE_LX3
#define VID_HARDWARE_LX3 100
#endif

#ifdef LINUX_2_6
static struct v4l2_file_operations vid_fops = {
   .owner   = THIS_MODULE,
   .open    = vid_open,
   .release = vid_close,
   .read    = vid_read,
   .write   = vid_write,
   .mmap    = vid_mmap,
   .ioctl   = vid_ioctl,
   .poll    = vid_poll,
   //  .llseek  = no_llseek
};
#endif

#ifndef REV2
static int
v4l2_init_done(struct v4l2_device *v)
{
   DMSG(3,"\n");
   return 0;
}

static struct v4l2_device v4l2_vid_template = {
   .name   = "AMD GeodeLX V4L2 Capture driver",
   .vfl_type   = V4L2_TYPE_CAPTURE,
   .minor  = VIDEO_MINOR,
   .open   = v4l2_open,
   .close  = v4l2_close,
   .read   = v4l2_read,
   .write  = v4l2_write,
   .ioctl  = v4l2_ioctl,
   .mmap   = v4l2_mmap,
   .poll   = v4l2_poll,
   .initialize = v4l2_init_done,
   .priv = NULL
};
#endif

static struct video_device v4l_vid_template = {
  // .owner   = THIS_MODULE,
   .name    = "LX",
   .vfl_type    = VID_TYPE_CAPTURE | VID_TYPE_OVERLAY,
//   .hardware = VID_HARDWARE_LX3,
#ifdef LINUX_2_6
   .fops    = &vid_fops,
#else
   .open    = vid_open,
   .close   = vid_close,
   .read    = vid_read,
   .write   = vid_write,
   .ioctl   = vid_ioctl,
   .poll    = vid_poll,
   .mmap    = vid_mmap,
#endif
#ifdef REV2
   .release = v4l_dev_release,
#endif
   .minor =   -1,
};

#ifdef LINUX_2_6
static int
vbi_open(struct file *file)
{
   struct video_device *v = video_devdata(file);
   int flags = file->f_flags;
#else
static int
vbi_open(struct video_device *v,int flags)
{
#endif
   V4LDevice *pp = (V4LDevice *)video_get_drvdata(v);
   VbiDevice *dp = &pp->vbi;
   FilePriv *fp = &dp->fp;
   int ret = 0;
   DMSG(2,"vbi_open: %#x %s %d\n",flags,&v->name[0],fp->open_files);

   if( fp->open_files == 0 ) {
      fp->type = ft_vbi;
      fp->file = NULL;
   }

   ++fp->open_files;
   return ret;
}

#ifndef REV2
static int
vbi2_open(struct v4l2_device *v2, int flags, void **idptr)
{
   struct video_device *v = (struct video_device *)v2->priv;
   int ret = vid_open(v,flags);
   if( ret == 0 ) *idptr = v;
   return ret;
}
#endif

#ifdef LINUX_2_6
static int
vbi_close(struct file *file)
{
   struct video_device *v = video_devdata(file);
#else
static void
vbi_close(struct video_device *v)
{
   struct file *file = (struct file *)-1;
#endif
   V4LDevice *pp = (V4LDevice *)video_get_drvdata(v);
   VbiDevice *dp = &pp->vbi;
   FilePriv *fp = &dp->fp;
   DMSG(2,"vbi_close: %s %d\n",&v->name[0],fp->open_files);

   if( --fp->open_files == 0 || fp->file == file ) {
      del_io_queue(&fp->io);
      fp->type = ft_none;
      fp->file = NULL;
   }
#ifdef LINUX_2_6
   return 0;
#endif
}

#ifndef REV2
static void
vbi2_close(void *id)
{
   struct video_device *v = (struct video_device *)id;
   vbi_close(v);
}
#endif

#ifdef LINUX_2_6
static ssize_t
vbi_write(struct file *file, const char __user * buf, size_t count, loff_t *ppos)
{
   struct video_device *v = video_devdata(file);
   int nonblock = O_NONBLOCK & file->f_flags;
#else
static long
vbi_write(struct video_device *v, const char *buf, unsigned long count, int nonblock)
{
#endif
   V4LDevice *pp = (V4LDevice *)video_get_drvdata(v);
   DMSG(2,"vbi_write: %s %lu %d\n",&v->name[0],(unsigned long)count,nonblock);
   return lx_vbi_write(&pp->vbi,(char *)buf,count,nonblock);
}

#ifndef REV2
static long
vbi2_write(void *id, const char *buf, unsigned long count, int nonblock)
{
   struct video_device *v = (struct video_device *)id;
   return vbi_write(v,buf,count,nonblock);
}
#endif

#ifdef LINUX_2_6
static ssize_t
vbi_read(struct file *file, char *buf, size_t count, loff_t *ppos)
{
   struct video_device *v = video_devdata(file);
   int nonblock = O_NONBLOCK & file->f_flags;
#else
static long
vbi_read(struct video_device *v, char *buf, unsigned long count, int nonblock)
{
#endif
   V4LDevice *pp = (V4LDevice *)video_get_drvdata(v);
   return lx_vbi_read(&pp->vbi,buf,count,nonblock);
}

#ifndef REV2
static long
vbi2_read(void *id, char *bfr, unsigned long count, int nonblock)
{
   struct video_device *v = (struct video_device *)id;
   return vbi_read(v,bfr,count,nonblock);
}
#endif

#ifndef BTTV_VBISIZE
#define BTTV_VBISIZE            _IOR('v' , BASE_VIDIOCPRIVATE+8, int)
#endif

#ifdef LINUX_2_6
static int
vbi_ioctl(struct file *file, unsigned int cmd, unsigned long ul_arg)
{
   void *arg = (void *)ul_arg;
   struct video_device *v = video_devdata(file);
#else
static int
vbi_ioctl(struct video_device *v, unsigned int cmd, void *arg)
{
#endif
   V4LDevice *pp = (V4LDevice *)video_get_drvdata(v);
   VbiDevice *dp = &pp->vbi;
   FilePriv *fp = &dp->fp;
   long size;
   DMSG(2,"vbi_ioctl: %s %08x\n",&v->name[0],cmd);

   switch ( cmd ) {

   case VIDIOCGCAP: { /* vbi ioctl VIDIOCGCAP */
      struct video_capability *b = arg;
      memset(b,0,sizeof(*b));
      strncpy(&b->name[0],v4l_name(),sizeof(b->name));
      b->name[sizeof(b->name)-1] = 0;
      b->type = v->vfl_type;
      return 0; }

   case BTTV_VBISIZE: /* vbi ioctl BTTV_VBISIZE */
      size = lx_vbi_get_buf_size(fp);
      return size;

   case VIDIOCGVBIFMT: { /* vbi ioctl VIDIOCGVBIFMT */
      struct vbi_format vbi_f;

      vbi_f.sampling_rate = 28636363;
      vbi_f.samples_per_line = dp->width;
      vbi_f.sample_format = VIDEO_PALETTE_RAW;
      vbi_f.start[0] = dp->start;
      vbi_f.start[1] = dp->start;
      vbi_f.start[0] = 10;
      vbi_f.start[1] = 272;
      vbi_f.count[0] = dp->height;
      vbi_f.count[1] = dp->height;
      vbi_f.flags = 0;
      return 0; }

   case VIDIOCSVBIFMT: /* vbi ioctl VIDIOCSVBIFMT */
      return -EBUSY;

   default:
      return -EINVAL;
   }
}

#ifndef REV2
static int
vbi2_ioctl(void *id, unsigned int cmd, void *arg)
{
   struct video_device *v = (struct video_device *)id;
   return vbi_ioctl(v,cmd,arg);
}
#endif

#ifdef LINUX_2_6
static unsigned int
vbi_poll(struct file *file, poll_table *wait)
{
   struct video_device *v = video_devdata(file);
#else
static unsigned int
vbi_poll(struct video_device *v, struct file *file, poll_table *wait)
{
#endif
   V4LDevice *pp = (V4LDevice *)video_get_drvdata(v);
   return lx_vbi_poll(&pp->vbi,file,wait);
}

#ifndef REV2
static int
vbi2_poll(void *id, struct file *file, poll_table *wait)
{
   struct video_device *v = (struct video_device *)id;
   return vbi_poll(v,file,wait);
}
#endif

#ifdef LINUX_2_6
static struct v4l2_file_operations vbi_fops = {
   .owner   = THIS_MODULE,
   .open    = vbi_open,
   .release = vbi_close,
   .read    = vbi_read,
   .write   = vbi_write,
   .ioctl   = vbi_ioctl,
   .poll    = vbi_poll,
//   .llseek  = no_llseek
};
#endif

#ifndef REV2
static int
vbi_init_done(struct v4l2_device *v)
{
   DMSG(3,"\n");
   return 0;
}

static struct v4l2_device v4l2_vbi_template = {
   .name   = "AMD GeodeLX V4L2 Capture driver",
   .type   = V4L2_TYPE_VBI,
   .minor  = VBI_MINOR,
   .open   = vbi2_open,
   .close  = vbi2_close,
   .read   = vbi2_read,
   .write  = vbi2_write,
   .ioctl  = vbi2_ioctl,
   .poll   = vbi2_poll,
   .initialize = vbi_init_done,
   .priv = NULL
};

static void
v4l2_file_priv_init(FilePriv *fp,struct v4l2_device *vd2)
{
   struct video_device *v = &fp->vd;
   struct v4l2_device *v2 = &fp->vd2;
   memcpy(v2,vd2,sizeof(*v2));
   v2->priv = v;
}
#endif

static struct video_device v4l_vbi_template = {
  // .owner   = THIS_MODULE,
   .name    = "LX",
   .vfl_type    = VID_TYPE_CAPTURE | VID_TYPE_OVERLAY,
//   .hardware = VID_HARDWARE_LX3,
#ifdef LINUX_2_6
   .fops    = &vbi_fops,
#else
   .open    = vbi_open,
   .close   = vbi_close,
   .read    = vbi_read,
   .write   = vbi_write,
   .ioctl   = vbi_ioctl,
   .poll    = vbi_poll,
#endif
#ifdef REV2
   .release = v4l_dev_release,
#endif
   .minor   =   -1,
};

static void
v4l_file_priv_init(FilePriv *fp,V4LDevice *dp,struct video_device *vd)
{
   struct video_device *v = &fp->vd;
   fp->type = ft_none;
   fp->io = NULL;
   init_MUTEX(&fp->open_sem);
   memcpy(v,vd,sizeof(*v));
   video_set_drvdata(v,dp);
   ++dp->refcnt;
   fp->file = NULL;
   fp->open_files = 0;
#ifdef TWO_DEVS
   v = &fp->vd1;
   memcpy(v,vd,sizeof(*v));
   video_set_drvdata(v,dp);
#endif
}

#ifdef CONFIG_PROC_FS

static int
proc_lxv4l2_vid_read(char *page,char **start,off_t off,int count,int *eof,void *data)
{
   VidDevice *dp = (VidDevice *)data;
   int len = 0;
   // len += sprintf(page+len,"lock %lu\n",spin_is_locked(&dp->lock));
   len += sprintf(page+len,"fp {\n");
   len += sprintf(page+len,"  type %d\n",dp->fp.type);
   len += sprintf(page+len,"  file %p\n",dp->fp.file);
   if( dp->fp.io != NULL ) {
      io_queue *io = dp->fp.io;
      io_buf *bp = &io->bfrs[0];
      len += sprintf(page+len,"  io {\n");
      len += sprintf(page+len,"    io_type %d\n",io->io_type);
      // len += sprintf(page+len,"    lock %lu\n",spin_is_locked(&io->lock));
      len += sprintf(page+len,"    rd_qbuf {\n");
      len += sprintf(page+len,"      next %p\n",io->rd_qbuf.next);
      len += sprintf(page+len,"      prev %p\n",io->rd_qbuf.prev);
      len += sprintf(page+len,"    }\n");
      len += sprintf(page+len,"    rd_dqbuf {\n");
      len += sprintf(page+len,"      next %p\n",io->rd_dqbuf.next);
      len += sprintf(page+len,"      prev %p\n",io->rd_dqbuf.prev);
      len += sprintf(page+len,"    }\n");
      len += sprintf(page+len,"    wr_qbuf {\n");
      len += sprintf(page+len,"      next %p\n",io->wr_qbuf.next);
      len += sprintf(page+len,"      prev %p\n",io->wr_qbuf.prev);
      len += sprintf(page+len,"    }\n");
      len += sprintf(page+len,"    wr_dqbuf {\n");
      len += sprintf(page+len,"      next %p\n",io->wr_dqbuf.next);
      len += sprintf(page+len,"      prev %p\n",io->wr_dqbuf.prev);
      len += sprintf(page+len,"    }\n");
      len += sprintf(page+len,"    sequence %lu\n",io->sequence);
      len += sprintf(page+len,"    size %lu\n",io->size);
      len += sprintf(page+len,"    stream_bfr %p\n",io->stream_bfr);
      len += sprintf(page+len,"    count %d\n",io->count);
      len += sprintf(page+len,"    offset %d\n",io->offset);
      len += sprintf(page+len,"    nbfrs %d\n",io->nbfrs);
      len += sprintf(page+len,"    mbfrs %d\n",io->mbfrs);
      len += sprintf(page+len,"    bfrs {\n");
      len += sprintf(page+len,"      bfrq {\n");
      len += sprintf(page+len,"        next %p\n",bp->bfrq.next);
      len += sprintf(page+len,"        prev %p\n",bp->bfrq.prev);
      len += sprintf(page+len,"      }\n");
      len += sprintf(page+len,"      type %d\n",bp->type);
      len += sprintf(page+len,"      index %d\n",bp->index);
      len += sprintf(page+len,"      memory %d\n",bp->memory);
      len += sprintf(page+len,"      flags %d\n",bp->flags);
      len += sprintf(page+len,"      sequence %lu\n",bp->sequence);
      len += sprintf(page+len,"      jiffies %llu\n",(unsigned long long)bp->jiffies);
      len += sprintf(page+len,"      start %lu\n",bp->start);
      len += sprintf(page+len,"      vmem %p\n",bp->vmem);
      len += sprintf(page+len,"      phys_addr %#lx\n",bp->phys_addr);
      len += sprintf(page+len,"      offset %lu\n",bp->offset);
      len += sprintf(page+len,"      bfrp %p\n",bp->bfrp);
      len += sprintf(page+len,"    }\n");
      len += sprintf(page+len,"  }\n");
   }
   else {
      len += sprintf(page+len,"  io %p\n",dp->fp.io);
   }
   len += sprintf(page+len,"  type %d\n",dp->fp.type);
   len += sprintf(page+len,"  vd {\n");
   len += sprintf(page+len,"    name {\"%s\"}\n",&dp->fp.vd.name[0]);
//   len += sprintf(page+len,"    type %d\n",dp->fp.vd.type);
#ifdef REV2
//   len += sprintf(page+len,"    type2 %d\n",dp->fp.vd.type2);
#endif
//   len += sprintf(page+len,"    hardware %d\n",dp->fp.vd.hardware);
   len += sprintf(page+len,"    minor %d\n",dp->fp.vd.minor);
   len += sprintf(page+len,"  }\n");
#ifndef REV2
   len += sprintf(page+len,"  vd2 {\n");
   len += sprintf(page+len,"    name {\"%s\"}\n",&dp->fp.vd2.name[0]);
   len += sprintf(page+len,"    type %d\n",dp->fp.vd2.type);
   len += sprintf(page+len,"    minor %d\n",dp->fp.vd2.minor);
   len += sprintf(page+len,"  }\n");
#endif
   len += sprintf(page+len,"  open_files %d\n",dp->fp.open_files);
   len += sprintf(page+len,"}\n");
   len += sprintf(page+len,"capt {\n");
   len += sprintf(page+len,"  width %u\n",dp->capt.width);
   len += sprintf(page+len,"  height %u\n",dp->capt.height);
#ifndef REV2
   len += sprintf(page+len,"  depth %u\n",dp->capt.depth);
#endif
   len += sprintf(page+len,"  pixelformat %u\n",dp->capt.pixelformat);
#ifndef REV2
   len += sprintf(page+len,"  flags %u\n",dp->capt.flags);
#else
   len += sprintf(page+len,"  field %u\n",dp->capt.field);
   len += sprintf(page+len,"  colorspace %u\n",dp->capt.colorspace);
#endif
   len += sprintf(page+len,"  bytesperline %u\n",dp->capt.bytesperline);
   len += sprintf(page+len,"  sizeimage %u\n",dp->capt.sizeimage);
   len += sprintf(page+len,"  priv %u\n",dp->capt.priv);
   len += sprintf(page+len,"}\n");
   len += sprintf(page+len,"capt_state %d\n",dp->capt_state);
   len += sprintf(page+len,"std_index %d\n",dp->std_index);
   len += sprintf(page+len,"std_num %d\n",dp->std_num);
   len += sprintf(page+len,"std_denom %d\n",dp->std_denom);
#ifdef PM_EVENTS
   len += sprintf(page+len,"pm_capt_state %d\n",dp->pm_capt_state);
   len += sprintf(page+len,"pm_ovly_state %d\n",dp->pm_ovly_state);
#endif
   len += sprintf(page+len,"capt_max_bfrs %d\n",dp->capt_max_bfrs);
   len += sprintf(page+len,"capt_dft_field %d\n",dp->capt_dft_field);
   len += sprintf(page+len,"capt_port_size %d\n",dp->capt_port_size);
   len += sprintf(page+len,"capt_inverted %d\n",dp->capt_inverted);
   len += sprintf(page+len,"capt_progressive %d\n",dp->capt_progressive);
   len += sprintf(page+len,"capt_420 %d\n",dp->capt_420);
   len += sprintf(page+len,"capt_planar %d\n",dp->capt_planar);
   len += sprintf(page+len,"capt_toggle %d\n",dp->capt_toggle);
   len += sprintf(page+len,"capt_vip_version %d\n",dp->capt_vip_version);
   len += sprintf(page+len,"capt_vip_task %d\n",dp->capt_vip_task);
   len += sprintf(page+len,"capt_vip_ey_offset %#x\n",dp->capt_ey_offset);
   len += sprintf(page+len,"capt_vip_oy_offset %#x\n",dp->capt_oy_offset);
   len += sprintf(page+len,"capt_vip_u_offset %#x\n",dp->capt_u_offset);
   len += sprintf(page+len,"capt_vip_v_offset %#x\n",dp->capt_v_offset);
   len += sprintf(page+len,"capt_field %d\n",dp->capt_field);
   len += sprintf(page+len,"capt_frame %d\n",dp->capt_frame);
   len += sprintf(page+len,"capt_stalled %d\n",dp->capt_stalled);
   len += sprintf(page+len,"capt_skipped %d\n",dp->capt_skipped);
   len += sprintf(page+len,"capt_dropped %d\n",dp->capt_dropped);
   len += sprintf(page+len,"capt_sequence %d\n",dp->capt_sequence);
   len += sprintf(page+len,"capt_hue %d\n",dp->capt_hue);
   len += sprintf(page+len,"capt_brightness %d\n",dp->capt_brightness);
   len += sprintf(page+len,"capt_contrast %d\n",dp->capt_contrast);
   len += sprintf(page+len,"capt_saturation %d\n",dp->capt_saturation);
   len += sprintf(page+len,"capt_obfr %p\n",dp->capt_obfr);
   len += sprintf(page+len,"capt_ebfr %p\n",dp->capt_ebfr);
   len += sprintf(page+len,"capt_olch %p\n",dp->capt_olch);
   len += sprintf(page+len,"capt_elch %p\n",dp->capt_elch);
   len += sprintf(page+len,"capt_onxt %p\n",dp->capt_onxt);
   len += sprintf(page+len,"capt_enxt %p\n",dp->capt_enxt);
   len += sprintf(page+len,"capt_num %d\n",dp->capt_num);
   len += sprintf(page+len,"capt_denom %d\n",dp->capt_denom);
   len += sprintf(page+len,"capt_start_time{\n");
   len += sprintf(page+len,"  tv_sec %lu\n",dp->capt_start_time.tv_sec);
   len += sprintf(page+len,"  tv_usec %lu\n",dp->capt_start_time.tv_usec);
   len += sprintf(page+len,"}\n");
   len += sprintf(page+len,"capt_start_jiffy %llu\n",(unsigned long long)dp->capt_start_jiffy);
   len += sprintf(page+len,"capt_jiffy_start %llu\n",(unsigned long long)dp->capt_jiffy_start);
   len += sprintf(page+len,"capt_jiffy_sequence %lu\n",dp->capt_jiffy_sequence);
   len += sprintf(page+len,"capt_wait {\n");
   // len += sprintf(page+len,"  lock %lu\n",spin_is_locked(&dp->capt_wait.lock));
   len += sprintf(page+len,"  task_list {\n");
   len += sprintf(page+len,"    next %p\n",dp->capt_wait.task_list.next);
   len += sprintf(page+len,"    prev %p\n",dp->capt_wait.task_list.prev);
   len += sprintf(page+len,"  }\n");
   len += sprintf(page+len,"}\n");
   len += sprintf(page+len,"capt_addr_is_set %d\n",dp->capt_addr_is_set);
   len += sprintf(page+len,"capt_phys_addr %#lx\n",dp->capt_phys_addr);
   len += sprintf(page+len,"ovly {\n");
#ifndef REV2
   len += sprintf(page+len,"  x %d\n",dp->ovly.x);
   len += sprintf(page+len,"  y %d\n",dp->ovly.y);
   len += sprintf(page+len,"  width %d\n",dp->ovly.width);
   len += sprintf(page+len,"  height %d\n",dp->ovly.height);
#else
   len += sprintf(page+len,"  w {\n");
   len += sprintf(page+len,"    top %d\n",dp->ovly.w.top);
   len += sprintf(page+len,"    left %d\n",dp->ovly.w.left);
   len += sprintf(page+len,"    width %d\n",dp->ovly.w.width);
   len += sprintf(page+len,"    height %d\n",dp->ovly.w.height);
   len += sprintf(page+len,"  }\n");
   len += sprintf(page+len,"  field %d\n",dp->ovly.field);
#endif
   len += sprintf(page+len,"  chromakey %d\n",dp->ovly.chromakey);
   len += sprintf(page+len,"  clips %p\n",dp->ovly.clips);
   len += sprintf(page+len,"  clipcount %d\n",dp->ovly.clipcount);
   len += sprintf(page+len,"  bitmap %p\n",dp->ovly.bitmap);
   len += sprintf(page+len,"}\n");
   len += sprintf(page+len,"fb_phys_addr %#lx\n",dp->fb_phys_addr);
   len += sprintf(page+len,"fb_is_set %d\n",dp->fb_is_set);
   len += sprintf(page+len,"fb_width %d\n",dp->fb_width);
   len += sprintf(page+len,"fb_height %d\n",dp->fb_height);
   len += sprintf(page+len,"fb_depth %d\n",dp->fb_depth);
   len += sprintf(page+len,"fb_pitch %d\n",dp->fb_pitch);
   len += sprintf(page+len,"ovly_src {\n");
   len += sprintf(page+len,"  is_set %d\n",dp->ovly_src.is_set);
   len += sprintf(page+len,"  x %d\n",dp->ovly_src.x);
   len += sprintf(page+len,"  y %d\n",dp->ovly_src.y);
   len += sprintf(page+len,"  w %d\n",dp->ovly_src.w);
   len += sprintf(page+len,"  h %d\n",dp->ovly_src.h);
   len += sprintf(page+len,"}\n");
   len += sprintf(page+len,"ovly_dst {\n");
   len += sprintf(page+len,"  is_set %d\n",dp->ovly_dst.is_set);
   len += sprintf(page+len,"  x %d\n",dp->ovly_dst.x);
   len += sprintf(page+len,"  y %d\n",dp->ovly_dst.y);
   len += sprintf(page+len,"  w %d\n",dp->ovly_dst.w);
   len += sprintf(page+len,"  h %d\n",dp->ovly_dst.h);
   len += sprintf(page+len,"}\n");
   len += sprintf(page+len, "ovly_state %d\n",dp->ovly_state);
   len += sprintf(page+len,"ovly_wait {\n");
   // len += sprintf(page+len,"  lock %lu\n",spin_is_locked(&dp->ovly_wait.lock));
   len += sprintf(page+len,"  task_list {\n");
   len += sprintf(page+len,"    next %p\n",dp->ovly_wait.task_list.next);
   len += sprintf(page+len,"    prev %p\n",dp->ovly_wait.task_list.prev);
   len += sprintf(page+len,"  }\n");
   len += sprintf(page+len,"}\n");
   len += sprintf(page+len,"ovly_addr_is_set %d\n",dp->ovly_addr_is_set);
   len += sprintf(page+len,"ovly_max_bfrs %d\n",dp->ovly_max_bfrs);
   len += sprintf(page+len,"ovly_bfr %p\n",dp->ovly_bfr);
   len += sprintf(page+len,"ovly_num %d\n",dp->ovly_num);
   len += sprintf(page+len,"ovly_denom %d\n",dp->ovly_denom);
   len += sprintf(page+len,"ovly_timer {\n");
#ifndef LINUX_2_6
   len += sprintf(page+len,"  list {\n");
   len += sprintf(page+len,"    next %p\n",dp->ovly_timer.list.next);
   len += sprintf(page+len,"    prev %p\n",dp->ovly_timer.list.prev);
#else
   len += sprintf(page+len,"  entry {\n");
   len += sprintf(page+len,"    next %p\n",dp->ovly_timer.entry.next);
   len += sprintf(page+len,"    prev %p\n",dp->ovly_timer.entry.prev);
#endif
   len += sprintf(page+len,"  }\n");
   len += sprintf(page+len,"  expires %lu\n",dp->ovly_timer.expires);
   len += sprintf(page+len,"  data %lu\n",dp->ovly_timer.data);
   len += sprintf(page+len,"  function %p\n",dp->ovly_timer.function);
   len += sprintf(page+len,"}\n");
   len += sprintf(page+len,"ovly_timer_active %d\n",dp->ovly_timer_active);
   len += sprintf(page+len,"ovly_start_time {\n");
   len += sprintf(page+len,"  tv_sec %lu\n",dp->ovly_start_time.tv_sec);
   len += sprintf(page+len,"  tv_usec %lu\n",dp->ovly_start_time.tv_usec);
   len += sprintf(page+len,"}\n");
   len += sprintf(page+len,"ovly_start_jiffy %llu\n",(unsigned long long)dp->ovly_start_jiffy);
   len += sprintf(page+len,"ovly_jiffy_start %llu\n",(unsigned long long)dp->ovly_jiffy_start);
   len += sprintf(page+len,"ovly_jiffy_sequence %lu\n",dp->ovly_jiffy_sequence);
   len += sprintf(page+len,"ovly_frames %d\n",dp->ovly_frames);
   len += sprintf(page+len,"ovly_dropped %d\n",dp->ovly_dropped);
   len += sprintf(page+len,"ovly_sequence %d\n",dp->ovly_sequence);
   len += sprintf(page+len,"ovly_phys_addr %#lx\n",dp->ovly_phys_addr);
   len += sprintf(page+len,"ovly_pixelformat %u\n",dp->ovly_pixelformat);
   len += sprintf(page+len,"ovly_obfr %p\n",dp->ovly_obfr);
   len += sprintf(page+len,"ovly_ebfr %p\n",dp->ovly_ebfr);
   len += sprintf(page+len,"ovly_format %d\n",dp->ovly_format);
   len += sprintf(page+len,"ovly_oy_offset %d\n",dp->ovly_oy_offset);
   len += sprintf(page+len,"ovly_ey_offset %d\n",dp->ovly_ey_offset);
   len += sprintf(page+len,"ovly_u_offset %d\n",dp->ovly_u_offset);
   len += sprintf(page+len,"ovly_v_offset %d\n",dp->ovly_v_offset);
   len += sprintf(page+len,"ovly_y_pitch %d\n",dp->ovly_y_pitch);
   len += sprintf(page+len,"ovly_uv_pitch %d\n",dp->ovly_uv_pitch);
   len += sprintf(page+len,"ovly_width %d\n",dp->ovly_width);
   len += sprintf(page+len,"ovly_height %d\n",dp->ovly_height);
   len += sprintf(page+len,"ovly_bobbing %d\n",dp->ovly_bobbing);
   len += sprintf(page+len,"ovly_bob_dy %d\n",dp->ovly_bob_dy);
   len += sprintf(page+len,"ovly_colorspace %d\n",dp->ovly_colorspace);
   len += sprintf(page+len,"ovly_keymode %d\n",dp->ovly_keymode);
   len += sprintf(page+len,"ovly_blackLvl %d\n",dp->ovly_blackLvl);
   len += sprintf(page+len,"ovly_redGain %d\n",dp->ovly_redGain);
   len += sprintf(page+len,"ovly_blueGain %d\n",dp->ovly_blueGain);
   len += sprintf(page+len,"ovly_brightness %d\n",dp->ovly_brightness);
   len += sprintf(page+len,"ovly_contrast %d\n",dp->ovly_contrast);
   len += sprintf(page+len,"jiffy %llu\n",(unsigned long long)jiffies);
   return len;
}
#endif

#ifdef MODULE
int debug = 0;
#endif

int irq = -1;
#ifdef PM_EVENTS
static int use_pm = 1;
#endif

#ifdef MODULE
MODULE_AUTHOR("William Morrow / Momtchil Momtchev");
MODULE_DESCRIPTION("AMD LX LINUX V4L2");
MODULE_LICENSE("GPL");
module_param(debug,int,0644);
module_param(irq,int,0644);
#ifdef PM_EVENTS
module_param(use_pm,int,0644);
#endif
module_param(capt_max_bfrs,int,0644);
module_param(ovly_max_bfrs,int,0644);
module_param(capt_formats,int,0644);
module_param(ovly_formats,int,0644);
module_param(plain_sensor,int,0644);
#endif

#ifdef PM_EVENTS
void
v4l_pm_resume(unsigned long data)
{
   VidDevice *vid;
   V4LDevice *pp = lx_dev;
   if( pp == NULL ) return;
    DMSG(1,"PM_RESUME timer\n");
   vid = &pp->vid;
   if( vid->pm_capt_state == capt_state_run ) {
      DMSG(1,"PM_RESUME capt\n");
      v4l_capt_stop(vid);
      udelay(1000);
      v4l_capt_start(vid);
   }
   if( vid->pm_ovly_state == ovly_state_run ) {
      Rect *src = &vid->ovly_src;
      Rect *dst = &vid->ovly_dst;
      DMSG(1,"PM_RESUME ovly\n");
      v4l_ovly_stop(vid);
      udelay(1000);
      lx_ovly_set_src_rect(vid,src->x,src->y,src->w,src->h);
      lx_ovly_set_dst_rect(vid,dst->x,dst->y,dst->w,dst->h);
      v4l_ovly_start(vid);
   }
}

int
v4l_pm_callback(struct pm_dev *dev, pm_request_t rqst, void *data)
{
   VidDevice *vid;
   V4LDevice *pp = lx_dev;
   if( pp == NULL ) return 0;
   DMSG(1,"pm event %x\n",rqst);
   vid = &pp->vid;
   switch (rqst) {
   case PM_SUSPEND:
      DMSG(1,"PM_SUSPEND\n");
      vid->pm_capt_state = vid->capt_state;
      if( vid->capt_state != capt_state_idle )
         v4l_capt_stop(vid);
      vid->pm_ovly_state = vid->ovly_state;
      if( vid->ovly_state != ovly_state_idle )
         v4l_ovly_stop(vid);
      break;
   case PM_RESUME:
      DMSG(1,"PM_RESUME\n");
#if 1
      init_timer(&vid->pm_timer);
      vid->pm_timer.function = v4l_pm_resume;
      vid->pm_timer.data = (unsigned long)vid;
      vid->pm_timer.expires = jiffies + HZ*10;
      add_timer(&vid->pm_timer);
#else
      v4l_pm_resume(0);
#endif
      break;
   }
   return 0;
}
#endif


#ifdef LINUX_2_6
struct i2c_client *i2c_get_client(int driver_id, int adapter_id,
    struct i2c_client *prev)
{
   return NULL;
}
#endif

static void
v4l_i2c_devinit(struct i2c_client **pcp,int bus_id)
{
   struct i2c_client *client = i2c_get_client(bus_id,0,0);
   *pcp = NULL;
   if( client != NULL ) {
      DMSG(1,"Found : %s id %02x addr %02x\n",&client->name[0],bus_id,client->addr<<1);
      if( v4l_component_ioctl(client,V4L2_COMPONENT_CHECK,NULL) != 0 ) {
         DMSG(1," %s failed component_check\n",&client->name[0]);
         return;
      }
#ifndef LINUX_2_6
      i2c_inc_use_client(client);
#endif
   }
   *pcp = client;
}

static void
v4l_i2c_devexit(struct i2c_client *cp)
{
   if( cp != NULL ) {
#ifndef LINUX_2_6
      i2c_dec_use_client(cp);
#endif
   }
}

#if 0
V4LDevice *v4l_dev_init(void)
{
   int ret, field, size, count, nbfrs;
   VidDevice *vid;
   VbiDevice *vbi;
   V4LDevice *pp;
   printk(KERN_INFO AMD_VERSION "\n");
   pp = kmalloc(sizeof(*pp),GFP_KERNEL);
   if( pp == NULL ) return NULL;
   memset(pp,0,sizeof(*pp));
   strncpy(&pp->name[0],DRVNAME,sizeof(pp->name));
   pp->name[sizeof(pp->name)-1] = 0;
   pp->debug_level = debug;
   pp->irq = irq;
   lx_dev = pp;
   DMSG(1,"\n");
   vid = &pp->vid;
#ifdef CONFIG_PROC_FS
   lx_dev->proc = proc_mkdir(PROC_PATH,0);
   if( lx_dev->proc != NULL )
      create_proc_read_entry("video",0,lx_dev->proc,proc_lxv4l2_vid_read,vid);
#endif
   if( (ret=vid_mem_init()) != 0 ) return NULL;
   vbi = &pp->vbi;
   spin_lock_init(&vbi->lock);
   spin_lock_init(&vid->lock);
   vid->std_index = STD_SD_NTSC;
   vid->std_num = v4l_std_data[vid->std_index].num;
   vid->std_denom = v4l_std_data[vid->std_index].denom;
   v4l_default_capt_format(vid,&vid->capt);
   vid->capt_state = capt_state_idle;
   size = phys_bfrsz(vid->capt.sizeimage);
   count = v4l_max_buffers(size,vid);
   DMSG(5,"v4l_dev_init: count: %d V4L_MIN_BFRS: %d\n", count, V4L_MIN_BFRS);
   nbfrs = count - V4L_MIN_BFRS;
   if( nbfrs < V4L_MIN_BFRS ) nbfrs = V4L_MIN_BFRS;
   vid->capt_max_bfrs = capt_max_bfrs>0 ? capt_max_bfrs : nbfrs;
   vid->capt_formats = capt_formats;
   vid->capt_dft_field = V4L2_FIELD_INTERLACED;
   vid->capt_progressive = lx_capt_std_progressive(vid,vid->std_index);
   vid->capt_420 = v4l_fmt_is_420(vid->capt.pixelformat);
   vid->capt_planar = v4l_fmt_is_planar(vid->capt.pixelformat);
   vid->capt_toggle = capt_toggle_none;
   vid->capt_vip_version = lx_capt_std_vip_version(vid,vid->std_index);
   vid->capt_port_size = lx_capt_std_port_size(vid,vid->std_index);
   vid->capt_inverted = 0;
   vid->capt_vip_task = CAPT_TASK_A;
   vid->capt_field = vid->capt_frame = 0;
   vid->capt_stalled = vid->capt_skipped = 0;
   vid->capt_dropped = vid->capt_sequence = 0;
   vid->capt_ey_offset = vid->capt_oy_offset = 0;
   vid->capt_u_offset = vid->capt_v_offset = 0;
   vid->capt_hue = V4L_DEFAULT_HUE;
   vid->capt_brightness = V4L_DEFAULT_BRIGHTNESS;
   vid->capt_contrast = V4L_DEFAULT_CONTRAST;
   vid->capt_saturation = V4L_DEFAULT_SATURATION;
   vid->capt_obfr = vid->capt_ebfr = NULL;
   vid->capt_olch = vid->capt_elch = NULL;
   vid->capt_onxt = vid->capt_enxt = NULL;
   vid->capt_num = vid->std_num * HZ;
   vid->capt_denom = vid->std_denom;
   vid->capt_start_time.tv_sec = 0;
   vid->capt_start_time.tv_usec = 0;
   vid->capt_start_jiffy = 0;
   vid->capt_jiffy_start = 0;
   vid->capt_jiffy_sequence = 0;
   vid->capt_addr_is_set = 0;
   vid->capt_phys_addr = 0;
   v4l_default_ovly_format(vid,&vid->ovly);
   vid->ovly_state = ovly_state_idle;
   vid->ovly_src.is_set = 0;
   vid->ovly_src.x = vid->ovly_src.y = 0;
   vid->ovly_dst.is_set = 0;
   vid->ovly_dst.x = vid->ovly_dst.y = 0;
   DMSG(5,"v4l_dev_init: count: %d vid->capt_max_bfrs: %d\n", count, vid->capt_max_bfrs);
   nbfrs = count - vid->capt_max_bfrs;
   if( nbfrs < V4L_MIN_BFRS ) nbfrs = V4L_MIN_BFRS;
   vid->ovly_max_bfrs = ovly_max_bfrs>0 ? ovly_max_bfrs : nbfrs;
   vid->ovly_formats = ovly_formats;
   vid->ovly_num = vid->std_num * HZ;
   vid->ovly_denom = vid->std_denom;
#ifdef REV2
   field = vid->ovly.field;
#else
   field = v4l_vid_field(&vid->capt);
#endif
   if( lx_capt_std_progressive(vid,vid->std_index) == 0 &&
       (field == V4L2_FIELD_ALTERNATE || field == V4L2_FIELD_NONE) )
      vid->ovly_denom *= 2;
   vid->ovly_bfr = NULL;
   init_timer(&vid->ovly_timer);
   vid->ovly_timer.function = v4l_ovly_frame_timer;
   vid->ovly_timer.data = (unsigned long)vid;
   vid->ovly_timer_active = 0;
   vid->ovly_start_time.tv_sec = 0;
   vid->ovly_start_time.tv_usec = 0;
   vid->ovly_start_jiffy = 0;
   vid->ovly_jiffy_start = 0;
   vid->ovly_jiffy_sequence = 0;
   vid->ovly_frames = 0;
   vid->ovly_dropped = vid->ovly_sequence = 0;
   vid->ovly_addr_is_set = 0;
   vid->ovly_phys_addr = 0;
   vid->fb_is_set = 0;
   vid->fb_phys_addr = 0;
   vid->fb_width = 0;
   vid->fb_height = 0;
   vid->fb_depth = 0;
   vid->fb_pitch = 0;
   vid->ovly_brightness = V4L_DEFAULT_BRIGHTNESS*256;
   vid->ovly_contrast = V4L_DEFAULT_CONTRAST*256;
   vid->ovly_blackLvl = V4L_DEFAULT_BLACKLVL*256;
   vid->ovly_redGain = 32768;
   vid->ovly_blueGain = 32768;
   vid->ovly_pixelformat = vid->capt.pixelformat;
   vid->ovly_obfr = vid->ovly_ebfr = NULL;
   vid->ovly_format = 0;
   vid->ovly_ey_offset = vid->ovly_oy_offset = 0;
   vid->ovly_u_offset = vid->ovly_v_offset = 0;
   vid->ovly_y_pitch = vid->ovly_uv_pitch = 0;
   vid->ovly_width = vid->ovly_height = 0;
   vid->ovly_bobbing = vid->ovly_bob_dy = 0;
   vid->ovly_colorspace = v4l_vid_colorspace(&vid->capt);
   lx_ovly_chromakey(vid,0,0,vid->ovly_keymode=0);
   vid->ovly_src.w = vid->ovly_dst.w = vid->capt.width;
   vid->ovly_src.h = vid->ovly_dst.h = vid->capt.height;
   init_waitqueue_head(&vid->capt_wait);
   init_waitqueue_head(&vid->ovly_wait);
   pp->refcnt = 0;
   v4l_file_priv_init(&pp->vid.fp,pp,&v4l_vid_template);
   v4l_file_priv_init(&pp->vbi.fp,pp,&v4l_vbi_template);
#ifdef REV2
   video_register_device(&pp->vid.fp.vd,VFL_TYPE_GRABBER,-1);
#ifdef TWO_DEVS
   video_register_device(&pp->vid.fp.vd1,VFL_TYPE_GRABBER,-1);
#endif
   video_register_device(&pp->vbi.fp.vd,VFL_TYPE_VBI,-1);
#else
   v4l2_file_priv_init(&pp->vid.fp,&v4l2_vid_template);
   v4l2_file_priv_init(&pp->vbi.fp,&v4l2_vbi_template);
   v4l2_register_device(&pp->vid.fp.vd2);
   v4l2_register_device(&pp->vbi.fp.vd2);
#endif

   v4l_i2c_devinit(&pp->tv_decoder,I2C_DECODER_ID);
   v4l_i2c_devinit(&pp->tv_tuner,I2C_TUNER_ID);
   v4l_i2c_devinit(&pp->tv_audio,I2C_AUDIO_ID);

#ifdef PM_EVENTS
   if( use_pm != 0 ) {
      struct pm_dev *pmdev;
      pmdev = pm_register(PM_PCI_DEV,PM_PCI_ID(pp->pci),v4l_pm_callback);
      if( pmdev != NULL ) {
         DMSG(1,"register PM callback\n");
      }
      else {
         DMSG(1,"register PM failed\n");
         use_pm = 0;
      }
   }
#endif
   return pp;
}

void v4l_dev_cleanup(void)
{
   V4LDevice *pp = lx_dev;
   if( pp == NULL ) return;
   DMSG(1,"\n");
   lx_capt_stop(&pp->vid);
   lx_ovly_stop(&pp->vid);
   lx_vbi_stop(&pp->vbi);
#ifdef CONFIG_PROC_FS
   if( pp->proc != NULL ) {
      remove_proc_entry("map" ,pp->proc);
      remove_proc_entry("video" ,pp->proc);
      pp->proc = NULL;
      remove_proc_entry(PROC_PATH,NULL);
   }
#endif
   v4l_i2c_devexit(pp->tv_decoder);
   v4l_i2c_devexit(pp->tv_tuner);
   v4l_i2c_devexit(pp->tv_audio);
#ifdef REV2
   video_unregister_device(&pp->vid.fp.vd);
#ifdef TWO_DEVS
   video_unregister_device(&pp->vid.fp.vd1);
#endif
   video_unregister_device(&pp->vbi.fp.vd);
#else
   v4l2_unregister_device(&pp->vid.fp.vd2);
   v4l2_unregister_device(&pp->vbi.fp.vd2);
   vid_mem_exit();
   kfree(pp);
#endif
#ifdef PM_EVENTS
   if( use_pm != 0 )
      pm_unregister_all(v4l_pm_callback);
#endif
   lx_dev = NULL;
}

static struct pci_device_id lxv4l2_ids[] = {
        { PCI_VENDOR_ID_AMD, PCI_DEVICE_ID_LX3_VIDEO, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0, },
        { 0, }
};

static struct pci_driver lxv4l2_driver = {
   .name = "lxv4l2",
   .id_table = lxv4l2_ids,
   .probe = lx_init,
   .remove = __devexit_p(lx_exit),
};


static int __init lxv4l2_init(void)
{
    int retval;

    retval = pci_register_driver(&lxv4l2_driver);
    if (retval)
        printk(KERN_ERR "lxv4l2: Unable to register pci driver.\n");
    else
        printk(KERN_INFO "lxv4l2: Driver installed.\n");

    return retval;
}

static void __exit lxv4l2_exit(void)
{
    pci_unregister_driver(&lxv4l2_driver);
    printk(KERN_INFO "lxv4l2: module cleanup complete\n");
}

module_init(lxv4l2_init);
module_exit(lxv4l2_exit);
#endif

static int
v4l_dev_init(void)
{
   int ret, field, size, count, nbfrs;
   VidDevice *vid;
   VbiDevice *vbi;
   V4LDevice *pp;

   printk(KERN_INFO AMD_VERSION "\n");

   pp = kmalloc(sizeof(*pp),GFP_KERNEL);
   if( pp == NULL ) return -ENOMEM;
   memset(pp,0,sizeof(*pp));
   strncpy(&pp->name[0],DRVNAME,sizeof(pp->name));
   pp->name[sizeof(pp->name)-1] = 0;
   pp->debug_level = debug;
   pp->irq = irq;
   lx_dev = pp;

   DMSG(1,"\n");

   vid = &pp->vid;
#ifdef CONFIG_PROC_FS
   lx_dev->proc = proc_mkdir(PROC_PATH,0);
   if( lx_dev->proc != NULL )
      create_proc_read_entry("video",0,lx_dev->proc,proc_lxv4l2_vid_read,vid);
#endif
   if( (ret=lx_init(pp)) != 0 ) return ret;
   if( (ret=vid_mem_init()) != 0 ) return ret;
   vbi = &pp->vbi;
   spin_lock_init(&vbi->lock);
   spin_lock_init(&vid->lock);

   vid->std_index = STD_SD_NTSC;
   vid->std_num = v4l_std_data[vid->std_index].num;
   vid->std_denom = v4l_std_data[vid->std_index].denom;
   v4l_default_capt_format(vid,&vid->capt);
   vid->capt_state = capt_state_idle;
   size = phys_bfrsz(vid->capt.sizeimage);
   count = v4l_max_buffers(size,vid);
   DMSG(5,"v4l_dev_init: count: %d V4L_MIN_BFRS: %d\n", count, V4L_MIN_BFRS);
   nbfrs = count - V4L_MIN_BFRS;
   if( nbfrs < V4L_MIN_BFRS ) nbfrs = V4L_MIN_BFRS;
   vid->capt_max_bfrs = capt_max_bfrs>0 ? capt_max_bfrs : nbfrs;
   vid->capt_formats = capt_formats;
   vid->capt_dft_field = V4L2_FIELD_INTERLACED;
   vid->capt_progressive = lx_capt_std_progressive(vid,vid->std_index);
   vid->capt_420 = v4l_fmt_is_420(vid->capt.pixelformat);
   vid->capt_planar = v4l_fmt_is_planar(vid->capt.pixelformat);
   vid->capt_toggle = capt_toggle_none;
   vid->capt_vip_version = lx_capt_std_vip_version(vid,vid->std_index);
   vid->capt_port_size = lx_capt_std_port_size(vid,vid->std_index);
   vid->capt_inverted = 0;
   vid->capt_vip_task = CAPT_TASK_A;
   vid->capt_field = vid->capt_frame = 0;
   vid->capt_stalled = vid->capt_skipped = 0;
   vid->capt_dropped = vid->capt_sequence = 0;
   vid->capt_ey_offset = vid->capt_oy_offset = 0;
   vid->capt_u_offset = vid->capt_v_offset = 0;
   vid->capt_hue = V4L_DEFAULT_HUE;
   vid->capt_brightness = V4L_DEFAULT_BRIGHTNESS;
   vid->capt_contrast = V4L_DEFAULT_CONTRAST;
   vid->capt_saturation = V4L_DEFAULT_SATURATION;
   vid->capt_obfr = vid->capt_ebfr = NULL;
   vid->capt_olch = vid->capt_elch = NULL;
   vid->capt_onxt = vid->capt_enxt = NULL;
   vid->capt_num = vid->std_num * HZ;
   vid->capt_denom = vid->std_denom;
   vid->capt_start_time.tv_sec = 0;
   vid->capt_start_time.tv_usec = 0;
   vid->capt_start_jiffy = 0;
   vid->capt_jiffy_start = 0;
   vid->capt_jiffy_sequence = 0;
   vid->capt_addr_is_set = 0;
   vid->capt_phys_addr = 0;
   v4l_default_ovly_format(vid,&vid->ovly);
   vid->ovly_state = ovly_state_idle;
   vid->ovly_src.is_set = 0;
   vid->ovly_src.x = vid->ovly_src.y = 0;
   vid->ovly_dst.is_set = 0;
   vid->ovly_dst.x = vid->ovly_dst.y = 0;
   DMSG(5,"v4l_dev_init: count: %d vid->capt_max_bfrs: %d\n", count, vid->capt_max_bfrs);
   nbfrs = count - vid->capt_max_bfrs;
   if( nbfrs < V4L_MIN_BFRS ) nbfrs = V4L_MIN_BFRS;
   vid->ovly_max_bfrs = ovly_max_bfrs>0 ? ovly_max_bfrs : nbfrs;
   vid->ovly_formats = ovly_formats;
   vid->ovly_num = vid->std_num * HZ;
   vid->ovly_denom = vid->std_denom;
#ifdef REV2
   field = vid->ovly.field;
#else
   field = v4l_vid_field(&vid->capt);
#endif
   if( lx_capt_std_progressive(vid,vid->std_index) == 0 &&
       (field == V4L2_FIELD_ALTERNATE || field == V4L2_FIELD_NONE) )
      vid->ovly_denom *= 2;
   vid->ovly_bfr = NULL;
   init_timer(&vid->ovly_timer);
   vid->ovly_timer.function = v4l_ovly_frame_timer;
   vid->ovly_timer.data = (unsigned long)vid;
   vid->ovly_timer_active = 0;
   vid->ovly_start_time.tv_sec = 0;
   vid->ovly_start_time.tv_usec = 0;
   vid->ovly_start_jiffy = 0;
   vid->ovly_jiffy_start = 0;
   vid->ovly_jiffy_sequence = 0;
   vid->ovly_frames = 0;
   vid->ovly_dropped = vid->ovly_sequence = 0;
   vid->ovly_addr_is_set = 0;
   vid->ovly_phys_addr = 0;
   vid->fb_is_set = 0;
   vid->fb_phys_addr = 0;
   vid->fb_width = 0;
   vid->fb_height = 0;
   vid->fb_depth = 0;
   vid->fb_pitch = 0;
   vid->ovly_brightness = V4L_DEFAULT_BRIGHTNESS*256;
   vid->ovly_contrast = V4L_DEFAULT_CONTRAST*256;
   vid->ovly_blackLvl = V4L_DEFAULT_BLACKLVL*256;
   vid->ovly_redGain = 32768;
   vid->ovly_blueGain = 32768;
   vid->ovly_pixelformat = vid->capt.pixelformat;
   vid->ovly_obfr = vid->ovly_ebfr = NULL;
   vid->ovly_format = 0;
   vid->ovly_ey_offset = vid->ovly_oy_offset = 0;
   vid->ovly_u_offset = vid->ovly_v_offset = 0;
   vid->ovly_y_pitch = vid->ovly_uv_pitch = 0;
   vid->ovly_width = vid->ovly_height = 0;
   vid->ovly_bobbing = vid->ovly_bob_dy = 0;
   vid->ovly_colorspace = v4l_vid_colorspace(&vid->capt);
   lx_ovly_chromakey(vid,0,0,vid->ovly_keymode=0);
   vid->ovly_src.w = vid->ovly_dst.w = vid->capt.width;
   vid->ovly_src.h = vid->ovly_dst.h = vid->capt.height;
   init_waitqueue_head(&vid->capt_wait);
   init_waitqueue_head(&vid->ovly_wait);
   pp->refcnt = 0;
   v4l_file_priv_init(&pp->vid.fp,pp,&v4l_vid_template);
   v4l_file_priv_init(&pp->vbi.fp,pp,&v4l_vbi_template);
#ifdef REV2
   video_register_device(&pp->vid.fp.vd,VFL_TYPE_GRABBER,-1);
#ifdef TWO_DEVS
   video_register_device(&pp->vid.fp.vd1,VFL_TYPE_GRABBER,-1);
#endif
   video_register_device(&pp->vbi.fp.vd,VFL_TYPE_VBI,-1);
#else
   v4l2_file_priv_init(&pp->vid.fp,&v4l2_vid_template);
   v4l2_file_priv_init(&pp->vbi.fp,&v4l2_vbi_template);
   v4l2_register_device(&pp->vid.fp.vd2);
   v4l2_register_device(&pp->vbi.fp.vd2);
#endif

   v4l_i2c_devinit(&pp->tv_decoder,I2C_DECODER_ID);
   v4l_i2c_devinit(&pp->tv_tuner,I2C_TUNER_ID);
   v4l_i2c_devinit(&pp->tv_audio,I2C_AUDIO_ID);
   ov7670_i2c_client = NULL;
   ov7670_mod_init();

   if (plain_sensor == 7670) {
      pp->sensor = i2c_get_client(I2C_OV7670_ID,0,0);
      if (pp->sensor != NULL) {
         printk(KERN_INFO "OV7670 sensor connected\n");
      } else
         printk(KERN_INFO "OV7670 sensor not found\n");

   }

#ifdef PM_EVENTS
   if( use_pm != 0 ) {
      struct pm_dev *pmdev;
      pmdev = pm_register(PM_PCI_DEV,PM_PCI_ID(pp->pci),v4l_pm_callback);
      if( pmdev != NULL ) {
         DMSG(1,"register PM callback\n");
      }
      else {
         DMSG(1,"register PM failed\n");
         use_pm = 0;
      }
   }
#endif
   return 0;
}

static void
v4l_dev_cleanup(void)
{
   V4LDevice *pp = lx_dev;
   if( pp == NULL ) return;
   DMSG(1,"\n");
   lx_capt_stop(&pp->vid);
   lx_ovly_stop(&pp->vid);
   lx_vbi_stop(&pp->vbi);
#ifdef CONFIG_PROC_FS
   if( pp->proc != NULL ) {
      remove_proc_entry("map" ,pp->proc);
      remove_proc_entry("video" ,pp->proc);
      pp->proc = NULL;
      remove_proc_entry(PROC_PATH,NULL);
   }
#endif
   v4l_i2c_devexit(pp->tv_decoder);
   v4l_i2c_devexit(pp->tv_tuner);
   v4l_i2c_devexit(pp->tv_audio);
   if (ov7670_i2c_client != NULL)
      ov7670_mod_exit();
   ov7670_i2c_client = NULL;
#ifdef REV2
   video_unregister_device(&pp->vid.fp.vd);
#ifdef TWO_DEVS
   video_unregister_device(&pp->vid.fp.vd1);
#endif
   video_unregister_device(&pp->vbi.fp.vd);
#else
   v4l2_unregister_device(&pp->vid.fp.vd2);
   v4l2_unregister_device(&pp->vbi.fp.vd2);
   lx_exit(pp);
   vid_mem_exit();
   kfree(pp);
#endif
#ifdef PM_EVENTS
   if( use_pm != 0 )
      pm_unregister_all(v4l_pm_callback);
#endif
   lx_dev = NULL;
}

module_init(v4l_dev_init);
module_exit(v4l_dev_cleanup);
