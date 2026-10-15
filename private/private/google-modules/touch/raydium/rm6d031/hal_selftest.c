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
#if SELFTEST
#ifdef __KERNEL__
#include <linux/delay.h>
#include <linux/mutex.h>
#include <linux/unistd.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/time64.h>
#include <linux/timekeeping.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/of_gpio.h>

#else
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#endif
#include <linux/i2c.h>
#include "drv_interface.h"
#include "ic_drv_global.h"
#include "ic_drv_interface.h"
#include "hal_interface.h"
#include "raydium_driver.h"


#define ENABLE_LOAD_IMAGE_FROM_DONGLE_FLASH			0
#define ENABLE_LOAD_IMAGE_FROM_HEADER_FILE			1
#define MAXLOGLEN						1600//(160)
struct timespec64 ts;

#if ENABLE_LOAD_IMAGE_FROM_HEADER_FILE
#include "tpselftest.h"
#include "display_init.h"
#endif

u64 hal_get_system_time(void)
{
	u64 u64_timer;

	ktime_get_real_ts64(&ts);
	u64_timer = (ts.tv_sec % 1000) * 1000 + (ts.tv_nsec / 1000000);
	return u64_timer;
}

void hal_delay_ms(uint32_t u32_ms)
{
	msleep(u32_ms);
}

void hal_delay_us(uint32_t u32_us)
{
	udelay(u32_us);
}

unsigned char hal_gpio_touch_int_pin_state_access(void)
{
	return gpio_get_value(g_raydium_ts->pdata->irq_gpio);
}

unsigned char hal_gpio_touch_int_access(unsigned char u8_is_clear_flag)
{
	unsigned char u8_flag = g_u8_raydium_flag;

	if (u8_is_clear_flag && u8_flag)
		g_u8_raydium_flag &= ~INT_FLAG;

	return u8_flag;
}

void hal_gpio_touch_reset_pin_control(unsigned char u8_high)
{
	if (u8_high)
		gpio_set_value(g_raydium_ts->pdata->reset_gpio, 1);
	else
		gpio_set_value(g_raydium_ts->pdata->reset_gpio, 0);
	return;

}

void hal_gpio_touch_hw_reset(void)
{
	hal_gpio_touch_reset_pin_control(0);
	usleep_range(10000, 10500);
	hal_gpio_touch_reset_pin_control(1);
}

int hal_i2c_cmd_read(unsigned char u8_secondary_id, unsigned char *p_u8_cmd_buff, int cmd_len, unsigned char *p_u8_read_buff, int rx_len)
{
	int i32_ret = -EIO;
	struct i2c_msg msg[] = {
		{
			.addr = u8_secondary_id,
			.flags = RAYDIUM_I2C_WRITE,
			.len = cmd_len,
			.buf = p_u8_cmd_buff,
		},
		{
			.addr = u8_secondary_id,
			.flags = RAYDIUM_I2C_READ,
			.len = rx_len,
			.buf = p_u8_read_buff,
		},
	};

	if (i2c_transfer(g_raydium_ts->client->adapter, msg, 2) == 2)
		i32_ret = rx_len;

	return i32_ret;
}

int hal_i2c_write(unsigned char u8_secondary_id, const unsigned char *p_u8_tx_buff, unsigned char u8_len)
{
	int i32_ret = -EIO;
	unsigned char u8_buf[MAX_WRITE_PACKET_SIZE + 1];
	struct i2c_msg msg[] = {
		{
			.addr = u8_secondary_id,
			.flags = RAYDIUM_I2C_WRITE,
			.len = u8_len,
			.buf = u8_buf,
		},
	};
	memcpy(u8_buf, p_u8_tx_buff, u8_len);

	if (i2c_transfer(g_raydium_ts->client->adapter, msg, 1) == 1)
		i32_ret = u8_len;

	return i32_ret;
}

unsigned char hal_spi_set_pda_address(unsigned char u8_secondary_id,
				unsigned int u32_addr, unsigned char u8_mode)
{
	return ERROR;
}
int hal_spi_m_cmd_read(unsigned char u8_secondary_addr, unsigned char *p_u8_cmd_buff, int i32_cmd_len, unsigned char *p_u8_read_buff, unsigned int u32_rx_len)
{
	return ERROR;
}
int hal_spi_m_send(unsigned char u8_secondary_addr, unsigned char *p_u8_buff, unsigned char u8_len)
{
	return ERROR;
}
unsigned int hal_spi_m_read(int i32_cmd_len, unsigned char *p_u8_read_buff, unsigned int u32_rx_len)
{
	return ERROR;
}

/*Read Data from image flash*/

