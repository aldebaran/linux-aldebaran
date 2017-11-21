/*
 * sbre-usb-i2c.c - I2C driver for SBRE USB to I2C adapter
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

#include <linux/errno.h>
#include <linux/init.h>
#include <linux/i2c.h>
#include <linux/kernel.h>
#include <linux/kobject.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/sysfs.h>
#include <linux/types.h>
#include <linux/usb.h>
#include <uapi/linux/i2c.h>
#include <linux/crc32.h>

/* Driver private data */
struct sbre_usb_i2c {
	/* devices handles */
	struct usb_device *usb_dev;
	struct usb_interface *interface;
	struct i2c_adapter adapter;
	/* Bulk buffers, must match firmware max bulk buffer size. */
	u8 oBuf[1024];
	u8 iBuf[1024];
	/* Not used, for future error management */
	uint64_t lastFailChainStatus;
};

/* Firmware endpoints */
#define USB_DATA_EP_IN  0x82
#define USB_DATA_EP_OUT 0x02

/* Firmware read firmware info, control read parameters. */
#define USB_READ_FWINFO_REQ     0
#define USB_READ_FWINFO_REQTYPE 0xA2
#define USB_READ_FWINFO_LEN     64

/* Communication timeout, 1s */
#define USB_IO_TIMEOUT (1*HZ)

/* Firmware protocol, transaction header */
struct i2c_cmd_header {
	/* Address and direction, 7bits address support only.
	 * Address is left aligned, bit 0 is direction (1 means read).
	 */
	uint8_t addrDir;
	/*
	 * Exchange flag, meaning depends on direction.
	 * Host to device: bit 0 is NOSTOP, do not generate STOP and do
	 *                 a repeated start.
	 * Device to host: bit 0 is ACK, 1 on success or 0 is nacked.
	 */
	uint8_t flags;
	/* Exchange data len */
	uint8_t length;
} __packed;
/*
 * Following the header, the exchange data if any, of size
 * i2c_cmd_header->length.
 * Condition of data presence: host to device write, or device to host
 * successful read (i2c_cmd_header->flags & 1).
 */

/* i2c_cmd_header direction mask */
#define I2C_DIR_MASK     0x01
/* i2c_cmd_header direction write */
#define I2C_DIR_WRITE    0
/* i2c_cmd_header direction read */
#define I2C_DIR_READ     1

/* USB management, send bulk command to device */
static int sbre_usb_i2c_recv(struct i2c_adapter *adapter, void *data, int len);
/* USB management, receive bulk answer from device */
static int sbre_usb_i2c_send(struct i2c_adapter *adapter, void *data, int len);

/*
 * Firmware protocol, bulk an i2c message list into a buffer to send
 *
 * Byte 0 is list length (max 255)
 * Payload is list of i2c_cmd_header and data in case of write transaction.
 * Last 4 bytes is a CRC32 validating the whole buffer (Poly 0x04C11DB7).
 */
static void sbre_usb_i2c_bulk(u8 **buffer, struct i2c_msg *msgs, int nmsgs)
{
	int i, j;
	u8 *buf = *buffer;
	u32 crc;
	struct i2c_cmd_header *cmd;

	nmsgs &= 0xff;
	*buf++ = nmsgs;
	for (i = 0; i < nmsgs; i++) {
		cmd = (struct i2c_cmd_header *)buf;
		cmd->addrDir = (msgs[i].addr&0xff)<<1;
		cmd->flags = 1; /* nostop */
		if ((msgs[i].flags & I2C_M_STOP) || i == nmsgs-1)
			cmd->flags &= ~1;
		cmd->length = msgs[i].len & 0xff;
		buf += sizeof(struct i2c_cmd_header);
		if (msgs[i].flags & I2C_M_RD) {
			cmd->addrDir |= I2C_DIR_READ;
		} else {
			for (j = 0; j < cmd->length; j++)
				*buf++ = msgs[i].buf[j];
		}
	}
	crc = crc32_le(0xffffffff, *buffer, buf-*buffer) ^ 0xffffffff;
	*buf++ = crc>>24;
	*buf++ = (crc>>16)&0xff;
	*buf++ = (crc>>8)&0xff;
	*buf++ = crc&0xff;
	*buffer = buf;
}

/* Firmware protocol, debulk a buffer and update i2c message list
 * Returns the number of successful messages (may be 0), else a negative errno.
 *
 * Byte 0 is list length (max 255)
 * Payload is list of i2c_cmd_header and data in case of successful read.
 * Last 4 bytes is a CRC32 validating the whole buffer (Poly 0x04C11DB7).
 *
 * Every fields from sent list are tested, any difference creates an error.
 */
