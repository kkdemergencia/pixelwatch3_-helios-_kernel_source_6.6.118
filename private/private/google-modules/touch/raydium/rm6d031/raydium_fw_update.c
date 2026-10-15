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

#include <linux/i2c.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/mutex.h>
#include <linux/module.h>
#include <linux/gpio.h>
#include <linux/unistd.h>
#include <linux/uaccess.h>
#include <linux/string.h>
#include <linux/timer.h>
#include <linux/device.h>
#include <asm/traps.h>
#include <linux/firmware.h>
#include "raydium_driver.h"
#if defined(FW_MAPPING_EN)
#include "rad_fw_image_31.h"
#endif

void raydium_mem_table_init(void)
{

	g_rad_boot_image = kzalloc(RAD_BOOT_3X_SIZE, GFP_KERNEL);
	g_rad_init_image = kzalloc(RAD_INIT_3X_SIZE, GFP_KERNEL);
	g_rad_tb_exe_image = kzalloc(RAD_TB_EXE_3X_SIZE, GFP_KERNEL);
	g_rad_fw_image = kzalloc(RAD_FW_3X_SIZE, GFP_KERNEL);
	g_rad_para_image = kzalloc(RAD_PARA_3X_SIZE + 4, GFP_KERNEL);
	g_rad_testfw_image = kzalloc(RAD_TESTFW_3X_SIZE, GFP_KERNEL);
	g_rad_testpara_image = kzalloc(RAD_PARA_3X_SIZE + 4,
				       GFP_KERNEL);
	g_u8_table_init = SUCCESS;
}

static int load_firmware_image(const struct firmware *fw)
{
	unsigned int u32_offset = 0;

	if (fw && (fw->size == RAD_ALLFW_3X_SIZE)) {
		LOGI("get firmware success size:%zx\n", fw->size);

		memcpy(g_rad_boot_image, fw->data, RAD_BOOT_3X_SIZE);
		u32_offset += RAD_BOOT_3X_SIZE;
		memcpy(g_rad_tb_exe_image, fw->data + u32_offset, RAD_TB_EXE_3X_SIZE);
		u32_offset += RAD_TB_EXE_3X_SIZE;
		memcpy(g_rad_init_image, fw->data + u32_offset, RAD_INIT_3X_SIZE);
		u32_offset += RAD_INIT_3X_SIZE;
		memcpy(g_rad_fw_image, fw->data + u32_offset, RAD_FW_3X_SIZE);
		u32_offset += RAD_FW_3X_SIZE;
		memcpy(g_rad_para_image, fw->data + u32_offset, RAD_PARA_3X_SIZE + 4);
		u32_offset += RAD_PARA_3X_SIZE + 4;
		memcpy(g_rad_testfw_image, fw->data + u32_offset, RAD_FW_3X_SIZE);
		u32_offset += RAD_FW_3X_SIZE;
		memcpy(g_rad_testpara_image, fw->data + u32_offset, RAD_PARA_3X_SIZE + 4);

		memcpy(g_rad_testfw_image + RAD_FW_3X_SIZE, g_rad_testpara_image
		       , RAD_PARA_3X_SIZE + 4);
		return SUCCESS;
	}
	LOGE("failed to get firmware\n");
	return ERROR;
}

void firmware_update_cb(const struct firmware *fw, void *ctx)
{
	unsigned int u32_image_version;
	int i32_ret = SUCCESS;

	i32_ret = load_firmware_image(fw);
	if (i32_ret != SUCCESS) {
		LOGE("get firmware fail\n");
		goto load_fw_fail;
	}
	u32_image_version = (g_rad_para_image[PARA_FW_VERSION_OFFSET] << 24) |
				(g_rad_para_image[PARA_FW_VERSION_OFFSET + 1] << 16) |
				(g_rad_para_image[PARA_FW_VERSION_OFFSET + 2] << 8) |
				g_rad_para_image[PARA_FW_VERSION_OFFSET + 3];

	LOGI("RAD Image FW ver : 0x%x\n", u32_image_version);

	i32_ret = raydium_fw_update_check(u32_image_version);
	if (i32_ret < 0)
		LOGE("fw update fail!\n");
load_fw_fail:
	/* We only do the fw update process during probing, it's an one-time only checking.
	 * After retrying 2 times, if we still failed on updating, it's reasonable that something
	 * wrong with the hardware and mark the fw update process is done to unblock the booting
	 * process.
	 * At this moment, we should mark that we can offload to MCU
	 */
	g_raydium_ts->finish_fw_update = true;
}
int raydium_load_image(unsigned char *u8_buf, const char *name)
{
	int i32_ret = SUCCESS;
	struct i2c_client *client = g_raydium_ts->client;

	i32_ret = request_firmware_nowait(THIS_MODULE, true, name, &client->dev,
					  GFP_KERNEL, g_raydium_ts, firmware_update_cb);
	if (i32_ret) {
		LOGE("failed to get firmware %s %d\n", name, i32_ret);
		return i32_ret;
	}

	return SUCCESS;
}
int raydium_mem_table_setting(void)
{
	int i32_ret = SUCCESS;

	if (!g_raydium_ts->fw_name_dt) {
		LOGE("No firmware path\n");
		return ERROR;
	}
	LOGI("firmware path %s\n", g_raydium_ts->fw_name_dt);
	i32_ret = raydium_load_image(g_rad_fw_image, g_raydium_ts->fw_name_dt);
	if (i32_ret < 0)
		return ERROR;

	return i32_ret;
}

unsigned int bits_reverse(unsigned int u32_num, unsigned int bit_num)
{
	unsigned int reverse = 0, u32_i;

	for (u32_i = 0; u32_i < bit_num; u32_i++) {
		if (u32_num & (1 << u32_i))
			reverse |= 1 << ((bit_num - 1) - u32_i);
	}
	return reverse;
}

unsigned int rc_crc32(const char *buf, unsigned int u32_len,
		      unsigned int u32_crc)
{
	unsigned int u32_i;
	unsigned char u8_flash_byte, u8_current, u8_j;

	for (u32_i = 0; u32_i < u32_len; u32_i++) {
		u8_flash_byte = buf[u32_i];
		u8_current = (unsigned char)bits_reverse(u8_flash_byte, 8);
		for (u8_j = 0; u8_j < 8; u8_j++) {
			if ((u32_crc ^ u8_current) & 0x01)
				u32_crc = (u32_crc >> 1) ^ 0xedb88320;
			else
				u32_crc >>= 1;
			u8_current >>= 1;
		}
	}
	return u32_crc;
}

int wait_fw_state(struct i2c_client *client, unsigned int u32_addr,
		  unsigned int u32_state, unsigned long u32_delay_us,
		  unsigned short u16_retry)
{
	unsigned char u8_buf[4] = {0};
	unsigned int u32_read_data = 0;
	unsigned int u32_min_delay_us = u32_delay_us - 500;
	unsigned int u32_max_delay_us = u32_delay_us + 500;

	do {
		if (handle_i2c_pda_read(client, u32_addr, u8_buf, 4) == ERROR)
			return ERROR;

		memcpy(&u32_read_data, u8_buf, 4);
		u16_retry--;
		usleep_range(u32_min_delay_us, u32_max_delay_us);
	} while ((u32_read_data != u32_state) && (u16_retry != 0));

	if (u32_read_data != u32_state) {
		LOGE("confirm data error : 0x%x\n", u32_read_data);
		return ERROR;
	}

	return SUCCESS;
}

