#ifndef __V4L_H__
#define __V4L_H__
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
#define LINUX_2_6

#include <linux/types.h>
#include <linux/autoconf.h>
#include <linux/version.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/list.h>
#include <linux/sched.h>
#include <linux/spinlock.h>
#include <linux/interrupt.h>
#include <linux/wait.h>
#include <linux/fs.h>
#include <linux/pci.h>
#include <linux/i2c.h>

#ifndef LINUX_2_6
#include <linux/wrapper.h>
typedef unsigned long jiffiez_t;
#define jiffiez jiffies
#else
typedef unsigned long long jiffiez_t;
#define jiffiez get_jiffies_64()
#endif

#ifdef CONFIG_PROC_FS
#include <linux/proc_fs.h>
#define PROC_PATH "driver/lxv4l2"
#endif

#include <linux/videodev.h>
#include <media/v4l2-dev.h>
#include <asm/div64.h>

#include "build_num.h"
#ifdef LINUX_2_6
#ifndef REV2
#define REV2
#endif
#ifndef BASE_VIDIOC_PRIVATE
#define BASE_VIDIOC_PRIVATE BASE_VIDIOCPRIVATE
#endif
#endif

#include "../lib/cimarron/cim/cim_rtns.h"
#include "../lib/cimarron/cim/cim_parm.h"
#include "vid_mem.h"
#include "buf_mem.h"

#ifndef ENOTSUP
#define ENOTSUP EOPNOTSUPP
#endif

#define LXPREFIX "LXV4L2"
#define DMSG(n,s...) do { if( lx_dev->debug_level >= (n) ) \
   { printk(KERN_INFO "%s %s(%d):" ,&lx_dev->name[0], __FUNCTION__,__LINE__); printk(s); } \
 } while(0)

#define DRVNAME "lxvideo"
#define V4L_DEFAULT_PIX_FMT  V4L2_PIX_FMT_UYVY
//#define V4L_DEFAULT_PIX_FMT  V4L2_PIX_FMT_YUYV
//#define V4L_DEFAULT_PIX_FMT  V4L2_PIX_FMT_RGB565
//#define V4L_DEFAULT_PIX_FMT  V4L2_PIX_FMT_YUV420
#define V4L_DEFAULT_STANDARD V4L2_STD_NTSC
#define V4L_DEFAULT_HUE 0x80
#define V4L_DEFAULT_BRIGHTNESS 0x80
#define V4L_DEFAULT_CONTRAST 0x80
#define V4L_DEFAULT_SATURATION 0x80
#define V4L_DEFAULT_BLACKLVL 0x80

#define Y_CAPT_PLNR_GRAIN 64
#define UV_CAPT_PLNR_GRAIN 32

#ifndef REV2
static inline void *video_get_drvdata(struct video_device *dev) { return dev->priv; }
static inline void video_set_drvdata(struct video_device *dev, void *data) { dev->priv = data; }
#define VIDEO_MINOR 0
#define VBI_MINOR 224
#endif

typedef struct sV4LDevice V4LDevice;
typedef struct sVidDevice VidDevice;
typedef struct sVbiDevice VbiDevice;
typedef struct sDevFile DevFile;
typedef struct sFilePriv FilePriv;

typedef enum {
   ft_none,
   ft_vid,
   ft_vbi,
   ft_max,
} FileType;

struct sFilePriv {
   int type;
   struct file *file;
   io_queue *io;
   struct semaphore open_sem;
   struct video_device vd;
#ifdef TWO_DEVS
   struct video_device vd1;
#endif
#ifndef REV2
   struct v4l2_device vd2;
#endif
   int open_files;
};

typedef struct sRect {
   int is_set;
   int x, y;
   int w, h;
} Rect;

struct sVidDevice {
   spinlock_t lock;
   FilePriv fp;
   int std_index;
   int std_num;
   int std_denom;
   int pm_capt_state;
   int pm_ovly_state;
   struct timer_list pm_timer;
   struct v4l2_pix_format capt;
   int capt_state;
   int capt_max_bfrs;
   int capt_formats;
   int capt_dft_field;
   int capt_port_size;
   int capt_inverted;
   int capt_progressive;
   int capt_420;
   int capt_planar;
   int capt_toggle;
   int capt_bfr_mask;
   int capt_vip_version;
   int capt_vip_task;
   int capt_ey_offset;
   int capt_oy_offset;
   int capt_u_offset;
   int capt_v_offset;
   int capt_field;
   int capt_frame;
   int capt_skipped;
   int capt_stalled;
   int capt_no_clocks;
   int capt_dropped;
   int capt_sequence;
   int capt_hue;
   int capt_brightness;
   int capt_contrast;
   int capt_saturation;
   io_buf *capt_obfr, *capt_ebfr;
   io_buf *capt_olch, *capt_elch;
   io_buf *capt_onxt, *capt_enxt;
   int capt_num;
   int capt_denom;
   struct timeval capt_start_time;
   jiffiez_t capt_start_jiffy;
   jiffiez_t capt_jiffy_start;
   unsigned long capt_jiffy_sequence;
   wait_queue_head_t capt_wait;
   int capt_addr_is_set;
   unsigned long capt_phys_addr;

