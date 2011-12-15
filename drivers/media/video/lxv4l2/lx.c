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
 * lx device level interface
 * for v4l/2 linux driver
 * interfaces to cimarron hardware api
 * </DOC_AMD_STD>  */

#include "v4l.h"

/* handled errors */
#define ERR_INT_ENABLES \
 ( VIP_INT_FIFO_WRAP | VIP_INT_ACTIVE_PIXELS | \
   VIP_INT_LONGLINE | VIP_INT_CLOCK_INPUT )


static int is_reported = 1;

/* device parameters by standard index */
static struct s_std_parms {
  int width;
  int height;
  int progressive;
  int rate;
  int vip_version;
  int port_size;
} lx_std_parms[STD_IDS_SZ] = {
  [STD_SD_NTSC]  = {  720,  480, 0, 30, 1, 8  },
  [STD_SD_PAL]   = {  720,  576, 0, 25, 1, 8  },
  [STD_HD_480P]  = {  720,  480, 1, 60, 2, 16 },
  [STD_HD_525P]  = {  720,  576, 1, 60, 2, 16 },
  [STD_HD_720P]  = { 1280,  720, 1, 60, 2, 16 },
  [STD_HD_1080I] = { 1920, 1080, 0, 60, 2, 16 },
  [STD_HD_VGA]  =  {  640,  480, 1, 30, 1, 8 },
  [STD_HD_QVGA] =  {  320,  240, 1, 30, 1, 8 },
};

/* cimarron buffer parameters by task id */
static struct {
  int enables;
  int task_buffers;
  int vip_buffers;
  int vip_odd_buffers;
  int vip_even_buffers;
} vip_task_data[CAPT_TASK_SZ] = {
  [CAPT_TASK_A] = { VIP_ENABLE_TASKA, VIP_BUFFER_TASK_A, VIP_BUFFER_A,
                    VIP_BUFFER_A_ODD, VIP_BUFFER_A_EVEN },
  [CAPT_TASK_B] = { VIP_ENABLE_TASKB, VIP_BUFFER_TASK_B, VIP_BUFFER_B,
                    VIP_BUFFER_B_ODD, VIP_BUFFER_B_EVEN },
};

/* just finished a field when starting other field */
static unsigned long capt_bfr_mask[capt_toggle_sz] = {
  [capt_toggle_none] = 0,
  [capt_toggle_odd] = VIP_INT_START_EVEN,
  [capt_toggle_even] = VIP_INT_START_ODD,
  [capt_toggle_both] = VIP_INT_START_ODD,
  [capt_toggle_vblank] = VIP_INT_START_VBLANK,
};

/** lx_capt_resume - queue capture buffers to vip */
int
lx_capt_resume(VidDevice *dp,io_queue *io)
{
   io_buf *op, *ep;
   int eidx, oidx, vip_buffers;
   int task = dp->capt_vip_task;
   int task_buffers = vip_task_data[task].task_buffers;
   VIPINPUTBUFFER *vip_inpbfr = &dp->vip_inpbfr;
   dp->capt_stalled = 1;
   task = dp->capt_vip_task;
   task_buffers = vip_task_data[task].task_buffers;

   if( dp->capt_addr_is_set == 0 ) {
      op = dp->capt_toggle == capt_toggle_odd ? dp->capt_elch : dp->capt_onxt;
      if( op == NULL ) {
         if( list_empty(&io->rd_qbuf) != 0 ) return 0;
         op = list_entry(io->rd_qbuf.next,io_buf,bfrq);
         list_del_init(&op->bfrq);
      }
      if( dp->capt_toggle == capt_toggle_both ||
          dp->capt_toggle == capt_toggle_odd ) {
         if( (ep=dp->capt_enxt) == NULL ) {
            if( list_empty(&io->rd_qbuf) != 0 ) {
               list_add(&op->bfrq,&io->rd_qbuf);
               return 0;
            }
            ep = list_entry(io->rd_qbuf.next,io_buf,bfrq);
            list_del_init(&ep->bfrq);
         }
      }
      else
         ep = op;
      dp->capt_onxt = op;  oidx = op->index;
      dp->capt_enxt = ep;  eidx = ep->index;
   }
   else
      oidx = eidx = 0;

   if( oidx != eidx ) {
      vip_inpbfr->current_buffer = eidx;
      vip_buffers = vip_task_data[task].vip_even_buffers;
      vip_toggle_video_offsets(vip_buffers,vip_inpbfr);
      vip_inpbfr->current_buffer = oidx;
      vip_buffers = vip_task_data[task].vip_odd_buffers;
      vip_toggle_video_offsets(vip_buffers,vip_inpbfr);
   }
   else {
      vip_inpbfr->current_buffer = oidx;
      vip_buffers = vip_task_data[task].vip_buffers;
      vip_toggle_video_offsets(vip_buffers,vip_inpbfr);
   }
   dp->capt_stalled = 0;

   ++dp->capt_sequence;
   ++dp->capt_jiffy_sequence;
   return 1;
}

/** lx_capt_start - start capture
 *  calculate buffer offsets, load buffers, and enable interrupts
 */
