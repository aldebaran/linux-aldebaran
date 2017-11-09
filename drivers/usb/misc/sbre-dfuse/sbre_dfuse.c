/*
 * sbre_dfuse.c - SBRE DfuSe flasher - dfuse protocol
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

#include <linux/ctype.h>
#include <linux/slab.h>
#include <linux/usb/ch9.h>

/* Offset of firmware info in microcontroller flash image */
#define SBRE_FIRMWARE_INFO_OFFSET 0xc0
/* Address of firmware info into microcontroller flash. */
static const uint32_t sbre_firmware_info_address = 0x08000000 | SBRE_FIRMWARE_INFO_OFFSET;

/* Communication timeout, 1s */
#define USB_IO_TIMEOUT (1*HZ)

/* Current state of the device (Status.bStatus) */
#define DFU_STATUS_OK            0
#define DFU_STATUS_ERRTARGET     1
#define DFU_STATUS_ERRFILE       2
#define DFU_STATUS_ERRWRITE      3
#define DFU_STATUS_ERRERASE      4
#define DFU_STATUS_ERRCHECKERASE 5
#define DFU_STATUS_ERRPROG       6
#define DFU_STATUS_ERRVERIFY     7
#define DFU_STATUS_ERRADDRESS    8
#define DFU_STATUS_ERRNOTDONE    9
#define DFU_STATUS_ERRFIRMWARE   10
#define DFU_STATUS_ERRVENDOR     11
#define DFU_STATUS_ERRUSBR       12
#define DFU_STATUS_ERRPOR        13
#define DFU_STATUS_ERRUNKNOWN    14
#define DFU_STATUS_ERRSTALLEDPKT 15

/*
 * State that the device is going to enter
 * immediately following the getstatus answer (Status.bState)
 */
#define DFU_STATE_APPIDLE              0
#define DFU_STATE_APPDETACH            1
#define DFU_STATE_DFUIDLE              2
#define DFU_STATE_DFUDOWNLOADSYNC      3
#define DFU_STATE_DFUDOWNLOADBUSY      4
#define DFU_STATE_DFUDOWNLOADIDLE      5
#define DFU_STATE_DFUMANIFESTSYNC      6
#define DFU_STATE_DFUMANIFEST          7
#define DFU_STATE_DFUMANIFESTWAITRESET 8
#define DFU_STATE_DFUUPLOADIDLE        9
#define DFU_STATE_DFUERROR             10

/* USB bmRequestType */
#define REQTYPE_RCV   (USB_DIR_IN|USB_TYPE_CLASS|USB_RECIP_INTERFACE)
#define REQTYPE_SND   (USB_DIR_OUT|USB_TYPE_CLASS|USB_RECIP_INTERFACE)

/* USB bRequest */
#define DFU_REQ_DL        1
#define DFU_REQ_UL        2
#define DFU_REQ_GETSTATUS 3
#define DFU_REQ_CLRSTATUS 4
#define DFU_REQ_ABORT     6

/*
 * Dfuse special commands
 *
 * ST DFU protocol of STM32 bootloader AN3156. Section 4.2 'Get command'.
 * http://www.st.com/resource/en/application_note/cd00264379.pdf
 */
#define DFUSE_CMD_GET            0
#define DFUSE_CMD_SET_ADDRESS    0x21
#define DFUSE_CMD_ERASE          0x41
#define DFUSE_CMD_READ_UNPROTECT 0x92

/*
 * Dfuse special command GETSTATUS.
 * Get current device status and parse it to struct sbre_dfuse_status.
 * Returns 0 on success, else a negative errno.
 */
static int dfuse_get_status(struct sbre_dfuse *dev,
                            struct sbre_dfuse_status *status)
{
	uint8_t buf[6];
	int rv;

	rv = usb_control_msg(dev->usb_dev,
	                     usb_rcvctrlpipe(dev->usb_dev, 0),
	                     DFU_REQ_GETSTATUS, REQTYPE_RCV,
	                     0, dev->itf_alt,
	                     buf, sizeof(buf), USB_IO_TIMEOUT);
	if (rv != sizeof(buf)) {
		dev_err(&dev->interface->dev, "Cannot get status.");
		return rv < 0 ? rv : -EREMOTEIO;
	}

	/* Parse */
	status->bStatus = buf[0];
	status->bwPollTimeOut = buf[1] + (buf[2] << 8) + (buf[3] << 16);
	status->bState = buf[4];
	status->iString = buf[5];