int wait_irq_state(struct i2c_client *client, unsigned int retry_time,
		   unsigned int u32_delay_us)
{
	int i32_ret = SUCCESS;
	unsigned int u32_retry;
	unsigned int u32_irq_value;
	unsigned int u32_min_delay_us = u32_delay_us - 500;
	unsigned int u32_max_delay_us = u32_delay_us + 500;

	u32_retry = retry_time;
	u32_irq_value = 0;
	while (u32_retry != 0 && u32_irq_value != 1) {
		u32_irq_value = gpio_get_value(g_raydium_ts->pdata->irq_gpio);
		usleep_range(u32_min_delay_us, u32_max_delay_us);
		u32_retry--;
	}
	LOGD("irq_value is %d\n", u32_irq_value);

	if (u32_retry == 0) {
		LOGE("%s, FW not ready, retry error!\n", __func__);
		i32_ret = ERROR;
	}

	return i32_ret;
}

int raydium_do_software_reset(struct i2c_client *client)
{
	int i32_ret = SUCCESS;

	unsigned char u8_buf[4];

	g_u8_i2c_retry_cnt = false;
	/*SW reset*/

	memset(u8_buf, 0, sizeof(u8_buf));
	u8_buf[0] = 0x01;
	LOGI("SW reset\n");
	i32_ret = handle_i2c_pda_write(client, RAYDIUM_PDA_BLKRST, u8_buf, 4);
	if (i32_ret < 0)
		goto exit;

	msleep(35);
	if (raydium_i2c_init() == ERROR) {
		LOGE("1.disable_i2c_deglitch_3x NG!\n");
		goto exit;
	}
exit:
	g_u8_i2c_retry_cnt = true;
	return i32_ret;
}

int set_skip_load(struct i2c_client *client)
{
	int i32_ret = SUCCESS;
	unsigned int u32_retry_time = 1000;
	unsigned char u8_buf[4];

	/*Skip load*/
	memset(u8_buf, 0, sizeof(u8_buf));
	u8_buf[0] = 0x10;
	u8_buf[1] = 0x08;
	i32_ret = handle_i2c_pda_write(client, RAYDIUM_PDA_BOOTREG, u8_buf, 4);
	if (i32_ret < 0)
		goto exit_upgrade;
	i32_ret = raydium_do_software_reset(client);
	if (i32_ret < 0)
		LOGE("%s, SW reset error!\n", __func__);
	msleep(35);

	i32_ret = wait_fw_state(client, RAYDIUM_PDA_BOOTSTATE, 0x82, 2000, u32_retry_time);
	if (i32_ret < 0)
		LOGE("%s, wait_fw_state error!\n", __func__);

exit_upgrade:
	return i32_ret;
}

/*check pram crc32*/
static int raydium_check_pram_crc_rm6d031(struct i2c_client *client,
				     unsigned int u32_addr,
				     unsigned int u32_len)
{
	int i32_ret = SUCCESS;
	unsigned int u32_crc_addr = u32_addr + u32_len;
	unsigned int u32_end_addr = u32_crc_addr - 1;
	unsigned int u32_crc_result, u32_read_data;
	unsigned int u32_retry = 20;
	unsigned char u8_buf[4], u8_retry = 3;

	memset(u8_buf, 0, sizeof(u8_buf));
	u8_buf[0] = (unsigned char)(u32_addr & 0xFF);
	u8_buf[1] = (unsigned char)((u32_addr & 0xFF00) >> 8);
	u8_buf[2] = (unsigned char)(u32_end_addr & 0xFF);
	u8_buf[3] = (unsigned char)((u32_end_addr & 0xFF00) >> 8);

	i32_ret = handle_i2c_pda_write(client, RAYDIUM_PDA_PRGCHKSUMADDR, u8_buf, 4);
	if (i32_ret < 0)
		goto exit_upgrade;
	i32_ret = handle_i2c_pda_read(client, RAYDIUM_PDA_PRGCHKSUMENG, u8_buf, 4);
	if (i32_ret < 0)
		goto exit_upgrade;
	u8_buf[3] |= 0x81;
	i32_ret = handle_i2c_pda_write(client, RAYDIUM_PDA_PRGCHKSUMENG, u8_buf, 4);

	while (u8_buf[3] != 0x80 && u32_retry != 0) {
		i32_ret = handle_i2c_pda_read(client, RAYDIUM_PDA_PRGCHKSUMENG, u8_buf, 4);
		if (i32_ret < 0)
			goto exit_upgrade;

		u32_retry--;
		usleep_range(4500, 5500);
	}
	if (u32_retry == 0) {
		LOGE("%s, Cal CRC not ready, retry error!\n", __func__);
		i32_ret = handle_i2c_pda_read(client, RAYDIUM_PDA_DISPLAY_MODE, u8_buf, 4);
		if (i32_ret < 0)
			goto exit_upgrade;
		LOGI("DSP is 0x%x,0x%x,0x%x,0x%x!\n", u8_buf[0], u8_buf[1], u8_buf[2], u8_buf[3]);
		i32_ret = ERROR;
	}

	i32_ret = handle_i2c_pda_read(client, RAYDIUM_PDA_PRGCHKSUMRESULT, u8_buf, 4);
	if (i32_ret < 0)
		goto exit_upgrade;
	memcpy(&u32_crc_result, u8_buf, 4);
	i32_ret = handle_i2c_pda_read(client, u32_crc_addr, u8_buf, 4);
	if (i32_ret < 0)
		goto exit_upgrade;
	memcpy(&u32_read_data, u8_buf, 4);

	while (u32_read_data != u32_crc_result && u8_retry > 0) {
		i32_ret = handle_i2c_pda_read(client, RAYDIUM_PDA_PRGCHKSUMRESULT, u8_buf, 4);
		if (i32_ret < 0)
			goto exit_upgrade;
		memcpy(&u32_crc_result, u8_buf, 4);
		usleep_range(1500, 2500);
		u8_retry--;
	}
	if (u32_read_data != u32_crc_result) {
		LOGE("check pram crc fail!!\n");
		LOGE("u32_read_data 0x%x\n", u32_read_data);
		LOGE("u32_crc_result 0x%x\n", u32_crc_result);
		i32_ret = ERROR;
		goto exit_upgrade;
	} else if ((u8_retry != 3) || (u32_crc_result == 0)) {
		LOGD("check pram crc fail!!\n");
		LOGD("u8_retry : %d\n", u8_retry);
		LOGD("u32_read_data 0x%x\n", u32_read_data);
		LOGD("u32_crc_result 0x%x\n", u32_crc_result);
		i32_ret = ERROR;
		goto exit_upgrade;
	} else {
		LOGD("check pram crc pass!!\n");
		LOGD("u8_retry : %d\n", u8_retry);
		LOGD("u32_read_data 0x%x\n", u32_read_data);
		LOGD("u32_crc_result 0x%x\n", u32_crc_result);
		i32_ret = SUCCESS;
	}

exit_upgrade:
	return i32_ret;
}

