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
#include <stdlib.h>
#endif

#include "drv_interface.h"
#include "ic_drv_global.h"
#include "ic_drv_interface.h"
#include "rm6d031_ic_control.h"
#include "rm6d031_ic_test.h"
#include "rm6d031_ic_reg.h"


#include "hal_interface.h"

void test_log_raw_data_remap_rm6d031(short *p_i16_source_buf, short *p_i16_result_buf, unsigned char u8_data_remap)
{
	short i16_raw_data_tmp[MAX_IMAGE_BUFFER_SIZE];
	unsigned char u8_idx;
//	unsigned char u8_i, u8_j;

	memset(i16_raw_data_tmp, 0, sizeof(i16_raw_data_tmp));
	memcpy(i16_raw_data_tmp, p_i16_source_buf, MAX_IMAGE_BUFFER_SIZE * 2);


	if (u8_data_remap == DATA_REMAP_TO_IC_PIN) {
		for (u8_idx = 0; u8_idx < MAX_IMAGE_BUFFER_SIZE; u8_idx++) {
			if (g_u8_wearable_pin_map[u8_idx] != RM6D030_NA_P)
				p_i16_result_buf[u8_idx] = i16_raw_data_tmp[g_u8_wearable_pin_map[u8_idx]];
			else
				p_i16_result_buf[u8_idx] = 0;
		}
	} else if (u8_data_remap == DATA_REMAP_TO_SENSOR_PAD) {
		for (u8_idx = 0; u8_idx < MAX_IMAGE_BUFFER_SIZE; u8_idx++) {
			if (g_u8_wearable_pin_map[u8_idx] != RM6D030_NA_P)
				p_i16_result_buf[g_u8_wearable_pin_map[u8_idx]] = i16_raw_data_tmp[u8_idx];
		}
	}
}

unsigned char set_test_info_thd_para_rm6d031(void)
{
	unsigned char u8_read_buf[96] = {0}, u8_i;
	unsigned char *p_u8_input_buf = NULL;

	if (handle_ic_write(FT_TEST_INFO_ADDR, sizeof(g_st_test_info), (unsigned char *)&g_st_test_info, g_u8_drv_interface, I2C_WORD_MODE) == ERROR)
		return ERROR;

	if (handle_ic_read(FT_TEST_INFO_ADDR, sizeof(g_st_test_info), (unsigned char *)u8_read_buf, g_u8_drv_interface, I2C_BYTE_MODE) == ERROR)
		return ERROR;

	p_u8_input_buf = (unsigned char *)&g_st_test_info;
	for (u8_i = 0; u8_i < sizeof(g_st_test_info); u8_i++) {
		if (p_u8_input_buf[u8_i] != u8_read_buf[u8_i]) {
			DEBUGOUT("Set INFO NG:[%d]=%d->%d\r\n", u8_i, p_u8_input_buf[u8_i], u8_read_buf[u8_i]);
			return ERROR;
		}
	}

	if (handle_ic_write(FT_TEST_THD_ADDR, sizeof(g_st_test_thd), (unsigned char *)&g_st_test_thd, g_u8_drv_interface, I2C_WORD_MODE) == ERROR)
		return ERROR;
	if (handle_ic_read(FT_TEST_THD_ADDR, sizeof(g_st_test_thd), (unsigned char *)u8_read_buf, g_u8_drv_interface, I2C_BYTE_MODE) == ERROR)
		return ERROR;

	p_u8_input_buf = (unsigned char *)&g_st_test_thd;
	for (u8_i = 0; u8_i < sizeof(g_st_test_thd); u8_i++) {
		if (p_u8_input_buf[u8_i] != u8_read_buf[u8_i]) {
			DEBUGOUT("Set THD NG:[%d]=%d->%d\r\n", u8_i, p_u8_input_buf[u8_i], u8_read_buf[u8_i]);
			return ERROR;
		}
	}

	if (handle_ic_write(FT_TEST_PARA_ADDR, sizeof(g_st_test_para), (unsigned char *)&g_st_test_para, g_u8_drv_interface, I2C_WORD_MODE) == ERROR)
		return ERROR;
	if (handle_ic_read(FT_TEST_PARA_ADDR, sizeof(g_st_test_para), (unsigned char *)u8_read_buf, g_u8_drv_interface, I2C_BYTE_MODE) == ERROR)
		return ERROR;

	p_u8_input_buf = (unsigned char *)&g_st_test_para;
	for (u8_i = 0; u8_i < sizeof(g_st_test_para); u8_i++) {
		if (p_u8_input_buf[u8_i] != u8_read_buf[u8_i]) {
			DEBUGOUT("Set Para NG:[%d]=%d->%d\r\n", u8_i, p_u8_input_buf[u8_i], u8_read_buf[u8_i]);
			return ERROR;
		}
	}

	if (handle_ic_write(SRAM_FT_UC_CC_ADDR, 96, (unsigned char *)g_u16_uc_golden_cc_buf, g_u8_drv_interface, I2C_WORD_MODE) == ERROR)
		return ERROR;

	if (handle_ic_read(SRAM_FT_UC_CC_ADDR, 96, (unsigned char *)u8_read_buf, g_u8_drv_interface, I2C_BYTE_MODE) == ERROR)
		return ERROR;

	p_u8_input_buf = (unsigned char *)g_u16_uc_golden_cc_buf;
	for (u8_i = 0; u8_i < 96; u8_i++) {
		if (p_u8_input_buf[u8_i] != u8_read_buf[u8_i]) {
			DEBUGOUT("Set PRAM_FT_FW_GOLDEN_CC_ADDR NG:[%d]=%d->%d\r\n", u8_i, p_u8_input_buf[u8_i], u8_read_buf[u8_i]);
			return ERROR;
		}
	}

	//DEBUGOUT("g_u16_uc_golden_cc_buf[0]=%X,g_u16_uc_golden_cc_buf[1]=%X,g_u16_uc_golden_cc_buf[2]=%X,g_u16_uc_golden_cc_buf[3]=%X,g_u16_uc_golden_cc_buf[4]=%X,g_u16_uc_golden_cc_buf[5]=%X\r\n",g_u16_uc_golden_cc_buf[0],g_u16_uc_golden_cc_buf[1],g_u16_uc_golden_cc_buf[2],g_u16_uc_golden_cc_buf[3],g_u16_uc_golden_cc_buf[4],g_u16_uc_golden_cc_buf[5]);
	//DEBUGOUT("u8_read_buf[0]=%X,u8_read_buf[1]=%X,u8_read_buf[2]=%X,u8_read_buf[3]=%X,u8_read_buf[4]=%X,u8_read_buf[5]=%X\r\n",u8_read_buf[0],u8_read_buf[1],u8_read_buf[2],u8_read_buf[3],u8_read_buf[4],u8_read_buf[5]);

	if (handle_ic_write(SRAM_FT_RAWDATA_3_CC_ADDR, 96, (unsigned char *)g_u16_raw_data3_golden_cc_buf, g_u8_drv_interface, I2C_WORD_MODE) == ERROR)
		return ERROR;

	if (handle_ic_read(SRAM_FT_RAWDATA_3_CC_ADDR, 96, (unsigned char *)u8_read_buf, g_u8_drv_interface, I2C_BYTE_MODE) == ERROR)
		return ERROR;

	p_u8_input_buf = (unsigned char *)g_u16_raw_data3_golden_cc_buf;
	for (u8_i = 0; u8_i < 96; u8_i++) {
		if (p_u8_input_buf[u8_i] != u8_read_buf[u8_i]) {
			DEBUGOUT("Set PRAM_FT_OPEN_GOLDEN_CC_ADDR NG:[%d]=%d->%d\r\n", u8_i, p_u8_input_buf[u8_i], u8_read_buf[u8_i]);
			return ERROR;
		}
	}

	return SUCCESS;
}

void dump_image_data_rm6d031(short *p_image_buf, unsigned char u8_remap)
{
	short *p_i16_buf = (short *)g_u16_raw_data_tmp;
	unsigned char u8_i, u8_j;

	memset(p_i16_buf, 0, MAX_IMAGE_BUFFER_SIZE * 2);
	if (u8_remap) {
		for (u8_i = 0; u8_i < MAX_IMAGE_BUFFER_SIZE; u8_i++) {
			if (g_u8_wearable_pin_map[u8_i] != RM6D030_NA_P)
				p_i16_buf[g_u8_wearable_pin_map[u8_i]] = p_image_buf[u8_i];
		}
	} else {
		memcpy(p_i16_buf, p_image_buf, MAX_IMAGE_BUFFER_SIZE << 1);
	}

	for (u8_j = 0; u8_j < g_u8_channel_y; u8_j++) {
		for (u8_i = 0; u8_i < g_u8_channel_x; u8_i++)
			DEBUGOUT("%4d, ", p_i16_buf[u8_i + u8_j * g_u8_channel_x]);
		DEBUGOUT("\r\n");
	}
}

void dump_image_hex_data_rm6d031(short *p_image_buf)
{

	short *p_i16_buf = (short *)g_u16_raw_data_tmp;
	unsigned char u8_i, u8_j;

	memset(p_i16_buf, 0, MAX_IMAGE_BUFFER_SIZE * 2);
	for (u8_i = 0; u8_i < MAX_IMAGE_BUFFER_SIZE; u8_i++) {
		if (g_u8_wearable_pin_map[u8_i] != RM6D030_NA_P)
			p_i16_buf[g_u8_wearable_pin_map[u8_i]] = p_image_buf[u8_i];
	}

	for (u8_j = 0; u8_j < g_u8_channel_y; u8_j++) {
		for (u8_i = 0; u8_i < g_u8_channel_x; u8_i++)
			DEBUGOUT("%4X, ", (p_i16_buf[u8_i + u8_j * g_u8_channel_x] & 0x7ff));
		DEBUGOUT("\r\n");
	}
}

unsigned char check_test_fw_status_rm6d031(unsigned char u8_target_status, unsigned char *p_u8_result)
{
	unsigned int u32_read = 0;

	if (handle_ic_read(FW_FT_ARG1_ADDR, 4, (unsigned char *)(&u32_read), g_u8_drv_interface, I2C_WORD_MODE) == ERROR)
		return ERROR;

	if ((u32_read & FT_TEST_STUATUS_PATTERN) == FT_TEST_STUATUS_PATTERN) {
		if (((unsigned char)(u32_read & 0x00FF)) == u8_target_status)
			*p_u8_result = 1;
		else {
			DEBUGOUT("[CTFS] Test STATUS NG!! (%x:%x)\r\n", u8_target_status, u32_read);
			*p_u8_result = 0;
			return ERROR;
		}
		DEBUGOUT("[CTFS] Test FW STATUS (%x:%x)\r\n", u8_target_status, u32_read);
	} else {
		DEBUGOUT("[CTFS] Test STATUS Not Ready!! (%x:%x)\r\n", u8_target_status, u32_read);
		*p_u8_result = 0;
	}
	return SUCCESS;
}

