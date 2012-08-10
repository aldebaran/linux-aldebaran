/*
 * OmniVision OV5640 sensor driver
 *
 * Copyright (C) 2011 Texas Instruments Incorporated - http://www.ti.com/
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

#include <media/v4l2-chip-ident.h>
#include <media/v4l2-device.h>
#include <media/ov5640.h>

/* OV5640 has only one fixed colorspace per pixelcode */
struct ov5640_datafmt {
	enum v4l2_mbus_pixelcode	code;
	enum v4l2_colorspace		colorspace;
};

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

struct ov5640_clk_cfg {
	u8 sc_pll_prediv;
	u8 sc_pll_rdiv;
	u8 sc_pll_mult;
	u8 sysclk_div;
	u8 mipi_div;
};

enum ov5640_size {
	OV5640_SIZE_QVGA,
	OV5640_SIZE_VGA,
	OV5640_SIZE_720P,
	OV5640_SIZE_1080P,
	OV5640_SIZE_5MP,
	OV5640_SIZE_LAST,
};

static const struct v4l2_frmsize_discrete ov5640_frmsizes[OV5640_SIZE_LAST] = {
	{  320,  240 },
	{  640,  480 },
	{ 1280,  720 },
	{ 1920, 1080 },
	{ 2592, 1944 },
};

/* Find a frame size in an array */
static int ov5640_find_framesize(u32 width, u32 height)
{
	int i;

	for (i = 0; i < OV5640_SIZE_LAST; i++) {
		if ((ov5640_frmsizes[i].width >= width) &&
		    (ov5640_frmsizes[i].height >= height))
			break;
	}

	/* If not found, select biggest */
	if (i >= OV5640_SIZE_LAST)
		i = OV5640_SIZE_LAST - 1;

	return i;
}

struct ov5640 {
	struct v4l2_subdev subdev;
	struct v4l2_mbus_framefmt format;

#if 0 // TODO: handle platform data
	const struct ov5640_platform_data *pdata;
#endif
	struct v4l2_ctrl *pixel_rate;

	/* HW control */
	struct clk *xvclk;
	struct regulator *avdd;
	struct regulator *dovdd;