int
lx_capt_start(VidDevice *dp)
{
   int ret;
   int imgsz, width, height, y_pitch, uv_pitch;
   int vip_buffers, y_imgsz, uv_imgsz;
   int interrupts, field;
   int i;  io_queue *io;  io_buf *bp;
   unsigned long oy_offset, ey_offset, flags;
   unsigned long offset1, offset2, u_offset, v_offset;
   struct v4l2_pix_format *fmt = &dp->capt;
   unsigned int capt_pixelformat = fmt->pixelformat;
   int task = dp->capt_vip_task;
   int task_buffers = vip_task_data[task].task_buffers;
   VIPSETMODEBUFFER vip_mode;
   VIPSUBWINDOWBUFFER swbfr;
   VIPINPUTBUFFER_ADDR *vip_inpadr;
   VIPINPUTBUFFER *vip_inpbfr = &dp->vip_inpbfr;
   FilePriv *fp = &dp->fp;

   memset(&vip_mode,0,sizeof(vip_mode));
   vip_mode.stream_enables = vip_task_data[task].enables;
   if( dp->capt_inverted != 0 ) vip_mode.flags |= VIP_MODEFLAG_INVERTPOLARITY;
   vip_mode.operating_mode = dp->capt_vip_version == 1 ? VIP_MODE_VIP1_8BIT :
      (dp->capt_port_size == 8 ? VIP_MODE_VIP2_8BIT : VIP_MODE_VIP2_16BIT);

   memset(vip_inpbfr,0,sizeof(*vip_inpbfr));

   width = fmt->width;
   height = fmt->height;
   y_pitch = lx_capt_ypitch(dp,capt_pixelformat,width);
   uv_pitch = lx_capt_uvpitch(dp,capt_pixelformat,width);
   offset1 = offset2 = u_offset = v_offset = 0;
  /* default is capt format, if not in spec */
   field = v4l_vid_field(&dp->capt);
   field = v4l_vid_field_def(field,dp->capt_dft_field);
   if( lx_capt_std_progressive(dp,dp->std_index) != 0 )
      vip_mode.flags |= VIP_MODEFLAG_PROGRESSIVE;
   else
      height /= 2;
   y_imgsz = y_pitch*height;
   uv_imgsz = uv_pitch*height;
   if( v4l_fmt_is_420(capt_pixelformat) > 0 ) {
      uv_imgsz /= 2;
      dp->capt_420 = 1;
   }
   else
      dp->capt_420 = 0;
   imgsz = y_imgsz + 2*uv_imgsz;
   oy_offset = ey_offset = 0;
   dp->capt_toggle = capt_toggle_even;
   switch( field ) {
   case V4L2_FIELD_SEQ_TB:         /* both fields seq 1 bfr, top-bottom */
      ey_offset += imgsz;
      break;
   case V4L2_FIELD_SEQ_BT:         /* same + bottom-top */
      oy_offset += imgsz;
      break;
   case V4L2_FIELD_INTERLACED:     /* both fields interlaced */
      ey_offset += y_pitch;
      y_pitch *= 2;
      uv_pitch *= 2;
      y_imgsz *= 2;
      uv_imgsz *= 2;
      break;
   case V4L2_FIELD_TOP:            /* top field only */
      dp->capt_toggle = dp->capt_progressive != 0 ?
         capt_toggle_vblank : capt_toggle_odd;
      break;
   case V4L2_FIELD_BOTTOM:         /* bottom field only */
      dp->capt_toggle = dp->capt_progressive != 0 ?
         capt_toggle_vblank : capt_toggle_even;
      break;
   case V4L2_FIELD_ALTERNATE:      /* both fields alternating 2 buffers */
   case V4L2_FIELD_NONE:           /* counts as progressive */
      dp->capt_toggle = dp->capt_progressive != 0 ?
         capt_toggle_vblank : capt_toggle_both;
      break;
   }
   if( dp->capt_progressive != 0 ) {
      if( dp->capt_toggle != capt_toggle_vblank ) {
         DMSG(1,"lx_capt_start:std field fmt progressive/"
                "field(%d) fmt mismatched\n",field);
         dp->capt_toggle = capt_toggle_vblank;
      }
   }
   if( v4l_fmt_is_planar(capt_pixelformat) > 0 ) {
      offset1 = y_imgsz;
      offset2 = offset1 + uv_imgsz;
      dp->capt_planar = 1;
   }
   else {
      dp->capt_planar = 0;
   }
   switch( capt_pixelformat ) {
   case V4L2_PIX_FMT_YVU420:
      v_offset = offset1;
      u_offset = offset2;
      break;
   case V4L2_PIX_FMT_YUV422P:
   case V4L2_PIX_FMT_YUV420:
      u_offset = offset1;
      v_offset = offset2;
      break;
   }

   dp->capt_ey_offset = ey_offset;
   dp->capt_oy_offset = oy_offset;
   dp->capt_u_offset = u_offset;
   dp->capt_v_offset = v_offset;

   memset(&swbfr,0,sizeof(swbfr));
   swbfr.enable = 1;
   swbfr.stop = height;
   vip_set_subwindow_enable(&swbfr);

   vip_mode.planar_capture = dp->capt_420 != 0 ?
      VIP_420CAPTURE_ALTERNATINGLINES : VIP_420CAPTURE_EVERYLINE;
   if( dp->capt_planar != 0 ) {
      vip_mode.flags |= VIP_MODEFLAG_PLANARCAPTURE;
      vip_inpbfr->flags |= VIP_INPUTFLAG_PLANAR;
   }

   vip_inpadr = &vip_inpbfr->offsets[task_buffers];
   if( dp->capt_addr_is_set == 0 ) {
      if( (io=fp->io) == NULL ) return -EINVAL; /* must have buffer(s) */
      for( i=io->mbfrs,bp=&io->bfrs[0]; --i>=0; ++bp ) {
         vip_inpadr->odd_base[bp->index]  = bp->phys_addr + oy_offset;
         vip_inpadr->even_base[bp->index] = bp->phys_addr + ey_offset;
      }
   }
   else {
      vip_inpadr->odd_base[0]  = dp->capt_phys_addr + oy_offset;
      vip_inpadr->even_base[0] = dp->capt_phys_addr + ey_offset;
   }
   vip_inpadr->y_pitch = y_pitch;
   vip_inpadr->uv_pitch = uv_pitch;
   vip_inpadr->odd_uoffset = u_offset;
   vip_inpadr->even_uoffset = u_offset;
   vip_inpadr->odd_voffset = v_offset;
   vip_inpadr->even_voffset = v_offset;

   is_reported = 0;
   DMSG(1," capt %4.4s %dx%d oy %lx ey %lx u %lx v %lx"
          " yp %x uvp %x tgl %d prg %d plnr %d 420 %d\n",
       (char*)&fmt->pixelformat, fmt->width, fmt->height,
       vip_inpadr->odd_base[0], vip_inpadr->even_base[0],
       u_offset, v_offset, y_pitch, uv_pitch,
       dp->capt_toggle, dp->capt_progressive,
       dp->capt_planar, dp->capt_420 );

   vip_max_address_enable(DEFAULT_MAX_ADDRESS, 0);
   spin_lock_irqsave(&dp->lock, flags);
   ret = vip_initialize(&vip_mode);
   if(ret)
     DMSG(3,"lx_capt_start: vip_initialize returned %d (%d:OK)\n", ret, CIM_STATUS_OK);

   vip_buffers = vip_task_data[task].vip_buffers;
   ret = vip_configure_capture_buffers(vip_buffers,vip_inpbfr);
   if(ret)
     DMSG(3,"lx_capt_start: vip_configure_capture_buffers returned %d (%d:OK)\n", ret, CIM_STATUS_OK);

   dp->capt_obfr = dp->capt_ebfr = NULL;
   dp->capt_onxt = dp->capt_enxt = NULL;
   dp->ovly_obfr = dp->ovly_ebfr = NULL;
   ret = lx_capt_resume(dp,fp->io);
   if(ret!=1)
     DMSG(3,"lx_capt_start: lx_capt_resume returned %d (1:OK)\n", ret);
   dp->capt_no_clocks = 0;
   vip_set_capture_state(VIP_STARTCAPTUREATNEXTFRAME);

   interrupts = ERR_INT_ENABLES;
   if( dp->capt_progressive == 0 )
      interrupts |= VIP_INT_START_ODD | VIP_INT_START_EVEN;
   else
      interrupts |= VIP_INT_START_VBLANK;
   vip_set_interrupt_enable(interrupts, 1);
   spin_unlock_irqrestore(&dp->lock, flags);
   return 0;
}

/** lx_capt_stop - stop capture */
void
lx_capt_stop(VidDevice *dp)
{
   vip_set_interrupt_enable(VIP_ALL_INTERRUPTS, 0);
   vip_set_capture_state(VIP_STOPCAPTURE);
   vip_set_genlock_enable(0);
   vip_terminate();
   return;
}

/** lx_capt_get_format - enumerate video formats */
int
lx_capt_get_format(VidDevice *dp,int index, struct v4l2_fmtdesc *fmt)
{
   int i, n, idx;
   unsigned int pixelformat;
   memset(fmt,0,sizeof(*fmt));
   fmt->index = index;
   n = dp->capt_formats;
   for( idx=0,i=sizeof(n)*8; --i>=0; n>>=1,++idx ) {
      if( (n&1) == 0 ) continue;
      if( --index < 0 ) break;
   }
   switch( idx ) {
   case 0: pixelformat = V4L2_PIX_FMT_UYVY;    break;
   case 1: pixelformat = V4L2_PIX_FMT_YUYV;    break;
   case 2: pixelformat = V4L2_PIX_FMT_YUV420;  break;
   case 3: pixelformat = V4L2_PIX_FMT_YVU420;  break;
   case 4: pixelformat = V4L2_PIX_FMT_YUV422P; break;
   default:
      return -EINVAL;
   }
   fmt->pixelformat = pixelformat;
   snprintf(&fmt->description[0],sizeof(fmt->description),
      "%4.4s", (char*)&pixelformat);
#ifdef REV2
   fmt->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
#else
   fmt->depth = 16;
#endif
   return 0;
}