/*Read initial code from image (Dongle Ext.Flash /display_init.h)*/
unsigned char hal_read_image_dsp_initial_code(unsigned int u32_index, unsigned short u16_data_length, unsigned char *u8_readout_buffer)
{
#if ENABLE_LOAD_IMAGE_FROM_HEADER_FILE

	unsigned int u32_total_size = 0;

	u32_total_size = sizeof(g_u8_dsp_init_code);

	if ((u32_index + u16_data_length) >= u32_total_size)
		return ERROR;

	memcpy(u8_readout_buffer, &g_u8_dsp_init_code[u32_index], u16_data_length);
	return SUCCESS;

#endif
}

/*Read test info from ini image (Dongle Ext.Flash /tpselftest.h)*/
unsigned char hal_read_image_test_ini(unsigned char u8_sel, unsigned short u16_data_length, unsigned char *p_u8_readout_buffer)
{
#if ENABLE_LOAD_IMAGE_FROM_HEADER_FILE
	unsigned int u32_total_size = 0;
	unsigned char *p_u8_image_buf = NULL;

	switch (u8_sel) {
	case TEST_INI_TEST_INFO:
		p_u8_image_buf = (unsigned char *)u8_test_info;
		u32_total_size = sizeof(u8_test_info);
		break;
	case TEST_INI_THRESHOLD:
		p_u8_image_buf = (unsigned char *)i8_ft_test_thd;
		u32_total_size = sizeof(i8_ft_test_thd);
		break;
	case TEST_INI_PARA:
		p_u8_image_buf = (unsigned char *)u8_test_para;
		u32_total_size = sizeof(u8_test_para);
		break;
	case TEST_INI_RAW_DATA_3_CC:
		p_u8_image_buf = (unsigned char *)u8_raw_data_3_cc;
		u32_total_size = sizeof(u8_raw_data_3_cc);
		break;
	case TEST_INI_UC_CC:
		p_u8_image_buf = (unsigned char *)u8_raw_uc_cc;
		u32_total_size = sizeof(u8_raw_uc_cc);
		break;
	default:
		return ERROR;
	}

	if ((u16_data_length) > u32_total_size) {
		DEBUGOUT("Read test ini Fail: %d:%d:%d\r\n", u8_sel, u16_data_length, u32_total_size);
		return ERROR;
	}
	memcpy(p_u8_readout_buffer, p_u8_image_buf, u16_data_length);
	return SUCCESS;
#endif
}

unsigned char hal_read_image_fw_to_pram(unsigned char u8_sel, unsigned int u32_data_length, unsigned int pram_start_addr)
{
	uint16_t u16_data_buf_length;
	unsigned char *p_u8_image_buf = NULL;
	uint32_t u32_index;

	switch (u8_sel) {
	case TEST_BL_IMG:
		p_u8_image_buf = (unsigned char *)g_rad_boot_image;
		break;
	case TEST_TB_EXE_IMG:
		p_u8_image_buf = (unsigned char *)g_rad_tb_exe_image;
		break;
	case TEST_INIT_IMG:
		p_u8_image_buf = (unsigned char *)g_rad_init_image;
		break;
	case TEST_USER_FW_IMG:
		p_u8_image_buf = (unsigned char *)g_rad_fw_image;
		break;
	case TEST_USER_PARA_IMG:
		p_u8_image_buf = (unsigned char *)g_rad_para_image;
		break;
	case TEST_TEST_FW_IMG:
		p_u8_image_buf = (unsigned char *)g_rad_testfw_image;
		break;
	default:
		return ERROR;
	}
	u32_index = 0;
	u16_data_buf_length = 0x40;

	while (u32_index < u32_data_length) {

		if ((u32_index + u16_data_buf_length) > u32_data_length)
			u16_data_buf_length = u32_data_length - u32_index;

		// Read Flash Data
		if (!memcpy(&g_u8_data_buf[0], (unsigned char *)(p_u8_image_buf + u32_index), u16_data_buf_length))
			return ERROR;
		handle_ic_write(pram_start_addr, u16_data_buf_length, g_u8_data_buf, g_u8_drv_interface, I2C_WORD_MODE);

		u32_index += u16_data_buf_length;
		pram_start_addr += u16_data_buf_length;
	}

	return SUCCESS;
}
unsigned char hal_check_panel_jig_action_state(void)
{
	return FALSE;
}
unsigned char hal_read_image_data(unsigned int u32_addr, unsigned short u16_data_length)
{
	return FALSE;
}
void hal_i2c_bus_init(int ID, int speed)
{
}
void hal_i2c_bus_de_init(int ID)
{
}
void hal_control_i2c_voltage(unsigned char u8_rising_trigger)
{
}
#endif

