/*
 * A V4L2 driver for Aptina MT9M114 cameras.
 *
 * Copyright 2015 Aldebaran Robotics  Written
 * by Joseph Pinkasfeld with substantial inspiration from ov7670 code.
 *
 *  joseph pinkasfeld <joseph.pinkasfeld@gmail.com>
 *  Ludovic SMAL <lsmal@aldebaran-robotics.com>
 *  Corentin Le Molgat <clemolgat@aldebaran.com>
 *  Ludovic Guegan <lguegan@aldebaran.com>
 *
 * This file may be distributed under the terms of the GNU General
 * Public License, version 2.
 */
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/delay.h>
#include <linux/videodev2.h>

#include <media/v4l2-mediabus.h>

#include <media/v4l2-device.h>
#include <media/v4l2-ctrls.h>


MODULE_AUTHOR("Joseph Pinkasfeld <joseph.pinkasfeld@gmail.com>;"
	      "Ludovic SMAL <lsmal@aldebaran.com>,"
	      "Corentin Le Molgat <clemolgat@aldebaran.com>,"
	      "Ludovic Guegan <lguegan@aldebaran.com>");
MODULE_DESCRIPTION("A low-level driver for Aptina MT9M114 sensors");
MODULE_LICENSE("GPL");

static int debug = 0;
module_param(debug, int, 0644);
MODULE_PARM_DESC(debug, "Debug level (0-1)");

// On Romeo the faceboard takes too much time to boot so
// we must delay configuration changes.
static int camera_sync = 0;
module_param(camera_sync, int, 0);
MODULE_PARM_DESC(camera_sync, "Delay the camera initialization (use on Romeo to wait the faceboard)");

static int ext_clock = 24;
module_param(ext_clock, int, 0644);
MODULE_PARM_DESC(ext_clock, "Frequency of the external clock in MHz (possible values: 12, 24, 48; default is 24)");

#ifndef V4L2_CID_CAM_INIT
#define V4L2_CID_CAM_INIT V4L2_CID_BAND_STOP_FILTER
#endif

// CID_STEREO == ((0x00980000 | 0x900) + 33)
#define CID_STEREO V4L2_CID_CAM_INIT


/*
 * Basic window sizes.	These probably belong somewhere more globally
 * useful.
 */
#define WXGA_WIDTH	1280
#define WXGA_HEIGHT	720
#define FULL_HEIGHT	960
#define VGA_WIDTH	640
#define VGA_HEIGHT	480
#define QVGA_WIDTH	320
#define QVGA_HEIGHT	240
#define QQVGA_WIDTH	160
#define QQVGA_HEIGHT	120
#define CIF_WIDTH	352
#define CIF_HEIGHT	288
#define QCIF_WIDTH	176
#define QCIF_HEIGHT	144


/*
 * Our nominal (default) frame rate.
 */
#define MT9M114_FRAME_RATE 256

/*
 * The MT9M114 sits on i2c with ID 0x48 or 0x5D
 * depends on input SADDR
 */
#define MT9M114_I2C_ADDR_S0 0x48
#define MT9M114_I2C_ADDR_S1 0x5D

/* Registers */

#define REG_CHIP_ID				  0x0000
#define REG_MON_MAJOR_VERSION			  0x8000
#define REG_MON_MINOR_VERION			  0x8002
#define REG_MON_RELEASE_VERSION			  0x8004
#define REG_RESET_AND_MISC_CONTROL		  0x001A
#define REG_USER_DEFINED_DEVICE_ADDRESS_ID	  0x002E
#define REG_PAD_SLEW_CONTROL			  0x001E
#define REG_PAD_CONTROL				  0x0032
#define REG_COMMAND_REGISTER			  0x0080
#define HOST_COMMAND_APPLY_PATCH		  0x0001
#define HOST_COMMAND_SET_STATE			  0x0002
#define HOST_COMMAND_REFRESH			  0x0004
#define HOST_COMMAND_WAIT_FOR_EVENT		  0x0008
#define HOST_COMMAND_OK				  0x8000
#define REG_ACCESS_CTL_STAT			  0x0982
#define REG_PHYSICAL_ADDRESS_ACCESS		  0x098A
#define REG_LOGICAL_ADDRESS_ACCESS		  0x098E
#define MCU_VARIABLE_DATA0			  0x0990
#define MCU_VARIABLE_DATA1			  0x0992
#define REG_RESET_REGISTER			  0x301A
#define REG_DAC_TXLO_ROW			  0x316A
#define REG_DAC_TXLO				  0x316C
#define REG_DAC_LD_4_5				  0x3ED0
#define REG_DAC_LD_6_7				  0x3ED2
#define REG_DAC_ECL				  0x316E
#define REG_DELTA_DK_CONTROL			  0x3180
#define REG_CHAIN_CONTROL			  0x31FC
#define CHAIN_CONTROL_MASTER			  0xe100
#define CHAIN_CONTROL_SLAVE			  0xc000
#define REG_SAMP_COL_PUP2			  0x3E14
#define REG_COLUMN_CORRECTION			  0x30D4
#define REG_LL_ALGO				  0xBC04
#define LL_EXEC_DELTA_DK_CORRECTION		  0x0200
#define REG_CAM_SYSCTL_PLL_ENABLE		  0xC97E
#define REG_CAM_SYSCTL_PLL_DIVIDER_M_N		  0xC980
#define REG_CAM_SYSCTL_PLL_DIVIDER_P		  0xC982
#define REG_CAM_SENSOR_CFG_Y_ADDR_START		  0xC800
#define REG_CAM_SENSOR_CFG_X_ADDR_START		  0xC802
#define REG_CAM_SENSOR_CFG_Y_ADDR_END		  0xC804
#define REG_CAM_SENSOR_CFG_X_ADDR_END		  0xC806
#define REG_CAM_SENSOR_CFG_PIXCLK		  0xC808
#define REG_CAM_SENSOR_CFG_ROW_SPEED		  0xC80C
#define REG_CAM_SENSOR_CFG_FINE_INTEG_TIME_MIN	  0xC80E
#define REG_CAM_SENSOR_CFG_FINE_INTEG_TIME_MAX	  0xC810
#define REG_CAM_SENSOR_CFG_FRAME_LENGTH_LINES	  0xC812
#define REG_CAM_SENSOR_CFG_LINE_LENGTH_PCK	  0xC814
#define REG_CAM_SENSOR_CFG_FINE_CORRECTION	  0xC816
#define REG_CAM_SENSOR_CFG_CPIPE_LAST_ROW	  0xC818
#define REG_CAM_SENSOR_CFG_REG_0_DATA		  0xC826
#define REG_CAM_SENSOR_CONTROL_READ_MODE	  0xC834
#define REG_CAM_MODE_SELECT			  0xC84C
#define REG_CAM_MODE_TEST_PATTERN_SELECT	  0xC84D
#define CAM_SENSOR_CONTROL_VERT_FLIP_EN		  0x0002
#define CAM_SENSOR_CONTROL_HORZ_FLIP_EN		  0x0001
#define CAM_SENSOR_CONTROL_BINNING_EN		  0x0330
#define CAM_SENSOR_CONTROL_SKIPPING_EN		  0x0110
#define REG_CAM_CROP_WINDOW_XOFFSET		  0xC854
#define REG_CAM_CROP_WINDOW_YOFFSET		  0xC856
#define REG_CAM_CROP_WINDOW_WIDTH		  0xC858
#define REG_CAM_CROP_WINDOW_HEIGHT		  0xC85A
#define REG_CAM_CROP_CROPMODE			  0xC85C
#define REG_CAM_OUTPUT_WIDTH			  0xC868
#define REG_CAM_OUTPUT_HEIGHT			  0xC86A
#define REG_CAM_OUTPUT_FORMAT			  0xC86C
#define REG_CAM_OUTPUT_OFFSET			  0xC870
#define REG_CAM_PORT_OUTPUT_CONTROL		  0xC984
#define REG_CAM_OUPUT_FORMAT_YUV		  0xC86E
#define REG_CAM_STAT_AWB_CLIP_WINDOW_XSTART	  0xC914
#define REG_CAM_STAT_AWB_CLIP_WINDOW_YSTART	  0xC916
#define REG_CAM_STAT_AWB_CLIP_WINDOW_XEND	  0xC918
#define REG_CAM_STAT_AWB_CLIP_WINDOW_YEND	  0xC91A
#define REG_CAM_STAT_AE_INITIAL_WINDOW_XSTART	  0xC91C
#define REG_CAM_STAT_AE_INITIAL_WINDOW_YSTART	  0xC91E
#define REG_CAM_STAT_AE_INITIAL_WINDOW_XEND	  0xC920
#define REG_CAM_STAT_AE_INITIAL_WINDOW_YEND	  0xC922
#define REG_CAM_PGA_PGA_CONTROL			  0xC95E
#define REG_SYSMGR_NEXT_STATE			  0xDC00
#define REG_SYSMGR_CURRENT_STATE		  0xDC01
#define REG_PATCHLDR_LOADER_ADDRESS		  0xE000
#define REG_PATCHLDR_PATCH_ID			  0xE002
#define REG_PATCHLDR_FIRMWARE_ID		  0xE004
#define REG_PATCHLDR_APPLY_STATUS		  0xE008
#define REG_AUTO_BINNING_MODE			  0xE801
#define REG_CAM_SENSOR_CFG_MAX_ANALOG_GAIN	  0xC81C
#define REG_CROP_CROPMODE			  0xC85C
#define REG_CAM_AET_AEMODE			  0xC878
#define REG_CAM_AET_TARGET_AVG_LUMA		  0xC87A
#define REG_CAM_AET_TARGET_AVERAGE_LUMA_DARK	  0xC87B
#define REG_CAM_AET_BLACK_CLIPPING_TARGET	  0xC87C
#define REG_CAM_AET_AE_MAX_VIRT_AGAIN		  0xC886
#define REG_CAM_AET_MAX_FRAME_RATE		  0xC88C
#define REG_CAM_AET_MIN_FRAME_RATE		  0xC88E
#define REG_CAM_AET_TARGET_GAIN			  0xC890
#define REG_AE_ALGORITHM			  0xA404
#define REG_AE_TRACK_MODE			  0xA802
#define REG_AE_TRACK_AE_TRACKING_DAMPENING_SPEED  0xA80A

#define REG_CAM_LL_START_BRIGHTNESS		  0xC926
#define REG_CAM_LL_STOP_BRIGHTNESS		  0xC928
#define REG_CAM_LL_START_GAIN_METRIC		  0xC946
#define REG_CAM_LL_STOP_GAIN_METRIC		  0xC948
#define REG_CAM_LL_START_TARGET_LUMA_BM		  0xC952
#define REG_CAM_LL_STOP_TARGET_LUMA_BM		  0xC954
#define REG_CAM_LL_START_SATURATION		  0xC92A
#define REG_CAM_LL_END_SATURATION		  0xC92B
#define REG_CAM_LL_START_DESATURATION		  0xC92C
#define REG_CAM_LL_END_DESATURATION		  0xC92D
#define REG_CAM_LL_START_DEMOSAIC		  0xC92E
#define REG_CAM_LL_START_AP_GAIN		  0xC92F
#define REG_CAM_LL_START_AP_THRESH		  0xC930
#define REG_CAM_LL_STOP_DEMOSAIC		  0xC931
#define REG_CAM_LL_STOP_AP_GAIN			  0xC932
#define REG_CAM_LL_STOP_AP_THRESH		  0xC933
#define REG_CAM_LL_START_NR_RED			  0xC934
#define REG_CAM_LL_START_NR_GREEN		  0xC935
#define REG_CAM_LL_START_NR_BLUE		  0xC936
#define REG_CAM_LL_START_NR_THRESH		  0xC937
#define REG_CAM_LL_STOP_NR_RED			  0xC938
#define REG_CAM_LL_STOP_NR_GREEN		  0xC939
#define REG_CAM_LL_STOP_NR_BLUE			  0xC93A
#define REG_CAM_LL_STOP_NR_THRESH		  0xC93B
#define REG_CAM_LL_START_CONTRAST_BM		  0xC93C
#define REG_CAM_LL_STOP_CONTRAST_BM		  0xC93E
#define REG_CAM_LL_GAMMA			  0xC940
#define REG_CAM_LL_START_CONTRAST_GRADIENT	  0xC942
#define REG_CAM_LL_STOP_CONTRAST_GRADIENT	  0xC943
#define REG_CAM_LL_START_CONTRAST_LUMA_PERCENTAGE 0xC944
#define REG_CAM_LL_STOP_CONTRAST_LUMA_PERCENTAGE  0xC945
#define REG_CAM_LL_START_FADE_TO_BLACK_LUMA	  0xC94A
#define REG_CAM_LL_STOP_FADE_TO_BLACK_LUMA	  0xC94C
#define REG_CAM_LL_CLUSTER_DC_TH_BM		  0xC94E
#define REG_CAM_LL_CLUSTER_DC_GATE_PERCENTAGE	  0xC950
#define REG_CAM_LL_SUMMING_SENSITIVITY_FACTOR	  0xC951
#define REG_CCM_DELTA_GAIN			  0xB42A


#define REG_CAM_HUE_ANGLE			  0xC873

// AWB
#define REG_AWB_AWB_MODE			  0xC909
#define REG_AWB_MIN_TEMPERATURE			  0xC8EC // default 2700 Kelvin
#define REG_AWB_MAX_TEMPERATURE			  0xC8EE // default 6500 Kelvin
#define REG_AWB_COLOR_TEMPERATURE		  0xC8F0

// UVC
#define REG_UVC_AE_MODE				  0xCC00
#define REG_UVC_AUTO_WHITE_BALANCE_TEMPERATURE	  0xCC01
#define REG_UVC_AE_PRIORITY			  0xCC02
#define REG_UVC_POWER_LINE_FREQUENCY		  0xCC03
#define REG_UVC_EXPOSURE_TIME			  0xCC04
#define REG_UVC_BACKLIGHT_COMPENSATION		  0xCC08
#define REG_UVC_BRIGHTNESS			  0xCC0A
#define REG_UVC_CONTRAST			  0xCC0C
#define REG_UVC_GAIN				  0xCC0E
#define REG_UVC_HUE				  0xCC10
#define REG_UVC_SATURATION			  0xCC12
#define REG_UVC_SHARPNESS			  0xCC14
#define REG_UVC_GAMMA				  0xCC16
#define REG_UVC_WHITE_BALANCE_TEMPERATURE	  0xCC18
#define REG_UVC_FRAME_INTERVAL			  0xCC1C
#define REG_UVC_MANUAL_EXPOSURE			  0xCC20
#define REG_UVC_FLICKER_AVOIDANCE		  0xCC21
#define REG_UVC_ALGO				  0xCC22
#define REG_UVC_RESULT_STATUS			  0xCC24


/* SYS_STATE values (for SYSMGR_NEXT_STATE and SYSMGR_CURRENT_STATE) */
#define MT9M114_SYS_STATE_ENTER_CONFIG_CHANGE		0x28
#define MT9M114_SYS_STATE_STREAMING			0x31
#define MT9M114_SYS_STATE_START_STREAMING		0x34
#define MT9M114_SYS_STATE_ENTER_SUSPEND			0x40
#define MT9M114_SYS_STATE_SUSPENDED			0x41
#define MT9M114_SYS_STATE_ENTER_STANDBY			0x50
#define MT9M114_SYS_STATE_STANDBY			0x52
#define MT9M114_SYS_STATE_LEAVE_STANDBY			0x54

/*
 * Information we maintain about a known sensor.
 */
struct mt9m114_format_struct;  /* coming later */
struct mt9m114_info {
	struct v4l2_subdev sd;
	struct v4l2_ctrl_handler ctrl_handler;
	struct mt9m114_format_struct *fmt; /* Current format */
	unsigned char sat;		   /* Saturation value */
	int hue;			   /* Hue value */
	int flag_vflip;			   /* flip vertical */
	int flag_hflip;			   /* flip horizontal */
	int wsize_format;		   /* enum e_win_size_format */
	int stereo_on;			   /* 0 when not doing stereo */
};

/*
 * Returns private structure of the device
 */
static inline struct mt9m114_info *to_state(struct v4l2_subdev *sd)
{
	return container_of(sd, struct mt9m114_info, sd);
}

/*
 * The default register settings, as obtained from OmniVision. There
 * is really no making sense of most of these - lots of "reserved" values
 * and such.
 *
 * These settings give VGA YUYV.
 */
struct regval_list {
	u16 reg_num;
	u16 size;
	u32 value;
};