   struct v4l2_window ovly;
   unsigned long fb_phys_addr;
   int fb_is_set;
   int fb_width;
   int fb_height;
   int fb_depth;
   int fb_pitch;
   Rect ovly_src;
   Rect ovly_dst;
   int ovly_state;
   wait_queue_head_t ovly_wait;
   int ovly_addr_is_set;
   int ovly_max_bfrs;
   int ovly_formats;
   io_buf *ovly_bfr;
   int ovly_num;
   int ovly_denom;
   struct timer_list ovly_timer;
   int ovly_timer_active;
   struct timeval ovly_start_time;
   jiffiez_t ovly_start_jiffy;
   jiffiez_t ovly_jiffy_start;
   unsigned long ovly_jiffy_sequence;
   int ovly_frames;
   int ovly_dropped;
   int ovly_sequence;
   unsigned long ovly_phys_addr;
   unsigned int ovly_pixelformat;
   io_buf *ovly_obfr, *ovly_ebfr;
   int ovly_format;
   int ovly_ey_offset;
   int ovly_oy_offset;
   int ovly_u_offset;
   int ovly_v_offset;
   int ovly_y_pitch;
   int ovly_uv_pitch;
   int ovly_width;
   int ovly_height;
   int ovly_bobbing;
   int ovly_bob_dy;
   int ovly_colorspace;
   int ovly_keymode;
   int ovly_blackLvl;
   int ovly_redGain;
   int ovly_blueGain;
   int ovly_brightness;
   int ovly_contrast;

   VIPSETMODEBUFFER vip_mode;
   VIPINPUTBUFFER vip_inpbfr;
};

enum {
   capt_state_reset,
   capt_state_idle,
   capt_state_run,
};

enum {
   ovly_state_reset,
   ovly_state_idle,
   ovly_state_run,
};

struct sVbiDevice {
   spinlock_t lock;
   FilePriv fp;
   int start;
   int width;
   int height;
};

enum {
   capt_toggle_none,
   capt_toggle_odd,
   capt_toggle_even,
   capt_toggle_both,
   capt_toggle_vblank,
   capt_toggle_sz
};

struct sV4LDevice {
   char name[16];
   int refcnt;
   int irq_status;
   int debug_level;
   VidDevice vid;
   VbiDevice vbi;
   struct i2c_client *tv_decoder;
   struct i2c_client *tv_tuner;
   struct i2c_client *tv_audio;
   struct i2c_client *sensor;
   int irq;
   struct pci_dev *pci;
#ifdef CONFIG_PROC_FS
   struct proc_dir_entry *proc;
#endif
};

#define V4L2_COMPONENT_COMMAND  0xFF00FF00
#define V4L2_COMPONENT_CHECK    0xFF00FF01
#define V4L2_COMPONENT_INFO     0xFF00FF02
#define V4L2_COMPONENT_DEBUG    0xFF00FF03

#ifndef REV2
enum v4l2_field {
   V4L2_FIELD_ANY        = 0, /* driver can choose */
   V4L2_FIELD_NONE       = 1, /* this device has no fields ... */
   V4L2_FIELD_TOP        = 2, /* top field only */
   V4L2_FIELD_BOTTOM     = 3, /* bottom field only */
   V4L2_FIELD_INTERLACED = 4, /* both fields interlaced */
   V4L2_FIELD_SEQ_TB     = 5, /* both fields sequential, top-bottom order */
   V4L2_FIELD_SEQ_BT     = 6, /* both fields sequential, bottom-top order */
   V4L2_FIELD_ALTERNATE  = 7, /* both fields alternating into separate buffers */
};

enum v4l2_colorspace {
   V4L2_COLORSPACE_SMPTE170M     = 1, /* ITU-R 601 -- broadcast NTSC/PAL */
   V4L2_COLORSPACE_SMPTE240M     = 2, /* 1125-Line (US) HDTV */
   V4L2_COLORSPACE_REC709        = 3, /* HD and modern captures. */
   V4L2_COLORSPACE_BT878         = 4, /* broken BT878 extents (601, luma range 16-253 instead of 16-235) */
   V4L2_COLORSPACE_470_SYSTEM_M  = 5, /* These should be useful.  Assume 601 extents. */
   V4L2_COLORSPACE_470_SYSTEM_BG = 6,
   V4L2_COLORSPACE_JPEG          = 7, /* unspecified chromaticities and full 0-255 Y'CbCr components */
   V4L2_COLORSPACE_SRGB          = 8, /* For RGB colourspaces, this is probably a good start. */
};