/* upgrade firmware with image file */
static int raydium_write_to_pram_rm6d031(struct i2c_client *client,
				    unsigned int u32_fw_addr,
				    unsigned char u8_type)
{
	int i32_ret = ERROR;
	unsigned int u32_fw_size = 0;
	unsigned char *p_u8_firmware_data = NULL;
	unsigned int u32_write_offset = 0;
	unsigned short u16_write_length = 0;

	switch (u8_type) {
	case RAYDIUM_INIT:
		u32_fw_size = 0x24;
		p_u8_firmware_data = g_rad_init_image;
		break;
	case RAYDIUM_BOOTLOADER:
		u32_fw_size = 0x800;
		p_u8_firmware_data = g_rad_boot_image;
		break;
	case RAYDIUM_TB_EXE:
		u32_fw_size = 0x80;
		p_u8_firmware_data = g_rad_tb_exe_image;
		break;
	case RAYDIUM_PARA:
		u32_fw_size = RAYDIUM_PDA_PARALENGTH;
		p_u8_firmware_data = g_rad_para_image;
		break;
	case RAYDIUM_COMP:
		u32_fw_size = RAD_CC_3X_SIZE;
		p_u8_firmware_data = g_rad_cc_image;
		break;
	case RAYDIUM_FIRMWARE:
		u32_fw_size = RAYDIUM_PDA_FIRMWARELENGTH;
		p_u8_firmware_data = g_rad_fw_image;
		break;
	case RAYDIUM_TEST_FW:
		u32_fw_size = RAYDIUM_PDA_FIRMWARELENGTH + RAYDIUM_PDA_PARALENGTH;
		p_u8_firmware_data = g_rad_testfw_image;
		break;

	default:
		goto exit_upgrade;
	}

	g_u8_raydium_flag &= ~INT_FLAG;

	u32_write_offset = 0;
	while (u32_write_offset < u32_fw_size) {
		if ((u32_write_offset + MAX_WRITE_PACKET_SIZE) < u32_fw_size)
			u16_write_length = MAX_WRITE_PACKET_SIZE;
		else
			u16_write_length =
				(unsigned short)(u32_fw_size - u32_write_offset);

		i32_ret = handle_i2c_pda_write(
				  client,
				  (u32_fw_addr + u32_write_offset),
				  (p_u8_firmware_data + u32_write_offset),
				  u16_write_length);
		if (i32_ret < 0 || ((g_u8_raydium_flag & INT_FLAG) == INT_FLAG))
			goto exit_upgrade;

		u32_write_offset += (unsigned long)u16_write_length;
	}
	u32_fw_addr += u32_write_offset;

exit_upgrade:
	if (i32_ret < 0) {
		LOGE("upgrade failed\n");
		return i32_ret;
	}
	LOGI("upgrade success\n");
	return SUCCESS;
}

unsigned char raydium_stop_mcu_rm6d031(unsigned char u8_is_tp_reset)
{
	unsigned short u16_time_out = 100;
	unsigned int u32_read_data = 0;
	unsigned int u32_write = 0;

	if (u8_is_tp_reset) {
		gpio_set_value(g_raydium_ts->pdata->reset_gpio, 1);
		gpio_set_value(g_raydium_ts->pdata->reset_gpio, 0);
		msleep(RAYDIUM_RESET_INTERVAL_MSEC);
		gpio_set_value(g_raydium_ts->pdata->reset_gpio, 1);

		g_u8_i2c_mode = PDA2_MODE;
		msleep(35);
		if (raydium_i2c_init() == ERROR) {
			LOGE("disable_i2c_deglitch_3x NG!\n");
			return ERROR;
		}
	}
	LOGI("%s\n", __func__);

	u32_write = 0x30;
	if (handle_i2c_pda_write(g_raydium_ts->client, RAYDIUM_PDA_BOOTREG
				 , (unsigned char *)&u32_write, 4) == ERROR) {
		return ERROR;
	}

	u32_write = 0x01;
	if (handle_i2c_pda_write(g_raydium_ts->client, RAYDIUM_PDA_BLKRST, (unsigned char *)&u32_write, 4
				) == ERROR) {
		return ERROR;
	}
	msleep(100);

	if (raydium_i2c_init() == ERROR) {
		LOGE("disable_i2c_deglitch_3x NG!\n");
		return ERROR;
	}
	LOGI("stop_mcu 2\n");

	if (handle_i2c_pda_read(g_raydium_ts->client, RAYDIUM_PDA_BOOTREG
				, (unsigned char *)(&u32_read_data), 4) == ERROR) {
		return ERROR;
	}

	while ((u32_read_data & 0x2000) == 0 && u16_time_out-- > 0) {
		msleep(20);
		if (handle_i2c_pda_read(g_raydium_ts->client, RAYDIUM_PDA_BOOTREG
					, (unsigned char *)(&u32_read_data), 4) == ERROR) {
			return ERROR;
		}
	}

	LOGD("Stop MCU=0x%X(0x%x)(%d)!!\n", u32_read_data, (u32_read_data & 0x2000), u16_time_out);

	if ((u32_read_data & 0x2000) == 0)
		return ERROR;

	return SUCCESS;
}