	/* Directly sleeps here, assures next command is ok */
	msleep(status->bwPollTimeOut);

	return 0;
}

static int dfuse_wait_download_end(struct sbre_dfuse *dev,
                                   struct sbre_dfuse_status *status)
{
	int rv;

	/* Wait while status is downloadbusy */
	do {
		rv = dfuse_get_status(dev, status);
		if (rv < 0)
			return rv;
	} while (status->bState == DFU_STATE_DFUDOWNLOADBUSY);

	/* Verify command validated and status ok */
	if (status->bStatus != DFU_STATUS_OK ||
	    status->bState != DFU_STATE_DFUDOWNLOADIDLE)
		return -ECANCELED;

	return 0;
}

/*
 * Dfuse command CLRSTATUS.
 * Asks a jump from state DFU_ERROR to DFU_IDLE
 *
 * Returns 0 on success, else a negative errno.
 */
static int dfuse_clear_status(struct sbre_dfuse *dev)
{
	int rv;

	rv = usb_control_msg(dev->usb_dev,
	                     usb_sndctrlpipe(dev->usb_dev, 0),
	                     DFU_REQ_CLRSTATUS, REQTYPE_SND,
	                     0, dev->itf_alt,
	                     0, 0, USB_IO_TIMEOUT);
	if (rv < 0) {
		dev_err(&dev->interface->dev, "Can't send clear status command.");
		return -EREMOTEIO;
	}

	return 0;
}

/*
 * Dfuse command BOOT.
 * Asks a jump from state DFU to firmware
 *
 * Returns 0 on success, else a negative errno.
 */
static int dfuse_boot(struct sbre_dfuse *dev)
{
	int rv;
	struct sbre_dfuse_status status;

	/* Send boot command (empty download command) */
	rv = usb_control_msg(dev->usb_dev,
	                     usb_sndctrlpipe(dev->usb_dev, 0),
	                     DFU_REQ_DL, REQTYPE_SND,
	                     0, dev->itf_alt,
	                     0, 0, USB_IO_TIMEOUT);
	if (rv < 0) {
		dev_err(&dev->interface->dev, "Can't send boot command.");
		return -EREMOTEIO;
	}
	/* Activate boot command with a getstatus command */
	rv = dfuse_get_status(dev, &status);
	if (rv < 0) {
		dev_err(&dev->interface->dev,
		        "Can't boot, state=%u, status=%u.",
		        status.bState, status.bStatus);
		return rv;
	}

	return 0;
}

/*
 * Try to bring back Dfuse device to idle state.
 * Returns 0 on success, else a negative errno.
 *
 * See ST user manual UM0424, 10.5.3 DFU state diagram.
 * http://www.st.com/resource/en/user_manual/cd00158241.pdf
 */
static int dfuse_to_idle(struct sbre_dfuse *dev)
{
	int rv, tries = 3; /* Max 3 steps to jump from any state to dfuIdle */
	struct sbre_dfuse_status status;

	do {
		rv = dfuse_get_status(dev, &status);
		if (rv < 0)
			return rv;
		switch (status.bState) {
		case DFU_STATE_APPIDLE:
		case DFU_STATE_APPDETACH:
			/* No support of DFU mode selection, can't do anything */
			return 0;
		case DFU_STATE_DFUERROR:
			/* DFU error, send a clrstatus command */
			rv = dfuse_clear_status(dev);
			if (rv < 0)
				return rv;
			break;
		case DFU_STATE_DFUDOWNLOADSYNC:
		case DFU_STATE_DFUDOWNLOADIDLE:
		case DFU_STATE_DFUMANIFESTSYNC:
		case DFU_STATE_DFUUPLOADIDLE:
			/* send an abort command */
			rv = usb_control_msg(dev->usb_dev,
			                     usb_sndctrlpipe(dev->usb_dev, 0),
			                     DFU_REQ_ABORT, REQTYPE_SND,
			                     0, dev->itf_alt,
			                     0, 0, USB_IO_TIMEOUT);
			if (rv < 0) {
				dev_err(&dev->interface->dev, "Can't send abort command.");
				return -EREMOTEIO;
			}
			break;
		}
	} while (status.bState != DFU_STATE_DFUIDLE && --tries);

	if (status.bState != DFU_STATE_DFUIDLE) {
		dev_err(&dev->interface->dev,
		        "Can't set status to idle, state=%u, status=%u.",
		        status.bState, status.bStatus);
		return -EBUSY;
	}

	return 0;
}

