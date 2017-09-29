/*
 * sbre_dfuse_file.h - SBRE DfuSe flasher - dfuse file handling
 *
 * Copyright·(c)·2017 SoftBank Robotics Europe.
 * Stéphane Régnier <sregnier@softbankrobotics.com>
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

/*
 * Dfuse file data (simple, just 1 target and 1 element)
 *
 * uint8_t alt: target USB interface alternate setting
 * const uint8_t *fwInfo: pointer to firmware info field (64B wide)
 * const uint8_t *element: pointer to start of firmware.
 * uint32_t elementAddr: firmware load address
 * uint32_t elementSize: firmware size
 * uint16_t bcdDevice: target bcdDevice (not used)
 * uint16_t idVendor: target USB VID
 * uint16_t idProduct: target USB PID
 *
 * Documentation:
 * - ST User Manual UM0391 - DfuSe File Format Specification
 */
struct sbre_dfuse_file_simple {
	uint8_t alt;
	const uint8_t *fwInfo;
	const uint8_t *element;
	uint32_t elementAddr;
	uint32_t elementSize;
	uint16_t bcdDevice;
	uint16_t idVendor;
	uint16_t idProduct;
};

/*
 * Parse dfuse file and fill struct sbre_dfuse_file_simple.
 * Returns 0 on success, else a negative number.
 */
int sbre_dfuse_file_simple_parse(const uint8_t *buf, uint32_t len,
                                struct sbre_dfuse_file_simple *file);