#endif

#ifndef V4L2_COMPONENT_CHECK
#define V4L2_COMPONENT_CHECK 1
#endif
#ifndef DEFAULT_MAX_ADDRESS
#define DEFAULT_MAX_ADDRESS  0xc200
#endif
#ifndef PCI_VENDOR_ID_AMD
#define PCI_VENDOR_ID_AMD  0x1022
#endif
#ifndef PCI_DEVICE_ID_LX3_VIDEO
#define PCI_DEVICE_ID_LX3_VIDEO  0x2081
#endif

#ifdef REV2
#define V4L2_STD_UNK101 0x10000000UL /* HD_480P */
#define V4L2_STD_UNK102 0x20000000UL /* HD_525P */
#define V4L2_STD_UNK103 0x40000000UL /* HD_720P */
#define V4L2_STD_UNK104 0x80000000UL /* HD_1080I */
#define V4L2_STD_UNK105 0x08000000UL /* HD_VGA */
#define V4L2_STD_UNK106 0x04000000UL /* HD_QVGA */
#else
#define V4L2_STD_UNK101 0x101 /* HD_480P */
#define V4L2_STD_UNK102 0x102 /* HD_525P */
#define V4L2_STD_UNK103 0x103 /* HD_720P */
#define V4L2_STD_UNK104 0x104 /* HD_1080I */
#endif


#define VIDIOC_ALPHA_WINDOW (BASE_VIDIOC_PRIVATE+0)

struct v4l2_alpha_window
{
   int window;      /* window id 0-2 */
   int priority;    /* stacking order 0-2 */
   int alpha;       /* alpha 0-255 */
   int x, y, w, h;  /* geometry */
   long delta;      /* fader delta per frame */
   unsigned long color;
   unsigned long flags;
};


enum std_ids {
   STD_SD_UNKNOWN = -1,
   STD_SD_NTSC    = 0,
   STD_SD_PAL     = 1,
   STD_HD_480P    = 2,
   STD_HD_525P    = 3,
   STD_HD_720P    = 4,
   STD_HD_1080I   = 5,
   STD_HD_VGA     = 6,
   STD_HD_QVGA    = 7,
   STD_IDS_SZ     = 8,
};

enum capt_methods {
   CAPT_METHOD_PROGRESSIVE,
   CAPT_METHOD_WEAVE,
   CAPT_METHOD_BOB,
   CAPT_METHOD_SZ,
};

enum capt_tasks {
   CAPT_TASK_A,
   CAPT_TASK_B,
   CAPT_TASK_SZ
};

extern V4LDevice *lx_dev;

extern int irq;
extern int debug;

#if 0
V4LDevice *v4l_dev_init(void);
void v4l_dev_cleanup(void);

int lx_init(struct pci_dev *pci, const struct pci_device_id *pdp);
void lx_exit(struct pci_dev *pci);
#endif

int lx_init(V4LDevice *dev);
void lx_exit(V4LDevice *dev);

char *v4l_name(void);
int v4l_num_channels(void);
int v4l_num_audios(void);
int v4l_std_norm(int idx);
long long v4l_norm_std(int norm);
int v4l_standard_index(long long sid);
long long v4l_standard_id(int idx);
int v4l_pixelformat_to_palette(int pixelformat);
int v4l_palette_to_pixelformat(int palette);
int v4l_fmt_is_planar(unsigned int fmt);
int v4l_fmt_is_420(int fmt);
int v4l_fld_is_laced(int field);
int v4l_flds_per_bfr(int field);
int v4l_vid_ypitch(unsigned int pixelformat,int width);
int v4l_vid_uvpitch(unsigned int pixelformat,int width);
int v4l_vid_byte_depth(unsigned int pixelformat);
int v4l_vid_colorspace_def(int colorspace,int def);
int v4l_vid_field_def(int field,int def);
void v4l_vid_set_field(struct v4l2_pix_format *fmt,int field);
void v4l_vid_set_colorspace(struct v4l2_pix_format *fmt,int colorspace);
int v4l_vid_field(struct v4l2_pix_format *fmt);
int v4l_vid_colorspace(struct v4l2_pix_format *fmt);
int v4l_set_input(VidDevice *dp,int input);
int v4l_set_std_index(VidDevice *dp,int idx);
int v4l_set_norm(VidDevice *dp,int norm);
int v4l_set_control(VidDevice *dp,int id,int val);
void v4l_qbfr(VidDevice *dp,struct list_head *lp,io_buf *bp,int capt);
void v4l_qbfrs(VidDevice *dp,struct list_head *lp);
int v4l_dqbfr(VidDevice *dp,struct list_head *lp,io_buf **pbp,int nonblock,
   wait_queue_head_t *wait);