static struct regval_list pga_regs[] = {
	{ 0x098E, 2, 0 },
	{ 0xC95E, 2, 3 },
	{ 0xC95E, 2, 2 },
	{ 0x3640, 2, 368 },
	{ 0x3642, 2, 3787 },
	{ 0x3644, 2, 22480 },
	{ 0x3646, 2, 33549 },
	{ 0x3648, 2, 62062 },
	{ 0x364A, 2, 32303 },
	{ 0x364C, 2, 18603 },
	{ 0x364E, 2, 26192 },
	{ 0x3650, 2, 52556 },
	{ 0x3652, 2, 44686 },
	{ 0x3654, 2, 32431 },
	{ 0x3656, 2, 23244 },
	{ 0x3658, 2, 7056 },
	{ 0x365A, 2, 64140 },
	{ 0x365C, 2, 37614 },
	{ 0x365E, 2, 32207 },
	{ 0x3660, 2, 19178 },
	{ 0x3662, 2, 26800 },
	{ 0x3664, 2, 45101 },
	{ 0x3666, 2, 43151 },
	{ 0x3680, 2, 13964 },
	{ 0x3682, 2, 1869 },
	{ 0x3684, 2, 9871 },
	{ 0x3686, 2, 32394 },
	{ 0x3688, 2, 38832 },
	{ 0x368A, 2, 492 },
	{ 0x368C, 2, 2894 },
	{ 0x368E, 2, 4687 },
	{ 0x3690, 2, 45006 },
	{ 0x3692, 2, 34192 },
	{ 0x3694, 2, 973 },
	{ 0x3696, 2, 2349 },
	{ 0x3698, 2, 25323 },
	{ 0x369A, 2, 41294 },
	{ 0x369C, 2, 46959 },
	{ 0x369E, 2, 3405 },
	{ 0x36A0, 2, 47531 },
	{ 0x36A2, 2, 38860 },
	{ 0x36A4, 2, 22506 },
	{ 0x36A6, 2, 37359 },
	{ 0x36C0, 2, 3569 },
	{ 0x36C2, 2, 36620 },
	{ 0x36C4, 2, 30224 },
	{ 0x36C6, 2, 11116 },
	{ 0x36C8, 2, 42739 },
	{ 0x36CA, 2, 1681 },
	{ 0x36CC, 2, 61514 },
	{ 0x36CE, 2, 13265 },
	{ 0x36D0, 2, 44462 },
	{ 0x36D2, 2, 51635 },
	{ 0x36D4, 2, 23184 },
	{ 0x36D6, 2, 39789 },
	{ 0x36D8, 2, 22480 },
	{ 0x36DA, 2, 3885 },
	{ 0x36DC, 2, 64882 },
	{ 0x36DE, 2, 3505 },
	{ 0x36E0, 2, 46314 },
	{ 0x36E2, 2, 26864 },
	{ 0x36E4, 2, 36813 },
	{ 0x36E6, 2, 41555 },
	{ 0x3700, 2, 1325 },
	{ 0x3702, 2, 60557 },
	{ 0x3704, 2, 46961 },
	{ 0x3706, 2, 13199 },
	{ 0x3708, 2, 25234 },
	{ 0x370A, 2, 10253 },
	{ 0x370C, 2, 36912 },
	{ 0x370E, 2, 46449 },
	{ 0x3710, 2, 17713 },
	{ 0x3712, 2, 19282 },
	{ 0x3714, 2, 10509 },
	{ 0x3716, 2, 53295 },
	{ 0x3718, 2, 38417 },
	{ 0x371A, 2, 8881 },
	{ 0x371C, 2, 26834 },
	{ 0x371E, 2, 27981 },
	{ 0x3720, 2, 39469 },
	{ 0x3722, 2, 34321 },
	{ 0x3724, 2, 5232 },
	{ 0x3726, 2, 20978 },
	{ 0x3740, 2, 35307 },
	{ 0x3742, 2, 49806 },
	{ 0x3744, 2, 62036 },
	{ 0x3746, 2, 23250 },
	{ 0x3748, 2, 27830 },
	{ 0x374A, 2, 8111 },
	{ 0x374C, 2, 51085 },
	{ 0x374E, 2, 33653 },
	{ 0x3750, 2, 24914 },
	{ 0x3752, 2, 29270 },
	{ 0x3754, 2, 5133 },
	{ 0x3756, 2, 5933 },
	{ 0x3758, 2, 52436 },
	{ 0x375A, 2, 13362 },
	{ 0x375C, 2, 18166 },
	{ 0x375E, 2, 37550 },
	{ 0x3760, 2, 39566 },
	{ 0x3762, 2, 61300 },
	{ 0x3764, 2, 23602 },
	{ 0x3766, 2, 26198 },
	{ 0x3782, 2, 480 },
	{ 0x3784, 2, 672 },
	{ 0xC960, 2, 2800 },
	{ 0xC962, 2, 31149 },
	{ 0xC964, 2, 22448 },
	{ 0xC966, 2, 30936 },
	{ 0xC968, 2, 29792 },
	{ 0xC96A, 2, 4000 },
	{ 0xC96C, 2, 33143 },
	{ 0xC96E, 2, 33116 },
	{ 0xC970, 2, 33041 },
	{ 0xC972, 2, 32855 },
	{ 0xC974, 2, 6500 },
	{ 0xC976, 2, 31786 },
	{ 0xC978, 2, 26268 },
	{ 0xC97A, 2, 32319 },
	{ 0xC97C, 2, 29650 },
	{ 0xC95E, 2, 3 },
	{ 0xffff, 0xffff, 0xffff}
};

static struct regval_list ccm_awb_regs[] = {
	{ 0xC892, 2, 615 },
	{ 0xC894, 2, 65306 },
	{ 0xC896, 2, 65459 },
	{ 0xC898, 2, 65408 },
	{ 0xC89A, 2, 358 },
	{ 0xC89C, 2, 3 },
	{ 0xC89E, 2, 65434 },
	{ 0xC8A0, 2, 65204 },
	{ 0xC8A2, 2, 589 },
	{ 0xC8A4, 2, 447 },
	{ 0xC8A6, 2, 65281 },
	{ 0xC8A8, 2, 65523 },
	{ 0xC8AA, 2, 65397 },
	{ 0xC8AC, 2, 408 },
	{ 0xC8AE, 2, 65533 },
	{ 0xC8B0, 2, 65434 },
	{ 0xC8B2, 2, 65255 },
	{ 0xC8B4, 2, 680 },
	{ 0xC8B6, 2, 473 },
	{ 0xC8B8, 2, 65318 },
	{ 0xC8BA, 2, 65523 },
	{ 0xC8BC, 2, 65459 },
	{ 0xC8BE, 2, 306 },
	{ 0xC8C0, 2, 65512 },
	{ 0xC8C2, 2, 65498 },
	{ 0xC8C4, 2, 65229 },
	{ 0xC8C6, 2, 706 },
	{ 0xC8C8, 2, 117 },
	{ 0xC8CA, 2, 284 },
	{ 0xC8CC, 2, 154 },
	{ 0xC8CE, 2, 261 },
	{ 0xC8D0, 2, 164 },
	{ 0xC8D2, 2, 172 },
	{ 0xC8D4, 2, 2700 },
	{ 0xC8D6, 2, 3850 },
	{ 0xC8D8, 2, 6500 },
	{ 0xC914, 2, 0 },
	{ 0xC916, 2, 0 },
	{ 0xC918, 2, 1279 },
	{ 0xC91A, 2, 719 },
	{ 0xC904, 2, 51 },
	{ 0xC906, 2, 64 },
	{ 0xC8F2, 1, 3 },
	{ 0xC8F3, 1, 2 },
	{ 0xC906, 2, 60 },
	{ 0xC8F4, 2, 0 },
	{ 0xC8F6, 2, 0 },
	{ 0xC8F8, 2, 0 },
	{ 0xC8FA, 2, 59172 },
	{ 0xC8FC, 2, 5507 },
	{ 0xC8FE, 2, 8261 },
	{ 0xC900, 2, 1023 },
	{ 0xC902, 2, 124 },
	{ 0xC90C, 1, 128 },
	{ 0xC90D, 1, 128 },
	{ 0xC90E, 1, 128 },
	{ 0xC90F, 1, 136 },
	{ 0xC910, 1, 128 },
	{ 0xC911, 1, 128 },
	{ 0xffff, 0xffff, 0xffff }
};


static struct regval_list uvc_ctrl_regs[] = {
	{ 0xCC00, 1, 0x02 },
	{ 0xCC01, 1, 0x01 },
	{ 0xCC02, 1, 0x00 },
	{ 0xCC03, 1, 0x02 },
	{ 0xCC04, 4, 0x00000001 },
	{ 0xCC08, 2, 0x0001 },
	{ 0xCC0A, 2, 0x0037 },
	{ 0xCC0C, 2, 0x0020 },
	{ 0xCC0E, 2, 0x0020 },
	{ 0xCC10, 2, 0x0000 },
	{ 0xCC12, 2, 0x0080 },
	{ 0xCC14, 2, -7 },
	{ 0xCC16, 2, 0x00DC },
	{ 0xCC18, 2, 0x09C4 },
	{ 0xCC1C, 4, 0x00000001 },
	{ 0xCC20, 1, 0x00 },
	{ 0xCC21, 1, 0x00 },
	{ 0xCC22, 2, 0x0007 },
	{ 0xCC24, 1, 0x00 },
	{ 0xffff, 0xffff, 0xffff }
};


/*
 * Sensor configuration for various resolutions
 */

static struct regval_list mt9m114_960p30_regs[] = {
	{ REG_LOGICAL_ADDRESS_ACCESS, 2, 0x0000 },
	{ REG_CAM_SENSOR_CFG_Y_ADDR_START, 2, 4 },
	{ REG_CAM_SENSOR_CFG_X_ADDR_START, 2, 4 },
	{ REG_CAM_SENSOR_CFG_Y_ADDR_END, 2, 971 },
	{ REG_CAM_SENSOR_CFG_X_ADDR_END, 2, 1291 },
	{ REG_CAM_SENSOR_CFG_PIXCLK, 4, 48000000 },
	{ REG_CAM_SENSOR_CFG_ROW_SPEED, 2, 0x0001 },
	{ REG_CAM_SENSOR_CFG_FINE_INTEG_TIME_MIN, 2, 219 },
	{ REG_CAM_SENSOR_CFG_FINE_INTEG_TIME_MAX, 2, 1480 },
	{ REG_CAM_SENSOR_CFG_FRAME_LENGTH_LINES, 2, 1007 },
	{ REG_CAM_SENSOR_CFG_LINE_LENGTH_PCK, 2, 1611 },
	{ REG_CAM_SENSOR_CFG_FINE_CORRECTION, 2, 96 },
	{ REG_CAM_SENSOR_CFG_CPIPE_LAST_ROW, 2, 963 },
	{ REG_CAM_SENSOR_CFG_REG_0_DATA, 2, 0x0020 },
	{ REG_CAM_CROP_WINDOW_XOFFSET, 2, 0x0000 },
	{ REG_CAM_CROP_WINDOW_YOFFSET, 2, 0x0000 },
	{ REG_CAM_CROP_WINDOW_WIDTH, 2, 1280 },
	{ REG_CAM_CROP_WINDOW_HEIGHT, 2, 960 },
	{ REG_CROP_CROPMODE, 1, 0x03 },
	{ REG_CAM_OUTPUT_WIDTH, 2, 1280 },
	{ REG_CAM_OUTPUT_HEIGHT, 2, 960 },
	{ REG_CAM_AET_AEMODE, 1, 0x0 },
	{ REG_CAM_AET_MAX_FRAME_RATE, 2, 0x1D97 },
	{ REG_CAM_AET_MIN_FRAME_RATE, 2, 0x1D97 },
	{ REG_CAM_STAT_AWB_CLIP_WINDOW_XSTART, 2, 0x0000 },
	{ REG_CAM_STAT_AWB_CLIP_WINDOW_YSTART, 2, 0x0000 },
	{ REG_CAM_STAT_AWB_CLIP_WINDOW_XEND, 2, 1279 },
	{ REG_CAM_STAT_AWB_CLIP_WINDOW_YEND, 2, 959 },
	{ REG_CAM_STAT_AE_INITIAL_WINDOW_XSTART, 2, 0x0000 },
	{ REG_CAM_STAT_AE_INITIAL_WINDOW_YSTART, 2, 0x0000 },
	{ REG_CAM_STAT_AE_INITIAL_WINDOW_XEND, 2, 255 },
	{ REG_CAM_STAT_AE_INITIAL_WINDOW_YEND, 2, 191 },

	{ 0xffff, 0xffff, 0xffff }	/* END MARKER */
};


static struct regval_list mt9m114_720p36_regs[] = {
	{ REG_LOGICAL_ADDRESS_ACCESS, 2, 0x0000 },
	{ REG_CAM_SENSOR_CFG_Y_ADDR_START, 2, 124 },
	{ REG_CAM_SENSOR_CFG_X_ADDR_START, 2, 4 },
	{ REG_CAM_SENSOR_CFG_Y_ADDR_END, 2, 851 },
	{ REG_CAM_SENSOR_CFG_X_ADDR_END, 2, 1291 },
	{ REG_CAM_SENSOR_CFG_PIXCLK, 4, 48000000 },
	{ REG_CAM_SENSOR_CFG_ROW_SPEED, 2, 0x0001 },
	{ REG_CAM_SENSOR_CFG_FINE_INTEG_TIME_MIN, 2, 219 },
	{ REG_CAM_SENSOR_CFG_FINE_INTEG_TIME_MAX, 2, 1558 },
	{ REG_CAM_SENSOR_CFG_FRAME_LENGTH_LINES, 2, 778 },
	{ REG_CAM_SENSOR_CFG_LINE_LENGTH_PCK, 2, 1689 },
	{ REG_CAM_SENSOR_CFG_FINE_CORRECTION, 2, 96 },
	{ REG_CAM_SENSOR_CFG_CPIPE_LAST_ROW, 2, 723 },
	{ REG_CAM_SENSOR_CFG_REG_0_DATA, 2, 0x0020 },
	{ REG_CAM_CROP_WINDOW_XOFFSET, 2, 0x0000 },
	{ REG_CAM_CROP_WINDOW_YOFFSET, 2, 0x0000 },
	{ REG_CAM_CROP_WINDOW_WIDTH, 2, 1280 },
	{ REG_CAM_CROP_WINDOW_HEIGHT, 2, 720 },
	{ REG_CROP_CROPMODE, 1, 0x03 },
	{ REG_CAM_OUTPUT_WIDTH, 2, 1280 },
	{ REG_CAM_OUTPUT_HEIGHT, 2, 720 },
	{ REG_CAM_AET_AEMODE, 1, 0x00 },
	{ REG_CAM_AET_MAX_FRAME_RATE, 2, 0x24AB },
	{ REG_CAM_AET_MIN_FRAME_RATE, 2, 0x24AB },
	{ REG_CAM_STAT_AWB_CLIP_WINDOW_XSTART, 2, 0x0000 },
	{ REG_CAM_STAT_AWB_CLIP_WINDOW_YSTART, 2, 0x0000 },
	{ REG_CAM_STAT_AWB_CLIP_WINDOW_XEND, 2, 1279 },
	{ REG_CAM_STAT_AWB_CLIP_WINDOW_YEND, 2, 719 },
	{ REG_CAM_STAT_AE_INITIAL_WINDOW_XSTART, 2, 0x0000 },
	{ REG_CAM_STAT_AE_INITIAL_WINDOW_YSTART, 2, 0x0000 },
	{ REG_CAM_STAT_AE_INITIAL_WINDOW_XEND, 2, 255 },
	{ REG_CAM_STAT_AE_INITIAL_WINDOW_YEND, 2, 143 },

	{ 0xffff, 0xffff, 0xffff }	/* END MARKER */
};

static struct regval_list mt9m114_vga_30_binned_regs[] = {
	{ REG_LOGICAL_ADDRESS_ACCESS, 2, 0x0000 },
	{ REG_CAM_SENSOR_CFG_Y_ADDR_START, 2, 0 },
	{ REG_CAM_SENSOR_CFG_X_ADDR_START, 2, 0 },
	{ REG_CAM_SENSOR_CFG_Y_ADDR_END, 2, 973 },
	{ REG_CAM_SENSOR_CFG_X_ADDR_END, 2, 1293 },
	{ REG_CAM_SENSOR_CFG_PIXCLK, 4, 48000000 },
	{ REG_CAM_SENSOR_CFG_ROW_SPEED, 2, 1 },
	{ REG_CAM_SENSOR_CFG_FINE_INTEG_TIME_MIN, 2, 451 },
	{ REG_CAM_SENSOR_CFG_FINE_INTEG_TIME_MAX, 2, 1978 },
	{ REG_CAM_SENSOR_CFG_FRAME_LENGTH_LINES, 2, 723 },
	{ REG_CAM_SENSOR_CFG_LINE_LENGTH_PCK, 2, 2213 },
	{ REG_CAM_SENSOR_CFG_FINE_CORRECTION, 2, 224 },
	{ REG_CAM_SENSOR_CFG_CPIPE_LAST_ROW, 2, 483 },
	{ REG_CAM_SENSOR_CFG_REG_0_DATA, 2, 0x0020 },
	{ REG_CAM_CROP_WINDOW_XOFFSET, 2, 0 },
	{ REG_CAM_CROP_WINDOW_YOFFSET, 2, 0 },
	{ REG_CAM_CROP_WINDOW_WIDTH, 2, 640 },
	{ REG_CAM_CROP_WINDOW_HEIGHT, 2, 480 },
	{ REG_CROP_CROPMODE, 1, 3 },
	{ REG_CAM_OUTPUT_WIDTH, 2, 640 },
	{ REG_CAM_OUTPUT_HEIGHT, 2, 480 },
	{ REG_CAM_AET_AEMODE, 1, 0x00 },
	{ REG_CAM_AET_MAX_FRAME_RATE, 2, 0x1E00 },
	{ REG_CAM_AET_MIN_FRAME_RATE, 2, 0x1E00 },
	{ REG_CAM_STAT_AWB_CLIP_WINDOW_XSTART, 2, 0 },
	{ REG_CAM_STAT_AWB_CLIP_WINDOW_YSTART, 2, 0 },
	{ REG_CAM_STAT_AWB_CLIP_WINDOW_XEND, 2, 639 },
	{ REG_CAM_STAT_AWB_CLIP_WINDOW_YEND, 2, 479 },
	{ REG_CAM_STAT_AE_INITIAL_WINDOW_XSTART, 2, 0 },
	{ REG_CAM_STAT_AE_INITIAL_WINDOW_YSTART, 2, 0 },
	{ REG_CAM_STAT_AE_INITIAL_WINDOW_XEND, 2, 127 },
	{ REG_CAM_STAT_AE_INITIAL_WINDOW_YEND, 2, 95 },

