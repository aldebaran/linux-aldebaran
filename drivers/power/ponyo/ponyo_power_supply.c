/*
 *  ponyo_power_supply.c
 *  Retrieve Pepper Robot battery SOC from Linux onboard computer
 *
 *  Copyright (C) 2019 Sofbank Robotics
 *  Nicolas de Maubeuge <nicolas.demaubeuge@external.softbankrobotics.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/kref.h>
#include <linux/usb.h>
#include <linux/platform_device.h>
#include <linux/uaccess.h>
#include <linux/power_supply.h>
#include <linux/signal.h>
#include <linux/sched.h>

#define USB_PONYO_POWER_SUPPLY_VENDOR_ID	0x1d6b	/* PPS Linux Vendor ID */
#define USB_PONYO_POWER_SUPPLY_PRODUCT_ID	0x0104	/* PPS Linux Product ID */
#define USB_PONYO_POWER_SUPPLY_SUB_CLASS	43	/* PPS Linux Interface Subclass */
#define SIGSHUTDOWNREQ  52

/* structure to hold ponyo_power_supply device-specific data */
struct usb_ponyo_power_supply {
	/* USB data */
	struct usb_device *			udev;				/* usb_device for this device */
	struct urb *				int_urb;			/* usb interrupt urb */
	unsigned char *				int_endpoint_buffer;		/* buffer to receive data */
	size_t					int_endpoint_size;		/* size of the receive buffer */
	struct usb_driver			driver;				/* usb_driver for this device */

        /* power supply data */
        struct power_supply			*battery;			/* power supply for this device */
        struct power_supply_desc                bat_desc;
        int					capacity;			/* current charge, percent */
        int					time_to_empty;			/* remain. bat. discharge time (s) */
        int					time_to_full;			/* remain. bat. charge time (s) */
        int					status;				/* battery status */
        int					shutdown_request;		/* shutdown request */
};

static int ponyo_power_supply_get_property(struct power_supply *psy,
                            enum power_supply_property psp,
                            union power_supply_propval *val)
{
        struct usb_ponyo_power_supply *upps  = power_supply_get_drvdata(psy);
        // Reply to supported power supply properties
        switch (psp) {
        case POWER_SUPPLY_PROP_STATUS:
                val->intval = upps->status;
                break;
        case POWER_SUPPLY_PROP_CAPACITY:
                val->intval = upps->capacity;
                break;
        case POWER_SUPPLY_PROP_TIME_TO_FULL_NOW:
                val->intval = upps->time_to_full;
                break;
        case POWER_SUPPLY_PROP_TIME_TO_EMPTY_NOW:
                val->intval = upps->time_to_empty;
                break;
        default:
                return -EINVAL;
        }

        return 0;
}

/* ponyo_power_supply Linux power supply  properties */
static enum power_supply_property ponyo_power_supply_props[] = {
        POWER_SUPPLY_PROP_STATUS,
        POWER_SUPPLY_PROP_CAPACITY,
        POWER_SUPPLY_PROP_TIME_TO_FULL_NOW,
        POWER_SUPPLY_PROP_TIME_TO_EMPTY_NOW
};

/* ponyo_power_supply device/interface description */
static struct usb_device_id ponyo_power_supply_table [] = {
	{ USB_DEVICE_AND_INTERFACE_INFO(
			USB_PONYO_POWER_SUPPLY_VENDOR_ID,
			USB_PONYO_POWER_SUPPLY_PRODUCT_ID,
			USB_CLASS_VENDOR_SPEC,
			USB_PONYO_POWER_SUPPLY_SUB_CLASS,
			0) },
	{ }	/* Terminating entry */
};
MODULE_DEVICE_TABLE (usb, ponyo_power_supply_table);