	/* System Clock config */
	struct ov5640_clk_cfg clk_cfg;
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

/* TODO: Divide this properly */
static const struct ov5640_reg configscript_common1[] = {
	{ 0x3103, 0x03 },
	{ 0x3017, 0x00 },
	{ 0x3018, 0x00 },
	{ 0x3630, 0x2e },
	{ 0x3632, 0xe2 },
	{ 0x3633, 0x23 },
	{ 0x3634, 0x44 },
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
	{ 0x3600, 0x04 },
	{ 0x3601, 0x22 },
	{ 0x471c, 0x50 },
	{ 0x3002, 0x1c },
	{ 0x3006, 0xc3 },
	{ 0x300e, 0x05 },
	{ 0x302e, 0x08 },
	{ 0x3612, 0x4b },
	{ 0x3618, 0x04 },
	{ 0x3034, 0x18 },
	{ 0x3035, 0x11 },
	{ 0x3036, 0x54 },
	{ 0x3037, 0x13 },
	{ 0x3708, 0x21 },
	{ 0x3709, 0x12 },
	{ 0x370c, 0x00 },
};

/* TODO: Divide this properly */
static const struct ov5640_reg configscript_common2[] = {
	{ 0x3a02, 0x01 },
	{ 0x3a03, 0xec },
	{ 0x3a08, 0x01 },
	{ 0x3a09, 0x27 },
	{ 0x3a0a, 0x00 },
	{ 0x3a0b, 0xf6 },
	{ 0x3a0e, 0x06 },
	{ 0x3a0d, 0x08 },
	{ 0x3a14, 0x01 },
	{ 0x3a15, 0xec },
	{ 0x4001, 0x02 },
	{ 0x4004, 0x06 },
	{ 0x460b, 0x37 },
	{ 0x4750, 0x00 },
	{ 0x4751, 0x00 },
	{ 0x4800, 0x24 },
	{ 0x5a00, 0x08 },
	{ 0x5a21, 0x00 },
	{ 0x5a24, 0x00 },
	{ 0x5000, 0x27 },
	{ 0x5001, 0x87 },
	{ 0x3820, 0x40 },
	{ 0x3821, 0x06 },
	{ 0x3824, 0x01 },
	{ 0x5481, 0x08 },
	{ 0x5482, 0x14 },
	{ 0x5483, 0x28 },
	{ 0x5484, 0x51 },
	{ 0x5485, 0x65 },
	{ 0x5486, 0x71 },
	{ 0x5487, 0x7d },
	{ 0x5488, 0x87 },
	{ 0x5489, 0x91 },
	{ 0x548a, 0x9a },
	{ 0x548b, 0xaa },
	{ 0x548c, 0xb8 },
	{ 0x548d, 0xcd },
	{ 0x548e, 0xdd },
	{ 0x548f, 0xea },
	{ 0x5490, 0x1d },
	{ 0x5381, 0x20 },
	{ 0x5382, 0x64 },
	{ 0x5383, 0x08 },
	{ 0x5384, 0x20 },
	{ 0x5385, 0x80 },
	{ 0x5386, 0xa0 },
	{ 0x5387, 0xa2 },
	{ 0x5388, 0xa0 },
	{ 0x5389, 0x02 },
	{ 0x538a, 0x01 },
	{ 0x538b, 0x98 },
	{ 0x5300, 0x08 },
	{ 0x5301, 0x30 },
	{ 0x5302, 0x10 },
	{ 0x5303, 0x00 },
	{ 0x5304, 0x08 },
	{ 0x5305, 0x30 },
	{ 0x5306, 0x08 },
	{ 0x5307, 0x16 },
	{ 0x5580, 0x00 },
	{ 0x5587, 0x00 },
	{ 0x5588, 0x00 },
	{ 0x5583, 0x40 },
	{ 0x5584, 0x10 },
	{ 0x5589, 0x10 },
	{ 0x558a, 0x00 },
	{ 0x558b, 0xf8 },
	{ 0x3a0f, 0x36 },
	{ 0x3a10, 0x2e },
	{ 0x3a1b, 0x38 },
	{ 0x3a1e, 0x2c },
	{ 0x3a11, 0x70 },
	{ 0x3a1f, 0x18 },
	{ 0x3a18, 0x00 },
	{ 0x3a19, 0xf8 },
	{ 0x3003, 0x03 },
	{ 0x3003, 0x01 },
};

static const struct ov5640_timing_cfg timing_cfg[OV5640_SIZE_LAST] = {
	[OV5640_SIZE_QVGA] = {
		.x_addr_start = 0,
		.y_addr_start = 0,
		.x_addr_end = 2623,
		.y_addr_end = 1951,
		.h_output_size = 320,
		.v_output_size = 240,
		.h_total_size = 2844,
		.v_total_size = 1968,
		.isp_h_offset = 16,
		.isp_v_offset = 6,
		.h_odd_ss_inc = 1,
		.h_even_ss_inc = 1,
		.v_odd_ss_inc = 1,
		.v_even_ss_inc = 1,
	},
	[OV5640_SIZE_VGA] = {
		.x_addr_start = 0,
		.y_addr_start = 0,
		.x_addr_end = 2623,
		.y_addr_end = 1951,
		.h_output_size = 640,
		.v_output_size = 480,
		.h_total_size = 2844,
		.v_total_size = 1968,
		.isp_h_offset = 16,
		.isp_v_offset = 6,
		.h_odd_ss_inc = 1,
		.h_even_ss_inc = 1,
		.v_odd_ss_inc = 1,
		.v_even_ss_inc = 1,
	},
	[OV5640_SIZE_720P] = {
		.x_addr_start = 336,
		.y_addr_start = 434,
		.x_addr_end = 2287,
		.y_addr_end = 1522,
		.h_output_size = 1280,
		.v_output_size = 720,
		.h_total_size = 2500,
		.v_total_size = 1120,
		.isp_h_offset = 16,
		.isp_v_offset = 4,
		.h_odd_ss_inc = 1,
		.h_even_ss_inc = 1,
		.v_odd_ss_inc = 1,
		.v_even_ss_inc = 1,
	},
	[OV5640_SIZE_1080P] = {
		.x_addr_start = 336,
		.y_addr_start = 434,
		.x_addr_end = 2287,
		.y_addr_end = 1522,
		.h_output_size = 1920,
		.v_output_size = 1080,
		.h_total_size = 2500,
		.v_total_size = 1120,
		.isp_h_offset = 16,
		.isp_v_offset = 4,
		.h_odd_ss_inc = 1,
		.h_even_ss_inc = 1,
		.v_odd_ss_inc = 1,
		.v_even_ss_inc = 1,
	},
	[OV5640_SIZE_5MP] = {
		.x_addr_start = 0,
		.y_addr_start = 0,
		.x_addr_end = 2623,
		.y_addr_end = 1951,
		.h_output_size = 2592,
		.v_output_size = 1944,
		.h_total_size = 2844,
		.v_total_size = 1968,
		.isp_h_offset = 16,
		.isp_v_offset = 6,
		.h_odd_ss_inc = 1,
		.h_even_ss_inc = 1,
		.v_odd_ss_inc = 1,
		.v_even_ss_inc = 1,
	},
};

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
	dev_err(&client->dev, "Failed reading register 0x%02x!\n", reg);
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
		dev_err(&client->dev, "Failed writing register 0x%02x!\n", reg);
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

static unsigned long ov5640_get_pclk(struct v4l2_subdev *sd)
{
	struct ov5640 *ov5640 = to_ov5640(sd);
	unsigned long xvclk, vco, mipi_pclk;

	xvclk = clk_get_rate(ov5640->xvclk);

	vco = (xvclk / ov5640->clk_cfg.sc_pll_prediv) *
		ov5640->clk_cfg.sc_pll_mult;

	mipi_pclk = vco /
		ov5640->clk_cfg.sysclk_div /
		ov5640->clk_cfg.mipi_div;

	return mipi_pclk;
}

static int ov5640_config_timing(struct v4l2_subdev *sd)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct ov5640 *ov5640 = to_ov5640(sd);
	int ret, i;