/*
 * Dfuse special command SETADDRESS. Set current device data pointer.
 * Returns 0 on success, else a negative errno.
 */
static int dfuse_set_address(struct sbre_dfuse *dev, uint32_t addr)
{
	int rv;
	struct sbre_dfuse_status status;
	uint8_t buf[5] = {DFUSE_CMD_SET_ADDRESS,
	                  addr&0xff, (addr>>8)&0xff,
	                  (addr>>16)&0xff, (addr>>24)&0xff};

	rv = usb_control_msg(dev->usb_dev,
	                     usb_sndctrlpipe(dev->usb_dev, 0),
	                     DFU_REQ_DL, REQTYPE_SND,
	                     0, dev->itf_alt,
	                     buf, sizeof(buf), USB_IO_TIMEOUT);

	if (rv != sizeof(buf))
		return rv < 0 ? rv : -EREMOTEIO;

	/* Wait end of download */
	rv = dfuse_wait_download_end(dev, &status);
	if (rv < 0) {
		dev_err(&dev->interface->dev,
		        "Can't set address 0x%08x, state=%u, status=%u.",
		        addr, status.bState, status.bStatus);
		return rv;
	}

	/* Clean status */
	rv = dfuse_to_idle(dev);
	if (rv < 0)
		return rv;

	return 0;
}

/*
 * Dfuse special command ERASESECTOR. Erase given sector address.
 * Returns 0 on success, else a negative errno.
 */
static int dfuse_erase_page(struct sbre_dfuse *dev, uint32_t addr)
{
	int rv;
	struct sbre_dfuse_status status;
	uint8_t buf[5] = {DFUSE_CMD_ERASE,
	                  addr&0xff, (addr>>8)&0xff,
	                  (addr>>16)&0xff, (addr>>24)&0xff};

	rv = usb_control_msg(dev->usb_dev,
	                     usb_sndctrlpipe(dev->usb_dev, 0),
	                     DFU_REQ_DL, REQTYPE_SND,
	                     0, dev->itf_alt,
	                     buf, sizeof(buf), USB_IO_TIMEOUT);

	if (rv != sizeof(buf))
		return rv < 0 ? rv : -EREMOTEIO;

	/* Wait end of download */
	rv = dfuse_wait_download_end(dev, &status);
	if (rv < 0) {
		dev_err(&dev->interface->dev,
		        "Can't erase page 0x%08x, state=%u, status=%u.",
		        addr, status.bState, status.bStatus);
		return rv;
	}

	return 0;
}

/*
 * Download a block to dfuse device.
 * Returns 0 on success, else a negative errno.
 */
static int dfuse_flash_block(struct sbre_dfuse *dev, uint16_t block,
                             uint8_t *data, uint32_t len)
{
	int rv;
	struct sbre_dfuse_status status;

	rv = usb_control_msg(dev->usb_dev,
	                     usb_sndctrlpipe(dev->usb_dev, 0),
	                     DFU_REQ_DL, REQTYPE_SND,
	                     block, dev->itf_alt,
	                     data, len, USB_IO_TIMEOUT);

	if (rv != len)
		return rv < 0 ? rv : -EREMOTEIO;

	/* Wait end of download */
	rv = dfuse_wait_download_end(dev, &status);
	if (rv < 0) {
		dev_err(&dev->interface->dev,
		        "Can't flash block %u, state=%u, status=%u.",
		        block, status.bState, status.bStatus);
		return rv;
	}

	return 0;
}

/*
 * Boot a dfuse device.
 *
 * Clear current status, set current address to given one, send boot command
 * and validate it with a getstatus.
 *
 * Returns 0 on success, else a negative errno.
 */
int sbre_dfuse_boot(struct sbre_dfuse *dev, uint32_t address)
{
	int rv;
	struct sbre_dfuse_status status;

	/* Verify and clear status */
	rv = dfuse_to_idle(dev);
	if (rv < 0)
		return rv;

	/* Set boot address */
	rv = dfuse_set_address(dev, address);
	if (rv < 0)
		return rv;

	/* Send boot command */
	rv = dfuse_boot(dev);
	if (rv < 0)
		return rv;

	/* Verify status */
	if (status.bStatus != DFU_STATUS_OK) {
		dev_err(&dev->interface->dev, "Can't leave, bStatus=%u.",
		        status.bStatus);
		return -EBUSY;
	}

	return 0;
}