	{ 0xffff, 0xffff, 0xffff }	/* END MARKER */
};


static struct regval_list mt9m114_qvga_30_binned_regs[] = {
	{ REG_LOGICAL_ADDRESS_ACCESS, 2, 0x0000 },
	{ REG_CAM_SENSOR_CFG_Y_ADDR_START, 2, 0 },
	{ REG_CAM_SENSOR_CFG_X_ADDR_START, 2, 0 },
	{ REG_CAM_SENSOR_CFG_Y_ADDR_END, 2, 973 },
	{ REG_CAM_SENSOR_CFG_X_ADDR_END, 2, 1293 },
	{ REG_CAM_SENSOR_CFG_PIXCLK, 4, 48000000 },
	{ REG_CAM_SENSOR_CFG_ROW_SPEED, 2, 1 },
	{ REG_CAM_SENSOR_CFG_FINE_INTEG_TIME_MIN, 2, 451 },
	{ REG_CAM_SENSOR_CFG_FINE_INTEG_TIME_MAX, 2, 1978 },
	{ REG_CAM_SENSOR_CFG_FRAME_LENGTH_LINES, 2, 723 },
	{ REG_CAM_SENSOR_CFG_LINE_LENGTH_PCK, 2, 2213 },
	{ REG_CAM_SENSOR_CFG_FINE_CORRECTION, 2, 224 },
	{ REG_CAM_SENSOR_CFG_CPIPE_LAST_ROW, 2, 483 },
	{ REG_CAM_SENSOR_CFG_REG_0_DATA, 2, 0x0020 },
	{ REG_CAM_CROP_WINDOW_XOFFSET, 2, 0 },
	{ REG_CAM_CROP_WINDOW_YOFFSET, 2, 0 },
	{ REG_CAM_CROP_WINDOW_WIDTH, 2, 640 },
	{ REG_CAM_CROP_WINDOW_HEIGHT, 2, 480 },
	{ REG_CROP_CROPMODE, 1, 3 },
	{ REG_CAM_OUTPUT_WIDTH, 2, 320 },
	{ REG_CAM_OUTPUT_HEIGHT, 2, 240 },
	{ REG_CAM_AET_AEMODE, 1, 0x00 },
	{ REG_CAM_AET_MAX_FRAME_RATE, 2, 0x1E00 },
	{ REG_CAM_AET_MIN_FRAME_RATE, 2, 0x1E00 },
	{ REG_CAM_STAT_AWB_CLIP_WINDOW_XSTART, 2, 0 },
	{ REG_CAM_STAT_AWB_CLIP_WINDOW_YSTART, 2, 0 },
	{ REG_CAM_STAT_AWB_CLIP_WINDOW_XEND, 2, 319 },
	{ REG_CAM_STAT_AWB_CLIP_WINDOW_YEND, 2, 239 },
	{ REG_CAM_STAT_AE_INITIAL_WINDOW_XSTART, 2, 0 },
	{ REG_CAM_STAT_AE_INITIAL_WINDOW_YSTART, 2, 0 },
	{ REG_CAM_STAT_AE_INITIAL_WINDOW_XEND, 2, 63 },
	{ REG_CAM_STAT_AE_INITIAL_WINDOW_YEND, 2, 47 },

	{ 0xffff, 0xffff, 0xffff }	/* END MARKER */
};


static struct regval_list mt9m114_qqvga_30_binned_regs[] = {
	{ REG_LOGICAL_ADDRESS_ACCESS, 2, 0x0000 },
	{ REG_CAM_SENSOR_CFG_Y_ADDR_START, 2, 0 },
	{ REG_CAM_SENSOR_CFG_X_ADDR_START, 2, 0 },
	{ REG_CAM_SENSOR_CFG_Y_ADDR_END, 2, 973 },
	{ REG_CAM_SENSOR_CFG_X_ADDR_END, 2, 1293 },
	{ REG_CAM_SENSOR_CFG_PIXCLK, 4, 48000000 },
	{ REG_CAM_SENSOR_CFG_ROW_SPEED, 2, 1 },
	{ REG_CAM_SENSOR_CFG_FINE_INTEG_TIME_MIN, 2, 451 },
	{ REG_CAM_SENSOR_CFG_FINE_INTEG_TIME_MAX, 2, 1978 },
	{ REG_CAM_SENSOR_CFG_FRAME_LENGTH_LINES, 2, 723 },
	{ REG_CAM_SENSOR_CFG_LINE_LENGTH_PCK, 2, 2213 },
	{ REG_CAM_SENSOR_CFG_FINE_CORRECTION, 2, 224 },
	{ REG_CAM_SENSOR_CFG_CPIPE_LAST_ROW, 2, 483 },
	{ REG_CAM_SENSOR_CFG_REG_0_DATA, 2, 0x0020 },
	{ REG_CAM_CROP_WINDOW_XOFFSET, 2, 0 },
	{ REG_CAM_CROP_WINDOW_YOFFSET, 2, 0 },
	{ REG_CAM_CROP_WINDOW_WIDTH, 2, 640 },
	{ REG_CAM_CROP_WINDOW_HEIGHT, 2, 480 },
	{ REG_CROP_CROPMODE, 1, 3 },
	{ REG_CAM_OUTPUT_WIDTH, 2, 160 },
	{ REG_CAM_OUTPUT_HEIGHT, 2, 120 },
	{ REG_CAM_AET_AEMODE, 1, 0x00 },
	{ REG_CAM_AET_MAX_FRAME_RATE, 2, 0x1E00 },
	{ REG_CAM_AET_MIN_FRAME_RATE, 2, 0x1E00 },
	{ REG_CAM_STAT_AWB_CLIP_WINDOW_XSTART, 2, 0 },
	{ REG_CAM_STAT_AWB_CLIP_WINDOW_YSTART, 2, 0 },
	{ REG_CAM_STAT_AWB_CLIP_WINDOW_XEND, 2, 159 },
	{ REG_CAM_STAT_AWB_CLIP_WINDOW_YEND, 2, 119 },
	{ REG_CAM_STAT_AE_INITIAL_WINDOW_XSTART, 2, 0 },
	{ REG_CAM_STAT_AE_INITIAL_WINDOW_YSTART, 2, 0 },
	{ REG_CAM_STAT_AE_INITIAL_WINDOW_XEND, 2, 31 },
	{ REG_CAM_STAT_AE_INITIAL_WINDOW_YEND, 2, 23 },

	{ 0xffff, 0xffff, 0xffff }	/* END MARKER */
};

/*
 * Here we'll try to encapsulate the changes for just the output
 * video format.
 *
 */
static struct regval_list mt9m114_fmt_yuv422[] = {
	{ REG_CAM_OUTPUT_FORMAT, 2, 0x000A },
	{ REG_CAM_OUTPUT_OFFSET, 1, 0x10 },
	{ REG_CAM_OUPUT_FORMAT_YUV, 2, 0x1A },

	{ 0xffff, 0xffff, 0xffff }	/* END MARKER */
};

/* Low-level register I/O.*/
static int mt9m114_read(struct v4l2_subdev *sd,
			u16 reg,
			u16 size,
			u32 *value)
{
	int ret = 0, index = 0;
	u8 cmd[10];

	struct i2c_client *client = v4l2_get_subdevdata(sd);
	if (NULL == client)
		return -ENODEV;

	cmd[0] = (reg >> 8) & 0xff;
	cmd[1] = reg & 0xff;

	ret = i2c_master_send(client, cmd, 2);
	if (ret < 0)
		return ret;

	ret = i2c_master_recv(client, cmd, size);
	if (ret < 0)
		return ret;

	// Camera is in Big Endian !
	*value = 0;
	switch (size) {
	case 4:
		*value += ((u32)cmd[index++]) << 24;
		*value += ((u32)cmd[index++]) << 16;
	case 2:
		*value += ((u32)cmd[index++]) << 8;
	case 1:
		*value += ((u32)cmd[index++]);
		break;
	default:
		v4l_err(client, "%s: unexpected size: 0x%x\n", __func__, size);
		return -EINVAL;
	}

	return 0;
}

#define MAX_MASTER_WRITE 48

static int mt9m114_burst_write(struct v4l2_subdev *sd,
			       u16 reg,
			       u16 * array,
			       u16 size)
{
	int i = 0, ret = 0, index = 0, abs_index = 0, packet_size = 0;
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	u8 cmd[255];

	if (NULL == client)
		return -ENODEV;

	while (size) {
		if (size >= MAX_MASTER_WRITE)
			packet_size = MAX_MASTER_WRITE;
		else
			packet_size = size;
		size -= packet_size;

		index = 0;
		cmd[index++] = (reg >> 8) & 0xff;
		cmd[index++] = reg & 0xff;

		for (i = 0; i < packet_size; i++) {
			u16 val = array[abs_index++];
			cmd[index++] = (val >> 8) & 0xff;
			cmd[index++] = val & 0xff;
			reg += 2;
		}
		ret = i2c_master_send(client, cmd, index);
		if (ret < 0)
			return ret;
	}

	return 0;
}

static int mt9m114_write(struct v4l2_subdev *sd,
			 u16 reg,
			 u16 size,
			 u32 value)
{
	int index = 0, ret = 0;
	u8 cmd[10];

	struct i2c_client *client = v4l2_get_subdevdata(sd);
	if (NULL == client)
		return -ENODEV;

	cmd[index++] = (reg >> 8) & 0xff;
	cmd[index++] = reg & 0xff;

	// Camera is in Big Endian !
	switch (size) {
	case 4:
		cmd[index++] = (value >> 24) & 0xff;
		cmd[index++] = (value >> 16) & 0xff;
	case 2:
		cmd[index++] = (value >> 8) & 0xff;
	case 1:
		cmd[index++] = value & 0xff;
		break;
	default:
		return -EINVAL;
	}

	ret = i2c_master_send(client, cmd, index);
	if (ret < 0)
		return ret;

	return 0;
}


/*
 * Write a list of register settings; ff/ff stops the process.
 */
static int mt9m114_write_array(struct v4l2_subdev *sd, struct regval_list *vals)
{
	int i = 0;

	while ((vals[i].reg_num != 0xffff) || (vals[i].value != 0xffff)) {
		int ret = mt9m114_write(sd, vals[i].reg_num, vals[i].size, vals[i].value);
		if (ret < 0)
			return ret;
		i++;
	}
	return 0;
}

static int mt9m114_errata_1(struct v4l2_subdev *sd)
{
	return mt9m114_write(sd, REG_SAMP_COL_PUP2, 2, 0xFF39);
}

static int mt9m114_change_default_i2c_address (struct v4l2_subdev *sd,
					       u8 ID0,
					       u8 ID1)
{
	int value;
	value = ID0<<1 | (ID1<<9);
	return mt9m114_write(sd, REG_USER_DEFINED_DEVICE_ADDRESS_ID, 2, value);
}

static int mt9m114_errata_2(struct v4l2_subdev *sd)
{
	return mt9m114_write(sd, REG_RESET_REGISTER, 2, 564);
}

static int poll_command_register_bit(struct v4l2_subdev *sd, u16 bit_mask)
{
	int i = 0;
	u32 v = 0;

	for (i = 0; i < 10; i++) {
		msleep(10);
		mt9m114_read(sd, REG_COMMAND_REGISTER, 2, &v);
		if (!(v & bit_mask))
			return 0;
	}
	return 1;
}


// Patch 0202; Feature Recommended; Black level correction fix
static int mt9m114_patch2_black_lvl_correction_fix(struct v4l2_subdev *sd)
{
	int ret;
	u32 v = 0;
	u16 reg_burst[] = {
		0x70cf, 0xffff, 0xc5d4, 0x903a, 0x2144, 0x0c00, 0x2186, 0x0ff3,
		0xb844, 0xb948, 0xe082, 0x20cc, 0x80e2, 0x21cc, 0x80a2, 0x21cc,
		0x80e2, 0xf404, 0xd801, 0xf003, 0xd800, 0x7ee0, 0xc0f1, 0x08ba,
		0x0600, 0xc1a1, 0x76cf, 0xffff, 0xc130, 0x6e04, 0xc040, 0x71cf,
		0xffff, 0xc790, 0x8103, 0x77cf, 0xffff, 0xc7c0, 0xe001, 0xa103,
		0xd800, 0x0c6a, 0x04e0, 0xb89e, 0x7508, 0x8e1c, 0x0809, 0x0191,
		0xd801, 0xae1d, 0xe580, 0x20ca, 0x0022, 0x20cf, 0x0522, 0x0c5c,
		0x04e2, 0x21ca, 0x0062, 0xe580, 0xd901, 0x79c0, 0xd800, 0x0be6,
		0x04e0, 0xb89e, 0x70cf, 0xffff, 0xc8d4, 0x9002, 0x0857, 0x025e,
		0xffdc, 0xe080, 0x25cc, 0x9022, 0xf225, 0x1700, 0x108a, 0x73cf,
		0xff00, 0x3174, 0x9307, 0x2a04, 0x103e, 0x9328, 0x2942, 0x7140,
		0x2a04, 0x107e, 0x9349, 0x2942, 0x7141, 0x2a04, 0x10be, 0x934a,
		0x2942, 0x714b, 0x2a04, 0x10be, 0x130c, 0x010a, 0x2942, 0x7142,
		0x2250, 0x13ca, 0x1b0c, 0x0284, 0xb307, 0xb328, 0x1b12, 0x02c4,
		0xb34a, 0xed88, 0x71cf, 0xff00, 0x3174, 0x9106, 0xb88f, 0xb106,
		0x210a, 0x8340, 0xc000, 0x21ca, 0x0062, 0x20f0, 0x0040, 0x0b02,
		0x0320, 0xd901, 0x07f1, 0x05e0, 0xc0a1, 0x78e0, 0xc0f1, 0x71cf,
		0xffff, 0xc7c0, 0xd840, 0xa900, 0x71cf, 0xffff, 0xd02c, 0xd81e,
		0x0a5a, 0x04e0, 0xda00, 0xd800, 0xc0d1, 0x7ee0
	};
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	mt9m114_write(sd, REG_ACCESS_CTL_STAT, 2, 0x0001);
	mt9m114_write(sd, REG_PHYSICAL_ADDRESS_ACCESS, 2, 0x5000);
	mt9m114_burst_write(sd, 0xd000, reg_burst, ARRAY_SIZE(reg_burst));
	mt9m114_write(sd, REG_LOGICAL_ADDRESS_ACCESS, 2, 0x0000);
	mt9m114_write(sd, REG_PATCHLDR_LOADER_ADDRESS, 2, 0x010c);
	mt9m114_write(sd, REG_PATCHLDR_PATCH_ID, 2, 0x0202);
	mt9m114_write(sd, REG_PATCHLDR_FIRMWARE_ID, 4, 0x41030202);

	v4l_dbg(0, debug, client, "applying patch 2, Black level correction fix...\n");
	mt9m114_write(sd, REG_COMMAND_REGISTER, 2, HOST_COMMAND_OK);
	v = HOST_COMMAND_OK | HOST_COMMAND_APPLY_PATCH;
	mt9m114_write(sd, REG_COMMAND_REGISTER, 2, v);

	if (poll_command_register_bit(sd, HOST_COMMAND_APPLY_PATCH))
		v4l_warn(client, "%s: poll apply patch timeout\n", __func__);

	ret = mt9m114_read(sd, REG_COMMAND_REGISTER, 2, &v);
	if (ret) {
		v4l_err(client, "%s: read REG_COMMAND_REGISTER failed (%d)\n",
			__func__, ret);
		return ret;
	}
	if (!(v & HOST_COMMAND_OK)) {
		v4l_err(client, "%s: host_command not OK\n", __func__);
		return -EAGAIN;
	}

	ret = mt9m114_read(sd, REG_PATCHLDR_APPLY_STATUS, 1, &v);
	if (ret) {
		v4l_err(client, "%s: read REG_PATCHLDR_APPLY_STATUS failed (%d)\n",
			__func__, ret);
		return ret;
	}
	if (v) {
		v4l_err(client, "%s: apply status non-zero - value: 0x%x\n",
			__func__, v);
		return -EAGAIN;
	}

	return 0;
}