	i = ov5640_find_framesize(ov5640->format.width, ov5640->format.height);

	ret = ov5640_reg_write(client,
			0x3800,
			(timing_cfg[i].x_addr_start & 0xFF00) >> 8);
	if (ret)
		return ret;

	ret = ov5640_reg_write(client,
			0x3801,
			timing_cfg[i].x_addr_start & 0xFF);
	if (ret)
		return ret;

	ret = ov5640_reg_write(client,
			0x3802,
			(timing_cfg[i].y_addr_start & 0xFF00) >> 8);
	if (ret)
		return ret;

	ret = ov5640_reg_write(client,
			0x3803,
			timing_cfg[i].y_addr_start & 0xFF);
	if (ret)
		return ret;

	ret = ov5640_reg_write(client,
			0x3804,
			(timing_cfg[i].x_addr_end & 0xFF00) >> 8);
	if (ret)
		return ret;

	ret = ov5640_reg_write(client,
			0x3805,
			timing_cfg[i].x_addr_end & 0xFF);
	if (ret)
		return ret;

	ret = ov5640_reg_write(client,
			0x3806,
			(timing_cfg[i].y_addr_end & 0xFF00) >> 8);
	if (ret)
		return ret;

	ret = ov5640_reg_write(client,
			0x3807,
			timing_cfg[i].y_addr_end & 0xFF);
	if (ret)
		return ret;

	ret = ov5640_reg_write(client,
			0x3808,
			(timing_cfg[i].h_output_size & 0xFF00) >> 8);
	if (ret)
		return ret;

	ret = ov5640_reg_write(client,
			0x3809,
			timing_cfg[i].h_output_size & 0xFF);
	if (ret)
		return ret;

	ret = ov5640_reg_write(client,
			0x380A,
			(timing_cfg[i].v_output_size & 0xFF00) >> 8);
	if (ret)
		return ret;

	ret = ov5640_reg_write(client,
			0x380B,
			timing_cfg[i].v_output_size & 0xFF);
	if (ret)
		return ret;

	ret = ov5640_reg_write(client,
			0x380C,
			(timing_cfg[i].h_total_size & 0xFF00) >> 8);
	if (ret)
		return ret;

	ret = ov5640_reg_write(client,
			0x380D,
			timing_cfg[i].h_total_size & 0xFF);
	if (ret)
		return ret;

