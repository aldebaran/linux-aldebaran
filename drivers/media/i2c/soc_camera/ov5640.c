/*
 * OmniVision OV5640 sensor driver
 *
 * Copyright (C) 2012 Aldebaran Robotics
 * Copyright (C) 2011 Texas Instruments Incorporated - http://www.ti.com/
 * Copyright (C) 2011 Freescale Semiconductor, Inc. All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation version 2.
 *
 * This program is distributed "as is" WITHOUT ANY WARRANTY of any
 * kind, whether express or implied; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */


#include <linux/videodev2.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/log2.h>
#include <linux/delay.h>
#include <linux/module.h>

#include <linux/gpio.h>
#include <linux/clk.h>
#include <linux/regulator/consumer.h>

#include <media/v4l2-mediabus.h>
#include <media/v4l2-device.h>
#include <media/v4l2-ctrls.h>


#ifndef V4L2_CID_GREEN_BALANCE
#define V4L2_CID_GREEN_BALANCE V4L2_CID_DO_WHITE_BALANCE
#endif

static int debug = 0;
module_param(debug, int, 0644);
MODULE_PARM_DESC(debug, "Debug level (0-1)");

struct ov5640_timing_cfg {
	u16 x_addr_start;
	u16 y_addr_start;
	u16 x_addr_end;
	u16 y_addr_end;
	u16 h_output_size;
	u16 v_output_size;
	u16 h_total_size;
	u16 v_total_size;
	u16 isp_h_offset;
	u16 isp_v_offset;
	u8 h_odd_ss_inc;
	u8 h_even_ss_inc;
	u8 v_odd_ss_inc;
	u8 v_even_ss_inc;
};

enum ov5640_mode {
	ov5640_mode_MIN = 0,
	ov5640_mode_QVGA_320_240 = 0,
	ov5640_mode_VGA_640_480 = 1,
	ov5640_mode_MAX = 2
};

struct ov5640_format {
	u32 code;
	enum v4l2_colorspace colorspace;
	u8 fmtreg;
	u8 fmtmuxreg;
};

enum ov5640_format_name {
	ov5640_format_YUYV = 0,
	ov5640_format_UYVY = 1,
	ov5640_format_MAX = 2
};

/* supported pixel formats */
static const struct ov5640_format ov5640_formats[] = {
	[ov5640_format_YUYV] = {
		.code		= MEDIA_BUS_FMT_YUYV8_2X8,
		.colorspace	= V4L2_COLORSPACE_JPEG,
		.fmtreg		= 0x30,
		.fmtmuxreg	= 0,
	},
	[ov5640_format_UYVY] = {
		.code		= MEDIA_BUS_FMT_UYVY8_2X8,
		.colorspace	= V4L2_COLORSPACE_JPEG,
		.fmtreg		= 0x32,
		.fmtmuxreg	= 0,
	}
};

/** struct ov5640 - sensor object
 * @subdev: v4l2_subdev associated data
 * @curr_fmt: color format description
 * @curr_size: size format description
 * @angle: current hue in range [0-255]
 * @auto_focus_enabled: keep track of auto focus state since register is
 * reset when powerdown is performed.*/
struct ov5640 {
	struct v4l2_subdev subdev;
	const struct ov5640_format *curr_fmt;
	const struct ov5640_mode_info *curr_size;
	struct v4l2_ctrl_handler ctrl_handler;
	struct {
		/* exposure cluster */
		struct v4l2_ctrl *auto_exposure;
		struct v4l2_ctrl *exposure;
	};
	int angle;
	bool auto_focus_enabled;
};

static inline struct ov5640 *to_ov5640(struct v4l2_subdev *sd)
{
	return container_of(sd, struct ov5640, subdev);
}

/**
 * struct ov5640_reg - ov5640 register format
 * @reg: 16-bit offset to register
 * @val: 8/16/32-bit register value
 * @length: length of the register
 *
 * Define a structure for OV5640 register initialization values
 */
struct ov5640_reg {
	u16	reg;
	u8	val;
};


/* SYSTEM AND I/O pad control registers */
#define SYSTEM_RESET_00			0x3000
#define SYSTEM_RESET_01			0x3001
#define SYSTEM_RESET_02			0x3002
#define SYSTEM_RESET_03			0x3003
#define CLOCK_ENABLE_00			0x3004
#define CLOCK_ENABLE_02			0x3006
#define SYSTEM_CTRL_0			0x3008
#define MIPI_CONTROL_0			0x300e
#define PAD_OUTPUT_ENABLE_01		0x3017
#define PAD_OUTPUT_ENABLE_02		0x3018
#define CHIP_REVISION			0x302A
#define SC_PLL_CONTROL_0		0x3034
#define SC_PLL_CONTROL_1		0x3035
#define SC_PLL_CONTROL_2		0x3036
#define SC_PLL_CONTROL_3		0x3037

/* SCCB control */
#define SCCB_SYSTEM_CTRL_1		0x3103
#define SYSTEM_ROOT_DIVIDER		0x3108

/* AF control registers*/
#define AF_CTRL				0x3022
#define AF_STATUS			0x3029
#define AF_STATUS_FOCUSING		0x00
#define AF_STATUS_FOCUSED		0x10
#define AF_STATUS_IDLE			0x70 /* focus released */
#define AF_STATUS_STARTUP		0x7e /* focus fw is initializing */
#define AF_STATUS_FIRMWARE		0x7f /* focus fw downloaded and not run */
#define AF_CONTINUE			0x04
#define AF_RELEASE			0x08

/* AEC/AGC control registers */
#define AEC_AGC_MANUAL			0x3503
#define AEC_MANUAL			0x01
#define AGC_MANUAL			0x02
#define AGC_REAL_GAIN_HIGH		0x350A
#define AGC_REAL_GAIN_LOW		0x350B
#define AEC_EXPOSURE_19_16		0x3500
#define AEC_EXPOSURE_15_8		0x3501
#define AEC_EXPOSURE_7_0		0x3502
#define AEC_MAX_EXPOSURE_HIGH_60HZ      0x3A02
#define AEC_MAX_EXPOSURE_LOW_60HZ       0x3A03
#define AEC_STABLE_RANGE_HIGH           0x3A0F
#define AEC_STABLE_RANGE_LOW            0x3A10
#define AEC_HYSTERESIS_RANGE_HIGH       0x3A1B
#define AEC_HYSTERESIS_RANGE_LOW        0x3A1E

#define AEC_MAX_EXPOSURE_HIGH_50HZ      0x3A14
#define AEC_MAX_EXPOSURE_LOW_50HZ       0x3A15

#define AVG_READOUT    0x56A1

/* Timing control */
#define TIMING_HS_HIGH			0x3800
#define TIMING_HS_LOW			0x3801
#define TIMING_VS_HIGH			0x3802
#define TIMING_VS_LOW			0x3803
#define TIMING_HW_HIGH			0x3804
#define TIMING_HW_LOW			0x3805
#define TIMING_VH_HIGH			0x3806
#define TIMING_VH_LOW			0x3807
#define TIMING_DVPHO_HIGH		0x3808
#define TIMING_DVPHO_LOW		0x3809
#define TIMING_DVPVO_HIGH		0x380A
#define TIMING_DVPVO_LOW		0x380B
#define TIMING_HTS_HIGH			0x380C
#define TIMING_HTS_LOW			0x380D
#define TIMING_VTS_HIGH			0x380E
#define TIMING_VTS_LOW			0x380F
#define TIMING_HOFFSET_HIGH		0x3810
#define TIMING_HOFFSET_LOW		0x3811
#define TIMING_VOFFSET_HIGH		0x3812
#define TIMING_VOFFSET_LOW		0x3813
#define TIMING_X_INC			0x3814
#define TIMING_Y_INC			0x3815
#define TIMING_TC_VFLIP			0x3820
#define TIMING_TC_HFLIP			0x3821

/* 50/60Hz detctor control */
#define LIGHT_METER1_THRES_LOW	0x3C07

/* BLC control */
#define BLC_CTRL04				0x4004
#define BLC_CTRL05			0x4005

/* VFIFO control */
#define VFIFO_CTRL_0C			0x460C

/* DVP Control */
#define JPG_MODE_SELECT			0x4713
#define CCIR656_CTRL			0x4719
#define CCIR656_CTRL_00			0x4730

/* MIPI Control */
#define MIPI_CTRL_00			0x4800

/* Format control */
#define FORMAT_CONTROL_0		0x4300

/* ISP top control */
#define ISP_CONTROL_0			0x5000
#define ISP_CONTROL_1			0x5001
#define ISP_CTRL_AWB			0x01

#define FORMAT_MUX_CONTROL		0x501F

/* AWB control registers */
#define AWB_RED_GAIN_HIGH		0x519F
#define AWB_RED_GAIN_LOW		0x51A0
#define AWB_GREEN_GAIN_HIGH		0x51A1
#define AWB_GREEN_GAIN_LOW		0x51A2
#define AWB_BLUE_GAIN_HIGH		0x51A3
#define AWB_BLUE_GAIN_LOW		0x51A4

/* SDE Control */
#define SDE_CTRL_0			0x5580
#define SDE_CTRL_HUE_COS		0x5581
#define SDE_CTRL_HUE_SIN		0x5582
#define SDE_CTRL_CONTRAST		0x5586
#define SDE_CTRL_BRIGHTNESS		0x5587
#define SDE_CTRL_SATURATION_U		0x5583
#define SDE_CTRL_SATURATION_V		0x5584

/* GAMMA Control */
#define GAMMA_CONTROL00                 0x5480
#define GAMMA_YST00                     0x5481
#define GAMMA_YST01                     0x5482
#define GAMMA_YST02                     0x5483
#define GAMMA_YST03                     0x5484
#define GAMMA_YST04                     0x5485
#define GAMMA_YST05                     0x5486
#define GAMMA_YST06                     0x5487
#define GAMMA_YST07                     0x5488
#define GAMMA_YST08                     0x5489
#define GAMMA_YST09                     0x548A
#define GAMMA_YST0A                     0x548B
#define GAMMA_YST0B                     0x548C
#define GAMMA_YST0C                     0x548D
#define GAMMA_YST0D                     0x548E
#define GAMMA_YST0E                     0x548F
#define GAMMA_YST0F                     0x5490

/* OV5640 init register values
 * /!\ HIGHLY WRONG according to "OV5640 init R2.16" /!\*/