//
// Patch 03 - Feature request, Adaptive Sensitivity.
//
// This patch implements the new feature VGA auto binning mode. This was a
// request to support automatic mode transition between VGA scaled and binning
// mode (and back)
//
// To support this feature a new Firmware variable page has been added which
// controls this functionality as well as hold configuration parameters for
// the automatic binning mode of operation. This pasge needs to be configured
// correctly as these values will be used to populate the CAM page during the
// switch
//
//
// Main control variables
//     AUTO_BINNING_MODE.AUTO_BINNING_MODE_ENABLE:
//	   Controls automatic binning mode (0=disabled, 1=enabled).
//	   NOTE: Requires Change-Congig to apply
//     AUTO_BINNING_STATUS.AUTO_BINNING_STATUS_ENABLE
//	   Current enable/disable state of automatic binning mode (0=disabled, 1=enabled)
//     AUTO_BINNING_THRESHOLD_BM
//	   Switching threshold in terms of inverse brightness metric (ufixed8)
//     AUTO_BINNING_GATE_PERCENTAGE
//	   Gate width as a percentage of threshold
//
// Notes:
//     CAM_LL_SUMMING_SENSITIVITY_FACTOR
//	   This is the sensitivity gain that is achieved when sub-sampled
//	   read mode is selected, summing or average (approximately 2.0x
//	   unity=32)
//
//     The sensitivity factor and gate width must be tuned correctly to avoid
//     oscillation during the switch

static int mt9m114_patch3_adaptive_sensitivity(struct v4l2_subdev *sd)
{
	int ret;
	u32 v = 0;
	u16 reg_burst[] = {
		0x70cf, 0xffff, 0xc5d4, 0x903a, 0x2144, 0x0c00, 0x2186, 0x0ff3,
		0xb844, 0x262f, 0xf008, 0xb948, 0x21cc, 0x8021, 0xd801, 0xf203,
		0xd800, 0x7ee0, 0xc0f1, 0x71cf, 0xffff, 0xc610, 0x910e, 0x208c,
		0x8014, 0xf418, 0x910f, 0x208c, 0x800f, 0xf414, 0x9116, 0x208c,
		0x800a, 0xf410, 0x9117, 0x208c, 0x8807, 0xf40c, 0x9118, 0x2086,
		0x0ff3, 0xb848, 0x080d, 0x0090, 0xffea, 0xe081, 0xd801, 0xf203,
		0xd800, 0xc0d1, 0x7ee0, 0x78e0, 0xc0f1, 0x71cf, 0xffff, 0xc610,
		0x910e, 0x208c, 0x800a, 0xf418, 0x910f, 0x208c, 0x8807, 0xf414,
		0x9116, 0x208c, 0x800a, 0xf410, 0x9117, 0x208c, 0x8807, 0xf40c,
		0x9118, 0x2086, 0x0ff3, 0xb848, 0x080d, 0x0090, 0xffd9, 0xe080,
		0xd801, 0xf203, 0xd800, 0xf1df, 0x9040, 0x71cf, 0xffff, 0xc5d4,
		0xb15a, 0x9041, 0x73cf, 0xffff, 0xc7d0, 0xb140, 0x9042, 0xb141,
		0x9043, 0xb142, 0x9044, 0xb143, 0x9045, 0xb147, 0x9046, 0xb148,
		0x9047, 0xb14b, 0x9048, 0xb14c, 0x9049, 0x1958, 0x0084, 0x904a,
		0x195a, 0x0084, 0x8856, 0x1b36, 0x8082, 0x8857, 0x1b37, 0x8082,
		0x904c, 0x19a7, 0x009c, 0x881a, 0x7fe0, 0x1b54, 0x8002, 0x78e0,
		0x71cf, 0xffff, 0xc350, 0xd828, 0xa90b, 0x8100, 0x01c5, 0x0320,
		0xd900, 0x78e0, 0x220a, 0x1f80, 0xffff, 0xd4e0, 0xc0f1, 0x0811,
		0x0051, 0x2240, 0x1200, 0xffe1, 0xd801, 0xf006, 0x2240, 0x1900,
		0xffde, 0xd802, 0x1a05, 0x1002, 0xfff2, 0xf195, 0xc0f1, 0x0e7e,
		0x05c0, 0x75cf, 0xffff, 0xc84c, 0x9502, 0x77cf, 0xffff, 0xc344,
		0x2044, 0x008e, 0xb8a1, 0x0926, 0x03e0, 0xb502, 0x9502, 0x952e,
		0x7e05, 0xb5c2, 0x70cf, 0xffff, 0xc610, 0x099a, 0x04a0, 0xb026,
		0x0e02, 0x0560, 0xde00, 0x0a12, 0x0320, 0xb7c4, 0x0b36, 0x03a0,
		0x70c9, 0x9502, 0x7608, 0xb8a8, 0xb502, 0x70cf, 0x0000, 0x5536,
		0x7860, 0x2686, 0x1ffb, 0x9502, 0x78c5, 0x0631, 0x05e0, 0xb502,
		0x72cf, 0xffff, 0xc5d4, 0x923a, 0x73cf, 0xffff, 0xc7d0, 0xb020,
		0x9220, 0xb021, 0x9221, 0xb022, 0x9222, 0xb023, 0x9223, 0xb024,
		0x9227, 0xb025, 0x9228, 0xb026, 0x922b, 0xb027, 0x922c, 0xb028,
		0x1258, 0x0101, 0xb029, 0x125a, 0x0101, 0xb02a, 0x1336, 0x8081,
		0xa836, 0x1337, 0x8081, 0xa837, 0x12a7, 0x0701, 0xb02c, 0x1354,
		0x8081, 0x7fe0, 0xa83a, 0x78e0, 0xc0f1, 0x0dc2, 0x05c0, 0x7608,
		0x09bb, 0x0010, 0x75cf, 0xffff, 0xd4e0, 0x8d21, 0x8d00, 0x2153,
		0x0003, 0xb8c0, 0x8d45, 0x0b23, 0x0000, 0xea8f, 0x0915, 0x001e,
		0xff81, 0xe808, 0x2540, 0x1900, 0xffde, 0x8d00, 0xb880, 0xf004,
		0x8d00, 0xb8a0, 0xad00, 0x8d05, 0xe081, 0x20cc, 0x80a2, 0xdf00,
		0xf40a, 0x71cf, 0xffff, 0xc84c, 0x9102, 0x7708, 0xb8a6, 0x2786,
		0x1ffe, 0xb102, 0x0b42, 0x0180, 0x0e3e, 0x0180, 0x0f4a, 0x0160,
		0x70c9, 0x8d05, 0xe081, 0x20cc, 0x80a2, 0xf429, 0x76cf, 0xffff,
		0xc84c, 0x082d, 0x0051, 0x70cf, 0xffff, 0xc90c, 0x8805, 0x09b6,
		0x0360, 0xd908, 0x2099, 0x0802, 0x9634, 0xb503, 0x7902, 0x1523,
		0x1080, 0xb634, 0xe001, 0x1d23, 0x1002, 0xf00b, 0x9634, 0x9503,
		0x6038, 0xb614, 0x153f, 0x1080, 0xe001, 0x1d3f, 0x1002, 0xffa4,
		0x9602, 0x7f05, 0xd800, 0xb6e2, 0xad05, 0x0511, 0x05e0, 0xd800,
		0xc0f1, 0x0cfe, 0x05c0, 0x0a96, 0x05a0, 0x7608, 0x0c22, 0x0240,
		0xe080, 0x20ca, 0x0f82, 0x0000, 0x190b, 0x0c60, 0x05a2, 0x21ca,
		0x0022, 0x0c56, 0x0240, 0xe806, 0x0e0e, 0x0220, 0x70c9, 0xf048,
		0x0896, 0x0440, 0x0e96, 0x0400, 0x0966, 0x0380, 0x75cf, 0xffff,
		0xd4e0, 0x8d00, 0x084d, 0x001e, 0xff47, 0x080d, 0x0050, 0xff57,
		0x0841, 0x0051, 0x8d04, 0x9521, 0xe064, 0x790c, 0x702f, 0x0ce2,
		0x05e0, 0xd964, 0x72cf, 0xffff, 0xc700, 0x9235, 0x0811, 0x0043,
		0xff3d, 0x080d, 0x0051, 0xd801, 0xff77, 0xf025, 0x9501, 0x9235,
		0x0911, 0x0003, 0xff49, 0x080d, 0x0051, 0xd800, 0xff72, 0xf01b,
		0x0886, 0x03e0, 0xd801, 0x0ef6, 0x03c0, 0x0f52, 0x0340, 0x0dba,
		0x0200, 0x0af6, 0x0440, 0x0c22, 0x0400, 0x0d72, 0x0440, 0x0dc2,
		0x0200, 0x0972, 0x0440, 0x0d3a, 0x0220, 0xd820, 0x0bfa, 0x0260,
		0x70c9, 0x0451, 0x05c0, 0x78e0, 0xd900, 0xf00a, 0x70cf, 0xffff,
		0xd520, 0x7835, 0x8041, 0x8000, 0xe102, 0xa040, 0x09f1, 0x8114,
		0x71cf, 0xffff, 0xd4e0, 0x70cf, 0xffff, 0xc594, 0xb03a, 0x7fe0,
		0xd800, 0x0000, 0x0000, 0x0500, 0x0500, 0x0200, 0x0330, 0x0000,
		0x0000, 0x03cd, 0x050d, 0x01c5, 0x03b3, 0x00e0, 0x01e3, 0x0280,
		0x01e0, 0x0109, 0x0080, 0x0500, 0x0000, 0x0000, 0x0000, 0x0000,
		0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
		0x0000, 0x0000, 0xffff, 0xc9b4, 0xffff, 0xd324, 0xffff, 0xca34,
		0xffff, 0xd3ec
	};
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	mt9m114_write(sd, REG_ACCESS_CTL_STAT, 2, 0x0001);
	mt9m114_write(sd, REG_PHYSICAL_ADDRESS_ACCESS, 2, 0x512c);
	mt9m114_burst_write(sd, 0xd12c, reg_burst, ARRAY_SIZE(reg_burst));

	mt9m114_write(sd, REG_LOGICAL_ADDRESS_ACCESS, 2, 0x0000);
	mt9m114_write(sd, REG_PATCHLDR_LOADER_ADDRESS, 2, 0x04b4);
	mt9m114_write(sd, REG_PATCHLDR_PATCH_ID, 2, 0x0302);
	mt9m114_write(sd, REG_PATCHLDR_FIRMWARE_ID, 4, 0x41030202);

	v4l_dbg(0, debug, client, "applying patch 3, Adaptive Sensitivity...\n");
	v = HOST_COMMAND_APPLY_PATCH | HOST_COMMAND_OK;
	ret = mt9m114_write(sd, REG_COMMAND_REGISTER, 2, v);
	if (ret) {
		v4l_err(client, "%s: write REG_COMMAND_REGISTER failed (%d)\n",
			__func__, ret);
		return ret;
	}

	if (poll_command_register_bit(sd, HOST_COMMAND_APPLY_PATCH))
		v4l_warn(client, "%s: Poll apply patch timeout\n", __func__);

	ret = mt9m114_read(sd, REG_COMMAND_REGISTER, 2, &v);
	if (ret) {
		v4l_err(client, "%s: read REG_COMMAND_REGISTER failed (%d)\n",
			__func__, ret);
		return ret;
	}
	if (!(v & HOST_COMMAND_OK)) {
		v4l_err(client, "%s: host_command not OK\n", __func__);
		return -EAGAIN;
	}

	ret = mt9m114_read(sd, REG_PATCHLDR_APPLY_STATUS, 1, &v);
	if (ret) {
		v4l_err(client, "%s: read REG_PATCHLDR_APPLY_STATUS failed (%d)\n",
			__func__, ret);
		return ret;
	}
	if (v) {
		v4l_err(client, "%s: apply status non-zero - value: 0x%x\n", __func__, v);
		return -EAGAIN;
	}

	return 0;
}

static int mt9m114_set_state_command(struct v4l2_subdev *sd,
				     int command,
				     int wait_state_change)
{
	u32 v = 0;
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	// Set the desired next state (SYS_STATE_ENTER_CONFIG_CHANGE = 0x28)
	int ret = mt9m114_write(sd, REG_SYSMGR_NEXT_STATE, 1, command);
	if (ret) {
		v4l_err(client, "%s: failed to set next state (%d).\n",
			__func__, ret);
		return ret;
	}

	// (Optional) First check that the FW is ready to accept a new command
	ret = mt9m114_read(sd, REG_COMMAND_REGISTER, 2, &v);
	if (ret) {
		v4l_err(client, "%s: read REG_COMMAND_REGISTER failed (%d).\n",
			__func__, ret);
		return ret;
	}
	if (v & HOST_COMMAND_SET_STATE) {
		v4l_err(client, "%s: set State cmd bit is already set 0x%x\n",
			__func__, v);
		return -EBUSY;
	}

	// (Mandatory) Issue the Set State command
	// We set the 'OK' bit so we can detect if the command fails
	ret = mt9m114_write(sd, REG_COMMAND_REGISTER, 2,
			    HOST_COMMAND_SET_STATE | HOST_COMMAND_OK);
	if (ret) {
		v4l_err(client, "%s: write REG_COMMAND_REGISTER failed (%d).\n",
			__func__, ret);
		return ret;
	}

	if (wait_state_change) {
		// Wait for the FW to complete the command (clear the HOST_COMMAND_1 bit)
		ret = poll_command_register_bit(sd, HOST_COMMAND_SET_STATE);
		if (ret) {
			v4l_err(client, "%s: set state timeout (%d)\n",
				__func__, ret);
			return ret;
		}

		// Check the 'OK' bit to see if the command was successful
		ret = mt9m114_read(sd, REG_COMMAND_REGISTER, 2, &v);
		if (ret) {
			v4l_err(client, "%s: read REG_COMMAND_REGISTER failed (%d).\n",
				__func__, ret);
			return ret;
		}
		if (!(v & HOST_COMMAND_OK)) {
			v4l_err(client, "%s: set state command fail\n",
				__func__);
			return -EAGAIN;
		}
	}
	return 0;
}

static int mt9m114_get_state(struct v4l2_subdev *sd, u32 *state)
{
	int ret;
	ret = mt9m114_read(sd, REG_SYSMGR_CURRENT_STATE, 1, state);
	if (ret < 0) {
		struct i2c_client *client = v4l2_get_subdevdata(sd);
		v4l_err(client, "%s: failed to read current state.\n", __func__);
		return ret;
	}
	return 0;
}

static int mt9m114_set_state(struct v4l2_subdev *sd, u32 command, u32 state)
{
	u32 v = 0;
	int ret;
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	ret = mt9m114_set_state_command(sd, command, 1);
	if (ret)
		v4l_err(client, "%s: setting state failed.\n", __func__);

	ret = mt9m114_get_state(sd, &v);
	if (ret < 0) {
		return ret;
	}
	if (v != state) {
		v4l_err(client, "%s: system state (0x%x) is not as expected state (0x%x).\n",
			__func__, v, state);
		return -EBUSY;
	}
	v4l_dbg(1, debug, client, "%s: Change state to 0x%x\n", __func__, v);

	return 0;
}

//-----------------------------------------------------------------------------
// refresh - The Refresh command is intended to refresh subsystems without
// requiring a sensor configuration change
//-----------------------------------------------------------------------------
static int mt9m114_refresh(struct v4l2_subdev *sd)
{
	int ret;
	u32 v = 0;
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	// First check that the FW is ready to accept a Refresh command
	mt9m114_read(sd, REG_COMMAND_REGISTER, 2, &v);
	if (v & HOST_COMMAND_REFRESH) {
		v4l_err(client, "%s: refresh cmd bit is already set 0x%x\n",
			__func__, v);
		return -EBUSY;
	}

	// Issue the Refresh command, and set the 'OK' bit at the time time so
	//we can detect if the command fails
	ret = mt9m114_write(sd, REG_COMMAND_REGISTER, 2,
			    HOST_COMMAND_REFRESH | HOST_COMMAND_OK);
	if (ret) {
		v4l_err(client, "%s: write REG_COMMAND_REGISTER failed (%d).\n",
			__func__, ret);
		return ret;
	}

	// Wait for the FW to complete the command
	if (poll_command_register_bit(sd, HOST_COMMAND_REFRESH))
		v4l_warn(client, "set state timeout");

	// Check the 'OK' bit to see if the command was successful
	ret = mt9m114_read(sd, REG_COMMAND_REGISTER, 2, &v);
	if (ret) {
		v4l_err(client, "%s: read REG_COMMAND_REGISTER failed (%d).\n",
			__func__, ret);
		return ret;
	}
	if (!(v & HOST_COMMAND_OK)) {
		v4l_err(client, "%s: refresh command fail\n", __func__);
		return -EAGAIN;
	}

	mt9m114_read(sd, REG_UVC_RESULT_STATUS, 1, &v);
	v4l_dbg(1, debug, client, "%s REG_UVC_RESULT_STATUS: 0x%x\n", __func__, v);

	return 0;
}

static int mt9m114_change_config_async(struct v4l2_subdev *sd)
{
	int ret = mt9m114_set_state_command(sd, MT9M114_SYS_STATE_ENTER_CONFIG_CHANGE, 0);
	if (ret) {
		struct i2c_client *client = v4l2_get_subdevdata(sd);
		v4l_err(client, "%s: failed to change state (%d).\n", __func__, ret);
		return ret;
	}

	// In async configuration mode, if we don't wait, it is possible for
	// the master to send the signal before the slave is ready to catch it.
	// This sleep is NOT documented in reference documentation
	// MT9M114_DG_C.pdf.
	msleep(10);

	// Don't wait for the FW to complete the command.
	return 0;
}