unsigned char ft_test_do_fw_test_rm6d031(unsigned short u16_test_items)
{

	unsigned char u8_write_data[5], u8_result;
	unsigned int u32_read_data = 0;
	short i16_time_out = 0;
#if ENABLE_TEST_TIME_MEASUREMENT
	u64  u64_fun_time = 0;

	u64_fun_time = hal_get_system_time();
	DEBUGOUT("ft_test_do_fw_test Start Time= %d\r\n", u64_fun_time);
#endif

	g_u32_wearable_test_result &= ~WEARABLE_FT_TEST_RESULT_TEST_INIT_NG;

	if ((u16_test_items & (IC_TEST_ITEMS_OPEN | IC_TEST_ITEMS_SHORT | IC_TEST_ITEMS_UC)) == 0)
		return SUCCESS;
	memset(u8_write_data, 0, sizeof(u8_write_data));
	u8_write_data[1] = FT_CMD_DO_FT_TEST | 0x80;
	u8_write_data[2] = (unsigned char) u16_test_items;
	u8_write_data[3] = (unsigned char)(u16_test_items >> 8);

	DEBUGOUT("Star FW Test!!\r\n");

	if (handle_ic_write(FW_SYS_CMD_ADDR, 4, u8_write_data, g_u8_drv_interface, I2C_WORD_MODE) == ERROR)
		return ERROR;

	//Wait test fw finish INT //Wait 20 Sec
	i16_time_out = 1000;

	while (i16_time_out--) {
		hal_delay_ms(20);
		if (!hal_gpio_touch_int_pin_state_access() || (i16_time_out < 10)) {
			if (check_test_fw_status_rm6d031(FT_CMD_DO_FT_TEST, &u8_result) == ERROR) {
				DEBUGOUT("[FWT] Wait Test CMD NG (0x%x:%d)\r\n", FT_CMD_DO_FT_TEST, i16_time_out);
				return ERROR;
			} else if (u8_result == TRUE) {
				break;
			}
		}
	}

	if (handle_ic_read(FW_SYS_CMD_ADDR, 4, (unsigned char *)(&u32_read_data), g_u8_drv_interface, I2C_WORD_MODE) == ERROR)
		return ERROR;
	DEBUGOUT("[FWT]SysCMD1:0x%x (%d)\r\n", u32_read_data, i16_time_out);

	if (i16_time_out == IC_TEST_TIME_OUT) {
		DEBUGOUT("[FWT] Wait Test CMD Timeout!! (0x%x)\r\n", FT_CMD_DO_FT_TEST);
		return ERROR;
	}

#if ENABLE_TEST_TIME_MEASUREMENT
	DEBUGOUT("[FWT] FW Test Time(%d)\r\n", hal_get_system_time() - u64_fun_time);
#endif

	return SUCCESS;
}
unsigned char enter_fw_test_mode_rm6d031(void)
{
	unsigned char u8_result = 0;
	short i16_time_out = 0;

#if ENABLE_TEST_TIME_MEASUREMENT
	u64 u64_fun_time = 0;

	u64_fun_time = hal_get_system_time();
#endif

	g_u32_wearable_test_result &= ~WEARABLE_FT_TEST_RESULT_TEST_INIT_NG;

	DEBUGOUT("[EFTM] Start\r\n");

	if ((g_st_test_info.u16_ft_test_item & (IC_TEST_ITEMS_OPEN | IC_TEST_ITEMS_SHORT |
						IC_TEST_ITEMS_UC)) == 0) {
		return SUCCESS;
	}

	g_u8_is_normal_fw = FALSE;

	i16_time_out = 300;
	u8_result = 0;
	while (i16_time_out--) {
		if (!hal_gpio_touch_int_pin_state_access() || (i16_time_out < 20)) {
			if (check_test_fw_status_rm6d031(FT_CMD_INIT, &u8_result) == ERROR)
				goto IC_INIT_NG;
			else if (u8_result == TRUE)
				break;
		}
		hal_delay_ms(5);
	}

	if (i16_time_out == IC_TEST_TIME_OUT) {
		DEBUGOUT("[EFTM] FW State Check NG\r\n");
		goto IC_INIT_NG;
	}

	DEBUGOUT("[EFTM] Enter FT mode\r\n");

	if (set_test_info_thd_para_rm6d031() == ERROR) {
		DEBUGOUT("[RUPI] Set test info, thd, para NG!\r\n");
		return ERROR;
	}

#if ENABLE_TEST_TIME_MEASUREMENT
	DEBUGOUT("[EFTM] (%d)\r\n", hal_get_system_time() - u64_fun_time);
#endif
	return SUCCESS;

IC_INIT_NG:
	g_u32_wearable_test_result = WEARABLE_FT_TEST_RESULT_TEST_INIT_NG;
	return ERROR;

}

unsigned char ft_test_read_used_pin_infor_rm6d031(unsigned char *p_u8_infor)
{
//	unsigned char u8_i;
	unsigned char u8_r_buf[2] = {0};

	//get Pin remap
	if (handle_ic_read(FW_FT_PIN_ADDR, 48, p_u8_infor, g_u8_drv_interface, I2C_BYTE_MODE) == ERROR) {
		DEBUGOUT("[RUPI] NG!\r\n");
		return ERROR;
	}

	if (handle_ic_read(FW_FT_CHANNEL_X_ADDR, 2, u8_r_buf, g_u8_drv_interface, I2C_BYTE_MODE) == ERROR) {
		DEBUGOUT("[RUPI] NG!\r\n");
		return ERROR;
	}
	g_u8_channel_x = u8_r_buf[0];
	g_u8_channel_y = u8_r_buf[1];
	DEBUGOUT("[RUPI] X:%d,Y:%d\r\n", g_u8_channel_x, g_u8_channel_y);


	return SUCCESS;
}

unsigned char ft_test_ctrl_mbist_fun_rm6d031(unsigned char u8_enable)
{
	unsigned int u32_read = 0;

	if (u8_enable) {
		if (handle_ic_read(REG_SYSCON_BLKEN_ADDR, 4, (unsigned char *)(&u32_read), g_u8_drv_interface, I2C_WORD_MODE) == ERROR) {
			DEBUGOUT("Data RAM Test NG !!!\r\n");
			return ERROR;
		}

		u32_read |= (1 << 29);
		if (handle_ic_write(REG_SYSCON_BLKEN_ADDR, 4, (unsigned char *)&u32_read, g_u8_drv_interface, I2C_WORD_MODE) == ERROR) {
			DEBUGOUT("Data RAM Test NG !!!\r\n");
			return ERROR;
		}
	} else {
		if (handle_ic_read(REG_SYSCON_BLKEN_ADDR, 4, (unsigned char *)(&u32_read), g_u8_drv_interface, I2C_WORD_MODE) == ERROR) {
			DEBUGOUT("Data RAM Test NG !!!\r\n");
			return ERROR;
		}

		u32_read &= ~(1 << 29);
		if (handle_ic_write(REG_SYSCON_BLKEN_ADDR, 4, (unsigned char *)&u32_read, g_u8_drv_interface, I2C_WORD_MODE) == ERROR) {
			DEBUGOUT("Data RAM Test NG !!!\r\n");
			return ERROR;
		}
	}
	return SUCCESS;
}

unsigned char ft_test_ram_test_rm6d031(unsigned char u8_is_stop_mcu)
{
	unsigned int u32_read = 0;
	unsigned char u8_retry_times = 2;
	unsigned int u32_write = 0;

	if ((g_st_test_info.u16_ft_test_info_1) == WEARBLE_FT_PRAM_SYSTEM_NG_CASE) {
		g_u32_wearable_test_result = WEARABLE_FT_TEST_RESULT_PRAM_NG;
		return ERROR;
	}

RETRY_RAM_TEST:

	if (u8_is_stop_mcu)
		stop_mcu_rm6d031(TRUE);

	if (ft_test_ctrl_mbist_fun_rm6d031(ENABLE) == ERROR)
		goto EXIT_ERROR;

	u32_write = 0x04000080;
	if (handle_ic_write(RAM_WRITE_TEST_ADDR1, 4, (unsigned char *)&u32_write, g_u8_drv_interface, I2C_WORD_MODE) == ERROR)
		goto EXIT_ERROR;

	u32_write = 0x03FE9FFE;
	if (handle_ic_write(RAM_WRITE_TEST_ADDR2, 4, (unsigned char *)&u32_write, g_u8_drv_interface, I2C_WORD_MODE) == ERROR)
		goto EXIT_ERROR;

	u32_write = 0x0A800080;
	if (handle_ic_write(RAM_WRITE_TEST_ADDR3, 4, (unsigned char *)&u32_write, g_u8_drv_interface, I2C_WORD_MODE) == ERROR)
		goto EXIT_ERROR;

	hal_delay_ms(5);

	if (handle_ic_read(RAM_READ_TEST_ADDR1, 4, (unsigned char *)(&u32_read), g_u8_drv_interface, I2C_WORD_MODE) == ERROR)
		goto EXIT_ERROR;

	if (u32_read != 0x80000000) {
		DEBUGOUT("RAM Test NG 954[0x%x]!!!\r\n", u32_read);
		goto EXIT_ERROR;
	}

	if (handle_ic_read(RAM_READ_TEST_ADDR2, 4, (unsigned char *)(&u32_read), g_u8_drv_interface, I2C_WORD_MODE) == ERROR)
		goto EXIT_ERROR;

	if (u32_read != 0xF9007F00) {
		DEBUGOUT("RAM Test NG B04[0x%x]!!!\r\n", u32_read);
		goto EXIT_ERROR;
	}

	if (handle_ic_read(RAM_READ_TEST_ADDR3, 4, (unsigned char *)(&u32_read), g_u8_drv_interface, I2C_WORD_MODE) == ERROR)
		goto EXIT_ERROR;

	if (u32_read != 0xC0007F00) {
		DEBUGOUT("RAM Test NG B08[0x%x]!!!\r\n", u32_read);
		goto EXIT_ERROR;
	}

	if (ft_test_ctrl_mbist_fun_rm6d031(DISABLE) == ERROR)
		goto EXIT_ERROR;

	DEBUGOUT("RAM Test Pass\r\n");
	return SUCCESS;

EXIT_ERROR:
	if ((u8_retry_times-- > 0))
		goto RETRY_RAM_TEST;

	ft_test_ctrl_mbist_fun_rm6d031(DISABLE);
	g_u32_wearable_test_result = WEARABLE_FT_TEST_RESULT_PRAM_NG;
	DEBUGOUT("PRAM Test NG !!!\r\n");
	return ERROR;
}

