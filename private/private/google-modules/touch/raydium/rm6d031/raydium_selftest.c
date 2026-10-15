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

#include <linux/delay.h>
#include <linux/limits.h>
#include <linux/mutex.h>
#include <linux/unistd.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/timer.h>
#include <linux/time.h>
#include <linux/ktime.h>
#include <linux/rtc.h>
#include <linux/fs.h>

#include <linux/uaccess.h>

#include "hal_interface.h"
#include "drv_interface.h"
#include "ic_drv_interface.h"
#include "raydium_selftest.h"
#include "ic_drv_global.h"
#include "raydium_driver.h"
#include "rm6d031_ic_test.h"

#define RM_SELF_TEST_CUSTOMER_VERSION		0x08
#define RM_SELF_TEST_PLATFORM_VERSION		0x01
#define RM_SELF_TEST_PROJECT_VERSION		0x40
#define RM_SELF_TEST_MAIN_VERSION		1
#define RM_SELF_TEST_SUB_VERSION		0

#define RESULT_SUCCESS				0
#define RESULT_NG				1

#define RELATIVE_PATH				0
#define WRITE_STRING_MAX_SIZE			1000

#define WRITE_TO_FILE_AND_SHELL(size_limit, data_offset, fmt, ...)                             \
	do {                                                                                   \
		self_test_save_to_file(RM_SELF_TEST_LOGFILE, write_string,                     \
				       scnprintf(write_string, WRITE_STRING_MAX_SIZE, fmt,     \
						 ##__VA_ARGS__));                              \
		data_offset += scnprintf((p_i8_buf + data_offset), (size_limit - data_offset), \
					 "%s", write_string);                                  \
	} while (0)

unsigned char g_create_file_flag;
unsigned char g_u8_normal_fw_version_buf[4];
char str_ini_path[100];

/* Protected by `g_raydium_ts->sensor_impact_lock` */
static unsigned short g_u16_sensor_impact[MAX_SENSING_PIN_NUM];

static int self_test_all(void)
{
	int ret = 0;

	g_u8_raydium_flag |= ENG_MODE;
	handle_ic_test();
	ret = g_u32_wearable_test_result;

	/*g_u8_raydium_flag &= ~ENG_MODE;*/
	DEBUGOUT("%s finish\n", __func__);

	return ret;
}

int self_test_save_to_file(char *file_name, char *p_string, short len)
{
#if defined(WRITE_TO_USERPSACE)
	struct file *filp = NULL;

	if (g_create_file_flag == 0)
		filp = filp_open(file_name, O_RDWR | O_CREAT | O_APPEND, 0666);
	else
		filp = filp_open(file_name, O_RDWR | O_CREAT, 0666);
	if (IS_ERR(filp)) {
		DEBUGOUT("can't open file:%s\n", RM_SELF_TEST_LOGFILE);
		return 0;
	}

	kernel_write(filp, p_string, len, &filp->f_pos);

	filp_close(filp, NULL);

	return 1;
#else
	return 0;
#endif
}

static int raydium_check_ini_version(void)
{
	int ret = 0;
	unsigned int u32_test_version;

	memcpy(&u32_test_version, &g_rad_testpara_image[4], 4);

	if (u32_test_version != g_st_test_para.u32_test_fw_version) {
		DEBUGOUT("test fw versio 0x%X != ini version 0x%X\n", u32_test_version,
			 g_st_test_para.u32_test_fw_version);
		ret = WEARABLE_FT_TEST_RESULT_TEST_FW_VER_NG;
	}

	return ret;
}

static int self_test_init(void)
{
	int ret = 0;
	unsigned int u32_dev_id = 0;

	g_u8_drv_interface = I2C_INTERFACE;
	g_u16_dev_id = DEVICE_ID_RM6D031;

	if (handle_ic_read(RAYDIUM_CHK_I2C_CMD, 4, (unsigned char *)&u32_dev_id, g_u8_drv_interface,
			   I2C_WORD_MODE) == ERROR) {
		ret = WEARABLE_FT_TEST_RESULT_JIG_NG;
		return ret;
	}
	g_u16_dev_id = ((u32_dev_id & 0xFFFF0000) >> 16);

	if (g_u16_dev_id == DEVICE_ID_RM6D031) {
		handle_ic_read(RAYDIUM_PDA_FW_VERSION_ADDR, 4, g_u8_normal_fw_version_buf,
			       g_u8_drv_interface, I2C_WORD_MODE);
		DEBUGOUT("FW Version=0x%.2X%.2X%.2X%.2X\n", g_u8_normal_fw_version_buf[0],
			 g_u8_normal_fw_version_buf[1], g_u8_normal_fw_version_buf[3],
			 g_u8_normal_fw_version_buf[2]);
	} else {
		DEBUGOUT("read ic namd fail\n");
		ret = WEARABLE_FT_TEST_RESULT_TEST_INIT_NG;
		return ret;
	}
	if (!wearable_ic_test_init())
		ret = WEARABLE_FT_TEST_RESULT_TEST_INIT_NG;
	if (raydium_check_ini_version() != 0)
		ret = WEARABLE_FT_TEST_RESULT_TEST_FW_VER_NG;

	return ret;
}

int get_selftest_start_time(char *write_string, unsigned int size_limit)
{
#if defined(RTC_TIME_SUPPORT)
	struct rtc_time t = rtc_ktime_to_tm(ktime_get_real());

	DEBUGOUT("%ptRs\n", &t);
	return scnprintf(write_string, size_limit, "start test time: %ptRs\n", &t);
#else
	return 0;
#endif
}

int self_test_save_test_raw_data_to_file(int i32_ng_type, char *p_i8_buf, short size_limit)
{
	/*struct tm *time_infor;*/
	/*time_t raw_time;*/
	unsigned char u8_i = 0, u8_j = 0;
	unsigned int data_offset = 0;
	short *p_i16_buf = (short *)g_u16_raw_data_tmp;
	char *write_string;

	write_string = kzalloc(WRITE_STRING_MAX_SIZE, GFP_KERNEL);
	if (!write_string) {
		DEBUGOUT("failed to allocate memory to store selftest result\n");
		return -1;
	}

	/*FW Version*/
	if (g_u16_dev_id != 0) {
		WRITE_TO_FILE_AND_SHELL(size_limit, data_offset, "FW Version=0x%.2X%.2X%.2X%.2X\n",
					g_u8_normal_fw_version_buf[0],
					g_u8_normal_fw_version_buf[1],
					g_u8_normal_fw_version_buf[2],
					g_u8_normal_fw_version_buf[3]);
	}

	/*Version*/
	WRITE_TO_FILE_AND_SHELL(size_limit, data_offset, "Selftest Version=%x.%x.%x.%x.%x\n",
				RM_SELF_TEST_CUSTOMER_VERSION, RM_SELF_TEST_PLATFORM_VERSION,
				RM_SELF_TEST_PROJECT_VERSION, RM_SELF_TEST_MAIN_VERSION,
				RM_SELF_TEST_SUB_VERSION);

	/*Test result*/

	WRITE_TO_FILE_AND_SHELL(size_limit, data_offset, "Test Result = 0x%08X\n", i32_ng_type);

	if (i32_ng_type == 0) {
		WRITE_TO_FILE_AND_SHELL(size_limit, data_offset, "All pass\n");
	} else {
		if (i32_ng_type & WEARABLE_FT_TEST_RESULT_JIG_NG)
			WRITE_TO_FILE_AND_SHELL(size_limit, data_offset, "SYSTEM NG ");

		if (i32_ng_type & WEARABLE_FT_TEST_RESULT_I2C_NG)
			WRITE_TO_FILE_AND_SHELL(size_limit, data_offset, "I2C NG ");

		if (i32_ng_type & WEARABLE_FT_TEST_RESULT_INT_NG)
			WRITE_TO_FILE_AND_SHELL(size_limit, data_offset, "INT NG ");

		if (i32_ng_type & WEARABLE_FT_TEST_RESULT_RESET_NG)
			WRITE_TO_FILE_AND_SHELL(size_limit, data_offset, "RESET NG ");

		if (i32_ng_type & WEARABLE_FT_TEST_RESULT_PRAM_NG)
			WRITE_TO_FILE_AND_SHELL(size_limit, data_offset, "PRAM NG ");

		if (i32_ng_type & WEARABLE_FT_TEST_RESULT_NORMAL_FW_NG)
			WRITE_TO_FILE_AND_SHELL(size_limit, data_offset, "NORMAL_FW_NG ");

		if (i32_ng_type & WEARABLE_FT_TEST_RESULT_OPEN_NG)
			WRITE_TO_FILE_AND_SHELL(size_limit, data_offset, "OPEN NG ");

		if (i32_ng_type & WEARABLE_FT_TEST_RESULT_SHORT_NG)
			WRITE_TO_FILE_AND_SHELL(size_limit, data_offset, "SHORT NG ");

		if (i32_ng_type & WEARABLE_FT_TEST_RESULT_BURN_CC_NG)
			WRITE_TO_FILE_AND_SHELL(size_limit, data_offset, "BURN CC NG ");

		if (i32_ng_type & WEARABLE_FT_TEST_RESULT_GET_DATA_NG)
			WRITE_TO_FILE_AND_SHELL(size_limit, data_offset, "GET DATA NG ");

		if (i32_ng_type & WEARABLE_FT_TEST_RESULT_FLASH_ID_NG)
			WRITE_TO_FILE_AND_SHELL(size_limit, data_offset, "FLASH ID NG ");

		if (i32_ng_type & WEARABLE_FT_TEST_RESULT_DISPLAY_MODE_NG)
			WRITE_TO_FILE_AND_SHELL(size_limit, data_offset, "DISPLAY MODE CHECK NG ");

		if (i32_ng_type & WEARABLE_FT_TEST_RESULT_TEST_FW_VER_NG)
			WRITE_TO_FILE_AND_SHELL(size_limit, data_offset, "TEST FW VER NG ");

		if (i32_ng_type & WEARABLE_FT_TEST_RESULT_TEST_INIT_NG)
			WRITE_TO_FILE_AND_SHELL(size_limit, data_offset, "TEST INIT NG ");

		if (i32_ng_type & WEARABLE_FT_TEST_RESULT_LOAD_TESTFW_NG)
			WRITE_TO_FILE_AND_SHELL(size_limit, data_offset, "LOAD TESTFW NG ");

		if (i32_ng_type & WEARABLE_FT_TEST_RESULT_BURN_FW_NG)
			WRITE_TO_FILE_AND_SHELL(size_limit, data_offset, "BURN FW NG ");

		if (i32_ng_type & WEARABLE_FT_TEST_RESULT_SINGLE_CC_OPEN_NG)
			WRITE_TO_FILE_AND_SHELL(size_limit, data_offset,
						"Open NG (Single Pin CC) ");

		if (i32_ng_type & WEARABLE_FT_TEST_RESULT_UC_NG)
			WRITE_TO_FILE_AND_SHELL(size_limit, data_offset, "Uniformity CC NG ");

		WRITE_TO_FILE_AND_SHELL(size_limit, data_offset, "\n");
	}

	if (i32_ng_type & WEARABLE_FT_TEST_RESULT_OPEN_NG) {
		WRITE_TO_FILE_AND_SHELL(size_limit, data_offset, "OPEN NG Pin:");
		for (u8_i = 0; u8_i < MAX_SENSING_PIN_NUM; u8_i++) {
			if ((g_u8_test_result[u8_i] & IC_TEST_NODE_STATE_OPEN_NG) ==
			    IC_TEST_NODE_STATE_OPEN_NG) {
				WRITE_TO_FILE_AND_SHELL(size_limit, data_offset, "%2d,", u8_i);
			}
		}
		WRITE_TO_FILE_AND_SHELL(size_limit, data_offset, "\n");
	}

	if (i32_ng_type & WEARABLE_FT_TEST_RESULT_SHORT_NG) {
		WRITE_TO_FILE_AND_SHELL(size_limit, data_offset, "Short NG Pin:");
		for (u8_i = 0; u8_i < MAX_SENSING_PIN_NUM; u8_i++) {
			if ((g_u8_test_result[u8_i] & IC_TEST_NODE_STATE_SHORT_NG) ==
			    IC_TEST_NODE_STATE_SHORT_NG) {
				WRITE_TO_FILE_AND_SHELL(size_limit, data_offset, "%2d,", u8_i);
			}
		}
		WRITE_TO_FILE_AND_SHELL(size_limit, data_offset, "\n");
	}

	if (i32_ng_type & WEARABLE_FT_TEST_RESULT_SINGLE_CC_OPEN_NG) {
		WRITE_TO_FILE_AND_SHELL(size_limit, data_offset, "OpenCC NG Pin:");
		for (u8_i = 0; u8_i < MAX_SENSING_PIN_NUM; u8_i++) {
			if ((g_u8_test_result[u8_i] & IC_TEST_NODE_STATE_SINGLE_CC_OPEN_NG) ==
			    IC_TEST_NODE_STATE_SINGLE_CC_OPEN_NG) {
				WRITE_TO_FILE_AND_SHELL(size_limit, data_offset, "%2d,", u8_i);
			}
		}
		WRITE_TO_FILE_AND_SHELL(size_limit, data_offset, "\n");
	}

	if (i32_ng_type & WEARABLE_FT_TEST_RESULT_UC_NG) {
		WRITE_TO_FILE_AND_SHELL(size_limit, data_offset, "UC NG Pin:");
		for (u8_i = 0; u8_i < MAX_SENSING_PIN_NUM; u8_i++) {
			if ((g_u8_test_result[u8_i] & IC_TEST_NODE_STATE_UC_NG) ==
			    IC_TEST_NODE_STATE_UC_NG) {
				WRITE_TO_FILE_AND_SHELL(size_limit, data_offset, "%2d,", u8_i);
			}
		}
		WRITE_TO_FILE_AND_SHELL(size_limit, data_offset, "\n");
	}

	/*Threshold*/
	WRITE_TO_FILE_AND_SHELL(
		size_limit, data_offset,
		"0x%02X, 0x%02X\n0x%02X, 0x%02X\n0x%02X, 0x%02X\n0x%02X, 0x%02X\n0x%02X, 0x%02X\n0x%02X, 0x%02X\n0x%02X, 0x%02X\n0x%02X, 0x%02X\n0x%02X, 0x%02X\n",
		(g_st_test_thd.i16_ft_test_open_lower_thd >> 8) & 0xFF,
		(g_st_test_thd.i16_ft_test_open_lower_thd & 0xFF),
		(g_st_test_thd.i16_ft_test_short_upper_thd >> 8) & 0xFF,
		(g_st_test_thd.i16_ft_test_short_upper_thd & 0xFF),
		(g_st_test_thd.i16_ft_test_short_lower_thd >> 8) & 0xFF,
		(g_st_test_thd.i16_ft_test_short_lower_thd & 0xFF),
		(g_st_test_thd.i16_ft_test_single_cc_upper_thd >> 8) & 0xFF,
		(g_st_test_thd.i16_ft_test_single_cc_upper_thd & 0xFF),
		(g_st_test_thd.i16_ft_test_single_cc_lower_thd >> 8) & 0xFF,
		(g_st_test_thd.i16_ft_test_single_cc_lower_thd & 0xFF),
		(g_st_test_thd.i16_ft_test_uniformity_bl_upper_thd >> 8) & 0xFF,
		(g_st_test_thd.i16_ft_test_uniformity_bl_upper_thd & 0xFF),
		(g_st_test_thd.i16_ft_test_uniformity_bl_lower_thd >> 8) & 0xFF,
		(g_st_test_thd.i16_ft_test_uniformity_bl_lower_thd & 0xFF),
		(g_st_test_thd.i16_ft_test_uniformity_cc_upper_thd >> 8) & 0xFF,
		(g_st_test_thd.i16_ft_test_uniformity_cc_upper_thd & 0xFF),
		(g_st_test_thd.i16_ft_test_uniformity_cc_lower_thd >> 8) & 0xFF,
		(g_st_test_thd.i16_ft_test_uniformity_cc_lower_thd & 0xFF));

	for (u8_i = 0; u8_i < MAX_SENSING_PIN_NUM; u8_i++) {
		WRITE_TO_FILE_AND_SHELL(size_limit, data_offset, "0x%2X,",
					g_u16_uc_golden_cc_buf[u8_i]);
	}

	WRITE_TO_FILE_AND_SHELL(size_limit, data_offset, "\n");

	for (u8_i = 0; u8_i < MAX_SENSING_PIN_NUM; u8_i++) {
		WRITE_TO_FILE_AND_SHELL(size_limit, data_offset, "0x%2X,",
					g_u16_raw_data3_golden_cc_buf[u8_i]);
	}

	WRITE_TO_FILE_AND_SHELL(size_limit, data_offset, "\n");

	if ((i32_ng_type & 0xFFF0FBF8) < 4) {
		if ((g_st_test_info.u16_ft_test_item & (IC_TEST_ITEMS_SHORT)) != 0) {
			/*Raw data*/
			/*Raw data slow*/
			WRITE_TO_FILE_AND_SHELL(size_limit, data_offset, "\n");

			WRITE_TO_FILE_AND_SHELL(size_limit, data_offset, "Short\n");
			/*Raw data1*/
			memset(p_i16_buf, 0, MAX_IMAGE_BUFFER_SIZE * 2);
			for (u8_i = 0; u8_i < MAX_SENSING_PIN_NUM; u8_i++)
				if (g_u8_wearable_pin_map[u8_i] != RM6D030_NA_P)
					p_i16_buf[g_u8_wearable_pin_map[u8_i]] =
						g_i16_raw_data_1_short_buf[u8_i];

			for (u8_j = 0; u8_j < g_u8_channel_y; u8_j++) {
				for (u8_i = 0; u8_i < g_u8_channel_x; u8_i++) {
					WRITE_TO_FILE_AND_SHELL(
						size_limit, data_offset, "%05d,",
						p_i16_buf[u8_i + u8_j * g_u8_channel_x]);
				}

				WRITE_TO_FILE_AND_SHELL(size_limit, data_offset, "\n");
			}
		}
		if ((g_st_test_info.u16_ft_test_item & (IC_TEST_ITEMS_OPEN)) != 0) {
			WRITE_TO_FILE_AND_SHELL(size_limit, data_offset, "\n");

			/*Raw data slow*/
			WRITE_TO_FILE_AND_SHELL(size_limit, data_offset, "Open\n");
			/*Raw data2*/
			memset(p_i16_buf, 0, MAX_IMAGE_BUFFER_SIZE * 2);
			for (u8_i = 0; u8_i < MAX_SENSING_PIN_NUM; u8_i++)
				if (g_u8_wearable_pin_map[u8_i] != RM6D030_NA_P)
					p_i16_buf[g_u8_wearable_pin_map[u8_i]] =
						g_i16_raw_data_2_open_buf[u8_i];

			for (u8_j = 0; u8_j < g_u8_channel_y; u8_j++) {
				for (u8_i = 0; u8_i < g_u8_channel_x; u8_i++) {
					WRITE_TO_FILE_AND_SHELL(
						size_limit, data_offset, "%05d,",
						p_i16_buf[u8_i + u8_j * g_u8_channel_x]);
				}

				WRITE_TO_FILE_AND_SHELL(size_limit, data_offset, "\n");
			}
		}
		if ((g_st_test_info.u16_ft_test_item &
		     (IC_TEST_ITEMS_OPEN | IC_TEST_ITEMS_SHORT)) != 0) {
			WRITE_TO_FILE_AND_SHELL(size_limit, data_offset, "\n");

			/*Raw data 3*/
			WRITE_TO_FILE_AND_SHELL(size_limit, data_offset, "Open CC\n");
			/*Raw data3*/
			memset(p_i16_buf, 0, MAX_IMAGE_BUFFER_SIZE * 2);
			for (u8_i = 0; u8_i < MAX_SENSING_PIN_NUM; u8_i++)
				if (g_u8_wearable_pin_map[u8_i] != RM6D030_NA_P)
					p_i16_buf[g_u8_wearable_pin_map[u8_i]] =
						g_u16_raw_data3_cc_buf[u8_i];

			for (u8_j = 0; u8_j < g_u8_channel_y; u8_j++) {
				for (u8_i = 0; u8_i < g_u8_channel_x; u8_i++) {
					WRITE_TO_FILE_AND_SHELL(
						size_limit, data_offset, "%05d,",
						p_i16_buf[u8_i + u8_j * g_u8_channel_x]);
				}

				WRITE_TO_FILE_AND_SHELL(size_limit, data_offset, "\n");
			}

			WRITE_TO_FILE_AND_SHELL(size_limit, data_offset, "\n");

			/*Raw data 3 - golden open cc*/
			WRITE_TO_FILE_AND_SHELL(size_limit, data_offset,
						"Raw Data 3 - Golden open cc\n");
			memset(p_i16_buf, 0, MAX_IMAGE_BUFFER_SIZE * 2);
			for (u8_i = 0; u8_i < MAX_SENSING_PIN_NUM; u8_i++)
				if (g_u8_wearable_pin_map[u8_i] != RM6D030_NA_P)
					p_i16_buf[g_u8_wearable_pin_map[u8_i]] =
						(g_u16_raw_data3_cc_buf[u8_i] -
						 g_u16_raw_data3_golden_cc_buf[u8_i]);

			for (u8_j = 0; u8_j < g_u8_channel_y; u8_j++) {
				for (u8_i = 0; u8_i < g_u8_channel_x; u8_i++) {
					WRITE_TO_FILE_AND_SHELL(
						size_limit, data_offset, "%05d,",
						p_i16_buf[u8_i + u8_j * g_u8_channel_x]);
				}
				WRITE_TO_FILE_AND_SHELL(size_limit, data_offset, "\n");
			}
		}
		if ((g_st_test_info.u16_ft_test_item & (IC_TEST_ITEMS_UC)) != 0) {
			WRITE_TO_FILE_AND_SHELL(size_limit, data_offset, "\n");
			/*Raw data Uniformity CC*/
			WRITE_TO_FILE_AND_SHELL(size_limit, data_offset, "Raw Data_UC\n");
			/*Raw data uc*/
			memset(p_i16_buf, 0, MAX_IMAGE_BUFFER_SIZE * 2);
			for (u8_i = 0; u8_i < MAX_SENSING_PIN_NUM; u8_i++)
				if (g_u8_wearable_pin_map[u8_i] != RM6D030_NA_P)
					p_i16_buf[g_u8_wearable_pin_map[u8_i]] = g_u16_uc_buf[u8_i];

			DEBUGOUT("Image:0x%x\n", p_i16_buf[0]);

			for (u8_j = 0; u8_j < g_u8_channel_y; u8_j++) {
				for (u8_i = 0; u8_i < g_u8_channel_x; u8_i++) {
					WRITE_TO_FILE_AND_SHELL(
						size_limit, data_offset, "%05d,",
						p_i16_buf[u8_i + u8_j * g_u8_channel_x]);
				}

				WRITE_TO_FILE_AND_SHELL(size_limit, data_offset, "\n");
			}

			WRITE_TO_FILE_AND_SHELL(size_limit, data_offset, "\n");

			/*Raw data Uniformity CC - golden CC*/
			WRITE_TO_FILE_AND_SHELL(size_limit, data_offset,
						"Raw Data_UC - Golden UC\n");

			memset(p_i16_buf, 0, MAX_IMAGE_BUFFER_SIZE * 2);
			for (u8_i = 0; u8_i < MAX_SENSING_PIN_NUM; u8_i++)
				if (g_u8_wearable_pin_map[u8_i] != RM6D030_NA_P)
					p_i16_buf[g_u8_wearable_pin_map[u8_i]] =
						(g_u16_uc_buf[u8_i] - g_u16_uc_golden_cc_buf[u8_i]);

			for (u8_j = 0; u8_j < g_u8_channel_y; u8_j++) {
				for (u8_i = 0; u8_i < g_u8_channel_x; u8_i++) {
					WRITE_TO_FILE_AND_SHELL(
						size_limit, data_offset, "%05d,",
						p_i16_buf[u8_i + u8_j * g_u8_channel_x]);
				}
				WRITE_TO_FILE_AND_SHELL(size_limit, data_offset, "\n");
			}
		}
	}

	WRITE_TO_FILE_AND_SHELL(size_limit, data_offset, "\n");
	kfree(write_string);
	return 1;
}

int raydium_do_selftest(struct raydium_ts_data *ts, char *p_i8_buf, short size_limit)
{
	int ret = RESULT_SUCCESS;
	unsigned char u8_retry = 3;
	u64 time_start, time_end, time_start2, time_end2;

	time_start = hal_get_system_time();

	pr_info("Selftest Version=%x.%x.%x.%x.%x\n", RM_SELF_TEST_CUSTOMER_VERSION,
		RM_SELF_TEST_PLATFORM_VERSION, RM_SELF_TEST_PROJECT_VERSION,
		RM_SELF_TEST_MAIN_VERSION, RM_SELF_TEST_SUB_VERSION);

	ic_drv_init();

	ret = self_test_init();
	if (ret != 0) {
		DEBUGOUT("mapping ic fw fail\n");
	} else {
		DEBUGOUT("Test all\n");
		ret |= self_test_all();
	}

	while (u8_retry--) {
		hal_gpio_touch_hw_reset();
		LOGI("hw reset success after selftest.\n");
		break;
	}
	g_u8_raydium_flag &= ~ENG_MODE;

	raydium_i2c_mode_control(ts->client, ENABLE_TOUCH_INT);
#if ENABLE_TIME_MEASUREMENT
	time_start2 = hal_get_system_time();
#endif
	self_test_save_test_raw_data_to_file(ret, p_i8_buf, size_limit);

#if ENABLE_TIME_MEASUREMENT
	time_end2 = hal_get_system_time();
	DEBUGOUT("Write log Finish(%llums)\n", time_end2 - time_start2);
#endif
	if (ret != 0) {
		DEBUGOUT("Selftest Test Result=0x%x\n", ret);
		ret = RESULT_NG;
		DEBUGOUT("Selftest Result=%d\n", ret);
	} else {
		DEBUGOUT("Selftest Pass ^_^!!!\n");
		ret = RESULT_SUCCESS;
	}

	time_end = hal_get_system_time();
	DEBUGOUT("All Test Finish(%llums)\n", time_end - time_start);

	return ret;
}

/* Acquire the `sensor_impact_lock` before calling this function */
int stimulus_test_save_test_raw_data_to_file_locked(int i32_ng_type, char *p_i8_buf,
						    unsigned int size_limit)
{
	unsigned char u8_i = 0, u8_j = 0;
	unsigned int u32_size = 0;
	int16_t *p_i16_buf = (int16_t *)g_u16_raw_data_tmp;

	/*get test time*/
	u32_size += get_selftest_start_time(p_i8_buf + u32_size, size_limit - u32_size);

	/*FW Version*/
	if (g_u16_dev_id != 0) {
		u32_size += scnprintf((p_i8_buf + u32_size), (size_limit - u32_size),
				      "FW Version=0x%.2X%.2X%.2X%.2X\n",
				      g_u8_normal_fw_version_buf[0], g_u8_normal_fw_version_buf[1],
				      g_u8_normal_fw_version_buf[2], g_u8_normal_fw_version_buf[3]);
	}

	/*Test result*/
	u32_size += scnprintf((p_i8_buf + u32_size), (size_limit - u32_size),
			      "Test Result = 0x%08X\n", i32_ng_type);

	if (i32_ng_type == 0) {
		u32_size += scnprintf((p_i8_buf + u32_size), (size_limit - u32_size),
				      "stimulus pass\n");
	} else {
		u32_size += scnprintf((p_i8_buf + u32_size), (size_limit - u32_size),
				      "stimulus NG\n\nstimulus NG Pin:\n");
		for (u8_i = 0; u8_i < MAX_SENSING_PIN_NUM; u8_i++) {
			if ((g_u8_test_result[u8_i] & IC_TEST_NODE_STATE_SENSOR_IMPACT_NG) ==
			    IC_TEST_NODE_STATE_SENSOR_IMPACT_NG) {
				u32_size += scnprintf((p_i8_buf + u32_size),
						      (size_limit - u32_size), "%2d,\n", u8_i);
			}
		}
	}

	/*Stimulus Golden*/
	u32_size +=
		scnprintf((p_i8_buf + u32_size), (size_limit - u32_size), "\nStimulus Golden\n");

	memset(p_i16_buf, 0, MAX_IMAGE_BUFFER_SIZE * 2);
	for (u8_i = 0; u8_i < MAX_SENSING_PIN_NUM; u8_i++)
		if (g_u8_wearable_pin_map[u8_i] != RM6D031_NA_P)
			p_i16_buf[g_u8_wearable_pin_map[u8_i]] = g_u16_sensor_impact[u8_i];

	for (u8_j = 0; u8_j < g_u8_channel_y; u8_j++) {
		for (u8_i = 0; u8_i < g_u8_channel_x; u8_i++) {
			u32_size += scnprintf((p_i8_buf + u32_size), (size_limit - u32_size),
					      "%05d, ", p_i16_buf[u8_i + u8_j * g_u8_channel_x]);
		}
		u32_size += scnprintf((p_i8_buf + u32_size), (size_limit - u32_size), "\n");
	}

	/*Stimulus THD*/
	u32_size += scnprintf((p_i8_buf + u32_size), (size_limit - u32_size), "\n\nStimulus THD\n");

	memset(p_i16_buf, 0, MAX_IMAGE_BUFFER_SIZE * 2);
	for (u8_i = 0; u8_i < MAX_SENSING_PIN_NUM; u8_i++)
		if (g_u8_wearable_pin_map[u8_i] != RM6D031_NA_P)
			p_i16_buf[g_u8_wearable_pin_map[u8_i]] =
				(g_u16_sensor_impact[u8_i] *
				 g_st_test_thd.i16_ft_test_sensor_impact_thd_inside / 100);

	for (u8_j = 0; u8_j < g_u8_channel_y; u8_j++) {
		for (u8_i = 0; u8_i < g_u8_channel_x; u8_i++) {
			u32_size += scnprintf((p_i8_buf + u32_size), (size_limit - u32_size),
					      "%05d, ", p_i16_buf[u8_i + u8_j * g_u8_channel_x]);
		}
		u32_size += scnprintf((p_i8_buf + u32_size), (size_limit - u32_size), "\n");
	}

	/*Stimulus Data*/
	u32_size +=
		scnprintf((p_i8_buf + u32_size), (size_limit - u32_size), "\n\nStimulus Data\n");

	memset(p_i16_buf, 0, MAX_IMAGE_BUFFER_SIZE * 2);
	for (u8_i = 0; u8_i < MAX_IMAGE_BUFFER_SIZE; u8_i++)
		p_i16_buf[u8_i] = g_i16_sensor_test_data[u8_i];

	for (u8_j = 0; u8_j < g_u8_channel_y; u8_j++) {
		for (u8_i = 0; u8_i < g_u8_channel_x; u8_i++) {
			u32_size += scnprintf((p_i8_buf + u32_size), (size_limit - u32_size),
					      "%05d, ", p_i16_buf[u8_i + u8_j * g_u8_channel_x]);
		}
		u32_size += scnprintf((p_i8_buf + u32_size), (size_limit - u32_size), "\n");
	}

	return u32_size;
}

int update_sensor_impact(u16 *buf, size_t size)
{
	if (!buf) {
		LOGE("%s: null buffer\n", __func__);
		return -EINVAL;
	}
	if (size > sizeof(g_u16_sensor_impact)) {
		LOGE("%s: buffer size too large, get %zd\n", __func__, size);
		return -EINVAL;
	}
	mutex_lock(&g_raydium_ts->sensor_impact_lock);
	memcpy(g_u16_sensor_impact, buf, size);
	mutex_unlock(&g_raydium_ts->sensor_impact_lock);
	return 0;
}

int raydium_do_sensor_test_setup(struct i2c_client *client)
{
	int i32_ret = RESULT_NG;
	unsigned char u8_wbuffer[4];
	unsigned char u8_rbuffer[4];
	unsigned char u8_retry = 0;

	mutex_lock(&g_raydium_ts->lock);
	g_u8_raydium_flag |= ENG_MODE;

	ic_drv_init();

	self_test_init();

	memset(g_i16_sensor_test_data, 0x00, sizeof(g_i16_sensor_test_data));

	i32_ret = raydium_i2c_pda2_set_page(client, g_raydium_ts->is_suspend, RAYDIUM_PDA2_PAGE_0);
	if (i32_ret < 0)
		goto exit_i2c_error;

	//disable TP zone
	u8_wbuffer[0] = RAYDIUM_HOST_CMD_TP_MANUAL_MODE;
	u8_wbuffer[1] = 0;
	u8_wbuffer[2] = 0x81;
	u8_wbuffer[3] = 0;

	i32_ret = raydium_i2c_pda2_write(client, RAYDIUM_PDA2_HOST_CMD_ADDR, u8_wbuffer, 4);

	if (i32_ret < 0)
		goto exit_i2c_error;

	// Wait Test Command ready
	do {
		if (u8_rbuffer[0] == RAYDIUM_HOST_CMD_NO_OP)
			break;
		msleep(20);
		i32_ret = raydium_i2c_pda2_read(client, RAYDIUM_PDA2_HOST_CMD_ADDR, u8_rbuffer, 1);
		if (i32_ret < 0)
			goto exit_i2c_error;
		LOGD("RAD %s return 0x%02x!!\n", __func__, u8_rbuffer[0]);
	} while (u8_retry++ < (SYN_I2C_RETRY_TIMES * 2));

	u8_rbuffer[0] = RAYDIUM_HOST_CMD_CALIBRATION;
	i32_ret = raydium_i2c_pda2_write(client, RAYDIUM_PDA2_HOST_CMD_ADDR, u8_rbuffer, 1);
	if (i32_ret < 0)
		goto exit_i2c_error;

	do {
		if (u8_rbuffer[0] == RAYDIUM_HOST_CMD_NO_OP)
			break;

		msleep(1000);

		i32_ret = raydium_i2c_pda2_read(client, RAYDIUM_PDA2_HOST_CMD_ADDR, u8_rbuffer, 1);
		if (i32_ret < 0)
			goto exit_i2c_error;

		LOGD("RAD %s return 0x%02x!!\n", __func__, u8_rbuffer[0]);
	} while (u8_retry++ < (SYN_I2C_RETRY_TIMES * 2));

	msleep(20);

	//disable BL  update
	u8_wbuffer[0] = RAYDIUM_HOST_CMD_DIS_BS_UPDATE;
	u8_wbuffer[1] = 0;
	u8_wbuffer[2] = 0x01;
	u8_wbuffer[3] = 0;

	i32_ret = raydium_i2c_pda2_write(client, RAYDIUM_PDA2_HOST_CMD_ADDR, u8_wbuffer, 4);

	if (i32_ret < 0)
		goto exit_i2c_error;

	// Wait Test Command ready
	do {
		if (u8_rbuffer[0] == RAYDIUM_HOST_CMD_NO_OP)
			break;
		msleep(20);
		i32_ret = raydium_i2c_pda2_read(client, RAYDIUM_PDA2_HOST_CMD_ADDR, u8_rbuffer, 1);
		if (i32_ret < 0)
			goto exit_i2c_error;
		LOGD("RAD %s return 0x%02x!!\n", __func__, u8_rbuffer[0]);
	} while (u8_retry++ < (SYN_I2C_RETRY_TIMES * 2));

	//set function flah
	u8_wbuffer[0] = RAYDIUM_HOST_CMD_FT_FUN_FLAG;
	u8_wbuffer[1] = 0;
	u8_wbuffer[2] = 0x1;
	u8_wbuffer[3] = 0;

	i32_ret = raydium_i2c_pda2_write(client, RAYDIUM_PDA2_HOST_CMD_ADDR, u8_wbuffer, 4);

	if (i32_ret < 0)
		goto exit_i2c_error;

	// Wait Test Command ready
	do {
		if (u8_rbuffer[0] == RAYDIUM_HOST_CMD_NO_OP)
			break;
		msleep(20);
		i32_ret = raydium_i2c_pda2_read(client, RAYDIUM_PDA2_HOST_CMD_ADDR, u8_rbuffer, 1);
		if (i32_ret < 0)
			goto exit_i2c_error;
		LOGD("RAD %s return 0x%02x!!\n", __func__, u8_rbuffer[0]);
	} while (u8_retry++ < (SYN_I2C_RETRY_TIMES * 2));

	if (ft_test_read_used_pin_infor_rm6d031(g_u8_wearable_pin_map) == ERROR) {
		i32_ret = RESULT_NG;
		goto exit_i2c_error;
	}

	i32_ret = RESULT_SUCCESS;

exit_i2c_error:

	g_u8_raydium_flag &= ~ENG_MODE;
	mutex_unlock(&g_raydium_ts->lock);

	return i32_ret;
}

int raydium_do_stimulus_test(struct i2c_client *client, char *p_i8_buf, unsigned int size_limit)
{
	int i32_status = RESULT_NG;

	unsigned char u8_wbuffer[4];
	unsigned char u8_rbuffer[4];
	unsigned char u8_retry = 0, u8_retry_limit = 30;
	unsigned char u8_i = 0, u8_idx;
	unsigned int u32_size = 0;
	int temp_sum = 0;
	short i16_sensor_impact_golden_buffer[MAX_IMAGE_BUFFER_SIZE] = { 0 };
	short test_result_temp[MAX_IMAGE_BUFFER_SIZE];

	/* Required for accessing `g_u8_raydium_flag` */
	mutex_lock(&g_raydium_ts->lock);

	/* Required for accessing `g_u16_sensor_impact` */
	mutex_lock(&g_raydium_ts->sensor_impact_lock);

	g_u8_raydium_flag |= ENG_MODE;

	// Wait Test Command ready
	do {
		if (u8_rbuffer[0] == RAYDIUM_HOST_CMD_NO_OP)
			break;
		msleep(20);
		i32_status =
			raydium_i2c_pda2_read(client, RAYDIUM_PDA2_HOST_CMD_ADDR, u8_rbuffer, 1);
		if (i32_status < 0)
			goto exit_i2c_error;
		LOGD("RAD %s return 0x%02x!!\n", __func__, u8_rbuffer[0]);
	} while (u8_retry++ < (SYN_I2C_RETRY_TIMES * 2));

	for (u8_i = 0; u8_i < SENSOR_TEST_FRAME; u8_i++) {
		memset(u8_wbuffer, 0x00, 4);
		u8_wbuffer[RAD_HOST_CMD_POS] = RAYDIUM_HOST_CMD_NO_OP;
		u8_wbuffer[RAD_FT_CMD_POS] = 0x10;
		u8_wbuffer[RAD_FT_STATUS0_POS] = 0x01;

		i32_status = raydium_i2c_pda2_write(client, RAYDIUM_PDA2_HOST_CMD_ADDR, u8_wbuffer,
						    RAD_FT_CMD_LENGTH);
		if (i32_status < 0)
			goto exit_i2c_error;
		memset(u8_rbuffer, 0x00, 4);

		/* make sure update flag was set*/
		for (u8_retry = 0; u8_retry < u8_retry_limit; u8_retry++) {
			i32_status = raydium_i2c_pda2_read(client, RAYDIUM_PDA2_HOST_CMD_ADDR,
							   u8_rbuffer, RAD_FT_CMD_LENGTH);
			if (i32_status < 0)
				goto exit_i2c_error;

			if ((u8_rbuffer[RAD_FT_CMD_POS] & RAYDIUM_FT_UPDATE) == RAYDIUM_FT_UPDATE)
				break;

			usleep_range(500, 1500);
		}

		if (u8_retry == u8_retry_limit) {
			LOGD("RAD %s retry limit 0x%02x!!\n", __func__, u8_retry);

			i32_status = -1;
			goto exit_i2c_error;
		}

		i32_status =
			raydium_i2c_pda2_read(client, RAYDIUM_PDA2_RAWDATA_ADDR,
					      (unsigned char *)g_i16_raw_data_frame_buffer[u8_i],
					      MAX_IMAGE_BUFFER_SIZE * 2);

		LOGD("frame 0x%02x!!\n", u8_i);
		if (i32_status < 0)
			goto exit_i2c_error;
	}

	for (u8_idx = 0; u8_idx < MAX_IMAGE_BUFFER_SIZE; u8_idx++) {
		temp_sum = 0;
		for (u8_i = 0; u8_i < SENSOR_TEST_FRAME; u8_i++)
			temp_sum += g_i16_raw_data_frame_buffer[u8_i][u8_idx];
		g_i16_sensor_test_data[u8_idx] = temp_sum / SENSOR_TEST_FRAME;
	}

	for (u8_idx = 0; u8_idx < RM6D031_MAX_SENSING_PIN_NUM; u8_idx++) {
		if (g_u8_wearable_pin_map[u8_idx] != RM6D031_NA_P)
			i16_sensor_impact_golden_buffer[g_u8_wearable_pin_map[u8_idx]] =
				g_u16_sensor_impact[u8_idx];
	}

	/* Validate the return value and clear it */
	if (g_i16_sensor_test_data[SENSOR_IMPACT_VALIDATE_ADDR1] != SENSOR_IMPACT_VALIDATE_VALUE1 ||
	    g_i16_sensor_test_data[SENSOR_IMPACT_VALIDATE_ADDR2] != SENSOR_IMPACT_VALIDATE_VALUE2) {
		DEBUGOUT("Validtion of the read value failed, value = (%d, %d)",
			 g_i16_sensor_test_data[SENSOR_IMPACT_VALIDATE_ADDR1],
			 g_i16_sensor_test_data[SENSOR_IMPACT_VALIDATE_ADDR2]);
		i32_status = -EIO;
		goto exit_validation_error;
	}
	g_i16_sensor_test_data[SENSOR_IMPACT_VALIDATE_ADDR1] = 0;
	g_i16_sensor_test_data[SENSOR_IMPACT_VALIDATE_ADDR2] = 0;

	i32_status = RESULT_SUCCESS;

	for (u8_idx = 0; u8_idx < MAX_IMAGE_BUFFER_SIZE; u8_idx++) {
		test_result_temp[u8_idx] = ABS(g_i16_sensor_test_data[u8_idx] -
					       i16_sensor_impact_golden_buffer[u8_idx]);

		if ((i16_sensor_impact_golden_buffer[u8_idx] *
		     g_st_test_thd.i16_ft_test_sensor_impact_thd_inside / 100) <
		    test_result_temp[u8_idx]) {
			g_u8_test_result[u8_idx] |= IC_TEST_NODE_STATE_SENSOR_IMPACT_NG;

			i32_status = RESULT_NG;

			DEBUGOUT("NG_Location[%d] = %d:THD =%d\r\n", u8_idx,
				 test_result_temp[u8_idx],
				 (i16_sensor_impact_golden_buffer[u8_idx] *
				  g_st_test_thd.i16_ft_test_sensor_impact_thd_inside / 100));
		}
	}

exit_i2c_error:
exit_validation_error:
	u32_size += stimulus_test_save_test_raw_data_to_file_locked(
		i32_status, (p_i8_buf + u32_size), (size_limit - u32_size));

	if (i32_status != 0)
		DEBUGOUT("Stimulus Test Failed, Result=0x%x\n", i32_status);
	else
		DEBUGOUT("Stimulus Test Pass\n");

	hal_gpio_touch_hw_reset();
	LOGD("hw reset success after stimulus2.\n");
	g_u8_raydium_flag &= ~ENG_MODE;

	mutex_unlock(&g_raydium_ts->sensor_impact_lock);
	mutex_unlock(&g_raydium_ts->lock);
	return u32_size;
}

int raydium_do_noise_test(struct i2c_client *client, char *p_i8_buf, unsigned int size_limit)
{
	int i32_status = RESULT_NG;

	unsigned char u8_wbuffer[4];
	unsigned char u8_rbuffer[4];
	unsigned char u8_retry = 0, u8_retry_limit = 30;
	unsigned char u8_i = 0, u8_j = 0, u8_idx = 0;
	unsigned int u32_size = 0;
	int16_t i16_raw_data_frame_buffer[MAX_IMAGE_BUFFER_SIZE] = { 0 };
	int16_t i16_raw_data_frame_min[MAX_IMAGE_BUFFER_SIZE];
	int16_t i16_raw_data_frame_max[MAX_IMAGE_BUFFER_SIZE];
	int16_t *p_i16_buf = (int16_t *)g_u16_raw_data_tmp;

	static_assert(ARRAY_SIZE(i16_raw_data_frame_buffer) == ARRAY_SIZE(i16_raw_data_frame_min));
	static_assert(ARRAY_SIZE(i16_raw_data_frame_buffer) == ARRAY_SIZE(i16_raw_data_frame_max));

	/* Required for accessing `g_u8_raydium_flag` */
	mutex_lock(&g_raydium_ts->lock);
	g_u8_raydium_flag |= ENG_MODE;
	for (u8_idx = 0; u8_idx < ARRAY_SIZE(i16_raw_data_frame_buffer); u8_idx++) {
		i16_raw_data_frame_min[u8_idx] = SHRT_MAX;
		i16_raw_data_frame_max[u8_idx] = SHRT_MIN;
	}

	// Wait Test Command ready
	do {
		i32_status =
			raydium_i2c_pda2_read(client, RAYDIUM_PDA2_HOST_CMD_ADDR, u8_rbuffer, 1);
		if (i32_status < 0) {
			LOGE("%s: I2C read failed, ret = %d\n", __func__, i32_status);
			goto exit_i2c_error;
		}

		LOGD("RAD %s return 0x%02x!!\n", __func__, u8_rbuffer[0]);
		if (u8_rbuffer[0] == RAYDIUM_HOST_CMD_NO_OP)
			break;
		msleep(20);
	} while (u8_retry++ < SYN_I2C_RETRY_TIMES);

	for (u8_i = 0; u8_i < SENSOR_TEST_FRAME; u8_i++) {
		memset(u8_wbuffer, 0x00, sizeof(u8_wbuffer));
		u8_wbuffer[RAD_HOST_CMD_POS] = RAYDIUM_HOST_CMD_NO_OP;
		u8_wbuffer[RAD_FT_CMD_POS] = RAYDIUM_TOUCH_RAW_DATA;
		u8_wbuffer[RAD_FT_STATUS0_POS] = 0x01;

		i32_status = raydium_i2c_pda2_write(client, RAYDIUM_PDA2_HOST_CMD_ADDR, u8_wbuffer,
						    RAD_FT_CMD_LENGTH);
		if (i32_status < 0) {
			LOGE("%s: I2C write failed, ret = %d\n", __func__, i32_status);
			goto exit_i2c_error;
		}
		memset(u8_rbuffer, 0x00, sizeof(u8_rbuffer));

		/* make sure update flag was set*/
		for (u8_retry = 0; u8_retry < u8_retry_limit; u8_retry++) {
			i32_status = raydium_i2c_pda2_read(client, RAYDIUM_PDA2_HOST_CMD_ADDR,
							   u8_rbuffer, RAD_FT_CMD_LENGTH);
			if (i32_status < 0) {
				LOGE("%s: I2C read failed, ret = %d\n", __func__, i32_status);
				goto exit_i2c_error;
			}

			if ((u8_rbuffer[RAD_FT_CMD_POS] & RAYDIUM_FT_UPDATE) == RAYDIUM_FT_UPDATE)
				break;

			usleep_range(500, 1500);
		}

		if (u8_retry == u8_retry_limit) {
			LOGE("RAD %s retry limit 0x%02x!!\n", __func__, u8_retry);

			i32_status = -ETIMEDOUT;
			goto exit_i2c_error;
		}

		i32_status = raydium_i2c_pda2_read(client, RAYDIUM_PDA2_RAWDATA_ADDR,
						   (unsigned char *)i16_raw_data_frame_buffer,
						   sizeof(i16_raw_data_frame_buffer));
		if (i32_status < 0) {
			LOGE("%s: I2C read failed, ret = %d\n", __func__, i32_status);
			goto exit_i2c_error;
		}

		for (u8_idx = 0; u8_idx < ARRAY_SIZE(i16_raw_data_frame_buffer); u8_idx++) {
			i16_raw_data_frame_min[u8_idx] = min(i16_raw_data_frame_min[u8_idx],
							     i16_raw_data_frame_buffer[u8_idx]);
			i16_raw_data_frame_max[u8_idx] = max(i16_raw_data_frame_max[u8_idx],
							     i16_raw_data_frame_buffer[u8_idx]);
		}
	}

	i32_status = RESULT_SUCCESS;

	memset(p_i16_buf, 0, MAX_IMAGE_BUFFER_SIZE * 2);
	for (u8_idx = 0; u8_idx < ARRAY_SIZE(i16_raw_data_frame_buffer); u8_idx++)
		p_i16_buf[u8_idx] = i16_raw_data_frame_max[u8_idx] - i16_raw_data_frame_min[u8_idx];

	for (u8_j = 0; u8_j < g_u8_channel_y; u8_j++) {
		for (u8_i = 0; u8_i < g_u8_channel_x; u8_i++)
			u32_size += scnprintf((p_i8_buf + u32_size), (size_limit - u32_size),
					      "%05d, ", p_i16_buf[u8_i + u8_j * g_u8_channel_x]);
		u32_size += scnprintf((p_i8_buf + u32_size), (size_limit - u32_size), "\n");
	}

exit_i2c_error:
	if (i32_status != RESULT_SUCCESS)
		u32_size += scnprintf(p_i8_buf + u32_size, size_limit - u32_size,
				      "Noise Test Failed, Result=0x%x\n", i32_status);

	hal_gpio_touch_hw_reset();
	LOGD("hw reset success after noise2.\n");
	g_u8_raydium_flag &= ~ENG_MODE;

	mutex_unlock(&g_raydium_ts->lock);
	return u32_size;
}