/* Raydium fireware upgrade flow */
static int raydium_fw_upgrade_rm6d031(struct i2c_client *client,
				 unsigned char u8_type,
				 unsigned char u8_check_crc)
{
	int i32_ret = 0;
	unsigned char u8_buf[4] = {0};
	unsigned short u16_retry = 1000;
	unsigned int u32_write = 0;

	/*##### wait for boot-loader start #####*/
	LOGI("Type is %x\n", u8_type);

	g_u8_i2c_retry_cnt = false;

	/*read Boot version*/
	if (handle_i2c_pda_read(client, RAYDIUM_PDA_BOOTVERSION, u8_buf, 4) == ERROR)
		return ERROR;
	u32_write = (u8_buf[3] << 24) | (u8_buf[2] << 16) | (u8_buf[1] << 8) | u8_buf[0];
	LOGI("bootloader ver: 0x%x\n", u32_write);

	if (u8_type != RAYDIUM_COMP) {
		if (raydium_stop_mcu_rm6d031(0) == ERROR)
			return ERROR;
	}

	/*#start write data to PRAM*/
	if (u8_type == RAYDIUM_FIRMWARE) {
		/* unlock PRAM */
		u8_buf[0] = (BOTLR_LOCK | COMP_LOCK | BASEL_LOCK | INICO_LOCK);
		i32_ret = handle_i2c_pda_write(client,
					       RAYDIUM_PDA_PRAMLOCK,
					       u8_buf, 4);
		if (i32_ret < 0)
			goto exit_upgrade;
		i32_ret = raydium_write_to_pram_rm6d031(client, RAYDIUM_PDA_FIRMWAREADDR,
						   RAYDIUM_FIRMWARE);
		if (i32_ret < 0)
			goto exit_upgrade;

		i32_ret = raydium_write_to_pram_rm6d031(client, RAYDIUM_PDA_PARAADDR,
						   RAYDIUM_PARA);
		if (i32_ret < 0)
			goto exit_upgrade;

		i32_ret = raydium_check_pram_crc_rm6d031(client, RAYDIUM_PDA_FIRMWAREADDR,
						    RAYDIUM_PDA_CRCLENGTH);
		if (i32_ret < 0)
			goto exit_upgrade;

	} else if (u8_type == RAYDIUM_BOOTLOADER) {
		/* unlock PRAM */
		u8_buf[0] = (BOTLR_LOCK | CONFIG_LOCK | COMP_LOCK | BASEL_LOCK | INICO_LOCK);

		i32_ret = handle_i2c_pda_write(client, RAYDIUM_PDA_PRAMLOCK,
					       u8_buf, 4);
		if (i32_ret < 0)
			goto exit_upgrade;
		/* bootloader */
		i32_ret = raydium_write_to_pram_rm6d031(client, 0x0800,
						   RAYDIUM_BOOTLOADER);
		if (i32_ret < 0)
			goto exit_upgrade;
		i32_ret = raydium_check_pram_crc_rm6d031(client, 0x800,
						    0x7FC);
		if (i32_ret < 0)
			goto exit_upgrade;

		/* table execution */
		i32_ret = raydium_write_to_pram_rm6d031(client, 0x1000,
						   RAYDIUM_TB_EXE);
		if (i32_ret < 0)
			goto exit_upgrade;
		i32_ret = raydium_check_pram_crc_rm6d031(client, 0x1000,
						    0x7C);
		if (i32_ret < 0)
			goto exit_upgrade;

		/* initial code */
		i32_ret = raydium_write_to_pram_rm6d031(client, 0x1080,
						   RAYDIUM_INIT);
		if (i32_ret < 0)
			goto exit_upgrade;
		i32_ret = raydium_check_pram_crc_rm6d031(client, 0x1080,
						    0x20);
		if (i32_ret < 0)
			goto exit_upgrade;
	}

	if (u8_type != RAYDIUM_COMP) {
		/*release mcu hold*/
		/*Skip load*/
		i32_ret = set_skip_load(client);
		if (i32_ret < 0)
			LOGE("%s, set skip_load error!\n", __func__);
		/*#confirm in burn mode*/
		if (wait_fw_state(client, RAYDIUM_PDA_BOOTSTATE, 0x82,
				  2000, u16_retry) == ERROR) {
			LOGE("Error, confirm in burn mode 1\n");
			i32_ret = ERROR;
			goto exit_upgrade;
		}

	}

	/*#setting burning mode*/
	memset(u8_buf, 0, sizeof(u8_buf));
	u8_buf[0] = 0x01;
	i32_ret = handle_i2c_pda_write(client, RAYDIUM_PDA_BOOTENG1, u8_buf, 4);
	if (i32_ret < 0)
		goto exit_upgrade;

	memset(u8_buf, 0, sizeof(u8_buf));
	i32_ret = handle_i2c_pda_write(client, RAYDIUM_PDA_BOOTENG2, u8_buf, 4);
	if (i32_ret < 0)
		goto exit_upgrade;

	memset(u8_buf, 0, sizeof(u8_buf));
	i32_ret = handle_i2c_pda_write(client, RAYDIUM_PDA_BOOTENG3, u8_buf, 4);
	if (i32_ret < 0)
		goto exit_upgrade;

	memset(u8_buf, 0, sizeof(u8_buf));
	u8_buf[0] = 0x01;
	i32_ret = handle_i2c_pda_write(client, RAYDIUM_PDA_BOOTMODE, u8_buf, 4);
	if (i32_ret < 0)
		goto exit_upgrade;

	/*#confirm in burn mode*/
	if (wait_fw_state(client, RAYDIUM_PDA_BOOTSTATE, 0xFF,
			  2000, u16_retry) == ERROR) {
		LOGE("bootloader wrt empty flash fail\n");
		LOGE("bootloader confirm in burn mode fail, Type=%d\n", u8_type);
		i32_ret = ERROR;
		goto exit_upgrade;
	}

	/* burning setting */
	memset(u8_buf, 0, sizeof(u8_buf));
	u8_buf[0] = 0x10;
	i32_ret = handle_i2c_pda_write(client, RAYDIUM_PDA_BOOTREG, u8_buf, 4);
	if (i32_ret < 0)
		goto exit_upgrade;

	memset(u8_buf, 0, sizeof(u8_buf));
	u8_buf[0] = u8_type;
	i32_ret = handle_i2c_pda_write(client, RAYDIUM_PDA_PRAMTYPE, u8_buf, 4);
	if (i32_ret < 0)
		goto exit_upgrade;

	/*#set PRAM length (at 'h5000_090C)*/
	if (u8_type == RAYDIUM_COMP) {
		memset(u8_buf, 0, sizeof(u8_buf));
		u8_buf[0] = 0x98;
		u8_buf[1] = 0x8A;
		i32_ret = handle_i2c_pda_write(client, RAYDIUM_PDA_PRAMADDR,
					       u8_buf, 4);
		if (i32_ret < 0)
			goto exit_upgrade;

	} else if (u8_type == RAYDIUM_FIRMWARE) {
		memset(u8_buf, 0, sizeof(u8_buf));
		u8_buf[0] = 0x00;
		u8_buf[1] = 0x08;
		i32_ret = handle_i2c_pda_write(client, RAYDIUM_PDA_PRAMADDR,
					       u8_buf, 4);
		if (i32_ret < 0)
			goto exit_upgrade;

	} else if (u8_type == RAYDIUM_BOOTLOADER) {
		memset(u8_buf, 0, sizeof(u8_buf));
		u8_buf[0] = 0x00;
		u8_buf[1] = 0x08;
		i32_ret = handle_i2c_pda_write(client, 0x50000908,
					       u8_buf, 4);
		if (i32_ret < 0)
			goto exit_upgrade;
	}


	/*#set sync_data(RAYDIUM_PDA_SYNCDATA) = 0 as WRT data finish*/
	memset(u8_buf, 0, sizeof(u8_buf));
	i32_ret = handle_i2c_pda_write(client, RAYDIUM_PDA_SYNCDATA, u8_buf, 4);
	if (i32_ret < 0)
		goto exit_upgrade;

	/*#wait for sync_data finish*/
	if (wait_fw_state(client, RAYDIUM_PDA_BOOTENG4, 168, 1000,
			  u16_retry) == ERROR) {
		LOGE("Error, wait for input unlock key\n");
		i32_ret = ERROR;
		goto exit_upgrade;
	}

	/*#flash unlock key*/
	memset(u8_buf, 0, sizeof(u8_buf));
	u8_buf[0] = 0xd7;
	i32_ret = handle_i2c_pda_write(client, RAYDIUM_PDA_FLKEY2, u8_buf, 4);
	if (i32_ret < 0)
		goto exit_upgrade;

	memset(u8_buf, 0, sizeof(u8_buf));
	u8_buf[0] = 0xa5;
	i32_ret = handle_i2c_pda_write(client, RAYDIUM_PDA_FLKEY1, u8_buf, 4);
	if (i32_ret < 0)
		goto exit_upgrade;

	memset(u8_buf, 0, sizeof(u8_buf));
	i32_ret = handle_i2c_pda_write(client, RAYDIUM_PDA_FLKEY1, u8_buf, 4);
	if (i32_ret < 0)
		goto exit_upgrade;

	memset(u8_buf, 0, sizeof(u8_buf));
	u8_buf[0] = 0xa5;
	i32_ret = handle_i2c_pda_write(client, RAYDIUM_PDA_FLKEY1, u8_buf, 4);
	if (i32_ret < 0)
		goto exit_upgrade;

	memset(u8_buf, 0, sizeof(u8_buf));
	i32_ret = handle_i2c_pda_write(client, RAYDIUM_PDA_FLKEY2, u8_buf, 4);
	if (i32_ret < 0)
		goto exit_upgrade;

	memset(u8_buf, 0, sizeof(u8_buf));
	i32_ret = handle_i2c_pda_write(client, RAYDIUM_PDA_FLASHPRO, u8_buf, 4);
	if (i32_ret < 0)
		goto exit_upgrade;

	/*#ready to burn flash*/
	memset(u8_buf, 0, sizeof(u8_buf));
	u8_buf[0] = 0xa8;
	i32_ret = handle_i2c_pda_write(client, RAYDIUM_PDA_BOOTSTATE, u8_buf, 4);
	if (i32_ret < 0)
		goto exit_upgrade;

	/*#clr sync_data(RAYDIUM_PDA_SYNCDATA) = 0 as finish*/
	memset(u8_buf, 0, sizeof(u8_buf));
	i32_ret = handle_i2c_pda_write(client, RAYDIUM_PDA_SYNCDATA, u8_buf, 4);
	if (i32_ret < 0)
		goto exit_upgrade;

	/* wait erase/wrt finish
	 * confirm burning_state result (gu8I2CSyncData.burning_state =
	 * BURNING_WRT_FLASH_FINISH at RAYDIUM_PDA_BOOTENG3)
	 */
	if (wait_fw_state(client, RAYDIUM_PDA_BOOTENG3, 6, 8000,
			  u16_retry) == ERROR) {
		LOGE("Error, wait erase/wrt finish\n");
		i32_ret = ERROR;
		goto exit_upgrade;
	}
	LOGI("Burn flash ok\n");

	if (u8_check_crc) {
		memset(u8_buf, 0, sizeof(u8_buf));
		i32_ret = handle_i2c_pda_write(client, RAYDIUM_PDA_SYNCDATA,
					       u8_buf, 4);
		if (i32_ret < 0)
			goto exit_upgrade;

		/*#wait software reset finish*/
		msleep(35);

		/* wait sw reset finished RAYDIUM_PDA_BOOTSTATE = 0x82 */
		if (wait_fw_state(client, RAYDIUM_PDA_BOOTSTATE, 0x82, 2000,
				  u16_retry) == ERROR) {
			LOGE("Error, wait sw reset finished\n");
			i32_ret = ERROR;
			goto exit_upgrade;
		}

		/*#set test_mode = 1 start to check crc*/
		memset(u8_buf, 0, sizeof(u8_buf));
		u8_buf[0] = 0x01;
		i32_ret = handle_i2c_pda_write(client, RAYDIUM_PDA_BOOTMODE,
					       u8_buf, 4);
		if (i32_ret < 0)
			goto exit_upgrade;

		/*#wait crc check finish*/
		if (wait_fw_state(client, RAYDIUM_PDA_BOOTENG2, 2,
				  2000, u16_retry)
		    == ERROR) {
			LOGE("Error, wait crc check finish\n");
			i32_ret = ERROR;
			goto exit_upgrade;
		}

		/*#crc check pass RAYDIUM_PDA_BOOTSTATE = 0x81*/
		if (wait_fw_state(client, RAYDIUM_PDA_BOOTSTATE, 0x81,
				  2000, u16_retry)
		    == ERROR) {
			LOGE("Error, confirm crc result\n");
			i32_ret = ERROR;
			goto exit_upgrade;
		}
		/*#run to next step*/
		LOGI("Type 0x%x => Pass\n", u8_type);

		/* sw rest */

		i32_ret = raydium_do_software_reset(client);
		if (i32_ret < 0)
			goto exit_upgrade;

		g_u8_i2c_mode = PDA2_MODE;

		LOGI("Burn FW finish!\n");
	}

exit_upgrade:
	g_u8_i2c_retry_cnt = true;
	return i32_ret;
}

