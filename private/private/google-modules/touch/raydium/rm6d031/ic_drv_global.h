/* SPDX-License-Identifier: GPL-2.0+ */
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


#ifndef _DRVGLOBAL_H_
#define _DRVGLOBAL_H_
#include "config.h"
#ifdef __KERNEL__
#include <linux/types.h>
#else
#include <stdint.h>
#endif


#ifdef __KERNEL__
enum STATUS {
	error = -1,
	success = 1
};

#else
#if !NXP_COMPILE
enum STATUS {
	ERROR = 0,
	SUCCESS = 1
};
enum FunctionalState {
	DISABLE = 0,
	ENABLE = 1
};


enum bool {
	false = 0,
	true = 1
};
#endif
#endif
/********* Basic Parameter Definition ***********
 ****************************************************************************
 *
 *                  Declared Global Variable
 *
 ****************************************************************************/

#define ENABLE_TEST_RSU_DATA_SHOW		0
#define ENABLE_FW_UPDATE_VERIFY_LOG		0
#define ENABLE_DISPLAY_PATTERN_VERIFY_LOG	0

#define MAX_IMAGE_BUFFER_SIZE			64
#define MAX_GP_IMAGE_BUFFER_SIZE		24	// (wearable) (GSF + GPS)
#if DEVICE_RM6D031
#define MAX_FT_IMAGE_BUFFER_SIZE		(MAX_IMAGE_BUFFER_SIZE + MAX_GP_IMAGE_BUFFER_SIZE)
#else
#define MAX_FT_IMAGE_BUFFER_SIZE		MAX_IMAGE_BUFFER_SIZE
#endif
#define MAX_SENSING_PIN_NUM			50	// (wearable)
#define DATA_BUFFER_SIZE			(0x100)
#define MAX_SUB_FREQ_NUM			4	// (F303 wearable)

#define SENSOR_SIDE_1	4
#define SENSOR_SIDE_2	12
#define SENSOR_SIDE_3	8

#define TEST_INI_TEST_INFO			0
#define TEST_INI_THRESHOLD			1
#define TEST_INI_PARA				2
#define TEST_INI_RAW_DATA2_BL			3
#define TEST_INI_RAW_DATA_3_CC			4
#define TEST_INI_UC_CC				5
#define TEST_INI_SENSOR_IMPACT			6

#define TEST_BL_IMG				0
#define TEST_INIT_IMG				1
#define TEST_USER_FW_IMG			2
#define TEST_USER_PARA_IMG			3
#define TEST_TEST_FW_IMG			4
#define TEST_TB_EXE_IMG				5

#define RM6D030_MAX_SENSING_PIN_NUM		48
#define RM6D030_NA_P				65

#define RM6D031_MAX_SENSING_PIN_NUM		48
#define RM6D031_NA_P				65

#define RM6DC10_MAX_SENSING_PIN_NUM		48
#define RM6DC10_NA_P				65

#define PRINT_DEBUG_MSG_DSP_FALSE		0x01
#define PRINT_DEBUG_MSG_DSP_S2			0x02
#define PRINT_DEBUG_MSG_DSP_S1			0x04
#define PRINT_DEBUG_MSG_OPEN_SHORT		0x08
#define PRINT_DEBUG_MSG_UC			0x10
#define PRINT_DEBUG_MSG_NOISE			0x20
#define PRINT_DEBUG_MSG_I2C_CMD			0x40
#define PRINT_DEBUG_MSG_TYPE_8			0x80

#define FW_SYS_CMD_ADDR				0x20000288

#define SYS_CMD_TP_MANUAL_MODE			0x34
#define SYS_CMD_DO_BL_CAL			0x5A
#define SYS_CMD_DO_CC_CAL			0x5C

#define FW_TP_SEQ_NUM_ADDR			0x20000290
#define FW_TP_POINT_DATA_ADDR			0x20000294
#define FW_TP_FINGER2_ADDR			0x200002A0
#define REG_SYSCON_MISCIER_ADDR			0x40000014
#define FW_FT_IMG_ADDR				0x2000019C
#define RM_DATAMEM0_BASE			0x20000000

#define RM6D030_DRAM_FT_DBG_START		0x2000004C
#define RM6DC10_DRAM_FT_DBG_START		0x2000004C

#define RAM_PANEL_TEST_DATA1_ADDR		0x10000f6c
#define RAM_PANEL_TEST_DATA2_ADDR		0x10000fd0
#define RAM_PANEL_TEST_DATA3_ADDR		0x10001034
#define RAM_PANEL_TEST_DATA4_ADDR		0x10001098
#define RAM_PANEL_TEST_DATA5_ADDR		0x100010fc
#define RAM_PANEL_TEST_DATA6_ADDR		0x10001160