static void ponyo_power_supply_usb_int_callback(struct urb *urb)
{
	uint32_t* payload;
	struct usb_ponyo_power_supply *dev;

	/* retrieve ponyo_power_supply_context */
	dev = urb->context;

	pr_debug("ponyo_power_supply: interrupt callback");

	/* urb status check */
	if (urb->status) {
		printk_ratelimited(KERN_ERR "ponyo_power_supply: error reading"
				"usb int request, Error number: %d"
				, urb->status);
		return;
	}

	/* unpack data from interrupt buffer */
	if (urb->actual_length == 5*sizeof(uint32_t)) {
		payload = (uint32_t*) dev->int_endpoint_buffer;
		dev->time_to_empty	= *payload;
		dev->time_to_full	= *(payload + 1);
	       	dev->status		= *(payload + 2);
		dev->capacity		= *(payload + 3);
		dev->shutdown_request	= *(payload + 4);

		pr_debug("ponyo_power_supply: read time_to_empty: %d,"
				"time_to_full: %d,"
				"battery_status: %d, capacity: %d, shutdown_request: %d",
		dev->time_to_empty, dev->time_to_full, dev->status, dev->capacity,
		dev->shutdown_request);

		/* notify kernel of power supply changes */
		power_supply_changed(dev->battery);

		if(dev->shutdown_request) {
			/* Finding task_struct corresponding to init process */
			struct task_struct *p = find_task_by_vpid(1);
			/* Send SIGSHUTDOWNREQ to init process */
			send_sig(SIGSHUTDOWNREQ, p, 0);
		}
	}
	else {
		printk_ratelimited(KERN_ERR "ponyo_power_supply:"
			       	"usb interrupt buffer, invalid size: %dB"
				, urb->actual_length);
	}

	/* register urb for next interrupt */
	usb_submit_urb(dev->int_urb, GFP_ATOMIC);
}

static int ponyo_power_supply_usb_probe(
		struct usb_interface *interface,
		const struct usb_device_id *id)
{
	struct usb_ponyo_power_supply *upps		= NULL;
	struct usb_host_interface *interface_desc 	= NULL;
	struct usb_endpoint_descriptor *endpoint	= NULL;
	struct usb_driver *driver 			= NULL;

	int i 						= 0;
	int retval 					= 0;

	pr_info("ponyo_power_supply: usb probe");

	/* retrieve interface descriptor */
	interface_desc = interface->cur_altsetting;

	/* get usb driver */
	driver = to_usb_driver(interface->dev.driver);

	/* get usb_ponyo_power_supply device */
	upps = container_of(driver, struct usb_ponyo_power_supply, driver);

	/* get usb device  */
	upps->udev = usb_get_dev(interface_to_usbdev(interface));

	/* check interface is a ponyo_power_supply interface */
	if (interface_desc->desc.bInterfaceClass != USB_CLASS_VENDOR_SPEC ||
			interface_desc->desc.bInterfaceSubClass
			!= USB_PONYO_POWER_SUPPLY_SUB_CLASS) {
		pr_err("ponyo_power_supply: wrong interface");
		return -ENXIO;
	}

	/* get interrupt endpoint from interface */
	for (i = 0; i < interface_desc->desc.bNumEndpoints; i++) {
		if (usb_endpoint_xfer_int(&interface_desc->endpoint[i].desc)) {
			endpoint = &interface_desc->endpoint[i].desc;
			break;
		}
	}

	if (!endpoint) {
		pr_err("ponyo_power_supply: could not find interrupt endpoint");
		return -ENXIO;
	}

	/* register interrupt endpoint into device structure */
	upps->int_endpoint_size = endpoint->wMaxPacketSize;

	/* allocate endpoint buffer */
	upps->int_endpoint_buffer = kmalloc(upps->int_endpoint_size, GFP_KERNEL);
	if (!upps->int_endpoint_buffer) {
		pr_err("ponyo_power_supply: could not allocate"
			       	"interrupt endpoint buffer");
		return -ENOMEM;
	}

	/* allocate interrupt urb */
	upps->int_urb = usb_alloc_urb(0, GFP_KERNEL);
	if (!upps->int_urb) {
		pr_err("ponyo_power_supply: could not allocate urb");
		return -ENOMEM;
	}

	/* initialize interrupt urb */
	usb_fill_int_urb(upps->int_urb,
			 upps->udev,
			 usb_rcvintpipe(upps->udev,
				 	endpoint->bEndpointAddress),
			 upps->int_endpoint_buffer,
			 le16_to_cpu(upps->int_endpoint_size),
			 ponyo_power_supply_usb_int_callback,
			 upps,
			 endpoint->bInterval);

	/* submit interrupt urb */
	retval = usb_submit_urb(upps->int_urb, GFP_KERNEL);
	if (retval) {
		pr_err("ponyo_power_supply: error submitting USB urb");
		return retval;
	}

	/* save our data pointer in this interface device */
	usb_set_intfdata(interface, upps);

       	/* notify kernel battery info was updated */
	power_supply_changed(upps->battery);

	pr_info("ponyo_power_supply: battery configuration success");

	return 0;
}