static int raydium_boot_upgrade_rm6d031(struct i2c_client *client, unsigned char u8_is_tp_reset)
{
	int i32_ret = SUCCESS;
	unsigned char u8_buf[4];
	unsigned short u16_retry = 1000;

	g_u8_i2c_retry_cnt = false;

	/*set mcu hold*/
	memset(u8_buf, 0, sizeof(u8_buf));
	if (raydium_stop_mcu_rm6d031(u8_is_tp_reset) == ERROR)
		return ERROR;
	/*WRT boot-loader to PRAM first*/
	memset(u8_buf, 0, sizeof(u8_buf));
	u8_buf[0] = 0x1E;
	i32_ret = handle_i2c_pda_write(client, RAYDIUM_PDA_PRAMLOCK, u8_buf, 4);

	/*Sending bootloader*/
	i32_ret = raydium_write_to_pram_rm6d031(client, 0x0000,
					   RAYDIUM_BOOTLOADER);
	if (i32_ret < 0)
		goto exit_upgrade;

	i32_ret = raydium_check_pram_crc_rm6d031(client, 0x000, 0x7FC);
	if (i32_ret < 0)
		goto exit_upgrade;

	/*Sending TB execution*/
	i32_ret = raydium_write_to_pram_rm6d031(client, 0x8f80,
					   RAYDIUM_TB_EXE);
	if (i32_ret < 0)
		goto exit_upgrade;

	i32_ret = raydium_check_pram_crc_rm6d031(client, 0x8F80, 0x7C);
	if (i32_ret < 0)
		goto exit_upgrade;

	/*release mcu hold*/
	/*Skip load*/
	i32_ret = set_skip_load(client);
	if (i32_ret < 0)
		LOGE("%s, set skip_load error!\n", __func__);
	msleep(35);
	if (wait_fw_state(client, RAYDIUM_PDA_BOOTSTATE, 0x82, 1000,
			  u16_retry) == ERROR) {
		LOGE("Error, wait for input unlock key\n");
		i32_ret = ERROR;
		goto exit_upgrade;
	}
exit_upgrade:
	g_u8_i2c_retry_cnt = true;
	return i32_ret;
}