unsigned char ft_test_connect_test_rm6d031(void)
{
	unsigned char u8_retry = IC_TEST_RETRY_TIME;
	unsigned int u32_w_buf = 0, u32_r_buf = 0;
	unsigned int u32_test_addr = RM_DATAMEM0_BASE;
	char i8_str[8];

	if ((g_st_test_info.u16_ft_test_info_1) == WEARBLE_FT_I2C_SYSTEM_NG_CASE) {
		g_u32_wearable_test_result = WEARABLE_FT_TEST_RESULT_I2C_NG;
		return ERROR;
	}

RETRY_CONNECT_TEST:

	if (g_u8_drv_interface == SPI_INTERFACE)
		sprintf(i8_str, "SPI");
	else
		sprintf(i8_str, "I2C");

	u32_w_buf = 0x55aa00ff;

	if (handle_ic_write(u32_test_addr, 4, (unsigned char *)&u32_w_buf, g_u8_drv_interface, I2C_WORD_MODE) == ERROR) {
		DEBUGOUT("%s Test Write NG!!\r\n", i8_str);
		goto NG_CASE;
	}

	if (handle_ic_read(u32_test_addr, 4, (unsigned char *)&u32_r_buf, g_u8_drv_interface, I2C_BYTE_MODE) == ERROR) {
		DEBUGOUT("%s Test Read NG!!\r\n", i8_str);
		goto NG_CASE;
	}


	if (u32_w_buf != u32_r_buf) {
		DEBUGOUT("%s Test Compare NG [%d], W=0x%x,R=0x%x\r\n", i8_str, 0, u32_w_buf, u32_r_buf);
		goto NG_CASE;
	}

	DEBUGOUT("%s Test Pass\r\n", i8_str);
	return SUCCESS;

NG_CASE:
	if (--u8_retry > 0) {
		DEBUGOUT("%s Test Retry=%d\r\n", i8_str, u8_retry);
		if (hardware_reset_rm6d031(TRUE) == ERROR)
			goto NG_CASE2;
		//Stop MCU
		stop_mcu_rm6d031(FALSE);
		goto RETRY_CONNECT_TEST;
	}

NG_CASE2:

	g_u32_wearable_test_result = WEARABLE_FT_TEST_RESULT_I2C_NG;
	DEBUGOUT("%s Test NG !!!\r\n", i8_str);
	return ERROR;
}

unsigned char ft_test_I2C_JIG_verify_rm6d031(void)
{
	unsigned int u32_w_buf[8], u32_r_buf[8];
	unsigned int u32_test_addr = RM_DATAMEM0_BASE;
	char i8_str[8];
	unsigned short u16_times, u16_i;
	unsigned char u8_i, u8_j, u8_retry = 2;

	g_u8_enable_i2c_retry = 0;
	memset(u32_w_buf, 0, sizeof(u32_w_buf));
	memset(u32_r_buf, 0, sizeof(u32_r_buf));
	u16_times = 5000;

	hal_control_i2c_voltage(0);	//set i2c to 1.44V
	for (u8_j = 0; u8_j < 2; u8_j++) {

		if (u8_j == 0) {
			hal_i2c_bus_de_init(I2C1);
			hal_i2c_bus_init(I2C1, SPEED_600KHZ);
			DEBUGOUT("I2C 600K\r\n");
			hal_delay_ms(5);
		} else {
			hal_i2c_bus_de_init(I2C1);
			hal_i2c_bus_init(I2C1, SPEED_500KHZ);
			DEBUGOUT("I2C 500K\r\n");
			hal_delay_ms(5);
		}
			u8_retry = 2;
RETRY_CONNECT_TEST:
		for (u8_i = 0; u8_i < 8 ; u8_i++)	//init test pattern
			u32_w_buf[u8_i] = 0x55aa00ff;
		for (u16_i = 0 ; u16_i < u16_times; u16_i++) {
#if ENABLE_WDT
			Chip_WWDT_Feed(LPC_WWDT);// reload the Watchdog timer
#endif
			if (handle_ic_write(u32_test_addr, 32, (unsigned char *)&u32_w_buf, g_u8_drv_interface, I2C_WORD_MODE) == ERROR) {
				DEBUGOUT("%s Test Write NG!!\r\n", i8_str);
				goto NG_CASE;
			}

			if (handle_ic_read(u32_test_addr, 32, (unsigned char *)&u32_r_buf, g_u8_drv_interface, I2C_BYTE_MODE) == ERROR) {
				DEBUGOUT("%s Test Read NG!!\r\n", i8_str);
				goto NG_CASE;
			}

			for (u8_i = 0; u8_i < 8 ; u8_i++) {
				if (u32_w_buf[u8_i] != u32_r_buf[u8_i]) {
					DEBUGOUT("%s Test Compare NG [%d ], W=0x%x,R=0x%x\r\n", i8_str, u8_i, u32_w_buf[u8_i], u32_r_buf[u8_i]);
					goto NG_CASE;
				}
			}
		}
	}

	hal_control_i2c_voltage(1);//set i2c to 1.8V
	hal_i2c_bus_de_init(I2C1);
	hal_i2c_bus_init(I2C1, SPEED_400KHZ);
	g_u8_enable_i2c_retry = 1;
	DEBUGOUT("%s JIG Verify Test Pass\r\n", i8_str);
	return SUCCESS;
NG_CASE:
	if (--u8_retry > 0) {
		DEBUGOUT("%s Test Retry=%d\r\n", i8_str, u8_retry);
		if (hardware_reset_rm6d031(TRUE) == ERROR)
			goto NG_CASE2;
		//Stop MCU
		stop_mcu_rm6d031(FALSE);
		goto RETRY_CONNECT_TEST;
	}

NG_CASE2:
	hal_control_i2c_voltage(1);//set i2c to 1.8V
	hal_i2c_bus_de_init(I2C1);
	hal_i2c_bus_init(I2C1, SPEED_400KHZ);
	g_u8_enable_i2c_retry = 1;
	g_u32_wearable_test_result = WEARABLE_FT_TEST_RESULT_VIH_NG;
	DEBUGOUT("%s JIG Verify Test NG !!!\r\n", i8_str);
	return ERROR;

}

unsigned char ft_test_I2C_500K_VIH_rm6d031(void)
{
	unsigned int u32_w_buf[8], u32_r_buf[8];
	unsigned int u32_test_addr = RM_DATAMEM0_BASE;
	char i8_str[8];
	unsigned short u16_times, u16_i;
	unsigned char u8_i, u8_j, u8_retry = IC_TEST_RETRY_TIME;

	g_u8_enable_i2c_retry = 0;

	if (g_u8_drv_interface == SPI_INTERFACE)
		sprintf(i8_str, "SPI");
	else
		sprintf(i8_str, "I2C");

	memset(u32_w_buf, 0, sizeof(u32_w_buf));
	memset(u32_r_buf, 0, sizeof(u32_r_buf));
	u16_times = 256;
	for (u8_j = 0; u8_j < 2; u8_j++) {

		if (u8_j == 0) {
			hal_control_i2c_voltage(1);//set i2c to 1.8V
			hal_i2c_bus_de_init(I2C1);
			hal_i2c_bus_init(I2C1, SPEED_500KHZ);
			DEBUGOUT("I2C 500K 1.8V\r\n");
			hal_delay_ms(5);
		} else {
			hal_control_i2c_voltage(0);//set i2c to 1.44V
			hal_i2c_bus_de_init(I2C1);
			hal_i2c_bus_init(I2C1, SPEED_400KHZ);
			DEBUGOUT("I2C 400K 1.44V\r\n");
			hal_delay_ms(5);
		}
		u8_retry = IC_TEST_RETRY_TIME;
RETRY_CONNECT_TEST:

		for (u8_i = 0; u8_i < 8 ; u8_i++)	//init test pattern
			u32_w_buf[u8_i] = 0x55aa00ff;
		for (u16_i = 0 ; u16_i < u16_times; u16_i++) {
#if ENABLE_WDT
			Chip_WWDT_Feed(LPC_WWDT);// reload the Watchdog timer
#endif
			if (handle_ic_write(u32_test_addr, 32, (unsigned char *)&u32_w_buf, g_u8_drv_interface, I2C_WORD_MODE) == ERROR) {
				DEBUGOUT("%s Test Write NG!!\r\n", i8_str);
				goto NG_CASE;
			}

			if (handle_ic_read(u32_test_addr, 32, (unsigned char *)&u32_r_buf, g_u8_drv_interface, I2C_BYTE_MODE) == ERROR) {
				DEBUGOUT("%s Test Read NG!!\r\n", i8_str);
				goto NG_CASE;
			}

			for (u8_i = 0; u8_i < 8 ; u8_i++) {
				if (u32_w_buf[u8_i] != u32_r_buf[u8_i]) {
					DEBUGOUT("%s Test Compare NG [%d ], W=0x%x,R=0x%x\r\n", i8_str, u8_i, u32_w_buf[u8_i], u32_r_buf[u8_i]);
					goto NG_CASE;
				}
			}
		}
	}

	hal_control_i2c_voltage(1);//set i2c to 1.8V
	hal_i2c_bus_de_init(I2C1);
	hal_i2c_bus_init(I2C1, SPEED_400KHZ);
	g_u8_enable_i2c_retry = 1;
	DEBUGOUT("%s VIH Test Pass\r\n", i8_str);
	return SUCCESS;

NG_CASE:
	if (--u8_retry > 0) {
		DEBUGOUT("%s Test Retry=%d\r\n", i8_str, u8_retry);
		if (hardware_reset_rm6d031(TRUE) == ERROR)
			goto NG_CASE2;
		//Stop MCU
		stop_mcu_rm6d031(FALSE);
		goto RETRY_CONNECT_TEST;
	}

NG_CASE2:

	hal_control_i2c_voltage(1);//set i2c to 1.8V
	hal_i2c_bus_de_init(I2C1);
	hal_i2c_bus_init(I2C1, SPEED_400KHZ);
	g_u8_enable_i2c_retry = 1;
	g_u32_wearable_test_result = WEARABLE_FT_TEST_RESULT_VIH_NG;
	DEBUGOUT("%s VIH Test NG !!!\r\n", i8_str);
	return ERROR;

}