/** lx_capt_min_rect - return min capture geometry */
void
lx_capt_min_rect(VidDevice *dp,int *w, int *h)
{
   *w = 320;  *h = 240;
}

/** lx_capt_max_rect - return max capture geometry */
void
lx_capt_max_rect(VidDevice *dp,int *w, int *h)
{
   *w = 1920;  *h = 1080;
}

/** lx_capt_std_geom - return capture geometry by std_index */
void
lx_capt_std_geom(VidDevice *dp,int idx, int *w, int *h)
{
   *w = lx_std_parms[idx].width;
   *h = lx_std_parms[idx].height;
}

/** lx_capt_is_supported_format - test whether format is supported and enabled */
int
lx_capt_is_supported_format(VidDevice *dp,int fmt)
{
   int n;
   switch( fmt ) {
   case V4L2_PIX_FMT_UYVY:     n = 0;  break;
   case V4L2_PIX_FMT_YUYV:     n = 1;  break;
   case V4L2_PIX_FMT_YUV420:   n = 2;  break;
   case V4L2_PIX_FMT_YVU420:   n = 3;  break;
   case V4L2_PIX_FMT_YUV422P:  n = 4;  break;
   default:
      return 0;
   }
   return (dp->capt_formats >> n) & 1;
}

/** lx_capt_max_framerate - return max capture (nearest int) framerate */
int
lx_capt_max_framerate(VidDevice *dp,int idx)
{
   return lx_std_parms[idx].rate;
}

/** lx_capt_std_progressive - test standard is progressinve by std_index */
int
lx_capt_std_progressive(VidDevice *dp,int idx)
{
   return lx_std_parms[idx].progressive;
}

/** lx_capt_std_vip_version - vip version by std_index */
int
lx_capt_std_vip_version(VidDevice *dp,int idx)
{
   return lx_std_parms[idx].vip_version;
}

/** lx_capt_std_port_size - vip port size by std_index */
int
lx_capt_std_port_size(VidDevice *dp,int idx)
{
   return lx_std_parms[idx].port_size;
}

/** lx_capt_validate_format - validate format parameters, modify to valid */
void
lx_capt_validate_format(VidDevice *dp, struct v4l2_pix_format *fmt)
{
   int n, h, w, width, height, progressive;
   int y_pitch, uv_pitch;
   int field, colorspace;
   if( lx_capt_is_supported_format(dp,fmt->pixelformat) == 0 )
      fmt->pixelformat = V4L_DEFAULT_PIX_FMT;
   width = fmt->width;
   height = fmt->height;
   lx_capt_min_rect(dp,&w,&h);
   if( width < w ) width = w;
   if( height < h ) height = h;
   lx_capt_max_rect(dp,&w,&h);
   if( width > w ) width = w;
   if( height > h ) height = h;
   progressive = lx_capt_std_progressive(dp,dp->std_index);
   if( progressive == 0 ) {
      field = v4l_vid_field(fmt);
      field = v4l_vid_field_def(field,dp->capt_dft_field);
   }
   else
      field = V4L2_FIELD_NONE;
   v4l_vid_set_field(fmt,field);
   colorspace = v4l_vid_colorspace(fmt);
   colorspace = v4l_vid_colorspace_def(colorspace,V4L2_COLORSPACE_SMPTE170M);
   v4l_vid_set_colorspace(fmt,colorspace);
   y_pitch = lx_capt_ypitch(dp,fmt->pixelformat,width);
   uv_pitch = lx_capt_uvpitch(dp,fmt->pixelformat,width);
   if( v4l_fmt_is_420(fmt->pixelformat) > 0 )
      uv_pitch /= 2;
   fmt->width = width;
   fmt->height = height;
   fmt->bytesperline = y_pitch + 2*uv_pitch;
   n = v4l_flds_per_bfr(field);
   if( (n == 0 && progressive == 0) || n == 1 )
      height /= 2;
   fmt->sizeimage = fmt->bytesperline*height;
   fmt->priv = 0;
}

/** lx_capt_is_field_valid - test field valid for format/std */
int
lx_capt_is_field_valid(VidDevice *dp,unsigned int pixfmt,int field)
{
   if( v4l_fmt_is_planar(pixfmt) > 0 ) {
      if( v4l_flds_per_bfr(field) == 2 )
         return 0;
   }
   if( lx_capt_std_progressive(dp,dp->std_index) != 0 ) {
      if( field != V4L2_FIELD_NONE )
         return 0;
   }
#ifdef REV2
   else {
      if( field == V4L2_FIELD_NONE )
         return 0;
   }
#endif
   return 1;
}

/** lx_capt_ypitch - return capture y_image pitch */
int
lx_capt_ypitch(VidDevice *dp,unsigned int pixelformat,int width)
{
   int y_pitch = v4l_vid_ypitch(pixelformat,width);
   if( v4l_fmt_is_planar(pixelformat) > 0 )
      y_pitch = (y_pitch+Y_CAPT_PLNR_GRAIN-1) & ~(Y_CAPT_PLNR_GRAIN-1);
   return y_pitch;
}

/** lx_capt_uvpitch - return capture uv_image pitch */
int
lx_capt_uvpitch(VidDevice *dp,unsigned int pixelformat,int width)
{
   int uv_pitch = v4l_vid_uvpitch(pixelformat,width);
   if( v4l_fmt_is_planar(pixelformat) > 0 )
      uv_pitch = (uv_pitch+UV_CAPT_PLNR_GRAIN-1) & ~(UV_CAPT_PLNR_GRAIN-1);
   return uv_pitch;
}

/** lx_lx_display_is_interlaced - test for interlaced output (tv) mode */
#define DC3_IRQ_FILT_CTL                    0x00000094  /* VBlank interrupt and filters  */
#define DC3_IRQFILT_INTL_EN                 0x00000800  /* interlace enabled */
int lx_display_is_interlaced(void)
{
   unsigned int irq_filt_ctl;
   irq_filt_ctl = *(volatile unsigned int *)(cim_get_vg_ptr()+DC3_IRQ_FILT_CTL);
   return (irq_filt_ctl & DC3_IRQFILT_INTL_EN) != 0 ? 1 : 0;
}

/** lx_ovly_set_scale - set overlay scaling */
int
lx_ovly_set_scale(VidDevice *dp,int src_w,int src_h,int dst_w,int dst_h)
{
   int field;
   if( dst_w <= 0 || dst_h <= 0 ) return -EINVAL;
  /* default is capt format, if not in spec */
   field = v4l_vid_field(&dp->capt);
   field = v4l_vid_field_def(field,dp->capt_dft_field);
#ifdef REV2
   field = v4l_vid_field_def(dp->ovly.field,field);
#endif
   if( v4l_fld_is_laced(field) > 0 && v4l_flds_per_bfr(field) == 1 )
      src_h /= 2;
   if( df_set_video_scale(src_w,src_h,dst_w,dst_h,
          DF_SCALEFLAG_CHANGEX | DF_SCALEFLAG_CHANGEY) != 0 )
      return -EINVAL;
   if( src_h > 0 )
      dp->ovly_bob_dy = dst_h / (2*src_h);
   DMSG(3,"lx_ovly_set_scale src %dx%d dst %dx%d\n",src_w,src_h,dst_w,dst_h);
   return 0;
}