int raydium_fw_update_check(unsigned int u32_check_version)
{
	int i32_ret = SUCCESS;
	unsigned int u32_fw_version = 0;
	unsigned char u8_rbuffer[4] = {0};
	unsigned int u32_retry_limit = (g_raydium_ts->is_suspend) ? 10 : 2;
	unsigned int u32_retry_count = 0;

	mutex_lock(&g_raydium_ts->lock);
	// To avoid touch IC is in STB mode when we are going to do fw update
	if (g_raydium_ts->is_sleep_bits) {
		LOGI("%s: wake up touch IC due to FW update\n", __func__);
		i32_ret = raydium_i2c_touch_sleep_locked(false);
		if (i32_ret)
			LOGW("Failed to wake up touch IC for FW update, ret=%d\n", i32_ret);
	}

	if (g_raydium_ts->fw_version != u32_check_version) {
		LOGI("FW need update.\n");
		g_u8_raydium_flag |= ENG_MODE;

		for (; u32_retry_count < u32_retry_limit; u32_retry_count++) {
			i32_ret = raydium_burn_fw(g_raydium_ts->client);
			if (i32_ret >= 0)
				break;
			LOGW("FW update failed, retrying\n");
			usleep_range(2000, 2500);
		}
		if (i32_ret < 0) {
			LOGE("FW update fail after retrying for %d times: %d\n", u32_retry_limit,
			     i32_ret);
			goto exit_error;
		}

		g_u8_raydium_flag &= ~ENG_MODE;

		i32_ret = raydium_i2c_pda2_set_page(g_raydium_ts->client,
							g_raydium_ts->is_suspend,
							RAYDIUM_PDA2_PAGE_0);
		if (i32_ret < 0)
			goto exit_error;

		i32_ret = raydium_i2c_pda2_read(g_raydium_ts->client,
						RAYDIUM_PDA2_FW_VERSION_ADDR,
						u8_rbuffer,
						4);
		if (i32_ret < 0)
			goto exit_error;

		u32_fw_version = (u8_rbuffer[0] << 24) |
				(u8_rbuffer[1] << 16) |
				(u8_rbuffer[2] << 8) |
				u8_rbuffer[3];
		LOGI("RAD FW ver is 0x%x\n", u32_fw_version);
		g_raydium_ts->fw_version = u32_fw_version;
	} else {
		LOGI("FW is the latest version.\n");
	}

exit_error:
	// Let touch IC be in STB mode if stb mode is requested and fw update is finished
	if (g_raydium_ts->is_sleep_bits) {
		LOGI("%s: sleep touch IC back because FW update is finished\n", __func__);
		i32_ret = raydium_i2c_touch_sleep_locked(true);
		if (i32_ret)
			LOGW("Failed to sleep back touch IC for FW update, ret=%d\n", i32_ret);
	}
	mutex_unlock(&g_raydium_ts->lock);
	return i32_ret;
}

int raydium_burn_fw(struct i2c_client *client)
{
	int i32_ret = ERROR;

	g_u8_resetflag = true;
	LOGI("start burn function!\n");
	i32_ret = raydium_boot_upgrade_rm6d031(client, 1);
	if (i32_ret < 0)
		goto exit_upgrade;

	i32_ret = raydium_fw_upgrade_rm6d031(client, RAYDIUM_BOOTLOADER, 0);
	if (i32_ret < 0)
		goto exit_upgrade;

	i32_ret = raydium_fw_upgrade_rm6d031(client, RAYDIUM_FIRMWARE, 1);
	if (i32_ret < 0)
		goto exit_upgrade;

exit_upgrade:
	return i32_ret;
}

int raydium_fw_update_init(unsigned short u16_i2c_data)
{

	unsigned int u32_fw_version = 0;
	unsigned char u8_rbuffer[4] = {0};

	int i32_ret = ERROR;

	mutex_lock(&g_raydium_ts->lock);
	i32_ret = raydium_i2c_pda2_set_page(g_raydium_ts->client,
					    g_raydium_ts->is_suspend,
					    RAYDIUM_PDA2_PAGE_0);
	if (i32_ret < 0)
		goto exit_error;

	i32_ret = raydium_i2c_pda2_read(g_raydium_ts->client,
					RAYDIUM_PDA2_FW_VERSION_ADDR,
					u8_rbuffer,
					4);
	if (i32_ret < 0)
		goto exit_error;

	mutex_unlock(&g_raydium_ts->lock);

	u32_fw_version = (u8_rbuffer[0] << 24) | (u8_rbuffer[1] << 16) |
			 (u8_rbuffer[2] << 8) | u8_rbuffer[3];
	LOGI("RAD FW ver 0x%.8x\n", u32_fw_version);

	g_raydium_ts->fw_version = u32_fw_version;

	i32_ret = handle_i2c_pda_read(g_raydium_ts->client,
					RAYDIUM_PDA_PID,
					u8_rbuffer,
					4);

	g_raydium_ts->id = (u8_rbuffer[3] << 24) | (u8_rbuffer[2] << 16) |
			 (u8_rbuffer[1] << 8) | u8_rbuffer[0];

	raydium_mem_table_init();
	if (raydium_mem_table_setting() != SUCCESS) {
		LOGE("Mem setting failed, Stop fw upgrade!\n");
		return i32_ret;
	}
	LOGI("RAD Load FW success\n");
	return i32_ret;

exit_error:
	mutex_unlock(&g_raydium_ts->lock);
	return i32_ret;
}
int raydium_burn_comp(struct i2c_client *client)
{
	int i32_ret = ERROR;

	i32_ret = raydium_boot_upgrade_rm6d031(client, 0);
	if (i32_ret < 0)
		goto exit_upgrade;

	i32_ret = set_skip_load(client);
	if (i32_ret < 0)
		goto exit_upgrade;


	i32_ret = raydium_fw_upgrade_rm6d031(client, RAYDIUM_COMP, 1);
	if (i32_ret < 0)
		goto exit_upgrade;

	i32_ret = SUCCESS;

exit_upgrade:
	return i32_ret;
}

int raydium_check_cc_ready(struct i2c_client *client)
{
	int i32_ret = SUCCESS;
	unsigned int u32_retry = 100;
	unsigned char u8_buf[4];

	while (((u8_buf[0] & 0x03) == 0) && u32_retry != 0) {
		i32_ret = handle_i2c_pda_read(client, 0x8F78, u8_buf, 4);
		if (i32_ret < 0)
			goto exit;

		u32_retry--;
		usleep_range(5000, 5500);
	}

	if (u32_retry == 0) {
		LOGE("FW CC not ready: %x, retry error!\n", u8_buf[0]);
		handle_i2c_pda_read(client, RAYDIUM_PDA_DISPLAY_MODE, u8_buf, 4);
		LOGE("DSP is %2x.%2x.%2x.%2x\n", u8_buf[3], u8_buf[2], u8_buf[1], u8_buf[0]);
		i32_ret = -1;
	} else {
		LOGI("FW CC is ready!!\n");
		usleep_range(5000, 5500);
	}

	if ((u8_buf[0] & 0x40) == 0x40) {
		LOGE("Reset FW CC!!\n");
		i32_ret = -1;
	}

exit:
	return i32_ret;
}

