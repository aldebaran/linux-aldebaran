/*
 * A V4L2 driver for OmniVision OV7670 cameras.
 *
 * Copyright 2006 One Laptop Per Child Association, Inc.  Written
 * by Jonathan Corbet with substantial inspiration from Mark
 * McClelland's ovcamchip code.
 *
 * Copyright 2006-7 Jonathan Corbet <corbet@lwn.net>
 *
 * This file may be distributed under the terms of the GNU General
 * Public License, version 2.
 */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/videodev.h>
#include <media/v4l2-common.h>
#include <media/v4l2-chip-ident.h>
#include <linux/i2c.h>
//#include <math.h>


MODULE_AUTHOR("Jonathan Corbet <corbet@lwn.net>");
MODULE_DESCRIPTION("A low-level driver for OmniVision ov7670 sensors");
MODULE_LICENSE("GPL");

#define VERSION "2.00"

/* History
 * 1.0 - by Momtchil Momtchev
 * 1.03 - stability patches (Bri)
 * 1.04 - added exposition control (Bri)
 * */

/*
 * Basic window sizes.  These probably belong somewhere more globally
 * useful.
 */
#define VGA_WIDTH	640
#define VGA_HEIGHT	480
#define QVGA_WIDTH	320
#define QVGA_HEIGHT	240
#define CIF_WIDTH	352
#define CIF_HEIGHT	288
#define QCIF_WIDTH	176
#define	QCIF_HEIGHT	144

#undef REGDUMP

/*
 * Our nominal (default) frame rate.
 */
#define OV7670_FRAME_RATE 30

/*
 * The 7670 sits on i2c with ID 0x21
 */
#define OV7670_I2C_ADDR		0x21
#define OV7670_I2C_MAXRETRIES 3

/* Registers */
#define REG_GAIN	0x00	/* Gain lower 8 bits (rest in vref) */
#define REG_BLUE	0x01	/* blue gain */
#define REG_RED		0x02	/* red gain */
#define REG_VREF	0x03	/* Pieces of GAIN, VSTART, VSTOP */
#define REG_COM1	0x04	/* Control 1 */
#define  COM1_CCIR656	  0x40  /* CCIR656 enable */
#define REG_BAVE	0x05	/* U/B Average level */
#define REG_GbAVE	0x06	/* Y/Gb Average level */
#define REG_AECHH	0x07	/* AEC MS 5 bits */
#define REG_RAVE	0x08	/* V/R Average level */
#define REG_COM2	0x09	/* Control 2 */
#define  COM2_SSLEEP	  0x10	/* Soft sleep mode */
#define REG_PID		0x0a	/* Product ID MSB */
#define REG_VER		0x0b	/* Product ID LSB */
#define REG_COM3	0x0c	/* Control 3 */
#define  COM3_SWAP	  0x40	  /* Byte swap */
#define  COM3_SCALEEN	  0x08	  /* Enable scaling */
#define  COM3_DCWEN	  0x04	  /* Enable downsamp/crop/window */
#define REG_COM4	0x0d	/* Control 4 */
#define REG_COM5	0x0e	/* All "reserved" */
#define REG_COM6	0x0f	/* Control 6 */
#define REG_AECH	0x10	/* More bits of AEC value */
#define REG_CLKRC	0x11	/* Clocl control */
#define   CLK_EXT	  0x40	  /* Use external clock directly */
#define   CLK_SCALE	  0x3f	  /* Mask for internal clock scale */
#define REG_COM7	0x12	/* Control 7 */
#define   COM7_RESET	  0x80	  /* Register reset */
#define   COM7_FMT_MASK	  0x38
#define   COM7_FMT_VGA	  0x00
#define	  COM7_FMT_CIF	  0x20	  /* CIF format */
#define   COM7_FMT_QVGA	  0x10	  /* QVGA format */
#define   COM7_FMT_QCIF	  0x08	  /* QCIF format */
#define	  COM7_RGB	  0x04	  /* bits 0 and 2 - RGB format */
#define	  COM7_YUV	  0x00	  /* YUV */
#define	  COM7_BAYER	  0x01	  /* Bayer format */
#define	  COM7_PBAYER	  0x05	  /* "Processed bayer" */
#define REG_COM8	0x13	/* Control 8 */
#define   COM8_FASTAEC	  0x80	  /* Enable fast AGC/AEC */
#define   COM8_AECSTEP	  0x40	  /* Unlimited AEC step size */
#define   COM8_BFILT	  0x20	  /* Band filter enable */
#define   COM8_AGC	  0x04	  /* Auto gain enable */
#define   COM8_AWB	  0x02	  /* White balance enable */
#define   COM8_AEC	  0x01	  /* Auto exposure enable */
#define REG_COM9	0x14	/* Control 9  - gain ceiling */
#define REG_COM10	0x15	/* Control 10 */
#define   COM10_HSYNC	  0x40	  /* HSYNC instead of HREF */
#define   COM10_PCLK_HB	  0x20	  /* Suppress PCLK on horiz blank */
#define   COM10_HREF_REV  0x08	  /* Reverse HREF */
#define   COM10_VS_LEAD	  0x04	  /* VSYNC on clock leading edge */
#define   COM10_VS_NEG	  0x02	  /* VSYNC negative */
#define   COM10_HS_NEG	  0x01	  /* HSYNC negative */
#define REG_HSTART	0x17	/* Horiz start high bits */
#define REG_HSTOP	0x18	/* Horiz stop high bits */
#define REG_VSTART	0x19	/* Vert start high bits */
#define REG_VSTOP	0x1a	/* Vert stop high bits */
#define REG_PSHFT	0x1b	/* Pixel delay after HREF */
#define REG_MIDH	0x1c	/* Manuf. ID high */
#define REG_MIDL	0x1d	/* Manuf. ID low */
#define REG_MVFP	0x1e	/* Mirror / vflip */
#define   MVFP_MIRROR	  0x20	  /* Mirror image */
#define   MVFP_FLIP	  0x10	  /* Vertical flip */

#define REG_AEW		0x24	/* AGC upper limit */
#define REG_AEB		0x25	/* AGC lower limit */
#define REG_VPT		0x26	/* AGC/AEC fast mode op region */
#define REG_HSYST	0x30	/* HSYNC rising edge delay */
#define REG_HSYEN	0x31	/* HSYNC falling edge delay */
#define REG_HREF	0x32	/* HREF pieces */
#define REG_TSLB	0x3a	/* lots of stuff */
#define   TSLB_YLAST	  0x04	  /* UYVY or VYUY - see com13 */
#define REG_COM11	0x3b	/* Control 11 */
#define   COM11_NIGHT	  0x80	  /* NIght mode enable */
#define   COM11_NMFR	  0x60	  /* Two bit NM frame rate (1/8 of normal mode frame rate) */
#define   COM11_HZAUTO	  0x10	  /* Auto detect 50/60 Hz */
#define	  COM11_50HZ	  0x08	  /* Manual 50Hz select */
#define   COM11_EXP	  0x02
#define REG_COM12	0x3c	/* Control 12 */
#define   COM12_HREF	  0x80	  /* HREF always */
#define REG_COM13	0x3d	/* Control 13 */
#define   COM13_GAMMA	  0x80	  /* Gamma enable */
#define	  COM13_UVSAT	  0x40	  /* UV saturation auto adjustment */
#define   COM13_UVSWAP	  0x01	  /* V before U - w/TSLB */
#define REG_COM14	0x3e	/* Control 14 */
#define   COM14_DCWEN	  0x10	  /* DCW/PCLK-scale enable */
#define REG_EDGE	0x3f	/* Edge enhancement factor */
#define REG_COM15	0x40	/* Control 15 */
#define   COM15_R10F0	  0x00	  /* Data range 10 to F0 */
#define	  COM15_R01FE	  0x80	  /*            01 to FE */
#define   COM15_R00FF	  0xc0	  /*            00 to FF */
#define   COM15_RGB565	  0x10	  /* RGB565 output */
#define   COM15_RGB555	  0x30	  /* RGB555 output */
#define REG_COM16	0x41	/* Control 16 */
#define   COM16_AWBGAIN   0x08	  /* AWB gain enable */
#define REG_COM17	0x42	/* Control 17 */
#define   COM17_AECWIN	  0xc0	  /* AEC window - must match COM4 */
#define   COM17_CBAR	  0x08	  /* DSP Color bar */
#define REG_ABLC  0xb1  /* ABLC control */
#define   ABLC_ENABLE   0x04    /* ABLC enable */
#define REG_ABLC_TARGET 0xb3 /* ABLC Target */
#define REG_ABLC_RANGE  0xb5 /* ABLC Stable Range */
#define REG_BLC_B       0xbe /* Blue channel Black Level Compensation */
#define REG_BLC_R       0xbf /* Red channel Black Level Compensation */
#define REG_BLC_GB      0xc0 /* Gb channel Black Level Compensation */
#define REG_BLC_GR      0xc1 /* Gr channel Black Level Compensation */

/*
 * This matrix defines how the colors are generated, must be
 * tweaked to adjust hue and saturation.
 *
 * Order: v-red, v-green, v-blue, u-red, u-green, u-blue
 *
 * They are nine-bit signed quantities, with the sign bit
 * stored in 0x58.  Sign for v-red is bit 0, and up from there.
 */
#define	REG_CMATRIX_BASE 0x4f
#define   CMATRIX_LEN 6
#define REG_CMATRIX_SIGN 0x58


#define REG_BRIGHT	0x55	/* Brightness */
#define REG_CONTRAS	0x56	/* Contrast control */

#define REG_LCC1	0x62	/* Lens Corrections C1 */
#define REG_LCC2	0x63	/* Lens Corrections C2 */

#define REG_GFIX	0x69	/* Fix gain control */

#define REG_REG76	0x76	/* OV's name */
#define   R76_BLKPCOR	  0x80	  /* Black pixel correction enable */
#define   R76_WHTPCOR	  0x40	  /* White pixel correction enable */

#define REG_RGB444	0x8c	/* RGB 444 control */
#define   R444_ENABLE	  0x02	  /* Turn on RGB444, overrides 5x5 */
#define   R444_RGBX	  0x01	  /* Empty nibble at end */

#define REG_HAECC1	0x9f	/* Hist AEC/AGC control 1 */
#define REG_HAECC2	0xa0	/* Hist AEC/AGC control 2 */

#define REG_BD50MAX	0xa5	/* 50hz banding step limit */
#define REG_HAECC3	0xa6	/* Hist AEC/AGC control 3 */
#define REG_HAECC4	0xa7	/* Hist AEC/AGC control 4 */
#define REG_HAECC5	0xa8	/* Hist AEC/AGC control 5 */
#define REG_HAECC6	0xa9	/* Hist AEC/AGC control 6 */
#define REG_HAECC7	0xaa	/* Hist AEC/AGC control 7 */
#define REG_BD60MAX	0xab	/* 60hz banding step limit */



#define B1_CAMSELECT 0x01	/* one PIN/bit : 0 0 0 0 0 0 B1bis B1 */
#define B1_CAM_SAME_MODES 0x02 /* if false, only same resolution and pixel format. If true, same gain, wb, contrast, etc. settings */

#define LAST_REGISTER 0xC9


static struct ov7670_param {
	struct v4l2_format format; 	// size and pixel format (e.g. VGA, QVGA, YUV:4:2:2)
	struct v4l2_streamparm framerate;
	int redbalance;	// REG_RED
	int bluebalance;// REG_BLUE
	unsigned char agc_aec_awb;	// REG_COM8

	int brightness;// REG_BRIGHT
	int contrast;	// REG_CONTRAS
	int gain;		// REG_GAIN
	int expo;		// REG_AECHH,REG_AECH,REG_COM1
	//bool agc;		// REG_COM8
	//bool awb;		// REG_COM8
	//bool aec;		// REG_COM8
} ov7670_params;


/*
 * Information we maintain about a known sensor.
 */
struct ov7670_format_struct;  /* coming later */
struct ov7670_info {
	struct ov7670_format_struct *fmt;  /* Current format */
	unsigned char sat;		/* Saturation value */
	int hue;			/* Hue value */
	struct i2c_client *i2c_ctrl;
};

struct i2c_client *ov7670_i2c_client;