/** lx_ovly_start - enable overlay */
int
lx_ovly_start(VidDevice *dp)
{
   int i, n, ret;
   ret = lx_ovly_set_scale(dp,dp->ovly_src.w,dp->ovly_src.h,dp->ovly_dst.w,dp->ovly_dst.h);
   if( ret != 0 ) return ret;
   df_set_video_filter_coefficients(NULL, 1);
   /* if any alpha windows active, must disable per pixel alpha blending */
   for( n=i=0; i<3; ++i )
      n += df_get_alpha_window_enable(i);
   df_set_video_enable(1,n==0 ? 0 : DF_ENABLEFLAG_NOCOLORKEY);
   return 0;
}

/** lx_ovly_stop - disable overlay */
void
lx_ovly_stop(VidDevice *dp)
{
   df_set_video_enable(0,0);
   return;
}

/** lx_ovly_is_supported_format - test format supported by hw */
int
lx_ovly_is_supported_format(VidDevice *dp,int fmt)
{
   return lx_ovly_hw_fmt(dp,fmt) >= 0 ? 1 : 0;
}

/** lx_ovly_is_supported_depth - test depth supported by hw
 *  this function is obsolete and broken.  There is no
 *  such thing as depth since pixels are "macropixels" and
 *  potentially spread over parameter spaces
 */
int
lx_ovly_is_supported_depth(VidDevice *dp,int depth)
{
   return depth == 16 ? 1 : 0;
}

/** lx_ovly_min_rect - return min overlay geometry */
void
lx_ovly_min_rect(VidDevice *dp,int *w, int *h)
{
   *w = 1;  *h = 1;
}

/** lx_ovly_max_rect - return max overlay geometry */
void
lx_ovly_max_rect(VidDevice *dp,int *w, int *h)
{
   *w = 2*1920;  *h = 2*1080;
}

/** lx_ovly_get_format - enumerate video formats */
int
lx_ovly_get_format(VidDevice *dp,int index, struct v4l2_fmtdesc *fmt)
{
   int i, n, idx;
   unsigned int pixelformat;
   char *cp;
   memset(fmt,0,sizeof(*fmt));
   fmt->index = index;
   n = dp->ovly_formats;
   for( idx=0,i=sizeof(n)*8; --i>=0; n>>=1,++idx ) {
      if( (n&1) == 0 ) continue;
      if( --index < 0 ) break;
   }
   switch( idx ) {
   case 0: pixelformat = V4L2_PIX_FMT_UYVY;    cp = "UYVY";       break;
   case 1: pixelformat = V4L2_PIX_FMT_YUYV;    cp = "YUY2";       break;
   case 2: pixelformat = V4L2_PIX_FMT_YUV420;  cp = "YUV 4:2:0";  break;
   case 3: pixelformat = V4L2_PIX_FMT_YVU420;  cp = "YVU 4:2:0";  break;
   case 4: pixelformat = V4L2_PIX_FMT_RGB565;  cp = "RGB 16bit";  break;
//   case 5: pixelformat = V4L2_PIX_FMT_YUV422P; cp = "YUV 4:2:2";  break;
   default:
      return -EINVAL;
   }
   fmt->pixelformat = pixelformat;
   snprintf(&fmt->description[0],sizeof(fmt->description),"%s", cp);
#ifdef REV2
   fmt->type = V4L2_BUF_TYPE_VIDEO_OVERLAY;
#else
   fmt->depth = 16;
#endif
   return 0;
}

/** lx_ovly_hw_fmt - map pixelformat to cimarron format */
int
lx_ovly_hw_fmt(VidDevice *dp,int fmt)
{
   int n, hw_fmt;
   switch( fmt ) {
   case V4L2_PIX_FMT_YUYV:    hw_fmt = DF_VIDFMT_YUYV;      n = 0;  break;
   case V4L2_PIX_FMT_UYVY:    hw_fmt = DF_VIDFMT_UYVY;      n = 1;  break;
   case V4L2_PIX_FMT_YUV420:  hw_fmt = DF_VIDFMT_Y0Y1Y2Y3;  n = 2;  break;
   case V4L2_PIX_FMT_YVU420:  hw_fmt = DF_VIDFMT_Y0Y1Y2Y3;  n = 3;  break;
   case V4L2_PIX_FMT_RGB565:  hw_fmt = DF_VIDFMT_RGB;       n = 4;  break;
//   case V4L2_PIX_FMT_YUV422P: hw_fmt = DF_VIDFMT_YUYV;  n = 5;  break;
   default:
      return -1;
   }
   return ((dp->ovly_formats>>n)&1) != 0 ? hw_fmt : -1;
}

/** lx_ovly_reset_dst_rect - apply current ovly cimaaron position data */
/* unfortunately, this routine is needed to set the src x, because it
   uses df_set_video_postion.  So it will be called in cases both of
   src/dst geometry updates */
void
lx_ovly_reset_dst_rect(VidDevice *dp,int py)
{
   int x, y, w, h;
   int xofs, yofs;
   Rect *src = &dp->ovly_src;
   Rect *dst = &dp->ovly_dst;
   DF_VIDEO_POSITION pos;
   memset(&pos,0,sizeof(pos));
   if( (xofs=-dst->x) < 0 ) xofs = 0;
   if( (yofs=-dst->y) < 0 ) yofs = 0;
   x = dst->x + xofs;  y = dst->y + yofs;
   w = dst->w - xofs;  h = dst->h - yofs;
   /* only works to multple of 4, unless on left edge */
   pos.left_clip = src->x + xofs*src->w/dst->w;
   /* if displaying on the left edge, start video early to clip video grain */
   pos.dst_clip = x == 0 ? (pos.left_clip&3)*dst->w/src->w : 0;
   pos.flags |= DF_POSFLAG_DIRECTCLIP;
   pos.x = x;      pos.y = y+py;
   pos.width = w;  pos.height = h;
   df_set_video_position(&pos);
}

/** lx_ovly_set_dst_rect - update ovly dst rect, apply and scale */
int
lx_ovly_set_dst_rect(VidDevice *dp,int x, int y, int w, int h)
{
   int ret;
   Rect *src = &dp->ovly_src;
   Rect *dst = &dp->ovly_dst;
   Rect orig = *dst;
   dst->x = x;  dst->y = y;
   dst->w = w;  dst->h = h;
   dst->is_set = 1;
   ret = lx_ovly_set_src_rect(dp,src->x,src->y,src->w,src->h);
   DMSG(4,"lx_ovly_set_dst_rect src %d,%d %dx%d dst %d,%d %dx%d = %d\n",
      src->x,src->y,src->w,src->h,dst->x,dst->y,dst->w,dst->h,ret);
   if( ret != 0 ) *dst = orig;
   return ret;
}

/** lx_ovly_set_offsets - update ovly buffer device parameters
 *  offsets are recalculated, device format reset
 */