unsigned char ft_test_500K_VIH_Experiment_rm6d031(void)
{
	unsigned int u32_w_buf[8], u32_r_buf[8];
	unsigned int u32_test_addr = RM_DATAMEM0_BASE;
	char i8_str[8];
	unsigned short u16_times, u16_i;
	unsigned char u8_i, u8_retry = IC_TEST_RETRY_TIME;


	g_u8_enable_i2c_retry = 0;

	if (g_u8_drv_interface == SPI_INTERFACE)
		sprintf(i8_str, "SPI");
	else
		sprintf(i8_str, "I2C");

	memset(u32_w_buf, 0, sizeof(u32_w_buf));
	memset(u32_r_buf, 0, sizeof(u32_r_buf));
	u16_times = 256;
	//Chip_GPIO_SetPinOutHigh(LPC_GPIO, DONGLE_GPIO_PORT_1, DONGLE_GPIO_PIN_10);
	hal_control_i2c_voltage(0);//set i2c to 1.44V
	hal_i2c_bus_de_init(I2C1);
	if (g_u32_i2c_speed) {
		hal_i2c_bus_init(I2C1, g_u32_i2c_speed);
		DEBUGOUT("I2C SPEED: %d\r\n", g_u32_i2c_speed);
	} else {
		hal_i2c_bus_init(I2C1, SPEED_500KHZ);
		DEBUGOUT("I2C SPEED: %d\r\n", SPEED_500KHZ);
	}


	DEBUGOUT("I2C 500K 1.44V\r\n");
	hal_delay_ms(5);
	u8_retry = IC_TEST_RETRY_TIME;
RETRY_CONNECT_TEST:

	for (u8_i = 0; u8_i < 8 ; u8_i++)	//init test pattern
		u32_w_buf[u8_i] = 0x55aa00ff;
	for (u16_i = 0 ; u16_i < u16_times; u16_i++) {
#if ENABLE_WDT
		Chip_WWDT_Feed(LPC_WWDT);// reload the Watchdog timer
#endif
		if (handle_ic_write(u32_test_addr, 32, (unsigned char *)&u32_w_buf, g_u8_drv_interface, I2C_WORD_MODE) == ERROR) {
			DEBUGOUT("%s Test Write NG!!\r\n", i8_str);
			goto NG_CASE;
		}

		if (handle_ic_read(u32_test_addr, 32, (unsigned char *)&u32_r_buf, g_u8_drv_interface, I2C_BYTE_MODE) == ERROR) {
			DEBUGOUT("%s Test Read NG!!\r\n", i8_str);
			goto NG_CASE;
		}

		for (u8_i = 0; u8_i < 8 ; u8_i++) {
			if (u32_w_buf[u8_i] != u32_r_buf[u8_i]) {
				DEBUGOUT("%s Test Compare NG [%d ], W=0x%x,R=0x%x\r\n", i8_str, u8_i, u32_w_buf[u8_i], u32_r_buf[u8_i]);
				goto NG_CASE;
			}
		}
	}

	hal_control_i2c_voltage(1);//set i2c to 1.8V
	//Chip_GPIO_SetPinOutLow(LPC_GPIO, DONGLE_GPIO_PORT_1, DONGLE_GPIO_PIN_10);
	hal_i2c_bus_de_init(I2C1);
	hal_i2c_bus_init(I2C1, SPEED_400KHZ);
	g_u8_enable_i2c_retry = 1;
	DEBUGOUT("%s VIH Test Pass\r\n", i8_str);
	g_u32_i2c_speed = 0;
	return SUCCESS;

NG_CASE:
	if (--u8_retry > 0) {
		DEBUGOUT("%s Test Retry=%d\r\n", i8_str, u8_retry);
		if (hardware_reset_rm6d031(TRUE) == ERROR)
			goto NG_CASE2;
		//Stop MCU
		stop_mcu_rm6d031(FALSE);
		goto RETRY_CONNECT_TEST;
	}

NG_CASE2:
	hal_control_i2c_voltage(1);//set i2c to 1.8V
	hal_i2c_bus_de_init(I2C1);
	hal_i2c_bus_init(I2C1, SPEED_400KHZ);
	g_u8_enable_i2c_retry = 1;
	g_u32_wearable_test_result = WEARABLE_FT_TEST_RESULT_VIH_NG;
	DEBUGOUT("%s VIH Test NG !!!\r\n", i8_str);
	g_u32_i2c_speed = 0;
	return ERROR;

}


unsigned char ft_test_reset_pin_test_rm6d031(void)
{
	unsigned int u32_write = 0;

	if ((g_st_test_info.u16_ft_test_info_1) == WEARBLE_FT_RESET_PIN_SYSTEM_NG_CASE) {
		g_u32_wearable_test_result = WEARABLE_FT_TEST_RESULT_RESET_NG;
		return ERROR;
	}

	u32_write = 0x00000404;
	if (handle_ic_write(REG_SYSCON_MISCIER_ADDR, 4, (unsigned char *)&u32_write, g_u8_drv_interface, I2C_WORD_MODE) == ERROR)
		return ERROR;

	if (!hal_gpio_touch_int_pin_state_access()) { // check INT in high state
		g_u32_wearable_test_result = WEARABLE_FT_TEST_RESULT_INT_NG;
		return ERROR;
	}
	hal_gpio_touch_reset_pin_control(FALSE);//Low
	hal_delay_ms(1);
	if (interface_init_rm6d031() == TRUE) {
		g_u32_wearable_test_result = WEARABLE_FT_TEST_RESULT_RESET_NG;
		DEBUGOUT("Reset Pin Test NG I2C(Not Low) !!!\r\n");
		return ERROR;
	}

	hal_gpio_touch_reset_pin_control(TRUE);//High
	hal_delay_ms(60);

	if (interface_init_rm6d031() == ERROR)
		return ERROR;

	DEBUGOUT("Reset Pin Test Pass !!!\r\n");
	return SUCCESS;
}

unsigned char ft_test_panel_model_check_rm6d031(unsigned short u16_version)
{
	unsigned int u32_read = 0;
	unsigned short u16_model_version = 0;

	if (handle_ic_read(FW_FT_SRAM_FW_VERSION, 4, (unsigned char *)(&u32_read), g_u8_drv_interface, I2C_WORD_MODE) == ERROR)
		return ERROR;
	u16_model_version = (u32_read & 0x0000FFFF);
	DEBUGOUT("Panel Model Test! 0x%08X:0x%08X\r\n", u32_read, u16_version);
	DEBUGOUT("Panel Model 0x%08X\r\n", u16_model_version);
	if (u16_model_version == u16_version)
		return SUCCESS;

	DEBUGOUT("Panel Model NG! 0x%x:0x%x\r\n", u16_model_version, u16_version);

	return ERROR;
}

void ft_raw_data_checksum_cal_rm6d031(unsigned short *u16_buffer)
{
	unsigned char u8_i;

	u16_buffer[48] = 0x55AA;
	u16_buffer[49] = 0;
	for (u8_i = 0; u8_i < (MAX_SENSING_PIN_NUM - 1); u8_i++)
		u16_buffer[49] += u16_buffer[u8_i];

	DEBUGOUT("[RDCSA] %x:%x\r\n", u16_buffer[48], u16_buffer[49]);
}

void ft_test_result_checksum_cal_rm6d031(unsigned char *u8_buffer)
{
	unsigned char u8_i;

	u8_buffer[48] = 0x5A;
	u8_buffer[49] = 0;
	for (u8_i = 0; u8_i < (MAX_SENSING_PIN_NUM - 1); u8_i++)
		u8_buffer[49] += u8_buffer[u8_i];

	DEBUGOUT("[TRCSA] %x:%x\r\n", u8_buffer[48], u8_buffer[49]);
}

unsigned char ft_raw_data_checksum_check_rm6d031(unsigned short *u16_buffer)
{
	unsigned char u8_i;
	unsigned short u16_sum = 0;

	if (u16_buffer[48] != 0x55AA) {
		DEBUGOUT("u16_buffer[34]:%x\r\n", u16_buffer[48]);
		DEBUGOUT("[RDCSC] Pattern NG! [0x%p](0x%x)\r\n", u16_buffer, u16_buffer[48]);
		return ERROR;
	}

	//u16_buffer[35]=0;
	for (u8_i = 0; u8_i < (MAX_SENSING_PIN_NUM - 1); u8_i++)
		u16_sum += u16_buffer[u8_i];

	if (u16_buffer[49] != u16_sum) {
		DEBUGOUT("[RDCSC] Check SUM NG! [0x%p](0x%x:0x%x)\r\n", u16_buffer, u16_sum, u16_buffer[49]);
		return ERROR;
	}

	DEBUGOUT("[RDCSC] PASS (0x%x:0x%x)\r\n", u16_buffer[48], u16_buffer[49]);

	return SUCCESS;
}

unsigned char ft_test_result_checksum_check_rm6d031(unsigned char *u8_buffer)
{
	unsigned char u8_i;
	unsigned char u8_sum = 0;

	if (u8_buffer[48] != 0x5A) {
		DEBUGOUT("[TRCSC] Pattern NG! [0x%p](0x%x:0x%x)\r\n", u8_buffer, u8_buffer[48], u8_buffer[49]);
		return ERROR;
	}

	//u32_buffer[35]=0;
	for (u8_i = 0; u8_i < (MAX_SENSING_PIN_NUM - 1); u8_i++)
		u8_sum += u8_buffer[u8_i];

	if (u8_buffer[49] != u8_sum) {
		DEBUGOUT("[TRCSC] Check SUM NG! [0x%p](0x%x:0x%x)\r\n", u8_buffer, u8_sum, u8_buffer[49]);
		return ERROR;
	}

	DEBUGOUT("[TRCSC] PASS (0x%x:0x%x)\r\n", u8_buffer[48], u8_buffer[49]);

	return SUCCESS;
}

unsigned char wait_fw_init_ready_rm6d031(void)
{
	unsigned short u16_time_out;
	unsigned char u8_r_buf[2];

	u16_time_out = 100;
	//Check FW Ready ?
	do {
		if (handle_ic_read(FW_FT_ARG0_ADDR, 2, u8_r_buf, g_u8_drv_interface, I2C_BYTE_MODE) == ERROR)
			return ERROR;

		DEBUGOUT("FW Flag = 0x%x,0x%x\r\n", u8_r_buf[0], u8_r_buf[1]);
		if (u8_r_buf[0] == 0xAA
			&& u8_r_buf[1] == 0x55) {
			return SUCCESS;
		}
		hal_delay_ms(2);

	} while (u16_time_out-- > 0);

	return ERROR;
}

unsigned char enter_normal_fw_rm6d031(void)
{
	unsigned char u8_retry = IC_TEST_RETRY_TIME;
//	unsigned char u8_pattern_noise_only = 0;
#if ENABLE_TEST_TIME_MEASUREMENT
	u64 u64_fun_time = 0;

	u64_fun_time = hal_get_system_time();
#endif

	if (g_u8_is_normal_fw)
		return SUCCESS;

	DEBUGOUT("[EFNM] Start\r\n");
	g_u32_wearable_test_result &= ~(WEARABLE_FT_TEST_RESULT_NORMAL_FW_NG);

RETRY:


	//g_u32_wearable_test_result &= ~WEARABLE_FT_TEST_RESULT_NORMAL_FW_RESET_NG;
	if (g_u8_drv_interface == I2C_INTERFACE) {
		if ((g_st_test_info.u16_ft_test_item & ~(IC_TEST_ITEMS_SYSTEM | IC_TEST_ITEMS_PANEL_TEST_2)) == FALSE) {
//			u8_pattern_noise_only = TRUE;
			DEBUGOUT("Panel Test!!!\r\n");
		}
	}

	if (hardware_reset_rm6d031(FALSE) == ERROR) {
		DEBUGOUT("[ENF]No INT, no OTP!!\r\n");
		goto ERROR_EXIT;
	}

	if (wait_fw_init_ready_rm6d031() == ERROR)
		goto ERROR_EXIT;

	if (ft_test_read_used_pin_infor_rm6d031(g_u8_wearable_pin_map) == ERROR)
		goto ERROR_EXIT;

#if ENABLE_TEST_TIME_MEASUREMENT
	DEBUGOUT("[ENF] (%d)\r\n", hal_get_system_time() - u64_fun_time);
#endif

	g_u8_is_normal_fw = TRUE;
	return SUCCESS;

ERROR_EXIT:
	//g_u32_wearable_test_result |= WEARABLE_FT_TEST_RESULT_NORMAL_FW_RESET_NG;
	if (u8_retry--)
		goto RETRY;

	return ERROR;
}