/*
 * The default register settings, as obtained from OmniVision.  There
 * is really no making sense of most of these - lots of "reserved" values
 * and such.
 *
 * These settings give VGA YUYV.
 */

struct regval_list {
	unsigned char reg_num;
	unsigned char value;
};

static struct regval_list ov7670_default_regs[] = {
	{ REG_COM7, COM7_RESET },
#if 1 /* Linux or OmniVision values? */
/*
 * Clock scale: 3 = 15fps
 *              2 = 20fps
 *              1 = 30fps
 */
	{ REG_CLKRC, 0x1 },	/* OV: clock scale (30 fps) */
	{ REG_TSLB,  0x04 },	/* OV */
	{ REG_COM7, 0 },	/* VGA */
	/*
	 * Set the hardware window.  These values from OV don't entirely
	 * make sense - hstop is less than hstart.  But they work...
	 */
	{ REG_HSTART, 0x13 },	{ REG_HSTOP, 0x01 },
	{ REG_HREF, 0xb6 },	{ REG_VSTART, 0x02 },
	{ REG_VSTOP, 0x7a },	{ REG_VREF, 0x0a },

	{ REG_COM3, 0 },	{ REG_COM14, 0 },
	/* Mystery scaling numbers */
	{ 0x70, 0x3a },		{ 0x71, 0x35 },
	{ 0x72, 0x11 },		{ 0x73, 0xf0 },
	{ 0xa2, 0x02 },		{ REG_COM10, 0x0 },

	/* Gamma curve values */
	{ 0x7a, 0x20 },		{ 0x7b, 0x10 },
	{ 0x7c, 0x1e },		{ 0x7d, 0x35 },
	{ 0x7e, 0x5a },		{ 0x7f, 0x69 },
	{ 0x80, 0x76 },		{ 0x81, 0x80 },
	{ 0x82, 0x88 },		{ 0x83, 0x8f },
	{ 0x84, 0x96 },		{ 0x85, 0xa3 },
	{ 0x86, 0xaf },		{ 0x87, 0xc4 },
	{ 0x88, 0xd7 },		{ 0x89, 0xe8 },

	/* AGC and AEC parameters.  Note we start by disabling those features,
	   then turn them only after tweaking the values. */
	{ REG_COM8, COM8_FASTAEC | COM8_AECSTEP | COM8_BFILT },
	{ REG_GAIN, 0 },	{ REG_AECH, 0 },
	{ REG_COM4, 0x40 }, /* magic reserved bit */
	{ REG_COM9, 0x38 }, /* 4x gain + magic rsvd bit */
	{ REG_BD50MAX, 0x05 },	{ REG_BD60MAX, 0x07 },
	{ REG_AEW, 0x95 },	{ REG_AEB, 0x33 },
	{ REG_VPT, 0xe3 },	{ REG_HAECC1, 0x78 },
	{ REG_HAECC2, 0x68 },	{ 0xa1, 0x03 }, /* magic */
	{ REG_HAECC3, 0xd8 },	{ REG_HAECC4, 0xd8 },
	{ REG_HAECC5, 0xf0 },	{ REG_HAECC6, 0x90 },
	{ REG_HAECC7, 0x94 },
	{ REG_COM8, COM8_FASTAEC|COM8_AECSTEP|COM8_BFILT|COM8_AGC|COM8_AEC },

	/* Almost all of these are magic "reserved" values.  */
	{ REG_COM5, 0x61 },	{ REG_COM6, 0x4b },
	{ 0x16, 0x02 },		{ REG_MVFP, 0x07 },
	{ 0x21, 0x02 },		{ 0x22, 0x91 },
	{ 0x29, 0x07 },		{ 0x33, 0x0b },
	{ 0x35, 0x0b },		{ 0x37, 0x1d },
	{ 0x38, 0x71 },		{ 0x39, 0x2a },
	{ REG_COM12, 0x78 },	{ 0x4d, 0x40 },
	{ 0x4e, 0x20 },		{ REG_GFIX, 0 },
	{ 0x6b, 0x4a },		{ 0x74, 0x10 },
	{ 0x8d, 0x4f },		{ 0x8e, 0 },
	{ 0x8f, 0 },		{ 0x90, 0 },
	{ 0x91, 0 },		{ 0x96, 0 },
	{ 0x9a, 0 },		{ 0xb0, 0x84 },
	{ REG_ABLC, 0x00 },		{ 0xb2, 0x0e },
	{ REG_ABLC_TARGET, 0xff }, { REG_ABLC_RANGE, 0xff}, { 0xb8, 0x0a },
	{ REG_BLC_B, 0x00 },
	{ REG_BLC_R, 0x00 },
	{ REG_BLC_GB, 0x00 },
	{ REG_BLC_GR, 0x00 },

	/* More reserved magic, some of which tweaks white balance */
	{ 0x43, 0x0a },		{ 0x44, 0xf0 },
	{ 0x45, 0x34 },		{ 0x46, 0x58 },
	{ 0x47, 0x28 },		{ 0x48, 0x3a },
	{ 0x59, 0x88 },		{ 0x5a, 0x88 },
	{ 0x5b, 0x44 },		{ 0x5c, 0x67 },
	{ 0x5d, 0x49 },		{ 0x5e, 0x0e },
	{ 0x6c, 0x0a },		{ 0x6d, 0x55 },
	{ 0x6e, 0x11 },		{ 0x6f, 0x9f }, /* "9e for advance AWB" */
	{ 0x6a, 0x40 },		{ REG_BLUE, 0x40 },
	{ REG_RED, 0x60 },
	{ REG_COM8, COM8_FASTAEC|COM8_AECSTEP|COM8_BFILT|COM8_AGC|COM8_AEC|COM8_AWB },

	/* Matrix coefficients */
	{ 0x4f, 0x80 },		{ 0x50, 0x80 },
	{ 0x51, 0 },		{ 0x52, 0x22 },
	{ 0x53, 0x5e },		{ 0x54, 0x80 },
	{ 0x58, 0x1e },

	{ REG_COM16, COM16_AWBGAIN },	{ REG_EDGE, 0 },
	{ 0x75, 0x02 },		{ 0x76, 0xe0 },
	{ 0x4c, 0 },		{ 0x77, 0x01 },
	{ REG_COM13, 0x80 },	{ 0x4b, 0x09 },
	{ 0xc9, 0x60 },		{ REG_COM16, COM16_AWBGAIN },
	{ 0x56, 0x40 },   { 0x57, 0x80 },

	{ 0x34, 0x11 },		{ REG_COM11, COM11_EXP|COM11_HZAUTO },
	{ 0xa4, 0x88 },		{ 0x96, 0 },
	{ 0x97, 0x30 },		{ 0x98, 0x20 },
	{ 0x99, 0x30 },		{ 0x9a, 0x84 },
	{ 0x9b, 0x29 },		{ 0x9c, 0x03 },
	{ 0x9d, 0x4c },		{ 0x9e, 0x3f },
	{ 0x78, 0x04 },

	//{ REG_COM17, COM17_CBAR },

	/* Extra-weird stuff.  Some sort of multiplexor register */
	{ 0x79, 0x01 },		{ 0xc8, 0xf0 },
	{ 0x79, 0x0f },		{ 0xc8, 0x00 },
	{ 0x79, 0x10 },		{ 0xc8, 0x7e },
	{ 0x79, 0x0a },		{ 0xc8, 0x80 },
	{ 0x79, 0x0b },		{ 0xc8, 0x01 },
	{ 0x79, 0x0c },		{ 0xc8, 0x0f },
	{ 0x79, 0x0d },		{ 0xc8, 0x20 },
	{ 0x79, 0x09 },		{ 0xc8, 0x80 },
	{ 0x79, 0x02 },		{ 0xc8, 0xc0 },
	{ 0x79, 0x03 },		{ 0xc8, 0x40 },
	{ 0x79, 0x05 },		{ 0xc8, 0x30 },
	{ 0x79, 0x26 },
#else
	{ 0x12, 0x80 }, { 0x11, 0x03 },
	{ 0x3a, 0x04 }, { 0x12, 0x00 },
	{ 0x17, 0x13 }, { 0x18, 0x01 },
	{ 0x32, 0xb6 }, { 0x19, 0x02 },
	{ 0x1a, 0x7a }, { 0x03, 0x0a },
	{ 0x0c, 0x00 }, { 0x3e, 0x00 },
	{ 0x70, 0x3a }, { 0x71, 0x35 },
	{ 0x72, 0x11 }, { 0x73, 0xf0 },
	{ 0xa2, 0x02 }, { 0x7a, 0x20 },
	{ 0x7b, 0x1c }, { 0x7c, 0x28 },
	{ 0x7d, 0x3c }, { 0x7e, 0x5a },
	{ 0x7f, 0x68 }, { 0x80, 0x76 },
	{ 0x81, 0x80 }, { 0x82, 0x88 },
	{ 0x83, 0x8f }, { 0x84, 0x96 },
	{ 0x85, 0xa3 }, { 0x86, 0xaf },
	{ 0x87, 0xc4 }, { 0x88, 0xd7 },
	{ 0x89, 0xe8 }, { 0x13, 0xe0 },
	{ 0x00, 0x00 }, { 0x10, 0x00 },
	{ 0x0d, 0x40 }, { 0x14, 0x38 },
	{ 0xa5, 0x05 }, { 0xab, 0x07 },
	{ 0x24, 0x95 }, { 0x25, 0x33 },
	{ 0x26, 0xe3 }, { 0x9f, 0x78 },
	{ 0xa0, 0x68 }, { 0xa1, 0x0b },
	{ 0xa6, 0xd8 }, { 0xa7, 0xd8 },
	{ 0xa8, 0xf0 }, { 0xa9, 0x90 },
	{ 0xaa, 0x94 }, { 0x13, 0xe5 },
	{ 0x0e, 0x61 }, { 0x0f, 0x4b },
	{ 0x16, 0x02 }, { 0x21, 0x02 },
	{ 0x22, 0x91 }, { 0x29, 0x07 },
	{ 0x33, 0x03 }, { 0x35, 0x0b },
	{ 0x37, 0x1c }, { 0x38, 0x71 },
	{ 0x3c, 0x78 }, { 0x4d, 0x40 },
	{ 0x4e, 0x20 }, { 0x69, 0x55 },
	{ 0x6b, 0x4a }, { 0x74, 0x19 },
	{ 0x8d, 0x4f }, { 0x8e, 0x00 },
	{ 0x8f, 0x00 }, { 0x90, 0x00 },
	{ 0x91, 0x00 }, { 0x96, 0x00 },
	{ 0x9a, 0x80 }, { 0xb0, 0x8c },
	{ 0xb1, 0x0c }, { 0xb2, 0x0e },
	{ 0xb3, 0x82 }, { 0xb8, 0x0a },
	{ 0x43, 0x0a }, { 0x44, 0xf0 },
	{ 0x45, 0x34 }, { 0x46, 0x62 },
	{ 0x47, 0x24 }, { 0x48, 0x39 },
	{ 0x59, 0x88 }, { 0x5a, 0x92 },
	{ 0x5b, 0x44 }, { 0x5c, 0x80 },
	{ 0x5d, 0x48 }, { 0x5e, 0x0e },
	{ 0x6c, 0x0e }, { 0x6d, 0x55 },
	{ 0x6e, 0x11 }, { 0x6f, 0x9e },
	{ 0x6a, 0x40 }, { 0x01, 0x40 },
	{ 0x02, 0x40 }, { 0x13, 0xe7 },
	{ 0x4f, 0x80 }, { 0x50, 0x80 },
	{ 0x51, 0x00 }, { 0x52, 0x22 },
	{ 0x53, 0x5e }, { 0x54, 0x80 },
	{ 0x58, 0x9e }, { 0x41, 0x08 },
	{ 0x3f, 0x00 }, { 0x75, 0x05 },
	{ 0x76, 0xe1 }, { 0x4c, 0x00 },
	{ 0x77, 0x01 }, { 0x3d, 0xc2 },
	{ 0x4b, 0x09 }, { 0xc9, 0x60 },
	{ 0x41, 0x38 }, { 0x56, 0x40 },
	{ 0x34, 0x11 }, { 0x3b, 0x02 },
	{ 0xa4, 0x88 }, { 0x96, 0x00 },
	{ 0x97, 0x30 }, { 0x98, 0x20 },
	{ 0x99, 0x20 }, { 0x9a, 0x84 },
	{ 0x9b, 0x29 }, { 0x9c, 0x03 },
	{ 0x9d, 0x4c }, { 0x9e, 0x3f },
	{ 0x78, 0x04 }, { 0x79, 0x01 },
	{ 0xc8, 0xf0 }, { 0x79, 0x0f },
	{ 0xc8, 0x20 }, { 0x79, 0x10 },
	{ 0xc8, 0x7e }, { 0x79, 0x0b },
	{ 0xc8, 0x01 }, { 0x79, 0x0c },
	{ 0xc8, 0x07 }, { 0x79, 0x0d },
	{ 0xc8, 0x20 }, { 0x79, 0x09 },
	{ 0xc8, 0x80 }, { 0x79, 0x02 },
	{ 0xc8, 0xc0 }, { 0x79, 0x03 },
	{ 0xc8, 0x40 }, { 0x79, 0x05 },
	{ 0xc8, 0x30 }, { 0x79, 0x26 },
	{ 0x64, 0x10 }, { 0x65, 0x00 },
	{ 0x94, 0x0e }, { 0x95, 0x19 },
	{ 0x66, 0x05 }, { 0x04, 0x40 },
#endif
	{ 0xff, 0xff },	/* END MARKER */
};