	ret = ov5640_reg_write(client,
			0x380E,
			(timing_cfg[i].v_total_size & 0xFF00) >> 8);
	if (ret)
		return ret;

	ret = ov5640_reg_write(client,
			0x380F,
			timing_cfg[i].v_total_size & 0xFF);
	if (ret)
		return ret;

	ret = ov5640_reg_write(client,
			0x3810,
			(timing_cfg[i].isp_h_offset & 0xFF00) >> 8);
	if (ret)
		return ret;

	ret = ov5640_reg_write(client,
			0x3811,
			timing_cfg[i].isp_h_offset & 0xFF);
	if (ret)
		return ret;

	ret = ov5640_reg_write(client,
			0x3812,
			(timing_cfg[i].isp_v_offset & 0xFF00) >> 8);
	if (ret)
		return ret;

	ret = ov5640_reg_write(client,
			0x3813,
			timing_cfg[i].isp_v_offset & 0xFF);
	if (ret)
		return ret;

	ret = ov5640_reg_write(client,
			0x3814,
			((timing_cfg[i].h_odd_ss_inc & 0xF) << 4) |
			(timing_cfg[i].h_even_ss_inc & 0xF));
	if (ret)
		return ret;

	ret = ov5640_reg_write(client,
			0x3815,
			((timing_cfg[i].v_odd_ss_inc & 0xF) << 4) |
			(timing_cfg[i].v_even_ss_inc & 0xF));

	return ret;
}

# if 0 // TODO remake ov5640_g_fmt
static struct v4l2_mbus_framefmt *
__ov5640_get_pad_format(struct ov5640 *ov5640, struct v4l2_subdev_fh *fh,
			 unsigned int pad, enum v4l2_subdev_format_whence which)
{
	switch (which) {
	case V4L2_SUBDEV_FORMAT_TRY:
		return v4l2_subdev_get_try_format(fh, pad);
	case V4L2_SUBDEV_FORMAT_ACTIVE:
		return &ov5640->format;
	default:
		return NULL;
	}
}
#endif
/* -----------------------------------------------------------------------------
 * V4L2 subdev internal operations
 */

static int ov5640_s_power(struct v4l2_subdev *sd, int on)
{
#if 0 //TODO handle power off/power on of chip
	struct ov5640 *ov5640 = to_ov5640(sd);
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct device *dev = &client->dev;


	if (on) {
		int ret;

		if (ov5640->pdata->pre_poweron) {
			ret = ov5640->pdata->pre_poweron(sd);
			if (ret) {
				dev_err(dev,
					"Error in pre_poweron (%d)\n", ret);
				return ret;
			}
		}

		if (ov5640->dovdd) {
			ret = regulator_enable(ov5640->dovdd);
			if (ret) {
				dev_err(dev,
					"Error in enabling DOVDD (%d)\n", ret);
				if (ov5640->pdata->post_poweroff)
					ov5640->pdata->post_poweroff(sd);
				return ret;
			}
		}

		if (ov5640->avdd) {
			ret = regulator_enable(ov5640->avdd);
			if (ret) {
				dev_err(dev,
					"Error in enabling AVDD (%d)\n", ret);
				if (ov5640->dovdd)
					regulator_disable(ov5640->dovdd);
				if (ov5640->pdata->post_poweroff)
					ov5640->pdata->post_poweroff(sd);
				return ret;
			}
			usleep_range(5000, 5000);
		}

		ret = clk_enable(ov5640->xvclk);
		if (ret) {
			dev_err(dev, "Error in enabling XVCLK (%d)\n", ret);
			if (ov5640->avdd)
				regulator_disable(ov5640->avdd);
			if (ov5640->dovdd)
				regulator_disable(ov5640->dovdd);
			if (ov5640->pdata->post_poweroff)
				ov5640->pdata->post_poweroff(sd);
			return ret;
		}
		if (gpio_is_valid(ov5640->pdata->gpio_pwdn)) {
			gpio_set_value(ov5640->pdata->gpio_pwdn,
				       ov5640->pdata->is_gpio_pwdn_acthi ?
				       1 : 0);
		}
		usleep_range(2000, 2000);
	} else {
		if (gpio_is_valid(ov5640->pdata->gpio_pwdn)) {
			gpio_set_value(ov5640->pdata->gpio_pwdn,
				       ov5640->pdata->is_gpio_pwdn_acthi ?
				       0 : 1);
		}
		clk_disable(ov5640->xvclk);
		if (ov5640->avdd)
			regulator_disable(ov5640->avdd);
		if (ov5640->dovdd)
			regulator_disable(ov5640->dovdd);
		if (ov5640->pdata->post_poweroff)
			ov5640->pdata->post_poweroff(sd);
	}
#endif
	return 0;
}