unsigned char hw_int_pin_Test_rm6d031(void)
{
	unsigned int u32_write = 0;

	if ((g_st_test_info.u16_ft_test_info_1) == WEARBLE_FT_INT_SYSTEM_NG_CASE) {
		g_u32_wearable_test_result = WEARABLE_FT_TEST_RESULT_INT_NG;
		return ERROR;
	}

	//Trigger INT to Low
	u32_write = 0x00000004;
	if (handle_ic_write(REG_SYSCON_MISCIER_ADDR, 4, (unsigned char *)&u32_write, g_u8_drv_interface, I2C_WORD_MODE) == ERROR) {
		g_u32_wearable_test_result = WEARABLE_FT_TEST_RESULT_I2C_NG;
		return ERROR;
	}

	//Read Pin state
	if (!hal_gpio_touch_int_pin_state_access()) {
		//DEBUGOUT("INT is LOW\r\n");
	} else {
		DEBUGOUT("INT Test NG!\r\n");
		g_u32_wearable_test_result = WEARABLE_FT_TEST_RESULT_INT_NG;
		return ERROR;
	}

	//Trigger INT to High
	u32_write = 0x00000404;
	if (handle_ic_write(REG_SYSCON_MISCIER_ADDR, 4, (unsigned char *)&u32_write, g_u8_drv_interface, I2C_WORD_MODE) == ERROR) {
		g_u32_wearable_test_result = WEARABLE_FT_TEST_RESULT_I2C_NG;
		return ERROR;
	}

	//Read Pin state
	if (hal_gpio_touch_int_pin_state_access()) {
		//DEBUGOUT("INT is High\r\n");
	} else {
		DEBUGOUT("INT Test NG!\r\n");
		g_u32_wearable_test_result = WEARABLE_FT_TEST_RESULT_INT_NG;
		return ERROR;
	}
	hal_gpio_touch_int_access(TRUE);
	DEBUGOUT("INT Test Pass\r\n");
	return SUCCESS;
}

unsigned char read_test_fw_data_rm6d031(unsigned short u16_test_items)
{
	unsigned char u8_retry = IC_TEST_RETRY_TIME;
	unsigned int u32_test_item_result;
#if ENABLE_TEST_TIME_MEASUREMENT
	u64 u64_fun_time = 0;

	u64_fun_time = hal_get_system_time();
#endif

	g_u32_wearable_test_result &= ~(WEARABLE_FT_TEST_RESULT_GET_DATA_NG | WEARABLE_FT_TEST_RESULT_OPEN_NG
					| WEARABLE_FT_TEST_RESULT_SHORT_NG
					| WEARABLE_FT_TEST_RESULT_UC_NG | WEARABLE_FT_TEST_RESULT_SINGLE_CC_OPEN_NG
					| WEARABLE_FT_TEST_RESULT_SUB_FREQ_NOISE_NG);
	if ((g_st_test_info.u16_ft_test_item & (IC_TEST_ITEMS_OPEN | IC_TEST_ITEMS_SHORT |
						IC_TEST_ITEMS_UC)) == 0) {
		return SUCCESS;
	}

RETRY:
	memset(g_i16_raw_data_1_short_buf, 0, sizeof(g_i16_raw_data_1_short_buf));
	memset(g_i16_raw_data_2_open_buf, 0, sizeof(g_i16_raw_data_2_open_buf));
	memset(g_u16_raw_data3_cc_buf, 0, sizeof(g_u16_raw_data3_cc_buf));
	memset(g_u16_uc_buf, 0, sizeof(g_u16_uc_buf));
#if !ENABLE_RAM_SHRINK
	memset(g_u16_cb, 0, sizeof(g_u16_cb));
#endif
	memset(g_u8_wearable_pin_map, RM6D030_NA_P, sizeof(g_u8_wearable_pin_map));

	if (handle_ic_read(FT_RAWDATA1_SHORT_BUF_ADDR, MAX_SENSING_PIN_NUM * 2, (unsigned char *)g_i16_raw_data_1_short_buf, g_u8_drv_interface, I2C_BYTE_MODE) == ERROR)
		goto ERROR_EXIT;


	if (handle_ic_read(FT_RAWDATA2_OPEN_BUF_ADDR, MAX_SENSING_PIN_NUM * 2, (unsigned char *)g_i16_raw_data_2_open_buf, g_u8_drv_interface, I2C_BYTE_MODE) == ERROR)
		goto ERROR_EXIT;

	if (handle_ic_read(FT_RAWDATA3_CC_BUF_ADDR, MAX_SENSING_PIN_NUM * 2, (unsigned char *)(g_u16_raw_data3_cc_buf), g_u8_drv_interface, I2C_BYTE_MODE) == ERROR)
		goto ERROR_EXIT;

	if (handle_ic_read(FT_UC_BUF_ADDR, MAX_SENSING_PIN_NUM * 2, (unsigned char *)g_u16_uc_buf, g_u8_drv_interface, I2C_BYTE_MODE) == ERROR)
		goto ERROR_EXIT;

	if (handle_ic_read(FT_TEST_RESULT_BUF_ADDR, MAX_SENSING_PIN_NUM, (unsigned char *)(g_u8_test_result), g_u8_drv_interface, I2C_BYTE_MODE) == ERROR)
		goto ERROR_EXIT;

	if (handle_ic_read(FT_TEST_ITEM_RESULT, 4, (unsigned char *)(&u32_test_item_result), g_u8_drv_interface, I2C_BYTE_MODE) == ERROR)
		goto ERROR_EXIT;

	if (ft_raw_data_checksum_check_rm6d031((unsigned short *)g_i16_raw_data_1_short_buf) == ERROR)
		goto ERROR_EXIT;

	if (ft_raw_data_checksum_check_rm6d031((unsigned short *)g_i16_raw_data_2_open_buf) == ERROR)
		goto ERROR_EXIT;

	if (ft_raw_data_checksum_check_rm6d031((unsigned short *)g_u16_raw_data3_cc_buf) == ERROR)
		goto ERROR_EXIT;

	if (ft_raw_data_checksum_check_rm6d031(g_u16_uc_buf) == ERROR)
		goto ERROR_EXIT;

	if (ft_test_result_checksum_check_rm6d031(g_u8_test_result) == ERROR)
		goto ERROR_EXIT;

	if (ft_test_read_used_pin_infor_rm6d031(g_u8_wearable_pin_map) == ERROR)
		goto ERROR_EXIT;

#if ENABLE_TEST_RSU_DATA_SHOW
	DEBUGOUT("Slow Data:\r\n");
	dump_image_data_rm6d031(g_i16_raw_data_1_short_buf, TRUE);
	DEBUGOUT("Quick Data:\r\n");
	dump_image_data_rm6d031(g_i16_raw_data_2_open_buf, TRUE);
	DEBUGOUT("P CC:\r\n");
	dump_image_data_rm6d031((short *)g_u16_raw_data3_cc_buf, TRUE);
	DEBUGOUT("Open Golden CC:\r\n");
	dump_image_data_rm6d031((short *)g_u16_raw_data3_golden_cc_buf, TRUE);
	DEBUGOUT("UC:\r\n");
	dump_image_data_rm6d031((short *)g_u16_uc_buf, TRUE);
	DEBUGOUT("Golden UC:\r\n");
	dump_image_data_rm6d031((short *)g_u16_uc_golden_cc_buf, TRUE);
	DEBUGOUT("test item result:0x%x\r\n", u32_test_item_result);
#endif

	if ((g_st_test_info.u16_ft_test_info_3)  & PRINT_DEBUG_MSG_OPEN_SHORT) {
		DEBUGOUT("Slow Data:\r\n");
		dump_image_data_rm6d031(g_i16_raw_data_1_short_buf, TRUE);
		DEBUGOUT("Quick Data:\r\n");
		dump_image_data_rm6d031(g_i16_raw_data_2_open_buf, TRUE);
		DEBUGOUT("P CC:\r\n");
		dump_image_data_rm6d031((short *)g_u16_raw_data3_cc_buf, TRUE);

	}
	if ((g_st_test_info.u16_ft_test_info_3)  & PRINT_DEBUG_MSG_UC) {
		DEBUGOUT("UC Data:\r\n");
		dump_image_data_rm6d031((short *)g_u16_uc_buf, TRUE);
	}

#if ENABLE_TEST_TIME_MEASUREMENT
	DEBUGOUT("#read_test_data end=%d\r\n", hal_get_system_time() - u64_fun_time);
#endif
	g_u32_wearable_test_result |= u32_test_item_result;
	DEBUGOUT("Read Test FW Result Finish\r\n");
	return SUCCESS;

ERROR_EXIT:
	if (u8_retry) {
		u8_retry--;
		DEBUGOUT("Read Test FW Result Retry:%d", u8_retry);
		goto RETRY;
	}

	g_u32_wearable_test_result |= WEARABLE_FT_TEST_RESULT_GET_DATA_NG;
	DEBUGOUT("Read Test FW Result NG\r\n");
	return ERROR;
}

void test_item_message_rm6d031(void)
{
	unsigned short u16_test_item = g_st_test_info.u16_ft_test_item;

	DEBUGOUT("================================\r\n");
	DEBUGOUT("Enter rm6d030 IC Test,(%llu)\r\n", hal_get_system_time());
	DEBUGOUT("IC Test Items=0x%X\r\n", u16_test_item);

	if (u16_test_item & IC_TEST_ITEMS_SYSTEM)
		DEBUGOUT("Enable System Test\r\n");

	if (u16_test_item & IC_TEST_ITEMS_OPEN)
		DEBUGOUT("Enable Open Test\r\n");

	if (u16_test_item & IC_TEST_ITEMS_SHORT)
		DEBUGOUT("Enable Short Test\r\n");

	if (u16_test_item & IC_TEST_ITEMS_UC)
		DEBUGOUT("Enable Uniformity CC test\r\n");

	if (u16_test_item & IC_TEST_ITEMS_BURN_FW)
		DEBUGOUT("Enable Burn FW\r\n");

	if (u16_test_item & IC_TEST_ITEMS_BURN_CC)
		DEBUGOUT("Enable Burn CC\r\n");
}

unsigned char fw_update_check_rm6d031(unsigned char *p_u8_update)
{
	unsigned int u32_ic_fw_ver;
	//if (g_u32_wearable_test_result == WEARABLE_FT_TEST_RESULT_INIT_STATE)
	//	g_u32_wearable_test_result = WEARABLE_FT_TEST_RESULT_PASS;
	*p_u8_update = FALSE;
	//Auto Burn Enable
	if (g_st_test_info.u16_ft_eng_item & IC_TEST_ENG_ITEMS_AUTO_BURN_FW) {
		//Read IC FW version from PRAM
		g_u32_wearable_test_result &= ~(WEARABLE_FT_TEST_RESULT_IC_FW_VERIFY_NG);
		if (handle_ic_read(FW_FT_SRAM_FW_VERSION, 4, (unsigned char *)(&u32_ic_fw_ver), g_u8_drv_interface, I2C_BYTE_MODE) == ERROR) {
			DEBUGOUT("Read Normal FW Version Fail !!!\r\n");
			g_u32_wearable_test_result |= WEARABLE_FT_TEST_RESULT_IC_FW_VERIFY_NG;
			return ERROR;
		}
		if (u32_ic_fw_ver == g_st_test_para.u32_normal_fw_version) {
			*p_u8_update = FALSE;
			DEBUGOUT("[AUTO Burn] FW is same as chip\r\n");
		} else {
			*p_u8_update = TRUE;
			DEBUGOUT("[AUTO Burn] FW need to update: 0x%x->0x%x\r\n", u32_ic_fw_ver, g_st_test_para.u32_normal_fw_version);
		}
	} else {
		if ((g_st_test_info.u16_ft_test_item & IC_TEST_ITEMS_BURN_FW))
			*p_u8_update = TRUE;
	}
	return SUCCESS;
}