static const struct ov5640_reg configscript_common1[] = {
	{ SCCB_SYSTEM_CTRL_1, 0x03 },
	{ PAD_OUTPUT_ENABLE_01, 0x1F }, /*PCLK, D[9:6] output enable*/
	{ PAD_OUTPUT_ENABLE_02, 0xF0 }, /*D[5:0] output enable */
	{ SC_PLL_CONTROL_0, 0x1A },
	{ SC_PLL_CONTROL_1, 0x12 },
	{ SC_PLL_CONTROL_2, 0x38 },
	{ SC_PLL_CONTROL_3, 0x13 },
	{ SYSTEM_ROOT_DIVIDER, 0x01}, // default 1 lane
	{ CCIR656_CTRL, 0x01},
	{ CCIR656_CTRL_00, 0x03},
	{ 0x3630, 0x2e },
	{ 0x3632, 0xe2 },
	{ 0x3633, 0x23 },
	{ 0x3621, 0xe0 },
	{ 0x3704, 0xa0 },
	{ 0x3703, 0x5a },
	{ 0x3715, 0x78 },
	{ 0x3717, 0x01 },
	{ 0x370b, 0x60 },
	{ 0x3705, 0x1a },
	{ 0x3905, 0x02 },
	{ 0x3906, 0x10 },
	{ 0x3901, 0x0a },
	{ 0x3731, 0x12 },
	{ 0x3600, 0x08 },
	{ 0x3601, 0x33 },
	{ 0x302d, 0x60 },
	{ 0x3620, 0x52 },
	{ 0x371b, 0x20 },
	{ 0x471c, 0x50 },

	/* Autoexposure Control Functions */
	{AEC_STABLE_RANGE_HIGH,     0x48},
	{AEC_HYSTERESIS_RANGE_HIGH, 0x48},
	{AEC_STABLE_RANGE_LOW,      0x30},
	{AEC_HYSTERESIS_RANGE_LOW,  0x30},

	{ 0x3a18, 0x00 },
	{ 0x3a19, 0xf8 },

	{ 0x3635, 0x1c },
	{ 0x3634, 0x40 },
	{ 0x3622, 0x01 },
	{ 0x3c01, 0x34 },
	{ 0x3c04, 0x28 },
	{ 0x3c05, 0x98 },
	{ 0x3c06, 0x00 },

	/* VGA */
	{ LIGHT_METER1_THRES_LOW, 0x08 },

	{ 0x3c08, 0x00 },
	{ 0x3c09, 0x1c },
	{ 0x3c0a, 0x9c },
	{ 0x3c0b, 0x40 },

	{ TIMING_TC_VFLIP, 0x41 },
	{ TIMING_TC_HFLIP, 0x01 },

	/* Timing VGA */
	{ TIMING_X_INC, 0x31 },
	{ TIMING_Y_INC, 0x31 },
	{ TIMING_HS_HIGH, 0x00 },
	{ TIMING_HS_LOW, 0x00 },
	{ TIMING_VS_HIGH, 0x00 },
	{ TIMING_VS_LOW, 0x00 },
	{ TIMING_HW_HIGH, 0x0a },
	{ TIMING_HW_LOW, 0x3f },
	{ TIMING_VH_HIGH, 0x07 },
	{ TIMING_VH_LOW, 0x9b },
	{ TIMING_DVPHO_HIGH, 0x02 },
	{ TIMING_DVPHO_LOW, 0x80 },
	{ TIMING_DVPVO_HIGH, 0x01 },
	{ TIMING_DVPVO_LOW, 0xe0 },
	{ TIMING_HTS_HIGH, 0x07 },
	{ TIMING_HTS_LOW, 0x68 },
	{ TIMING_VTS_HIGH, 0x03 },
	{ TIMING_VTS_LOW, 0xd8 },
	{ TIMING_HOFFSET_HIGH, 0x00 },
	{ TIMING_HOFFSET_LOW, 0x10 },
	{ TIMING_VOFFSET_HIGH, 0x00 },
	{ TIMING_VOFFSET_LOW, 0x06 },

	/* VGA*/
	{ 0x3618, 0x00 },
	{ 0x3612, 0x29 },
	{ 0x3708, 0x62 },
	{ 0x3709, 0x52 },
	{ 0x370c, 0x03 },
	{ 0x3a02, 0x03 },
	{ 0x3a03, 0xd8 },
	{ 0x3a08, 0x01 },
	{ 0x3a09, 0x27 },
	{ 0x3a0a, 0x00 },
	{ 0x3a0b, 0xf6 },
	{ 0x3a0e, 0x03 },
	{ 0x3a0d, 0x04 },
	{ 0x3a14, 0x03 },
	{ 0x3a15, 0xd8 },
	{ 0x4001, 0x02 },
	/* VGA */
	{ BLC_CTRL04, 0x02 },

	{ SYSTEM_RESET_00, 0x00 },
	{ SYSTEM_RESET_02, 0x1c },
	{ CLOCK_ENABLE_00, 0xff },
	{ CLOCK_ENABLE_02, 0xc3 },
	{ MIPI_CONTROL_0, 0x58 },
	{ 0x302e, 0x00 },

	/* YUYV */
	{ 0x4300, 0x30 },
	{ 0x501f, 0x00 },

	{ JPG_MODE_SELECT, 0x03 },
	{ 0x4407, 0x04 },

	/* VGA */
	{ VFIFO_CTRL_0C, 0x22 },
	{ 0x3824, 0x02 },
	{ 0x460b, 0x35 },

	{ ISP_CONTROL_0, 0xa7 },
	{ ISP_CONTROL_1, 0xa3 }
};

static const struct ov5640_reg configscript_common2[] = {
	{0x5180, 0xff}, {0x5181, 0xf2}, {0x5182, 0x00},
	{0x5183, 0x14}, {0x5184, 0x25}, {0x5185, 0x24},
	{0x5186, 0x09}, {0x5187, 0x09}, {0x5188, 0x09},
	{0x5189, 0x75}, {0x518a, 0x54}, {0x518b, 0xe0},
	{0x518c, 0xb2}, {0x518d, 0x42}, {0x518e, 0x3d},
	{0x518f, 0x56}, {0x5190, 0x46}, {0x5191, 0xf8},
	{0x5192, 0x04}, {0x5193, 0x70}, {0x5194, 0xf0},
	{0x5195, 0xf0}, {0x5196, 0x03}, {0x5197, 0x01},
	{0x5198, 0x04}, {0x5199, 0x12}, {0x519a, 0x04},
	{0x519b, 0x00}, {0x519c, 0x06}, {0x519d, 0x82},
	{0x519e, 0x38}, {0x5381, 0x1c}, {0x5382, 0x5a},
	{0x5383, 0x06}, {0x5384, 0x0a}, {0x5385, 0x7e},
	{0x5386, 0x88}, {0x5387, 0x7c}, {0x5388, 0x6c},
	{0x5389, 0x10}, {0x538a, 0x01}, {0x538b, 0x98},
	{0x5300, 0x08}, {0x5301, 0x30}, {0x5302, 0x10},
	{0x5303, 0x00}, {0x5304, 0x08}, {0x5305, 0x30},
	{0x5306, 0x08}, {0x5307, 0x16}, {0x5309, 0x08},
	{0x530a, 0x30}, {0x530b, 0x04}, {0x530c, 0x06},

	/* GAMMA */
	{GAMMA_CONTROL00, 0x01}, {GAMMA_YST00, 0x06}, {GAMMA_YST01, 0x19},
	{GAMMA_YST02, 0x30},     {GAMMA_YST03, 0x43}, {GAMMA_YST04, 0x53},
	{GAMMA_YST05, 0x62},     {GAMMA_YST06, 0x6F}, {GAMMA_YST07, 0x7C},
	{GAMMA_YST08, 0x88},     {GAMMA_YST09, 0x9A}, {GAMMA_YST0A, 0xAB},
	{GAMMA_YST0B, 0xBB},     {GAMMA_YST0C, 0xD5}, {GAMMA_YST0D, 0xEB},
	{GAMMA_YST0E, 0xFF},     {GAMMA_YST0F, 0x0F},

	/* SDE */
	{SDE_CTRL_0, 0x07}, {SDE_CTRL_HUE_COS, 0x80}, {SDE_CTRL_HUE_SIN, 0x00},
	{0x5583, 0x40}, {0x5584, 0x10}, {0x5589, 0x10},
	{0x558a, 0x00}, {0x558b, 0xf8}, {0x5800, 0x23},
	{0x5801, 0x15}, {0x5802, 0x10}, {0x5803, 0x10},
	{0x5804, 0x15}, {0x5805, 0x23}, {0x5806, 0x0c},
	{0x5807, 0x08}, {0x5808, 0x05}, {0x5809, 0x05},
	{0x580a, 0x08}, {0x580b, 0x0c}, {0x580c, 0x07},
	{0x580d, 0x03}, {0x580e, 0x00}, {0x580f, 0x00},
	{0x5810, 0x03}, {0x5811, 0x07}, {0x5812, 0x07},
	{0x5813, 0x03}, {0x5814, 0x00}, {0x5815, 0x00},
	{0x5816, 0x03}, {0x5817, 0x07}, {0x5818, 0x0b},
	{0x5819, 0x08}, {0x581a, 0x05}, {0x581b, 0x05},
	{0x581c, 0x07}, {0x581d, 0x0b}, {0x581e, 0x2a},
	{0x581f, 0x16}, {0x5820, 0x11}, {0x5821, 0x11},
	{0x5822, 0x15}, {0x5823, 0x29}, {0x5824, 0xbf},
	{0x5825, 0xaf}, {0x5826, 0x9f}, {0x5827, 0xaf},
	{0x5828, 0xdf}, {0x5829, 0x6f}, {0x582a, 0x8e},
	{0x582b, 0xab}, {0x582c, 0x9e}, {0x582d, 0x7f},
	{0x582e, 0x4f}, {0x582f, 0x89}, {0x5830, 0x86},
	{0x5831, 0x98}, {0x5832, 0x6f}, {0x5833, 0x4f},
	{0x5834, 0x6e}, {0x5835, 0x7b}, {0x5836, 0x7e},
	{0x5837, 0x6f}, {0x5838, 0xde}, {0x5839, 0xbf},
	{0x583a, 0x9f}, {0x583b, 0xbf}, {0x583c, 0xec},
	{0x5025, 0x00}
};

/* OV5640 af firmware register values */
static const struct ov5640_reg af_firmware[] = {
#include "ov5640_af_firmware.h"
};

