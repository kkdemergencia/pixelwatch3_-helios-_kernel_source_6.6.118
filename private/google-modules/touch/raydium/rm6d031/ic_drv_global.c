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


#include "config.h"
#include "ic_drv_global.h"
#include "ic_drv_interface.h"
#ifdef __KERNEL__
#include <linux/delay.h>
#include <linux/mutex.h>
#include <linux/unistd.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#endif

#include "drv_interface.h"
#include "hal_interface.h"

/*****************************************************************************
 *
 *                  Declared Global Variable
 *
 *****************************************************************************/

/********************* Global *********************/

unsigned char g_u8_display_pattern_sleep_to_active[17] = {
	0xF3, 0x01, 0x10, 0x00, 0x11, 0x00, 0x04,
	0x02, 0xFE, 0x00,
	0x01, 0x11,
	0x02, WEARABLE_DISPLAY_FT_CMD_DELAY, 40,
	0x01, 0x29,
};

unsigned char g_u8_display_pattern_active_to_sleep[17] = {
	0xF3, 0x01, 0x10, 0x00, 0x11, 0x00, 0x04,
	0x02, 0xFE, 0x00,
	0x01, 0x28,
	0x02, WEARABLE_DISPLAY_FT_CMD_DELAY, 40,
	0x01, 0x10,
};



unsigned short g_u16_dev_id;

unsigned char g_u8_drv_interface;

unsigned char g_u8_gpio_irq_trigger;

//unsigned char g_u8_raw_data_buffer[MAX_IMAGE_BUFFER_SIZE * 2];
unsigned short g_u16_raw_data_tmp[MAX_IMAGE_BUFFER_SIZE];

// IC Test
short g_i16_raw_data_1_short_buf[MAX_SENSING_PIN_NUM];
short g_i16_raw_data_2_open_buf[MAX_SENSING_PIN_NUM];
unsigned short g_u16_raw_data3_cc_buf[MAX_SENSING_PIN_NUM];
unsigned short g_u16_uc_buf[MAX_SENSING_PIN_NUM];
unsigned short g_u16_raw_data3_golden_cc_buf[MAX_SENSING_PIN_NUM];
unsigned short g_u16_uc_golden_cc_buf[MAX_SENSING_PIN_NUM];
#if !ENABLE_RAM_SHRINK
unsigned short g_u16_cb[MAX_SENSING_PIN_NUM];
#endif
#if ENABLE_TEST_DO_SLEEP_CC
unsigned short g_u16_uc_sleep_buf[MAX_SENSING_PIN_NUM];
#endif

unsigned int g_u32_test_result[MAX_SENSING_PIN_NUM]; // each node test result (bit[0]:open ng, bit[1]:short ng, bit[2]:uniformity ng..etc)
unsigned char g_u8_test_result[MAX_IMAGE_BUFFER_SIZE];

short g_i16_raw_data_frame_buffer[IC_TEST_DATA_FRAME_BUFFER_NUMBER][MAX_IMAGE_BUFFER_SIZE];
short g_i16_sensor_test_data[MAX_IMAGE_BUFFER_SIZE];

unsigned int g_u32_wearable_test_result;
unsigned char g_u8_wearable_pin_map[MAX_IMAGE_BUFFER_SIZE];


unsigned char g_u8_data_buf[DATA_BUFFER_SIZE];
unsigned char g_u8_mipi_read_buf[2];

unsigned char g_u8_channel_x;
unsigned char g_u8_channel_y;

unsigned char g_u8_is_normal_fw;

unsigned short g_u16_test_items_host_cmd;
unsigned short g_u16_test_items_tool_cmd;
unsigned char g_u8_ic_power_on_ng;
unsigned short g_u16_panel_jig_set_test_items;
bool b_is_auo_jig_testing;

#if ENABLE_TEST_TIME_MEASUREMENT || ENABLE_TEST_TIME_MEASUREMENT_CC
unsigned int g_u32_spend_time;
#endif

unsigned int g_u32_fw_cc_version;
unsigned char g_u8_print_debug_msg;
unsigned char g_u8_display_interface;
unsigned char g_u8_PAGE_ADDR;

unsigned int g_u32_pram_boot_addr;
unsigned int g_u32_pram_init_addr;
unsigned int g_u32_pram_tb_exe_addr;
unsigned int g_u32_pram_fw_addr;
unsigned int g_u32_pram_para_addr;
unsigned int g_u32_test_fw_addr;
unsigned int g_u32_flash_boot_addr;
unsigned int g_u32_flash_init_addr;
unsigned int g_u32_flash_fw_addr;
unsigned int g_u32_flash_para_addr;

unsigned char g_u8_test_log_burn_times;
bool g_b_is_burn_test_log_success = TRUE;
unsigned int g_u32_i2c_speed;


void ic_drv_init(void)
{
	g_u16_test_items_host_cmd = 0xFFFF;
	g_u16_test_items_tool_cmd = 0;

#if DEVICE_RM6D031
	g_u16_dev_id = DEVICE_ID_RM6D031;
	g_u8_i2c_nid = I2C_ID_RM6D030;
#endif
	g_u8_i2c_try_nid[0] = 0x39;
	g_u8_i2c_try_nid[1] = 0x38;
	g_u8_print_debug_msg = 0;
	g_u8_display_interface = DISPLAY_TOUCH_2_DRIVER_MODE;
	b_is_auo_jig_testing = false;
	g_u8_test_log_burn_times = 0;
}

/*****************************************************************************
 *                            End Of File
 *****************************************************************************/