long v4l_stream_copy(VidDevice *dp,unsigned long imgsz, int wr,
   char *bfr, unsigned long count,int nonblock);
void v4l_vid_buffer(VidDevice *dp,struct v4l2_buffer *bfr,io_buf *bp,
   int used,int size,int flags);
int v4l_alloc_bfrs(VidDevice *dp,int io_type,int max,int imgsz,int qbfrs);
int v4l_standard(int index,struct v4l2_standard *std);
int v4l_max_buffers(unsigned long size,VidDevice *dp);
void v4l_default_capt_format(VidDevice *dp,struct v4l2_pix_format *fmt);
void v4l_default_ovly_format(VidDevice *dp,struct v4l2_window *win);
int v4l_capt_start(VidDevice *dp);
int v4l_capt_unstall(VidDevice *dp);
void v4l_capt_stop(VidDevice *dp);
int v4l_ovly_start(VidDevice *dp);
void v4l_ovly_stop(VidDevice *dp);
int v4l_component_ioctl(struct i2c_client *client,unsigned int fn,void *arg);

int lx_capt_resume(VidDevice *dp,io_queue *io);
int lx_capt_resume2(VidDevice *dp,io_queue *io);
int lx_capt_start(VidDevice *dp);
void lx_capt_stop(VidDevice *dp);
int lx_capt_get_format(VidDevice *dp,int index,struct v4l2_fmtdesc *fmt);
void lx_capt_min_rect(VidDevice *dp,int *w,int *h);
void lx_capt_max_rect(VidDevice *dp,int *w,int *h);
void lx_capt_std_geom(VidDevice *dp,int idx,int *w,int *h);
char *lx_capt_std_name(int idx);
char *lx_capt_v4l2_name(int idx);
int lx_capt_is_supported_format(VidDevice *dp,int fmt);
int lx_capt_is_field_valid(VidDevice *dp,unsigned int pixfmt,int field);
int lx_capt_max_framerate(VidDevice *dp,int idx);
int lx_capt_std_progressive(VidDevice *dp,int idx);
int lx_capt_std_vip_version(VidDevice *dp,int idx);
int lx_capt_std_port_size(VidDevice *dp,int idx);
void lx_capt_validate_format(VidDevice *dp,struct v4l2_pix_format *fmt);
int lx_capt_ypitch(VidDevice *dp,unsigned int pixelformat,int width);
int lx_capt_uvpitch(VidDevice *dp,unsigned int pixelformat,int width);
int lx_ovly_start(VidDevice *dp);
void lx_ovly_stop(VidDevice *dp);
int lx_ovly_set_scale(VidDevice *dp,int src_w,int src_h,int dst_w,int dst_h);
int lx_ovly_is_supported_format(VidDevice *dp,int fmt);
int lx_ovly_is_supported_depth(VidDevice *dp,int depth);
void lx_ovly_min_rect(VidDevice *dp,int *w,int *h);
void lx_ovly_max_rect(VidDevice *dp,int *w,int *h);
int lx_ovly_get_format(VidDevice *dp,int index,struct v4l2_fmtdesc *fmt);
int lx_ovly_hw_fmt(VidDevice *dp,int fmt);
void lx_ovly_reset_dst_rect(VidDevice *dp,int dy);
int lx_ovly_set_dst_rect(VidDevice *dp,int x,int y,int w,int h);
void lx_ovly_set_offsets(VidDevice *dp);
int lx_ovly_set_bfr(VidDevice *dp,unsigned long phys_addr);
int lx_ovly_set_src_rect(VidDevice *dp,int x,int y,int w,int h);
void lx_ovly_validate_window(VidDevice *dp,unsigned int pixfmt, struct v4l2_window *win);
int lx_ovly_ypitch(VidDevice *dp,int width);
int lx_ovly_uvpitch(VidDevice *dp,int width);
int lx_ovly_has_chomakey(void);
int lx_ovly_has_clipping(void);
int lx_ovly_has_scaleup(void);
int lx_ovly_has_scaledn(void);
void lx_ovly_palette(VidDevice *dp);
void lx_ovly_chromakey(VidDevice *dp,int key,int mask,int enable);
int lx_set_alpha_window(struct v4l2_alpha_window *awin);
int lx_vbi_start(VbiDevice *dp);
void lx_vbi_stop(VbiDevice *dp);
long lx_vbi_write(VbiDevice *fp,char *buf,unsigned long count,int nonblock);
long lx_vbi_read(VbiDevice *fp,char *buf,unsigned long count,int nonblock);
int lx_vbi_poll(VbiDevice *fp,struct file *file,poll_table *wait);
int lx_vbi_get_buf_size(FilePriv *fp);

#endif