static const struct ov5640_reg ov5640_setting_7_5fps_QSXGA_2592_1944[] = {
	{SYSTEM_ROOT_DIVIDER, 0x16}, // 0x3108
	{SC_PLL_CONTROL_1, 0x11}, // 0x3035
	{SC_PLL_CONTROL_2, 0x50}, // 0x3036
	{ LIGHT_METER1_THRES_LOW, 0x07}, // 0x3c07

	{0x3618, 0x04},
	{0x3612, 0x2b},
	{0x3708, 0x21},
	{0x3709, 0x12},
	{0x370c, 0x00},
	// banding Filter
	{AEC_MAX_EXPOSURE_HIGH_60HZ, 0x07}, // 0x3a02
	{AEC_MAX_EXPOSURE_LOW_60HZ, 0xb0}, // 0x3a03
	{0x3a08, 0x01}, // B50 high
	{0x3a09, 0x27}, // B50 low
	{0x3a0a, 0x00}, // B60 high
	{0x3a0b, 0xf6}, // B60 low
	{0x3a0e, 0x06}, // B50 max
	{0x3a0d, 0x08}, // B60 max
	{AEC_MAX_EXPOSURE_HIGH_50HZ, 0x07}, // 0x3a14
	{AEC_MAX_EXPOSURE_LOW_50HZ, 0xb0}, // 0x3a15

	{BLC_CTRL04, 0x06}, // 0x4004
	{BLC_CTRL05, 0x1a}, // 0x4005
	{JPG_MODE_SELECT, 0x02}, // 0x4713
	{0x460b, 0x35},
	{VFIFO_CTRL_0C, 0x22}, // 0x460c
	{0x3824, 0x01},
};

static const struct ov5640_reg ov5640_setting_15fps_QSXGA_2592_1944[] = {
	{ LIGHT_METER1_THRES_LOW, 0x07 },
	{0x3618, 0x04},
	{0x3612, 0x2b},
	{0x3708, 0x21},
	{0x3709, 0x12},
	{0x370c, 0x00},
	{AEC_MAX_EXPOSURE_HIGH_60HZ, 0x07},
	{AEC_MAX_EXPOSURE_LOW_60HZ, 0xb0},
	{0x3a08, 0x01},
	{0x3a09, 0x27},
	{0x3a0a, 0x00},
	{0x3a0b, 0xf6},
	{0x3a0e, 0x06},
	{0x3a0d, 0x08},
	{AEC_MAX_EXPOSURE_HIGH_50HZ, 0x07},
	{AEC_MAX_EXPOSURE_LOW_50HZ, 0xb0},
	{BLC_CTRL04, 0x06},
	{JPG_MODE_SELECT, 0x02},
	{VFIFO_CTRL_0C, 0x20},
	{0x3824, 0x04},
	{0x460b, 0x37},
};

static const struct ov5640_reg ov5640_setting_30fps_VGA_640_480[] = {
	{SYSTEM_ROOT_DIVIDER, 0x01}, // 0x3108
	{SC_PLL_CONTROL_1, 0x12}, // 0x3035
	{SC_PLL_CONTROL_2, 0x38}, // 0x3036
	{LIGHT_METER1_THRES_LOW, 0x08},
	{0x3618, 0x00},
	{0x3612, 0x29},
	{0x3708, 0x62},
	{0x3709, 0x52},
	{0x370c, 0x03},
	{AEC_MAX_EXPOSURE_HIGH_60HZ, 0x03},
	{AEC_MAX_EXPOSURE_LOW_60HZ, 0xd8},
	{0x3a08, 0x01},
	{0x3a09, 0x27},
	{0x3a0a, 0x00},
	{0x3a0b, 0xf6},
	{0x3a0e, 0x03},
	{0x3a0d, 0x04},
	{AEC_MAX_EXPOSURE_HIGH_50HZ, 0x03},
	{AEC_MAX_EXPOSURE_LOW_50HZ, 0xd8},
	{BLC_CTRL04, 0x02},
	{JPG_MODE_SELECT, 0x03},
	{VFIFO_CTRL_0C, 0x22},
	{0x3824, 0x02},
	{0x460b, 0x35},
};

static const struct ov5640_reg ov5640_setting_30fps_QVGA_320_240[] = {
	{SYSTEM_ROOT_DIVIDER, 0x01}, // 0x3108
	{SC_PLL_CONTROL_1, 0x12}, // 0x3035
	{SC_PLL_CONTROL_2, 0x38}, // 0x3036
	{ LIGHT_METER1_THRES_LOW, 0x08 },
	{0x3618, 0x00},
	{0x3612, 0x29},
	{0x3708, 0x62},
	{0x3709, 0x52},
	{0x370c, 0x03},
	{AEC_MAX_EXPOSURE_HIGH_60HZ, 0x03},
	{AEC_MAX_EXPOSURE_LOW_60HZ, 0xd8},
	{0x3a08, 0x01},
	{0x3a09, 0x27},
	{0x3a0a, 0x00},
	{0x3a0b, 0xf6},
	{0x3a0e, 0x03},
	{0x3a0d, 0x04},
	{AEC_MAX_EXPOSURE_HIGH_50HZ, 0x03},
	{AEC_MAX_EXPOSURE_LOW_50HZ, 0xd8},
	{BLC_CTRL04, 0x02},
	{JPG_MODE_SELECT, 0x03},
	{VFIFO_CTRL_0C, 0x22},
	{0x3824, 0x02},
	{0x460b, 0x35},
};

static const struct ov5640_reg ov5640_setting_30fps_720P_1280_720[] = {
	{SYSTEM_ROOT_DIVIDER, 0x02}, // 0x3108
	{SC_PLL_CONTROL_1, 0x21}, // 0x3035
	{SC_PLL_CONTROL_2, 0x54}, // 0x3036
	{LIGHT_METER1_THRES_LOW, 0x07},
	{0x3618, 0x00},
	{0x3612, 0x29},
	{0x3708, 0x62},
	{0x3709, 0x52},
	{0x370c, 0x03},
	{AEC_MAX_EXPOSURE_HIGH_60HZ, 0x02},
	{AEC_MAX_EXPOSURE_LOW_60HZ, 0xe4},
	{0x3a08, 0x01},
	{0x3a09, 0xbc},
	{0x3a0a, 0x01},
	{0x3a0b, 0x72},
	{0x3a0e, 0x01},
	{0x3a0d, 0x02},
	{AEC_MAX_EXPOSURE_HIGH_50HZ, 0x02},
	{AEC_MAX_EXPOSURE_LOW_50HZ, 0xe4},
	{BLC_CTRL04, 0x02},
	{JPG_MODE_SELECT, 0x02},
	{VFIFO_CTRL_0C, 0x20},
	{0x3824, 0x04},
	{0x460b, 0x37},
};

static const struct ov5640_reg ov5640_setting_30fps_1080P_1920_1080[] = {
	{SYSTEM_ROOT_DIVIDER, 0x02}, // 0x3108
	{SC_PLL_CONTROL_1, 0x21}, // 0x3035
	{SC_PLL_CONTROL_2, 0x54}, // 0x3036
	{LIGHT_METER1_THRES_LOW, 0x07},
	{0x3618, 0x04},
	{0x3612, 0x2b},
	{0x3708, 0x62},
	{0x3709, 0x12},
	{0x370c, 0x00},
	{AEC_MAX_EXPOSURE_HIGH_60HZ, 0x04},
	{AEC_MAX_EXPOSURE_LOW_60HZ, 0x60},
	{0x3a08, 0x01},
	{0x3a09, 0x50},
	{0x3a0a, 0x01},
	{0x3a0b, 0x18},
	{0x3a0e, 0x03},
	{0x3a0d, 0x04},
	{AEC_MAX_EXPOSURE_HIGH_50HZ, 0x04},
	{AEC_MAX_EXPOSURE_LOW_50HZ, 0x60},
	{BLC_CTRL04, 0x06},
	{JPG_MODE_SELECT, 0x02},
	{VFIFO_CTRL_0C, 0x20},
	{0x3824, 0x04},
	{0x460b, 0x37},
};

struct ov5640_mode_info {
	enum ov5640_mode mode;
	u32 width;
	u32 height;
	const struct ov5640_reg *init_data_ptr;
	u32 init_data_size;
};

const struct ov5640_mode_info ov5640_mode_info_data[ov5640_mode_MAX] = {
	[ov5640_mode_QVGA_320_240]{
		ov5640_mode_QVGA_320_240,   320,  240,
		ov5640_setting_30fps_QVGA_320_240,
		ARRAY_SIZE(ov5640_setting_30fps_QVGA_320_240),
	},
	[ov5640_mode_VGA_640_480]{
		ov5640_mode_VGA_640_480,    640,  480,
		ov5640_setting_30fps_VGA_640_480,
		ARRAY_SIZE(ov5640_setting_30fps_VGA_640_480),
	},
};


static const struct ov5640_timing_cfg timing_cfg[ov5640_mode_MAX] = {
	[ov5640_mode_QVGA_320_240] = {
		.x_addr_start = 0,
		.y_addr_start = 4,
		.x_addr_end = 2623,
		.y_addr_end = 1947,
		.h_output_size = 320,
		.v_output_size = 240,
		.h_total_size = 1896,
		.v_total_size = 984,
		.isp_h_offset = 16,
		.isp_v_offset = 6,
		.h_odd_ss_inc = 3,
		.h_even_ss_inc = 1,
		.v_odd_ss_inc = 3,
		.v_even_ss_inc = 1,
	},
	[ov5640_mode_VGA_640_480] = {
		.x_addr_start = 0,
		.y_addr_start = 4,
		.x_addr_end = 2623,
		.y_addr_end = 1947,
		.h_output_size = 640,
		.v_output_size = 480,
		.h_total_size = 1896,
		.v_total_size = 984,
		.isp_h_offset = 16,
		.isp_v_offset = 6,
		.h_odd_ss_inc = 3,
		.h_even_ss_inc = 1,
		.v_odd_ss_inc = 3,
		.v_even_ss_inc = 1,
	},
};

/* Find a frame size in an array */
static int ov5640_find_framesize(u32 width, u32 height)
{
	int i;

	for (i = 0; i < ov5640_mode_MAX; i++) {
		if ((ov5640_mode_info_data[i].width >= width) &&
				(ov5640_mode_info_data[i].height >= height))
			break;
	}

	/* If not found, select biggest */
	if (i >= ov5640_mode_MAX)
		i = ov5640_mode_MAX - 1;

	return i;
}



/**
 * ov5640_reg_read - Read a value from a register in an ov5640 sensor device
 * @client: i2c driver client structure
 * @reg: register address / offset
 * @val: stores the value that gets read
 *
 * Read a value from a register in an ov5640 sensor device.
 * The value is returned in 'val'.
 * Returns zero if successful, or non-zero otherwise.
 */