static int ov5640_g_fmt(struct v4l2_subdev *sd,
			struct v4l2_format *format)
{
#if 0 // TODO rewrite ov5640_g_fmt
	struct ov5640 *ov5640 = to_ov5640(sd);

	format->format = *__ov5640_get_pad_format(ov5640, fh, format->pad,
						   format->which);
#endif
	return 0;
}

static int ov5640_s_fmt(struct v4l2_subdev *sd,
			struct v4l2_format *format)
{
#if 0 // TODO rewrite ov5640_s_fmt
	struct ov5640 *ov5640 = to_ov5640(sd);
	struct v4l2_mbus_framefmt *__format;

	__format = __ov5640_get_pad_format(ov5640, fh, format->pad,
					    format->which);

	*__format = format->format;

	ov5640->pixel_rate->cur.val64 = ov5640_get_pclk(sd) / 16;
#endif
	return 0;
}

static int ov5640_enum_fmt(struct v4l2_subdev *subdev,
							struct v4l2_fmtdesc *fmt)
{
#if 0 // TODO rewrite ov5640_enum_fmt
	if (code->index >= 2)
		return -EINVAL;

	switch (code->index) {
	case 0:
		code->code = V4L2_MBUS_FMT_UYVY8_1X16;
		break;
	case 1:
		code->code = V4L2_MBUS_FMT_YUYV8_1X16;
		break;
	}
#endif
	return 0;
}

static int ov5640_enum_framesizes(struct v4l2_subdev *subdev,
									struct v4l2_fmtdesc *fmt)
{
#if 0 // TODO rewrite ov5640_enum_framesizes
	if ((fse->index >= OV5640_SIZE_LAST) ||
	    (fse->code != V4L2_MBUS_FMT_UYVY8_1X16 &&
	     fse->code != V4L2_MBUS_FMT_YUYV8_1X16))
		return -EINVAL;

	fse->min_width = ov5640_frmsizes[fse->index].width;
	fse->max_width = fse->min_width;
	fse->min_height = ov5640_frmsizes[fse->index].height;
	fse->max_height = fse->min_height;
#endif
	return 0;
}

static int ov5640_s_stream(struct v4l2_subdev *sd, int enable)
{
	struct ov5640 *ov5640 = to_ov5640(sd);
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	int ret = 0;

	if (enable) {
		u8 fmtreg = 0, fmtmuxreg = 0;
		int i;

		switch ((u32)ov5640->format.code) {
		case V4L2_PIX_FMT_UYVY:
			fmtreg = 0x32;
			fmtmuxreg = 0;
			break;
		case V4L2_PIX_FMT_YUYV:
			fmtreg = 0x30;
			fmtmuxreg = 0;
			break;
		default:
			/* This shouldn't happen */
			ret = -EINVAL;
			return ret;
		}

		ret = ov5640_reg_write(client, 0x4300, fmtreg);
		if (ret)
			return ret;

		ret = ov5640_reg_write(client, 0x501F, fmtmuxreg);
		if (ret)
			return ret;

		ret = ov5640_config_timing(sd);
		if (ret)
			return ret;

		i = ov5640_find_framesize(ov5640->format.width, ov5640->format.height);
		if ((i == OV5640_SIZE_QVGA) ||
		    (i == OV5640_SIZE_VGA) ||
		    (i == OV5640_SIZE_720P)) {
			ret = ov5640_reg_write(client, 0x3108,
					(i == OV5640_SIZE_720P) ? 0x1 : 0);
			if (ret)
				return ret;
			ret = ov5640_reg_set(client, 0x5001, 0x20);
		} else {
			ret = ov5640_reg_clr(client, 0x5001, 0x20);
			if (ret)
				return ret;
			ret = ov5640_reg_write(client, 0x3108, 0x2);
		}

		ret = ov5640_reg_clr(client, 0x3008, 0x40);
		if (ret)
			goto out;
	} else {
		u8 tmpreg = 0;

		ret = ov5640_reg_read(client, 0x3008, &tmpreg);
		if (ret)
			goto out;

		ret = ov5640_reg_write(client, 0x3008, tmpreg | 0x40);
		if (ret)
			goto out;
	}

out:
	return ret;
}

