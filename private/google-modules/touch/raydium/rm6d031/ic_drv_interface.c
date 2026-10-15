// SPDX-License-Identifier: GPL-2.0+
/*
 * Raydium TouchScreen driver.
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.

 * Copyright (c) 2021  Raydium tech Ltd.
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
 *
 * BSD LICENSE
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *  * Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *  * Neither the name of Google Inc. or Linaro Ltd. nor the names of
 *    its contributors may be used to endorse or promote products
 *    derived from this software without specific prior written
 *    permission.
 * * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
 * IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */


#ifdef __KERNEL__
#include <linux/delay.h>
#include <linux/mutex.h>
#include <linux/unistd.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#else
#include <stdint.h>
#include <string.h>
#endif
#include "config.h"
#include "ic_drv_interface.h"
#include "drv_interface.h"
#include "ic_drv_global.h"
#if DEVICE_RM6D031
#include "rm6d031_ic_control.h"
#include "rm6d031_ic_test.h"
#endif
#include "hal_interface.h"

struct st_test_threshold g_st_test_thd;
struct st_test_info g_st_test_info;
unsigned char g_u8_ic_test_state;
struct st_test_para  g_st_test_para;

unsigned char wearable_ic_test_read_info(void)
{
	if (hal_read_image_test_ini(TEST_INI_TEST_INFO, 16, (unsigned char *)&g_st_test_info) == ERROR) {
		DEBUGOUT("Read Test Info ERROR\r\n");
		return ERROR;
	}
	return SUCCESS;
}

unsigned char wearable_ic_test_info_init(void)
{


	if (!wearable_ic_test_read_info()) {
		DEBUGOUT("Read Info ERROR\r\n");
		return ERROR;
	}

	if (g_u16_test_items_host_cmd != 0)
		g_st_test_info.u16_ft_test_item &= g_u16_test_items_host_cmd;
	g_u16_panel_jig_set_test_items |= g_st_test_info.u16_ft_test_item;

	DEBUGOUT("TestItem:0x%x:0x%x\r\n", g_st_test_info.u16_ft_test_item, g_st_test_info.u16_ft_eng_item);
	DEBUGOUT("g_u16_panel_jig_set_test_items:0x%x\r\n", g_u16_panel_jig_set_test_items);
	if (hal_read_image_test_ini(TEST_INI_THRESHOLD, 36, (unsigned char *)&g_st_test_thd) == ERROR) {
		DEBUGOUT("Read THD Data ERROR\r\n");
		return ERROR;
	}
	DEBUGOUT("THD:\r\n");
	DEBUGOUT("%d,%d,%d,%d,\r\n", g_st_test_thd.i16_ft_test_open_lower_thd,
		 g_st_test_thd.i16_ft_test_short_upper_thd,
		 g_st_test_thd.i16_ft_test_short_lower_thd,
		 g_st_test_thd.i16_ft_test_single_cc_upper_thd);
	DEBUGOUT("%d,%d,%d,%d,\r\n", g_st_test_thd.i16_ft_test_single_cc_lower_thd,
		 g_st_test_thd.i16_ft_test_uniformity_bl_upper_thd,
		 g_st_test_thd.i16_ft_test_uniformity_bl_lower_thd,
		 g_st_test_thd.i16_ft_test_uniformity_cc_upper_thd);
	DEBUGOUT("%d,%d,%d,%d,\r\n", g_st_test_thd.i16_ft_test_uniformity_cc_lower_thd,
		 g_st_test_thd.i16_ft_test_panel_test_1_thd,
		 g_st_test_thd.i16_ft_test_panel_test_3_thd,
		 g_st_test_thd.i16_ft_test_panel_test_2_thd);

	if (hal_read_image_test_ini(TEST_INI_PARA, 48, (unsigned char *)&g_st_test_para) == ERROR) {
		DEBUGOUT("Read THD Data ERROR\r\n");
		return ERROR;
	}

	if (hal_read_image_test_ini(TEST_INI_RAW_DATA_3_CC, 96, (unsigned char *)g_u16_raw_data3_golden_cc_buf) == ERROR) {
		DEBUGOUT("Read Open CC Golden ERROR\r\n");
		return ERROR;
	}

	if (hal_read_image_test_ini(TEST_INI_UC_CC, 96, (unsigned char *)g_u16_uc_golden_cc_buf) == ERROR) {
		DEBUGOUT("Read UC Golden ERROR\r\n");
		return ERROR;
	}

	return SUCCESS;
}