unsigned char burn_fw_rm6d031(void)
{
	unsigned char u8_status = SUCCESS;
	unsigned char u8_fw_update;
#if ENABLE_TEST_TIME_MEASUREMENT
	u64 u64_fun_time = 0;

	u64_fun_time = hal_get_system_time();
#endif

	g_u32_wearable_test_result &= ~(WEARABLE_FT_TEST_RESULT_CMD_NG | WEARABLE_FT_TEST_RESULT_BURN_FW_NG);
	if (fw_update_check_rm6d031(&u8_fw_update) == ERROR) {
		g_u32_wearable_test_result = WEARABLE_FT_TEST_RESULT_CMD_NG;
		u8_status = ERROR;
		goto EXIT;
	}
	if (u8_fw_update == FALSE)
		return SUCCESS;

	//Update FW
	DEBUGOUT("[BF] Burn FW Start!!\r\n");
	if ((g_u8_drv_interface == SPI_INTERFACE) || (g_st_test_info.u16_ft_eng_item & IC_TEST_ENG_ITEMS_I2C_FLASHLESS)) {
		//flashless
		DEBUGOUT("[BF]Flashless Burn FW Bypass!!\r\n");
		u8_status = SUCCESS;
	} else {
		//burn ic flash from dongle flash
		u8_status = burn_ext_flash_from_image_rm6d031();
		if (u8_status == ERROR) {
			DEBUGOUT("[BF] Burn FW NG!!\r\n");
			goto EXIT;
		}
		hardware_reset_rm6d031(FALSE);
	}

EXIT:
#if ENABLE_TEST_TIME_MEASUREMENT
	DEBUGOUT("Burn FW Finished (Time:%d)\r\n", hal_get_system_time() - u64_fun_time);
#endif
	return u8_status;
}

unsigned char burn_cc_rm6d031(unsigned short u16_test_items)
{
#if ENABLE_TEST_TIME_MEASUREMENT
	u64 u64_fun_time = 0;

	u64_fun_time = hal_get_system_time();
#endif
	g_u32_wearable_test_result &= ~(WEARABLE_FT_TEST_RESULT_BURN_CC_NG | WEARABLE_FT_TEST_RESULT_CC_CALIBRATION_NG);

	if ((g_st_test_info.u16_ft_test_info_1) == WEARBLE_FT_BURN_CC_SYSTEM_NG_CASE) {
		g_u32_wearable_test_result = WEARABLE_FT_TEST_RESULT_BURN_CC_NG;
		return ERROR;
	}

	if ((g_st_test_info.u16_ft_test_item & (IC_TEST_ITEMS_BURN_CC)) == 0) {
		//g_u32_wearable_test_result &= ~WEARABLE_FT_TEST_RESULT_NORMAL_FW_RESET_NG;
		return SUCCESS;
	}

	if ((g_u8_drv_interface == SPI_INTERFACE) || (g_st_test_info.u16_ft_eng_item & IC_TEST_ENG_ITEMS_I2C_FLASHLESS)) {
		//flashless, bypass
		DEBUGOUT("[BC]Flashless Burn CC ByPass!!\r\n");
		return SUCCESS;
	}

	if (enter_normal_fw_rm6d031() == ERROR) {
		g_u32_wearable_test_result |= WEARABLE_FT_TEST_RESULT_NORMAL_FW_NG;
		DEBUGOUT("Enter Normal FW NG\r\n");
		return ERROR;
	}

	//Do CC
	DEBUGOUT("[BC] Burn CC Start!!\r\n");

	//Burn CC to Flash
	if (do_calibration_rm6d031(TRUE, TRUE) == SUCCESS) {
		DEBUGOUT("[BC] Burn CC Pass!!\r\n");
	} else {
		DEBUGOUT("[BC] Burn CC NG!!\r\n");
		return ERROR;
	}

#if ENABLE_TEST_TIME_MEASUREMENT
	DEBUGOUT("[BC] Burn CC Time(%d)\r\n", hal_get_system_time() - u64_fun_time);
#endif

	return SUCCESS;
}

unsigned char load_test_fw_ft_rm6d031(void)
{
	unsigned int u32_read_data;
	unsigned char u8_retry = IC_TEST_RETRY_TIME;
	unsigned int u32_write = 0;
	uint8_t u8_ret = ERROR;

	g_u8_enable_i2c_retry = 0;
	g_u32_pram_boot_addr = PRAM_BOOT_START;
	g_u32_pram_tb_exe_addr = PRAM_TB_EXE_START;
	g_u32_pram_init_addr = DG_EXT_FLASH_INIT_START;
	g_u32_pram_fw_addr = PRAM_FW_START;
	g_u32_pram_para_addr = PRAM_PARA_START;
	g_u32_test_fw_addr = PRAM_SIZE;

RETRY:

	// set skip load & MCU hold then SW reset
	// sync_data:210;cmd_type:210h;ret_data:214h;test_mode:218h
	if (stop_mcu_rm6d031(1) == ERROR)
		goto ERROR_EXIT;

	if ((g_st_test_info.u16_ft_eng_item & IC_TEST_ENG_ITEMS_SET_DSP_INIT_AF_TP_RESET)) {
		DEBUGOUT("Set Display Initial & Normal On\r\n");
		if (!handle_ic_set_initial_code())
			DEBUGOUT("Set Display Initial Fail\r\n");
	}

	if (g_u8_drv_interface == SPI_INTERFACE) {
		u32_write = 0;
		if (handle_ic_write(REG_FLASHCTL_FLASH_PRAM_LOCK, 4, (unsigned char *)&u32_write, g_u8_drv_interface, I2C_WORD_MODE) == ERROR)
			return ERROR;
	} else {
		//WRT boot-loader to PRAM first
		u32_write = INICO_LOCK;
		if (handle_ic_write(REG_FLASHCTL_FLASH_PRAM_LOCK, 4, (unsigned char *)&u32_write, g_u8_drv_interface, I2C_WORD_MODE) == ERROR)
			return ERROR;
	}
	//read image data and write to pram
	//read image bootloader data and write to pram

#if ENABLE_FW_UPDATE_VERIFY_LOG
	DEBUGOUT("[LTFW] Load Test FW to PRAM\r\n");
#endif
	DEBUGOUT("[LTFW]g_u8_drv_interface 1 = 0x%x\r\n", g_u8_drv_interface);
	//Unlock SPI wrt to PRAM
	if (unlock_spi_wrt_to_pram_rm6d031(g_u8_drv_interface) == ERROR)
		return ERROR;

	//read image data and write to pram
	u8_ret = hal_read_image_fw_to_pram(TEST_BL_IMG, PRAM_BOOT_LENGTH, PRAM_BOOT_START);
	if (u8_ret == ERROR) {
		DEBUGOUT("[LTFW] Confirm flash BLDR crc error\r\n");
		goto ERROR_EXIT;
	}

	//check pram checksum
	if (check_pram_crc32_rm6d031(PRAM_BOOT_START, PRAM_BOOT_CRC_LENGTH) == ERROR) {
		DEBUGOUT("[LTFW]Check PRAM BLDR crc fail\r\n");
		goto ERROR_EXIT;
	}

	if (g_u8_drv_interface == SPI_INTERFACE) {
		//read image inittial cdoe data and write to pram
		u8_ret = hal_read_image_fw_to_pram(TEST_TB_EXE_IMG, PRAM_TB_EXE_LENGTH, PRAM_TB_EXE_START);
		if (u8_ret == ERROR) {
			DEBUGOUT("[LTFW] Confirm flash Initial Code crc error\r\n");
			goto ERROR_EXIT;
		}

		//check initial code checksum
		if (check_pram_crc32_rm6d031(PRAM_TB_EXE_START, PRAM_TB_EXE_CRC_LENGTH) == ERROR) {
			DEBUGOUT("[LTFW]Check PRAM Initial Code crc fail\r\n");
			goto ERROR_EXIT;
		}
	}
	u8_ret = hal_read_image_fw_to_pram(TEST_TEST_FW_IMG, PRAM_FW_LENGTH + PRAM_PARA_LENGTH, PRAM_FW_START);
	if (u8_ret == ERROR) {
		DEBUGOUT("[LTFW] Confirm flash FW crc error\r\n");
		goto ERROR_EXIT;
	}

	//check pram  checksum
	if (check_pram_crc32_rm6d031(PRAM_FW_START, PRAM_FW_CRC_LENGTH) == ERROR) {
		DEBUGOUT("[LTFW]Check PRAM FW crc fail\r\n");
		goto ERROR_EXIT;
	}

	DEBUGOUT("[LTFW]g_u8_drv_interface 2 = 0x%x\r\n", g_u8_drv_interface);
	//clear CC & BL info
	u32_write = 0x00000000;
	if (handle_ic_write(PRAM_ADDR_CC_INFO, 4, (unsigned char *)&u32_write, g_u8_drv_interface, I2C_WORD_MODE) == ERROR)
		return ERROR;

	// set skip load & MCU hold then SW reset
	// sync_data:210;cmd_type:210h;ret_data:214h;test_mode:218h
	//read bootloader version
	if (handle_ic_read(0x80, 4, (unsigned char *)(&u32_read_data), g_u8_drv_interface, I2C_WORD_MODE) == ERROR)
		goto ERROR_EXIT;
	DEBUGOUT("[LTFW]Read bootloader ver=0x%x!!\r\n", u32_read_data);

	u32_write = SKIP_LOAD | TEST_FW_MODE;

	DEBUGOUT("[LTFW]write skip load 0x%x!!\r\n", u32_write);
	if (handle_ic_write(REG_FLASHCTL_FLASH_STATE_REG_ADDR, 4, (unsigned char *)&u32_write, g_u8_drv_interface, I2C_WORD_MODE) == ERROR)
		return ERROR;

	if (handle_ic_read(REG_FLASHCTL_FLASH_STATE_REG_ADDR, 4, (unsigned char *)(&u32_read_data), g_u8_drv_interface, I2C_WORD_MODE) == ERROR)
		goto ERROR_EXIT;
	DEBUGOUT("Read MCU STATUS=0x%x!!\r\n", u32_read_data);

	//wait sw rst finish
	u32_write = BLKRST_SW_RST;
	if (handle_ic_write(REG_SYSCON_BLKRST_ADDR, 4, (unsigned char *)&u32_write, g_u8_drv_interface, I2C_WORD_MODE) == ERROR)
		return ERROR;
	hal_delay_ms(35);

	if (handle_ic_read(REG_FLASHCTL_FLASH_STATE_REG_ADDR, 4, (unsigned char *)(&u32_read_data), g_u8_drv_interface, I2C_WORD_MODE) == ERROR)
		goto ERROR_EXIT;
	DEBUGOUT("Read MCU STATUS=0x%x!!\r\n", u32_read_data);

	if (handle_ic_read(FW_FT_SRAM_FW_VERSION, 4, (unsigned char *)(&u32_read_data), g_u8_drv_interface, I2C_WORD_MODE) == ERROR)
		goto ERROR_EXIT;

	if (u32_read_data != g_st_test_para.u32_test_fw_version) {
		DEBUGOUT("Read FW Version NG=0x%X:0x%X!!\r\n", u32_read_data, g_st_test_para.u32_test_fw_version);
		//DEBUGOUT("Read Dev ID:%x\r\n", u32_read_data);
		goto ERROR_EXIT;
	} else
		DEBUGOUT("Read FW Version=0x%X!!\r\n", u32_read_data);


	g_u8_enable_i2c_retry = 1;
	return SUCCESS;

ERROR_EXIT:
	if (u8_retry) {
		u8_retry--;
		goto RETRY;
	}
	g_u8_enable_i2c_retry = 1;
	return ERROR;
}