void
lx_ovly_set_offsets(VidDevice *dp)
{
   int field, scale;
   unsigned long imgsz, offset1, offset2, y_imgsz, uv_imgsz;
   unsigned long u_offset, v_offset, oy_offset, ey_offset;
   unsigned int ovly_pixelformat = dp->ovly_pixelformat;
   Rect *src = &dp->ovly_src;
   Rect *dst = &dp->ovly_dst;
   int dy = dst->y >= 0 ? 0 : (-dst->y*src->h)/dst->h;
   int y = dp->ovly_src.y + dy;
#ifdef REV2
   int width = dp->ovly.w.width;
   int height = dp->ovly.w.height;
#else
   int width = dp->ovly.width;
   int height = dp->ovly.height;
#endif
   unsigned long y_pitch = lx_ovly_ypitch(dp,width);
   unsigned long uv_pitch = lx_ovly_uvpitch(dp,width);
   int video_format = lx_ovly_hw_fmt(dp,ovly_pixelformat);
   offset1 = offset2 = u_offset = v_offset = 0;
  /* default is capt format, if not in spec */
   field = v4l_vid_field(&dp->capt);
   field = v4l_vid_field_def(field,dp->capt_dft_field);
#ifdef REV2
   field = v4l_vid_field_def(dp->ovly.field,field);
#endif
   if( (scale=v4l_flds_per_bfr(field)) > 0 )
      height /= scale;
   y_imgsz = y_pitch*height;
   uv_imgsz = uv_pitch*height;
   if( v4l_fmt_is_420(ovly_pixelformat) > 0 ) {
      y &= ~1;   /* design limitation */
      uv_imgsz /= 2;
   }
   imgsz = y_imgsz + 2*uv_imgsz;
   if( scale == 1 ) y /= 2;
   oy_offset = y*y_pitch;
   ey_offset = oy_offset;
   switch( field ) {
   case V4L2_FIELD_SEQ_BT:         /* same + bottom-top */
      if( lx_display_is_interlaced() != 0 )
         oy_offset += imgsz;
      break;
   case V4L2_FIELD_SEQ_TB:         /* both fields seq 1 bfr, top-bottom */
      if( lx_display_is_interlaced() != 0 )
         ey_offset += imgsz;
      break;
   case V4L2_FIELD_INTERLACED:     /* both fields interlaced */
      y_imgsz *= 2;
      uv_imgsz *= 2;
      ey_offset += y_pitch;
      if( lx_display_is_interlaced() != 0 ) {
         y_pitch *= 2;
         uv_pitch *= 2;
      }
      break;
   case V4L2_FIELD_TOP:            /* top field only */
   case V4L2_FIELD_BOTTOM:         /* bottom field only */
   case V4L2_FIELD_ALTERNATE:      /* both fields alternating 2 buffers */
   case V4L2_FIELD_NONE:           /* counts as progressive */
      break;
   }
   if( v4l_fmt_is_planar(ovly_pixelformat) > 0 ) {
      if( v4l_fmt_is_420(ovly_pixelformat) > 0 ) y /= 2;
      offset1 = y_imgsz + y*uv_pitch;
      offset2 = y_imgsz + uv_imgsz + y*uv_pitch;
   }
   switch( ovly_pixelformat ) {
   case V4L2_PIX_FMT_YVU420:
      v_offset = offset1;
      u_offset = offset2;
      break;
   case V4L2_PIX_FMT_YUV422P:
   case V4L2_PIX_FMT_YUV420:
      u_offset = offset1;
      v_offset = offset2;
      break;
   }

   dp->ovly_format = video_format;
   dp->ovly_ey_offset = ey_offset;
   dp->ovly_oy_offset = oy_offset;
   dp->ovly_u_offset = u_offset;
   dp->ovly_v_offset = v_offset;
   dp->ovly_y_pitch = y_pitch;
   dp->ovly_uv_pitch = uv_pitch;
   dp->ovly_width = width;
   dp->ovly_height = height;

   if( is_reported == 0 ) {
#ifdef REV2
      DMSG(1,"lx_ovly_set_offsets bfr %d,%d %dx%d src %d,%d %dx%d dst %d,%d %dx%d\n",
         dp->ovly.w.left,dp->ovly.w.top,dp->ovly.w.width,dp->ovly.w.height,
         dp->ovly_src.x,dp->ovly_src.y,dp->ovly_src.w,dp->ovly_src.h,
         dp->ovly_dst.x,dp->ovly_dst.y,dp->ovly_dst.w,dp->ovly_dst.h);
#else
      DMSG(1,"lx_ovly_set_offsets bfr %d,%d %dx%d src %d,%d %dx%d dst %d,%d %dx%d\n",
         dp->ovly.x,dp->ovly.y,dp->ovly.width,dp->ovly.height,
         dp->ovly_src.x,dp->ovly_src.y,dp->ovly_src.w,dp->ovly_src.h,
         dp->ovly_dst.x,dp->ovly_dst.y,dp->ovly_dst.w,dp->ovly_dst.h);
#endif
      is_reported = 1;
   }
}

/** lx_ovly_set_bfr - device offsets/format updated
 * phys address used to specify ovly buffer base
 */
int
lx_ovly_set_bfr(VidDevice *dp,unsigned long phys_addr)
{
   int flags, colorspace;
   DF_VIDEO_SOURCE_PARAMS odd, even;
   unsigned long fb_base, offset;
   DMSG(5," addr %08lx fmt %d %dx%d oy %x ey %x u %x v %x yp %x uvp %x\n",
      phys_addr, dp->ovly_format, dp->ovly_width,dp->ovly_height,
      dp->ovly_oy_offset,dp->ovly_ey_offset, dp->ovly_u_offset,dp->ovly_v_offset,
      dp->ovly_y_pitch, dp->ovly_uv_pitch);

   fb_base = cim_get_fb_base();
   if( phys_addr < fb_base ) return -EINVAL;
   offset = phys_addr - fb_base;
   if( offset >= cim_get_fb_size() ) return -EINVAL;

   colorspace = dp->ovly_colorspace;
   flags = colorspace == V4L2_COLORSPACE_REC709 ? DF_SOURCEFLAG_HDTVSOURCE : 0;
   flags |= DF_SOURCEFLAG_IMPLICITSCALING;

   odd.video_format = even.video_format = dp->ovly_format;
   odd.y_offset  = dp->ovly_oy_offset + offset;
   even.y_offset = dp->ovly_ey_offset + offset;
   odd.u_offset  = even.u_offset = dp->ovly_u_offset + offset;
   odd.v_offset  = even.v_offset = dp->ovly_v_offset + offset;
   odd.y_pitch   = even.y_pitch  = dp->ovly_y_pitch;
   odd.uv_pitch  = even.uv_pitch = dp->ovly_uv_pitch;
   odd.width     = even.width    = dp->ovly_width;
   odd.height    = even.height   = dp->ovly_height;
   odd.flags     = even.flags    = flags;

   df_configure_video_source(&odd,&even);
   dp->ovly_phys_addr = phys_addr;
   return 0;
}

/** lx_ovly_set_src_rect - update ovly src rect, recalc offsets and scale */
int
lx_ovly_set_src_rect(VidDevice *dp,int x,int y,int w,int h)
{
   int width, height, ret;
   Rect *src = &dp->ovly_src;
   Rect *dst = &dp->ovly_dst;
   lx_ovly_min_rect(dp,&width,&height);
   if( w < width ) w = width;
   if( h < height ) h = height;
   DMSG(1,"lx_ovly_set_src_rect(%d,%d %dx%d)\n",x,y,w,h);
   ret = lx_ovly_set_scale(dp,w,h,dst->w,dst->h);
   if( ret != 0 ) return ret;
   src->x = x;  src->y = y;
   src->w = w;  src->h = h;
   src->is_set = 1;
   lx_ovly_set_offsets(dp);       /* set src y coord */
   if( dp->ovly_addr_is_set != 0 )
      lx_ovly_set_bfr(dp,dp->ovly_phys_addr);
   lx_ovly_reset_dst_rect(dp,0);  /* set dst rect,  set src x coord */
   return 0;
}