//-----------------------------------------------------------------------------
// Change-Config - re-configures device state using CAM configuration variables
//-----------------------------------------------------------------------------
static int mt9m114_change_config(struct v4l2_subdev *sd)
{
	u32 v = 0;
	int ret;
	struct mt9m114_info *info = to_state(sd);
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	/* Program orientation register. */
	mt9m114_write(sd, REG_LOGICAL_ADDRESS_ACCESS, 2, 0x0000);
	ret = mt9m114_read(sd, REG_CAM_SENSOR_CONTROL_READ_MODE, 2, &v);

	if (info->flag_vflip)
		v |= CAM_SENSOR_CONTROL_VERT_FLIP_EN;
	else
		v &= ~CAM_SENSOR_CONTROL_VERT_FLIP_EN;

	if (info->flag_hflip)
		v |= CAM_SENSOR_CONTROL_HORZ_FLIP_EN;
	else
		v &= ~CAM_SENSOR_CONTROL_HORZ_FLIP_EN;

	mt9m114_write(sd, REG_CAM_SENSOR_CONTROL_READ_MODE, 2, v);

	ret = mt9m114_set_state(sd, MT9M114_SYS_STATE_ENTER_CONFIG_CHANGE,
				MT9M114_SYS_STATE_STREAMING);
	if (ret) {
		v4l_err(client, "%s: failed to enter change config state (%d).\n",
			__func__, ret);
		return ret;
	}

	ret = mt9m114_read(sd, REG_UVC_RESULT_STATUS, 1, &v);
	if (ret) {
		v4l_err(client, "%s: read REG_UVC_RESULT_STATUS failed (%d)\n",
			__func__, ret);
		return ret;
	} else {
		v4l_dbg(1, debug, client, "REG_UVC_RESULT_STATUS: 0x%x\n", v);
	}

	return 0;
}

static int mt9m114_sensor_optimization(struct v4l2_subdev *sd)
{
	mt9m114_write(sd, REG_DAC_TXLO_ROW, 2, 0x8270);
	mt9m114_write(sd, REG_DAC_TXLO, 2, 0x8270);
	mt9m114_write(sd, REG_DAC_LD_4_5, 2, 0x3605);
	mt9m114_write(sd, REG_DAC_LD_6_7, 2, 0x77FF);
	mt9m114_write(sd, REG_DAC_ECL, 2, 0xC233);
	mt9m114_write(sd, REG_DELTA_DK_CONTROL, 2, 0x87FF);
	mt9m114_write(sd, REG_COLUMN_CORRECTION, 2, 0x6080);
	mt9m114_write(sd, REG_AE_TRACK_MODE, 2, 0x0008);
	return 0;
}

/* mt9m114_reset: always complete reset operation */
static int mt9m114_reset(struct v4l2_subdev *sd, u32 val)
{
	int ret;
	u32 v = 0;
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	ret = mt9m114_read(sd, REG_RESET_AND_MISC_CONTROL, 2, &v);
	if (ret) {
		v4l_warn(client, "%s: failed to read reset control (%d)\n",
			__func__, ret);
		v = 0x0004; /* default value */
	}

	/* 1. initiate internal reset */
	ret = mt9m114_write(sd, REG_RESET_AND_MISC_CONTROL, 2, v|0x01);
	if (ret)
		v4l_warn(client, "%s: failed to set reset control (%d)\n",
			__func__, ret);
	msleep(1);

	/* 2. restore for normal operation */
	ret = mt9m114_write(sd, REG_RESET_AND_MISC_CONTROL, 2, v&(~0x1));
	if (ret)
		v4l_warn(client, "%s: failed to restore normal operation (%d)\n",
			__func__, ret);

	/* 3. wait at least 44.5 ms */
	msleep(100);

	ret = mt9m114_errata_2(sd);
	if (ret)
		v4l_warn(client, "%s: write errata 2 failed (%d)\n",
			__func__, ret);

	/* the only way to leave stereo mode is to do a reset */
	to_state(sd)->stereo_on = 0;
	return 0;
}

/*
 * Configure the sensor frequency to 48 MHz (see MT9M114 developer guide page 21).
 * Fsensor = (M * Fext ) / ((N + 1) * (P + 1))
 * Fext, the frequency of the external clock is ext_clock.
 */
static int mt9m114_PLL_settings(struct v4l2_subdev *sd)
{
	int m, n, p, reg_m_n, reg_p;
	switch(ext_clock) {
	case 48:
		m = 16; n = 1; p = 7;
		break;
	case 12:
		m = 64; n = 1; p = 7;
		break;
	case 24:
	default:
		ext_clock = 24;
		m = 32; n = 1; p = 7;
		break;

	}
	reg_m_n = (n << 8) | m;
	reg_p = p << 8;

	mt9m114_write(sd, REG_LOGICAL_ADDRESS_ACCESS, 2, 0x0000);
	mt9m114_write(sd, REG_CAM_SYSCTL_PLL_ENABLE, 1, 1);
	mt9m114_write(sd, REG_CAM_SYSCTL_PLL_DIVIDER_M_N, 2, reg_m_n);
	mt9m114_write(sd, REG_CAM_SYSCTL_PLL_DIVIDER_P, 2, reg_p);
	return 0;
}

static int mt9m114_CPIPE_preference(struct v4l2_subdev *sd)
{
	mt9m114_write(sd, REG_CAM_LL_START_BRIGHTNESS, 2, 0x0020);
	mt9m114_write(sd, REG_CAM_LL_STOP_BRIGHTNESS, 2, 0x009A);
	mt9m114_write(sd, REG_CAM_LL_START_GAIN_METRIC, 2, 0x0070);
	mt9m114_write(sd, REG_CAM_LL_STOP_GAIN_METRIC, 2, 0x00F3);

	mt9m114_write(sd, REG_CAM_LL_START_TARGET_LUMA_BM, 2, 0x0020);
	mt9m114_write(sd, REG_CAM_LL_STOP_TARGET_LUMA_BM, 2, 0x009A);

	mt9m114_write(sd, REG_CAM_LL_START_SATURATION, 1, 0x80);
	mt9m114_write(sd, REG_CAM_LL_END_SATURATION, 1, 0x4B);
	mt9m114_write(sd, REG_CAM_LL_START_DESATURATION, 1, 0x00);
	mt9m114_write(sd, REG_CAM_LL_END_DESATURATION, 1, 0xFF);

	mt9m114_write(sd, REG_CAM_LL_START_DEMOSAIC, 1, 0x1E);
	mt9m114_write(sd, REG_CAM_LL_START_AP_GAIN, 1, 0x02);
	mt9m114_write(sd, REG_CAM_LL_START_AP_THRESH, 1, 0x06);
	mt9m114_write(sd, REG_CAM_LL_STOP_DEMOSAIC, 1, 0x3C);
	mt9m114_write(sd, REG_CAM_LL_STOP_AP_GAIN, 1, 0x01);
	mt9m114_write(sd, REG_CAM_LL_STOP_AP_THRESH, 1, 0x0C);

	mt9m114_write(sd, REG_CAM_LL_START_NR_RED, 1, 0x3C);
	mt9m114_write(sd, REG_CAM_LL_START_NR_GREEN, 1, 0x3C);
	mt9m114_write(sd, REG_CAM_LL_START_NR_BLUE, 1, 0x3C);
	mt9m114_write(sd, REG_CAM_LL_START_NR_THRESH, 1, 0x0F);
	mt9m114_write(sd, REG_CAM_LL_STOP_NR_RED, 1, 0x64);
	mt9m114_write(sd, REG_CAM_LL_STOP_NR_GREEN, 1, 0x64);
	mt9m114_write(sd, REG_CAM_LL_STOP_NR_BLUE, 1, 0x64);
	mt9m114_write(sd, REG_CAM_LL_STOP_NR_THRESH, 1, 0x32);

	mt9m114_write(sd, REG_CAM_LL_START_CONTRAST_BM, 2, 0x0020);
	mt9m114_write(sd, REG_CAM_LL_STOP_CONTRAST_BM, 2, 0x009A);
	mt9m114_write(sd, REG_CAM_LL_GAMMA, 2, 0x00DC);
	mt9m114_write(sd, REG_CAM_LL_START_CONTRAST_GRADIENT, 1, 0x38);
	mt9m114_write(sd, REG_CAM_LL_STOP_CONTRAST_GRADIENT, 1, 0x30);
	mt9m114_write(sd, REG_CAM_LL_START_CONTRAST_LUMA_PERCENTAGE, 1, 0x50);
	mt9m114_write(sd, REG_CAM_LL_STOP_CONTRAST_LUMA_PERCENTAGE, 1, 0x19);

	mt9m114_write(sd, REG_CAM_LL_START_FADE_TO_BLACK_LUMA, 2, 0x0230);
	mt9m114_write(sd, REG_CAM_LL_STOP_FADE_TO_BLACK_LUMA, 2, 0x0010);

	mt9m114_write(sd, REG_CAM_LL_CLUSTER_DC_TH_BM, 2, 0x0800);

	mt9m114_write(sd, REG_CAM_LL_CLUSTER_DC_GATE_PERCENTAGE, 1, 0x05);

	mt9m114_write(sd, REG_CAM_LL_SUMMING_SENSITIVITY_FACTOR, 1, 0x40);

	mt9m114_write(sd, REG_CAM_AET_TARGET_AVERAGE_LUMA_DARK, 1, 0x1B);

	mt9m114_write(sd, REG_CAM_AET_AEMODE, 1, 0x0E);
	mt9m114_write(sd, REG_CAM_AET_TARGET_GAIN, 2, 0x0080);
	mt9m114_write(sd, REG_CAM_AET_AE_MAX_VIRT_AGAIN, 2, 0x0100);
	mt9m114_write(sd, REG_CAM_SENSOR_CFG_MAX_ANALOG_GAIN, 2, 0x01F8);

	mt9m114_write(sd, REG_CAM_AET_BLACK_CLIPPING_TARGET, 2, 0x005A);

	mt9m114_write(sd, REG_CCM_DELTA_GAIN, 1, 0x05);
	mt9m114_write(sd, REG_AE_TRACK_AE_TRACKING_DAMPENING_SPEED, 1, 0x20);
	return 0;
}

static int mt9m114_features(struct v4l2_subdev *sd)
{
	mt9m114_write(sd, REG_LOGICAL_ADDRESS_ACCESS, 2, 0x0000);
	mt9m114_write(sd, REG_CAM_PORT_OUTPUT_CONTROL, 2, 0x8040);
	mt9m114_write(sd, REG_PAD_SLEW_CONTROL, 2 , 0x0777);
	mt9m114_write_array(sd, mt9m114_fmt_yuv422);

	mt9m114_write(sd, REG_UVC_ALGO, 2 , 0x07);
	mt9m114_write(sd, REG_UVC_FRAME_INTERVAL, 4, 0x1e00);

	return 0;
}

/* Store information about the video data format.  The color matrix
 * is deeply tied into the format, so keep the relevant values here.
 * The magic matrix numbers come from OmniVision.*/
static struct mt9m114_format_struct {
	u32 pixelcode;
	enum v4l2_colorspace colorspace;
	struct regval_list *regs;
} mt9m114_formats[] = {
	{
		.pixelcode	= MEDIA_BUS_FMT_YUYV8_2X8,
		.colorspace = V4L2_COLORSPACE_JPEG,
		.regs	= mt9m114_fmt_yuv422,
	},
};
#define N_MT9M114_FMTS ARRAY_SIZE(mt9m114_formats)

/* window size */
enum e_win_size_format {
	WIN_SIZE_WXGA_FULL = 0,
	WIN_SIZE_WXGA,
	WIN_SIZE_VGA,
	WIN_SIZE_QVGA,
	WIN_SIZE_QQVGA,
	WIN_SIZE_ARRAY_SIZE
};

/* Set VGA as the default resolution */
#define WIN_DEFAULT_RESOLUTION WIN_SIZE_VGA

/* Then there is the issue of window sizes.  Try to capture the info here.*/
static struct mt9m114_win_size {
	int	width;
	int	height;
	int ll_corection;
	int   binned;
	struct regval_list *regs; /* Regs to tweak */
	/* h/vref stuff */
} mt9m114_win_sizes[WIN_SIZE_ARRAY_SIZE] = {

	/* 960p@28fps */
	[WIN_SIZE_WXGA_FULL] = {
		.width			= WXGA_WIDTH,
		.height			= FULL_HEIGHT,
		.ll_corection		= 0,
		.binned			= 0,
		.regs			= mt9m114_960p30_regs,
	},
	/* 720p@36fps */
	[WIN_SIZE_WXGA] = {
		.width			= WXGA_WIDTH,
		.height			= WXGA_HEIGHT,
		.ll_corection		= 0,
		.binned			= 0,
		.regs			= mt9m114_720p36_regs,
	},
	/* VGA@30fps scaling*/
	[WIN_SIZE_VGA] = {
		.width			= VGA_WIDTH,
		.height			= VGA_HEIGHT,
		.ll_corection		= 0,
		.binned			= 1,
		.regs			= mt9m114_vga_30_binned_regs,
	},
	/* QVGA@30fps scaling*/
	[WIN_SIZE_QVGA] = {
		.width			= QVGA_WIDTH,
		.height			= QVGA_HEIGHT,
		.ll_corection		= 1,
		.binned			= 1,
		.regs			= mt9m114_qvga_30_binned_regs,
	},
	/* QQVGA@30fps scaling*/
	[WIN_SIZE_QQVGA] = {
		.width			= QQVGA_HEIGHT,
		.height			= QQVGA_WIDTH,
		.ll_corection		= 1,
		.binned			= 1,
		.regs			= mt9m114_qqvga_30_binned_regs,
	},
};

#define CHECK_AND_EXIT(client, ret)						\
	do { if (ret) {								\
		v4l_err(client, "%s:%d ERROR (%d).\n", __func__, __LINE__, ret);\
		return ret;							\
	} } while (0)

static int mt9m114_init(struct v4l2_subdev *sd, u32 val)
{
	int ret = 0;
	struct mt9m114_info *info = to_state(sd);
	struct mt9m114_win_size win_size = mt9m114_win_sizes[info->wsize_format];
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	ret = mt9m114_reset(sd, 0);
	CHECK_AND_EXIT(client, ret);
	ret = mt9m114_PLL_settings(sd);
	CHECK_AND_EXIT(client, ret);

	ret = mt9m114_change_default_i2c_address(sd,
			MT9M114_I2C_ADDR_S0, MT9M114_I2C_ADDR_S1);
	CHECK_AND_EXIT(client, ret);

	ret = mt9m114_write_array(sd, win_size.regs);
	CHECK_AND_EXIT(client, ret);
	ret = mt9m114_sensor_optimization(sd);
	CHECK_AND_EXIT(client, ret);
	ret = mt9m114_errata_1(sd);
	CHECK_AND_EXIT(client, ret);
	ret = mt9m114_errata_2(sd);
	CHECK_AND_EXIT(client, ret);
	ret = mt9m114_write_array(sd, pga_regs);
	CHECK_AND_EXIT(client, ret);
	ret = mt9m114_write_array(sd, ccm_awb_regs);
	CHECK_AND_EXIT(client, ret);
	ret = mt9m114_CPIPE_preference(sd);
	CHECK_AND_EXIT(client, ret);
	ret = mt9m114_features(sd);
	CHECK_AND_EXIT(client, ret);
	ret = mt9m114_write_array(sd, uvc_ctrl_regs);
	CHECK_AND_EXIT(client, ret);

	if (camera_sync == 0) {
		ret = mt9m114_change_config(sd);
		CHECK_AND_EXIT(client, ret);
		ret = mt9m114_patch2_black_lvl_correction_fix(sd);
		CHECK_AND_EXIT(client, ret);
		ret = mt9m114_patch3_adaptive_sensitivity(sd);
		CHECK_AND_EXIT(client, ret);
		ret = mt9m114_set_state(sd, MT9M114_SYS_STATE_ENTER_SUSPEND,
				MT9M114_SYS_STATE_SUSPENDED);
		CHECK_AND_EXIT(client, ret);
	}
	return 0;
}

static int mt9m114_detect(struct v4l2_subdev *sd)
{
	u32 chip_id;
	u32 mon_major_version;
	u32 mon_minor_version;
	u32 mon_release_version;
	int ret;
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	ret = mt9m114_read(sd, REG_CHIP_ID, 2, &chip_id);
	if (ret) {
		v4l_dbg(1, debug, client, "chip id read failed (0x%x)\n", ret);
		return -ENODEV;
	}
	ret = mt9m114_read(sd, REG_MON_MAJOR_VERSION, 2, &mon_major_version);
	if (ret) {
		v4l_err(client, "%s: read REG_MON_MAJOR_VERSION failed (0x%x)\n", __func__, ret);
		return ret;
	}
	ret = mt9m114_read(sd, REG_MON_MINOR_VERION, 2, &mon_minor_version);
	if (ret) {
		v4l_err(client, "%s: read REG_MON_MINOR_VERION failed (0x%x)\n", __func__, ret);
		return ret;
	}
	ret = mt9m114_read(sd, REG_MON_RELEASE_VERSION, 2, &mon_release_version);
	if (ret) {
		v4l_err(client, "%s: read REG_MON_RELEASE_VERSION failed (0x%x)\n", __func__, ret);
		return ret;
	}

	if (chip_id != 0)
		v4l_dbg(0, debug, client, "found chip_id: 0x%x major: 0x%x minor: 0x%x release: 0x%x\n",
			chip_id, mon_major_version, mon_minor_version, mon_release_version);

	if (chip_id != 0x2481) /* default chip id*/
		return -ENODEV;

	// mt9m114 found, init it
	ret = mt9m114_init(sd, 0);
	if (ret) {
		struct i2c_client *client = v4l2_get_subdevdata(sd);
		v4l_err(client, "%s: initialization failed\n", __func__);
	}

	return ret;
}