int raydium_save_cc_value(struct i2c_client *client)
{
	int i32_ret = SUCCESS;
	unsigned int u32_read_offset;
	unsigned short u16_read_length;

	u32_read_offset = 0;
	while (u32_read_offset < RAD_CC_3X_SIZE) {
		if ((u32_read_offset + MAX_READ_PACKET_SIZE) < RAD_CC_3X_SIZE)
			u16_read_length = MAX_READ_PACKET_SIZE;
		else
			u16_read_length = (unsigned short)(RAD_CC_3X_SIZE - u32_read_offset);

		i32_ret = handle_i2c_pda_read(client, (RAYDIUM_PDA_CCADDR + u32_read_offset), (g_rad_cc_image + u32_read_offset), u16_read_length);
		if (i32_ret < 0) {
			LOGE("Save FW CC failed!\n");
			goto exit;
		}

		u32_read_offset += (unsigned long)u16_read_length;
	}
	g_u8_cc_info = g_rad_cc_image[0x4E0];
	g_u8_cc_ready = true;
	LOGI("Save FW CC success!\n");

exit:
	return i32_ret;
}

int raydium_check_fw_ready(struct i2c_client *client)
{
	int i32_ret = SUCCESS;
	unsigned int u32_retry = 20;
	unsigned char u8_buf[4];

	u8_buf[1] = 0;
	while (u8_buf[1] != 0x40 && u32_retry != 0) {
		i32_ret = handle_i2c_pda_read(client, RAYDIUM_PDA_BOOTREG, u8_buf, 4);
		if (i32_ret < 0)
			goto exit;

		u32_retry--;
		usleep_range(4500, 5500);
	}

	if (u32_retry == 0) {
		LOGE("%s, FW not ready, retry error!\n", __func__);
		i32_ret = ERROR;
	} else {
		LOGI("%s, FW is ready!!\n", __func__);
		usleep_range(4500, 5500);
	}

exit:
	return i32_ret;
}
int raydium_load_test_fw(struct i2c_client *client)
{
	unsigned int u32_read_data = 0;
	unsigned int u32_write = 0;
	int i32_ret = ERROR;

	g_u8_i2c_retry_cnt = false;
	/* set skip load & MCU hold then SW reset*/
	/* sync_data:210;cmd_type:210h;ret_data:214h;test_mode:218h*/
	if (raydium_stop_mcu_rm6d031(1) == ERROR)
		goto ERROR_EXIT;

	/*WRT boot-loader to PRAM first*/
	u32_write = 0;
	if (handle_i2c_pda_write(client, RAYDIUM_PDA_PRAMLOCK, (unsigned char *)&u32_write, 4) == ERROR)
		return ERROR;


	/*Sending bootloader*/

	i32_ret = raydium_write_to_pram_rm6d031(client, 0x0000,
					   RAYDIUM_BOOTLOADER);
	if (i32_ret < 0)
		goto ERROR_EXIT;

	i32_ret = raydium_check_pram_crc_rm6d031(client, 0x000, 0x7FC);
	if (i32_ret < 0) {
		LOGE("check bootloader crc failed\n");
		goto ERROR_EXIT;
	}

	i32_ret = raydium_write_to_pram_rm6d031(client, RAYDIUM_PDA_FIRMWAREADDR,
					   RAYDIUM_TEST_FW);
	if (i32_ret < 0)
		goto ERROR_EXIT;

	i32_ret = raydium_check_pram_crc_rm6d031(client, RAYDIUM_PDA_FIRMWAREADDR,
					    RAYDIUM_PDA_CRCLENGTH);
	if (i32_ret < 0) {
		LOGE("check fw crc failed\n");
		goto ERROR_EXIT;
	}

	i32_ret = raydium_write_to_pram_rm6d031(client, 0x8F80, RAYDIUM_TB_EXE);
	if (i32_ret < 0)
		goto ERROR_EXIT;

	i32_ret = raydium_check_pram_crc_rm6d031(client, 0x8F80, 0x7C);
	if (i32_ret < 0) {
		LOGE("check init crc failed\n");
		goto ERROR_EXIT;
	}

	/*clear CC & BL info*/
	u32_write = 0x00000000;
	if (handle_i2c_pda_write(client, 0x8F78, (unsigned char *)&u32_write, 4) == ERROR)
		return ERROR;


	/* set skip load & MCU hold then SW reset*/
	/* sync_data:210;cmd_type:210h;ret_data:214h;test_mode:218h*/
	/* read bootloader version*/
	if (handle_i2c_pda_read(client, 0x80, (unsigned char *)(&u32_read_data), 4) == ERROR)
		goto ERROR_EXIT;

	LOGI("Read bootloader ver=0x%x!!\n", u32_read_data);

	u32_write = 0x0410;
	if (handle_i2c_pda_write(client, RAYDIUM_PDA_BOOTREG, (unsigned char *)&u32_write, 4) == ERROR)
		return ERROR;

	if (handle_i2c_pda_read(client, RAYDIUM_PDA_BOOTREG, (unsigned char *)(&u32_read_data), 4) == ERROR)
		goto ERROR_EXIT;

	LOGD("write MCU STATUS=0x%x!!\n", u32_read_data);
	/*wait sw rst finish*/
	u32_write = 1;

	handle_i2c_pda_write(client, RAYDIUM_PDA_BLKRST, (unsigned char *)&u32_write, 4);
	msleep(35);

	if (handle_i2c_pda_read(client, RAYDIUM_PDA_BOOTREG, (unsigned char *)(&u32_read_data), 4) == ERROR)
		goto ERROR_EXIT;

	LOGI("Read MCU STATUS=0x%x!!\n", u32_read_data);
	if (handle_i2c_pda_read(client, 0x200006E0, (unsigned char *)(&u32_read_data), 4) == ERROR)
		goto ERROR_EXIT;

	LOGI("Read test fw version=0x%x!!\n", u32_read_data);

	g_u8_i2c_retry_cnt = true;
	return SUCCESS;

ERROR_EXIT:

	g_u8_i2c_retry_cnt = true;
	return ERROR;
}