static int ov5640_g_chip_ident(struct v4l2_subdev *sd,
    struct v4l2_dbg_chip_ident *chip)
{
  struct i2c_client *client = v4l2_get_subdevdata(sd);

  return v4l2_chip_ident_i2c_client(client, chip, V4L2_IDENT_OV5640, 0);
}

static struct v4l2_subdev_core_ops ov5640_subdev_core_ops = {
	.s_power	= ov5640_s_power,
	.g_chip_ident =ov5640_g_chip_ident,
};

static struct v4l2_subdev_video_ops ov5640_subdev_video_ops = {
	.s_stream	= ov5640_s_stream,
};

#if 0 // TODO put this function in sub_dev_core_ops
static struct v4l2_subdev_pad_ops ov5640_subdev_pad_ops = {
	.enum_mbus_code = ov5640_enum_fmt,
	.enum_frame_size = ov5640_enum_framesizes,
	.get_fmt = ov5640_g_fmt,
	.set_fmt = ov5640_s_fmt,
};

static int ov5640_g_skip_frames(struct v4l2_subdev *sd, u32 *frames)
{
	/* Quantity of initial bad frames to skip. Revisit. */
	*frames = 3;

	return 0;
}

static struct v4l2_subdev_sensor_ops ov5640_subdev_sensor_ops = {
	.g_skip_frames	= ov5640_g_skip_frames,
};
#endif


static struct v4l2_subdev_ops ov5640_subdev_ops = {
	.core	= &ov5640_subdev_core_ops,
	.video	= &ov5640_subdev_video_ops,
#if 0 // TODO only skip top line no skip frame in V4L2
	.sensor	= &ov5640_subdev_sensor_ops,
#endif
};

#if 0 // TODO no open/close/registered operation, no subdev_internal_ops
static int ov5640_registered(struct v4l2_subdev *subdev)
{
	struct i2c_client *client = v4l2_get_subdevdata(subdev);
	struct ov5640 *ov5640 = to_ov5640(subdev);

	int ret = 0;
	u8 revision = 0;

	ret = ov5640_s_power(subdev, 1);
	if (ret < 0) {
		dev_err(&client->dev, "OV5640 power up failed\n");
		return ret;
	}

	ret = ov5640_reg_read(client, 0x302A, &revision);
	if (ret) {
		dev_err(&client->dev, "Failure to detect OV5640 chip\n");
		goto out;
	}

	revision &= 0xF;

	dev_info(&client->dev, "Detected a OV5640 chip, revision %x\n",
		 revision);

	/* SW Reset */
	ret = ov5640_reg_set(client, 0x3008, 0x80);
	if (ret)
		goto out;

	msleep(2);

	ret = ov5640_reg_clr(client, 0x3008, 0x80);
	if (ret)
		goto out;

	/* SW Powerdown */
	ret = ov5640_reg_set(client, 0x3008, 0x40);
	if (ret)
		goto out;

	ret = ov5640_reg_writes(client, configscript_common1,
			ARRAY_SIZE(configscript_common1));
	if (ret)
		goto out;

	ret = ov5640_reg_writes(client, configscript_common2,
			ARRAY_SIZE(configscript_common2));
	if (ret)
		goto out;

	/* Init controls */
	ret = v4l2_ctrl_handler_init(&ov5640->ctrl_handler, 1);
	if (ret)
		goto out;

	ov5640->pixel_rate = v4l2_ctrl_new_std(
				&ov5640->ctrl_handler, NULL,
				V4L2_CID_PIXEL_RATE,
				0, 0, 1, 0);

	subdev->ctrl_handler = &ov5640->ctrl_handler;
out:
	ov5640_s_power(subdev, 0);

	return ret;
}

