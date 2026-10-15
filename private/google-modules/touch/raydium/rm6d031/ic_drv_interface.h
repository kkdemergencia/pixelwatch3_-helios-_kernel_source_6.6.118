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

#define DEVICE_ID_RM6D030			0xF303
#define DEVICE_ID_RM6D031			0xF303
#define DEVICE_ID_RM6DC10			0xF111

#define I2C_ID_RM6D030				0x39
#define I2C_ID_RM6DC00				0x38

#define I2C_PDA2_BYTE_MODE			0x03
#define I2C_PDA2_WORD_MODE			0x43
#define I2C_BYTE_MODE				(0x80|0x20)
#define I2C_WORD_MODE				(0xc0|0x20)
#define I2C_MCU_MODE				0x40
#define I2C_PDA2_MODE				0x10
#define I2C_PDA_MODE				0x08

#define SPI_BYTE_MODE				(0x80|0x23)
#define SPI_WORD_MODE				(0xc0|0x23)
#define SPI_PDA2_BYTE_MODE			0x03
#define SPI_PDA2_WORD_MODE			0x43
#define SPI_MCU_MODE				0x40

#define I2C_INTERFACE				0
#define SPI_INTERFACE				1

#define DISPLAY_SPI_MODE			1
#define DISPLAY_MIPI_MODE			2
#define DISPLAY_TOUCH_2_DRIVER_MODE		3

#define DATA_REMAP_TO_IC_PIN			1
#define DATA_REMAP_TO_SENSOR_PAD		2

#define IC_TEST_RETRY_TIME			3
#define IC_TEST_TIME_OUT			(-1)
#define FT_TEST_STUATUS_PATTERN			0x5A00

#define FT_CMD_INIT				0x11
#define FT_CMD_DO_FT_TEST			0x14

#define IC_TEST_ITEMS_SYSTEM			0x0001
#define IC_TEST_ITEMS_BURN_FW			0x0002
#define IC_TEST_ITEMS_OPEN			0x0004
#define IC_TEST_ITEMS_SHORT			0x0008
#define IC_TEST_ITEMS_UC			0x0010
#define IC_TEST_ITEMS_FW_VERSION_CHECK		0x0020
#define IC_TEST_ITEMS_BURN_CC			0x0040
#define IC_TEST_ITEMS_PANEL_TEST_1		0x0080
#define IC_TEST_ITEMS_PANEL_TEST_2		0x0100
#define IC_TEST_ITEMS_PANEL_TEST_3		0x0200
#define IC_TEST_ITEMS_TOUCH_ID			0x0400
#define IC_TEST_ITEMS_RESV_1			0x0800
#define IC_TEST_ITEMS_SENSOR_IMPACT		0x1000
#define IC_TEST_ITEMS_PANEL_TEST_4		0x2000
#define IC_TEST_ITEMS_RESV_4			0x4000
#define IC_TEST_ITEMS_RESV_5			0x8000


#define IC_TEST_ENG_ITEMS_SET_DSP_INIT_AF_TP_RESET		0x0001
#define IC_TEST_ENG_ITEMS_JIG_VERIFY				0x0002
#define IC_TEST_ENG_ITEMS_500K_VIH				0x0004
#define IC_TEST_ENG_ITEMS_TEST_ALL				0x0008
#define IC_TEST_ENG_ITEMS_AUTO_BURN_FW				0x0010
#define IC_TEST_ENG_ITEMS_ENABLE_HBM				0x0020
#define IC_TEST_ENG_ITEMS_DSP_NO_DC_DATA			0x0040
#define IC_TEST_ENG_ITEMS_I2C_FLASHLESS				0x0080
#define IC_TEST_ENG_ITEMS_PANEL_TEST_JIG			0x0100
#define IC_TEST_ENG_ITEMS_CHECK_I2C_NID				0x0200
#define IC_TEST_ENG_ITEMS_IC_SUB_VERSION_CHECK			0x0400
#define IC_TEST_ENG_ITEMS_PANEL_MODEL_CHECK			0x0800
#define IC_TEST_ENG_ITEMS_DO_SLEEP_GOLDEN_CC			0x1000
#define IC_TEST_ENG_ITEMS_DATA3_MERAGE_UC			0x2000
#define IC_TEST_ENG_ITEMS_DISABLE_TP_ZONE			0x4000
#define IC_TEST_ENG_ITEMS_ENABLE_BURN_TEST_LOG			0x8000