static void ponyo_power_supply_usb_disconnect(struct usb_interface *interface)
{
	struct usb_ponyo_power_supply* upps = NULL;

	pr_info("ponyo_power_supply: usb disconnect");

	/* retrieve device data structure */
	upps = usb_get_intfdata(interface);

	if (!upps) {
		pr_err("ponyo_power_supply: error getting device data");
	}

	/* set battery status to unknown */
	upps->status = POWER_SUPPLY_STATUS_UNKNOWN;
	power_supply_changed(upps->battery);


	/* set interface data to null */
	usb_set_intfdata(interface, NULL);
}

static int ponyo_power_supply_plat_probe(struct platform_device *pdev) {
	int result = 0;
	struct usb_ponyo_power_supply* upps;
	struct power_supply_config psy_cfg = {};

	pr_info("ponyo_power_supply: init");

	/* allocate usb_ponyo_power_supply structure */
	pr_debug("ponyo_power_supply: allocate device structure");

	upps = devm_kzalloc(&pdev->dev, sizeof(*upps), GFP_KERNEL);
	if (!upps) {
		pr_err("ponyo_power_supply:"
				"could not allocate device structure");
		return -ENOMEM;
	}

	platform_set_drvdata(pdev, upps);

	/* initialize device data structure */
	upps->bat_desc.name                = "ponyo-battery";
        upps->bat_desc.type                = POWER_SUPPLY_TYPE_BATTERY;
        upps->bat_desc.properties          = ponyo_power_supply_props;
        upps->bat_desc.num_properties      = ARRAY_SIZE(ponyo_power_supply_props);
        upps->bat_desc.get_property        = ponyo_power_supply_get_property;

        upps->capacity			= 50;
	upps->time_to_full		= 60;
	upps->time_to_empty		= 60;
	upps->status			= POWER_SUPPLY_STATUS_UNKNOWN;

	upps->driver.name		= "ponyo_power_supply";
	upps->driver.id_table		= ponyo_power_supply_table;
	upps->driver.probe		= ponyo_power_supply_usb_probe;
	upps->driver.disconnect		= ponyo_power_supply_usb_disconnect;

	psy_cfg.drv_data		= upps;

	/* register ponyo-power-supply-battery with power supply class */
	pr_debug("ponyo_power_supply: register battery");
	upps->battery = power_supply_register(&pdev->dev, &upps->bat_desc, &psy_cfg);
	if (IS_ERR(upps->battery)) {
		result = PTR_ERR(upps->battery);
		pr_err("ponyo_power_supply:"
				"power_supply register failed Error number %d"
				, result);
		return result;
	}

	/* register this driver with the USB subsystem */
	pr_debug("ponyo_power_supply: register usb driver");
        result = usb_register(&upps->driver);

	if (result) {
		pr_err("ponyo_power_supply: usb_register failed.Error number %d"
				, result);
		return result;
	}

	/* set platform device data */
	dev_set_drvdata(&pdev->dev, upps);

	return 0;
}

static int ponyo_power_supply_plat_remove(struct platform_device *pdev) {

	struct usb_ponyo_power_supply* upps;

	pr_info("ponyo_power_supply : exit");

	/* get device structure */
	upps = dev_get_drvdata(&pdev->dev);

	if (!upps) {
		pr_err("ponyo_power_supply: error getting device data");
		return -ENODEV;
	}

        /* deregister this driver with the USB subsystem */
	pr_debug("ponyo_power_supply: deregister usb driver");
	usb_deregister(&upps->driver);

	/* deregister ponyo-power-supply-battery with power-supply class */
	pr_debug("ponyo_power_supply: deregister battery");
	power_supply_unregister(upps->battery);

	/* free device structure */
	kfree(&pdev->dev);

	return 0;
}

static struct of_device_id ponyo_power_supply_plat_of_match[] = {
          { .compatible = "sbr,ponyo_power_supply"},
          {},
};
MODULE_DEVICE_TABLE(of, ponyo_power_supply_plat_of_match);

static struct platform_driver ponyo_power_supply_plat_driver = {
          .driver         = {
                  .name   = "ponyo_power_supply_plat",
                  .of_match_table = ponyo_power_supply_plat_of_match,
          },
          .probe          = ponyo_power_supply_plat_probe,
          .remove         = ponyo_power_supply_plat_remove,
};
module_platform_driver(ponyo_power_supply_plat_driver);


MODULE_AUTHOR("Nicolas de Maubeuge"
		"<nicolas.demaubeuge@external.softbankrobotics.com>");
MODULE_DESCRIPTION("Ponyo Power Supply Driver");
MODULE_LICENSE("GPL");