static int sbre_usb_i2c_debulk(struct i2c_msg *msgs, int nmsgs,
                               u8 *iBuf, int iLen)
{
	int i;
	int successes = 0;
	u32 crc;
	u8 *rcrc;
	struct i2c_cmd_header *cmd = 0;

	if (iLen < 1+4) /* invalid size */
		return -EIO;
	crc = crc32_le(0xffffffff, iBuf, iLen-4) ^ 0xffffffff;
	rcrc = iBuf+iLen-4;
	if ((crc>>24) != rcrc[0] || ((crc>>16)&0xff) != rcrc[1] ||
	    ((crc>>8)&0xff) != rcrc[2] || (crc&0xff) != rcrc[3])
		return -EIO; /* invalid crc */
	if (iBuf[0] != nmsgs)
		return -EIO; /* numbers of messages mismatch */
	if (iLen < 1+sizeof(struct i2c_cmd_header)*(int)nmsgs+4)
		return -EIO; /* invalid size */
	iBuf += 1;
	iLen -= 1+4;
	for (i = 0; i < nmsgs; i++) {
		if (iLen < sizeof(struct i2c_cmd_header))
			return -EIO; /* unexpected EOF */
		cmd = (struct i2c_cmd_header *)iBuf;
		iBuf += sizeof(struct i2c_cmd_header);
		iLen -= sizeof(struct i2c_cmd_header);
		if ((cmd->addrDir>>1) != (msgs[i].addr&0xff))
			return -EIO; /* invalid address */
		if (cmd->length != msgs[i].len)
			return -EIO; /* invalid length */
		if (msgs[i].flags & I2C_M_RD) {
			if ((cmd->addrDir & I2C_DIR_MASK) != I2C_DIR_READ)
				return -EIO; /* invalid direction */
			if (cmd->flags == 1) {
				if (iLen < cmd->length)
					return -EIO; /* unexpected EOF */
				memcpy(msgs[i].buf, iBuf, cmd->length);
				iBuf += cmd->length;
			}
		}
		if (cmd->flags == 1)
			successes++;
	}
	return successes;
}

/*
 * I2C management, execute transfer requests
 * Bulk message list, send via bulk endpoint, receive answer on bulk endpoint and debulk messages.
 */
static int sbre_usb_i2c_xfer(struct i2c_adapter *adapter, struct i2c_msg *msgs, int num)
{
	int ret, iLen;
	struct sbre_usb_i2c *dev = (struct sbre_usb_i2c *)adapter->algo_data;
	u8 *wbuf = dev->oBuf;
	u32 wlen = 0;

	dev_dbg(&adapter->dev, "master xfer %d messages:\n", num);

	sbre_usb_i2c_bulk(&wbuf, msgs, num);
	wlen = wbuf - dev->oBuf;
	if (sbre_usb_i2c_send(adapter, dev->oBuf, wlen) != wlen) {
		dev_err(&adapter->dev, "failure writing data\n");
		ret = -EREMOTEIO;
		goto out;
	}

	iLen = sbre_usb_i2c_recv(adapter, dev->iBuf, sizeof(dev->iBuf));
	if (iLen < 1) {
		dev_err(&adapter->dev, "failure reading data\n");
		ret = -EREMOTEIO;
		goto out;
	}

	return sbre_usb_i2c_debulk(msgs, num, dev->iBuf, iLen);

out:
	return ret;
}

/* I2C management, supported functionnalities */
static u32 sbre_usb_i2c_func(struct i2c_adapter *adapter)
{
	return I2C_FUNC_I2C | I2C_FUNC_NOSTART | I2C_FUNC_SMBUS_EMUL;
}

/* I2C management, algorithm */
static const struct i2c_algorithm i2c_algo = {
	.master_xfer	= sbre_usb_i2c_xfer,
	.functionality	= sbre_usb_i2c_func,
};

/* USB management, target devices */
static const struct usb_device_id sbre_usb_i2c_table[] = {
	{ USB_DEVICE(0xC001, 0x5253) },
	{ } /* end */
};
MODULE_DEVICE_TABLE(usb, sbre_usb_i2c_table);

/* USB management, send bulk command to device */
static int sbre_usb_i2c_recv(struct i2c_adapter *adapter, void *data, int len)
{
	struct sbre_usb_i2c *dev = (struct sbre_usb_i2c *)adapter->algo_data;
	int actual_len = 0;
	int ret = usb_bulk_msg(dev->usb_dev,
	                       usb_rcvbulkpipe(dev->usb_dev, USB_DATA_EP_IN),
	                       data, len, &actual_len,
	                       USB_IO_TIMEOUT);
	return ret >= 0 ? actual_len : ret;
}

