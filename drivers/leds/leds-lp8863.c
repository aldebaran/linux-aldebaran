/*
 * TI LP8860 4-Channel LED Driver
 *
 * Copyright (C) 2014 Texas Instruments
 *
 * Author: Dan Murphy <dmurphy@ti.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 */

#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/leds.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/gpio/consumer.h>
#include <linux/slab.h>
#include <uapi/linux/uleds.h>

/********** LP8863 **********/
#define LP8863_BL_MODE                  0x20
#define LP8863_DISP_BRT                 0x28
#define LP8863_INTERRUPT_STATUS_1       0x54
#define LP8863_INTERRUPT_STATUS_2       0x56
#define LP8863_INTERRUPT_STATUS_3       0x58
#define LP8863_GROUPING1                0x30
#define LP8863_GROUPING2                0x32
#define LP8863_USER_CONFIG1             0x40
#define LP8863_USER_CONFIG2             0x42
#define LP8863_INTERRUPT_ENABLE_1       0x50
#define LP8863_INTERRUPT_ENABLE_2       0x52
#define LP8863_INTERRUPT_ENABLE_3       0x4e
#define LP8863_JUNCTION_TEMPERATURE     0xe8
#define LP8863_TEMPERATURE_LIMIT_HIGH   0xec
#define LP8863_TEMPERATURE_LIMIT_LOW    0xee
#define LP8863_CLUSTER1_BRT             0x13c
#define LP8863_CLUSTER2_BRT             0x148
#define LP8863_CLUSTER3_BRT             0x154
#define LP8863_CLUSTER4_BRT             0x160
#define LP8863_CLUSTER5_BRT             0x16c
#define LP8863_BRT_DB_CONTROL           0x178
#define LP8863_LED0_CURRENT             0x1c2
#define LP8863_LED1_CURRENT             0x1c4
#define LP8863_LED2_CURRENT             0x1c6
#define LP8863_LED3_CURRENT             0x1c8
#define LP8863_LED4_CURRENT             0x1ca
#define LP8863_LED5_CURRENT             0x1cc
#define LP8863_BOOST_CONTROL            0x288
#define LP8863_SHORT_THRESH             0x28a
#define LP8863_FSM_DIAGNOSTICS          0x2a4
#define LP8863_PWM_INPUT_DIAGNOSTICS    0x2a6
#define LP8863_PWM_OUTPUT_DIAGNOSTICS   0x2a8
#define LP8863_LED_CURR_DIAGNOSTICS     0x2aa
#define LP8863_ADAPT_BOOST_DIAGNOSTICS  0x2ac
#define LP8863_AUTO_DETECT_DIAGNOSTICS  0x2ae

/**
 * struct lp8860_led -
 * @lock - Lock for reading/writing the device
 * @client - Pointer to the I2C client
 * @led_dev - led class device pointer
 * @regmap - Devices register map
 * @eeprom_regmap - EEPROM register map
 * @enable_gpio - VDDIO/EN gpio to enable communication interface
 * @regulator - LED supply regulator pointer
 * @label - LED label
 */
struct lp8863_led {
	struct mutex lock;
	struct i2c_client *client;
	struct led_classdev led_dev;
	struct regmap *regmap;
	struct gpio_desc *enable_gpio;
	struct regulator *regulator;
	char label[LED_MAX_NAME_SIZE];
};

static int lp8863_fault_check(struct lp8863_led *led)
{
	int ret;
//	int fault;
	unsigned int read_buf;

	printk("SBRE %s\n", __func__);

	ret = regmap_read(led->regmap, LP8863_INTERRUPT_STATUS_1, &read_buf);
//	printk("%s: ret=%d, LP8863_INTERRUPT_STATUS_1 fault=%d\n", __func__, ret, read_buf);
	if (ret)
		goto out;

	ret = regmap_read(led->regmap, LP8863_INTERRUPT_STATUS_2, &read_buf);
//	printk("%s: ret=%d, LP8863_INTERRUPT_STATUS_2 fault=%d\n", __func__, ret, read_buf);
	if (ret)
		goto out;

	ret = regmap_read(led->regmap, LP8863_INTERRUPT_STATUS_3, &read_buf);
//	printk("%s: ret=%d, LP8863_INTERRUPT_STATUS_3 fault=%d\n", __func__, ret, read_buf);
        if (ret)
                goto out;

out:
	return ret;
}

static void lp8863_brightness_set(struct led_classdev *led_cdev,
				enum led_brightness brt_val)
{
	struct lp8863_led *led =
			container_of(led_cdev, struct lp8863_led, led_dev);
	int disp_brightness = brt_val * 255;
	int brt_lsb, brt_msb;
	int ret;

	printk("SBRE %s\n", __func__);

	/* swaping MSB and LSB values*/
	brt_lsb = (disp_brightness & 0xff00) >> 8;
        brt_msb = (disp_brightness & 0x00ff) << 8;

	mutex_lock(&led->lock);

	ret = lp8863_fault_check(led);
	if (ret) {
		dev_err(&led->client->dev, "SBRE lp8863: Cannot read/clear faults\n");
		goto out;
	}

	ret = regmap_write(led->regmap, LP8863_DISP_BRT, (brt_lsb + brt_msb));
	if (ret) {
		dev_err(&led->client->dev, "SBRE lp8863: Cannot write DISP_BRT\n");
		goto out;
	}
out:
	mutex_unlock(&led->lock);
/*	return ret;*/
}

