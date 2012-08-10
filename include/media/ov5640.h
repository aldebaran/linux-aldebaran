#ifndef _MEDIA_OV5640_H
#define _MEDIA_OV5640_H

#include <media/v4l2-subdev.h>

struct ov5640_platform_data {
	const char *reg_avdd; /* Analog power regulator id */
	const char *reg_dovdd; /* I/O power regulator id */

	const char *clk_xvclk; /* System clock id */

	int gpio_pwdn; /* Power down GPIO number */
	int is_gpio_pwdn_acthi; /* Is Power down GPIO active high? */
	int gpio_resetb; /* Reset GPIO number */
	int is_gpio_resetb_acthi; /* Is Reset GPIO active high? */

	int (*pre_poweron)(struct v4l2_subdev *subdev);
	int (*post_poweroff)(struct v4l2_subdev *subdev);
};

#endif
