/*
 * sbre_dfuse.h - SBRE DfuSe flasher - dfuse protocol
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

#pragma once

#include <linux/types.h>
#include <linux/kref.h>
#include <linux/usb.h>

/*
 * ST DfuSe commands utils
 * Execute DFU and DFuse commands to update/flash/boot STM32 dfuse devices.
 *
 * Limitations:
 * - No support of DFU mode selection, described into UM0424 section 10.3
 *   "DFU mode selection". Device must already be in bootloader.
 * - Device must store firmware info. Used for request_firmware() file path
 *   and verify if target is up-to-date.
 *
 * Documentations:
 * - Universal Serial Bus Device Class Specification for Device Firmware
 *   Update v1.1. http://www.usb.org/developers/docs/devclass_docs/DFU_1.1.pdf
 * - ST DFU protocol of STM32 bootloader AN3156.
 *   http://www.st.com/resource/en/application_note/cd00264379.pdf
 * - ST user manual UM0424, section 10.2 "DFU extension protocol".
 *   http://www.st.com/resource/en/user_manual/cd00158241.pdf
 */

/* Dfuse status */
struct sbre_dfuse_status {
	uint8_t bStatus;        /* Current state of the device */
	uint32_t bwPollTimeOut; /* ms timeout required before next command */
	uint8_t bState;         /* State that the device is going to enter
	                         * immediately following the getstatus answer
	                         */
	uint8_t iString;        /* String descriptor number */
};

/*
 * Driver private data
 *
 * struct usb_device *usb_dev: usb device handle.
 * struct usb_interface *interface: usb interface handle.
 * int itf_alt: active USB interface alternate.
 * uint8_t itf_alt_strdesc[256]: active USB interface alternate string descriptor.
 * uint8_t fwInfo[]: firmware info (raw, non printable).
 * char sInfo[]: firmware info (printable, non ascii bytes replaced by "\x%02x").
 * char fwPath[]: firmware file name.
 * uint16_t transferSize: payload size of download/upload blocks.
 *
 */
struct sbre_dfuse {
	struct usb_device *usb_dev;
	struct usb_interface *interface;
	struct kref kref;
	int itf_alt;
	uint8_t itf_alt_strdesc[256];
	uint8_t fwInfo[64];              /* Fixed size into firmware */
	char sInfo[27+4*64];             /* Max size is size of "fwInfo: \"\" on usb XXX.XXX.\0"
	                                  * plus 64 non printable chars in "\\xXX" notation
	                                  */
	char fwPath[12+64];              /* Max size is size of "sbre-.dfuse\0" + fwInfo size */
	uint16_t transferSize;
};

#define to_sbre_dfuse_dev(d) container_of(d, struct sbre_dfuse, kref)

/*
 * Dfuse setup device, SoftBank Robotics Europe-specific.
 * Select USB interface alternate of flash partition and retrieve fw info
 * stored in flash.
 *
 * Returns 0 on success, else a negative errno.
 */
int sbre_dfuse_setup_device(struct sbre_dfuse *dev);

/*
 * Boot a dfuse device. Clear current status, set current address to given one,
 * send boot command (empty download command) and validate it with a getstatus.
 *
 * Returns 0 on success, else a negative errno.
 */
int sbre_dfuse_boot(struct sbre_dfuse *dev, uint32_t address);

/* Dfuse file descriptor */
struct sbre_dfuse_file_simple;

/*
 * Dfuse flash. Erase and flash pages overlapping dfuse file elements.
 *
 * Returns 0 on success, else a negative errno.
 */
int sbre_dfuse_flash(struct sbre_dfuse *dev, struct sbre_dfuse_file_simple *file);