/* USB management, receive bulk answer from device */
static int sbre_usb_i2c_send(struct i2c_adapter *adapter, void *data, int len)
{
	struct sbre_usb_i2c *dev = (struct sbre_usb_i2c *)adapter->algo_data;
	int actual_len = 0;
	int ret = usb_bulk_msg(dev->usb_dev,
	                       usb_sndbulkpipe(dev->usb_dev, USB_DATA_EP_OUT),
	                       data, len, &actual_len,
	                       USB_IO_TIMEOUT);
	return ret >= 0 ? actual_len : ret;
}

/* USB management, delete device */
static void sbre_usb_i2c_free(struct sbre_usb_i2c *dev)
{
	usb_put_dev(dev->usb_dev);
	kfree(dev);
}

/* Future use, attribute describing which message in list succeed or not */
static ssize_t failChainStatus_show(struct device *adapter_dev,
                                    struct device_attribute *attr, char *buf)
{
	struct i2c_adapter *adapter = container_of(adapter_dev, struct i2c_adapter, dev);
	struct sbre_usb_i2c *dev = (struct sbre_usb_i2c *)adapter->algo_data;

	return scnprintf(buf, PAGE_SIZE, "%llu\n", dev->lastFailChainStatus++);
}
/* Future use, attribute describing which message in list succeed or not */
static struct device_attribute failChainStatus_attribute = __ATTR_RO(failChainStatus);

/*
 * USB management, device probing
 * Does not probe anything, USB VID/PID criteria is enough.
 * Retrieve firmware info, setup usb device and i2c adapter.
 */
static int sbre_usb_i2c_probe(struct usb_interface *interface,
                              const struct usb_device_id *id)
{
	struct sbre_usb_i2c *dev;
	int rv = -ENOMEM;
	u8 infos[USB_READ_FWINFO_LEN];

	dev_dbg(&interface->dev, "probing usb device\n");

	/* allocate memory for our device state and initialize it */
	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (dev == NULL)
		goto error;

	dev->usb_dev = usb_get_dev(interface_to_usbdev(interface));
	dev->interface = interface;
	dev->lastFailChainStatus = 24;

	/* save our data pointer in this interface device */
	usb_set_intfdata(interface, dev);

	/* setup i2c adapter description */
	dev->adapter.owner = THIS_MODULE;
	dev->adapter.class = I2C_CLASS_HWMON;
	dev->adapter.algo = &i2c_algo;
	dev->adapter.algo_data = dev;
	snprintf(dev->adapter.name, sizeof(dev->adapter.name),
	         "SBRE USB-I2C on port %d.%d",
	         dev->usb_dev->bus->busnum,
	         dev->usb_dev->devnum);

	/* retrieve firmware info */
	rv = usb_control_msg(dev->usb_dev,
	                     usb_rcvctrlpipe(dev->usb_dev, 0),
	                     USB_READ_FWINFO_REQ, USB_READ_FWINFO_REQTYPE,
	                     0, 0,
	                     infos, sizeof(infos), USB_IO_TIMEOUT);
	if (rv >= 0) {
		dev_info(&interface->dev,
		         "SBRE USB-I2C on port %d.%d: '%s'",
		         dev->usb_dev->bus->busnum,
		         dev->usb_dev->devnum,
		         infos);
	}

	dev->adapter.dev.parent = &dev->interface->dev;

	/* attach to i2c layer */
	i2c_add_adapter(&dev->adapter);

	/* inform user about successful attachment to i2c layer */
	dev_info(&dev->adapter.dev,
	         "Connected with SBRE-USB-I2C on port %d.%d\n",
	         dev->usb_dev->bus->busnum,
	         dev->usb_dev->devnum);

	rv = sysfs_create_file(&dev->adapter.dev.kobj,
	                       &failChainStatus_attribute.attr);
	if (rv)
		dev_err(&interface->dev, "Can't create sysfs file");

	return rv;

 error:
	if (dev)
		sbre_usb_i2c_free(dev);

	return rv;
}

/* USB management, device disconnection */
static void sbre_usb_i2c_disconnect(struct usb_interface *interface)
{
	struct sbre_usb_i2c *dev = usb_get_intfdata(interface);

	if (dev) {
		sysfs_remove_file(&dev->adapter.dev.kobj, &failChainStatus_attribute.attr);
		i2c_del_adapter(&dev->adapter);
		usb_set_intfdata(interface, NULL);
		sbre_usb_i2c_free(dev);
		dev_info(&interface->dev, "disconnected\n");
	}
}

/* USB management, driver */
static struct usb_driver sbre_usb_i2c_driver = {
	.name       = "sbre-usb-i2c",
	.probe      = sbre_usb_i2c_probe,
	.disconnect = sbre_usb_i2c_disconnect,
	.id_table   = sbre_usb_i2c_table,
};

module_usb_driver(sbre_usb_i2c_driver);

MODULE_AUTHOR("Stéphane Régnier <sregnier@softbankrobotics.com>");
MODULE_DESCRIPTION("sbre-usb-i2c driver v1.0");
MODULE_LICENSE("GPL");