static int ov5640_reg_read(struct i2c_client *client, u16 reg, u8 *val)
{
	int ret;
	u8 data[2] = {0};
	struct i2c_msg msg = {
		.addr	= client->addr,
		.flags	= 0,
		.len	= 2,
		.buf	= data,
	};

	data[0] = (u8)(reg >> 8);
	data[1] = (u8)(reg & 0xff);

	ret = i2c_transfer(client->adapter, &msg, 1);
	if (ret < 0)
		goto err;

	msg.flags = I2C_M_RD;
	msg.len = 1;
	ret = i2c_transfer(client->adapter, &msg, 1);
	if (ret < 0)
		goto err;

	*val = data[0];
	return 0;

err:
	return ret;
}

/**
 * Write a value to a register in ov5640 sensor device.
 * @client: i2c driver client structure.
 * @reg: Address of the register to read value from.
 * @val: Value to be written to a specific register.
 * Returns zero if successful, or non-zero otherwise.
 */
static int ov5640_reg_write(struct i2c_client *client, u16 reg, u8 val)
{
	int ret;
	unsigned char data[3] = { (u8)(reg >> 8), (u8)(reg & 0xff), val };
	struct i2c_msg msg = {
		.addr	= client->addr,
		.flags	= 0,
		.len	= 3,
		.buf	= data,
	};

	ret = i2c_transfer(client->adapter, &msg, 1);
	if (ret < 0) {
		return ret;
	}

	return 0;
}

/**
 * Initialize a list of ov5640 registers.
 * The list of registers is terminated by the pair of values
 * @client: i2c driver client structure.
 * @reglist[]: List of address of the registers to write data.
 * Returns zero if successful, or non-zero otherwise.
 */
static int ov5640_reg_writes(struct i2c_client *client,
		const struct ov5640_reg reglist[],
		int size)
{
	int err = 0, i;

	for (i = 0; i < size; i++) {
		err = ov5640_reg_write(client, reglist[i].reg,
				reglist[i].val);
		if (err)
			return err;
	}
	return 0;
}

/**
 * Write up to 510 values to a register in ov5640 sensor device.
 * @client: i2c driver client structure.
 * @reg: Address of the register to read value from.
 * @val: Value to be written to a specific register.
 * @len: Length of the values to be written.
 * Returns zero if successful, or non-zero otherwise.
 */
static int ov5640_reg_write_up_to_510_bytes(struct i2c_client *client, u16 reg, u8 *val, int len)
{
	int ret;
	unsigned char data[510 + 2] = { (u8)(reg >> 8), (u8)(reg & 0xff) };

	struct i2c_msg msg = {
		.addr	= client->addr,
		.flags	= 0,
		.len	= len + 2,
		.buf	= data,
	};

	if (len > 510)
		return -EINVAL;

	memcpy(data + 2, val, len);

	ret = i2c_transfer(client->adapter, &msg, 1);
	if (ret < 0) {
		printk("%s transfert error ret = %d", __func__, ret);
		return ret;
	}

	return 0;
}

static int ov5640_reg_write_burst(struct i2c_client *client,
		const struct ov5640_reg *reglist,
		int size)
{
	int err;
	unsigned char buf[510];
	int burst_start = 0, burst_length;

	for (burst_start = 0; burst_start < size; burst_start += burst_length) {
		burst_length = 0;
		do {
			buf[burst_length] = reglist[burst_start + burst_length].val;
			burst_length++;
		} while (burst_start + burst_length < size &&
			burst_length < 510 &&
			reglist[burst_start + burst_length - 1].reg + 1 ==
				reglist[burst_start + burst_length].reg);
		err = ov5640_reg_write_up_to_510_bytes(client, reglist[burst_start].reg,
							buf, burst_length);
		if (err) {
			return err;
		}
	}

	return 0;
}

static int ov5640_reg_set(struct i2c_client *client, u16 reg, u8 val)
{
	int ret;
	u8 tmpval = 0;

	ret = ov5640_reg_read(client, reg, &tmpval);
	if (ret)
		return ret;

	return ov5640_reg_write(client, reg, tmpval | val);
}

static int ov5640_reg_clr(struct i2c_client *client, u16 reg, u8 val)
{
	int ret;
	u8 tmpval = 0;

	ret = ov5640_reg_read(client, reg, &tmpval);
	if (ret)
		return ret;

	return ov5640_reg_write(client, reg, tmpval & ~val);
}

static int ov5640_config_timing(struct v4l2_subdev *sd)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct ov5640 *ov5640 = to_ov5640(sd);
	int ret, i;

	i = ov5640_find_framesize(ov5640->curr_size->width, ov5640->curr_size->height);

	ret = ov5640_reg_write(client,
			TIMING_HS_HIGH,
			(timing_cfg[i].x_addr_start & 0xFF00) >> 8);
	if (ret)
		return ret;

	ret = ov5640_reg_write(client,
			TIMING_HS_LOW,
			timing_cfg[i].x_addr_start & 0xFF);
	if (ret)
		return ret;

	ret = ov5640_reg_write(client,
			TIMING_VS_HIGH,
			(timing_cfg[i].y_addr_start & 0xFF00) >> 8);
	if (ret)
		return ret;

	ret = ov5640_reg_write(client,
			TIMING_VS_LOW,
			timing_cfg[i].y_addr_start & 0xFF);
	if (ret)
		return ret;

	ret = ov5640_reg_write(client,
			TIMING_HW_HIGH,
			(timing_cfg[i].x_addr_end & 0xFF00) >> 8);
	if (ret)
		return ret;

	ret = ov5640_reg_write(client,
			TIMING_HW_LOW,
			timing_cfg[i].x_addr_end & 0xFF);
	if (ret)
		return ret;

	ret = ov5640_reg_write(client,
			TIMING_VH_HIGH,
			(timing_cfg[i].y_addr_end & 0xFF00) >> 8);
	if (ret)
		return ret;

	ret = ov5640_reg_write(client,
			TIMING_VH_LOW,
			timing_cfg[i].y_addr_end & 0xFF);
	if (ret)
		return ret;

	ret = ov5640_reg_write(client,
			TIMING_DVPHO_HIGH,
			(timing_cfg[i].h_output_size & 0xFF00) >> 8);
	if (ret)
		return ret;

	ret = ov5640_reg_write(client,
			TIMING_DVPHO_LOW,
			timing_cfg[i].h_output_size & 0xFF);
	if (ret)
		return ret;

	ret = ov5640_reg_write(client,
			TIMING_DVPVO_HIGH,
			(timing_cfg[i].v_output_size & 0xFF00) >> 8);
	if (ret)
		return ret;

	ret = ov5640_reg_write(client,
			TIMING_DVPVO_LOW,
			timing_cfg[i].v_output_size & 0xFF);
	if (ret)
		return ret;

	ret = ov5640_reg_write(client,
			TIMING_HTS_HIGH,
			(timing_cfg[i].h_total_size & 0xFF00) >> 8);
	if (ret)
		return ret;

	ret = ov5640_reg_write(client,
			TIMING_HTS_LOW,
			timing_cfg[i].h_total_size & 0xFF);
	if (ret)
		return ret;

	ret = ov5640_reg_write(client,
			TIMING_VTS_HIGH,
			(timing_cfg[i].v_total_size & 0xFF00) >> 8);
	if (ret)
		return ret;

	ret = ov5640_reg_write(client,
			TIMING_VTS_LOW,
			timing_cfg[i].v_total_size & 0xFF);
	if (ret)
		return ret;

	ret = ov5640_reg_write(client,
			TIMING_HOFFSET_HIGH,
			(timing_cfg[i].isp_h_offset & 0xFF00) >> 8);
	if (ret)
		return ret;

	ret = ov5640_reg_write(client,
			TIMING_HOFFSET_LOW,
			timing_cfg[i].isp_h_offset & 0xFF);
	if (ret)
		return ret;

	ret = ov5640_reg_write(client,
			TIMING_VOFFSET_HIGH,
			(timing_cfg[i].isp_v_offset & 0xFF00) >> 8);
	if (ret)
		return ret;

	ret = ov5640_reg_write(client,
			TIMING_VOFFSET_LOW,
			timing_cfg[i].isp_v_offset & 0xFF);
	if (ret)
		return ret;

	ret = ov5640_reg_write(client,
			TIMING_X_INC,
			((timing_cfg[i].h_odd_ss_inc & 0xF) << 4) |
			(timing_cfg[i].h_even_ss_inc & 0xF));
	if (ret)
		return ret;

	ret = ov5640_reg_write(client,
			TIMING_Y_INC,
			((timing_cfg[i].v_odd_ss_inc & 0xF) << 4) |
			(timing_cfg[i].v_even_ss_inc & 0xF));

	return ret;
}

static const struct ov5640_mode_info *try_size(struct v4l2_subdev *sd,
		struct v4l2_mbus_framefmt *mf)
{
	int i;
	/*
	 * Round requested image size down to the nearest
	 * we support, but not below the smallest.
	 */
	for (i = 0; i < ov5640_mode_MAX; i++)
	{
		if ((ov5640_mode_info_data[i].width >= mf->width) &&
				(ov5640_mode_info_data[i].height >= mf->height))
			break;
	}

	/* If not found, select biggest */
	if (i >= ov5640_mode_MAX)
		i = ov5640_mode_MAX - 1;

	/*
	 * Note the size we'll actually handle.
	 */
	mf->width = ov5640_mode_info_data[i].width;
	mf->height = ov5640_mode_info_data[i].height;
	return &ov5640_mode_info_data[i];
}

static const struct ov5640_format *try_fmt(struct v4l2_subdev *sd,
		struct v4l2_mbus_framefmt *mf)
{
	int i = ARRAY_SIZE(ov5640_formats);
	while (--i)
		if (mf->code == ov5640_formats[i].code)
			break;

	mf->code = ov5640_formats[i].code;
	mf->colorspace = ov5640_formats[i].colorspace;
	mf->field = V4L2_FIELD_NONE;

	return &ov5640_formats[i];
}

/* -----------------------------------------------------------------------------
 * V4L2 subdev internal operations
 */

/* Camera debug functions */
#ifdef CONFIG_VIDEO_ADV_DEBUG
static int ov5640_g_register(struct v4l2_subdev *sd, struct v4l2_dbg_register *reg)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	u16 addr = reg->reg & 0xffff;
	u8 val = 0;

	ov5640_reg_read(client, addr, &val);
	v4l2_dbg(2, debug, sd, "OV5640: ov5640_g_register addr: 0x%x, val: 0x%x\n", addr, val);
	reg->val = (u32)val;
	return 0;
}

static int ov5640_s_register(struct v4l2_subdev *sd, const struct v4l2_dbg_register *reg)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	u16 addr = reg->reg & 0xffff;
	u8 val = reg->val & 0xff;

	ov5640_reg_write(client, addr, val);
	v4l2_dbg(2, debug, sd, "OV5640: ov5640_s_register addr: 0x%x, val: 0x%x\n", addr, val);
	return 0;
}
#endif