/*
 * Read dfuse device page size from alternate setting string descriptor.
 * Returns page size on success, else 0
 *
 * See ST user manual UM0424, section 10.3.2 "DFU mode descriptor set".
 * http://www.st.com/resource/en/user_manual/cd00158241.pdf
 * Example: "@Internal Flash  /0x08000000/064*0002Kg" for STM32F070
 *
 * Feedbacks from dfu-util code:
 * - sector count and size may have more digits than documented
 * - space may appear between sector size and sector size multiplier
 * - sector size multiplier may be missing or replaced by a space
 *
 * Limitations:
 * - page size depends on memory sector it writes to. Here only single
 *   sector type devices is managed.
 *
 */
static uint32_t dfuse_guess_pagesize(struct sbre_dfuse *dev)
{
	uint8_t *desc = dev->itf_alt_strdesc;
	uint32_t pageSize = 0;

	/* Must start with '@' */
	if (desc[0] != '@')
		return 0;

	/* Skip name */
	while (*++desc && *desc != '/');
	if (!*desc)
		return 0;

	/* Skip start address */
	while (*++desc && *desc != '/');
	if (!*desc)
		return 0;

	/* Skip first sector count */
	while (*++desc && *desc != '*');
	if (!*desc)
		return 0;

	/* Parse sector size */
	while (*++desc && '0' <= *desc && *desc <= '9')
		pageSize = 10*pageSize + *desc - '0';
	if (!*desc)
		return 0;

	/* Parse sector multiplier */
	while (*desc && *desc == ' ')
		desc++;
	if (!*desc)
		return 0;
	if (*desc == 'K')
		pageSize *= 1024;
	else if (*desc == 'M')
		pageSize *= 1024 * 1024;
	/* 'B' or nothing means no multiplier */

	return pageSize;
}

/*
 * Erase Dfuse element memory. Erase all pages overlapping element data range.
 * Returns 0 on success, else a negative errno.
 */
static int dfuse_erase_element(struct sbre_dfuse *dev, uint32_t elementAddr,
                               uint32_t elementSize, uint32_t pageSize)
{
	int rv;
	uint32_t pageMask = ~(pageSize-1);

	while (elementSize) {
		rv = dfuse_erase_page(dev, elementAddr & pageMask);
		if (rv < 0)
			return rv;
		if (elementSize > pageSize) {
			elementAddr += pageSize;
			elementSize -= pageSize;
		} else {
			elementSize = 0;
		}
	}

	/* Cleanup status */
	rv = dfuse_to_idle(dev);
	if (rv < 0)
		return rv;

	return 0;
}

/*
 * Flash Dfuse element memory. Flash all element data.
 * Returns 0 on success, else a negative errno.
 */
static int dfuse_flash_element(struct sbre_dfuse *dev, uint32_t elementAddr,
                               uint32_t elementSize, const uint8_t *elementData)
{
	int rv;
	uint32_t xfer = dev->transferSize;
	uint16_t block = 2; /* See ST user manual UM0424, 10.5.4 Downloading and uploading. Data is written to AddressSet + (block-2) * transferSize */
	uint8_t *sendBuf;

	/* Allocate send buffer */
	sendBuf = kmalloc(dev->transferSize, GFP_KERNEL);
	if (!sendBuf)
		return -ENOMEM;

	/* Set start address */
	rv = dfuse_set_address(dev, elementAddr);
	if (rv < 0)
		goto exit;

	/* Write chunks */
	while (elementSize) {
		if (elementSize < dev->transferSize)
			xfer = elementSize;
		memcpy(sendBuf, elementData, xfer);
		rv = dfuse_flash_block(dev, block, sendBuf, xfer);
		if (rv < 0)
			goto exit;
		if (elementSize > xfer) {
			elementData += xfer;
			elementSize -= xfer;
			block++;
		} else {
			elementSize = 0;
		}
	}

	/* Cleanup status */
	rv = dfuse_to_idle(dev);
	if (rv < 0)
		goto exit;

exit:
	kfree(sendBuf);

	return rv;
}

/*
 * Dfuse setup device.
 *
 * Select USB interface alternate of flash partition and retrieve
 * firmware info stored in flash.
 *
 * Returns 0 on success, else a negative errno.
 */