/*
 * Here we'll try to encapsulate the changes for just the output
 * video format.
 *
 * RGB656 and YUV422 come from OV; RGB444 is homebrewed.
 *
 * IMPORTANT RULE: the first entry must be for COM7, see ov7670_s_fmt for why.
 */


static struct regval_list ov7670_fmt_yuv422[] = {
	{ REG_COM7, 0x0 },  /* Selects YUV mode */
	{ REG_RGB444, 0 },	/* No RGB444 please */
	{ REG_COM1, COM1_CCIR656 },
	{ REG_COM15, COM15_R01FE },
	{ REG_COM9, 0x38 }, /* 4x gain ceiling; 0x8 is reserved bit */
	{ 0x4f, 0x80 }, 	/* "matrix coefficient 1" */
	{ 0x50, 0x80 }, 	/* "matrix coefficient 2" */
	{ 0x51, 0    },		/* vb */
	{ 0x52, 0x22 }, 	/* "matrix coefficient 4" */
	{ 0x53, 0x5e }, 	/* "matrix coefficient 5" */
	{ 0x54, 0x80 }, 	/* "matrix coefficient 6" */
	{ REG_TSLB,  0x04 },
	{ REG_COM13, COM13_GAMMA|COM13_UVSAT },
	{ 0xff, 0xff },
};

static struct regval_list ov7670_fmt_rgb565[] = {
	{ REG_COM7, COM7_RGB },	/* Selects RGB mode */
	{ REG_RGB444, 0 },	/* No RGB444 please */
	{ REG_COM1, COM1_CCIR656 },
	{ REG_COM15, COM15_R01FE|COM15_RGB565 },
	{ REG_COM9, 0x38 }, 	/* 16x gain ceiling; 0x8 is reserved bit */
	{ 0x4f, 0xb3 }, 	/* "matrix coefficient 1" */
	{ 0x50, 0xb3 }, 	/* "matrix coefficient 2" */
	{ 0x51, 0    },		/* vb */
	{ 0x52, 0x3d }, 	/* "matrix coefficient 4" */
	{ 0x53, 0xa7 }, 	/* "matrix coefficient 5" */
	{ 0x54, 0xe4 }, 	/* "matrix coefficient 6" */
	{ REG_COM13, COM13_GAMMA|COM13_UVSAT },
	{ 0xff, 0xff },
};

static struct regval_list ov7670_fmt_rgb444[] = {
	{ REG_COM7, COM7_RGB },	/* Selects RGB mode */
	{ REG_RGB444, R444_ENABLE },	/* Enable xxxxrrrr ggggbbbb */
	{ REG_COM1, COM1_CCIR656 },	/* Magic reserved bit */
	{ REG_COM15, COM15_R01FE|COM15_RGB565 }, /* Data range needed? */
	{ REG_COM9, 0x38 }, 	/* 16x gain ceiling; 0x8 is reserved bit */
	{ 0x4f, 0xb3 }, 	/* "matrix coefficient 1" */
	{ 0x50, 0xb3 }, 	/* "matrix coefficient 2" */
	{ 0x51, 0    },		/* vb */
	{ 0x52, 0x3d }, 	/* "matrix coefficient 4" */
	{ 0x53, 0xa7 }, 	/* "matrix coefficient 5" */
	{ 0x54, 0xe4 }, 	/* "matrix coefficient 6" */
	{ REG_COM13, COM13_GAMMA|COM13_UVSAT|0x2 },  /* Magic rsvd bit */
	{ 0xff, 0xff },
};

static struct regval_list ov7670_fmt_raw[] = {
	{ REG_COM7, COM7_BAYER },
	{ REG_COM1, COM1_CCIR656 },	/* Magic reserved bit */
	{ REG_COM13, 0x08 }, /* No gamma, magic rsvd bit */
	{ REG_COM16, 0x3d }, /* Edge enhancement, denoise */
	{ REG_REG76, 0xe0 }, /* Pix correction, magic rsvd */
	{ 0xff, 0xff },
};



/*
 * Low-level register I/O.
 */

static int ov7670_read(struct i2c_client *c, unsigned char reg,
		unsigned char *value)
{
    //Bri. : added retries
    char nbretries=0;
	int ret;
    do {
        ret = i2c_smbus_read_byte_data(c, reg);
        if (ret<0) {
            msleep(1);
            nbretries++;
        }
    } while ((ret < 0) && (nbretries <= OV7670_I2C_MAXRETRIES) );

    if (ret < 0) {
        return ret;
    } else {
        (*value) = (unsigned char) (ret & 0xff);
        return 0;
    }
}


static int ov7670_write(struct i2c_client *c, unsigned char reg,
		unsigned char value)
{
    //Bri. : added retries
    char nbretries=0;
    int ret=0;
    do {
        ret = i2c_smbus_write_byte_data(c, reg, value);
        if (ret<0) {
            msleep(1);
            nbretries++;
        }
    } while ((ret < 0) && (nbretries <= OV7670_I2C_MAXRETRIES) );
   return ret;
}


/*
 * Write a list of register settings; ff/ff stops the process.
 */
static int ov7670_write_array(struct i2c_client *c, struct regval_list *vals)
{
    int ret=0;
	while (vals->reg_num != 0xff || vals->value != 0xff) {
        ret = ov7670_write(c, vals->reg_num, vals->value);
		if (ret < 0)
			return ret;
		vals++;
		mdelay(10);
	}
	return 0;
}


/*
 * Write a list of register settings and check them; ff/ff stops the process.
 */
static int ov7670_write_and_check_array(struct i2c_client *c, struct regval_list *vals)
{
  int ret=0;
	while (vals->reg_num != 0xff || vals->value != 0xff) {
    ret = ov7670_write(c, vals->reg_num, vals->value);
		if (ret < 0)
			return ret;

    //check writen value
    {
      char nbretries=0;
	    int ret;
      do {
        ret = i2c_smbus_read_byte_data(c, vals->reg_num);
        if (ret<0) {
          msleep(1);
          nbretries++;
        }
      } while ((ret < 0) && (nbretries <= OV7670_I2C_MAXRETRIES) );

      if (ret < 0) {
        return ret;
      } else {
        if ( vals->value != (unsigned char)(ret & 0xff) ) {
          return (- ((vals->reg_num << 16) | 0x00000BAD) );
        }
      }
    }

		vals++;
		mdelay(10);
	}
	return 0;
}


/*
 * Check a list of register settings with values in the array; ff/ff stops the process.
 */
static int ov7670_check_array(struct i2c_client *c, struct regval_list *vals)
{
  int ret=0;
	while (vals->reg_num != 0xff || vals->value != 0xff) {
    //Bri. : added retries
    char nbretries=0;
	  int ret;
    do {
      ret = i2c_smbus_read_byte_data(c, vals->reg_num);
      if (ret<0) {
        msleep(1);
        nbretries++;
      }
    } while ((ret < 0) && (nbretries <= OV7670_I2C_MAXRETRIES) );

    if (ret < 0) {
      return ret;
    } else {
      if ( vals->value != (unsigned char)(ret & 0xff) ) {
        return (- ((vals->reg_num << 16) | 0x00000BAD) );
      }
    }

		vals++;
		mdelay(10);
	}
	return 0;
}


/*
 * Dump the registers.
 */
#ifdef REGDUMP
static int ov7670_read_dump(struct i2c_client *c)
{
	unsigned char reg, val;

	printk(KERN_INFO "OV7670 Register dump\n");
	for (reg = 0; reg < 0xff; reg++)
		if (ov7670_read(c, reg, &val) >= 0)
			printk(KERN_INFO "\t%x\t%x\n", (unsigned)reg, (unsigned)val);
		else
			printk(KERN_INFO "\t%x\tfailed\n", (unsigned)reg);

	return 0;
}
#endif


/*
 * Stuff that knows about the sensor.
 */
static void ov7670_reset(struct i2c_client *client)
{
	ov7670_write(client, REG_COM7, COM7_RESET);
	msleep(1);
}


static int ov7670_init(struct i2c_client *client)
{
    //hack to ensure YUV ordering
    int returnVal;
    unsigned char val;
    unsigned char reg = (unsigned)0x32;
    
    do
    {
        printk(KERN_INFO "Applying default params...\n");
        
        returnVal = ov7670_write_array(client, ov7670_default_regs);
        
        ov7670_read(client, reg, &val);
        
    } while((unsigned)val != (unsigned)0xb6);
    
    return returnVal;
}


static int ov7670_detect(struct i2c_client *client)
{
	unsigned char v;
	int ret;

	printk(KERN_INFO "Trying to detect OmniVision 7670/7672 I2C adapters\n");
	ret = ov7670_init(client);
	printk(KERN_DEBUG "ov7670_init returned %d\n", ret);
	if (ret < 0)
		return ret;
	printk(KERN_DEBUG "Phase 1\n");
	ret = ov7670_read(client, REG_MIDH, &v);
	if (ret < 0)
		return ret;
	printk(KERN_DEBUG "Phase 2\n");
	if (v != 0x7f) /* OV manuf. id. */
		return -ENODEV;
	printk(KERN_DEBUG "Phase 3\n");
	ret = ov7670_read(client, REG_MIDL, &v);
	if (ret < 0)
		return ret;
	printk(KERN_DEBUG "Phase 4\n");
	if (v != 0xa2)
		return -ENODEV;
	printk(KERN_DEBUG "Phase 5\n");
	/*
	 * OK, we know we have an OmniVision chip...but which one?
	 */
	ret = ov7670_read(client, REG_PID, &v);
	if (ret < 0)
		return ret;
	printk(KERN_DEBUG "Phase 6\n");
	if (v != 0x76)  /* PID + VER = 0x76 / 0x73 */
		return -ENODEV;
	printk(KERN_DEBUG "Phase 7\n");
	ret = ov7670_read(client, REG_VER, &v);
	if (ret < 0)
		return ret;
	printk(KERN_DEBUG "Phase 8\n");
	if (v != 0x73)  /* PID + VER = 0x76 / 0x73 */
		return -ENODEV;
	printk(KERN_DEBUG "Phase 9\n");
	printk(KERN_INFO "OmniVision 7670/7671 I2C Found\n");

	return 0;
}


/*
 * Store information about the video data format.  The color matrix
 * is deeply tied into the format, so keep the relevant values here.
 * The magic matrix nubmers come from OmniVision.
 */