/*
 * Camera control functions
 */

static int ov5640_g_hflip(struct v4l2_subdev *sd, __s32 *value)
{
	u8 reg;
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	ov5640_reg_read(client, TIMING_TC_HFLIP, &reg);

	*value=(reg&0x04)>>2;

	v4l2_dbg(2, debug, sd, "get horizontal flip: %d\n", *value);
	return 0;
}

static int ov5640_s_hflip(struct v4l2_subdev *sd, int value)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	v4l2_dbg(2, debug, sd, "set horizontal flip: %d\n", value);
	if(value==0)
		ov5640_reg_clr(client, TIMING_TC_HFLIP, 0x06);
	else
		ov5640_reg_set(client, TIMING_TC_HFLIP, 0x06);
	return 0;
}


static int ov5640_g_vflip(struct v4l2_subdev *sd, __s32 *value)
{
	u8 reg;
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	ov5640_reg_read(client, TIMING_TC_VFLIP, &reg);

	*value=(reg&0x04)>>2;

	v4l2_dbg(2, debug, sd, "get vertical flip: %d\n", *value);
	return 0;
}

static int ov5640_s_vflip(struct v4l2_subdev *sd, int value)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	v4l2_dbg(2, debug, sd, "set vertical flip: %d\n", value);
	if(value==0)
		ov5640_reg_clr(client, TIMING_TC_VFLIP, 0x06);
	else
		ov5640_reg_set(client, TIMING_TC_VFLIP, 0x06);
	return 0;
}

static int ov5640_s_auto_focus(struct v4l2_subdev *sd, int value)
{
	int ret = 0;
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	v4l2_dbg(2, debug, sd, "set auto focus: %d\n", value);
	if (value==1)
		ret = ov5640_reg_write(client, AF_CTRL, AF_CONTINUE);
	else
		ret = ov5640_reg_write(client, AF_CTRL, AF_RELEASE);

	if (ret) {
		v4l_err(client, "Failed to set auto focus control register\n");
	}
	to_ov5640(sd)->auto_focus_enabled = (value == 1) ? true: false;
	return ret;
}

static int ov5640_g_auto_focus(struct v4l2_subdev *sd, __s32 *value)
{
	*value = to_ov5640(sd)->auto_focus_enabled ? 1: 0;
	v4l2_dbg(2, debug, sd, "get auto focus: %d\n", *value);
	return 0;
}

static int ov5640_s_auto_exposure(struct v4l2_subdev *sd, int value)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	v4l2_dbg(2, debug, sd, "set auto exposure: %d\n", value);
	if (value == V4L2_EXPOSURE_AUTO)
		ov5640_reg_clr(client, AEC_AGC_MANUAL, AEC_MANUAL);
	else if (value == V4L2_EXPOSURE_MANUAL)
		ov5640_reg_set(client, AEC_AGC_MANUAL, AEC_MANUAL);
	else
		return -EINVAL;
	return 0;
}

static int ov5640_s_auto_white_balance(struct v4l2_subdev *sd, int value)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	v4l2_dbg(2, debug, sd, "set auto white balance: %d\n", value);
	if(value==0)
		ov5640_reg_clr(client, ISP_CONTROL_1, ISP_CTRL_AWB);
	else
		ov5640_reg_set(client, ISP_CONTROL_1, ISP_CTRL_AWB);
	return 0;
}

static int ov5640_g_auto_white_balance(struct v4l2_subdev *sd, __s32 *value)
{
	u8 reg;
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	ov5640_reg_read(client, ISP_CONTROL_1, &reg);

	*value=(reg&ISP_CTRL_AWB);
	v4l2_dbg(2, debug, sd, "get auto white balance: %d\n", *value);
	return 0;
}

static int ov5640_s_auto_gain(struct v4l2_subdev *sd, int value)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	v4l2_dbg(2, debug, sd, "set auto gain: %d\n", value);
	if(value==1)
		ov5640_reg_clr(client, AEC_AGC_MANUAL, AGC_MANUAL);
	else
		ov5640_reg_set(client, AEC_AGC_MANUAL, AGC_MANUAL);
	return 0;
}

static int ov5640_g_auto_gain(struct v4l2_subdev *sd, __s32 *value)
{
	u8 reg;
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	ov5640_reg_read(client, AEC_AGC_MANUAL, &reg);

	*value=!((reg&AGC_MANUAL)>>1);
	v4l2_dbg(2, debug, sd, "get auto gain: %d\n", *value);
	return 0;
}

static int ov5640_s_brightness(struct v4l2_subdev *sd, int value)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	v4l2_dbg(2, debug, sd, "set brightness: %d\n", value);
	ov5640_reg_write(client, SDE_CTRL_BRIGHTNESS, value);

	return 0;
}

static int ov5640_g_brightness(struct v4l2_subdev *sd, __s32 *value)
{
	u8 reg;
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	ov5640_reg_read(client, SDE_CTRL_BRIGHTNESS, &reg);

	*value=reg;
	v4l2_dbg(2, debug, sd, "get brightness: %d\n", *value);
	return 0;
}

static int ov5640_s_contrast(struct v4l2_subdev *sd, int value)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	v4l2_dbg(2, debug, sd, "set contrast: %d\n", value);
	ov5640_reg_write(client, SDE_CTRL_CONTRAST, value);

	return 0;
}

static int ov5640_g_contrast(struct v4l2_subdev *sd, __s32 *value)
{
	u8 reg;
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	ov5640_reg_read(client, SDE_CTRL_CONTRAST, &reg);

	*value=reg;
	v4l2_dbg(2, debug, sd, "get contrast: %d\n", *value);
	return 0;
}

static int ov5640_s_saturation(struct v4l2_subdev *sd, int value)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	v4l2_dbg(2, debug, sd, "set saturation: %d\n", value);
	ov5640_reg_write(client, SDE_CTRL_SATURATION_U, value);
	ov5640_reg_write(client, SDE_CTRL_SATURATION_V, value);

	return 0;
}

static int ov5640_g_saturation(struct v4l2_subdev *sd, __s32 *value)
{
	u8 reg;
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	ov5640_reg_read(client, SDE_CTRL_SATURATION_U, &reg);

	*value=reg;
	v4l2_dbg(2, debug, sd, "get saturation: %d\n", *value);
	return 0;
}

/*
#!/usr/bin/env python
import numpy as np
t = np.arange(-180, 181, 1.0) * np.pi / 180
cos = np.round(np.cos(t) * 128)
sin = np.round(np.sin(t) * 128)

def generate(name, table):
s = "static char "+ name +"_lut[] = {"
for i in range(len(table)):
if 0 == i%8 : s += "\n";
s += `int(table[i])`
if i != len(table)-1: s += ", ";
s += "};"
print s
generate("cos", cos)
print ""
generate("sin", sin)
*/
static unsigned char cos_lut[] = {
	-128, -128, -128, -128, -128, -128, -127, -127,
	-127, -126, -126, -126, -125, -125, -124, -124,
	-123, -122, -122, -121, -120, -119, -119, -118,
	-117, -116, -115, -114, -113, -112, -111, -110,
	-109, -107, -106, -105, -104, -102, -101, -99,
	-98, -97, -95, -94, -92, -91, -89, -87,
	-86, -84, -82, -81, -79, -77, -75, -73,
	-72, -70, -68, -66, -64, -62, -60, -58,
	-56, -54, -52, -50, -48, -46, -44, -42,
	-40, -37, -35, -33, -31, -29, -27, -24,
	-22, -20, -18, -16, -13, -11, -9, -7,
	-4, -2, 0, 2, 4, 7, 9, 11,
	13, 16, 18, 20, 22, 24, 27, 29,
	31, 33, 35, 37, 40, 42, 44, 46,
	48, 50, 52, 54, 56, 58, 60, 62,
	64, 66, 68, 70, 72, 73, 75, 77,
	79, 81, 82, 84, 86, 87, 89, 91,
	92, 94, 95, 97, 98, 99, 101, 102,
	104, 105, 106, 107, 109, 110, 111, 112,
	113, 114, 115, 116, 117, 118, 119, 119,
	120, 121, 122, 122, 123, 124, 124, 125,
	125, 126, 126, 126, 127, 127, 127, 128,
	128, 128, 128, 128, 128, 128, 128, 128,
	128, 128, 127, 127, 127, 126, 126, 126,
	125, 125, 124, 124, 123, 122, 122, 121,
	120, 119, 119, 118, 117, 116, 115, 114,
	113, 112, 111, 110, 109, 107, 106, 105,
	104, 102, 101, 99, 98, 97, 95, 94,
	92, 91, 89, 87, 86, 84, 82, 81,
	79, 77, 75, 73, 72, 70, 68, 66,
	64, 62, 60, 58, 56, 54, 52, 50,
	48, 46, 44, 42, 40, 37, 35, 33,
	31, 29, 27, 24, 22, 20, 18, 16,
	13, 11, 9, 7, 4, 2, 0, -2,
	-4, -7, -9, -11, -13, -16, -18, -20,
	-22, -24, -27, -29, -31, -33, -35, -37,
	-40, -42, -44, -46, -48, -50, -52, -54,
	-56, -58, -60, -62, -64, -66, -68, -70,
	-72, -73, -75, -77, -79, -81, -82, -84,
	-86, -87, -89, -91, -92, -94, -95, -97,
	-98, -99, -101, -102, -104, -105, -106, -107,
	-109, -110, -111, -112, -113, -114, -115, -116,
	-117, -118, -119, -119, -120, -121, -122, -122,
	-123, -124, -124, -125, -125, -126, -126, -126,
	-127, -127, -127, -128, -128, -128, -128, -128, -128};

