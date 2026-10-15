// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2024 Google, Inc.
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

#include <linux/vmalloc.h>

#include "main.h"
#include "bl_nxp.h"

#define MAX_BUFFER_SIZE        1024

int autobahn_bl_open(struct autobahn_data *data)
{
	int ret;

	data->bl.tx_buffer = kmalloc(MAX_BUFFER_SIZE, GFP_KERNEL | GFP_DMA);
	if (!data->bl.tx_buffer) {
		ret = -ENOMEM;
		goto out;
	}

	data->bl.rx_buffer = kmalloc(MAX_BUFFER_SIZE, GFP_KERNEL | GFP_DMA);
	if (!data->bl.rx_buffer) {
		ret = -ENOMEM;
		goto free_tx;
	}

	ret = data->bl.open(&data->bl);

	if (ret == 0)
		return ret;

	kfree(data->bl.rx_buffer);
free_tx:
	kfree(data->bl.tx_buffer);
out:
	return ret;
}

void autobahn_bl_close(struct autobahn_data *data)
{
	data->bl.close(&data->bl);
	kfree(data->bl.tx_buffer);
	kfree(data->bl.rx_buffer);
}

static int autobahn_bl_read_generic_response(struct autobahn_data *data,
					     uint8_t expected_command_tag)
{
	struct autobahn_bl *bl = &data->bl;
	int ret, status;

	ret = bl->read_response(bl);
	if (ret < 0)
		return ret;
	if (bl->rx_buffer[1] != BL_FRAME_COMMAND ||
			bl->rx_buffer[6] != BL_RESPONSE_GENERIC ||
			bl->rx_buffer[14] != expected_command_tag)
		return -BL_ERR_BAD_RESPONSE;

	status = get_le32(bl->rx_buffer + 10);
	return -status;
}

int autobahn_bl_set_property(struct autobahn_data *data, uint32_t tag,
			     uint32_t value)
{
	int ret = data->bl.write_cmd(&data->bl, BL_COMMAND_SET_PROPERTY, 0, 2, tag,
			value);

	if (ret < 0)
		return ret;
	return autobahn_bl_read_generic_response(data, BL_COMMAND_SET_PROPERTY);
}

int autobahn_bl_write_memory(struct autobahn_data *data, const uint8_t *buffer,
			     uint32_t size, uint32_t address,
			     uint32_t *data_abort_offset)
{
	int sched = 0, chunk = BL_DATA_CHUNK_SIZE, offset;
	int ret;

	// command
	ret = data->bl.write_cmd(&data->bl, BL_COMMAND_WRITE_MEMORY,
			BL_FLAG_HAS_DATA, 3, address, size, 0);
	if (ret < 0)
		return ret;

	// initial response
	ret = autobahn_bl_read_generic_response(data, BL_COMMAND_WRITE_MEMORY);
	if (ret < 0)
		return ret;

	// data phase
	for (offset = 0; offset < size; offset += BL_DATA_CHUNK_SIZE) {
		if (size - offset < BL_DATA_CHUNK_SIZE)
			chunk = size - offset;
		ret = data->bl.write_data(&data->bl, buffer + offset, chunk);
		if (ret == -BL_FRAME_ABORT) {
			*data_abort_offset = offset;
			break;
		}
		if (ret < 0)
			return ret;
		if (++sched % 20 == 0)
			schedule(); // ~10-20ms
	}

	// final response
	return autobahn_bl_read_generic_response(data, BL_COMMAND_WRITE_MEMORY);
}

static int autobahn_bl_write_memory_retry(struct autobahn_data *data,
					  const uint8_t *buffer, uint32_t size,
					  uint32_t address, int max_attempts)
{
	int attempt, offset = 0, data_abort_offset = 0;
	int ret;

	for (attempt = 0; attempt < max_attempts; attempt++) {
		ret = autobahn_bl_write_memory(data, buffer + offset,
				size - offset, address + offset,
				&data_abort_offset);
		if (!data_abort_offset)
			break;
		offset += data_abort_offset;
		data_abort_offset = 0;
		pr_warn("autobahn: %s data abort offset=%d\n", __func__, offset);
	}
	return ret;
}

int autobahn_bl_execute(struct autobahn_data *data, uint32_t prgm_counter,
			uint32_t ram_addr, uint32_t stack_ptr)
{
	int ret = data->bl.write_cmd(&data->bl, BL_COMMAND_EXECUTE, 0, 3,
			prgm_counter, ram_addr, stack_ptr);
	if (ret < 0)
		return ret;
	return autobahn_bl_read_generic_response(data, BL_COMMAND_EXECUTE);
}

int autobahn_bl_download_firmware(struct autobahn_data *data,
				  const uint8_t *buffer, uint32_t size)
{

	uint32_t firmware_size = size;
	uint32_t firmware_offset = 0;

	// Handle signed firmware
	const uint32_t kSignedOffset = 0x1000;
	const uint32_t kMagicOffset = 0x400;
	const uint8_t kMagic[] = { 'C', 'H', 'R', 'E' };

	if (firmware_size > kSignedOffset &&
			memcmp(buffer + kMagicOffset, kMagic, sizeof(kMagic)) == 0) {
		firmware_offset = kSignedOffset;
		firmware_size -= kSignedOffset;
	}

	if (firmware_size < 64) {
		pr_err("autobahn: %s invalid size\n", __func__);
		return -ETOOSMALL;
	}

	return autobahn_bl_download_firmware_buffer(
			data, buffer + firmware_offset, firmware_size);
}

int autobahn_bl_download_firmware_buffer(struct autobahn_data *data,
					 const uint8_t *buffer,
					 uint32_t size)
{
	const uint32_t kSpOffset = 0;
	const uint32_t kPcOffset = 1;
	const uint32_t kLoadOffset = 13;
	uint32_t ram_addr, stack_ptr, prgm_counter;
	uint32_t *firmware = (uint32_t *)buffer;
	int ret = 0;

	ram_addr = firmware[kLoadOffset];
	stack_ptr = firmware[kSpOffset];
	prgm_counter = firmware[kPcOffset];

	// Enable GPIO mode using pin 4,29, shared with mcu_ap_wake_int.
	ret = autobahn_bl_set_property(data, 0x1c, 0x8000041d);
	if (ret < 0) {
		pr_err("autobahn: %s set_property ret=%d\n", __func__, ret);
		return ret;
	}

	ret = autobahn_bl_write_memory_retry(data, buffer, size, ram_addr, 10);
	if (ret < 0) {
		pr_err("autobahn: %s write_memory ret=%d\n", __func__, ret);
		return ret;
	}

	ret = autobahn_bl_execute(data, prgm_counter, ram_addr, stack_ptr);
	if (ret < 0) {
		pr_err("autobahn: %s execute ret=%d\n", __func__, ret);
		return ret;
	}
	return 0;
}