static struct ov7670_format_struct {
	__u8 *desc;
	__u32 pixelformat;
	struct regval_list *regs;
	int cmatrix[CMATRIX_LEN];
	int bpp;   /* Bytes per pixel */
} ov7670_formats[] = {
	{
		.desc		= "YUYV 4:2:2",
		.pixelformat	= V4L2_PIX_FMT_YUYV,
		.regs 		= ov7670_fmt_yuv422,
		.cmatrix	= { 128, -128, 0, -34, -94, 128 },
// Why is is null ?? This leads to B&W images when hue/saturation is
//  changed. Julien
//.cmatrix	= { 0, 0, 0, 0, 0, 0 },
		.bpp		= 2,
	},
	{
		.desc		= "RGB 444",
		.pixelformat	= V4L2_PIX_FMT_RGB444,
		.regs		= ov7670_fmt_rgb444,
		.cmatrix	= { 179, -179, 0, -61, -176, 228 },
		.bpp		= 2,
	},
	{
		.desc		= "RGB 565",
		.pixelformat	= V4L2_PIX_FMT_RGB565,
		.regs		= ov7670_fmt_rgb565,
		.cmatrix	= { 179, -179, 0, -61, -176, 228 },
		.bpp		= 2,
	},
	{
		.desc		= "Raw RGB Bayer",
		.pixelformat	= V4L2_PIX_FMT_SBGGR8,
		.regs 		= ov7670_fmt_raw,
		.cmatrix	= { 0, 0, 0, 0, 0, 0 },
		.bpp		= 1
	},
};
#define N_OV7670_FMTS ARRAY_SIZE(ov7670_formats)


/*
 * Then there is the issue of window sizes.  Try to capture the info here.
 */

/*
 * QCIF mode is done (by OV) in a very strange way - it actually looks like
 * VGA with weird scaling options - they do *not* use the canned QCIF mode
 * which is allegedly provided by the sensor.  So here's the weird register
 * settings.
 */
static struct regval_list ov7670_qcif_regs[] = {
	{ REG_COM3, COM3_SCALEEN|COM3_DCWEN },
	{ REG_COM3, COM3_DCWEN },
	{ REG_COM14, COM14_DCWEN | 0x01},
	{ 0x73, 0xf1 },
	{ 0xa2, 0x52 },
	{ 0x7b, 0x1c },
	{ 0x7c, 0x28 },
	{ 0x7d, 0x3c },
	{ 0x7f, 0x69 },
	{ REG_COM9, 0x38 },
	{ 0xa1, 0x0b },
	{ 0x74, 0x19 },
	{ 0x9a, 0x80 },
	{ 0x43, 0x14 },
	{ REG_COM13, 0xc0 },
	{ 0xff, 0xff },
};

static struct ov7670_win_size {
	int	width;
	int	height;
	unsigned char com7_bit;
	int	hstart;		/* Start/stop values for the camera.  Note */
	int	hstop;		/* that they do not always make complete */
	int	vstart;		/* sense to humans, but evidently the sensor */
	int	vstop;		/* will do the right thing... */
	struct regval_list *regs; /* Regs to tweak */
/* h/vref stuff */
} ov7670_win_sizes[] = {
	/* VGA */
	{
		.width		= VGA_WIDTH,
		.height		= VGA_HEIGHT,
		.com7_bit	= COM7_FMT_VGA,
		.hstart		= 158,		/* These values from */
		.hstop		=  14,		/* Omnivision */
		.vstart		=  10,
		.vstop		= 490,
		.regs 		= NULL,
	},
	/* CIF */
	{
		.width		= CIF_WIDTH,
		.height		= CIF_HEIGHT,
		.com7_bit	= COM7_FMT_CIF,
		.hstart		= 170,		/* Empirically determined */
		.hstop		=  90,
		.vstart		=  14,
		.vstop		= 494,
		.regs 		= NULL,
	},
	/* QVGA */
	{
		.width		= QVGA_WIDTH,
		.height		= QVGA_HEIGHT,
		.com7_bit	= COM7_FMT_QVGA,
		.hstart		= 164,		/* Empirically determined */
		.hstop		=  20,
		.vstart		=  14,
		.vstop		= 494,
		.regs 		= NULL,
	},
	/* QCIF */
	{
		.width		= QCIF_WIDTH,
		.height		= QCIF_HEIGHT,
		.com7_bit	= COM7_FMT_VGA, /* see comment above */
		.hstart		= 456,		/* Empirically determined */
		.hstop		=  24,
		.vstart		=  14,
		.vstop		= 494,
		.regs 		= ov7670_qcif_regs,
	},
};

#define N_WIN_SIZES (sizeof(ov7670_win_sizes)/sizeof(ov7670_win_sizes[0]))


/*
 * Store a set of start/stop values into the camera.
 */
static int ov7670_set_hw(struct i2c_client *client, int hstart, int hstop,
		int vstart, int vstop)
{
	int ret;
	unsigned char v;
/*
 * Horizontal: 11 bits, top 8 live in hstart and hstop.  Bottom 3 of
 * hstart are in href[2:0], bottom 3 of hstop in href[5:3].  There is
 * a mystery "edge offset" value in the top two bits of href.
 */
	ret =  ov7670_write(client, REG_HSTART, (hstart >> 3) & 0xff);
	ret += ov7670_write(client, REG_HSTOP, (hstop >> 3) & 0xff);
	ret += ov7670_read(client, REG_HREF, &v);
	v = (v & 0xc0) | ((hstop & 0x7) << 3) | (hstart & 0x7);
	msleep(100);
	ret += ov7670_write(client, REG_HREF, v);
/*
 * Vertical: similar arrangement, but only 10 bits.
 */
	ret += ov7670_write(client, REG_VSTART, (vstart >> 2) & 0xff);
	ret += ov7670_write(client, REG_VSTOP, (vstop >> 2) & 0xff);
	ret += ov7670_read(client, REG_VREF, &v);
	v = (v & 0xf0) | ((vstop & 0x3) << 2) | (vstart & 0x3);
	msleep(100);
	ret += ov7670_write(client, REG_VREF, v);
	return ret;
}


/*
 * Enumerate formats.
 */
static int ov7670_enum_fmt(struct i2c_client *c, struct v4l2_fmtdesc *fmt)
{
	struct ov7670_format_struct *ofmt;

	if (fmt->index >= N_OV7670_FMTS)
		return -EINVAL;

	ofmt = ov7670_formats + fmt->index;
	fmt->flags = 0;
	strcpy(fmt->description, ofmt->desc);
	fmt->pixelformat = ofmt->pixelformat;
	return 0;
}


/*
 * Try a format.
 */
static int ov7670_try_fmt(struct i2c_client *c, struct v4l2_format *fmt,
		struct ov7670_format_struct **ret_fmt,
		struct ov7670_win_size **ret_wsize)
{
	int index;
	struct ov7670_win_size *wsize;
	struct v4l2_pix_format *pix = &fmt->fmt.pix;

#if 0
	printk(KERN_INFO "Asking for format %x\n", (int)pix->pixelformat);
#endif
	for (index = 0; index < N_OV7670_FMTS; index++) {
#if 0
		printk(KERN_INFO "Have format %x\n", (int)ov7670_formats[index].pixelformat);
#endif
		if (ov7670_formats[index].pixelformat == pix->pixelformat)
			break;
	}
	if (index >= N_OV7670_FMTS) {
		printk(KERN_INFO "No such format %d\n", (int)index);
		return -EINVAL;
	}
	if (ret_fmt != NULL)
		*ret_fmt = ov7670_formats + index;
	/*
	 * Fields: the OV devices claim to be progressive.
	 */
	if (pix->field == V4L2_FIELD_ANY)
		pix->field = V4L2_FIELD_NONE;
	else if (pix->field != V4L2_FIELD_NONE) {
		printk(KERN_INFO "V4L2_FIELD_ANY required\n");
		return -EINVAL;
	}
	/*
	 * Round requested image size down to the nearest
	 * we support, but not below the smallest.
	 */
#if 0
	printk(KERN_INFO "Asking for size %dx%d\n", (int)pix->width, (int)pix->height);
#endif
	for (wsize = ov7670_win_sizes; wsize < ov7670_win_sizes + N_WIN_SIZES;
	     wsize++)
		if (pix->width >= wsize->width && pix->height >= wsize->height)
			break;
	if (wsize >= ov7670_win_sizes + N_WIN_SIZES)
		wsize--;   /* Take the smallest one */
	if (ret_wsize != NULL)
		*ret_wsize = wsize;
	/*
	 * Note the size we'll actually handle.
	 */
	pix->width = wsize->width;
	pix->height = wsize->height;
	pix->bytesperline = pix->width*ov7670_formats[index].bpp;
	pix->sizeimage = pix->height*pix->bytesperline;
#if 0
	printk(KERN_INFO "Have size %dx%d\n", (int)pix->width, (int)pix->height);
#endif
	return 0;
}

/*
 * Set a format.
 */
static int ov7670_s_fmt(struct i2c_client *c, struct v4l2_format *fmt)
{
	int ret;
	struct ov7670_format_struct *ovfmt;
	struct ov7670_win_size *wsize;
	struct ov7670_info *info = i2c_get_clientdata(c);
	unsigned char com7, clkrc;

	ret = ov7670_try_fmt(c, fmt, &ovfmt, &wsize);
	if (ret)
		return ret;
	/*
	 * HACK: if we're running rgb565 we need to grab then rewrite
	 * CLKRC.  If we're *not*, however, then rewriting clkrc hoses
	 * the colors.
	 */
	if (fmt->fmt.pix.pixelformat == V4L2_PIX_FMT_RGB565) {
		ret = ov7670_read(c, REG_CLKRC, &clkrc);
		if (ret)
			return ret;
	}
	/*
	 * COM7 is a pain in the ass, it doesn't like to be read then
	 * quickly written afterward.  But we have everything we need
	 * to set it absolutely here, as long as the format-specific
	 * register sets list it first.
	 */
	com7 = ovfmt->regs[0].value;
	com7 |= wsize->com7_bit;
	ov7670_write(c, REG_COM7, com7);
	/*
	 * Now write the rest of the array.  Also store start/stops
	 */
	ov7670_write_array(c, ovfmt->regs + 1);
	ov7670_set_hw(c, wsize->hstart, wsize->hstop, wsize->vstart,
			wsize->vstop);
	ret = 0;
	if (wsize->regs)
		ret = ov7670_write_array(c, wsize->regs);
	info->fmt = ovfmt;

	if (fmt->fmt.pix.pixelformat == V4L2_PIX_FMT_RGB565 && ret == 0)
		ret = ov7670_write(c, REG_CLKRC, clkrc);
	
	/* PATCH to avoid page default on fmt->fmt.pix.pixelformat later */
	fmt->fmt.pix.pixelformat = 0x56595559;

	return ret;
}

/*
 * Implement G/S_PARM.  There is a "high quality" mode we could try
 * to do someday; for now, we just do the frame rate tweak.
 */