static unsigned char sin_lut[] = {
	0, -2, -4, -7, -9, -11, -13, -16,
	-18, -20, -22, -24, -27, -29, -31, -33,
	-35, -37, -40, -42, -44, -46, -48, -50,
	-52, -54, -56, -58, -60, -62, -64, -66,
	-68, -70, -72, -73, -75, -77, -79, -81,
	-82, -84, -86, -87, -89, -91, -92, -94,
	-95, -97, -98, -99, -101, -102, -104, -105,
	-106, -107, -109, -110, -111, -112, -113, -114,
	-115, -116, -117, -118, -119, -119, -120, -121,
	-122, -122, -123, -124, -124, -125, -125, -126,
	-126, -126, -127, -127, -127, -128, -128, -128,
	-128, -128, -128, -128, -128, -128, -128, -128,
	-127, -127, -127, -126, -126, -126, -125, -125,
	-124, -124, -123, -122, -122, -121, -120, -119,
	-119, -118, -117, -116, -115, -114, -113, -112,
	-111, -110, -109, -107, -106, -105, -104, -102,
	-101, -99, -98, -97, -95, -94, -92, -91,
	-89, -87, -86, -84, -82, -81, -79, -77,
	-75, -73, -72, -70, -68, -66, -64, -62,
	-60, -58, -56, -54, -52, -50, -48, -46,
	-44, -42, -40, -37, -35, -33, -31, -29,
	-27, -24, -22, -20, -18, -16, -13, -11,
	-9, -7, -4, -2, 0, 2, 4, 7,
	9, 11, 13, 16, 18, 20, 22, 24,
	27, 29, 31, 33, 35, 37, 40, 42,
	44, 46, 48, 50, 52, 54, 56, 58,
	60, 62, 64, 66, 68, 70, 72, 73,
	75, 77, 79, 81, 82, 84, 86, 87,
	89, 91, 92, 94, 95, 97, 98, 99,
	101, 102, 104, 105, 106, 107, 109, 110,
	111, 112, 113, 114, 115, 116, 117, 118,
	119, 119, 120, 121, 122, 122, 123, 124,
	124, 125, 125, 126, 126, 126, 127, 127,
	127, 128, 128, 128, 128, 128, 128, 128,
	128, 128, 128, 128, 127, 127, 127, 126,
	126, 126, 125, 125, 124, 124, 123, 122,
	122, 121, 120, 119, 119, 118, 117, 116,
	115, 114, 113, 112, 111, 110, 109, 107,
	106, 105, 104, 102, 101, 99, 98, 97,
	95, 94, 92, 91, 89, 87, 86, 84,
	82, 81, 79, 77, 75, 73, 72, 70,
	68, 66, 64, 62, 60, 58, 56, 54,
	52, 50, 48, 46, 44, 42, 40, 37,
	35, 33, 31, 29, 27, 24, 22, 20,
	18, 16, 13, 11, 9, 7, 4, 2, 0};

static int ov5640_s_hue(struct v4l2_subdev *sd, int value)
{
	int ret;
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	u8 cos = cos_lut[value+180];
	u8 sin = sin_lut[value+180];

	v4l2_dbg(1, debug, sd, " %s set hue: angle: %d, cos: %d, sin: %d\n",
			__func__, value, cos, sin);
	ret = ov5640_reg_write(client, SDE_CTRL_HUE_COS, cos);
	ret += ov5640_reg_write(client, SDE_CTRL_HUE_SIN, sin);
	to_ov5640(sd)->angle = value;

	return ret;
}

static int ov5640_g_hue(struct v4l2_subdev *sd, __s32 *value)
{
	u8 reg_cos, reg_sin;
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	ov5640_reg_read(client, SDE_CTRL_HUE_COS, &reg_cos);
	ov5640_reg_read(client, SDE_CTRL_HUE_SIN, &reg_sin);

	*value = to_ov5640(sd)->angle;
	v4l2_dbg(1, debug, sd, "get hue: angle: %d, cos: %d, sin: %d\n", *value, reg_cos, reg_sin);
	return 0;
}

static int ov5640_s_gain(struct v4l2_subdev *sd, int value)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	v4l2_dbg(2, debug, sd, "set gain: %d\n", value);
	ov5640_reg_write(client, AGC_REAL_GAIN_HIGH, (value&0x300)>>8);
	ov5640_reg_write(client, AGC_REAL_GAIN_LOW, (value&0xFF));

	return 0;
}

static int ov5640_g_gain(struct v4l2_subdev *sd, __s32 *value)
{
	u8 reg_high;
	u8 reg_low;
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	ov5640_reg_read(client, AGC_REAL_GAIN_HIGH, &reg_high);
	ov5640_reg_read(client, AGC_REAL_GAIN_LOW, &reg_low);

	*value=(((u32)reg_high)<<8)|((u32)reg_low);

	v4l2_dbg(2, debug, sd, "get gain: %d\n", *value);
	return 0;
}

static int ov5640_g_red_balance(struct v4l2_subdev *sd, __s32 *value)
{
	u8 reg_high;
	u8 reg_low;
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	ov5640_reg_read(client, AWB_RED_GAIN_HIGH, &reg_high);
	ov5640_reg_read(client, AWB_RED_GAIN_LOW, &reg_low);

	*value=(((u32)reg_high)<<8)|((u32)reg_low);

	v4l2_dbg(2, debug, sd, "get red balance: %d\n", *value);
	return 0;
}

static int ov5640_g_green_balance(struct v4l2_subdev *sd, __s32 *value)
{
	u8 reg_high;
	u8 reg_low;
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	ov5640_reg_read(client, AWB_GREEN_GAIN_HIGH, &reg_high);
	ov5640_reg_read(client, AWB_GREEN_GAIN_LOW, &reg_low);

	*value=(((u32)reg_high)<<8)|((u32)reg_low);

	v4l2_dbg(2, debug, sd, "get green balance: %d\n", *value);
	return 0;
}

static int ov5640_g_blue_balance(struct v4l2_subdev *sd, __s32 *value)
{
	u8 reg_high;
	u8 reg_low;
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	ov5640_reg_read(client, AWB_BLUE_GAIN_HIGH, &reg_high);
	ov5640_reg_read(client, AWB_BLUE_GAIN_LOW, &reg_low);

	*value=(((u32)reg_high)<<8)|((u32)reg_low);

	v4l2_dbg(2, debug, sd, "get blue balance: %d\n", *value);
	return 0;
}

static int ov5640_s_exposure(struct v4l2_subdev *sd, int value)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	v4l2_dbg(2, debug, sd, "set exposure: %d\n", value);
	value = value << 4;
	ov5640_reg_write(client, AEC_EXPOSURE_19_16, (value&0xF0000)>>16);
	ov5640_reg_write(client, AEC_EXPOSURE_15_8, (value&0xFF00)>>8);
	ov5640_reg_write(client, AEC_EXPOSURE_7_0, (value&0xF0));

	return 0;
}

static int ov5640_g_exposure(struct v4l2_subdev *sd, __s32 *value)
{
	u8 reg_7_0;
	u8 reg_15_8;
	u8 reg_19_16;
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	ov5640_reg_read(client, AEC_EXPOSURE_19_16, &reg_19_16);
	ov5640_reg_read(client, AEC_EXPOSURE_15_8, &reg_15_8);
	ov5640_reg_read(client, AEC_EXPOSURE_7_0, &reg_7_0);

	*value=(u32)reg_7_0|(u32)(reg_15_8)<<8|(u32)(reg_19_16)<<16;
	*value = *value >> 4;
	v4l2_dbg(2, debug, sd, "get exposure: %d\n", *value);
	return 0;
}

static int ov5640_g_luminance(struct v4l2_subdev *sd, __s32 *value)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	u8 reg;
	ov5640_reg_read(client, AVG_READOUT, &reg);
	*value=(u32)reg;

	v4l2_dbg(2, debug, sd, "get luminance: %d\n", *value);
	return 0;
}

struct control_params {
	u32 id;
	s32 min;
	s32 max;
	union {
		s32 mask; // if is_menu
		u32 step; // else
	} s;
	s32 def;
	bool is_menu;
	bool is_volatile;
	bool is_read_only;
};

static const struct control_params ov5640_ctrl[] = {
	{
		.id = V4L2_CID_FOCUS_AUTO,
		.min = 0,
		.max = 1,
		.s.step = 1,
		.def = 1,
		.is_menu = false,
		.is_volatile = false,
		.is_read_only = false,
	},
	{
		.id = V4L2_CID_BRIGHTNESS,
		.min = 0,
		.max = 255,
		.s.step = 1,
		.def = 0,
		.is_menu = false,
		.is_volatile = false,
		.is_read_only = false,
	},
	{
		.id = V4L2_CID_CONTRAST,
		.min = 0,
		.max = 255,
		.s.step = 1,
		.def = 32,
		.is_menu = false,
		.is_volatile = true,
		.is_read_only = false,
	},
	{
		.id = V4L2_CID_SATURATION,
		.min = 0,
		.max = 255,
		.s.step = 1,
		.def = 64,
		.is_menu = false,
		.is_volatile = false,
		.is_read_only = false,
	},
	{
		.id = V4L2_CID_HUE,
		.min = -180,
		.max = 180,
		.s.step = 1,
		.def = 0,
		.is_menu = false,
		.is_volatile = false,
		.is_read_only = false,
	},
	{
		.id = V4L2_CID_VFLIP,
		.min = 0,
		.max = 1,
		.s.step = 1,
		.def = 0,
		.is_menu = false,
		.is_volatile = false,
		.is_read_only = false,
	},
	{
		.id = V4L2_CID_HFLIP,
		.min = 0,
		.max = 1,
		.s.step = 1,
		.def = 0,
		.is_menu = false,
		.is_volatile = false,
		.is_read_only = false,
	},
	{
		.id = V4L2_CID_EXPOSURE_AUTO,
		.max = V4L2_EXPOSURE_MANUAL,
		.s.mask = 0,
		.def = V4L2_EXPOSURE_AUTO,
		.is_menu = true,
		.is_volatile = false,
		.is_read_only = false,
	},
	{
		.id = V4L2_CID_AUTO_WHITE_BALANCE,
		.min = 0,
		.max = 1,
		.s.step = 1,
		.def = 1,
		.is_menu = false,
		.is_volatile = false,
		.is_read_only = false,
	},
	{
		.id = V4L2_CID_AUTOGAIN,
		.min = 0,
		.max = 1,
		.s.step = 1,
		.def = 1,
		.is_menu = false,
		.is_volatile = false,
		.is_read_only = false,
	},
	{
		.id = V4L2_CID_GAIN,
		.min = 0,
		.max = 1024,
		.s.step = 1,
		.def = 32,
		.is_menu = false,
		.is_volatile = true,
		.is_read_only = false,
	},
	{
		.id = V4L2_CID_EXPOSURE,
		.min = 0,
		.max = 65535,
		.s.step = 1,
		.def = 32,
		.is_menu = false,
		.is_volatile = true,
		.is_read_only = false,
	},
	{
		.id = V4L2_CID_GREEN_BALANCE,
		.min = 0,
		.max = 4096,
		.s.step = 1,
		.def = 2048,
		.is_menu = false,
		.is_volatile = true,
		.is_read_only = true,
	},
	{
		.id = V4L2_CID_BLUE_BALANCE,
		.min = 0,
		.max = 4096,
		.s.step = 1,
		.def = 2048,
		.is_menu = false,
		.is_volatile = true,
		.is_read_only = true,
	},
	{
		.id = V4L2_CID_RED_BALANCE,
		.min = 0,
		.max = 4096,
		.s.step = 1,
		.def = 2048,
		.is_menu = false,
		.is_volatile = true,
		.is_read_only = true,
	},
	{
		.id = V4L2_CID_BG_COLOR,
		.min = 0,
		.max = 0xffffff,
		.s.step = 1,
		.def = 0,
		.is_menu = false,
		.is_volatile = true,
		.is_read_only = true,
	},
};

