/*
 * TI LP8866 LED driver based on LP8860 4-Channel LED Driver
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

/********** LP8866 **********/
#define LP8866_BRT_CONTROL              0x00
#define LP8866_LED_CURR_CONFIG          0x02
#define LP8866_USER_CONFIG_1            0x04
#define LP8866_USER_CONFIG_2            0x06
#define LP8866_SUPPLY_INT_EN            0x08
#define LP8866_BOOST_INT_EN             0x0A
#define LP8866_LED_INT_EN               0x0C
#define LP8866_SUPPLY_STATUS            0x0E
#define LP8866_BOOST_STATUS             0x10
#define LP8866_LED_STATUS               0x12

/**
 * struct lp8866_led -
 * @lock - Lock for reading/writing the device
 * @client - Pointer to the I2C client
 * @led_dev - led class device pointer
 * @regmap - Devices register map
 * @eeprom_regmap - EEPROM register map
 * @enable_gpio - VDDIO/EN gpio to enable communication interface
 * @regulator - LED supply regulator pointer
 * @label - LED label
 */
struct lp8866_led {
	struct mutex lock;
	struct i2c_client *client;
	struct led_classdev led_dev;
	struct regmap *regmap;
	struct gpio_desc *enable_gpio;
	struct regulator *regulator;
	char label[LED_MAX_NAME_SIZE];
};

static int lp8866_fault_check(struct lp8866_led *led)
{
	int ret;
//	int fault;
	unsigned int read_buf;

	printk("SBRE %s\n", __func__);

	ret = regmap_read(led->regmap, LP8866_SUPPLY_STATUS, &read_buf);
//	printk("%s: ret=%d, LP8866_SUPPLY_STATUS fault=%d\n", __func__, ret, read_buf);
	if (ret)
		goto out;

	ret = regmap_read(led->regmap, LP8866_BOOST_STATUS, &read_buf);
//	printk("%s: ret=%d, LP8866_BOOST_STATUS fault=%d\n", __func__, ret, read_buf);
	if (ret)
		goto out;

	ret = regmap_read(led->regmap, LP8866_LED_STATUS, &read_buf);
//	printk("%s: ret=%d, LP8866_LED_STATUS fault=%d\n", __func__, ret, read_buf);
        if (ret)
                goto out;

out:
	return ret;
}

static void lp8866_brightness_set(struct led_classdev *led_cdev,
				enum led_brightness brt_val)
{
	struct lp8866_led *led =
			container_of(led_cdev, struct lp8866_led, led_dev);
	int disp_brightness = brt_val * 255;
	int brt_lsb, brt_msb;
	int ret;

	printk("SBRE %s\n", __func__);

	/* swaping MSB and LSB values*/
	brt_lsb = (disp_brightness & 0xff00) >> 8;
        brt_msb = (disp_brightness & 0x00ff) << 8;

	mutex_lock(&led->lock);

	ret = lp8866_fault_check(led);
	if (ret) {
		dev_err(&led->client->dev, "SBRE lp8866: Cannot read/clear faults\n");
		goto out;
	}

	ret = regmap_write(led->regmap, LP8866_BRT_CONTROL, (brt_lsb + brt_msb));
	if (ret) {
		dev_err(&led->client->dev, "SBRE lp8866: Cannot write BRT_CONTROL\n");
		goto out;
	}
out:
	mutex_unlock(&led->lock);
/*	return ret;*/
}

static int lp8866_init(struct lp8866_led *led)
{
	int ret;

	printk("SBRE %s\n", __func__);

	/***** Enable INTERRUPT_ENABLE pins to check faults *****/
	ret = regmap_write(led->regmap, LP8866_SUPPLY_INT_EN, 0xff3f);
	if (ret)
		printk("%s: Could not write to Supply Interrupt Enable pin\n", __func__);

	ret = regmap_write(led->regmap, LP8866_BOOST_INT_EN, 0xfcff);
	if (ret)
                printk("%s: Could not write to Boost Interrupt Enable pin\n", __func__);

	ret = regmap_write(led->regmap, LP8866_LED_INT_EN, 0xff00);
	if (ret)
                printk("%s: Could not write to LED Interrupt Enable pin\n", __func__);
        /********************************************/

	ret = lp8866_fault_check(led);
	if (ret)
		return ret;

	return 0;
}