int raydium_load_fw(struct i2c_client *client, unsigned char u8_type)
{
	int i32_ret = SUCCESS;
	unsigned char u8_buf[4];

	g_u8_i2c_retry_cnt = false;
	if (raydium_stop_mcu_rm6d031(0) == ERROR)
		goto exit_upgrade;


	/*Set INT pin to High*/
	memset(u8_buf, 0, sizeof(u8_buf));
	u8_buf[0] = 0x04;
	u8_buf[1] = 0x04;
	i32_ret = handle_i2c_pda_write(client, RAYDIUM_PDA_MISCIER, u8_buf, 4);
	if (i32_ret < 0)
		goto exit_upgrade;

	i32_ret = handle_i2c_pda_read(client, RAYDIUM_PDA_BOOTREG, u8_buf, 4);
	if (i32_ret < 0)
		goto exit_upgrade;
	LOGI("50000918= 0x%x%x%x%x\n", u8_buf[0], u8_buf[1], u8_buf[2], u8_buf[3]);

	memset(u8_buf, 0, sizeof(u8_buf));
	LOGI("Raydium load fw\n");
	LOGD("type:%d\n", u8_type);

	i32_ret = handle_i2c_pda_write(client, RAYDIUM_PDA_PRAMLOCK, u8_buf, 4);
	if (i32_ret < 0)
		goto exit_upgrade;

	/* SPI flash protection register */
	memset(u8_buf, 0, sizeof(u8_buf));
	i32_ret = handle_i2c_pda_write(client, 0x50000524, u8_buf, 4);
	if (i32_ret < 0)
		goto exit_upgrade;

	/*Sending fw*/
	if (u8_type == RAYDIUM_FIRMWARE) {
		i32_ret = raydium_write_to_pram_rm6d031(client, 0x000, RAYDIUM_BOOTLOADER);
		if (i32_ret < 0)
			goto exit_upgrade;

		i32_ret = raydium_check_pram_crc_rm6d031(client, 0x000, 0x7FC);
		if (i32_ret < 0) {
			LOGE("check bootloader crc failed\n");
			goto exit_upgrade;
		}

		i32_ret = raydium_write_to_pram_rm6d031(client, 0x800, RAYDIUM_FIRMWARE);
		if (i32_ret < 0)
			goto exit_upgrade;

		i32_ret = raydium_write_to_pram_rm6d031(client, 0x7B00, RAYDIUM_PARA);
		if (i32_ret < 0)
			goto exit_upgrade;

		i32_ret = raydium_check_pram_crc_rm6d031(client, RAYDIUM_PDA_FIRMWAREADDR,
						    RAYDIUM_PDA_CRCLENGTH);
		if (i32_ret < 0) {
			LOGE("check fw crc failed\n");
			goto exit_upgrade;
		}

		i32_ret = raydium_write_to_pram_rm6d031(client, 0x8F80, RAYDIUM_TB_EXE);
		if (i32_ret < 0)
			goto exit_upgrade;
		i32_ret = raydium_check_pram_crc_rm6d031(client, 0x8F80, 0x7C);
		if (i32_ret < 0) {
			LOGE("check init crc failed\n");
			goto exit_upgrade;
		}

		if (g_u8_cc_ready) {
			/* Use CC image */
			LOGI("Use CC image\n");
			i32_ret = raydium_write_to_pram_rm6d031(client, 0x8A98, RAYDIUM_COMP);
			if (i32_ret < 0)
				goto exit_upgrade;

			i32_ret = raydium_check_pram_crc_rm6d031(client, 0x8A98, RAD_CC_3X_SIZE - 4);
			if (i32_ret < 0) {
				LOGE("check CC crc failed\n");
				goto exit_upgrade;
			}
		} else {
			/*Clear CC & BL INFO*/
			LOGI("Clear CC INFO\n");
			memset(u8_buf, 0, sizeof(u8_buf));
			i32_ret = handle_i2c_pda_write(client, 0x8F78, u8_buf, 4);
			if (i32_ret < 0)
				goto exit_upgrade;
		}

	} else if (u8_type == RAYDIUM_TEST_FW) {
		i32_ret = raydium_write_to_pram_rm6d031(client, 0x000, RAYDIUM_BOOTLOADER);
		if (i32_ret < 0)
			goto exit_upgrade;
		i32_ret = raydium_check_pram_crc_rm6d031(client, 0x000, 0x7FC);
		if (i32_ret < 0) {
			LOGE("check bootloader crc failed\n");
			goto exit_upgrade;
		}

		i32_ret = raydium_write_to_pram_rm6d031(client, 0x800, RAYDIUM_TEST_FW);
		if (i32_ret < 0)
			goto exit_upgrade;
		i32_ret = raydium_check_pram_crc_rm6d031(client, RAYDIUM_PDA_FIRMWAREADDR,
						    RAYDIUM_PDA_CRCLENGTH);
		if (i32_ret < 0) {
			LOGE("check test fw crc failed\n");
			goto exit_upgrade;
		}

		i32_ret = raydium_write_to_pram_rm6d031(client, 0x8F80, RAYDIUM_TB_EXE);
		if (i32_ret < 0)
			goto exit_upgrade;
		i32_ret = raydium_check_pram_crc_rm6d031(client, 0x8F80, 0x7C);
		if (i32_ret < 0) {
			LOGE("check init crc failed\n");
			goto exit_upgrade;
		}

		/*Clear CC & BL INFO*/
		memset(u8_buf, 0, sizeof(u8_buf));
		i32_ret = handle_i2c_pda_write(client, 0x8F78, u8_buf, 4);
		if (i32_ret < 0)
			goto exit_upgrade;

	}

	/*Skip load*/
	i32_ret = handle_i2c_pda_read(client, RAYDIUM_PDA_BOOTREG, u8_buf, 4);
	if (i32_ret < 0)
		goto exit_upgrade;

	if (u8_type == RAYDIUM_FIRMWARE) {
		u8_buf[0] = 0x50;
		u8_buf[1] = 0x00;
		u8_buf[2] = 0x00;
		u8_buf[3] = 0x00;
	} else if (u8_type == RAYDIUM_TEST_FW) {
		u8_buf[0] = 0x10;
		u8_buf[1] = 0x04;
		u8_buf[2] = 0x00;
		u8_buf[3] = 0x00;
	}

	i32_ret = handle_i2c_pda_write(client, RAYDIUM_PDA_BOOTREG, u8_buf, 4);
	if (i32_ret < 0)
		goto exit_upgrade;

	i32_ret = raydium_do_software_reset(client);
	if (i32_ret < 0)
		goto exit_upgrade;

	i32_ret = raydium_check_fw_ready(client);
	if (i32_ret < 0)
		goto exit_upgrade;

	i32_ret = raydium_check_cc_ready(client);
	if ((i32_ret == SUCCESS) && (g_u8_cc_ready == false)) {
		LOGI("Save cc value");
		raydium_save_cc_value(client);
	}

	i32_ret = handle_i2c_pda_read(client, 0x7F7C, u8_buf, 4);
	if (i32_ret < 0)
		goto exit_upgrade;
	LOGI("CC CRC is %2x.%2x.%2x.%2x", u8_buf[3], u8_buf[2], u8_buf[1], u8_buf[0]);

	i32_ret = handle_i2c_pda_read(client, RAYDIUM_PDA_FW_VERSION_ADDR, u8_buf, 4);
	if (i32_ret < 0)
		goto exit_upgrade;
	LOGI("fw update finish, version:%x.%x.%x.%x", u8_buf[3], u8_buf[2], u8_buf[1], u8_buf[0]);

exit_upgrade:
	g_u8_i2c_retry_cnt = true;
	return i32_ret;
}