static int ov5640_s_ctrl(struct v4l2_ctrl *ctrl)
{
	int ret;
	struct ov5640 *ov5640 = container_of(ctrl->handler,
			struct ov5640, ctrl_handler);
	struct v4l2_subdev *sd = &ov5640->subdev;

	v4l2_dbg(1, debug, sd, " %s id: 0x%x val: 0x%x\n", __func__,
			ctrl->id, ctrl->val);

	switch (ctrl->id) {
		case V4L2_CID_BRIGHTNESS:
			return ov5640_s_brightness(sd, ctrl->val);
		case V4L2_CID_CONTRAST:
			return ov5640_s_contrast(sd, ctrl->val);
		case V4L2_CID_SATURATION:
			return ov5640_s_saturation(sd, ctrl->val);
		case V4L2_CID_HUE:
			return ov5640_s_hue(sd, ctrl->val);
		case V4L2_CID_VFLIP:
			return ov5640_s_vflip(sd, ctrl->val);
		case V4L2_CID_HFLIP:
			return ov5640_s_hflip(sd, ctrl->val);
		case V4L2_CID_FOCUS_AUTO:
			return ov5640_s_auto_focus(sd, ctrl->val);
		case V4L2_CID_EXPOSURE_AUTO:
			ret = ov5640_s_auto_exposure(sd, ov5640->auto_exposure->val);
			if (!ret && ctrl->val == V4L2_EXPOSURE_MANUAL)
				/* set exposure only in manual mode */
				ret = ov5640_s_exposure(sd, ov5640->exposure->val);
			return ret;
		case V4L2_CID_AUTO_WHITE_BALANCE:
			return ov5640_s_auto_white_balance(sd, ctrl->val);
		case V4L2_CID_AUTOGAIN:
			return ov5640_s_auto_gain(sd, ctrl->val);
		case V4L2_CID_GAIN:
			return ov5640_s_gain(sd, ctrl->val);
	}
	return -EINVAL;
}

static int ov5640_g_ctrl(struct v4l2_ctrl *ctrl)
{
	struct ov5640 *ov5640 = container_of(ctrl->handler,
			struct ov5640, ctrl_handler);
	struct v4l2_subdev *sd = &ov5640->subdev;

	v4l2_dbg(1, debug, sd, " %s id: 0x%x\n", __func__, ctrl->id);

	switch (ctrl->id) {
		case V4L2_CID_BRIGHTNESS:
			return ov5640_g_brightness(sd, &ctrl->val);
		case V4L2_CID_CONTRAST:
			return ov5640_g_contrast(sd, &ctrl->val);
		case V4L2_CID_SATURATION:
			return ov5640_g_saturation(sd, &ctrl->val);
		case V4L2_CID_HUE:
			return ov5640_g_hue(sd, &ctrl->val);
		case V4L2_CID_VFLIP:
			return ov5640_g_vflip(sd, &ctrl->val);
		case V4L2_CID_HFLIP:
			return ov5640_g_hflip(sd, &ctrl->val);
		case V4L2_CID_FOCUS_AUTO:
			return ov5640_g_auto_focus(sd, &ctrl->val);
		case V4L2_CID_EXPOSURE_AUTO:
			/* do not update auto exposure value since it is managed
			   by the framework which makes assumption on its state
			   while changing from auto to manual.
			   see update_from_auto_cluster. */
			return ov5640_g_exposure(sd, &ov5640->exposure->val);
		case V4L2_CID_AUTO_WHITE_BALANCE:
			return ov5640_g_auto_white_balance(sd, &ctrl->val);
		case V4L2_CID_AUTOGAIN:
			return ov5640_g_auto_gain(sd, &ctrl->val);
		case V4L2_CID_GAIN:
			return ov5640_g_gain(sd, &ctrl->val);
		case V4L2_CID_BG_COLOR:
			return ov5640_g_luminance(sd, &ctrl->val);
		case V4L2_CID_GREEN_BALANCE:
			return ov5640_g_green_balance(sd, &ctrl->val);
		case V4L2_CID_RED_BALANCE:
			return ov5640_g_red_balance(sd, &ctrl->val);
		case V4L2_CID_BLUE_BALANCE:
			return ov5640_g_blue_balance(sd, &ctrl->val);
	}
	return -EINVAL;
}

static int ov5640_s_power(struct v4l2_subdev *sd, int enable)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	int ret = 0;

	if (enable) {
		v4l2_dbg(1, debug, sd, "Power up SOC...");

		ret = ov5640_reg_clr(client, SYSTEM_CTRL_0, 0x40);
		if (ret)
			goto out;

		// activate AF if needed since powerdown release focus
		if (to_ov5640(sd)->auto_focus_enabled) {
			// Then set ov5640 to continuous focus mode
			v4l2_dbg(1, debug, sd, "Enable auto focus...\n");
			ret = ov5640_reg_write(client, AF_CTRL, AF_CONTINUE);
			if (ret) {
				v4l2_err(sd, "Failed to set continuous focus mode\n");
				goto out;
			}
			v4l2_dbg(1, debug, sd, "Enable auto focus...done\n");
		}
	} else {
		u8 tmpreg = 0;
		v4l2_dbg(1, debug, sd, "Power down SOC...");

		ret = ov5640_reg_read(client, SYSTEM_CTRL_0, &tmpreg);
		if (ret)
			goto out;

		ret = ov5640_reg_write(client, SYSTEM_CTRL_0, tmpreg | 0x40);
		if (ret)
			goto out;
	}

out:
	return ret;
}

static int ov5640_init(struct v4l2_subdev *sd, u32 val)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	int ret = 0;
	u8 revision = 0;

	to_ov5640(sd)->angle = 0;
	to_ov5640(sd)->auto_focus_enabled = true; // enable auto focus by default

	ret = ov5640_reg_read(client, CHIP_REVISION, &revision);
	if (ret) {
		dev_info(&client->dev, "OV5640 device not detected\n");
		goto nodev;
	}

	revision &= 0xF;

	dev_info(&client->dev, "Detected a OV5640 chip, revision %x\n",
			revision);

	ret = ov5640_reg_write(client, SCCB_SYSTEM_CTRL_1, 0x11);
	if (ret)
		goto out;

	/* SW Reset */
	v4l2_info(sd, "Reset device...\n");
	ret = ov5640_reg_set(client, SYSTEM_CTRL_0, 0x80);
	if (ret) {
		v4l2_err(sd, "Failed to set the soft reset\n");
		goto out;
	}
	msleep(5); // 5ms in the init doc
	ret = ov5640_reg_clr(client, SYSTEM_CTRL_0, 0x80);
	if (ret) {
		v4l2_err(sd, "Failed to stop the soft reset\n");
		goto out;
	}
	v4l2_info(sd, "Reset device...done\n");

	/* SW Powerup */
	v4l2_info(sd, "Power up device...\n");
	ret = ov5640_s_power(sd, 1);
	if (ret) {
		v4l2_err(sd, "Failed to power up the device\n");
		goto out;
	}
	v4l2_info(sd, "Power up device...done\n");

	v4l2_info(sd, "Set default configuration...\n");
	ret = ov5640_reg_writes(client, configscript_common1,
			ARRAY_SIZE(configscript_common1));
	if (ret) {
		v4l2_err(sd, "Failed to set default configuration 1\n");
		goto out;
	}
	ret = ov5640_reg_writes(client, configscript_common2,
			ARRAY_SIZE(configscript_common2));
	if (ret) {
		v4l2_err(sd, "Failed to set default configuration 2\n");
		goto out;
	}
	v4l2_info(sd, "Set default configuration...done\n");

out:
	v4l2_info(sd, "Power down device...\n");
	ret = ov5640_s_power(sd, 0);
	if (ret) {
		v4l2_err(sd, "Failed to power down the device\n");
	} else {
		v4l2_info(sd, "Power down...done\n");
	}

	v4l2_dbg(1, debug, sd, "Init chip...done");
 nodev:
	return ret;
}

static int ov5640_load_firmware(struct v4l2_subdev *sd)
{
	int ret = 0;
	int timeout = 1000;
	u8 status = 0;
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	v4l2_info(sd, "Write auto focus firmware...\n");
	ret = ov5640_reg_write_burst(client, af_firmware, ARRAY_SIZE(af_firmware));
	if (ret) {
		v4l2_err(sd, "Failed to load auto-focus firmware\n");
		return ret;
	}
	else
	{
		v4l2_info(sd, "Write auto focus firmware...DONE\n");
		// wait until the autofocus status is idle
		status = 0;
		while (status != AF_STATUS_IDLE && timeout >= 0) {
			v4l2_dbg(2, debug, sd, "Read auto focus status...\n");
			ov5640_reg_read(client, AF_STATUS, &status);
			v4l2_dbg(2, debug, sd, "Status: %x\n", status);
			msleep(1);
			timeout--;
		}

		if (timeout < 0)
			v4l2_err(sd, "Autofocus setup failed: timeout (0x%2x)\n",
					status);
	}

	return ret;
}

static const struct v4l2_ctrl_ops ov5640_ctrl_ops = {
	.s_ctrl = ov5640_s_ctrl,
	.g_volatile_ctrl = ov5640_g_ctrl,
};

static struct v4l2_ctrl* ov5640_register_ctrl(struct v4l2_subdev *sd,
		struct v4l2_ctrl_handler *hl, const struct control_params* std) {
	struct v4l2_ctrl *ctrl;

	if (std->is_menu) {
		ctrl = v4l2_ctrl_new_std_menu(hl, &ov5640_ctrl_ops,
				std->id, std->max, std->s.mask, std->def);
	} else {
		ctrl = v4l2_ctrl_new_std(hl, &ov5640_ctrl_ops, std->id, std->min,
				std->max, std->s.step, std->def);
	}
	if (ctrl == NULL) {
		int err = hl->error;
		v4l2_warn(sd, "std id 0x%x error: %x\n", std->id, err);
		return NULL;
	}
	if (std->is_volatile) {
		ctrl->flags |= V4L2_CTRL_FLAG_VOLATILE;

		/*If ctrl is writable, set flag to update volatile parameter on write */
		if (!std->is_read_only) {
			ctrl->flags |= V4L2_CTRL_FLAG_EXECUTE_ON_WRITE;
		}
	}
	if (std->is_read_only) {
		ctrl->flags |= V4L2_CTRL_FLAG_READ_ONLY;
	}
	return ctrl;
}