static int ov5640_open(struct v4l2_subdev *subdev, struct v4l2_subdev_fh *fh)
{
	struct v4l2_mbus_framefmt *format;

	format = v4l2_subdev_get_try_format(fh, 0);
	format->code = V4L2_MBUS_FMT_UYVY8_1X16;
	format->width = ov5640_frmsizes[OV5640_SIZE_VGA].width;
	format->height = ov5640_frmsizes[OV5640_SIZE_VGA].height;
	format->field = V4L2_FIELD_NONE;
	format->colorspace = V4L2_COLORSPACE_JPEG;

	return 0;
}

static int ov5640_close(struct v4l2_subdev *subdev, struct v4l2_subdev_fh *fh)
{
	return 0;
}

static struct v4l2_subdev_internal_ops ov5640_subdev_internal_ops = {
	.registered = ov5640_registered,
	.open = ov5640_open,
	.close = ov5640_close,
};
#endif

static int ov5640_get_resources(struct ov5640 *ov5640, struct device *dev)
{
#if 0 // TODO rewrite clock management
	const struct ov5640_platform_data *pdata = ov5640->pdata;
	int ret = 0;

	ov5640->xvclk = clk_get(dev, pdata->clk_xvclk);
	if (IS_ERR(ov5640->xvclk)) {
		dev_err(dev, "Unable to get XVCLK (%s)\n", pdata->clk_xvclk);
		return -ENODEV;
	}

	if (clk_round_rate(ov5640->xvclk, 24000000) != 24000000)
		dev_warn(dev, "XVCLK set to rounded aproximate (%lu Hz)",
			 clk_round_rate(ov5640->xvclk, 24000000));

	if (clk_set_rate(ov5640->xvclk,
			 clk_round_rate(ov5640->xvclk, 24000000))) {
		dev_err(dev, "Unable to change XVCLK (%s) rate!\n",
			pdata->clk_xvclk);
		ret = -EINVAL;
		goto err_clk_set_rate;
	}
#endif

#if 0 // TODO rewrite power management and chip reset
	if (!pdata->reg_avdd)
		goto get_reg_dovdd;

	ov5640->avdd = devm_regulator_get(dev, pdata->reg_avdd);
	if (IS_ERR(ov5640->avdd)) {
		dev_err(dev, "Unable to get AVDD (%s) regulator\n",
			pdata->reg_avdd);
		ret = -ENODEV;
		goto err_reg_avdd;
	}

	if (regulator_set_voltage(ov5640->avdd, 2800000, 2800000)) {
		dev_err(dev, "Unable to set valid AVDD (%s) regulator"
			" voltage to: 2.8V\n", pdata->reg_avdd);
		ret = -ENODEV;
		goto err_reg_avdd;
	}

get_reg_dovdd:
	if (!pdata->reg_dovdd)
		goto get_gpio_pwdn;

	ov5640->dovdd = devm_regulator_get(dev, pdata->reg_dovdd);
	if (IS_ERR(ov5640->dovdd)) {
		dev_err(dev, "Unable to get DOVDD (%s) regulator\n",
			pdata->reg_dovdd);
		ret = -ENODEV;
		goto err_reg_dovdd;
	}

	if (regulator_set_voltage(ov5640->dovdd, 1800000, 1800000)) {
		dev_err(dev, "Unable to set valid DOVDD (%s) regulator"
			" voltage to: 1.8V\n", pdata->reg_dovdd);
		ret = -ENODEV;
		goto err_reg_dovdd;
	}

get_gpio_pwdn:
	if (!gpio_is_valid(pdata->gpio_pwdn))
		goto get_gpio_resetb;

	if (gpio_request_one(pdata->gpio_pwdn,
			     pdata->is_gpio_pwdn_acthi ?
			     GPIOF_OUT_INIT_LOW : GPIOF_OUT_INIT_HIGH,
			     "OV5640_PWDN")) {
		dev_err(dev, "Cannot request GPIO %d\n", pdata->gpio_pwdn);
		ret = -ENODEV;
		goto err_gpio_pwdn;
	}

get_gpio_resetb:
	if (!gpio_is_valid(pdata->gpio_resetb))
		goto out;

	if (gpio_request_one(pdata->gpio_resetb,
			     pdata->is_gpio_resetb_acthi ?
			     GPIOF_OUT_INIT_LOW : GPIOF_OUT_INIT_HIGH,
			     "OV5640_RESETB")) {
		dev_err(dev, "Cannot request GPIO %d\n", pdata->gpio_resetb);
		ret = -ENODEV;
		goto err_gpio_resetb;
	}

out:
	return 0;

err_gpio_resetb:
	if (gpio_is_valid(pdata->gpio_pwdn))
		gpio_free(pdata->gpio_pwdn);
err_gpio_pwdn:
err_reg_dovdd:
err_reg_avdd:

err_clk_set_rate:
	clk_put(ov5640->xvclk);
#endif
	return 0;
}