#define WEARABLE_FT_TEST_RESULT_IC_INIT_STATE			0xDFFFE5DF
#define WEARABLE_FT_TEST_RESULT_PANEL_INIT_STATE		0x20001A20
#define WEARABLE_FT_TEST_RESULT_PASS				0x00000000
#define WEARABLE_FT_TEST_RESULT_OPEN_NG				0x00000001
#define WEARABLE_FT_TEST_RESULT_SHORT_NG			0x00000002
#define WEARABLE_FT_TEST_RESULT_TOUCH_ID_NG			0x00000004
#define WEARABLE_FT_TEST_RESULT_I2C_NG				0x00000008
#define WEARABLE_FT_TEST_RESULT_INT_NG				0x00000010
#define WEARABLE_FT_TEST_RESULT_PANEL_TEST_1_NG			0x00000020
#define WEARABLE_FT_TEST_RESULT_RESET_NG			0x00000040
#define WEARABLE_FT_TEST_RESULT_DRAWING_NG			0x00000080
#define WEARABLE_FT_TEST_RESULT_PRAM_NG				0x00000100
#define WEARABLE_FT_TEST_RESULT_NORMAL_FW_NG			0x00000200
#define WEARABLE_FT_TEST_RESULT_BURN_CC_NG			0x00000400
#define WEARABLE_FT_TEST_RESULT_PANEL_TEST_3_NG			0x00000800
#define WEARABLE_FT_TEST_RESULT_GET_DATA_NG			0x00001000
#define WEARABLE_FT_TEST_RESULT_FLASH_ID_NG			0x00002000
#define WEARABLE_FT_TEST_RESULT_DISPLAY_MODE_NG			0x00004000
#define WEARABLE_FT_TEST_RESULT_TEST_FW_VER_NG			0x00008000
#define WEARABLE_FT_TEST_RESULT_UC_NG				0x00010000
#define WEARABLE_FT_TEST_RESULT_SINGLE_CC_OPEN_NG		0x00020000
#define WEARABLE_FT_TEST_RESULT_SUB_FREQ_NOISE_NG		0x00040000
#define WEARABLE_FT_TEST_RESULT_CC_CALIBRATION_NG		0x00080000
#define WEARABLE_FT_TEST_RESULT_TEST_INIT_NG			0x00100000
#define WEARABLE_FT_TEST_RESULT_LOAD_TESTFW_NG			0x00200000
#define WEARABLE_FT_TEST_RESULT_BURN_FW_NG			0x00400000
#define WEARABLE_FT_TEST_RESULT_IC_FW_VERIFY_NG			0x00800000
#define WEARABLE_FT_TEST_RESULT_SENSOR_IMPACT_NG		0x01000000
#define WEARABLE_FT_TEST_RESULT_MCU_HOLD_NG			0x02000000
#define WEARABLE_FT_TEST_RESULT_JIG_NG				0x04000000
#define WEARABLE_FT_TEST_RESULT_I2C_NID_NG			0x08000000
#define WEARABLE_FT_TEST_RESULT_VIH_NG				0x10000000
#define WEARABLE_FT_TEST_RESULT_CMD_NG				0x20000000
#define WEARABLE_FT_TEST_RESULT_IC_SUB_VERSION_NG		0x40000000
#define WEARABLE_FT_TEST_RESULT_IC_VERSION_NG			0x80000000

#define WEARBLE_FT_BURN_CC_SYSTEM_NG_CASE						1
#define WEARBLE_FT_FW_VER_SYSTEM_NG_CASE						2
#define WEARBLE_FT_I2C_SYSTEM_NG_CASE							3
#define WEARBLE_FT_INT_SYSTEM_NG_CASE							4
#define WEARBLE_FT_LOAD_TEST_FW_SYSTEM_NG_CASE						5
#define WEARBLE_FT_PRAM_SYSTEM_NG_CASE							6
#define WEARBLE_FT_RESET_PIN_SYSTEM_NG_CASE						7


#define ENABLE_PANEL_JIG_LOG                                    0x01


// Define ic test state
enum IC_TEST_STATE {
	IC_TEST_INIT = 0,
	IC_TEST_SYSTEM,			//1
	IC_TEST_BURN_FW,		//2
	IC_TEST_BURN_CC,		//3
	IC_TEST_LOAD_TEST_FW,		//4
	IC_TEST_ENTER_FW_TEST_MODE,	//5
	IC_TEST_OPEN_SHORT,		//6
	IC_TEST_READ_TEST_FW_DATA,	//7
	IC_ENTER_NORMAL_FW,		//8
	IC_TEST_UC_UB,			//9
	IC_TEST_PANEL_PATTERN_TEST,	//10
	IC_TEST_PANEL_SENSOR_IMPACT,	//11
	IC_TEST_FW_VERSION_CHECK,	//12
	IC_TEST_EXIT,			//13
};

#pragma pack(push)
#pragma pack(1)
struct st_test_info {
	//-------- FT Test Info (Byte 16)-----------------//
	unsigned char u8_device_id;
	unsigned char u8_company_id;
	unsigned char u8_project_id;
	unsigned char u8_station_id;
	unsigned short u16_ft_test_item;
	unsigned short u16_ft_eng_item;
	unsigned short u16_ft_test_info_1;
	unsigned short u16_ft_test_info_2;
	unsigned short u16_ft_test_info_3;
	unsigned short u16_ft_test_info_4;
};
extern struct st_test_info g_st_test_info;