static int ov5640_register_controls(struct v4l2_subdev *sd, struct v4l2_ctrl_handler *hl)
{
	int i;
	struct v4l2_ctrl *ctrl;
	struct ov5640 *ov5640 = to_ov5640(sd);
	for (i = 0; i < ARRAY_SIZE(ov5640_ctrl); i++) {
		ctrl = ov5640_register_ctrl(sd, hl, &ov5640_ctrl[i]);
		if (ctrl == NULL) {
			v4l2_dbg(1, debug, sd, "fail to register control %d\n", i);
		}
		/* save controls for exposure cluster */
		if (ov5640_ctrl[i].id == V4L2_CID_EXPOSURE_AUTO) {
			ov5640->auto_exposure = ctrl;
		} else if (ov5640_ctrl[i].id == V4L2_CID_EXPOSURE) {
			ov5640->exposure = ctrl;
		}
	}
	if (hl->error) {
		int err = hl->error;
		v4l2_warn(sd, "ctrl_handler error: %x\n", err);
		return err;
	}
	v4l2_ctrl_auto_cluster(2, &ov5640->auto_exposure,
			      V4L2_EXPOSURE_MANUAL, true);
	return 0;
}

static int ov5640_config_stream(struct v4l2_subdev *sd) {
	struct ov5640 *ov5640 = to_ov5640(sd);
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	int ret = 0;

	const struct ov5640_format *curr_fmt = ov5640->curr_fmt;
	const struct ov5640_mode_info *curr_size = ov5640->curr_size;

	v4l2_dbg(1, debug, sd, "%s size: %dx%d, code: %d, color: %d\n", __func__,
			ov5640->curr_size->width, ov5640->curr_size->height,
			ov5640->curr_fmt->code, ov5640->curr_fmt->colorspace);

	if (curr_fmt == NULL || curr_size == NULL) {
		v4l2_dbg(1, debug, sd, "%s fmt: 0x%p, size: 0x%p\n", __func__, curr_fmt, curr_size);
		return -EFAULT;
	}

	ret = ov5640_reg_write(client, FORMAT_CONTROL_0, curr_fmt->fmtreg);
	if (ret)
		return ret;

	ret = ov5640_reg_write(client, FORMAT_MUX_CONTROL, curr_fmt->fmtmuxreg);
	if (ret)
		return ret;

	ret = ov5640_config_timing(sd);
	if (ret)
		return ret;

	ret = ov5640_reg_writes(client, curr_size->init_data_ptr, curr_size->init_data_size);
	if (ret)
		return ret;

	if ((curr_size->mode == ov5640_mode_QVGA_320_240) ||
			(curr_size->mode == ov5640_mode_VGA_640_480)) {
		ret = ov5640_reg_write(client, ISP_CONTROL_1, 0xa3);
	} else {
		ret = ov5640_reg_write(client, ISP_CONTROL_1, 0x83);
	}
	return ret;
}

static int ov5640_s_stream(struct v4l2_subdev *sd, int enable) {
	if (enable) {
		v4l2_dbg(1, debug, sd, "Enable stream...");
	} else {
		v4l2_dbg(1, debug, sd, "Disable stream...");
	}
	return 0;
}

static int ov5640_get_fmt(struct v4l2_subdev *sd,
	       struct v4l2_subdev_pad_config *cfg,
	       struct v4l2_subdev_format *format)
{
	struct v4l2_mbus_framefmt *mf = &format->format;
	struct ov5640 *ov5640 = to_ov5640(sd);

	if (format->pad)
		return -EINVAL;

	mf->width = ov5640->curr_size->width;
	mf->height = ov5640->curr_size->width;
	mf->code = ov5640->curr_fmt->code;
	mf->field = V4L2_FIELD_NONE;
	mf->colorspace = ov5640->curr_fmt->colorspace;

	return 0;
}

static int ov5640_set_fmt(struct v4l2_subdev *sd,
		struct v4l2_subdev_pad_config *cfg,
		struct v4l2_subdev_format *format)
{
	struct v4l2_mbus_framefmt *mf = &format->format;
	struct ov5640 *ov5640 = to_ov5640(sd);
	int ret = 0;

	if (format->pad)
		return -EINVAL;

	if (format->which == V4L2_SUBDEV_FORMAT_TRY)
	{
		try_fmt(sd, mf);
		try_size(sd, mf);
		cfg->try_fmt = *mf;
		return 0;
	}
	ov5640->curr_fmt = try_fmt(sd, mf);
	ov5640->curr_size = try_size(sd, mf);

	ret = ov5640_config_stream(sd);
	if (ret < 0) {
		v4l2_warn(sd, "%s: ov5640_config_stream fail: %d\n",
				__func__, ret);
		return ret;
	}
	return 0;
}

static int ov5640_enum_mbus_code(struct v4l2_subdev *sd,
		struct v4l2_subdev_pad_config *cfg,
		struct v4l2_subdev_mbus_code_enum *code)
{
	if (code->pad || code->index >= ARRAY_SIZE(ov5640_formats))
		return -EINVAL;

	code->code = ov5640_formats[code->index].code;
	return 0;
}

static struct v4l2_subdev_core_ops ov5640_subdev_core_ops = {
	.queryctrl = v4l2_subdev_queryctrl,
	.querymenu = v4l2_subdev_querymenu,
	.g_ctrl = v4l2_subdev_g_ctrl,
	.s_ctrl = v4l2_subdev_s_ctrl,
	.g_ext_ctrls = v4l2_subdev_g_ext_ctrls,
	.try_ext_ctrls = v4l2_subdev_try_ext_ctrls,
	.s_ext_ctrls = v4l2_subdev_s_ext_ctrls,
	.init		= ov5640_init,
	.s_power	= ov5640_s_power,
#ifdef CONFIG_VIDEO_ADV_DEBUG
	.g_register = ov5640_g_register,
	.s_register = ov5640_s_register,
#endif
};

static struct v4l2_subdev_video_ops ov5640_subdev_video_ops = {
	.s_stream	= ov5640_s_stream,
};

static struct v4l2_subdev_pad_ops ov5640_subdev_pad_ops = {
	.enum_mbus_code = ov5640_enum_mbus_code,
	.get_fmt	= ov5640_get_fmt,
	.set_fmt	= ov5640_set_fmt,
};

static struct v4l2_subdev_ops ov5640_subdev_ops = {
	.core	= &ov5640_subdev_core_ops,
	.video	= &ov5640_subdev_video_ops,
	.pad    = &ov5640_subdev_pad_ops,
};

static int ov5640_probe(struct i2c_client *client,
		const struct i2c_device_id *did)
{
	struct ov5640 *ov5640;
	struct v4l2_subdev *sd;
	int ret;

	ov5640 = kzalloc(sizeof(*ov5640), GFP_KERNEL);
	if (!ov5640) {
		v4l2_err(client, "Failed to allocate memory for private data!\n");
		return -ENOMEM;
	}

	ov5640->curr_size = &ov5640_mode_info_data[ov5640_mode_VGA_640_480];
	ov5640->curr_fmt = &ov5640_formats[ov5640_format_YUYV];

	v4l2_i2c_subdev_init(&ov5640->subdev, client, &ov5640_subdev_ops);
	sd = &ov5640->subdev;

	ret = ov5640_init(sd,0);
	if (ret<0) {
		kfree(ov5640);
		return -ENODEV;
	}

	ret = ov5640_config_stream(sd);
	if (ret < 0) {
		v4l2_warn(sd, "%s: ov5640_config_stream fail: %d\n",
				__func__, ret);
		return ret;
	}

	ret = ov5640_s_power(sd, 1);
	if (ret < 0) {
		v4l2_err(sd, "fail to power up soc during probe\n");
		kfree(ov5640);
		return ret;
	}

	ret = ov5640_load_firmware(sd);
	if (ret < 0) {
		v4l2_err(sd, "fail to load autofocus firmware\n");
		kfree(ov5640);
		return ret;
	}

	ret = ov5640_s_power(sd, 0);
	if (ret < 0) {
		v4l2_err(sd, "fail to power down soc during probe\n");
		kfree(ov5640);
		return ret;
	}

	ret = v4l2_ctrl_handler_init(&ov5640->ctrl_handler, ARRAY_SIZE(ov5640_ctrl));
	if (ret) {
		v4l2_err(sd, "fail to init ctrl handler for V4L2 sub device\n");
		kfree(ov5640);
		return -ENODEV;
	}
	ret = ov5640_register_controls(sd, &ov5640->ctrl_handler);
	if (ret) {
		v4l2_err(sd, "fail to add standard controls for V4L2 sub device\n");
		v4l2_ctrl_handler_free(&ov5640->ctrl_handler);
		kfree(ov5640);
		return ret;
	}
	v4l2_ctrl_handler_setup(&ov5640->ctrl_handler);
	if (ov5640->ctrl_handler.error) {
		int err = ov5640->ctrl_handler.error;
		v4l2_err(sd, "fail to setup control handler for V4L2 sub device\n");
		v4l2_ctrl_handler_free(&ov5640->ctrl_handler);
		kfree(ov5640);
		return err;
	}
	ov5640->subdev.ctrl_handler = &ov5640->ctrl_handler;

	v4l2_dbg(1, debug, sd, "%s find device\n", __func__);
	return 0;
}

static int ov5640_remove(struct i2c_client *client)
{
	struct v4l2_subdev *subdev = i2c_get_clientdata(client);
	struct ov5640 *ov5640 = to_ov5640(subdev);

	struct v4l2_ctrl_handler *ctrl_handler = subdev->ctrl_handler;
	v4l2_device_unregister_subdev(subdev);
	v4l2_ctrl_handler_free(ctrl_handler);

	kfree(ov5640);
	return 0;
}

static const struct i2c_device_id ov5640_id[] = {
	{ "ov5640", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, ov5640_id);

static struct i2c_driver ov5640_i2c_driver = {
	.driver = {
		.name = "ov5640",
	},
	.probe		= ov5640_probe,
	.remove		= ov5640_remove,
	.id_table	= ov5640_id,
};

static int __init ov5640_mod_init(void)
{
	return i2c_add_driver(&ov5640_i2c_driver);
}

static void __exit ov5640_mod_exit(void)
{
	i2c_del_driver(&ov5640_i2c_driver);
}

module_init(ov5640_mod_init);
module_exit(ov5640_mod_exit);

MODULE_AUTHOR("Aldebaran Robotics");
MODULE_DESCRIPTION("OmniVision OV5640 Camera driver");
MODULE_LICENSE("GPL v2");
MODULE_VERSION("1.1");