unsigned char fw_version_check_rm6d031(void)
{
	unsigned int u32_read = 0;

	g_u32_wearable_test_result &= ~WEARABLE_FT_TEST_RESULT_IC_FW_VERIFY_NG;

	if (g_u8_drv_interface == SPI_INTERFACE) {
		DEBUGOUT("[BF]Flashless Check FW Bypass!!\r\n");
		return SUCCESS;
	}

	if ((g_st_test_info.u16_ft_test_item & IC_TEST_ITEMS_FW_VERSION_CHECK) == FALSE)
		return SUCCESS;

	if (handle_ic_read(FW_FT_SRAM_FW_VERSION, 4, (unsigned char *)(&u32_read), g_u8_drv_interface, I2C_WORD_MODE) == ERROR)
		return ERROR;

	if (u32_read != g_st_test_para.u32_normal_fw_version) {
		DEBUGOUT("FW Version NG! 0x%X:0x%X\r\n", g_st_test_para.u32_normal_fw_version, u32_read);
		return ERROR;
	}

	DEBUGOUT("FW Version Pass 0x%X:0x%X\r\n", g_st_test_para.u32_normal_fw_version, u32_read);

	return SUCCESS;
}

/*Use dongle ext.Flash to read test fw and write to pram*/
unsigned char load_test_fw_rm6d031(void)
{
	unsigned char u8_ret = ERROR;
#if ENABLE_TEST_TIME_MEASUREMENT
	u64 u64_fun_time = 0;

	u64_fun_time = hal_get_system_time();
	DEBUGOUT("load_test_fw Start Time= %d\r\n", u64_fun_time);
#endif
	g_u32_wearable_test_result &= ~(WEARABLE_FT_TEST_RESULT_LOAD_TESTFW_NG);

	if (g_st_test_info.u16_ft_test_info_1 == WEARBLE_FT_LOAD_TEST_FW_SYSTEM_NG_CASE) {
		g_u32_wearable_test_result = WEARABLE_FT_TEST_RESULT_LOAD_TESTFW_NG;
		return ERROR;
	}

	if ((g_st_test_info.u16_ft_test_item & (IC_TEST_ITEMS_OPEN | IC_TEST_ITEMS_SHORT | IC_TEST_ITEMS_UC)) == 0)
		return SUCCESS;

	DEBUGOUT("[LTFW] start load test fw\r\n");

	u8_ret = load_test_fw_ft_rm6d031();

	DEBUGOUT("[LTFW] Load test fw finish!!\r\n");
#if ENABLE_TEST_TIME_MEASUREMENT
	DEBUGOUT("load_test_fw Spend Time= %d\r\n", hal_get_system_time() - u64_fun_time);
#endif

	return u8_ret;
}

unsigned char system_test_rm6d031(void)
{
	unsigned char u8_test_result = SUCCESS;
	unsigned short u16_panel_mode;
	unsigned int u32_panel_id[3];

#if ENABLE_TEST_TIME_MEASUREMENT
	u64 u64_fun_time = hal_get_system_time();
#endif
	//Clear Test Item Test result
	g_u32_wearable_test_result &= ~(WEARABLE_FT_TEST_RESULT_VIH_NG | WEARABLE_FT_TEST_RESULT_I2C_NG | WEARABLE_FT_TEST_RESULT_INT_NG
					| WEARABLE_FT_TEST_RESULT_RESET_NG | WEARABLE_FT_TEST_RESULT_PRAM_NG | WEARABLE_FT_TEST_RESULT_TOUCH_ID_NG
					| WEARABLE_FT_TEST_RESULT_IC_SUB_VERSION_NG | WEARABLE_FT_TEST_RESULT_IC_FW_VERIFY_NG | WEARABLE_FT_TEST_RESULT_I2C_NID_NG);

	if ((g_st_test_info.u16_ft_test_item & IC_TEST_ITEMS_SYSTEM) == FALSE)
		return SUCCESS;

	if (g_st_test_info.u16_ft_eng_item & IC_TEST_ENG_ITEMS_CHECK_I2C_NID) {
		if (check_i2c_nid_rm6d031(g_st_test_para.u8_check_i2c_nid) == ERROR) {
			u8_test_result = ERROR;
			g_u32_wearable_test_result = WEARABLE_FT_TEST_RESULT_I2C_NID_NG;
			goto ERROR_EXIT;
		}
	}

	stop_mcu_rm6d031(1);

	//Check Dongle Ext Flash FW/Test FW version PK INI
//	if (check_ext_flash_fw_version() == ERROR) {
//		u8_test_result = ERROR;
//		g_u32_wearable_test_result = WEARABLE_FT_TEST_RESULT_IC_FW_VERIFY_NG;
//		goto ERROR_EXIT;
//	}

#if ENABLE_TEST_TIME_MEASUREMENT
	u64_fun_time = hal_get_system_time();
#endif
	//I2C or SPI Test
	if (ft_test_connect_test_rm6d031() == ERROR) {
		u8_test_result = ERROR;
		goto ERROR_EXIT;
	}
#if ENABLE_TEST_TIME_MEASUREMENT
	DEBUGOUT("Interface test Spend Time= %d\r\n", hal_get_system_time() - u64_fun_time);
#endif
#if ENABLE_TEST_TIME_MEASUREMENT
		u64_fun_time = hal_get_system_time();
#endif

	if ((g_st_test_info.u16_ft_eng_item & IC_TEST_ENG_ITEMS_JIG_VERIFY)) {
		DEBUGOUT("Start JIG_VERIFY\r\n");
		if (ft_test_I2C_JIG_verify_rm6d031() == ERROR) {
			u8_test_result = ERROR;
			goto ERROR_EXIT;
		}
	}
#if ENABLE_TEST_TIME_MEASUREMENT
		DEBUGOUT("VIH 600K test Spend Time= %d\r\n", hal_get_system_time() - u64_fun_time);
#endif
#if ENABLE_TEST_TIME_MEASUREMENT
			u64_fun_time = hal_get_system_time();
#endif
	if ((g_st_test_info.u16_ft_eng_item & IC_TEST_ENG_ITEMS_500K_VIH)) {
		DEBUGOUT("Start 500K_VIH_Experiment\r\n");
		if (ft_test_500K_VIH_Experiment_rm6d031() == ERROR) {
			u8_test_result = ERROR;
			goto ERROR_EXIT;
		}
	}

#if ENABLE_TEST_TIME_MEASUREMENT
		DEBUGOUT("VIH 500K test Spend Time= %d\r\n", hal_get_system_time() - u64_fun_time);
#endif

	if ((g_st_test_info.u16_ft_eng_item & IC_TEST_ENG_ITEMS_PANEL_MODEL_CHECK)) {
		DEBUGOUT("INI Model = 0x%08X\r\n", g_st_test_para.u32_normal_fw_version);
		u16_panel_mode = (g_st_test_para.u32_normal_fw_version & 0x0000FFFF);
		if (ft_test_panel_model_check_rm6d031(u16_panel_mode) == ERROR) {
			u8_test_result = ERROR;
			g_u32_wearable_test_result = WEARABLE_FT_TEST_RESULT_IC_FW_VERIFY_NG;
			goto ERROR_EXIT;
		}
	}

#if ENABLE_TEST_TIME_MEASUREMENT
	u64_fun_time = hal_get_system_time();
#endif
	//INT Pin Test
	if (hw_int_pin_Test_rm6d031() == ERROR) {
		u8_test_result = ERROR;
		goto ERROR_EXIT;
	}
#if ENABLE_TEST_TIME_MEASUREMENT
	DEBUGOUT("INT Spend Time= %d\r\n", hal_get_system_time() - u64_fun_time);
#endif

#if ENABLE_TEST_TIME_MEASUREMENT
	u64_fun_time = hal_get_system_time();
#endif
	//RAM Test
	if (ft_test_ram_test_rm6d031(0) == ERROR) {
		u8_test_result = ERROR;
		goto ERROR_EXIT;
	}
#if ENABLE_TEST_TIME_MEASUREMENT
	DEBUGOUT("RAM Spend Time= %d\r\n", hal_get_system_time() - u64_fun_time);
#endif

	if ((g_st_test_info.u16_ft_eng_item & IC_TEST_ENG_ITEMS_IC_SUB_VERSION_CHECK)) {
		if (check_dev_sub_version_rm6d031(g_st_test_para.u8_ic_sub_version) == ERROR) {
			u8_test_result = ERROR;
			g_u32_wearable_test_result = WEARABLE_FT_TEST_RESULT_IC_SUB_VERSION_NG;
			goto ERROR_EXIT;
		}
	}

#if ENABLE_TEST_TIME_MEASUREMENT
	u64_fun_time = hal_get_system_time();
#endif
//Reset Pin Test
	if (ft_test_reset_pin_test_rm6d031() == ERROR) {
		u8_test_result = ERROR;
		goto ERROR_EXIT;
	}
#if ENABLE_TEST_TIME_MEASUREMENT
	DEBUGOUT("System Spend Time= %d\r\n", hal_get_system_time() - u64_fun_time);
#endif

	//check touch id
	if ((g_st_test_info.u16_ft_test_item & IC_TEST_ITEMS_TOUCH_ID)) {
		u32_panel_id[0] = (g_st_test_para.u8_touch_id_version_3 << 24 | g_st_test_para.u8_touch_id_version_2 << 16 | g_st_test_para.u8_touch_id_version_1 << 8 | g_st_test_para.u8_touch_id_version_0);
		u32_panel_id[1] = (g_st_test_para.u8_touch_id_version_7 << 24 | g_st_test_para.u8_touch_id_version_6 << 16 | g_st_test_para.u8_touch_id_version_5 << 8 | g_st_test_para.u8_touch_id_version_4);
		u32_panel_id[2] = (g_st_test_para.u8_touch_id_version_9 << 8 | g_st_test_para.u8_touch_id_version_8);

		DEBUGOUT("Panel ID_30=0x%x\r\n", u32_panel_id[0]);
		DEBUGOUT("Panel ID_74=0x%x\r\n", u32_panel_id[1]);
		DEBUGOUT("Panel ID_98=0x%x\r\n", u32_panel_id[2]);


		if (check_dev_touch_id_rm6d031(u32_panel_id) == ERROR) {
			u8_test_result = ERROR;
			g_u32_wearable_test_result = WEARABLE_FT_TEST_RESULT_TOUCH_ID_NG;
			goto ERROR_EXIT;
		}
	}

		if (check_display_mode_rm6d031() == ERROR) {
			u8_test_result = ERROR;
			g_u32_wearable_test_result = WEARABLE_FT_TEST_RESULT_DISPLAY_MODE_NG;
			goto ERROR_EXIT;
		}



ERROR_EXIT:

	if (u8_test_result == ERROR)
		DEBUGOUT("System Test NG!!\r\n");
	else
		DEBUGOUT("System Test PASS\r\n");



	return u8_test_result;
}