struct st_test_threshold {
	//-------- FT Test Threshold (Byte 26)----------------//
	unsigned char u8_ft_test_company_id;
	unsigned char u8_ft_test_station_id;
	signed short i16_ft_test_open_lower_thd;		//TEST_THD_01
	signed short i16_ft_test_short_upper_thd;		//TEST_THD_02
	signed short i16_ft_test_short_lower_thd;		//TEST_THD_03
	signed short i16_ft_test_single_cc_upper_thd;		//TEST_THD_04
	signed short i16_ft_test_single_cc_lower_thd;		//TEST_THD_05
	signed short i16_ft_test_uniformity_bl_upper_thd;	//TEST_THD_06
	signed short i16_ft_test_uniformity_bl_lower_thd;	//TEST_THD_07
	signed short i16_ft_test_uniformity_cc_upper_thd;	//TEST_THD_08
	signed short i16_ft_test_uniformity_cc_lower_thd;	//TEST_THD_09
	signed short i16_ft_test_panel_test_1_thd;		//TEST_THD_10
	signed short i16_ft_test_panel_test_3_thd;		//TEST_THD_11
	signed short i16_ft_test_panel_test_2_thd;		//TEST_THD_12
	//-------- reserve (Byte 10)-----------------//
	signed short i16_ft_test_panel_test_2_s2_thd;		//TEST_THD_13
	signed short i16_ft_test_drawing_width_thd;		//TEST_THD_14
	signed short i16_ft_test_sensor_impact_thd_inside;	//TEST_THD_15
	signed short i16_ft_test_sensor_impact_thd_outside;	//TEST_THD_16
	signed short i16_ft_test_thd_resv;
};
extern struct st_test_threshold g_st_test_thd;

struct st_test_para {
	//-------- FT Test Parameter (Byte 48)-----------------//
	unsigned char u8_touch_id_version_9;			//reserve
	unsigned char u8_dsp_ns_test_run_times;
	unsigned char u8_dsp_ns_test_frame;
	unsigned char u8_dsp_ns_switch_1_frame;			// Need to smaller than u8_dsp_ns_test_frame
	unsigned char u8_dsp_ns_switch_2_frame;			// Need to smaller than u8_dsp_ns_test_frame
	unsigned char u8_dsp_ns_switch_3_frame;			// Need to smaller than u8_dsp_ns_test_frame
	unsigned char u8_dsp_ns_switch_4_frame;			// Need to smaller than u8_dsp_ns_test_frame
	unsigned char u8_dsp_ns_dc_ratio;
	unsigned char u8_dsp_ns_mv_ratio;
	unsigned char u8_dsp_ns_retry;
	unsigned char u8_reserve_1;				//reserve
	unsigned char u8_dsp_ns_switch_hbm_frame;
	unsigned char u8_dsp_ns_gain;
	unsigned char u8_static_ns_test_run_times;
	unsigned char u8_static_ns_test_frame;
	unsigned char u8_static_ns_dc_ratio;
	unsigned char u8_static_ns_mv_ratio;
	unsigned char u8_static_ns_retry;
	unsigned char u8_sub_freq_ns_test_frame;
	unsigned char u8_check_i2c_nid;
	unsigned char u8_reserve_2;				//reserve
	unsigned char u8_ic_sub_version;
	unsigned char u8_touch_id_version_0;
	unsigned char u8_open_test_retry;
	unsigned char u8_touch_id_version_4;
	unsigned char u8_touch_id_version_5;
	unsigned char u8_open_test_frame;
	unsigned char u8_open_test_drv_wdt;
	unsigned char u8_open_test_trans_wdt;
	unsigned char u8_touch_id_version_1;
	unsigned char u8_touch_id_version_6;
	unsigned char u8_short_test_retry;
	unsigned char u8_touch_id_version_7;
	unsigned char u8_short_test_rpt;
	unsigned char u8_touch_id_version_8;
	unsigned char u8_touch_id_version_2;
	unsigned char u8_touch_id_version_3;
	unsigned char u8_uc_test_retry;
	unsigned char u8_drawing_shape;
	unsigned char u8_drawing_type;
	unsigned int u32_normal_fw_version;
	unsigned int u32_test_fw_version;
};

extern struct st_test_para g_st_test_para;

#pragma pack(pop)

extern unsigned char g_u8_ic_test_state;
extern unsigned char wearable_ic_test_read_info(void);
extern unsigned char wearable_ic_test_init(void);
extern void wearable_ic_test_init_buffer(void);

extern unsigned char wearable_ic_test_info_init(void);
extern void handle_ic_test(void);
extern unsigned char handle_ic_display_write(unsigned char *p_u8_data, unsigned short u16_data_length);

extern unsigned char handle_burn_log_to_flash(void);
extern void handle_set_display_interface(unsigned char u8_interface);