/************ Commenting as this function is not used *************/
/*
static void lp8866_regs_check(struct lp8866_led *led) {

	int ret;
	unsigned int read_buf;

	ret = regmap_read(led->regmap, LP8866_GROUPING1, &read_buf);
        if (ret){
		printk("%s: Can not read register %x\n", __func__, LP8866_GROUPING1);
	}
	else {
		printk("%s: Value in the register %x is %x\n", __func__, LP8866_GROUPING1, read_buf);
	}
}
*/
/****************************************************************/

static const struct reg_default lp8866_reg_defs[] = {
        { LP8866_USER_CONFIG_1, 0xA30A},
	{ LP8866_BRT_CONTROL, 0xffff},
	{ LP8866_SUPPLY_INT_EN, 0x0000},
	{ LP8866_BOOST_INT_EN, 0x0000},
	{ LP8866_LED_INT_EN, 0x0000},
        { LP8866_SUPPLY_STATUS, 0x0000},
	{ LP8866_BOOST_STATUS, 0x0000},
	{ LP8866_LED_STATUS, 0x0000},
};

static const struct regmap_config lp8866_regmap_config = {
	.reg_bits = 8,
	.val_bits = 16,

	.max_register = LP8866_LED_STATUS,
	.reg_defaults = lp8866_reg_defs,
	.num_reg_defaults = ARRAY_SIZE(lp8866_reg_defs),
	.cache_type = REGCACHE_NONE,
};

static int lp8866_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	int ret;
	struct lp8866_led *led;
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

	led->regmap = devm_regmap_init_i2c(client, &lp8866_regmap_config);
	if (IS_ERR(led->regmap)) {
		ret = PTR_ERR(led->regmap);
		dev_err(&client->dev, "lp8866: Failed to allocate register map: %d\n",
			ret);
		return ret;
	}

	ret = regmap_write(led->regmap, LP8866_USER_CONFIG_1, 0xA30A); /*To set DISP_BRT reg to control the brightness not PWM*/
	if (ret)
		printk("%s: Could not set backlight reg mode", __func__);
	led->led_dev.brightness_set = lp8866_brightness_set;

	ret = lp8866_init(led);
	if (ret)
		return ret;

	ret = led_classdev_register(&client->dev, &led->led_dev);
	if (ret < 0) {
		dev_err(&client->dev, "lp8866: led register err: %d\n", ret);
		return ret;
	}

        //lp8866_regs_check(led); /*Function added to check the lp8866 LED backlight driver register default values*/
	printk("SBRE: %s, Probing Done\n", __func__);

	return 0;
}

static int lp8866_remove(struct i2c_client *client)
{
	struct lp8866_led *led = i2c_get_clientdata(client);

	mutex_destroy(&led->lock);
	led_classdev_unregister(&led->led_dev);

	return 0;
}

static const struct i2c_device_id lp8866_id[] = {
	{ "lp8866", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, lp8866_id);

static const struct of_device_id of_lp8866_leds_match[] = {
	{ .compatible = "ti,lp8866", },
	{},
};
MODULE_DEVICE_TABLE(of, of_lp8866_leds_match);

static struct i2c_driver lp8866_driver = {
	.driver = {
		.name	= "lp8866",
		.of_match_table = of_lp8866_leds_match,
	},
	.probe		= lp8866_probe,
	.remove		= lp8866_remove,
	.id_table	= lp8866_id,
};
module_i2c_driver(lp8866_driver);

MODULE_DESCRIPTION("Texas Instruments LP8866 LED driver");
MODULE_AUTHOR("Dan Murphy <dmurphy@ti.com>");
MODULE_LICENSE("GPL v2");