static void ov5640_put_resources(struct ov5640 *ov5640)
{
#if 0 // TODO rewrite power management, chip reset and clk
	if (gpio_is_valid(ov5640->pdata->gpio_resetb))
		gpio_free(ov5640->pdata->gpio_resetb);
	if (gpio_is_valid(ov5640->pdata->gpio_pwdn))
		gpio_free(ov5640->pdata->gpio_pwdn);
	clk_put(ov5640->xvclk);
#endif
}

static int ov5640_probe(struct i2c_client *client,
			 const struct i2c_device_id *did)
{
	struct ov5640 *ov5640;
	int ret;
	u8 revision = 0;

#if 0 // TODO: handle platform data
	if (!client->dev.platform_data) {
		dev_err(&client->dev, "No platform data!!\n");
		return -ENODEV;
	}
#endif

	ov5640 = kzalloc(sizeof(*ov5640), GFP_KERNEL);
	if (!ov5640)
		return -ENOMEM;

#if 0 // TODO: handle platform data
	ov5640->pdata = client->dev.platform_data;
#endif

	ret = ov5640_get_resources(ov5640, &client->dev);
	if (ret) {
		kfree(ov5640);
		return ret;
	}

	ov5640->format.code = V4L2_PIX_FMT_UYVY;
	ov5640->format.width = ov5640_frmsizes[OV5640_SIZE_VGA].width;
	ov5640->format.height = ov5640_frmsizes[OV5640_SIZE_VGA].height;
	ov5640->format.field = V4L2_FIELD_NONE;
	ov5640->format.colorspace = V4L2_COLORSPACE_JPEG;

	ov5640->clk_cfg.sc_pll_prediv = 3;
	ov5640->clk_cfg.sc_pll_rdiv = 1;
	ov5640->clk_cfg.sc_pll_mult = 84;
	ov5640->clk_cfg.sysclk_div = 1;
	ov5640->clk_cfg.mipi_div = 1;

	v4l2_i2c_subdev_init(&ov5640->subdev, client, &ov5640_subdev_ops);

	ret = ov5640_reg_read(client,0x302A, &revision);
	if (ret<0) {
		dev_info(&client->dev,"Failure to detect OV5640\n");
		return ret;
	}

	dev_info(&client->dev, "Detected a OV5640 chip, revision %x\n",
          revision);


#if 0 // TODO all this feature not supported by this version of V4L2
	ov5640->subdev.internal_ops = &ov5640_subdev_internal_ops;
	ov5640->subdev.flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
	ov5640->subdev.entity.type = MEDIA_ENT_T_V4L2_SUBDEV_SENSOR;
	ov5640->pad.flags = MEDIA_PAD_FL_SOURCE;

	ret = media_entity_init(&ov5640->subdev.entity, 1, &ov5640->pad, 0);
	if (ret < 0) {
		media_entity_cleanup(&ov5640->subdev.entity);
		ov5640_put_resources(ov5640);
		kfree(ov5640);
	}
#endif

	return ret;
}

static int ov5640_remove(struct i2c_client *client)
{
	struct v4l2_subdev *subdev = i2c_get_clientdata(client);
	struct ov5640 *ov5640 = to_ov5640(subdev);

#if 0 //TODO this function not supported by this version of V4L2
	v4l2_ctrl_handler_free(&ov5640->ctrl_handler);
	media_entity_cleanup(&subdev->entity);
#endif
	v4l2_device_unregister_subdev(subdev);
	ov5640_put_resources(ov5640);
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

MODULE_DESCRIPTION("OmniVision OV5640 Camera driver");
MODULE_AUTHOR("Sergio Aguirre <saaguirre@ti.com>");
MODULE_LICENSE("GPL v2");