static int mt9m114_enum_fmt(struct v4l2_subdev *sd, unsigned int index,
			    u32 *code) {
	if (index >= ARRAY_SIZE(mt9m114_formats))
		return -EINVAL;

	*code = mt9m114_formats[index].pixelcode;
	return 0;
}

/* Find a data format by a pixel code */
static struct mt9m114_format_struct *mt9m114_find_datafmt(u32 code) {
	int index;
	for (index = 0; index < ARRAY_SIZE(mt9m114_formats); index++)
		if (mt9m114_formats[index].pixelcode == code)
			return mt9m114_formats + index;

	/* default to first format */
	pr_debug("mt9m114: pixel fmt not supported 0x%x\n", code);
	return mt9m114_formats;
}

enum e_win_size_format mt9m114_find_datasize_format(int width, int height) {
	/* Round requested image size down to the nearest
	 * we support, but not below the smallest.*/
	int i;
	for (i = 0; i < WIN_SIZE_ARRAY_SIZE; i++) {
		struct mt9m114_win_size *wsize = &mt9m114_win_sizes[i];
		if (width >= wsize->width && height >= wsize->height)
			return i;
	}
	/* use VGA as default resolution */
	return WIN_DEFAULT_RESOLUTION;
}

/* updates fmt with the closest supported configuration */
static void mt9m114_try_fmt_internal(struct v4l2_subdev *sd,
				    struct v4l2_mbus_framefmt *fmt,
				    struct mt9m114_format_struct **ret_fmt,
				    enum e_win_size_format *ret_format)
{
	enum e_win_size_format wsize_format;
	struct mt9m114_format_struct *data_fmt;
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	v4l_dbg(2, debug, client, "try format (%d, %d, 0x%x, 0x%x)\n",
		fmt->width, fmt->height, fmt->code, fmt->colorspace);

	data_fmt = mt9m114_find_datafmt(fmt->code);
	if (ret_fmt != NULL)
		*ret_fmt = data_fmt;

	fmt->code = data_fmt->pixelcode;
	fmt->colorspace = data_fmt->colorspace;

	wsize_format = mt9m114_find_datasize_format(fmt->width, fmt->height);
	if (ret_format != NULL)
		*ret_format = wsize_format;

	/* Note the size we'll actually handle.*/
	fmt->width = mt9m114_win_sizes[wsize_format].width;
	fmt->height = mt9m114_win_sizes[wsize_format].height;
	fmt->field = V4L2_FIELD_NONE;

	v4l_dbg(2, debug, client, "final format (%d, %d, 0x%x, 0x%x)\n",
		fmt->width, fmt->height, fmt->code, fmt->colorspace);
}

static int mt9m114_set_format(struct v4l2_subdev *sd,
			   enum e_win_size_format wsize_format)
{
	int ret;
	u32 v = 0;
	struct mt9m114_win_size *wsize = &mt9m114_win_sizes[wsize_format];
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct mt9m114_info *info = to_state(sd);

	ret = mt9m114_write_array(sd, wsize->regs);
	if (ret) {
		v4l_err(client, "%s: regs write failed (%d)\n", __func__,
			wsize_format);
		return ret;
	}

	ret = mt9m114_read(sd, REG_LL_ALGO, 2, &v);
	if (ret) {
		v4l_err(client, "%s: read REG_LL_ALGO failed (%d)\n",
			__func__, ret);
		return ret;
	}

	if (wsize->ll_corection)
		v = v | LL_EXEC_DELTA_DK_CORRECTION;
	else
		v = v & (~LL_EXEC_DELTA_DK_CORRECTION);

	ret = mt9m114_write(sd, REG_LL_ALGO, 2, v);
	if (ret) {
		v4l_err(client, "%s: write REG_LL_ALGO failed (%d)\n",
			__func__, ret);
		return ret;
	}

	ret = mt9m114_read(sd, REG_CAM_SENSOR_CONTROL_READ_MODE, 2, &v);
	if (ret) {
		v4l_err(client, "%s: read REG_CAM_SENSOR_CONTROL_READ_MODE failed (%d)\n",
			__func__, ret);
		return ret;
	}

	if (wsize->binned)
		v = v | CAM_SENSOR_CONTROL_BINNING_EN;
	else
		v = v & (~CAM_SENSOR_CONTROL_BINNING_EN);

	ret = mt9m114_write(sd, REG_CAM_SENSOR_CONTROL_READ_MODE, 2, v);
	if (ret) {
		v4l_err(client, "%s: write REG_CAM_SENSOR_CONTROL_READ_MODE failed (%d)\n",
			__func__, ret);
		return ret;
	}

	info->wsize_format = wsize_format;
	return 0;
}

static int mt9m114_try_fmt(struct v4l2_subdev *sd, struct v4l2_mbus_framefmt *fmf)
{
	mt9m114_try_fmt_internal(sd, fmf, NULL, NULL);
	return 0;
}

/* Set a format.*/
static int mt9m114_s_fmt(struct v4l2_subdev *sd, struct v4l2_mbus_framefmt *fmt)
{
	int ret;
	enum e_win_size_format wsize_format;
	struct mt9m114_format_struct *mtfmt;
	struct mt9m114_info *info = to_state(sd);
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	v4l_dbg(1, debug, client, "%s entered in function \n", __func__);

	mt9m114_try_fmt_internal(sd, fmt, &mtfmt, &wsize_format);
	info->fmt = mtfmt;
	ret = mt9m114_set_format(sd, wsize_format);
	if (ret) {
		v4l_err(client, "%s: failed to set format\n", __func__);
		return ret;
	}

	ret = mt9m114_change_config(sd);
	if (ret) {
		v4l_err(client, "%s: change config failed (%d)\n",
			__func__, ret);
		return ret;
	}

	return 0;
}

/* Implement G/S_PARM.
 * There is a variable framerate available to do someday*/