int sbre_dfuse_setup_device(struct sbre_dfuse *dev)
{
	int rv;
	unsigned i;
	char *pWr;
	struct usb_host_interface *altSetting;
	static const char nonSeparator[] =
		"ABCDEFGHIJKLMNOPQRSTUVWXYZ"
		"abcdefghijklmnopqrstuvwxyz"
		"0123456789"
		"!#$%&()*+,-.:<=>?@[]^_{|}";

	/* Select alternate */
	for (i = 0; i < dev->interface->num_altsetting; i++) {
		altSetting = usb_altnum_to_altsetting(dev->interface, i);
		if (altSetting->string && strstr(altSetting->string, "Internal Flash")) {
			rv = usb_set_interface(dev->usb_dev,
			                       altSetting->desc.bInterfaceNumber,
			                       altSetting->desc.bAlternateSetting);
			if (rv < 0)
				return rv;
			dev->itf_alt = (int)i;
			snprintf(dev->itf_alt_strdesc, sizeof(dev->itf_alt_strdesc)-1,
			         altSetting->string);
			break;
		}
	}
	if (dev->itf_alt < 0) {
		dev_err(&dev->interface->dev,
		        "No matching alt setting found, aborting");
		return -ENOMEDIUM;
	}

	/* Cleanup status */
	rv = dfuse_to_idle(dev);
	if (rv < 0)
		return rv;

	/* Set address to fwInfo data */
	rv = dfuse_set_address(dev, sbre_firmware_info_address);
	if (rv < 0)
		return rv;

	/* Read fwInfo */
	rv = usb_control_msg(dev->usb_dev,
	                     usb_rcvctrlpipe(dev->usb_dev, 0),
	                     DFU_REQ_UL, REQTYPE_RCV,
	                     2, dev->itf_alt,
	                     dev->fwInfo, sizeof(dev->fwInfo), USB_IO_TIMEOUT);
	if (rv != sizeof(dev->fwInfo))
		return rv < 0 ? rv : -EREMOTEIO;

	/*
	 * Build firmware path name
	 * Source is firmware info, cut at first separator, in lower case.
	 * Separators are chars not in nonSeparator string.
	 */
	for (i = 0; i < sizeof(dev->fwInfo); i++)
		if (!strchr(nonSeparator, dev->fwInfo[i]))
			break;
	if (i != sizeof(dev->fwInfo)) {
		strcpy(dev->fwPath, "sbre-");
		strncat(dev->fwPath, dev->fwInfo, i);
		strcat(dev->fwPath, ".dfuse");
		for (pWr = dev->fwPath; *pWr; pWr++)
			*pWr = tolower(*pWr);
	} else {
		/*
		 * Name fallback.
		 * In case of empty firmware info, fallback to first SBRE device
		 * managed by this driver.
		 */
		dev_warn(&dev->interface->dev,
		         "Can't find firmware info, fallback to usb-i2c firmware.");
		sprintf(dev->fwPath, "sbre-usb-i2c.dfuse");
	}

	/* Build printable firmware info */
	pWr = dev->sInfo;
	pWr += sprintf(dev->sInfo, "fwInfo: \"");
	for (i = 0; i < sizeof(dev->fwInfo); i++) {
		if (isascii(dev->fwInfo[i]))
			*pWr++ = dev->fwInfo[i];
		else
			pWr += sprintf(pWr, "\\x%02x", dev->fwInfo[i]);
	}
	pWr += sprintf(pWr, "\" on usb %d.%d.",
	               dev->usb_dev->bus->busnum, dev->usb_dev->devnum);

	/* Cleanup status */
	rv = dfuse_to_idle(dev);
	if (rv < 0)
		return rv;

	return 0;
}

/*
 * Dfuse flash. Erase and flash pages overlapping dfuse file elements.
 * Returns 0 on success, else a negative errno.
 *
 * Simple dfuse flasher, does not test memory mapping, multiple flash sectors or
 * special memory protections. Only guess page size from first sector found.
 *
 * It is assumed that dev->transferSize is correctly set for this device
 * and memory section.
 */
int sbre_dfuse_flash(struct sbre_dfuse *dev, struct sbre_dfuse_file_simple *file)
{
	int rv;

	/* TODO verify file alt is selected alt, else change it */

	/* Retrieve page size */
	uint32_t pageSize = dfuse_guess_pagesize(dev);
	if (!pageSize) {
		dev_err(&dev->interface->dev, "Can't guess device page size.");
		return -EFAULT;
	}

	/* Erase */
	rv = dfuse_erase_element(dev, file->elementAddr,
	                         file->elementSize, pageSize);
	if (rv < 0)
		return rv;

	/* Flash */
	rv = dfuse_flash_element(dev, file->elementAddr,
	                         file->elementSize, file->element);
	if (rv < 0)
		return rv;

	return 0;
}