static int ov7670_g_parm(struct i2c_client *c, struct v4l2_streamparm *parms)
{
	struct v4l2_captureparm *cp = &parms->parm.capture;
	unsigned char clkrc;
	int ret;

	if (parms->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;
	ret = ov7670_read(c, REG_CLKRC, &clkrc);
	if (ret < 0)
		return ret;
	memset(cp, 0, sizeof(struct v4l2_captureparm));
	cp->capability = V4L2_CAP_TIMEPERFRAME;
	cp->timeperframe.numerator = 1;
	cp->timeperframe.denominator = OV7670_FRAME_RATE;
	if ((clkrc & CLK_EXT) == 0 && (clkrc & CLK_SCALE) > 1)
		cp->timeperframe.denominator /= (clkrc & CLK_SCALE);
	return 0;
}

static int ov7670_s_parm(struct i2c_client *c, struct v4l2_streamparm *parms)
{
	struct v4l2_captureparm *cp = &parms->parm.capture;
	struct v4l2_fract *tpf = &cp->timeperframe;
	unsigned char clkrc;
	int ret, div;

	if (parms->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;
	if (cp->extendedmode != 0)
		return -EINVAL;
	/*
	 * CLKRC has a reserved bit, so let's preserve it.
	 */
	ret = ov7670_read(c, REG_CLKRC, &clkrc);
	if (ret < 0)
		return ret;
	if (tpf->numerator == 0 || tpf->denominator == 0)
		div = 1;  /* Reset to full rate */
	else
		div = (tpf->numerator*OV7670_FRAME_RATE)/tpf->denominator;
	if (div == 0)
		div = 1;
	else if (div > CLK_SCALE)
		div = CLK_SCALE;
	clkrc = (clkrc & 0x80) | div;
	tpf->numerator = 1;
	tpf->denominator = OV7670_FRAME_RATE/div;
	return ov7670_write(c, REG_CLKRC, clkrc);
}



/*
 * Code for dealing with controls.
 */





static int ov7670_store_cmatrix(struct i2c_client *client,
		int matrix[CMATRIX_LEN])
{
	int i, ret;
	unsigned char signbits;

	/*
	 * Weird crap seems to exist in the upper part of
	 * the sign bits register, so let's preserve it.
	 */
	ret = ov7670_read(client, REG_CMATRIX_SIGN, &signbits);
	signbits &= 0xc0;

	for (i = 0; i < CMATRIX_LEN; i++) {
		unsigned char raw;

		if (matrix[i] < 0) {
			signbits |= (1 << i);
			if (matrix[i] < -255)
				raw = 0xff;
			else
				raw = (-1 * matrix[i]) & 0xff;
		}
		else {
			if (matrix[i] > 255)
				raw = 0xff;
			else
				raw = matrix[i] & 0xff;
		}
        //Bri.
        msleep(10);
		ret += ov7670_write(client, REG_CMATRIX_BASE + i, raw);
	}
    //Bri.
    msleep(10);
	ret += ov7670_write(client, REG_CMATRIX_SIGN, signbits);
	return ret;
}


/*
 * Hue also requires messing with the color matrix.  It also requires
 * trig functions, which tend not to be well supported in the kernel.
 * So here is a simple table of sine values, 0-90 degrees, in steps
 * of five degrees.  Values are multiplied by 1000.
 *
 * The following naive approximate trig functions require an argument
 * carefully limited to -180 <= theta <= 180.
 */
#define SIN_STEP 5
static const int ov7670_sin_table[] = {
	   0,	 87,   173,   258,   342,   422,
	 499,	573,   642,   707,   766,   819,
	 866,	906,   939,   965,   984,   996,
	1000
};

static int ov7670_sine(int theta)
{
	int chs = 1;
	int sine;

	if (theta < 0) {
		theta = -theta;
		chs = -1;
	}
	if (theta <= 90)
		sine = ov7670_sin_table[theta/SIN_STEP];
	else {
		theta -= 90;
		sine = 1000 - ov7670_sin_table[theta/SIN_STEP];
	}
	return sine*chs;
}

static int ov7670_cosine(int theta)
{
	theta = 90 - theta;
	if (theta > 180)
		theta -= 360;
	else if (theta < -180)
		theta += 360;
	return ov7670_sine(theta);
}




static void ov7670_calc_cmatrix(struct ov7670_info *info,
		int matrix[CMATRIX_LEN])
{
	int i;
	/*
	 * Apply the current saturation setting first.
	 */
	for (i = 0; i < CMATRIX_LEN; i++)
		matrix[i] = (info->fmt->cmatrix[i]*info->sat) >> 7;

/* Test debug, Julien
	for (i = 0; i < CMATRIX_LEN; i++)
		printk(KERN_INFO "Val %d=%d from %d and %d\n", i, matrix[i], info->fmt->cmatrix[i], info->sat);
*/
	/*
	 * Then, if need be, rotate the hue value.
	 */
	if (info->hue != 0) {
		int sinth, costh, tmpmatrix[CMATRIX_LEN];

		memcpy(tmpmatrix, matrix, CMATRIX_LEN*sizeof(int));
		sinth = ov7670_sine(info->hue);
		costh = ov7670_cosine(info->hue);

        //Bri. rightside matrix changed to tmpmatrix
		matrix[0] = (tmpmatrix[3]*sinth + tmpmatrix[0]*costh)/1000;
		matrix[1] = (tmpmatrix[4]*sinth + tmpmatrix[1]*costh)/1000;
		matrix[2] = (tmpmatrix[5]*sinth + tmpmatrix[2]*costh)/1000;
		matrix[3] = (tmpmatrix[3]*costh - tmpmatrix[0]*sinth)/1000;
		matrix[4] = (tmpmatrix[4]*costh - tmpmatrix[1]*sinth)/1000;
		matrix[5] = (tmpmatrix[5]*costh - tmpmatrix[2]*sinth)/1000;
	}
}



static int ov7670_t_sat(struct i2c_client *client, int value)
{
	struct ov7670_info *info = i2c_get_clientdata(client);
	int matrix[CMATRIX_LEN];
	int ret;

    //Bri.
    if (info == NULL) return -EINVAL;
	info->sat = value;
	ov7670_calc_cmatrix(info, matrix);
	ret = ov7670_store_cmatrix(client, matrix);
	return ret;
}

static int ov7670_q_sat(struct i2c_client *client, __s32 *value)
{
	struct ov7670_info *info = i2c_get_clientdata(client);

    //Bri.
    if (info == NULL) return -EINVAL;
    if (value == NULL) return -EINVAL;
	*value = info->sat;
	return 0;
}

static int ov7670_t_hue(struct i2c_client *client, int value)
{
	struct ov7670_info *info = i2c_get_clientdata(client);
	int matrix[CMATRIX_LEN];
	int ret;

	if (value < -180 || value > 180)
		return -EINVAL;
	info->hue = value;
	ov7670_calc_cmatrix(info, matrix);
	ret = ov7670_store_cmatrix(client, matrix);
	return ret;
}


static int ov7670_q_hue(struct i2c_client *client, __s32 *value)
{
	struct ov7670_info *info = i2c_get_clientdata(client);

    //Bri.
    if (info == NULL) return -EINVAL;
    if (value == NULL) return -EINVAL;
	*value = info->hue;
	return 0;
}


/*
 * Some weird registers seem to store values in a sign/magnitude format!
 */
static unsigned char ov7670_sm_to_abs(unsigned char v)
{
	if ((v & 0x80) == 0)
		return v + 128;
	else
		return 128 - (v & 0x7f);
}


static unsigned char ov7670_abs_to_sm(unsigned char v)
{
	if (v > 127)
		return v & 0x7f;
	else
		return (128 - v) | 0x80;
}

static int ov7670_t_brightness(struct i2c_client *client, int value)
{
	unsigned char com8, v;
	int ret;

	ov7670_read(client, REG_COM8, &com8);
	com8 &= ~COM8_AEC;
	ov7670_write(client, REG_COM8, com8);
	v = ov7670_abs_to_sm(value);
	ret = ov7670_write(client, REG_BRIGHT, v);
	return ret;
}

static int ov7670_q_brightness(struct i2c_client *client, __s32 *value)
{
	unsigned char v;
	int ret = ov7670_read(client, REG_BRIGHT, &v);

	*value = ov7670_sm_to_abs(v);
	return ret;
}

static int ov7670_t_contrast(struct i2c_client *client, int value)
{
	return ov7670_write(client, REG_CONTRAS, (unsigned char) value);
}

static int ov7670_q_contrast(struct i2c_client *client, __s32 *value)
{
	unsigned char v;
	int ret = ov7670_read(client, REG_CONTRAS, &v);

	*value = v;
	return ret;
}

static int ov7670_t_redbalance(struct i2c_client *client, int value)
{
	return ov7670_write(client, REG_RED, (unsigned char) value);
}

static int ov7670_q_redbalance(struct i2c_client *client, __s32 *value)
{
	unsigned char v;
	int ret = ov7670_read(client, REG_RED, &v);

	*value = v;
	return ret;
}

static int ov7670_t_bluebalance(struct i2c_client *client, int value)
{
	return ov7670_write(client, REG_BLUE, (unsigned char) value);
}

static int ov7670_q_bluebalance(struct i2c_client *client, __s32 *value)
{
	unsigned char v;
	int ret = ov7670_read(client, REG_BLUE, &v);

	*value = v;
	return ret;
}

static int ov7670_t_gain(struct i2c_client *client, int value)
{
  /* Bits 0 to 3 are in 1/16 step
     Bits 4 to 7 are x2 per bit */
  unsigned char v;
  if(value > 496)
    v = 0xFF;
  else if(value < 15)
    v = 0x00;
  else
  {
    if(value > 255)
      v = 0xF0 + ((unsigned char)(value/16)-16);
    else if(value > 127)
      v = 0x70 + ((unsigned char)(value/8)-16);
    else if(value > 63)
      v = 0x30 + ((unsigned char)(value/4)-16);
    else if(value > 31)
      v = 0x10 + ((unsigned char)(value/2)-16);
    else
      v = (unsigned char)value-16;
  }

  return ov7670_write(client, REG_GAIN, (unsigned char) v);
}

static int ov7670_q_gain(struct i2c_client *client, __s32 *value)
{
	unsigned char v;
	int ret = ov7670_read(client, REG_GAIN, &v);

  /* Bits 0 to 3 are in 1/16 step
     Bits 4 to 7 are x2 per bit */
  unsigned int low = 16 + (v & 0x0F);
  unsigned int multiplicator =  (1 + ((v & 0x10)!=0) )
                              * (1 + ((v & 0x20)!=0) )
                              * (1 + ((v & 0x40)!=0) )
                              * (1 + ((v & 0x80)!=0) );
  *value = multiplicator * low;
	return ret;
}

static int ov7670_t_hcenter(struct i2c_client *client, int value)
{
	return ov7670_write(client, REG_LCC1, (unsigned char) value);
}

static int ov7670_q_hcenter(struct i2c_client *client, __s32 *value)
{
	unsigned char v;
	int ret = ov7670_read(client, REG_LCC1, &v);

	*value = v;
	return ret;
}

static int ov7670_t_vcenter(struct i2c_client *client, int value)
{
	return ov7670_write(client, REG_LCC2, (unsigned char) value);
}

static int ov7670_q_vcenter(struct i2c_client *client, __s32 *value)
{
	unsigned char v;
	int ret = ov7670_read(client, REG_LCC2, &v);

	*value = v;
	return ret;
}

static int ov7670_q_hflip(struct i2c_client *client, __s32 *value)
{
	int ret;
	unsigned char v;

	ret = ov7670_read(client, REG_MVFP, &v);
	*value = (v & MVFP_MIRROR) == MVFP_MIRROR;
	return ret;
}


static int ov7670_t_hflip(struct i2c_client *client, int value)
{
	unsigned char v;
	int ret;

	ret = ov7670_read(client, REG_MVFP, &v);
	if (value)
		v |= MVFP_MIRROR;
	else
		v &= ~MVFP_MIRROR;
	ret += ov7670_write(client, REG_MVFP, v);
	return ret;
}



static int ov7670_q_vflip(struct i2c_client *client, __s32 *value)
{
	int ret;
	unsigned char v;

	ret = ov7670_read(client, REG_MVFP, &v);
	*value = (v & MVFP_FLIP) == MVFP_FLIP;
	return ret;
}

static int ov7670_t_vflip(struct i2c_client *client, int value)
{
	unsigned char v;
	int ret;

	ret = ov7670_read(client, REG_MVFP, &v);
	if (value)
		v |= MVFP_FLIP;
	else
		v &= ~MVFP_FLIP;
	ret += ov7670_write(client, REG_MVFP, v);
	return ret;
}

static int ov7670_q_agc(struct i2c_client *client, __s32 *value)
{
	int ret;
	unsigned char v;

	ret = ov7670_read(client, REG_COM8, &v);
	*value = (v & COM8_AGC) == COM8_AGC;
	return ret;
}

static int ov7670_t_agc(struct i2c_client *client, int value)
{
	unsigned char v;
	int ret;

	ret = ov7670_read(client, REG_COM8, &v);
	if (value)
		v |= COM8_AGC;
	else
		v &= ~COM8_AGC;
	ret += ov7670_write(client, REG_COM8, v);
	return ret;
}

static int ov7670_q_awb_g_channel_gain(struct i2c_client *client, __s32 *value)
{
	unsigned char v;
	int ret = ov7670_read(client, 0x6A, &v);

	*value = v;
	return ret;
}

static int ov7670_t_awb_g_channel_gain(struct i2c_client *client, int value)
{
	return ov7670_write(client, 0x6A, (unsigned char) value);
}

static int ov7670_q_ygb_average_level(struct i2c_client *client, __s32 *value)
{
	unsigned char v;
	int ret = ov7670_read(client, REG_GbAVE, &v);

	*value = v;
	return ret;
}

static int ov7670_t_ygb_average_level(struct i2c_client *client, int value)
{
	return ov7670_write(client, REG_GbAVE, (unsigned char) value);
}

static int ov7670_q_awb(struct i2c_client *client, __s32 *value)
{
	int ret;
	unsigned char v;

	ret = ov7670_read(client, REG_COM8, &v);
	*value = (v & COM8_AWB) == COM8_AWB;
	return ret;
}

static int ov7670_t_awb(struct i2c_client *client, int value)
{
	unsigned char v;
	int ret;

	ret = ov7670_read(client, REG_COM8, &v);
	if (value)
		v |= COM8_AWB;
	else
		v &= ~COM8_AWB;

	ret += ov7670_write(client, REG_COM8, v);
	return ret;
}

static int ov7670_q_aec(struct i2c_client *client, __s32 *value)
{
	int ret;
	unsigned char v;

	ret = ov7670_read(client, REG_COM8, &v);
	*value = (v & COM8_AEC) == COM8_AEC;
	return ret;
}

static int ov7670_t_aec(struct i2c_client *client, int value)
{
	unsigned char v;
	int ret;

	ret = ov7670_read(client, REG_COM8, &v);
	if (value)
		v |= COM8_AEC;
	else
		v &= ~COM8_AEC;

	ret += ov7670_write(client, REG_COM8, v);
	return ret;
}

static int ov7670_q_ablc(struct i2c_client *client, __s32 *value)
{
	int ret;
	unsigned char v;

	ret = ov7670_read(client, REG_ABLC, &v);
	*value = (v & ABLC_ENABLE) == ABLC_ENABLE;
	return ret;
}

static int ov7670_t_ablc(struct i2c_client *client, int value)
{
	unsigned char v;
	int ret;

	ret = ov7670_read(client, REG_ABLC, &v);
	if (value)
		v |= ABLC_ENABLE;
	else
		v &= ~ABLC_ENABLE;

	ret += ov7670_write(client, REG_ABLC, v);
	return ret;
}

static int ov7670_q_ablc_target(struct i2c_client *client, __s32 *value)
{
	unsigned char v;
	int ret = ov7670_read(client, REG_ABLC_TARGET, &v);

	*value = v;
	return ret;
}

static int ov7670_t_ablc_target(struct i2c_client *client, int value)
{
  return ov7670_write(client, REG_ABLC_TARGET, (unsigned char) value);
}

static int ov7670_q_ablc_range(struct i2c_client *client, __s32 *value)
{
	unsigned char v;
	int ret = ov7670_read(client, REG_ABLC_RANGE, &v);

	*value = v;
	return ret;
}

static int ov7670_t_ablc_range(struct i2c_client *client, int value)
{
  return ov7670_write(client, REG_ABLC_RANGE, (unsigned char) value);
}

static int ov7670_q_blcb(struct i2c_client *client, __s32 *value)
{
	unsigned char v;
	int ret = ov7670_read(client, REG_BLC_B, &v);

	*value = v;
	return ret;
}

static int ov7670_t_blcb(struct i2c_client *client, int value)
{
  return ov7670_write(client, REG_BLC_B, (unsigned char) value);
}

static int ov7670_q_blcr(struct i2c_client *client, __s32 *value)
{
	unsigned char v;
	int ret = ov7670_read(client, REG_BLC_R, &v);

	*value = v;
	return ret;
}

static int ov7670_t_blcr(struct i2c_client *client, int value)
{
  return ov7670_write(client, REG_BLC_R, (unsigned char) value);
}

static int ov7670_q_blcgb(struct i2c_client *client, __s32 *value)
{
	unsigned char v;
	int ret = ov7670_read(client, REG_BLC_GB, &v);

	*value = v;
	return ret;
}

static int ov7670_t_blcgb(struct i2c_client *client, int value)
{
  return ov7670_write(client, REG_BLC_GB, (unsigned char) value);
}

static int ov7670_q_blcgr(struct i2c_client *client, __s32 *value)
{
	unsigned char v;
	int ret = ov7670_read(client, REG_BLC_GR, &v);

	*value = v;
	return ret;
}

static int ov7670_t_blcgr(struct i2c_client *client, int value)
{
  return ov7670_write(client, REG_BLC_GR, (unsigned char) value);
}


static int ov7670_q_sat_auto(struct i2c_client *client, __s32 *value)
{
	int ret;
	unsigned char v;

	ret = ov7670_read(client, REG_COM13, &v);
	*value = (v & COM13_UVSAT) == COM13_UVSAT;
	return ret;
}

static int ov7670_t_sat_auto(struct i2c_client *client, int value)
{
	unsigned char v;
	int ret;

	ret = ov7670_read(client, REG_COM13, &v);
	if (value)
		v |= COM13_UVSAT;
	else
		v &= ~COM13_UVSAT;

	ret += ov7670_write(client, REG_COM13, v);
	return ret;
}

static int ov7670_t_uvsat_result(struct i2c_client *client, int value)
{
	unsigned char v;
	int ret;

	ret = ov7670_read(client, 0xC9, &v);
	v &= 0xF0;
	v |= (value & 0x0000000F);
	ret += ov7670_write(client, 0xC9, v);

	return ret;
}

static int ov7670_q_uvsat_result(struct i2c_client *client, __s32 *value)
{
	unsigned char v;
	int ret;

	ret = ov7670_read(client, 0xC9, &v);
	*value = v & 0x0F;
	return ret;
}

static int ov7670_q_expo(struct i2c_client *client, __s32 *expo)
{
	int ret = 0;
	unsigned char com1, aech, aechh;

	ret = ov7670_read(client, REG_COM1, &com1);
	ret += ov7670_read(client, REG_AECH, &aech);
	ret += ov7670_read(client, REG_AECHH, &aechh);

    if (ret) {
        return ret;
    }

	*expo = ((int)(aechh & 0x3f) << 10) + ((int)aech << 2) + (com1 & 0x03);

	return ret;
}

/* exposition is splited in 3 registers:
 * REG_COM1[1:0] = expo[1:0]
 * REG_AECH[7:0] = expo[9:2]
 * REG_AECHH[5:0] = expo[15:10]
 * */

static int ov7670_t_expo(struct i2c_client *client, int expo)
{
	unsigned char com1, aech, aechh;
	int ret = 0;

	ret = ov7670_read(client, REG_COM1, &com1);
	ret += ov7670_read(client, REG_AECH, &aech);
	ret += ov7670_read(client, REG_AECHH, &aechh);

    if (ret) {
        return ret;
    }


    com1 = (com1 & 0xfc) + (expo & 0x0003);
    aech = (expo & 0x03fc) >> 2;
    aechh= (aechh & 0xc0) + ((expo & 0xfc00) >> 10);

	ret = ov7670_write(client, REG_AECHH, aechh);
	ret += ov7670_write(client, REG_AECH, aech);
	ret += ov7670_write(client, REG_COM1, com1);

	return ret;
}


static int ov7670_q_aec_algorithm(struct i2c_client *client, __s32 *value)
{
	int ret = 0;
	unsigned char haecc7;

	ret = ov7670_read(client, REG_HAECC7, &haecc7);

    if (ret) {
        return ret;
    }

	*value = ((int)(haecc7 & 0x80) >> 7);

	return ret;
}

static int ov7670_t_aec_algorithm(struct i2c_client *client, int value)
{
	unsigned char haecc7;
	int ret = 0;

	ret = ov7670_read(client, REG_HAECC7, &haecc7);

    if (ret) {
        return ret;
    }
    haecc7 = (haecc7 & 0x7f) + ((value!=0) << 7);

	ret = ov7670_write(client, REG_HAECC7, haecc7);

	return ret;
}




static int ov7670_q_expo_correction(struct i2c_client *client, __s32 *expo_correction)
{
	int ret = 0;
	unsigned char aew;

	/* testing one value is enough */
	ret = ov7670_read(client, REG_AEW, &aew);

    if (ret) {
        return ret;
    }

	if(aew>234)
		*expo_correction = 6;
	else if(aew>186)
		*expo_correction = 5;
	else if(aew>147)
		*expo_correction = 4;
	else if(aew>117)
		*expo_correction = 3;
	else if(aew>93)
		*expo_correction = 2;
	else if(aew>74)
		*expo_correction = 1;
	else if(aew>59)
		*expo_correction = 0;
	else if(aew>46)
		*expo_correction = -1;
	else if(aew>37)
		*expo_correction = -2;
	else if(aew>29)
		*expo_correction = -3;
	else if(aew>24)
		*expo_correction = -4;
	else if(aew>19)
		*expo_correction = -5;
	else 
		*expo_correction = -6;

	return ret;
}

static int ov7670_t_expo_correction(struct i2c_client *client, int expo_correction)
{
	unsigned char aew, aeb;
	//int tmpVal;
	int ret = 0;

        /* set the aec algorithm to average-based mode
            (in case it was in histogram-based mode)*/
        unsigned char haecc7;
        ret = ov7670_read(client, REG_HAECC7, &haecc7);
        if (ret) {
          return ret;
        }
        haecc7 = (haecc7 & 0x7f);
        msleep(1);
        ret = ov7670_write(client, REG_HAECC7, haecc7);
        if (ret) {
          return ret;
        }
        msleep(1);

	/* pow() not available in kernel space

	double power = (double)expo_correction / 3.,
	tmpVal = (int)(117. * pow(2., power);
	aew = ( tmpVal>255 ) ? 255 : tmpVal;

	tmpVal = (int)(99. * pow(2., power);
	aeb = ( tmpVal>255 ) ? 255 : tmpVal;*/

	switch(expo_correction)
	{
	case 6: 
		aew = 255;
		aeb = 237;
		break;
	case 5: 
		aew = 234;
		aeb = 198;
		break;
	case 4: 
		aew = 186;
		aeb = 157;
		break;
	case 3: 
		aew = 147;
		aeb = 125;
		break;
	case 2: 
		aew = 117;
		aeb =  99;
		break;
	case 1: 
		aew = 93;
		aeb = 75;//79;
		break;
	case 0: 
		aew = 74;
		aeb = 56;//62;

        	/* set the aec algorithm back to histogram-based mode */
        	haecc7 = (haecc7 | 0x80);
        	//msleep(1);
        	ret = ov7670_write(client, REG_HAECC7, haecc7);
        	if (ret) {
        	  return ret;
        	}
        	msleep(1);

		break;
	case -1: 
		aew = 59;
		aeb = 41;//49;
		break;
	case -2: 
		aew = 46;
		aeb = 28;//39;
		break;
	case -3: 
		aew = 37;
		aeb = 19;//31;
		break;
	case -4: 
		aew = 29;
		aeb = 11;//25;
		break;
	case -5: 
		aew = 24;
		aeb = 6;//19;
		break;
	case -6: 
		aew = 19;
		aeb = 1;//16;
		break;
	default: 
		printk(KERN_ERR "Writing uninitialized values. This is a bug.\n");
		BUG();
		break;
	}

	ret = ov7670_write(client, REG_AEW, aew);
        msleep(1);
	ret += ov7670_write(client, REG_AEB, aeb);

	return ret;
}


/*static int ov7670_q_sharpness(struct i2c_client *client, __s32 *value)
{
	int ret = 0;
	unsigned char autoSharpness, sharpnessMagnitude;

	ret  = ov7670_read(client, 0xB4, &autoSharpness);
        ret += ov7670_read(client, 0xB6, &sharpnessMagnitude);

    if (ret) {
        return ret;
    }

	*value = (int)((autoSharpness & 0x20) + (sharpnessMagnitude & 0x1F));

	return ret;
}

static int ov7670_t_sharpness(struct i2c_client *client, int value)
{
	unsigned char autoSharpness, sharpnessMagnitude;
	int ret = 0;

	ret  = ov7670_read(client, 0xB4, &autoSharpness);
        ret += ov7670_read(client, 0xB6, &sharpnessMagnitude);

    if (ret) {
        return ret;
    }

        autoSharpness = (autoSharpness & 0xdf) + (value & 0x20);
        sharpnessMagnitude = (sharpnessMagnitude & 0xe0) + (value & 0x1f);
    
	ret  = ov7670_write(client, 0xB4, autoSharpness);
	ret += ov7670_write(client, 0xB6, sharpnessMagnitude);

	return ret;
}*/

static int ov7670_q_sharpness(struct i2c_client *client, __s32 *value)
{
	int ret = 0;
	unsigned char sharpness;

	ret  = ov7670_read(client, 0x75, &sharpness);

    if (ret) {
        return ret;
    }

	*value = (int)(sharpness & 0x1F);

	return ret;
}

static int ov7670_t_sharpness(struct i2c_client *client, int value)
{
	unsigned char sharpness;
	int ret = 0;

	ret  = ov7670_read(client, 0x75, &sharpness);

    if (ret) {
        return ret;
    }

        sharpness = (sharpness & 0xE0) + (value & 0x1F);

    	ret  = ov7670_write(client, 0x75, sharpness);

	return ret;
}

static int ov7670_t_edge_enhancement_factor(struct i2c_client *client, int value)
{
	int ret = 0;
	unsigned char v;

	ret = ov7670_read(client, REG_EDGE, &v);
	v &= 0xE0;
	v |= (value & 0x0000001F);
	ret += ov7670_write(client, REG_EDGE, v);

	return ret;
}

static int ov7670_q_edge_enhancement_factor(struct i2c_client *client, __s32 *value)
{
	int ret = 0;
	unsigned char v;

	ret = ov7670_read(client, REG_EDGE, &v);
	*value = v & 0x1F;

	return ret;
}

static int ov7670_t_denoise_strength(struct i2c_client *client, int value)
{
	int ret = 0;
	unsigned char v;
	v = (unsigned char) value;
	ret = ov7670_write(client, 0x4C, v);
	return ret;
}

static int ov7670_q_denoise_strength(struct i2c_client *client, __s32 *value)
{
	int ret = 0;
	unsigned char v;
	ret = ov7670_read(client, 0x4C, &v);
	*value = v;
	return ret;
}

static int ov7670_q_auto_contrast_center(struct i2c_client *client, __s32 *value)
{
	int ret = 0;
	unsigned char v;
	ret = ov7670_read(client, 0x58, &v);
	*value = (v & 0x80) != 0;
	return ret;
}

static int ov7670_t_auto_contrast_center(struct i2c_client *client, int value)
{
	int ret = 0;
	unsigned char v;
	ret = ov7670_read(client, 0x58, &v);
	if (value)
		v |= 0x80;
	else
		v &= 0x7F;
	ret += ov7670_write(client, 0x58, v);

	return ret;
}

static int ov7670_t_contrast_center(struct i2c_client *client, int value)
{
	int ret = 0;
	unsigned char v = (unsigned char) value;
	ret = ov7670_write(client, 0x57, v);
	return ret;
}

static int ov7670_q_contrast_center(struct i2c_client *client, __s32 *value)
{
	int ret = 0;
	unsigned char v;
	ret = ov7670_read(client, 0x57, &v);
	*value = v;
	return ret;
}


 /*
  * Initialise camera with default parameters
  */
static int ov7670_t_setDefaultParam(struct i2c_client *client, int value)
{
	int ret = ov7670_init(client);
    return ret;
}


static struct ov7670_control {
	struct v4l2_queryctrl qc;
	int (*query)(struct i2c_client *c, __s32 *value);
	int (*tweak)(struct i2c_client *c, int value);
} ov7670_controls[] =
{
	{
		.qc = {
			.id = V4L2_CID_BRIGHTNESS,
			.type = V4L2_CTRL_TYPE_INTEGER,
			.name = "Brightness",
			.minimum = 0,
			.maximum = 255,
			.step = 1,
			.default_value = 0x80,
			.flags = V4L2_CTRL_FLAG_SLIDER
		},
		.tweak = ov7670_t_brightness,
		.query = ov7670_q_brightness,
	},
	{
		.qc = {
			.id = V4L2_CID_CONTRAST,
			.type = V4L2_CTRL_TYPE_INTEGER,
			.name = "Contrast",
			.minimum = 0,
			.maximum = 127,
			.step = 1,
			.default_value = 0x40,   /* XXX ov7670 spec */
			.flags = V4L2_CTRL_FLAG_SLIDER
		},
		.tweak = ov7670_t_contrast,
		.query = ov7670_q_contrast,
	},
	{
		.qc = {
			.id = V4L2_CID_SATURATION,
			.type = V4L2_CTRL_TYPE_INTEGER,
			.name = "Saturation",
			.minimum = 0,
			.maximum = 256,
			.step = 1,
			.default_value = 0x80,
			.flags = V4L2_CTRL_FLAG_SLIDER
		},
		.tweak = ov7670_t_sat,
		.query = ov7670_q_sat,
	},
	{
		.qc = {
			.id = V4L2_CID_HUE,
			.type = V4L2_CTRL_TYPE_INTEGER,
			.name = "HUE",
			.minimum = -180,
			.maximum = 180,
			.step = 5,
			.default_value = 0,
			.flags = V4L2_CTRL_FLAG_SLIDER
		},
		.tweak = ov7670_t_hue,
		.query = ov7670_q_hue,
	},
	{
		.qc = {
			.id = V4L2_CID_VFLIP,
			.type = V4L2_CTRL_TYPE_BOOLEAN,
			.name = "Vertical flip",
			.minimum = 0,
			.maximum = 1,
			.step = 1,
			.default_value = 0,
		},
		.tweak = ov7670_t_vflip,
		.query = ov7670_q_vflip,
	},
	{
		.qc = {
			.id = V4L2_CID_HFLIP,
			.type = V4L2_CTRL_TYPE_BOOLEAN,
			.name = "Horizontal mirror",
			.minimum = 0,
			.maximum = 1,
			.step = 1,
			.default_value = 0,
		},
		.tweak = ov7670_t_hflip,
		.query = ov7670_q_hflip,
	},
	{
		.qc = {
			.id = V4L2_CID_RED_BALANCE,
			.type = V4L2_CTRL_TYPE_INTEGER,
			.name = "Red Balance",
			.minimum = 0,
			.maximum = 0xFF,
			.step = 1,
			.default_value = 0x80,
			.flags = V4L2_CTRL_FLAG_SLIDER
		},
		.tweak = ov7670_t_redbalance,
		.query = ov7670_q_redbalance,
	},
	{
		.qc = {
			.id = V4L2_CID_BLUE_BALANCE,
			.type = V4L2_CTRL_TYPE_INTEGER,
			.name = "Blue Balance",
			.minimum = 0,
			.maximum = 0xFF,
			.step = 1,
			.default_value = 0x80,
			.flags = V4L2_CTRL_FLAG_SLIDER
		},
		.tweak = ov7670_t_bluebalance,
		.query = ov7670_q_bluebalance,
	},
	{
		.qc = {
			.id = V4L2_CID_GAIN,
			.type = V4L2_CTRL_TYPE_INTEGER,
			.name = "Gain",
			.minimum = 0,
			.maximum = 0xFF,
			.step = 1,
			.default_value = 0x00,
			.flags = V4L2_CTRL_FLAG_SLIDER
		},
		.tweak = ov7670_t_gain,
		.query = ov7670_q_gain,
	},
	{
		.qc = {
			.id = V4L2_CID_AWB_G_CHANNEL_GAIN,
			.type = V4L2_CTRL_TYPE_INTEGER,
			.name = "AWB G Channel Gain",
			.minimum = 0,
			.maximum = 0xFF,
			.step = 1,
			.default_value = 0x40,
			.flags = V4L2_CTRL_FLAG_SLIDER
		},
		.tweak = ov7670_t_awb_g_channel_gain,
		.query = ov7670_q_awb_g_channel_gain
	},
	{
		.qc = {
			.id = V4L2_CID_YGB_AVERAGE_LEVEL,
			.type = V4L2_CTRL_TYPE_INTEGER,
			.name = "AWB Y/Gb average level",
			.minimum = 0,
			.maximum = 0xFF,
			.step = 1,
			.default_value = 0x80,
			.flags = V4L2_CTRL_FLAG_SLIDER
		},
		.tweak = ov7670_t_ygb_average_level,
		.query = ov7670_q_ygb_average_level
	},
	{
		.qc = {
			.id = V4L2_CID_HCENTER,
			.type = V4L2_CTRL_TYPE_INTEGER,
			.name = "Lens Correction X",
			.minimum = 0,
			.maximum = 0xFF,
			.step = 1,
			.default_value = 0x00,
			.flags = V4L2_CTRL_FLAG_SLIDER
		},
		.tweak = ov7670_t_hcenter,
		.query = ov7670_q_hcenter,
	},
	{
		.qc = {
			.id = V4L2_CID_VCENTER,
			.type = V4L2_CTRL_TYPE_INTEGER,
			.name = "Lens Correction Y",
			.minimum = 0,
			.maximum = 0xFF,
			.step = 1,
			.default_value = 0x00,
			.flags = V4L2_CTRL_FLAG_SLIDER
		},
		.tweak = ov7670_t_vcenter,
		.query = ov7670_q_vcenter,
	},
	{
		.qc = {
			.id = V4L2_CID_AUTOGAIN,
			.type = V4L2_CTRL_TYPE_BOOLEAN,
			.name = "AGC Enable",
			.minimum = 0,
			.maximum = 1,
			.step = 1,
			.default_value = 0x00,
		},
		.tweak = ov7670_t_agc,
		.query = ov7670_q_agc,
	},
	{
		.qc = {
			.id = V4L2_CID_AUTO_WHITE_BALANCE,
			.type = V4L2_CTRL_TYPE_BOOLEAN,
			.name = "AWB Enable",
			.minimum = 0,
			.maximum = 1,
			.step = 1,
			.default_value = 0x00,
		},
		.tweak = ov7670_t_awb,
		.query = ov7670_q_awb,
	},
	{
		.qc = {
			.id = V4L2_CID_AUTOEXPOSURE,
			.type = V4L2_CTRL_TYPE_BOOLEAN,
			.name = "AEC Enable",
			.minimum = 0,
			.maximum = 1,
			.step = 1,
			.default_value = 0x00,
		},
		.tweak = ov7670_t_aec,
		.query = ov7670_q_aec,
	},
	{
		.qc = {
			.id = V4L2_CID_AUTO_BLACK_LEVEL,
			.type = V4L2_CTRL_TYPE_BOOLEAN,
			.name = "ABLC Enable",
			.minimum = 0,
			.maximum = 1,
			.step = 1,
			.default_value = 0x00,
		},
		.tweak = ov7670_t_ablc,
		.query = ov7670_q_ablc,
	},
	{
		.qc = {
			.id = V4L2_CID_ABLC_TARGET,
			.type = V4L2_CTRL_TYPE_BOOLEAN,
			.name = "ABLC Target",
			.minimum = 0,
			.maximum = 255,
			.step = 1,
			.default_value = 0x80,
		},
		.tweak = ov7670_t_ablc_target,
		.query = ov7670_q_ablc_target,
	},
	{
		.qc = {
			.id = V4L2_CID_ABLC_RANGE,
			.type = V4L2_CTRL_TYPE_BOOLEAN,
			.name = "ABLC Stable Range",
			.minimum = 0,
			.maximum = 255,
			.step = 1,
			.default_value = 0x04,
		},
		.tweak = ov7670_t_ablc_range,
		.query = ov7670_q_ablc_range,
	},
	{
		.qc = {
			.id = V4L2_CID_BLACK_LEVEL_BLUE,
			.type = V4L2_CTRL_TYPE_BOOLEAN,
			.name = "BLC Blue",
			.minimum = 0,
			.maximum = 255,
			.step = 1,
			.default_value = 0x00,
		},
		.tweak = ov7670_t_blcb,
		.query = ov7670_q_blcb,
	},
	{
		.qc = {
			.id = V4L2_CID_BLACK_LEVEL_RED,
			.type = V4L2_CTRL_TYPE_BOOLEAN,
			.name = "BLC Red",
			.minimum = 0,
			.maximum = 255,
			.step = 1,
			.default_value = 0x00,
		},
		.tweak = ov7670_t_blcr,
		.query = ov7670_q_blcr,
	},
	{
		.qc = {
			.id = V4L2_CID_BLACK_LEVEL_GB,
			.type = V4L2_CTRL_TYPE_BOOLEAN,
			.name = "BLC Gb",
			.minimum = 0,
			.maximum = 255,
			.step = 1,
			.default_value = 0x00,
		},
		.tweak = ov7670_t_blcgb,
		.query = ov7670_q_blcgb,
	},
	{
		.qc = {
			.id = V4L2_CID_BLACK_LEVEL_GR,
			.type = V4L2_CTRL_TYPE_BOOLEAN,
			.name = "BLC Gr",
			.minimum = 0,
			.maximum = 255,
			.step = 1,
			.default_value = 0x00,
		},
		.tweak = ov7670_t_blcgr,
		.query = ov7670_q_blcgr,
	},
	{
		.qc = {
			.id = V4L2_CID_SAT_AUTO,
			.type = V4L2_CTRL_TYPE_BOOLEAN,
			.name = "UV saturation auto adjustment",
			.minimum = 0,
			.maximum = 1,
			.step = 1,
			.default_value = 0x00,
		},
		.tweak = ov7670_t_sat_auto,
		.query = ov7670_q_sat_auto,
	},
	{
		.qc = {
			.id = V4L2_CID_UVSAT_RESULT,
			.type = V4L2_CTRL_TYPE_INTEGER,
			.name = "UV saturation control result",
			.minimum = 0,
			.maximum = 15,
			.step = 1,
			.default_value = 0x00,
		},
		.tweak = ov7670_t_uvsat_result,
		.query = ov7670_q_uvsat_result,
	},
	{
		.qc = {
			.id = V4L2_CID_EXPOSURE,
			.type = V4L2_CTRL_TYPE_INTEGER,
			.name = "Exposure",
			.minimum = 0,
			.maximum = 4096, //65535,
			.step = 1,
			.default_value = 60,
			.flags = V4L2_CTRL_FLAG_SLIDER
		},
		.tweak = ov7670_t_expo,
		.query = ov7670_q_expo,
	},
	{
		.qc = {
			.id = V4L2_CID_CAM_INIT,
			.type = V4L2_CTRL_TYPE_BOOLEAN,
			.name = "Set default parameters",
			.minimum = 0,       //these parameters
			.maximum = 3,       //don't mean
			.step = 1,          // anything
			.default_value = 0, // anymore
		},
		.tweak = ov7670_t_setDefaultParam,
	},
	{
		.qc = {
			.id = V4L2_CID_AEC_ALGORITHM,
			.type = V4L2_CTRL_TYPE_BOOLEAN,
			.name = "AEC Algorithm",
			.minimum = 0,
			.maximum = 1,
			.step = 1,
			.default_value = 1,
		},
		.tweak = ov7670_t_aec_algorithm,
		.query = ov7670_q_aec_algorithm,
	},
	{
		.qc = {
			.id = V4L2_CID_EXPOSURE_CORRECTION,
			.type = V4L2_CTRL_TYPE_INTEGER,
			.name = "Exposure Correction",
			.minimum = -6,
			.maximum = 6,
			.step = 1,
			.default_value = 0,
			.flags = V4L2_CTRL_FLAG_SLIDER
		},
		.tweak = ov7670_t_expo_correction,
		.query = ov7670_q_expo_correction,
	},
	/*{
		.qc = {
			.id = V4L2_CID_SHARPNESS,
			.type = V4L2_CTRL_TYPE_INTEGER,
			.name = "Sharpness:[5]auto,[0-4]magnitude",
			.minimum = 0,
			.maximum = 63,
			.step = 1,
			.default_value = 0,
			.flags = V4L2_CTRL_FLAG_SLIDER
		},
		.tweak = ov7670_t_sharpness,
		.query = ov7670_q_sharpness,
	},*/
        {
		.qc = {
			.id = V4L2_CID_SHARPNESS,
			.type = V4L2_CTRL_TYPE_INTEGER,
			.name = "Sharpness",
			.minimum = 0,
			.maximum = 31,
			.step = 1,
			.default_value = 2,
			.flags = V4L2_CTRL_FLAG_SLIDER
		},
		.tweak = ov7670_t_sharpness,
		.query = ov7670_q_sharpness,
	},
	{
		.qc = {
			.id = V4L2_CID_EDGE_ENH_FACTOR,
			.type = V4L2_CTRL_TYPE_INTEGER,
			.name = "Edge enhancement factor",
			.minimum = 0,
			.maximum = 31,
			.step = 1,
			.default_value = 0,
			.flags = V4L2_CTRL_FLAG_SLIDER
		},
		.tweak = ov7670_t_edge_enhancement_factor,
		.query = ov7670_q_edge_enhancement_factor,
	},
	{
		.qc = {
			.id = V4L2_CID_DENOISE_STRENGTH,
			.type = V4L2_CTRL_TYPE_INTEGER,
			.name = "De-noise strength",
			.minimum = 0,
			.maximum = 0xFF,
			.step = 1,
			.default_value = 0,
			.flags = V4L2_CTRL_FLAG_SLIDER
		},
		.tweak = ov7670_t_denoise_strength,
		.query = ov7670_q_denoise_strength,
	},
	{
		.qc = {
			.id = V4L2_CID_AUTO_CONTRAST_CENTER,
			.type = V4L2_CTRL_TYPE_BOOLEAN,
			.name = "Auto contrast center",
			.minimum = 0,
			.maximum = 1,
			.step = 1,
			.default_value = 0
		},
		.tweak = ov7670_t_auto_contrast_center,
		.query = ov7670_q_auto_contrast_center,
	},
	{
		.qc = {
			.id = V4L2_CID_CONTRAST_CENTER,
			.type = V4L2_CTRL_TYPE_INTEGER,
			.name = "Set contrast center",
			.minimum = 0,
			.maximum = 0xFF,
			.step = 1,
			.default_value = 0x80,
			.flags = V4L2_CTRL_FLAG_SLIDER
		},
		.tweak = ov7670_t_contrast_center,
		.query = ov7670_q_contrast_center,
	},
};
#define N_CONTROLS (sizeof(ov7670_controls)/sizeof(ov7670_controls[0]))


static struct ov7670_control *ov7670_find_control(__u32 id)
{
	int i;

	for (i = 0; i < N_CONTROLS; i++)
		if (ov7670_controls[i].qc.id == id)
			return ov7670_controls + i;
	return NULL;
}


static int ov7670_queryctrl(struct i2c_client *client,
		struct v4l2_queryctrl *qc)
{
	struct ov7670_control *ctrl = ov7670_find_control(qc->id);

	if (ctrl == NULL)
		return -EINVAL;
	*qc = ctrl->qc;
	return 0;
}

static int ov7670_g_ctrl(struct i2c_client *client, struct v4l2_control *ctrl)
{
	struct ov7670_control *octrl = ov7670_find_control(ctrl->id);
	int ret;

	if (octrl == NULL)
		return -EINVAL;
	ret = octrl->query(client, &ctrl->value);
	if (ret >= 0)
		return 0;
	return ret;
}

static int ov7670_s_ctrl(struct i2c_client *client, struct v4l2_control *ctrl)
{
	struct ov7670_control *octrl = ov7670_find_control(ctrl->id);
	int ret;

	if (octrl == NULL)
		return -EINVAL;
	ret =  octrl->tweak(client, ctrl->value);
	if (ret >= 0)
		return 0;
	return ret;
}


/*
 * Basic i2c stuff.
 */
static struct i2c_driver ov7670_driver;

static int ov7670_attach(struct i2c_adapter *adapter)
{
	int ret;
	struct i2c_client *client;
	struct ov7670_info *info;
	printk(KERN_INFO "ov7670/1: driver attached: adapter id: %x\n", (int)adapter->id);
	/*
	 * For now: only deal with adapters we recognize.
	 */
	if (adapter->id != I2C_HW_SMBUS_CAFE &&
		adapter->id != I2C_HW_SMBUS_SCX200 &&
		adapter->id != I2C_HW_B_SER)
		return -ENODEV;

	client = kzalloc(sizeof (struct i2c_client), GFP_KERNEL);
	if (! client)
		return -ENOMEM;
	client->adapter = adapter;
	client->addr = OV7670_I2C_ADDR;
	client->driver = &ov7670_driver,
	strcpy(client->name, "OV7670");
	/*
	 * Set up our info structure.
	 */
	info = kzalloc(sizeof (struct ov7670_info), GFP_KERNEL);
	if (! info) {
		ret = -ENOMEM;
		goto out_free;
	}
	info->fmt = &ov7670_formats[0];
	info->sat = 128;	/* Review this */
	i2c_set_clientdata(client, info);

	/*
	 * Make sure it's an ov7670
	 */
	ret = ov7670_detect(client);
	if (ret)
		goto out_free_info;
	ret = i2c_attach_client(client);
	if (ret)
		goto out_free_info;

	ov7670_i2c_client = client;
	return 0;

  out_free_info:
	kfree(info);
  out_free:
	kfree(client);
	return ret;
}


static int ov7670_detach(struct i2c_client *client)
{
#ifdef REGDUMP
	ov7670_read_dump(client);
#endif
	i2c_detach_client(client);
	kfree(i2c_get_clientdata(client));
	kfree(client);
	return 0;
}


int ov7670_command(struct i2c_client *client, unsigned int cmd,
		void *arg)
{
	struct v4l2_capability *cap;
#if 0
	printk(KERN_INFO "Executing command %x\n", (int)cmd);
#endif
	switch (cmd) {

	case VIDIOC_QUERYCAP:
		cap = arg;
		strcpy(cap->driver, "OV7670");
		strcpy(cap->card, "OV7670");
		cap->version = 2;
		cap->capabilities =	V4L2_CAP_VIDEO_CAPTURE |
				V4L2_CAP_STREAMING     |
				V4L2_CAP_READWRITE;
		return 0;

	case VIDIOC_DBG_G_CHIP_IDENT:
		return v4l2_chip_ident_i2c_client(client, arg, V4L2_IDENT_OV7670, 0);

	case VIDIOC_INT_RESET:
		ov7670_reset(client);
		return 0;

	case VIDIOC_INT_INIT:
		return ov7670_init(client);

	case VIDIOC_ENUM_FMT:
		return ov7670_enum_fmt(client, (struct v4l2_fmtdesc *) arg);
	case VIDIOC_TRY_FMT:
		return ov7670_try_fmt(client, (struct v4l2_format *) arg, NULL, NULL);
	case VIDIOC_S_FMT:
	{	ov7670_params.format.type                = ((struct v4l2_format *)arg)->type;
		ov7670_params.format.fmt.pix.field       = ((struct v4l2_format *)arg)->fmt.pix.field;
		ov7670_params.format.fmt.pix.width       = ((struct v4l2_format *)arg)->fmt.pix.width;
		ov7670_params.format.fmt.pix.height      = ((struct v4l2_format *)arg)->fmt.pix.height;
		ov7670_params.format.fmt.pix.pixelformat = ((struct v4l2_format *)arg)->fmt.pix.pixelformat;
		return ov7670_s_fmt(client, (struct v4l2_format *) arg);
	}
	case VIDIOC_QUERYCTRL:
		return ov7670_queryctrl(client, (struct v4l2_queryctrl *) arg);
	case VIDIOC_S_CTRL:
		return ov7670_s_ctrl(client, (struct v4l2_control *) arg);
	case VIDIOC_G_CTRL:
		return ov7670_g_ctrl(client, (struct v4l2_control *) arg);
	case VIDIOC_S_PARM:
	{	ov7670_params.framerate.type                                  = ((struct v4l2_streamparm *)arg)->type;
		ov7670_params.framerate.parm.capture.timeperframe.numerator   = ((struct v4l2_streamparm *)arg)->parm.capture.timeperframe.numerator;
		ov7670_params.framerate.parm.capture.timeperframe.denominator = ((struct v4l2_streamparm *)arg)->parm.capture.timeperframe.denominator;
		ov7670_params.framerate.parm.capture.capability               = ((struct v4l2_streamparm *)arg)->parm.capture.capability;
		return ov7670_s_parm(client, (struct v4l2_streamparm *) arg);
	}
	case VIDIOC_G_PARM:
		return ov7670_g_parm(client, (struct v4l2_streamparm *) arg);
	default:
		printk(KERN_DEBUG "ov7670: ioctl called with unknown command.\n");
		return -EINVAL;
	}
	return -EINVAL;
}


static struct i2c_driver ov7670_driver = {
	.driver = {
		.name = "ov7670",
	},
	.id 		= I2C_DRIVERID_OV7670,
//	.class 		= I2C_CLASS_CAM_DIGITAL,
	.attach_adapter = ov7670_attach,
	.detach_client	= ov7670_detach,
	.command	= ov7670_command,
};

/*
 * Module initialization
 */
int ov7670_mod_init(void)
{
	printk(KERN_NOTICE "OmniVision ov7670 sensor driver, at your service (v %s)\n", VERSION);
	return i2c_add_driver(&ov7670_driver);
}

void ov7670_mod_exit(void)
{
	i2c_del_driver(&ov7670_driver);
}