/** lx_ovly_validate_format - validate format parameters, modify to valid */
void lx_ovly_validate_window(VidDevice *dp,unsigned int pixfmt, struct v4l2_window *win)
{
   int h, w;
#ifdef REV2
   int x = win->w.left;
   int y = win->w.top;
   int width = win->w.width;
   int height = win->w.height;
#else
   int x = win->x;
   int y = win->y;
   int width = win->width;
   int height = win->height;
#endif
   int key = win->chromakey;
   lx_ovly_min_rect(dp,&w,&h);
   if( width < w ) width = w;
   if( height < h ) height = h;
   lx_ovly_max_rect(dp,&w,&h);
   if( width > w ) width = w;
   if( height > h ) height = h;
   memset(win,0,sizeof(*win));
#ifdef REV2
   if( lx_capt_std_progressive(dp,dp->std_index) != 0 )
      win->field = V4L2_FIELD_NONE;
   else if( win->field == V4L2_FIELD_NONE || win->field == V4L2_FIELD_ANY )
      win->field = V4L2_FIELD_INTERLACED;
   win->w.left = x;
   win->w.top = y;
   win->w.width = width;
   win->w.height = height;
#else
   win->x = x;
   win->y = y;
   win->width = width;
   win->height = height;
#endif
   win->chromakey = key;
}

/** lx_ovly_ypitch - return overlay y_image pitch */
int lx_ovly_ypitch(VidDevice *dp,int width)
{
   return v4l_vid_ypitch(dp->ovly_pixelformat,width);
}

/** lx_ovly_uvpitch - return overlay uv_image pitch */
int lx_ovly_uvpitch(VidDevice *dp,int width)
{
   return v4l_vid_uvpitch(dp->ovly_pixelformat,width);
}

/* report hardware characteristics */
int lx_ovly_has_chomakey(void) { return 1; }
int lx_ovly_has_clipping(void) { return 0; }
int lx_ovly_has_scaleup(void) { return 1; }
int lx_ovly_has_scaledn(void) { return 1; }

/** lx_ovly_palette - reload video pallete using control params */
void lx_ovly_palette(VidDevice *dp)
{
   int x, y, w, wt;
   unsigned int red, blue;
   unsigned long palette[256];
   int contrast = (32768-dp->ovly_contrast) * 2;
   int blackLvl = dp->ovly_blackLvl - 32768;
   int brightness = (dp->ovly_brightness * (dp->ovly_brightness/128)) / 256;
   int redGain = (dp->ovly_redGain * (dp->ovly_redGain/128)) / 256;
   int blueGain = (dp->ovly_blueGain * (dp->ovly_blueGain/128)) / 256;
   for( x=0; x<256; ++x ) {
      wt = (x*(256-x))/(256/4);
      wt = (wt*contrast)/32768;
      w = (x*(256-wt) + 128*wt) / 256;
      y = ((brightness*w)/128 + blackLvl) / 256;
      if( y < 0 ) y = 0;
      if( y > 255 ) y = 255;
      red = (y*redGain)/32768;
      if( red > 255 ) red = 255;
      blue = (y*blueGain)/32768;
      if( blue > 255 ) blue = 255;
      palette[x] = (red<<16) + (y<<8) + blue;
   }
   df_set_video_palette(&palette[0]);
}

/** lx_ovly_chromakey - disable/enable overlay color/chroma key */
void lx_ovly_chromakey(VidDevice *dp,int key,int mask,int enable)
{
   if( enable == 0 )
      df_set_video_color_key(0,0,1);
   else
      df_set_video_color_key(key,mask,1);
}

int lx_set_alpha_window(struct v4l2_alpha_window *awin)
{
   int enable, window;
   window = awin->window;
   if( awin->priority >= 0 ) {
      DF_ALPHA_REGION_PARAMS dap;
      dap.x = awin->x;
      dap.y = awin->y;
      dap.width = awin->w;
      dap.height = awin->h;
      dap.alpha_value = awin->alpha;
      dap.priority = awin->priority;
      dap.color = awin->color;
      dap.flags = awin->flags;
      dap.delta = awin->delta;
      if( df_configure_alpha_window(window,&dap) != 0 )
         return -EINVAL;
      enable = 1;
   }
   else
      enable = 0;
   if( df_set_alpha_window_enable(window,enable) != 0 )
      return -EINVAL;
   return 0;
}

/* not yet implemented */
int lx_vbi_start(VbiDevice *dp) { return 0; }
void lx_vbi_stop(VbiDevice *dp) { return; }
long lx_vbi_write(VbiDevice *fp,char *buf,unsigned long count,int nonblock) { return 0; }
long lx_vbi_read(VbiDevice *fp,char *buf,unsigned long count,int nonblock) { return 0; }
int lx_vbi_poll(VbiDevice *fp, struct file *file, poll_table *wait) { return 0; }
int lx_vbi_get_buf_size(FilePriv *fp) { return 0; }