unsigned char wearable_ic_test_init(void)
{
	if (!wearable_ic_test_info_init())
		return ERROR;

	if (!(g_st_test_info.u16_ft_eng_item & IC_TEST_ENG_ITEMS_PANEL_TEST_JIG))
		wearable_ic_test_init_buffer();

#if ENABLE_GPIO_TEST_OUTPUT
	Chip_GPIO_SetPinOutLow(LPC_GPIO, DONGLE_GPIO_PORT_2, DONGLE_GPIO_PIN_0);
	Chip_GPIO_SetPinOutLow(LPC_GPIO, DONGLE_GPIO_PORT_2, DONGLE_GPIO_PIN_2);
#endif
	return SUCCESS;
}

void wearable_ic_test_init_buffer(void)
{
	g_u32_wearable_test_result = WEARABLE_FT_TEST_RESULT_IC_INIT_STATE;
	memset(g_u32_test_result, 0, sizeof(g_u32_test_result));
	memset(g_u8_test_result, 0, sizeof(g_u8_test_result));
	g_u8_is_normal_fw = FALSE;

	if (!(g_st_test_info.u16_ft_eng_item & IC_TEST_ENG_ITEMS_PANEL_TEST_JIG)) {
		memset(g_i16_raw_data_1_short_buf, 0, sizeof(g_i16_raw_data_1_short_buf));
		memset(g_i16_raw_data_2_open_buf, 0, sizeof(g_i16_raw_data_2_open_buf));
		memset(g_u16_raw_data3_cc_buf, 0, sizeof(g_u16_raw_data3_cc_buf));
		memset(g_u16_uc_buf, 0, sizeof(g_u16_uc_buf));
#if !ENABLE_RAM_SHRINK
		memset(g_u16_cb, 0, sizeof(g_u16_cb));
#endif
		memset(g_u8_wearable_pin_map, RM6DC10_NA_P, sizeof(g_u8_wearable_pin_map));
	}

}

void handle_ic_test(void)
{
#if DEVICE_RM6D031
	do_ic_test_rm6d031();
#endif
}

/******************************************************************************
 * Function name:		handle_ic_display_write
 *
 * Descriptions:		handle write data to display by touch interface
 *
 * parameters:			p_u8_data,		Data
				u16_data_length,	data length


 * Returned value:		ERROR, SUCCESS
 *
 *****************************************************************************/
unsigned char handle_ic_display_write(
	unsigned char *p_u8_data,
	unsigned short u16_data_length)
{
	if (g_st_test_info.u16_ft_eng_item & IC_TEST_ENG_ITEMS_PANEL_TEST_JIG) {
		DEBUGOUT("[HDW] Bypass display setting in Panel Jig Test mode!\r\n");
		return SUCCESS;
	}
#if DEVICE_RM6D031
	if (write_driver_by_touch_if_rm6d031(p_u8_data, u16_data_length) == ERROR) {
		DEBUGOUT("[HDW] write_driver_by_touch_if_rm6d031 NG!\r\n");
		return ERROR;
	}
#endif
	return SUCCESS;
}

void handle_set_display_interface(unsigned char u8_interface)
{
	g_u8_display_interface = u8_interface;
	DEBUGOUT("%s: 0x%x \r\n", __func__, u8_interface);
}