static int mt9m114_g_parm(struct v4l2_subdev *sd, struct v4l2_streamparm *parms)
{
	struct v4l2_captureparm *cp = &parms->parm.capture;
	u32 v = 0;
	int ret = 0;

	if (parms->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;

	memset(cp, 0, sizeof(struct v4l2_captureparm));
	cp->capability = V4L2_CAP_TIMEPERFRAME;

	mt9m114_read(sd, REG_CAM_AET_MAX_FRAME_RATE, 2, &v);

	cp->timeperframe.numerator = 1;
	cp->timeperframe.denominator = v/MT9M114_FRAME_RATE;

	return ret;
}

static int mt9m114_s_parm(struct v4l2_subdev *sd, struct v4l2_streamparm *parms)
{
	struct v4l2_captureparm *cp = &parms->parm.capture;
	struct v4l2_fract *tpf = &cp->timeperframe;
	int div;

	if (parms->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;
	if (cp->extendedmode != 0)
		return -EINVAL;

	if (tpf->numerator == 0 || tpf->denominator == 0)
		div = MT9M114_FRAME_RATE * 30;	/* Reset to full rate */
	else
		div = MT9M114_FRAME_RATE * tpf->denominator / tpf->numerator;
	if (div == 0)
		div = MT9M114_FRAME_RATE * 30;  /* Reset to full rate */

	tpf->numerator = 1;
	tpf->denominator = div/MT9M114_FRAME_RATE;
	mt9m114_write(sd, REG_CAM_AET_MAX_FRAME_RATE, 2, div);
	mt9m114_write(sd, REG_CAM_AET_MIN_FRAME_RATE, 2, div);
	mt9m114_change_config(sd);

	return 0;
}

static int mt9m114_stream_on(struct v4l2_subdev *sd)
{
	int ret = mt9m114_set_state(sd, MT9M114_SYS_STATE_START_STREAMING,
				    MT9M114_SYS_STATE_STREAMING);
	if (ret) {
		struct i2c_client *client = v4l2_get_subdevdata(sd);
		v4l_err(client, "%s: Failed to start streaming (0x%x)\n",
			__func__, v4l2_i2c_subdev_addr(sd));
	}
	return ret;
}

static int mt9m114_stream_off(struct v4l2_subdev *sd)
{
	int ret = mt9m114_set_state(sd, MT9M114_SYS_STATE_ENTER_SUSPEND,
				    MT9M114_SYS_STATE_SUSPENDED);
	if (ret < 0) {
		struct i2c_client *client = v4l2_get_subdevdata(sd);
		v4l_err(client, "%s: Failed to stop streaming (0x%x)\n",
			__func__, v4l2_i2c_subdev_addr(sd));
	}
	return ret;
}

/* enable/disable data stream */
static int mt9m114_s_stream(struct v4l2_subdev *sd, int enable)
{
	int ret;
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	if (enable) {
		u32 state;

		ret = mt9m114_get_state(sd, &state);
		if (ret < 0) {
			v4l_err(client, "Can't read current state.\n");
			return ret;
		}
		if (state == MT9M114_SYS_STATE_START_STREAMING ||
		    state == MT9M114_SYS_STATE_STREAMING) {
			v4l_dbg(1, debug, client, "already streaming... 0x%x\n",
				state);
			return 0;
		}

		ret = mt9m114_stream_on(sd);
	} else {
		v4l_dbg(1, debug, client, "Stop Streaming... (suspend)\n");
		ret = mt9m114_stream_off(sd);
	}
	return ret;
}

static int mt9m114_g_stream(struct v4l2_subdev *sd, int *enable)
{
	u32 current_state = 0;
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	int ret = mt9m114_get_state(sd, &current_state);
	if (ret < 0) {
		v4l_err(client, "%s: Can't read current state.\n", __func__);
		return ret;
	}
	if (current_state == MT9M114_SYS_STATE_START_STREAMING ||
			current_state == MT9M114_SYS_STATE_STREAMING) {
		*enable = 1;
	} else {
		*enable = 0;
	}
	return 0;
}

static int mt9m114_s_sat(struct v4l2_subdev *sd, int value)
{
	int ret = mt9m114_write(sd, REG_UVC_SATURATION, 2, value);
	mt9m114_refresh(sd);
	return ret;
}

static int mt9m114_g_sat(struct v4l2_subdev *sd, __s32 *value)
{
	u32 v = 0;
	int ret = mt9m114_read(sd, REG_UVC_SATURATION, 2, &v);

	*value = v;
	return ret;
}


static int mt9m114_s_hue(struct v4l2_subdev *sd, int value)
{
	int ret;

	ret = mt9m114_write(sd, REG_UVC_HUE, 2, (s16)value*10);
	mt9m114_refresh(sd);
	return ret;
}

static int mt9m114_g_hue(struct v4l2_subdev *sd, __s32 *value)
{
	u32 v = 0;
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	int ret = mt9m114_read(sd, REG_UVC_HUE, 2, &v);

	*value = v/10;

	v4l_dbg(1, debug, client, "%s 0x%x\n", __func__, v);
	return ret;
}


static int mt9m114_s_brightness(struct v4l2_subdev *sd, int value)
{
	int ret = 0;
	ret = mt9m114_write(sd, REG_UVC_BRIGHTNESS, 2, value>>2);

	mt9m114_refresh(sd);
	return ret;
}

static int mt9m114_g_brightness(struct v4l2_subdev *sd, __s32 *value)
{
	u32 v = 0;
	int ret = mt9m114_read(sd, REG_UVC_BRIGHTNESS, 2, &v);

	*value = v<<2;
	return ret;
}


static int mt9m114_s_contrast(struct v4l2_subdev *sd, int value)
{
	int ret = mt9m114_write(sd, REG_UVC_CONTRAST, 2, value);
	mt9m114_refresh(sd);
	return ret;
}

static int mt9m114_g_contrast(struct v4l2_subdev *sd, __s32 *value)
{
	u32 v = 0;
	int ret = mt9m114_read(sd, REG_UVC_CONTRAST, 2, &v);

	*value = v;
	return ret;
}


static int mt9m114_g_hflip(struct v4l2_subdev *sd, __s32 *value)
{
	struct mt9m114_info *info = to_state(sd);

	*value = info->flag_hflip;
	return 0;
}

static int mt9m114_s_hflip(struct v4l2_subdev *sd, int value)
{
	struct mt9m114_info *info = to_state(sd);

	info->flag_hflip = value;
	mt9m114_change_config(sd);
	return 0;
}


static int mt9m114_g_vflip(struct v4l2_subdev *sd, __s32 *value)
{
	struct mt9m114_info *info = to_state(sd);
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	*value = info->flag_vflip;
	v4l_dbg(1, debug, client, "%s: %d\n", __func__, *value);
	return 0;
}

static int mt9m114_s_vflip(struct v4l2_subdev *sd, int value)
{
	struct mt9m114_info *info = to_state(sd);
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	v4l_dbg(1, debug, client, "%s: %d\n", __func__, value);

	info->flag_vflip = value;
	return mt9m114_change_config(sd);
}


static int mt9m114_s_sharpness(struct v4l2_subdev *sd, int value)
{
	int ret = mt9m114_write(sd, REG_UVC_SHARPNESS, 2, value);
	mt9m114_refresh(sd);
	return ret;
}

static int mt9m114_g_sharpness(struct v4l2_subdev *sd, __s32 *value)
{
	u32 v = 0;
	int ret = mt9m114_read(sd, REG_UVC_SHARPNESS, 2, &v);

	*value = v;
	return ret;
}


static int mt9m114_s_auto_white_balance(struct v4l2_subdev *sd, int value)
{
	int ret = 0;
	if (value == 0x01) {
		ret = mt9m114_write(sd, REG_AWB_AWB_MODE, 1, 0x02);
		ret += mt9m114_write(sd, REG_UVC_AUTO_WHITE_BALANCE_TEMPERATURE, 1,
			      value);
	} else {
		ret = mt9m114_write(sd, REG_AWB_AWB_MODE, 1, 0x00);
		ret += mt9m114_write(sd, REG_UVC_AUTO_WHITE_BALANCE_TEMPERATURE, 1,
			      value);
	}

	mt9m114_refresh(sd);
	return ret;
}

static int mt9m114_g_auto_white_balance(struct v4l2_subdev *sd, __s32 *value)
{
	u32 v = 0;
	int ret = mt9m114_read(sd, REG_UVC_AUTO_WHITE_BALANCE_TEMPERATURE, 1,
			       &v);

	*value = v;
	return ret;
}


static int mt9m114_s_backlight_compensation(struct v4l2_subdev *sd, int value)
{
	int ret = mt9m114_write(sd, REG_UVC_BACKLIGHT_COMPENSATION, 2, value);
	if (ret) {
		struct i2c_client *client = v4l2_get_subdevdata(sd);
		v4l_err(client, "write REG_UVC_BACKLIGHT_COMPENSATION failed (%d).\n", ret);
		return ret;
	}
	return mt9m114_change_config(sd);
}

static int mt9m114_g_backlight_compensation(struct v4l2_subdev *sd, __s32 *value)
{
	u32 v = 0;
	int ret = mt9m114_read(sd, REG_UVC_BACKLIGHT_COMPENSATION, 2, &v);

	*value = v;
	return ret;
}


static int mt9m114_s_auto_exposure(struct v4l2_subdev *sd, int value)
{
	int ret = 0;
	if (value == 0x01) {
		ret = mt9m114_write(sd, REG_UVC_MANUAL_EXPOSURE, 1, 0x00);
		ret += mt9m114_write(sd, REG_UVC_AE_MODE, 1, 0x02);
	} else {
		ret = mt9m114_write(sd, REG_UVC_AE_MODE, 1, 0x01);
		ret += mt9m114_write(sd, REG_UVC_MANUAL_EXPOSURE, 1, 0x01);
	}

	mt9m114_refresh(sd);
	return ret;
}

static int mt9m114_g_auto_exposure(struct v4l2_subdev *sd, __s32 *value)
{
	u32 v = 0;
	int ret = mt9m114_read(sd, REG_UVC_AE_MODE, 1, &v);

	if (v == 0x02) {
		*value = 0x01;
	} else {
		*value = 0x00;
	}

	return ret;
}

static int mt9m114_s_auto_exposure_algorithm(struct v4l2_subdev *sd, int value)
{
	int ret = 0;

	if (value >= 0x0 && value <= 0x3) {
		ret = mt9m114_write(sd, REG_AE_ALGORITHM+1, 1, value);
	} else {
		return -EINVAL;
	}

	mt9m114_refresh(sd);
	return ret;
}

static int mt9m114_g_auto_exposure_algorithm(struct v4l2_subdev *sd, __s32 *value)
{
	u32 v = 0;
	int ret = mt9m114_read(sd, REG_AE_ALGORITHM+1, 1, &v);
	*value = v & 0x3;

	return ret;
}

static int mt9m114_s_gain(struct v4l2_subdev *sd, int value)
{
	int ret = mt9m114_write(sd, REG_UVC_GAIN, 2, value);
	mt9m114_refresh(sd);

	return ret;
}

static int mt9m114_g_gain(struct v4l2_subdev *sd, __s32 *value)
{
	u32 v = 0;
	int ret = mt9m114_read(sd, REG_UVC_GAIN, 2, &v);

	*value = v;

	return ret;
}


static int mt9m114_s_exposure(struct v4l2_subdev *sd, int value)
{
	int ret = mt9m114_write(sd, REG_UVC_EXPOSURE_TIME, 4, value<<2);
	mt9m114_refresh(sd);
	return ret;
}

static int mt9m114_g_exposure(struct v4l2_subdev *sd, __s32 *value)
{
	u32 v = 0;
	int ret = mt9m114_read(sd, REG_UVC_EXPOSURE_TIME, 4, &v);
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	*value = v>>2;

	v4l_dbg(1, debug, client, "%s: 0x%x\n", __func__, v);

	return ret;
}


static int mt9m114_s_white_balance(struct v4l2_subdev *sd, int value)
{
	int ret = mt9m114_write(sd, REG_AWB_COLOR_TEMPERATURE, 2, value);
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	v4l_dbg(1, debug, client, "set white balance to %d\n", value);
	if (ret < 0) {
		v4l_err(client, "set white balance fail.\n");
		return ret;
	}

	return mt9m114_refresh(sd);
}

static int mt9m114_g_white_balance(struct v4l2_subdev *sd, __s32 *value)
{
	int ret = 0;
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	ret = mt9m114_refresh(sd);
	if (ret < 0) {
		v4l_err(client, "refresh before read white balance fail.\n");
		return ret;
	}

	ret = mt9m114_read(sd, REG_AWB_COLOR_TEMPERATURE, 2, value);
	if (ret < 0) {
		v4l_err(client, "read white balance fail.\n");
		return ret;
	}

	v4l_dbg(1, debug, client, "white balance is %d\n", *value);
	return 0;
}

static int mt9m114_start_synchronisation(struct v4l2_subdev *sd0,
		struct v4l2_subdev *sd1)
{
	int ret, pad_control;
	struct i2c_client *client0 = v4l2_get_subdevdata(sd0);
	struct i2c_client *client1 = v4l2_get_subdevdata(sd1);
	struct mt9m114_info *info = to_state(sd0);

	if (client0 == NULL || client1 == NULL) {
		pr_err("mt9m114: %s: Missing i2c client.", __func__);
		return -EINVAL;
	}

	v4l_info(client0, "%s: reset camera\n", __func__);
	ret = mt9m114_reset(sd0,0);
	CHECK_AND_EXIT(client0, ret);
	v4l_info(client1, "%s: reset camera\n", __func__);
	ret = mt9m114_reset(sd1,0);
	CHECK_AND_EXIT(client1, ret);

	v4l_info(client0, "%s: go standby\n", __func__);
	// 1. Go to standby for the Master if it is not already.
	ret = mt9m114_set_state(sd0, MT9M114_SYS_STATE_ENTER_STANDBY,
			MT9M114_SYS_STATE_STANDBY);
	CHECK_AND_EXIT(client0, ret);

	// 2. Go to standby for the Slave if it is not already.
	ret = mt9m114_set_state(sd1, MT9M114_SYS_STATE_ENTER_STANDBY,
			MT9M114_SYS_STATE_STANDBY);
	CHECK_AND_EXIT(client1, ret);

	v4l_info(client0, "%s: change addresses\n", __func__);
	// 3. Sync both streams and exit stand-by
	// 3.1 Configure as master (now using address A0)
	ret = mt9m114_change_default_i2c_address (sd0, client0->addr,
			client0->addr);
	CHECK_AND_EXIT(client0, ret);

	// 3.2 Configure as slave (now using address A6)
	ret = mt9m114_change_default_i2c_address (sd1, client1->addr,
			client1->addr);
	CHECK_AND_EXIT(client1, ret);

	v4l_info(client0, "%s: chain control\n", __func__);
	// 3.3 Set the chain control to enable TMS as an output (drives it low)
	ret = mt9m114_write(sd0, REG_CHAIN_CONTROL, 2, CHAIN_CONTROL_MASTER);
	CHECK_AND_EXIT(client0, ret);

	// 3.4 Enable PAD Control as flash output
	ret = mt9m114_read(sd0, REG_PAD_CONTROL, 2, &pad_control);
	CHECK_AND_EXIT(client0, ret);
	pad_control = pad_control | 0x0002; // enable
	ret = mt9m114_write(sd0, REG_PAD_CONTROL, 2, pad_control);
	CHECK_AND_EXIT(client0, ret);

	// 3.5 Set the chain control on slave
	ret = mt9m114_write(sd1, REG_CHAIN_CONTROL, 2, CHAIN_CONTROL_SLAVE);
	CHECK_AND_EXIT(client1, ret);

	v4l_info(client0, "%s: master configuration\n", __func__);
	// 4. Master settings
	// 4.1 Timing PLL
	ret = mt9m114_PLL_settings(sd0);
	CHECK_AND_EXIT(client0, ret);
	// 4.2 Errata and Sensor optimization Setting
	ret = mt9m114_set_format(sd0, info->wsize_format);
	CHECK_AND_EXIT(client0, ret);
	ret = mt9m114_errata_1(sd0);
	CHECK_AND_EXIT(client0, ret);
	ret = mt9m114_errata_2(sd0);
	CHECK_AND_EXIT(client0, ret);
	ret = mt9m114_sensor_optimization(sd0);
	CHECK_AND_EXIT(client0, ret);
	// 4.3 APGA
	ret = mt9m114_write_array(sd0, pga_regs);
	CHECK_AND_EXIT(client0, ret);
	// 4.4 AWB & CCM
	ret = mt9m114_write_array(sd0, ccm_awb_regs);
	CHECK_AND_EXIT(client0, ret);
	// 4.5 Color Pipe preference settings, if any
	ret = mt9m114_CPIPE_preference(sd0);
	CHECK_AND_EXIT(client0, ret);
	// 4.6 Ports, special features, etc.
	ret = mt9m114_features(sd0);
	CHECK_AND_EXIT(client0, ret);

	v4l_info(client0, "%s: slave configuration\n", __func__);
	// 4. Slave settings
	// 4.1 Timing PLL
	ret = mt9m114_PLL_settings(sd1);
	CHECK_AND_EXIT(client1, ret);
	// 4.2 Errata and Sensor optimization Setting
	ret = mt9m114_set_format(sd1, info->wsize_format);
	CHECK_AND_EXIT(client1, ret);
	ret = mt9m114_errata_1(sd1);
	CHECK_AND_EXIT(client1, ret);
	ret = mt9m114_errata_2(sd1);
	CHECK_AND_EXIT(client1, ret);
	ret = mt9m114_sensor_optimization(sd1);
	CHECK_AND_EXIT(client1, ret);
	// 4.3 APGA
	ret = mt9m114_write_array(sd1, pga_regs);
	CHECK_AND_EXIT(client1, ret);
	// 4.4 AWB & CCM
	ret = mt9m114_write_array(sd1, ccm_awb_regs);
	CHECK_AND_EXIT(client1, ret);
	// 4.5 Color Pipe preference settings, if any
	ret = mt9m114_CPIPE_preference(sd1);
	CHECK_AND_EXIT(client1, ret);
	// 4.6 Ports, special features, etc.
	ret = mt9m114_features(sd1);
	CHECK_AND_EXIT(client1, ret);

	v4l_info(client1, "%s: change config async\n", __func__);
	// 5. Go Config-change for Master and Slave
	// 5.1 send Change-Config to Slave (no-wait)
	ret = mt9m114_change_config_async(sd1);
	CHECK_AND_EXIT(client1, ret);
	v4l_info(client0, "%s: change config\n", __func__);
	// 5.2 send Change-Config to Master
	ret = mt9m114_change_config(sd0);
	CHECK_AND_EXIT(client0, ret);

	v4l_info(client0, "%s: go suspend\n", __func__);
	// 6. Go suspend for Master and Slave
	// 6.2 Put Slave into suspend state
	ret = mt9m114_set_state(sd1, MT9M114_SYS_STATE_ENTER_SUSPEND,
			MT9M114_SYS_STATE_SUSPENDED);
	CHECK_AND_EXIT(client1, ret);
	// 6.1 Put Master into suspend state
	ret = mt9m114_set_state(sd0, MT9M114_SYS_STATE_ENTER_SUSPEND,
			MT9M114_SYS_STATE_SUSPENDED);
	CHECK_AND_EXIT(client0, ret);

	v4l_info(client0, "%s: load patches\n", __func__);
	// 7. Load Patches
	// 7.1 Master first
	ret = mt9m114_patch2_black_lvl_correction_fix(sd0);
	CHECK_AND_EXIT(client0, ret);
	ret = mt9m114_patch3_adaptive_sensitivity(sd0);
	CHECK_AND_EXIT(client0, ret);
	// 7.2 Slave first
	ret = mt9m114_patch2_black_lvl_correction_fix(sd1);
	CHECK_AND_EXIT(client1, ret);
	ret = mt9m114_patch3_adaptive_sensitivity(sd1);
	CHECK_AND_EXIT(client1, ret);

	v4l_info(client1, "%s: change config async\n", __func__);
	// 8. Config as master/slave and run
	// 8.1 send Change-Config to Slave (no-wait)
	ret = mt9m114_change_config_async(sd1);
	CHECK_AND_EXIT(client1, ret);

	// 8.2 send Change-Config to Master
	ret = mt9m114_change_config(sd0);
	CHECK_AND_EXIT(client0, ret);

	/* the only way to enter stereo mode is pass here */
	to_state(sd0)->stereo_on = 1;
	to_state(sd1)->stereo_on = 1;
	return 0;
}

static int mt9m114_stop_synchronisation(struct v4l2_subdev *sd0,
		struct v4l2_subdev *sd1)
{
	int ret, state0, state1;
	struct i2c_client *client0 = v4l2_get_subdevdata(sd0);
	struct i2c_client *client1 = v4l2_get_subdevdata(sd1);

	if (client0 == NULL || client1 == NULL) {
		pr_err("mt9m114: %s: Missing i2c client.", __func__);
		return -EINVAL;
	}

	// save cameras state
	ret = mt9m114_g_stream(sd0, &state0);
	CHECK_AND_EXIT(client0, ret);
	ret = mt9m114_g_stream(sd1, &state1);
	CHECK_AND_EXIT(client0, ret);

	v4l_info(client0, "%s: reset cameras\n", __func__);

	// reset the master camera first
	ret = mt9m114_init(sd0, 0);
	CHECK_AND_EXIT(client0, ret);

	// reset the slave camera
	ret = mt9m114_init(sd1, 0);
	CHECK_AND_EXIT(client1, ret);

	// restore cameras state
	mt9m114_s_stream(sd0, state0);
	CHECK_AND_EXIT(client0, ret);
	mt9m114_s_stream(sd1, state1);
	CHECK_AND_EXIT(client1, ret);

	return 0;
}

static int mt9m114_s_synchronisation(struct v4l2_subdev *sd, int enable)
{
	int ret;
	struct v4l2_subdev *sdx;
	struct v4l2_subdev *sd0 = NULL;
	struct v4l2_subdev *sd1 = NULL;

	struct i2c_client *client = v4l2_get_subdevdata(sd);
	v4l_info(client, "enter %s\n", __func__);

	/* look the device to prevent concurrent configuration */
	spin_lock(&sd->v4l2_dev->lock);
	/* look for the two subdevices */
	v4l2_device_for_each_subdev(sdx, sd->v4l2_dev) {
		unsigned short addr = v4l2_i2c_subdev_addr(sdx);
		if (addr == MT9M114_I2C_ADDR_S0) {
			sd0 = sdx;
		} else if (addr == MT9M114_I2C_ADDR_S1) {
			sd1 = sdx;
		}
	}
	spin_unlock(&sd->v4l2_dev->lock);

	if (sd0 == NULL) {
		v4l_err(client, "%s: missing master device\n", __func__);
		return -EINVAL;
	} else if (sd1 == NULL) {
		v4l_err(client, "%s: missing slave device\n", __func__);
		return -EINVAL;
	}

	if (enable) {
		ret = mt9m114_start_synchronisation(sd0, sd1);
		if (ret) {
			v4l_err(client, "%s: failed to start synchro\n",
				__func__);
			ret = mt9m114_init(sd0, 0);
			if (ret) v4l_err(client, "%s: failed to reset master sensor\n",
					 __func__);
			ret = mt9m114_init(sd1, 0);
			if (ret) v4l_err(client, "%s: failed to reset slave sensor\n",
					 __func__);
			return -EIO;
		}
	} else {
		ret = mt9m114_stop_synchronisation(sd0, sd1);
		if (ret) {
			v4l_err(client, "%s: failed to stop synchro\n", __func__);
			return -EIO;
		}
	}

	return 0;
}

static int mt9m114_g_synchronisation(struct v4l2_subdev *sd, __s32 *value)
{
	int ret = 0;
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	ret = mt9m114_refresh(sd);
	if (ret < 0) {
		v4l_err(client, "refresh before read synchronisation failed\n");
		return ret;
	}

	ret = mt9m114_read(sd, REG_CHAIN_CONTROL, 2, value);
	v4l_dbg(2, debug, client, "get REG_CHAIN_CONTROL: 0x%x\n", *value);

	if (*value) *value = 1;
	else *value = 0;

	return ret;
}

static int mt9m114_cropcap(struct v4l2_subdev *sd, struct v4l2_cropcap *a)
{
	return 0;
}

static int mt9m114_g_crop(struct v4l2_subdev *sd, struct v4l2_crop *a)
{
	return 0;
}

/* controls */
enum e_ctrl {
	CTRL_EXPOSURE_AUTO,
	CTRL_EXPOSURE_METERING,
	CTRL_EXPOSURE_ALGO,
	CTRL_BRIGHTNESS,
	CTRL_CONTRAST,
	CTRL_SATURATION,
	CTRL_VFLIP,
	CTRL_HFLIP,
	CTRL_SHARPNESS,
	CTRL_WHITE_BALANCE_AUTO,
	CTRL_GAIN,
	CTRL_EXPOSURE,
	CTRL_WHITE_BALANCE,
	CTRL_BACKLIGHT_COMPENSATION,
	CTRL_HUE,
	CTRL_STEREO,
	NSTD_CTRLS		/* number of controls */
};

struct control_params {
	u32 id;
	s32 min;
	s32 max;
	u32 step;
	s32 mask;
	s32 def;
	bool is_menu;
	bool is_volatile;
};

struct control_params ctrl_list[] = {
	[CTRL_EXPOSURE_AUTO] = {
		.id = V4L2_CID_EXPOSURE_AUTO,
		.max = V4L2_EXPOSURE_MANUAL,
		.mask = ~((1 << V4L2_EXPOSURE_AUTO) | (1 << V4L2_EXPOSURE_MANUAL)),
		.def = V4L2_EXPOSURE_AUTO,
		.is_menu = true,
		.is_volatile = false,
	},
	[CTRL_EXPOSURE_METERING] = {
		.id = V4L2_CID_EXPOSURE_METERING,
		.max = V4L2_EXPOSURE_METERING_MATRIX,
		.mask = ~((1 << V4L2_EXPOSURE_METERING_AVERAGE) |
			  (1 << V4L2_EXPOSURE_METERING_CENTER_WEIGHTED) |
			  (1 << V4L2_EXPOSURE_METERING_SPOT) |
			  (1 << V4L2_EXPOSURE_METERING_MATRIX)),
		.def = V4L2_EXPOSURE_METERING_CENTER_WEIGHTED,
		.is_menu = true,
		.is_volatile = false,
	},
	[CTRL_EXPOSURE_ALGO] = { // deprecated: use V4L2_CID_EXPOSURE_METERING
		.id = V4L2_CID_EXPOSURE_ALGORITHM,
		.max = V4L2_EXPOSURE_METERING_MATRIX,
		.mask = ~((1 << V4L2_EXPOSURE_METERING_AVERAGE) |
			  (1 << V4L2_EXPOSURE_METERING_CENTER_WEIGHTED) |
			  (1 << V4L2_EXPOSURE_METERING_SPOT) |
			  (1 << V4L2_EXPOSURE_METERING_MATRIX)),
		.def = V4L2_EXPOSURE_METERING_CENTER_WEIGHTED,
		.is_menu = true,
		.is_volatile = false,
	},
	[CTRL_BRIGHTNESS] = {
		.id = V4L2_CID_BRIGHTNESS,
		.min = 0,
		.max = 255,
		.step = 1,
		.def = 55,
		.is_menu = false,
		.is_volatile = false,
	},
	[CTRL_CONTRAST] = {
		.id = V4L2_CID_CONTRAST,
		.min = 0,
		.max = 127,
		.step = 1,
		.def = 32,
		.is_menu = false,
		.is_volatile = false,
	},
	[CTRL_SATURATION] = {
		.id = V4L2_CID_SATURATION,
		.min = 0,
		.max = 255,
		.step = 1,
		.def = 128,
		.is_menu = false,
		.is_volatile = false,
	},
	[CTRL_HUE] = {
		.id = V4L2_CID_HUE,
		.min = -180,
		.max = 180,
		.step = 1,
		.def = 0,
		.is_menu = false,
		.is_volatile = false,
	},
	[CTRL_VFLIP] = {
		.id = V4L2_CID_VFLIP,
		.min = 0,
		.max = 1,
		.step = 1,
		.def = 0,
		.is_menu = false,
		.is_volatile = false,
	},
	[CTRL_HFLIP] = {
		.id = V4L2_CID_HFLIP,
		.min = 0,
		.max = 1,
		.step = 1,
		.def = 0,
		.is_menu = false,
		.is_volatile = false,
	},
	[CTRL_SHARPNESS] = {
		.id = V4L2_CID_SHARPNESS,
		.min = 0, /* -7 is to ensure no sharpness */
		.max = 7,
		.step = 1,
		.def = 0,
		.is_menu = false,
		.is_volatile = false,
	},
	[CTRL_WHITE_BALANCE_AUTO] = {
		.id = V4L2_CID_AUTO_WHITE_BALANCE,
		.min = 0,
		.max = 1,
		.step = 1,
		.def = 1,
		.is_menu = false,
		.is_volatile = false,
	},
	[CTRL_GAIN] = {
		.id = V4L2_CID_GAIN,
		.min = 32,
		.max = 255,
		.step = 1,
		.def = 32,
		.is_menu = false,
		.is_volatile = false,
	},
	[CTRL_EXPOSURE] = {
		.id = V4L2_CID_EXPOSURE,
		.min = 0,
		.max = 512,
		.step = 1,
		.def = 0,
		.is_menu = false,
		.is_volatile = false,
	},
	[CTRL_WHITE_BALANCE] = {
		.id = V4L2_CID_DO_WHITE_BALANCE,
		.min = 0, /* initialize with mt9m114_set_white_balance_range() */
		.max = 6500, /* initialize with mt9m114_set_white_balance_range() */
		.step = 1,
		.def = 6500,
		.is_menu = false,
		.is_volatile = true,
	},
	[CTRL_BACKLIGHT_COMPENSATION] = {
		.id = V4L2_CID_BACKLIGHT_COMPENSATION,
		.min = 0,
		.max = 4,
		.step = 1,
		.def = 1,
		.is_menu = false,
		.is_volatile = false,
	},
	[CTRL_STEREO] = {
		.id = CID_STEREO,
		.min = 0,
		.max = 1,
		.step = 1,
		.def = 0,
		.is_menu = false,
		.is_volatile = false,
	}
};

static int mt9m114_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct mt9m114_info *info = container_of(ctrl->handler,
						 struct mt9m114_info, ctrl_handler);
	struct v4l2_subdev *sd = &info->sd;
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	v4l_dbg(1, debug, client, "%s id: 0x%x val: 0x%x\n",
		__func__, ctrl->id, ctrl->val);

	switch (ctrl->id) {
	case V4L2_CID_BRIGHTNESS:
		return mt9m114_s_brightness(sd, ctrl->val);
	case V4L2_CID_CONTRAST:
		return mt9m114_s_contrast(sd, ctrl->val);
	case V4L2_CID_SATURATION:
		return mt9m114_s_sat(sd, ctrl->val);
	case V4L2_CID_HUE:
		return mt9m114_s_hue(sd, ctrl->val);
	case V4L2_CID_VFLIP:
		return mt9m114_s_vflip(sd, ctrl->val);
	case V4L2_CID_HFLIP:
		return mt9m114_s_hflip(sd, ctrl->val);
	case V4L2_CID_SHARPNESS:
		return mt9m114_s_sharpness(sd, ctrl->val);
	case V4L2_CID_EXPOSURE_ALGORITHM:
		return mt9m114_s_auto_exposure_algorithm(sd, ctrl->val);
	case V4L2_CID_EXPOSURE_AUTO:
		return mt9m114_s_auto_exposure(sd, ctrl->val);
	case V4L2_CID_EXPOSURE_METERING:
		return mt9m114_s_auto_exposure_algorithm(sd, ctrl->val);
	case V4L2_CID_AUTO_WHITE_BALANCE:
		return mt9m114_s_auto_white_balance(sd, ctrl->val);
	case V4L2_CID_GAIN:
		return mt9m114_s_gain(sd, ctrl->val);
	case V4L2_CID_EXPOSURE:
		return mt9m114_s_exposure(sd, ctrl->val);
	case V4L2_CID_DO_WHITE_BALANCE:
		return mt9m114_s_white_balance(sd, ctrl->val);
	case V4L2_CID_BACKLIGHT_COMPENSATION:
		return mt9m114_s_backlight_compensation(sd, ctrl->val);
	case CID_STEREO:
		return mt9m114_s_synchronisation(sd, ctrl->val);
	}
	return -EINVAL;
}

static int mt9m114_g_ctrl(struct v4l2_ctrl *ctrl)
{
	struct mt9m114_info *info = container_of(ctrl->handler,
						 struct mt9m114_info, ctrl_handler);
	struct v4l2_subdev *sd = &info->sd;
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	v4l_dbg(1, debug, client, "%s id: 0x%x\n", __func__, ctrl->id);

	switch (ctrl->id) {
	case V4L2_CID_BRIGHTNESS:
		return mt9m114_g_brightness(sd, &ctrl->val);
	case V4L2_CID_CONTRAST:
		return mt9m114_g_contrast(sd, &ctrl->val);
	case V4L2_CID_SATURATION:
		return mt9m114_g_sat(sd, &ctrl->val);
	case V4L2_CID_HUE:
		return mt9m114_g_hue(sd, &ctrl->val);
	case V4L2_CID_VFLIP:
		return mt9m114_g_vflip(sd, &ctrl->val);
	case V4L2_CID_HFLIP:
		return mt9m114_g_hflip(sd, &ctrl->val);
	case V4L2_CID_SHARPNESS:
		return mt9m114_g_sharpness(sd, &ctrl->val);
	case V4L2_CID_EXPOSURE_ALGORITHM:
		return mt9m114_g_auto_exposure_algorithm(sd, &ctrl->val);
	case V4L2_CID_EXPOSURE_AUTO:
		return mt9m114_g_auto_exposure(sd, &ctrl->val);
	case V4L2_CID_EXPOSURE_METERING:
		return mt9m114_g_auto_exposure_algorithm(sd, &ctrl->val);
	case V4L2_CID_AUTO_WHITE_BALANCE:
		return mt9m114_g_auto_white_balance(sd, &ctrl->val);
	case V4L2_CID_GAIN:
		return mt9m114_g_gain(sd, &ctrl->val);
	case V4L2_CID_EXPOSURE:
		return mt9m114_g_exposure(sd, &ctrl->val);
	case V4L2_CID_DO_WHITE_BALANCE:
		return mt9m114_g_white_balance(sd, &ctrl->val);
	case V4L2_CID_BACKLIGHT_COMPENSATION:
		return mt9m114_g_backlight_compensation(sd, &ctrl->val);
	case CID_STEREO:
		return mt9m114_g_synchronisation(sd, &ctrl->val);
	}
	return -EINVAL;
}

static const struct v4l2_ctrl_ops mt9m114_ctrl_ops = {
	.s_ctrl = mt9m114_s_ctrl,
	.g_volatile_ctrl = mt9m114_g_ctrl,
};

static int mt9m114_set_white_balance_range(struct v4l2_subdev *sd)
{
	int ret, min, max;
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	ret = mt9m114_read(sd, REG_AWB_MIN_TEMPERATURE, 2, &min);
	if (ret < 0) {
		v4l_err(client, "read white balance min fail.\n");
		return ret;
	}
	ret = mt9m114_read(sd, REG_AWB_MAX_TEMPERATURE, 2, &max);
	if (ret < 0) {
		v4l_err(client, "read white balance max fail.\n");
		return ret;
	}
	ctrl_list[CTRL_WHITE_BALANCE].min = min;
	ctrl_list[CTRL_WHITE_BALANCE].max = max;
	return 0;
}

static int mt9m114_register_ctrl(struct v4l2_ctrl_handler *hl,
				 struct control_params* control) {
	struct v4l2_ctrl *ctrl;
	if (control->is_menu) {
		ctrl = v4l2_ctrl_new_std_menu(hl, &mt9m114_ctrl_ops,
					      control->id, control->max,
					      control->mask, control->def);
	} else {
		ctrl = v4l2_ctrl_new_std(hl, &mt9m114_ctrl_ops, control->id,
					 control->min, control->max, control->step,
					 control->def);
	}
	if (ctrl == NULL) {
		int err = hl->error;
		pr_err("mt9m114: control id %d error: 0x%x", control->id, err);
		return err;
	}
	if (control->is_volatile) {
		ctrl->flags |= V4L2_CTRL_FLAG_VOLATILE;
	}
	return 0;
}


static int mt9m114_ctrl_registration(struct v4l2_ctrl_handler *ctrl_handler)
{
	int i;

	for (i = 0; i < NSTD_CTRLS; i++) {
		int rc = mt9m114_register_ctrl(ctrl_handler, &ctrl_list[i]);
		if (rc)
			return rc;
	}

	return 0;
}

#ifdef CONFIG_VIDEO_ADV_DEBUG
static int mt9m114_g_register(struct v4l2_subdev *sd, struct v4l_dbg_register *reg)
{
	int ret = 0;
	u16 addr = reg->reg & 0xffff;
	u16 size = 1;
	u32 val = 0;
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	ret = mt9m114_read(sd, addr, size, &val);
	reg->val = val;

	v4l_dbg(1, debug, client, "%s addr: 0x%x, size: %d, value: 0x%x\n",
		__func__, addr, size, val);

	return ret;
}

static int mt9m114_s_register(struct v4l2_subdev *sd, struct v4l_dbg_register *reg)
{
	int ret = 0;
	u16 addr = reg->reg & 0xffff;
	u16 size = 1;
	u32 val = reg->val & 0xff;
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	v4l_dbg(1, debug, client, "%s addr: 0x%x, size: %d, val: 0x%x\n",
		__func__, addr, size, val);

	ret = mt9m114_write(sd, addr, size, val);
	mt9m114_refresh(sd);
	return ret;
}
#endif

/* ----------------------------------------------------------------------- */

static const struct v4l2_subdev_core_ops mt9m114_core_ops = {
	.queryctrl = v4l2_subdev_queryctrl,
	.querymenu = v4l2_subdev_querymenu,
	.g_ctrl = v4l2_subdev_g_ctrl,
	.s_ctrl = v4l2_subdev_s_ctrl,
	.g_ext_ctrls = v4l2_subdev_g_ext_ctrls,
	.try_ext_ctrls = v4l2_subdev_try_ext_ctrls,
	.s_ext_ctrls = v4l2_subdev_s_ext_ctrls,
	.reset = mt9m114_reset,
	.init = mt9m114_init,
#ifdef CONFIG_VIDEO_ADV_DEBUG
	.g_register = mt9m114_g_register,
	.s_register = mt9m114_s_register,
#endif
};

/* returns true if fmt is the current used format */
static int mt9m114_is_fmt(struct v4l2_subdev *sd, struct v4l2_mbus_framefmt *fmt)
{
	enum e_win_size_format wsize_format;
	struct mt9m114_format_struct *mtfmt;
	struct mt9m114_info *info = to_state(sd);

	mt9m114_try_fmt_internal(sd, fmt, &mtfmt, &wsize_format);
	if (info->fmt == mtfmt && info->wsize_format == wsize_format) {
		return 1;
	}
	return 0;
}

/* stereo wrapper for resolution change: when playing stereo stream
 * resolution change is not allowed */
static int mt9m114_stereo_s_fmt(struct v4l2_subdev *sd, struct v4l2_mbus_framefmt *fmt)
{
	int ret, restart_synchro;
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	/* same frame format */
	if (mt9m114_is_fmt(sd, fmt)) {
		return 0;
	}

	/* shall the synchro be stopped and restarted */
	restart_synchro = to_state(sd)->stereo_on;

	/* if synchro, stop the synchronization first */
	if (restart_synchro) {
		ret = mt9m114_s_synchronisation(sd, 0);
		if (ret) {
			v4l_err(client, "%s: failed to stop synchro\n", __func__);
			return ret;
		}
	}
	ret = mt9m114_s_fmt(sd, fmt);
	if (ret) {
		v4l_err(client, "%s: failed to set format\n", __func__);
		return ret;
	}
	/* if synchro, restart the synchronization */
	if (restart_synchro) {
		ret = mt9m114_s_synchronisation(sd, 1);
		if (ret) {
			v4l_err(client, "%s: failed to start synchro\n", __func__);
			return ret;
		}
	}
	return 0;
}

/* stereo wrapper for frame rate change: when playing stereo stream
 * framerate change is not allowed */
static int mt9m114_stereo_s_parm(struct v4l2_subdev *sd, struct v4l2_streamparm *parms)
{
	if (to_state(sd)->stereo_on) {
		struct i2c_client *client = v4l2_get_subdevdata(sd);
		v4l_err(client, "can not change framerate: stereo playing\n");
		return -EBUSY;
	}
	return mt9m114_s_parm(sd, parms);
}

/* stereo wrapper for stream on/off: when playing stereo stream
 * stop both sensor together */
static int mt9m114_stereo_s_stream(struct v4l2_subdev *sd, int enable)
{
	/* when stopping while playing stereo, stop the stereo */
	if (to_state(sd)->stereo_on && enable == 0) {
		mt9m114_s_synchronisation(sd, 0);
	}
	return mt9m114_s_stream(sd, enable);
}

static const struct v4l2_subdev_video_ops mt9m114_video_ops = {
	.enum_mbus_fmt = mt9m114_enum_fmt,
	.try_mbus_fmt = mt9m114_try_fmt,
	.s_mbus_fmt = mt9m114_stereo_s_fmt, /* stereo wrapper (set resolution and colorspace) */
	.cropcap = mt9m114_cropcap,
	.g_crop = mt9m114_g_crop,
	.s_parm = mt9m114_stereo_s_parm, /* stereo wrapper (set framerate) */
	.g_parm = mt9m114_g_parm,
	.s_stream = mt9m114_stereo_s_stream, /* stereo wrapper (stream on/off) */
};

static const struct v4l2_subdev_ops mt9m114_ops = {
	.core = &mt9m114_core_ops,
	.video = &mt9m114_video_ops,
};

/* ----------------------------------------------------------------------- */

static int mt9m114_probe(struct i2c_client *client,
			 const struct i2c_device_id *id)
{
	struct v4l2_subdev *sd;
	struct mt9m114_info *info;
	int ret;

	info = kzalloc(sizeof(struct mt9m114_info), GFP_KERNEL);
	if (info == NULL)
		return -ENOMEM;

	/* set default resolution to VGA */
	info->wsize_format = WIN_SIZE_VGA;
	sd = &info->sd;
	v4l2_i2c_subdev_init(sd, client, &mt9m114_ops);

	/* Make sure it's an mt9m114 */
	ret = mt9m114_detect(sd);
	if (ret) {
		v4l_dbg(1, debug, client,
			"chip found @ 0x%x (%s) is not an mt9m114 chip.\n",
			client->addr , client->adapter->name);
		kfree(info);
		return ret;
	}

	ret = v4l2_ctrl_handler_init(&info->ctrl_handler, NSTD_CTRLS);
	if (ret) {
		v4l_err(client, "%s: failed to init ctrl handler for V4L2 sub device\n",
			__func__);
		kfree(info);
		return ret;
	}

	ret = mt9m114_set_white_balance_range(sd);
	if (ret) {
		v4l_err(client, "%s: set white balance temperature failed\n",
			__func__);
		v4l2_ctrl_handler_free(&info->ctrl_handler);
		kfree(info);
		return ret;
	}
	ret = mt9m114_ctrl_registration(&info->ctrl_handler);
	if (ret) {
		v4l_err(client, "%s: failed to add standard controls for V4L2 sub device\n",
			__func__);
		v4l2_ctrl_handler_free(&info->ctrl_handler);
		kfree(info);
		return ret;
	}
	sd->ctrl_handler = &info->ctrl_handler;

	v4l_info(client, "chip found @ 0x%02x (%s)\n",
		 client->addr , client->adapter->name);

	info->fmt = &mt9m114_formats[0];
	info->flag_hflip = 0;
	info->flag_vflip = 0;

	return 0;
}


static int mt9m114_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct mt9m114_info *info = to_state(sd);

	v4l2_device_unregister_subdev(sd);
	v4l2_ctrl_handler_free(&info->ctrl_handler);
	kfree(info);
	return 0;
}

static const struct i2c_device_id mt9m114_id[] = {
	{ "mt9m114", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, mt9m114_id);

static struct i2c_driver mt9m114_i2c_driver = {
	.driver = {
		.name = "mt9m114",
	},
	.probe	= mt9m114_probe,
	.remove	= mt9m114_remove,
	.id_table	= mt9m114_id,
};

static int __init mt9m114_mod_init(void)
{
	return i2c_add_driver(&mt9m114_i2c_driver);
}

static void __exit mt9m114_mod_exit(void)
{
	i2c_del_driver(&mt9m114_i2c_driver);
}

module_init(mt9m114_mod_init);
module_exit(mt9m114_mod_exit);