void do_ic_test_rm6d031(void)
{
	unsigned short u16_test_items;

#if ENABLE_TEST_TIME_MEASUREMENT
	unsigned char u8_ic_test_state;
#endif

#if ENABLE_TEST_TIME_MEASUREMENT
	unsigned int u32_fun_time = 0;
#endif
	unsigned int u32_read_data = 0;

	g_u32_wearable_test_result &= ~(WEARABLE_FT_TEST_RESULT_IC_VERSION_NG
					| WEARABLE_FT_TEST_RESULT_MCU_HOLD_NG | WEARABLE_FT_TEST_RESULT_SENSOR_IMPACT_NG
					| WEARABLE_FT_TEST_RESULT_FLASH_ID_NG | WEARABLE_FT_TEST_RESULT_DISPLAY_MODE_NG
					| WEARABLE_FT_TEST_RESULT_PANEL_TEST_3_NG | WEARABLE_FT_TEST_RESULT_DRAWING_NG | WEARABLE_FT_TEST_RESULT_TEST_FW_VER_NG
					| WEARABLE_FT_TEST_RESULT_JIG_NG);

	if (!wearable_ic_test_init()) {
		g_u32_wearable_test_result |= WEARABLE_FT_TEST_RESULT_TEST_INIT_NG;
		g_u8_ic_test_state = IC_TEST_EXIT;
		return;
	}

	test_item_message_rm6d031();

	if (g_st_test_info.u16_ft_test_item == 0)
		return;

	hardware_reset_rm6d031(TRUE);

	handle_ic_read(REG_DRIVERMODE_ADDR, 4, (unsigned char *)(&u32_read_data), g_u8_drv_interface, I2C_WORD_MODE);
	DEBUGOUT("display mode 0x%x\r\n", u32_read_data);
	DEBUGOUT("g_u8_display_interface %d\r\n", g_u8_display_interface);

	g_u8_ic_test_state = IC_TEST_INIT;

	u16_test_items = g_st_test_info.u16_ft_test_item;

	g_u32_wearable_test_result &= ~(WEARABLE_FT_TEST_RESULT_IC_VERSION_NG
					| WEARABLE_FT_TEST_RESULT_MCU_HOLD_NG | WEARABLE_FT_TEST_RESULT_SENSOR_IMPACT_NG
					| WEARABLE_FT_TEST_RESULT_FLASH_ID_NG | WEARABLE_FT_TEST_RESULT_DISPLAY_MODE_NG
					| WEARABLE_FT_TEST_RESULT_PANEL_TEST_3_NG | WEARABLE_FT_TEST_RESULT_DRAWING_NG | WEARABLE_FT_TEST_RESULT_TEST_FW_VER_NG
					| WEARABLE_FT_TEST_RESULT_JIG_NG);


	while (u16_test_items) {
#if ENABLE_TEST_TIME_MEASUREMENT
		u8_ic_test_state = g_u8_ic_test_state;
		g_u64_spend_time = hal_get_system_time();
#endif

		switch (g_u8_ic_test_state) {
		case IC_TEST_INIT:
#if ENABLE_TEST_TIME_MEASUREMENT
			u64_fun_time = hal_get_system_time();
#endif
			g_u8_ic_test_state = IC_TEST_SYSTEM;
#if ENABLE_TEST_TIME_MEASUREMENT
			DEBUGOUT("IC_TEST_INIT Spend Time= %d\r\n", hal_get_system_time() - u64_fun_time);
#endif

			break;
		case IC_TEST_SYSTEM:
			if (system_test_rm6d031() == SUCCESS)
				g_u8_ic_test_state = IC_TEST_BURN_FW;
			else
				g_u8_ic_test_state = IC_TEST_EXIT;
			break;
		case IC_TEST_BURN_FW:
			if (burn_fw_rm6d031() == ERROR) {
				g_u32_wearable_test_result = WEARABLE_FT_TEST_RESULT_BURN_FW_NG;
				g_u8_ic_test_state = IC_TEST_EXIT;
			} else {
				g_u8_ic_test_state = IC_TEST_FW_VERSION_CHECK;
			}
			break;
		case IC_TEST_FW_VERSION_CHECK:
			if (fw_version_check_rm6d031() == ERROR) {
				g_u32_wearable_test_result = WEARABLE_FT_TEST_RESULT_IC_FW_VERIFY_NG;
				g_u8_ic_test_state = IC_TEST_EXIT;
			} else {
				g_u8_ic_test_state = IC_TEST_LOAD_TEST_FW;
			}
			break;
		case IC_TEST_LOAD_TEST_FW:
			if (load_test_fw_rm6d031() == ERROR) {
				DEBUGOUT("Load Test FW NG!!\r\n");
				g_u32_wearable_test_result = WEARABLE_FT_TEST_RESULT_LOAD_TESTFW_NG;
				g_u8_ic_test_state = IC_TEST_EXIT;
			} else {
				g_u8_ic_test_state = IC_TEST_ENTER_FW_TEST_MODE;
			}
			break;
		case IC_TEST_ENTER_FW_TEST_MODE:
			if (enter_fw_test_mode_rm6d031() == ERROR) {
				DEBUGOUT("Enter FW Test Mode NG!!\r\n");
				g_u8_ic_test_state = IC_TEST_EXIT;
			} else {
				g_u8_ic_test_state = IC_TEST_OPEN_SHORT;
			}
			break;
		case IC_TEST_OPEN_SHORT:
			if (ft_test_do_fw_test_rm6d031(u16_test_items) == SUCCESS) {
				DEBUGOUT("FW Test Finish! \r\n");
				g_u8_ic_test_state = IC_TEST_READ_TEST_FW_DATA;
			} else {
				DEBUGOUT("FW Test NG!!\r\n");
				g_u32_wearable_test_result = WEARABLE_FT_TEST_RESULT_TEST_INIT_NG;
				g_u8_ic_test_state = IC_TEST_EXIT;
			}
			break;
		case IC_TEST_READ_TEST_FW_DATA:
			if (read_test_fw_data_rm6d031(u16_test_items) == SUCCESS) {
				g_u8_ic_test_state = IC_TEST_BURN_CC;
			} else {
				DEBUGOUT("Read Test FW Data NG!!\r\n");
				if (g_st_test_info.u16_ft_eng_item & IC_TEST_ENG_ITEMS_TEST_ALL)
					g_u8_ic_test_state = IC_TEST_BURN_CC;
				else
					g_u8_ic_test_state = IC_TEST_EXIT;

			}
			break;
		case IC_TEST_BURN_CC:
			if (burn_cc_rm6d031(u16_test_items) == ERROR) {
				DEBUGOUT("Burn CC NG!!\r\n");
				g_u32_wearable_test_result |= WEARABLE_FT_TEST_RESULT_BURN_CC_NG;
				if (g_st_test_info.u16_ft_eng_item & IC_TEST_ENG_ITEMS_TEST_ALL)
					g_u8_ic_test_state = IC_TEST_PANEL_PATTERN_TEST;
				else
					g_u8_ic_test_state = IC_TEST_EXIT;
			} else {
				g_u8_ic_test_state = IC_TEST_PANEL_PATTERN_TEST;
			}
			break;
		case IC_TEST_PANEL_PATTERN_TEST:
#if ENABLE_TEST_GPIO_MEASUREMENT
			Chip_GPIO_SetPinOutHigh(LPC_GPIO, DONGLE_GPIO_PORT_1, DONGLE_GPIO_PIN_10);
			hal_delay_ms(1);
			Chip_GPIO_SetPinOutLow(LPC_GPIO, DONGLE_GPIO_PORT_1, DONGLE_GPIO_PIN_10);
#endif
			DEBUGOUT("B g_u32_wearable_test_result = 0x%08x\r\n", g_u32_wearable_test_result);


			g_u8_ic_test_state = IC_TEST_EXIT;
			break;
		case IC_TEST_EXIT:
#if ENABLE_TEST_TIME_MEASUREMENT
			u64_fun_time = hal_get_system_time();
			DEBUGOUT("Final action Start Time= %d\r\n", u64_fun_time);
#endif
			if ((g_st_test_info.u16_ft_test_info_1) == WEARBLE_FT_BURN_CC_SYSTEM_NG_CASE)
				g_u32_wearable_test_result = WEARABLE_FT_TEST_RESULT_BURN_CC_NG;

#if ENABLE_GPIO_TEST_OUTPUT
			g_u8_gpio_autorun_trigger = FALSE;
			if (g_u32_wearable_test_result & 0xFFFFFFFF) {
				Chip_GPIO_SetPinOutHigh(LPC_GPIO, DONGLE_GPIO_PORT_2, DONGLE_GPIO_PIN_2);
				DEBUGOUT("Test GPIO State NG!\r\n");
			} else {
				Chip_GPIO_SetPinOutHigh(LPC_GPIO, DONGLE_GPIO_PORT_2, DONGLE_GPIO_PIN_0);
				DEBUGOUT("Test GPIO State Pass!\r\n");
			}
#endif

			DEBUGOUT("g_u32_wearable_test_result = 0x%08x\r\n", g_u32_wearable_test_result);
			DEBUGOUT("Exit IC Test!\r\n");


			g_u8_ic_test_state = IC_TEST_INIT;
			if ((g_st_test_info.u16_ft_test_item & (IC_TEST_ITEMS_OPEN | IC_TEST_ITEMS_SHORT)) == 0) {
				ft_raw_data_checksum_cal_rm6d031((unsigned short *)g_i16_raw_data_1_short_buf);
				ft_raw_data_checksum_cal_rm6d031((unsigned short *)g_i16_raw_data_2_open_buf);
			}
			ft_raw_data_checksum_cal_rm6d031(g_u16_raw_data3_cc_buf);

			ft_raw_data_checksum_cal_rm6d031(g_u16_uc_buf);
#if !ENABLE_RAM_SHRINK
			ft_raw_data_checksum_cal_rm6d031(g_u16_cb);
#endif
			ft_raw_data_checksum_cal_rm6d031((unsigned short *)g_u16_raw_data3_golden_cc_buf);
			ft_raw_data_checksum_cal_rm6d031((unsigned short *)g_u16_uc_golden_cc_buf);
			ft_test_result_checksum_cal_rm6d031(g_u8_test_result);


			u16_test_items = 0;
#if ENABLE_TEST_TIME_MEASUREMENT
			DEBUGOUT("Final action Spend Time= %d\r\n", hal_get_system_time() - u64_fun_time);
#endif
			break;
		}

#if RAD_DONGLE_JIG
		hal_update_test_status(g_u8_ic_test_state);
#endif

#if ENABLE_TEST_TIME_MEASUREMENT
		DEBUGOUT("Test Item [%d] End (%d)\r\n", u8_ic_test_state, hal_get_system_time() - g_u64_spend_time);
#endif
	}



}