static int lp8863_init(struct lp8863_led *led)
{
	int ret;

	printk("SBRE %s\n", __func__);

	/***** Enable INTERRUPT_ENABLE pins to check faults *****/
	ret = regmap_write(led->regmap, LP8863_INTERRUPT_ENABLE_1, 0xfffc);
	if (ret)
		printk("%s: Could not write to Interrupt Enable1 pin\n", __func__);

	ret = regmap_write(led->regmap, LP8863_INTERRUPT_ENABLE_2, 0xc000);
	if (ret)
                printk("%s: Could not write to Interrupt Enable1 pin\n", __func__);

	ret = regmap_write(led->regmap, LP8863_INTERRUPT_ENABLE_3, 0xffff);
	if (ret)
                printk("%s: Could not write to Interrupt Enable1 pin\n", __func__);
        /********************************************/

	ret = lp8863_fault_check(led);
	if (ret)
		return ret;

	return 0;
}

static void lp8863_regs_check(struct lp8863_led *led) {

	int ret;
	unsigned int read_buf;

	ret = regmap_read(led->regmap, LP8863_GROUPING1, &read_buf);
        if (ret){
		printk("%s: Can not read register %x\n", __func__, LP8863_GROUPING1);
	}
	else {
		printk("%s: Value in the register %x is %x\n", __func__, LP8863_GROUPING1, read_buf);
	}
}

static const struct reg_default lp8863_reg_defs[] = {
        { LP8863_BL_MODE, 0x0002},
	{ LP8863_DISP_BRT, 0xffff},
	{ LP8863_GROUPING1, 0x0000},
        { LP8863_GROUPING2, 0x0000},
	{ LP8863_INTERRUPT_ENABLE_1, 0x0000},
	{ LP8863_INTERRUPT_ENABLE_2, 0x0000},
	{ LP8863_INTERRUPT_ENABLE_3, 0x0000},
        { LP8863_INTERRUPT_STATUS_1, 0x0000},
	{ LP8863_INTERRUPT_STATUS_2, 0x0000},
	{ LP8863_INTERRUPT_STATUS_3, 0x0000},
};

static const struct regmap_config lp8863_regmap_config = {
	.reg_bits = 8,
	.val_bits = 16,

	.max_register = LP8863_INTERRUPT_STATUS_3,
	.reg_defaults = lp8863_reg_defs,
	.num_reg_defaults = ARRAY_SIZE(lp8863_reg_defs),
	.cache_type = REGCACHE_NONE,
};

static int lp8863_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	int ret;
	struct lp8863_led *led;
	struct device_node *np = client->dev.of_node;
	struct device_node *child_node;
	const char *name;

	printk("SBRE: %s\n", __func__);

	led = devm_kzalloc(&client->dev, sizeof(*led), GFP_KERNEL);
	if (!led)
		return -ENOMEM;

	printk("SBRE: %s, after memory allocation\n", __func__);

	for_each_available_child_of_node(np, child_node) {
		led->led_dev.default_trigger = of_get_property(child_node,
						    "linux,default-trigger",
						    NULL);

		ret = of_property_read_string(child_node, "label", &name);
		if (!ret)
			snprintf(led->label, sizeof(led->label), "%s:%s",
				 id->name, name);
		else
			snprintf(led->label, sizeof(led->label),
				"%s::display_cluster", id->name);
	}

	led->client = client;
	led->led_dev.name = "lcd-backlight"; /* led->label changed to lcd-backlight as android has permission to lcd-backlight name*/
//	led->led_dev.brightness_set = lp8863_brightness_set;

	mutex_init(&led->lock);

	i2c_set_clientdata(client, led);

	led->regmap = devm_regmap_init_i2c(client, &lp8863_regmap_config);
	if (IS_ERR(led->regmap)) {
		ret = PTR_ERR(led->regmap);
		dev_err(&client->dev, "lp8863: Failed to allocate register map: %d\n",
			ret);
		return ret;
	}

	ret = regmap_write(led->regmap, LP8863_BL_MODE, 0x0200); /*To set DISP_BRT reg to control the brightness not PWM*/
	led->led_dev.brightness_set = lp8863_brightness_set;

	ret = lp8863_init(led);
	if (ret)
		return ret;

	ret = led_classdev_register(&client->dev, &led->led_dev);
	if (ret < 0) {
		dev_err(&client->dev, "lp8863: led register err: %d\n", ret);
		return ret;
	}

	lp8863_regs_check(led); /*Function added to check the lp8863 LED backlight driver register default values*/
	printk("SBRE: %s, Probing Done\n", __func__);

	return 0;
}

static int lp8863_remove(struct i2c_client *client)
{
	struct lp8863_led *led = i2c_get_clientdata(client);

	mutex_destroy(&led->lock);
	led_classdev_unregister(&led->led_dev);

	return 0;
}

static const struct i2c_device_id lp8863_id[] = {
	{ "lp8863", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, lp8863_id);

static const struct of_device_id of_lp8863_leds_match[] = {
	{ .compatible = "ti,lp8863", },
	{},
};
MODULE_DEVICE_TABLE(of, of_lp8863_leds_match);

static struct i2c_driver lp8863_driver = {
	.driver = {
		.name	= "lp8863",
		.of_match_table = of_lp8863_leds_match,
	},
	.probe		= lp8863_probe,
	.remove		= lp8863_remove,
	.id_table	= lp8863_id,
};
module_i2c_driver(lp8863_driver);

MODULE_DESCRIPTION("Texas Instruments LP8863 LED driver");
MODULE_AUTHOR("Dan Murphy <dmurphy@ti.com>");
MODULE_LICENSE("GPL v2");
