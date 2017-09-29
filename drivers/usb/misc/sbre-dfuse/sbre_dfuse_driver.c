/*
 * sbre_dfuse_driver.c - SBRE DfuSe flasher - usb driver
 *
 * Copyright·(c)·2017 SoftBank Robotics Europe.
 * Stéphane Régnier <sregnier@softbankrobotics.com>
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
*/

#include "sbre_dfuse.h"
#include "sbre_dfuse_file.h"

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/firmware.h>

/* Compatible devices */
#define USB_VID_STMICROELECTRONICS 0x0483
#define USB_PID_STM_DEVICE_IN_DFU 0xdf11
static const struct usb_device_id sbre_dfuse_idtable[] = {
	{ USB_DEVICE(USB_VID_STMICROELECTRONICS,
	             USB_PID_STM_DEVICE_IN_DFU) },
	{ }
};
MODULE_DEVICE_TABLE(usb, sbre_dfuse_idtable);

static struct usb_driver sbre_dfuse_driver;

static void sbre_dfuse_delete(struct kref *kref)
{
	struct sbre_dfuse *dev = to_sbre_dfuse_dev(kref);

	usb_put_dev(dev->usb_dev);
	kfree(dev);
}

static void sbre_dfuse_on_firmware(const struct firmware *fw, void *ctx)
{
	struct sbre_dfuse_file_simple file;
	int rv;
	struct sbre_dfuse *dev = ctx;

	/* Check firmware loading */
	if (!fw) {
		/* We have no idea of boot address, assume 0x08000000 */
		file.elementAddr = 0x08000000;
		dev_err(&dev->interface->dev,
		        "Firmware '%s' not found. Booting anyway at 0x%08x",
		        dev->fwPath, file.elementAddr);
		goto boot;
	}

	/* Parse dfuse file */
	rv = sbre_dfuse_file_simple_parse(fw->data, fw->size, &file);
	if (rv < 0) {
		dev_err(&dev->interface->dev, "Failed to parse dfuse file, err=%d.", rv);
		goto exit;
	}
	dev_dbg(&dev->interface->dev, "Firmware parsed: alt=%u, element @0x%08x size %d, bcdDev=0x%04x, Usb id %04x:%04x.",
	        file.alt, file.elementAddr, file.elementSize, file.bcdDevice,
	        file.idVendor, file.idProduct);

	/* Verify fw info */
	if (!memcmp(file.fwInfo, dev->fwInfo, sizeof(dev->fwInfo))) {
		/* Same firmware, boot */
		dev_dbg(&dev->interface->dev, "Firmware up to date, booting.");
	} else {
		/* Different, erase, flash then boot */
		dev_info(&dev->interface->dev, "Firmware mismatch, updating.");
		rv = sbre_dfuse_flash(dev, &file);
		if (rv < 0) {
			dev_err(&dev->interface->dev,
			        "Failed to flash new firmware, err=%d.", rv);
			goto exit;
		}
		/* Flashed, boot */
		dev_info(&dev->interface->dev, "Firmware flashed, booting.");
	}

boot:
	/* assumes boot address is start of element address */
	rv = sbre_dfuse_boot(dev, file.elementAddr);
	if (rv < 0)
		dev_err(&dev->interface->dev,
	            "Failed to send boot command, err=%d.", rv);

exit:
	if (fw)
		release_firmware(fw);
}

static int sbre_dfuse_probe(struct usb_interface *interface,
                           const struct usb_device_id *id)
{
	struct sbre_dfuse *dev;
	int rv = -ENOMEM;

	/* Allocate and init struct sbre_dfuse */
	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (!dev)
		goto error;

	kref_init(&dev->kref);

	dev->usb_dev = usb_get_dev(interface_to_usbdev(interface));
	dev->interface = interface;
	dev->itf_alt = -1;
	memset(dev->itf_alt_strdesc, 0, sizeof(dev->itf_alt_strdesc));
	memset(dev->fwInfo, 0, sizeof(dev->fwInfo));
	memset(dev->sInfo, 0, sizeof(dev->sInfo));
	memset(dev->fwPath, 0, sizeof(dev->fwPath));
	dev->transferSize = 2048; /* from ST app note AN3156, size of flash transfers. */

	/* Save interface private data */
	usb_set_intfdata(interface, dev);

	/* Dfuse setup (select USB interface alternate of flash partition and retrieve fw info) */
	rv = sbre_dfuse_setup_device(dev);
	if (rv < 0) {
		dev_err(&interface->dev, "Not able to probe dfuse device.");
		usb_set_intfdata(interface, NULL);
		goto error;
	}

	/* Request firmware (dfuse file) */
	dev_dbg(&interface->dev, "Requesting firmware '%s'.", dev->fwPath);
	rv = request_firmware_nowait(THIS_MODULE, true, dev->fwPath,
	                             &dev->usb_dev->dev, GFP_KERNEL, dev,
	                             sbre_dfuse_on_firmware);
	if (rv) {
		dev_err(&interface->dev, "Not able to request firmware.");
		usb_set_intfdata(interface, NULL);
		goto error;
	}

	dev_info(&interface->dev, "STM32 Dfuse device connected. %s", dev->sInfo);

	return 0;

error:
	if (dev)
		kref_put(&dev->kref, sbre_dfuse_delete);

	return rv;
}

static void sbre_dfuse_disconnect(struct usb_interface *interface)
{
	struct sbre_dfuse *dev;

	dev = usb_get_intfdata(interface);
	usb_set_intfdata(interface, NULL);

	dev->interface = NULL;

	kref_put(&dev->kref, sbre_dfuse_delete);

	dev_info(&interface->dev, "STM32 Dfuse device disconnected.");
}

static struct usb_driver sbre_dfuse_driver = {
	.name =		"sbre-dfuse",
	.probe =	sbre_dfuse_probe,
	.disconnect =	sbre_dfuse_disconnect,
	.id_table =	sbre_dfuse_idtable,
};
module_usb_driver(sbre_dfuse_driver);

MODULE_LICENSE("GPL");

