/*
 * sbre_dfuse_file.c - SBRE DfuSe flasher - dfuse file handling
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

#include "sbre_dfuse_file.h"

#include <linux/string.h>
#include <linux/crc32.h>

/* Offset of fw info in uC flash image */
static const uint32_t sbre_usb_i2c_firmware_info_offset = 0xc0;

/* Verify dfuse file CRC */
static int sbre_dfuse_file_verify_crc(const uint8_t *buf, uint32_t len)
{
	uint32_t crc = 0xffffffff;
	const uint8_t *dwCrc = buf + len - 4;

	crc = ether_crc_le(len - 4, buf);
	crc ^= dwCrc[0] | (dwCrc[1]<<8) | (dwCrc[2]<<16) | (dwCrc[3]<<24);

	return crc == 0 ? 0 : -1;
}

/*
 * Parse dfuse file and fill struct sbre_dfuse_file_simple.
 * Returns 0 on success, else a negative number.
 *
 * Verify dfuse file format header and footer.
 *
 * Limitations:
 * - File is limited to 1 target and 1 element
 *
 * Documentation:
 * - ST User Manual UM0391 - DfuSe File Format Specification
 */
int sbre_dfuse_file_simple_parse(const uint8_t *buf, uint32_t len,
                                struct sbre_dfuse_file_simple *file)
{
	const uint8_t *prefix, *targetPrefix, *imageElement, *suffix;
	uint32_t dfuImageSize, dwTargetSize;
	const char *szTargetName = 0;

	/* Initialize */
	memset(file, 0, sizeof(*file));

	/* Verify CRC */
	if (sbre_dfuse_file_verify_crc(buf, len) < 0)
		return -1;

	/* Check prefix header (1 target 1 element only) */
	prefix = buf;
	if (memcmp(prefix, "DfuSe", 5) || prefix[5] != 1 || prefix[10] != 1)
		return -2;

	/* Verify total file size is coherent */
	dfuImageSize = prefix[6] | (prefix[7]<<8) |
	               (prefix[8]<<16) | (prefix[9]<<24);
	if (dfuImageSize != len-16)
		return -3;

	/* Check prefix header */
	targetPrefix = prefix + 11;
	if (memcmp(targetPrefix, "Target", 6) || targetPrefix[270] != 1 ||
	    targetPrefix[271] != 0 || targetPrefix[272] != 0 ||
	    targetPrefix[273] != 0)
		return -4;

	/* Check suffix header */
	suffix = buf + len - 16;
	if (suffix[6] != 0x1a || suffix[7] != 0x01 ||
	    memcmp(suffix+8, "UFD", 3) || suffix[11] != 16)
		return -5;

	/* Verify total file size is coherent */
	dwTargetSize = targetPrefix[266] | (targetPrefix[267]<<8) |
	               (targetPrefix[268]<<16) | (targetPrefix[269]<<24);
	imageElement = targetPrefix + 274;
	if (imageElement+dwTargetSize != suffix)
		return -6;

	/* Save target name (if any) */
	if (targetPrefix[7])
		szTargetName = &targetPrefix[11];

	/* Target USB interface alternate setting */
	file->alt = targetPrefix[6];

	/* Save element (firmware) loading address, size and data pointer */
	file->elementAddr = imageElement[0] | (imageElement[1]<<8) |
	                    (imageElement[2]<<16) | (imageElement[3]<<24);
	file->elementSize = imageElement[4] | (imageElement[5]<<8) |
	                    (imageElement[6]<<16) | (imageElement[7]<<24);
	file->element = imageElement+8;

	/* Save fw info pointer */
	file->fwInfo = file->element + sbre_usb_i2c_firmware_info_offset;

	/* Save target bcdDevice and USB VID/PID */
	file->bcdDevice = suffix[0] | (suffix[1]<<8);
	file->idProduct = suffix[2] | (suffix[3]<<8);
	file->idVendor = suffix[4] | (suffix[5]<<8);

	return 0;
}

