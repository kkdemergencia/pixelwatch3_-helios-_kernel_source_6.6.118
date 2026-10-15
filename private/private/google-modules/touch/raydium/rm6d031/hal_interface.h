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

#ifdef __KERNEL__
#include <linux/delay.h>
#include <linux/mutex.h>
#include <linux/unistd.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/timer.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/of_gpio.h>
#include <linux/firmware.h>
#else
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#endif
#include "config.h"

#if SELFTEST
#include "raydium_driver.h"
#endif

#if THIRD_PARTY_TEST_JIG | SELFTEST
extern unsigned char *g_rad_fw_image, *g_rad_init_image;
extern unsigned char *g_rad_boot_image, *g_rad_para_image,  *g_rad_tb_exe_image;
extern unsigned char *g_rad_testfw_image, *g_rad_testpara_image;
#endif


#if SELFTEST
#define DEBUGOUT(format, arg...)	pr_info(LOG_PREFIX format, ##arg)
#define FALSE				(1 == 0)
#define TRUE				(1 == 1)
#if !NXP_COMPILE
#define WORD				4
#define I2C1				1
#define SPEED_400KHZ			400000
#define SPEED_500KHZ			500000
#define SPEED_600KHZ			600000
#endif
#define ENABLE_WDT			0


#define IC_TEST_NODE_STATE_PASS			0x00
#define IC_TEST_NODE_STATE_OPEN_NG		0x01
#define IC_TEST_NODE_STATE_SHORT_NG		0x02
#define IC_TEST_NODE_STATE_NOISE_NG		0x04
#define IC_TEST_NODE_STATE_DISPLAY_PATTERN_NG	0x08
#define IC_TEST_NODE_STATE_UC_NG		0x10
#define IC_TEST_NODE_STATE_SINGLE_CC_OPEN_NG	0x20
#define IC_TEST_NODE_STATE_SYSTEM_NG		0x40
#define IC_TEST_NODE_STATE_SENSOR_IMPACT_NG	0x80

#endif


#if DEVICE_RM6D031
#define DFLASH_TEST_FW_ADDR_OFFSET		0x8800
#define DONGLE_FLASH_TEST_INI_ADDR		0x12000
#define DONGLE_FLASH_DSP_INIT_CODE_ADDR		0x12400
#define SENSOR_TEST_FRAME			100
#endif


extern unsigned char hal_check_panel_jig_action_state(void);
extern unsigned char hal_notify_panel_jig_do_action(unsigned char u8_cmd, unsigned char u8_wait_finish);


extern void hal_update_test_status(unsigned char u8_test_status);

extern u64 hal_get_system_time(void);
extern void hal_delay_ms(unsigned int u32_ms);
extern void hal_delay_us(unsigned int u32_us);

extern unsigned char hal_gpio_touch_int_access(unsigned char u8_is_clear_flag);
extern unsigned char hal_gpio_touch_int_pin_state_access(void);
extern unsigned char hal_gpio_touch_gpio_pin_state_access(unsigned char u8_port, unsigned char u8_pin);


extern void hal_gpio_touch_reset_pin_control(unsigned char u8_high);
extern void hal_gpio_touch_hw_reset(void);
extern void hal_gpio_touch_int_trigger_control(unsigned char u8_rising_trigger);


extern int hal_i2c_cmd_read(unsigned char u8_secondary_id, unsigned char *p_u8_cmd_buff, int cmd_len, unsigned char *p_u8_read_buff, int rx_len);
extern int hal_i2c_write(unsigned char u8_secondary_id, const unsigned char *p_u8_tx_buff, unsigned char u8_len);

extern unsigned char hal_spi_set_pda_address(unsigned char u8_secondary_id, unsigned int u32_addr, unsigned char u8_mode);
extern int hal_spi_m_cmd_read(unsigned char u8_secondary_addr, unsigned char *p_u8_cmd_buff, int i32_cmd_len, unsigned char *p_u8_read_buff, unsigned int u32_rx_len);
extern int hal_spi_m_send(unsigned char u8_secondary_addr, unsigned char *p_u8_buff, unsigned char u8_len);
extern unsigned int hal_spi_m_read(int i32_cmd_len, unsigned char *p_u8_read_buff, unsigned int u32_rx_len);

extern unsigned char hal_read_image_data(unsigned int u32_addr, unsigned short u16_data_length);
extern unsigned char hal_read_image_dsp_initial_code(unsigned int u32_index, unsigned short u16_data_length, unsigned char *u8_readout_buffer);
extern unsigned char hal_read_image_test_ini(unsigned char u8_sel, unsigned short u16_data_length, unsigned char *p_u8_readout_buffer);
extern unsigned char hal_read_image_fw_to_pram(unsigned char u8_sel, unsigned int u32_data_length, unsigned int pram_start_addr);
extern void hal_i2c_bus_init(int ID, int speed);
extern void hal_i2c_bus_de_init(int ID);
extern void hal_control_i2c_voltage(unsigned char u8_rising_trigger);