/** lx_interrupt - vip interrupt handler */
static irqreturn_t lx_interrupt(int irq, void *dev_id)
{
   unsigned long status = vip_get_interrupt_state();
   unsigned long offset, y_offset, u_offset, v_offset;
   unsigned long sequence, dt;  jiffiez_t jiffy_time;
   long numer;  long long denom;
   int stalled, dy, wake, even, active, skip;
   int even_index, show_even, show_odd, latched;
   V4LDevice *dev = (V4LDevice *)dev_id;
   VidDevice *dp = &dev->vid;
   FilePriv *fp = &dp->fp;
   io_queue *io = fp->io;
   io_buf *bp;

   DMSG(9,"lx_interrupt: enter irq handler with status %lu\n", status);
   status &= VIP_ALL_INTERRUPTS;
   if( status == 0 ) return IRQ_NONE;  /* spurious/shared */
   dev->irq_status = status;

   /* report any unexpected status */
   while( status != 0 ) {
      unsigned long stat = status & ~(status-1);
      status &= ~stat;
      switch( stat ) {
      case VIP_INT_FIFO_WRAP:
         DMSG(1,"VIP_INT_FIFO_WRAP\n");
         vip_reset();
         break;
      case VIP_INT_LONGLINE:
         DMSG(1,"VIP_INT_LONGLINE\n");
         vip_reset();
         break;
      case VIP_INT_ACTIVE_PIXELS:
         DMSG(1,"VIP_INT_ACTIVE_PIXELS\n");
         vip_reset();
         break;
      case VIP_INT_CLOCK_INPUT:
         DMSG(1,"VIP_INT_CLOCK_INPUT\n");
         vip_set_interrupt_enable(ERR_INT_ENABLES,0);
         vip_set_capture_state(VIP_STOPCAPTURE);
         dp->capt_no_clocks = 1;
         goto xit;
      }
   }

   active = VIP_INT_START_EVEN | VIP_INT_START_ODD | VIP_INT_START_VBLANK;
   if( (active &= dev->irq_status) == 0 ) goto xit;

   if( dp->capt_no_clocks != 0 ) {
      DMSG(9,"lx_interrupt: capt_no_clocks = %d\n", (int)dp->capt_no_clocks);
      dp->capt_no_clocks = 0;
      vip_set_interrupt_enable(ERR_INT_ENABLES,1);
      vip_set_capture_state(VIP_STARTCAPTUREATNEXTFIELD);
      goto xit;
   }

   ++dp->capt_field;
   wake = skip = 0;
   latched = vip_is_buffer_update_latched() != 0 ? 1 : 0;
   even = (dev->irq_status & VIP_INT_START_EVEN) != 0 ? 1 : 0;
   jiffy_time = jiffiez;
   if( dp->capt_num != 0 && dp->capt_denom != 0 ) {
      numer = dp->capt_num;  denom = dp->capt_denom;
      dt = jiffy_time - dp->capt_jiffy_start;
      denom *= dt;
      do_div(denom,numer);
      sequence = denom;
      /* if seq_no ahead of rate spec, drop frame */
      if( dp->capt_jiffy_sequence > sequence ) skip = 1;
      DMSG(9," seq %d dt(%lu)=%llu-%llu, cseq(%lu)=(%lu*%d)/%d) jseq(%lu) skip(%d)\n",
         dp->capt_sequence,dt,(unsigned long long)jiffy_time,
         (unsigned long long)dp->capt_jiffy_start,
         sequence,dt,dp->capt_denom,dp->capt_num,dp->capt_jiffy_sequence,skip);
   }
   DMSG(8," frame %d seq %d obfr %d ebfr %d onxt %d enxt %d t %d e %d l %d\n",
      dp->capt_frame,dp->capt_sequence,
      dp->capt_obfr?dp->capt_obfr->index:-1,
      dp->capt_ebfr?dp->capt_ebfr->index:-1,
      dp->capt_onxt?dp->capt_onxt->index:-1,
      dp->capt_enxt?dp->capt_enxt->index:-1,
      dp->capt_toggle,even,latched);
   if( dp->capt_addr_is_set == 0 ) {
      if( io != NULL ) {  /* queued/flipped io */
         if( latched != 0 ) {
            /* if loaded and latched */
            if( dp->capt_onxt != NULL && dp->capt_obfr == NULL ) {
               /* propagate odd buffers */
               dp->capt_obfr = dp->capt_olch;
               dp->ovly_obfr = dp->capt_olch;
               dp->capt_olch = dp->capt_onxt;
               if( (bp=dp->capt_olch) != NULL )
                  bp->jiffies = jiffy_time;
               dp->capt_onxt = NULL;
            }
            if( dp->capt_enxt != NULL && dp->capt_ebfr == NULL ) {
               /* propagate even buffers */
               dp->capt_ebfr = dp->capt_elch;
               dp->ovly_ebfr = dp->capt_elch;
               dp->capt_elch = dp->capt_enxt;
               if( (bp=dp->capt_elch) != NULL )
                  bp->jiffies = jiffy_time;
               dp->capt_enxt = NULL;
            }
         }
         /* if autoflipping, display finished buffer */
         if( io->io_type == io_flipped ) {
            even_index = 0;
            if( lx_display_is_interlaced() != 0 ) {
               even_index = 1;
               show_even = show_odd = 1;
            }
            else if( dp->ovly_obfr != dp->ovly_ebfr ) {
               show_even = even != 0 ? 1 : 0;
               show_odd = 1 - show_even;
            }
            else {
               show_odd = 1;  show_even = 0;
               dp->ovly_ebfr = NULL;
            }
            if( show_odd != 0 && (bp=dp->ovly_obfr) != NULL ) {
               dp->ovly_obfr = NULL;
               offset = bp->offset + vid_mem_ofst();
               y_offset = offset + dp->ovly_oy_offset;
               u_offset = offset + dp->ovly_u_offset;
               v_offset = offset + dp->ovly_v_offset;
               df_set_video_offsets(0,y_offset,u_offset,v_offset);
            }
            if( show_even != 0 && (bp=dp->ovly_ebfr) != NULL ) {
               dp->ovly_ebfr = NULL;
               offset = bp->offset + vid_mem_ofst();
               y_offset = offset + dp->ovly_ey_offset;
               u_offset = offset + dp->ovly_u_offset;
               v_offset = offset + dp->ovly_v_offset;
               df_set_video_offsets(even_index,y_offset,u_offset,v_offset);
            }
            /* if autoflipping, and "bob"ing */
            if( dp->ovly_bobbing != 0 ) {
               dy = even != 0 ? dp->ovly_bob_dy : 0;
               lx_ovly_reset_dst_rect(dp,dy);
            }
            if( (bp=dp->capt_obfr) != NULL ) {
               dp->capt_obfr = NULL;
               if( dp->capt_toggle != capt_toggle_both )
                  dp->capt_ebfr = NULL;
               list_add_tail(&bp->bfrq,&io->rd_qbuf);
            }
            if( (bp=dp->capt_ebfr) != NULL ) {
               dp->capt_ebfr = NULL;
               list_add_tail(&bp->bfrq,&io->rd_qbuf);
            }
         }
         /* if buffer deliverable */
         if( (dev->irq_status&capt_bfr_mask[dp->capt_toggle]) != 0 ) {
            if( (bp=dp->capt_obfr) != NULL ) {
               dp->capt_obfr = NULL;
               if( dp->capt_toggle != capt_toggle_both )
                  dp->capt_ebfr = NULL;
               /* if not autoflipping, deliver buffer */
               if( io->io_type != io_flipped ) {
                  list_add_tail(&bp->bfrq,&io->rd_dqbuf);
                  bp->flags &= ~V4L2_BUF_FLAG_QUEUED;
#ifndef REV2
                  bp->flags &= ~(V4L2_BUF_FLAG_TOPFIELD|V4L2_BUF_FLAG_BOTFIELD);
                  if( dp->capt_toggle == capt_toggle_both )
                     bp->flags |= V4L2_BUF_FLAG_TOPFIELD;
#endif
                  bp->flags |= V4L2_BUF_FLAG_DONE;
                  wake = 1;
               }
            }
            if( (bp=dp->capt_ebfr) != NULL ) {
               dp->capt_ebfr = NULL;
               /* if not autoflipping, deliver buffer */
               if( io->io_type != io_flipped ) {
                  list_add_tail(&bp->bfrq,&io->rd_dqbuf);
                  bp->flags &= ~V4L2_BUF_FLAG_QUEUED;
#ifndef REV2
                  bp->flags &= ~(V4L2_BUF_FLAG_TOPFIELD|V4L2_BUF_FLAG_BOTFIELD);
                  if( dp->capt_toggle == capt_toggle_both )
                     bp->flags |= V4L2_BUF_FLAG_BOTFIELD;
#endif
                  bp->flags |= V4L2_BUF_FLAG_DONE;
                  wake = 1;
               }
            }
         }
         /* if end of frame */
         if( even != 0 || dp->capt_toggle == capt_toggle_vblank ) {
            if( latched != 0 && skip != 0 ) ++dp->capt_skipped;
            ++dp->capt_frame; /* update stats */
         }
         /* printk("capt e %d o %d\n",
           dp->ovly_ebfr!=NULL?dp->ovly_ebfr->index:-1,
           dp->ovly_obfr!=NULL?dp->ovly_obfr->index:-1); */
         if( skip == 0 ) { /* if not downsampling, load next buffers */
            stalled = dp->capt_stalled;
            if( lx_capt_resume2(dp,io) == 0 ) {
               if( stalled == 0 )
                  DMSG(4,"stalled\n");
               else if( even == 0 )
                  ++dp->capt_dropped;
            }
         }
      }
      else {
         DMSG(0,"capture active with no buffer!\n");
         lx_capt_stop(dp);
      }
   }
   else { /* point and shoot buffer capture */
      if( vip_is_buffer_update_latched() != 0 )
         lx_capt_resume(dp,io);
      if( even != 0 || dp->capt_toggle == capt_toggle_vblank )
         ++dp->capt_frame;
      ++dp->capt_sequence;
      wake = 1;
   }

   /* if buffer queued/captured, wake up waiting task */
   if( wake != 0 ) {
      if( waitqueue_active(&dp->capt_wait) != 0 )
         wake_up_interruptible(&dp->capt_wait);
   }

xit:
   vip_reset_interrupt_state(dev->irq_status);
   return IRQ_HANDLED;
}


