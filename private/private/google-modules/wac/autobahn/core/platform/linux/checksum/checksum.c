// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2025 Google, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/version.h>
#if (LINUX_VERSION_CODE < KERNEL_VERSION(6, 8, 0))
/* crc-ccitt is deprecated starting from 6.8 */
#include <linux/crc-ccitt.h>
#else
#include <linux/crc-itu-t.h>
#endif
#include <linux/crc32.h>
#include <linux/types.h>

#include "autobahn/platform/linux/checksum.h"

#define CRC16_INITIAL_VALUE         0xFFFF

#define CRC32_INITIAL_VALUE         0xFFFFFFFF
#define CRC32_FINAL_XOR_VALUE       0xFFFFFFFF

uint16_t abn_crc16_ccitt(u8 *input_buf, size_t size)
{
#if (LINUX_VERSION_CODE < KERNEL_VERSION(6, 8, 0))
	return crc_ccitt_false(CRC16_INITIAL_VALUE, input_buf, size);
#else
	return crc_itu_t(CRC16_INITIAL_VALUE, input_buf, size);
#endif
}

uint16_t abn_crc32(u8 *input_buf, size_t size)
{
	u32 crc = crc32(CRC32_INITIAL_VALUE, input_buf, size);

	crc = crc ^ CRC32_FINAL_XOR_VALUE;

	// The Autobahn frame only uses two bytes for the integrity check.
	// XOR the CRC32's upper and lower have to get a 16 bit checksum.
	uint16_t lower_half = crc & 0xFFFF;
	uint16_t upper_half = (crc >> 16) & 0xFFFF;

	return lower_half ^ upper_half;
}