#define IC_TEST_DATA_FRAME_BUFFER_NUMBER	101
#define I2C_PRIMARY_SECONDARY_TEST		0
#define WEARABLE_DISPLAY_FT_CMD_DELAY		0xFF
#define WEARABLE_SPI_MULTIPLE_CMD_KEYWORD	0x01
#define WEARABLE_SPI_LONG_CMD_KEYWORD		0x02
#define WEARABLE_SPI_END_LONG_CMD_KEYWORD	0x04
#define WEARABLE_MIPI_MULTIPLE_CMD_KEYWORD	0x10
#define WEARABLE_MIPI_LONG_CMD_KEYWORD		0x20
#define WEARABLE_MIPI_END_LONG_CMD_KEYWORD	0x40
#define WEARABLE_MIPI_REPEAT_PATTERN_KEYWORD	0x80
#define DONGLE_HID_STATE_REG_ADDR		0xB2
#define DONGLE_VERSION_REG_ADDR			0XB0
#define DONGLE_SPI_CLOCK_REG_ADDR		0xB3

#define SENSOR_IMPACT_VALIDATE_ADDR1		0
#define SENSOR_IMPACT_VALIDATE_ADDR2		8
#define SENSOR_IMPACT_VALIDATE_VALUE1		4
#define SENSOR_IMPACT_VALIDATE_VALUE2		1

//======================= Basic Hardware Define ==============================


//==================== End of Basic Hardware Define ==========================

//========== Basic Parameter Information ==========
/********************* Global *********************/

// Global variable for dongle
extern unsigned char g_u8_drv_interface;

extern unsigned char g_u8_gpio_irq_trigger;
extern unsigned short g_u16_dev_id;

//extern unsigned char g_u8_raw_data_buffer[MAX_IMAGE_BUFFER_SIZE * 2];
extern unsigned short g_u16_raw_data_tmp[MAX_IMAGE_BUFFER_SIZE];

extern short g_i16_raw_data_1_short_buf[MAX_SENSING_PIN_NUM];
extern short g_i16_raw_data_2_open_buf[MAX_SENSING_PIN_NUM];
extern unsigned short g_u16_raw_data3_cc_buf[MAX_SENSING_PIN_NUM];
extern unsigned short g_u16_uc_buf[MAX_SENSING_PIN_NUM];
#if ENABLE_TEST_DO_SLEEP_CC
extern unsigned short g_u16_uc_sleep_buf[MAX_SENSING_PIN_NUM];
#endif

extern unsigned short g_u16_raw_data3_golden_cc_buf[MAX_SENSING_PIN_NUM];
extern unsigned short g_u16_uc_golden_cc_buf[MAX_SENSING_PIN_NUM];
#if !ENABLE_RAM_SHRINK
extern unsigned short g_u16_cb[MAX_SENSING_PIN_NUM];
#endif
extern unsigned int g_u32_test_result[MAX_SENSING_PIN_NUM]; // each node test result (open ng, short ng, uniformity ng..etc)
extern unsigned char g_u8_wearable_pin_map[MAX_IMAGE_BUFFER_SIZE];
extern unsigned char g_u8_test_result[MAX_IMAGE_BUFFER_SIZE];

extern unsigned short g_u16_test_items_host_cmd;
extern unsigned short g_u16_test_items_tool_cmd;
extern unsigned short g_u16_panel_jig_set_test_items;

extern unsigned int g_u32_wearable_test_result;
extern unsigned char g_u8_channel_x;
extern unsigned char g_u8_channel_y;
extern unsigned char g_u8_is_normal_fw;
extern unsigned char g_u8_data_buf[DATA_BUFFER_SIZE];
extern unsigned char g_u8_mipi_read_buf[2];

extern unsigned int g_u32_fw_cc_version;
extern unsigned char g_u8_print_debug_msg;

extern bool b_is_auo_jig_testing;
#if ENABLE_TEST_TIME_MEASUREMENT || ENABLE_TEST_TIME_MEASUREMENT_CC
extern unsigned int g_u32_spend_time;
#endif


extern unsigned char g_u8_display_interface;
extern unsigned char g_u8_PAGE_ADDR;

extern unsigned int g_u32_pram_boot_addr;
extern unsigned int g_u32_pram_init_addr;
extern unsigned int g_u32_pram_tb_exe_addr;
extern unsigned int g_u32_pram_fw_addr;
extern unsigned int g_u32_pram_para_addr;
extern unsigned int g_u32_test_fw_addr;
extern unsigned int g_u32_flash_boot_addr;
extern unsigned int g_u32_flash_init_addr;
extern unsigned int g_u32_flash_fw_addr;
extern unsigned int g_u32_flash_para_addr;

extern unsigned char g_u8_test_log_burn_times;
extern bool g_b_is_burn_test_log_success;
extern unsigned int g_u32_i2c_speed;

extern short g_i16_raw_data_frame_buffer[IC_TEST_DATA_FRAME_BUFFER_NUMBER][MAX_IMAGE_BUFFER_SIZE];
extern short g_i16_sensor_test_data[MAX_IMAGE_BUFFER_SIZE];

extern unsigned char g_u8_display_pattern_sleep_to_active[17];
extern unsigned char g_u8_display_pattern_active_to_sleep[17];


extern void ic_drv_init(void);

#endif  /* end _DRVGLOBAL_H_*/

/******************************************************************************
 *                            End Of File
 *****************************************************************************/