/** lx_capt_resume2 - queue capture buffers to vip */
int
lx_capt_resume2(VidDevice *dp,io_queue *io)
{
   io_buf *op, *ep;
   int eidx, oidx, vip_buffers;
   int task;
   int task_buffers;
   VIPINPUTBUFFER *vip_inpbfr;

   unsigned long flags;
   struct list_head *lp;
   io_buf *bp1;
   if(dp==NULL) return 0;
   if(io==NULL) return 0;
   task = dp->capt_vip_task;
   vip_inpbfr = &dp->vip_inpbfr;

   dp->capt_stalled = 1;
   task = dp->capt_vip_task;
   task_buffers = vip_task_data[task].task_buffers;

   if( dp->capt_addr_is_set == 0 ) {
     op = dp->capt_toggle == capt_toggle_odd ? dp->capt_elch : dp->capt_onxt;
       if( op == NULL ) {
         if( list_empty(&io->rd_qbuf) != 0 )
         {
           // there are no more buffers into the input list for grabbing images,
           // so requeue first output buffer into input list 

           spin_lock_irqsave(&io->lock, flags);

           lp = &io->rd_dqbuf;
           if( ! list_empty(lp) ) {
             bp1 = list_entry(lp->next,io_buf,bfrq);	// get the struct for this entry / list_entry ( ptr, type, member) &struct list_head pointer/ type of the struct this is embedded in/ name of the list_struct within the struct. 
             list_del_init(&bp1->bfrq);		//deletes entry from list and reinitialize it
           }
           else {
             spin_unlock_irqrestore(&io->lock,flags);
             return 0;
           }

           lp = &io->rd_qbuf;
	         list_move_tail(&bp1->bfrq,lp);

            bp1->sequence = io->sequence++;
            bp1->flags &= ~V4L2_BUF_FLAG_DONE;
            bp1->flags |= V4L2_BUF_FLAG_QUEUED;

	          if( dp->capt_stalled != 0 )
	          {
	             DMSG(3,"------------ v4l_qbfr : capt != 0 && dp->capt_stalled != 0\n");
	             //v4l_capt_unstall(dp);
	          }

	          spin_unlock_irqrestore(&io->lock,flags);
            //return 0;
         }

         op = list_entry(io->rd_qbuf.next,io_buf,bfrq);
         list_del_init(&op->bfrq);
      }
      if( dp->capt_toggle == capt_toggle_both ||
         dp->capt_toggle == capt_toggle_odd ) {
         if( (ep=dp->capt_enxt) == NULL ) {
            if( list_empty(&io->rd_qbuf) != 0 ) {
               list_add(&op->bfrq,&io->rd_qbuf);
               return 0;
            }
            ep = list_entry(io->rd_qbuf.next,io_buf,bfrq);
            list_del_init(&ep->bfrq);
         }
      }
      else
      {
         ep = op;
      }
      dp->capt_onxt = op;  oidx = op->index;
      dp->capt_enxt = ep;  eidx = ep->index;
   }
   else
   {
      oidx = eidx = 0;
   }

   if( oidx != eidx ) {
      vip_inpbfr->current_buffer = eidx;
      vip_buffers = vip_task_data[task].vip_even_buffers;
      vip_toggle_video_offsets(vip_buffers,vip_inpbfr);
      vip_inpbfr->current_buffer = oidx;
      vip_buffers = vip_task_data[task].vip_odd_buffers;
      vip_toggle_video_offsets(vip_buffers,vip_inpbfr);
   }
   else {
      vip_inpbfr->current_buffer = oidx;
      vip_buffers = vip_task_data[task].vip_buffers;
      vip_toggle_video_offsets(vip_buffers,vip_inpbfr);
   }
   dp->capt_stalled = 0;

   ++dp->capt_sequence;
   ++dp->capt_jiffy_sequence;

   return 1;
}



#if 0
/** lx_init - hookup interrupt and cimarron kernel resources */
int lx_init(struct pci_dev *pci, const struct pci_device_id *pdp)
{
   int ret;
   V4LDevice *dev;

   dev = v4l_dev_init();
   if (dev == NULL)
     return -ENOMEM;

   if( irq < 0 ) {
      irq = dev->irq = pci->irq;
      printk("Found Geode LX VIP at IRQ %d\n", (int)dev->irq);
   } else {
      dev->irq = irq;
      printk("Forcing Geode LX VIP IRQ to %d\n", (int)dev->irq);
   }

   ret = request_irq(dev->irq, lx_interrupt, IRQF_SHARED, v4l_name(), dev);
   if( ret != 0 ) return ret;

   if( cim_get_vip_ptr() == NULL ) {
      DMSG(0,"cant access cimarron video memory\n");
      return -ENODEV;
   }

   df_set_video_palette(NULL);
   return 0;
}

/** lx_exit - free interrupt */
void lx_exit(struct pci_dev *pci)
{
   v4l_dev_cleanup();
   free_irq(lx_dev->irq, lx_dev);
}
#endif

static struct pci_device_id lx_pci_tbl[] = {
   { PCI_VENDOR_ID_AMD, PCI_DEVICE_ID_LX3_VIDEO, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
   { 0, },
};

/** lx_init - hookup interrupt and cimarron kernel resources */
int lx_init(V4LDevice *dev)
{
   int ret;
   struct pci_dev *pci = NULL;
   const struct pci_device_id *pdp = NULL;

#ifdef LINUX_2_6
   while( (pci=pci_get_device(PCI_VENDOR_ID_AMD, PCI_ANY_ID, pci)) != NULL )
#else
   pci_for_each_dev(pci)
#endif
   {
       pdp = pci_match_id(&lx_pci_tbl[0], pci);
       if( pdp != NULL) break;
   }
   if( pdp == NULL) {
      DMSG(0,"cant find video device\n");
      return -ENODEV;
   }
   dev->pci = pci;

   if( dev->irq < 0 ) {
      dev->irq = pci->irq;
      printk("Found Geode LX VIP at IRQ %d\n", (int)dev->irq);
   }
   ret = request_irq(dev->irq, lx_interrupt, IRQF_SHARED, v4l_name(), dev);
   if( ret != 0 ) return ret;

   if( cim_get_vip_ptr() == NULL ) {
      DMSG(0,"cant access cimarron video memory\n");
      return -ENODEV;
   }

   df_set_video_palette(NULL);
   return 0;
}

/** lx_exit - free interrupt */
void lx_exit(V4LDevice *dev)
{
   free_irq(dev->irq, dev);
}

