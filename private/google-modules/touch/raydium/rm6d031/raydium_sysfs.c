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

#include <linux/unistd.h>
#include <linux/uaccess.h>
#include <linux/fs.h>
#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/device.h>
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/input/mt.h>
#include <linux/interrupt.h>
#include <linux/gpio.h>
#include <linux/firmware.h>
#include "raydium_driver.h"
#include <wearable_touch_mcu.h>

#ifdef RAD_SELFTEST
#include "tpselftest.h"
#include "raydium_selftest.h"
#include "ic_drv_global.h"
#endif

static ssize_t raydium_touch_calibration_show(struct device *dev,
		struct device_attribute *attr,
		char *p_i8_buf)
{
	unsigned char u8_rbuffer[1];
	int i32_ret = -1;
	unsigned char u8_retry = 0;
	struct i2c_client *client = container_of(dev, struct i2c_client, dev);

	if (g_raydium_ts->is_suspend)
		LOGD("RAD is_suspend at %s\n", __func__);

	g_u8_raydium_flag |= ENG_MODE;

	mutex_lock(&g_raydium_ts->lock);

	i32_ret = raydium_i2c_pda2_set_page(client,
					    g_raydium_ts->is_suspend,
					    RAYDIUM_PDA2_PAGE_0);
	if (i32_ret < 0)
		goto exit_i2c_error;

	u8_rbuffer[0] = RAYDIUM_HOST_CMD_CALIBRATION;
	i32_ret = raydium_i2c_pda2_write(client, RAYDIUM_PDA2_HOST_CMD_ADDR,
					 u8_rbuffer, 1);
	if (i32_ret < 0)
		goto exit_i2c_error;

	do {
		if (u8_rbuffer[0] == RAYDIUM_HOST_CMD_NO_OP)
			break;

		msleep(1000);

		i32_ret = raydium_i2c_pda2_read(client,
						RAYDIUM_PDA2_HOST_CMD_ADDR,
						u8_rbuffer, 1);
		if (i32_ret < 0)
			goto exit_i2c_error;

		LOGI("RAD %s return 0x%02x!!\n", __func__, u8_rbuffer[0]);
	} while (u8_retry++ < (SYN_I2C_RETRY_TIMES * 2));

	i32_ret = sizeof(u8_rbuffer);
	memcpy(p_i8_buf, u8_rbuffer, i32_ret);

exit_i2c_error:
	mutex_unlock(&g_raydium_ts->lock);
	g_u8_raydium_flag &= ~ENG_MODE;
	return i32_ret;
}

static ssize_t raydium_i2c_pda_access_show(struct device *dev,
		struct device_attribute *attr,
		char *p_i8_buf)
{
	unsigned char u8_rbuffer[4];
	int i32_ret = -1;
	struct i2c_client *client = container_of(dev, struct i2c_client, dev);

	if (g_u32_length > 4)
		return -EINVAL;
	memset(u8_rbuffer, 0x00, 4);
	mutex_lock(&g_raydium_ts->lock);
	i32_ret = raydium_i2c_pda_read(client,
				       g_u32_addr,
				       u8_rbuffer,
				       g_u32_length);
	mutex_unlock(&g_raydium_ts->lock);
	if (i32_ret < 0)
		return i32_ret;

	return scnprintf(p_i8_buf, PAGE_SIZE, "0x%08X : 0x%02X%02X%02X%02X\n",
			 (unsigned int)g_u32_addr, u8_rbuffer[3], u8_rbuffer[2], u8_rbuffer[1],
			 u8_rbuffer[0]);
}


static ssize_t raydium_i2c_pda_access_via_pda2_show(struct device *dev,
		struct device_attribute *attr,
		char *p_i8_buf)
{
	unsigned char u8_rbuffer[4];
	int i32_ret = -1;
	struct i2c_client *client = container_of(dev, struct i2c_client, dev);

	if (g_u32_length > 4)
		return -EINVAL;
	memset(u8_rbuffer, 0x00, 4);
	mutex_lock(&g_raydium_ts->lock);
	i32_ret = handle_i2c_pda_read(client,
				      g_u32_addr,
				      u8_rbuffer,
				      g_u32_length);
	mutex_unlock(&g_raydium_ts->lock);
	if (i32_ret < 0)
		return i32_ret;

	return scnprintf(p_i8_buf, PAGE_SIZE, "0x%08X : 0x%02X%02X%02X%02X\n",
			 (unsigned int)g_u32_addr, u8_rbuffer[3], u8_rbuffer[2], u8_rbuffer[1],
			 u8_rbuffer[0]);
}

static ssize_t raydium_check_i2c_show_with_size(struct device *dev, struct device_attribute *attr,
						char *p_i8_buf, ssize_t size)
{
	unsigned char u8_rbuffer[4] = { 0 };
	int i32_ret = -1;

	if (g_raydium_ts->is_suspend)
		LOGD("RAD is_suspend at %s\n", __func__);

	mutex_lock(&g_raydium_ts->lock);
	i32_ret = handle_i2c_pda_read(g_raydium_ts->client,
				      RAYDIUM_CHK_I2C_CMD, u8_rbuffer, 4);
	if (i32_ret < 0)
		goto exit_i2c_error;

	i32_ret = scnprintf(p_i8_buf, size, "RAD Touch check i2c: %02X%02X\n", u8_rbuffer[3],
			    u8_rbuffer[2]);

exit_i2c_error:
	mutex_unlock(&g_raydium_ts->lock);
	return i32_ret;
}

static ssize_t raydium_check_i2c_show(struct device *dev, struct device_attribute *attr,
				      char *p_i8_buf)
{
	return raydium_check_i2c_show_with_size(dev, attr, p_i8_buf, PAGE_SIZE);
}

static ssize_t raydium_hw_reset_show(struct device *dev, struct device_attribute *attr,
				     char *p_i8_buf)
{
	struct i2c_client *client = container_of(dev, struct i2c_client, dev);
	int i32_ret = SUCCESS;

	i32_ret = raydium_hw_reset_helper_func(client);

	i32_ret = scnprintf(p_i8_buf, PAGE_SIZE, "Raydium HW Reset : %d\n", i32_ret);
	LOGI("%s\n", p_i8_buf);

	return i32_ret;
}

static ssize_t raydium_palm_status_show(struct device *dev,
					struct device_attribute *attr,
					char *p_i8_buf)
{
	struct raydium_report_touch_data report_touch_data;

	mutex_lock(&g_raydium_ts->lock);
	if (g_raydium_ts->use_wrapped_i2c)
		raydium_read_touchdata_wrapping_locked(&report_touch_data);
	else
		raydium_read_touchdata_locked(&report_touch_data);
	mutex_unlock(&g_raydium_ts->lock);
	return scnprintf(p_i8_buf, PAGE_SIZE, "[touch] palm_status : %d\n",
			 report_touch_data.touch_points.u8_gesture_state);
}
static ssize_t raydium_i2c_touch_lock_store(struct device *dev,
					struct device_attribute *attr,
					const char *p_i8_buf, size_t count)
{
	int i32_ret = 0;
	unsigned char u8_mode;

	if (g_raydium_ts->is_suspend)
		LOGD("RAD is_suspend at %s\n", __func__);

	/* receive command line arguments string */
	if (count > 2)
		return -EINVAL;

	i32_ret = kstrtou8(p_i8_buf, 16, &u8_mode);
	if (i32_ret < 0)
		return i32_ret;

	switch (u8_mode) {
	case 0: /* Disable Touch lock */
		// Exit STB first to avoid additional interrupt
		raydium_stb_control(DISABLE, STB_TOUCH_LOCK);
		raydium_irq_control(ENABLE, IRQ_TOUCH_LOCK);
		// Sync the touch lock information to MCU
		if (g_raydium_ts->offloading)
			wti_synchronizer_set_touch_lock(DISABLE);
		break;

	case 1: /* Enable Touch lock */
		// Disable IRQ first to avoid I2C ops error
		raydium_irq_control(DISABLE, IRQ_TOUCH_LOCK);
		raydium_stb_control(ENABLE, STB_TOUCH_LOCK);
		// Sync the touch lock information to MCU
		if (g_raydium_ts->offloading)
			wti_synchronizer_set_touch_lock(ENABLE);
		break;
	}

	return count;
}
static ssize_t raydium_i2c_touch_sleep_store(struct device *dev, struct device_attribute *attr,
					     const char *p_i8_buf, size_t count)
{
	int i32_ret = 0;
	bool enable;

	i32_ret = kstrtobool(p_i8_buf, &enable);
	if (i32_ret) {
		LOGE("kstrtobool failed, rc = %d\n", i32_ret);
		return -EINVAL;
	}

	raydium_stb_control(enable, STB_CMD);

	return count;
}

static ssize_t raydium_check_driver_version_show_with_size(struct device *dev,
							   struct device_attribute *attr,
							   char *p_i8_buf, ssize_t size)
{
	return scnprintf(p_i8_buf, size, "RAD Driver Ver: 0x%X\n", g_u32_driver_version);
}

static ssize_t raydium_check_driver_version_show(struct device *dev,
		struct device_attribute *attr,
		char *p_i8_buf)
{
	return raydium_check_driver_version_show_with_size(dev, attr, p_i8_buf, PAGE_SIZE);
}

static ssize_t raydium_check_fw_version_show_with_size(struct device *dev,
						       struct device_attribute *attr,
						       char *p_i8_buf, ssize_t size)
{
	unsigned char u8_rbuffer[4] = { 0 };
	int i32_ret = -1;
	struct i2c_client *client = container_of(dev, struct i2c_client, dev);

	unsigned int fw_version, image_version;

	if (g_raydium_ts->is_suspend)
		LOGD("RAD is_suspend at %s\n", __func__);

	g_u8_raydium_flag |= ENG_MODE;
	mutex_lock(&g_raydium_ts->lock);

	i32_ret = raydium_i2c_pda2_set_page(client,
					    g_raydium_ts->is_suspend,
					    RAYDIUM_PDA2_PAGE_0);
	if (i32_ret < 0)
		goto exit_i2c_error;
	i32_ret = raydium_i2c_pda2_read(client, RAYDIUM_PDA2_FW_VERSION_ADDR,
					u8_rbuffer, 4);
	if (i32_ret < 0)
		goto exit_i2c_error;
	i32_ret = scnprintf(p_i8_buf, size, "RAD Touch FW Ver : %02X%02X%02X%02X\n", u8_rbuffer[0],
			    u8_rbuffer[1], u8_rbuffer[2], u8_rbuffer[3]);

	fw_version = (u8_rbuffer[0] << 24)
		     | (u8_rbuffer[1] << 16)
		     | (u8_rbuffer[2] << 8)
		     | u8_rbuffer[3];
	LOGI("RAD FW ver : 0x%x\n", fw_version);

	image_version = (g_rad_para_image[PARA_FW_VERSION_OFFSET] << 24) |
			(g_rad_para_image[PARA_FW_VERSION_OFFSET + 1] << 16) |
			(g_rad_para_image[PARA_FW_VERSION_OFFSET + 2] << 8) |
			g_rad_para_image[PARA_FW_VERSION_OFFSET + 3];

	LOGI("RAD Image FW ver : 0x%x\n", image_version);

	if (fw_version != image_version)
		LOGI("%s, FW need upgrade.\n", __func__);

exit_i2c_error:
	mutex_unlock(&g_raydium_ts->lock);
	g_u8_raydium_flag &= ~ENG_MODE;
	return i32_ret;
}

static ssize_t raydium_check_fw_version_show(struct device *dev, struct device_attribute *attr,
					     char *p_i8_buf)
{
	return raydium_check_fw_version_show_with_size(dev, attr, p_i8_buf, PAGE_SIZE);
}

static ssize_t raydium_check_panel_version_show_with_size(struct device *dev,
							  struct device_attribute *attr,
							  char *p_i8_buf, ssize_t size)
{
	unsigned char u8_rbuffer[RAYDIUM_PDA2_DISPLAY_INFO_ADDR_DATA_SIZE] = { 0 };
	int i32_ret = -1;
	struct i2c_client *client = container_of(dev, struct i2c_client, dev);

	if (g_raydium_ts->is_suspend)
		LOGD("RAD is_suspend at %s\n", __func__);
	g_u8_raydium_flag |= ENG_MODE;
	mutex_lock(&g_raydium_ts->lock);

	i32_ret = raydium_i2c_pda2_set_page(client,
					    g_raydium_ts->is_suspend,
					    RAYDIUM_PDA2_PAGE_0);
	if (i32_ret < 0)
		goto exit_i2c_error;

	i32_ret = raydium_i2c_pda2_read(client, RAYDIUM_PDA2_DISPLAY_INFO_ADDR, u8_rbuffer,
					RAYDIUM_PDA2_DISPLAY_INFO_ADDR_DATA_SIZE);
	if (i32_ret < 0)
		goto exit_i2c_error;

	i32_ret = scnprintf(p_i8_buf, size,
											"RAD Touch Panel Resolution : %02X%02X%02X%02X\n",
										 	u8_rbuffer[3], u8_rbuffer[2],
											u8_rbuffer[1], u8_rbuffer[0]);

exit_i2c_error:
	mutex_unlock(&g_raydium_ts->lock);
	g_u8_raydium_flag &= ~ENG_MODE;
	return i32_ret;
}

static ssize_t raydium_check_panel_version_show(struct device *dev, struct device_attribute *attr,
						char *p_i8_buf)
{
	return raydium_check_panel_version_show_with_size(dev, attr, p_i8_buf, PAGE_SIZE);
}

static ssize_t raydium_fw_upgrade_store(struct device *dev,
					struct device_attribute *attr,
					const char *p_i8_buf, size_t count)
{
	int i32_ret = 0;

	/* receive command line arguments string */
	if (count > 2)
		return -EINVAL;

	i32_ret = kstrtou8(p_i8_buf, 16, &g_u8_upgrade_type);
	if (i32_ret < 0)
		return i32_ret;

	return count;
}

static ssize_t raydium_fw_upgrade_show(struct device *dev,
				       struct device_attribute *attr,
				       char *p_i8_buf)
{
	int i32_ret = 0, i32_result = FAIL;
	struct i2c_client *client = container_of(dev, struct i2c_client, dev);

	LOGI("RAD burn type is %d\n", g_u8_upgrade_type);

	g_u8_raydium_flag |= ENG_MODE;

	if ((g_u8_table_setting == 1) && (g_u8_table_init == 1))
		raydium_mem_table_setting();

	if (g_u8_upgrade_type == 1) {
		i32_ret = raydium_burn_fw(client);
		if (i32_ret < 0)
			goto exit_upgrade;
		i32_result = SUCCESS;
	} else if (g_u8_upgrade_type == 2) {
		i32_ret = raydium_burn_comp(client);
		if (i32_ret < 0)
			goto exit_upgrade;
		i32_result = SUCCESS;
	}
#ifdef RAD_SELFTEST
	else if (g_u8_upgrade_type == 4) {
		i32_ret = raydium_load_test_fw(client);
		if (i32_ret < 0)
			goto exit_upgrade;

		i32_result = SUCCESS;
	}
#endif

exit_upgrade:
	LOGD("g_u8_raydium_flag : %d", g_u8_raydium_flag);
	g_u8_raydium_flag &= ~ENG_MODE;
	g_u8_upgrade_type = 0;

	return scnprintf(p_i8_buf, PAGE_SIZE, "FW Upgrade result : %d\n", i32_result);
}
static ssize_t raydium_i2c_pda2_page_store(struct device *dev,
		struct device_attribute *attr,
		const char *p_i8_buf, size_t count)
{
	int i32_ret = 0;
	unsigned char u8_page = 0;
	char *temp_buf, *token, *free_temp_buf, *free_token;
	const char *delim = " ,";
	struct i2c_client *client = container_of(dev, struct i2c_client, dev);

	/* receive command line arguments string */
	if (count < 2)
		return -EINVAL;
	temp_buf = kzalloc(count + 1, GFP_KERNEL);
	if (temp_buf == NULL)
		return -ENOMEM;

	token = kzalloc(count + 1, GFP_KERNEL);
	if (token == NULL) {
		kfree(temp_buf);
		return -ENOMEM;
	}

	free_temp_buf = temp_buf;
	free_token = token;

	strscpy(temp_buf, p_i8_buf, count);

	token = strsep(&temp_buf, delim);

	if (temp_buf) {
		LOGE("input error, extra auguments!n");
		i32_ret = -EINVAL;
		goto exit_error;
	}
	i32_ret = kstrtou8(token, 16, &u8_page);

	if (i32_ret < 0)
		goto exit_error;

	mutex_lock(&g_raydium_ts->lock);

	i32_ret = raydium_i2c_pda2_set_page(client, g_raydium_ts->is_suspend, u8_page);
	if (i32_ret < 0)
		goto exit_set_error;

	/* TODO: Page check, Due to ISR will change page back to Page_0.
	 *  Or disable IRQ during PDA2 access period
	 */

exit_set_error:
	mutex_unlock(&g_raydium_ts->lock);

exit_error:
	kfree(free_token);
	kfree(free_temp_buf);
	return count;
}

static ssize_t raydium_i2c_raw_data_store(struct device *dev,
		struct device_attribute *attr,
		const char *p_i8_buf, size_t count)
{
	int i32_ret = 0;
	char *temp_buf, *token, *free_temp_buf, *free_token;
	const char *delim = " ,";
	unsigned char u8_w_data[RAD_FT_CMD_LENGTH];

	struct i2c_client *client = container_of(dev, struct i2c_client, dev);

	/* receive command line arguments string */
	if (count < 2)
		return -EINVAL;
	temp_buf = kzalloc(count + 1, GFP_KERNEL);
	if (temp_buf  == NULL) {
		LOGE("kzalloc temp_buf failed\n");
		return -ENOMEM;
	}

	token = kzalloc(count + 1, GFP_KERNEL);
	if (token  == NULL) {
		kfree(temp_buf);
		return -ENOMEM;
	}

	free_temp_buf = temp_buf;
	free_token = token;

	strscpy(temp_buf, p_i8_buf, count);

	token = strsep(&temp_buf, delim);

	i32_ret = kstrtou8(token, 16, &g_u8_raw_data_type);

	token = strsep(&temp_buf, delim);
	if (token) {
		i32_ret = kstrtouint(token, 16, &g_u32_raw_data_len);
		if (i32_ret < 0)
			goto exit_error;


	} else { /* without length info*/
		i32_ret = -EINVAL;
		goto exit_error;
	}

	token = strsep(&temp_buf, delim);
	if (token) {
		i32_ret = kstrtou8(token, 16, &g_u8_raw_data_pure);
		if (i32_ret < 0)
			goto exit_error;
	} else { /* without pure info, ,maybe old format */
		g_u8_raw_data_pure = 0;
	}

	if (temp_buf) { /* too much arguments*/
		i32_ret = -E2BIG;
		goto exit_error;
	}

	memset(u8_w_data, 0x00, RAD_FT_CMD_LENGTH);

	mutex_lock(&g_raydium_ts->lock);
	i32_ret = raydium_i2c_pda2_set_page(client,
					    g_raydium_ts->is_suspend,
					    RAYDIUM_PDA2_PAGE_0);
	if (i32_ret < 0) {
		mutex_unlock(&g_raydium_ts->lock);
		goto exit_error;
	}

	g_u8_resetflag = true;

	u8_w_data[RAD_HOST_CMD_POS] = RAYDIUM_HOST_CMD_NO_OP;
	u8_w_data[RAD_FT_CMD_POS] = g_u8_raw_data_type;
	u8_w_data[RAD_FT_STATUS0_POS] = g_u8_raw_data_pure;

	i32_ret = raydium_i2c_pda2_write(client, RAYDIUM_PDA2_HOST_CMD_ADDR,
					 u8_w_data, RAD_FT_CMD_LENGTH);
	mutex_unlock(&g_raydium_ts->lock);
	if (i32_ret < 0)
		goto exit_error;

	if (g_u8_raw_data_type == 0) {
		msleep(20);
		g_u8_resetflag = false;
	}
exit_error:
	kfree(free_token);
	kfree(free_temp_buf);

	return count;
}

static ssize_t raydium_i2c_raw_data_show(struct device *dev,
		struct device_attribute *attr,
		char *p_i8_buf)
{
	unsigned char u8_rbuffer[MAX_READ_PACKET_SIZE];
	unsigned int u32_target_addr;
	unsigned int u32_offset;
	unsigned short u16_read_length;

	int i32_ret = -1;
	int i32_retry = 0;
	struct i2c_client *client = container_of(dev, struct i2c_client, dev);
	struct raydium_ts_data *ts =
		(struct raydium_ts_data *)i2c_get_clientdata(client);
	unsigned char u8_retry_limit = (ts->is_suspend) ? 100 : 30;

	memset(u8_rbuffer, 0x00, MAX_READ_PACKET_SIZE);

	/* make sure update flag was set*/
	for (i32_retry = 0; i32_retry < u8_retry_limit; i32_retry++) {
		mutex_lock(&ts->lock);
		i32_ret = raydium_i2c_pda2_set_page(client,
						    ts->is_suspend,
						    RAYDIUM_PDA2_PAGE_0);
		if (i32_ret < 0)
			goto exit_i2c_error;

		i32_ret = raydium_i2c_pda2_read(client,
						RAYDIUM_PDA2_HOST_CMD_ADDR,
						u8_rbuffer,
						RAD_FT_CMD_LENGTH);
		mutex_unlock(&ts->lock);
		if (i32_ret < 0)
			goto exit_flag_error;

		if ((u8_rbuffer[RAD_FT_CMD_POS] & RAYDIUM_FT_UPDATE) ==
		    RAYDIUM_FT_UPDATE)
			break;

		usleep_range(500, 1500);
	}

	if (i32_retry == u8_retry_limit) {
		i32_ret = -EAGAIN;
		goto exit_flag_error;
	}

	u32_offset = 0;
	u16_read_length = 0;
	while (u32_offset < g_u32_raw_data_len) {
		if ((u32_offset + MAX_READ_PACKET_SIZE) <
		    g_u32_raw_data_len)
			u16_read_length = MAX_READ_PACKET_SIZE;
		else
			u16_read_length =
				(unsigned short)(g_u32_raw_data_len - u32_offset);

		u32_target_addr = RAD_READ_FT_DATA_CMD + u32_offset;

		mutex_lock(&(ts->lock));

		/*using byte mode to read 4 bytes*/
		i32_ret = handle_i2c_pda_read(client,
					      u32_target_addr,
					      u8_rbuffer,
					      u16_read_length);

		mutex_unlock(&(ts->lock));
		if (i32_ret < 0)
			goto exit_flag_error;

		if (u32_offset + u16_read_length > PAGE_SIZE) {
			LOGE("%s: output buffer is full\n", __func__);
			i32_ret = -ENOMEM;
			goto exit_flag_error;
		}
		memcpy((p_i8_buf + u32_offset), u8_rbuffer, u16_read_length);

		u32_offset += u16_read_length;
	}

	/* clear update flag to get next one*/
	u8_rbuffer[RAD_HOST_CMD_POS] = RAYDIUM_HOST_CMD_NO_OP;
	u8_rbuffer[RAD_FT_CMD_POS] = g_u8_raw_data_type;
	u8_rbuffer[RAD_FT_CMD_POS + 1] = 0;
	u8_rbuffer[RAD_FT_STATUS0_POS] = g_u8_raw_data_pure;
	mutex_lock(&ts->lock);
	i32_ret = raydium_i2c_pda2_write(client, RAYDIUM_PDA2_HOST_CMD_ADDR,
					 u8_rbuffer, RAD_FT_CMD_LENGTH);
	mutex_unlock(&ts->lock);
	if (i32_ret < 0)
		goto exit_flag_error;

	return g_u32_raw_data_len;
exit_i2c_error:
	mutex_unlock(&(ts->lock));
exit_flag_error:
	return i32_ret;
}

static ssize_t raydium_i2c_pda_access_store(struct device *dev,
		struct device_attribute *attr,
		const char *p_i8_buf, size_t count)
{
	int i32_ret = 0;
	char *temp_buf, *token, *free_temp_buf, *free_token;
	const char *delim = " ,";
	unsigned char u8_w_data[MAX_WRITE_PACKET_SIZE];
	unsigned int u32_data_count = 0;
	unsigned int u32_data_index = 0;
	struct i2c_client *client = container_of(dev, struct i2c_client, dev);

	/* receive command line arguments string */
	if (count < 2)
		return -EINVAL;

	temp_buf = kzalloc(count + 1, GFP_KERNEL);
	if (temp_buf == NULL)
		return -ENOMEM;

	token = kzalloc(count + 1, GFP_KERNEL);
	if (token == NULL) {
		kfree(temp_buf);
		return -ENOMEM;
	}

	free_temp_buf = temp_buf;
	free_token = token;

	strscpy(temp_buf, p_i8_buf, count);

	token = strsep(&temp_buf, delim);

	i32_ret = kstrtoul(token, 16, &g_u32_addr);

	token = strsep(&temp_buf, delim);
	if (token)
		i32_ret = kstrtouint(token, 16, &u32_data_count);
	else
		goto exit_error;
	if (g_u32_length > MAX_WRITE_PACKET_SIZE)
		return -EINVAL;
	g_u32_length = u32_data_count;

	memset(u8_w_data, 0x00, MAX_WRITE_PACKET_SIZE);

	if (temp_buf && u32_data_count) {
		u32_data_index = 0;
		while (u32_data_count) {
			token = strsep(&temp_buf, delim);
			i32_ret = kstrtou8(token, 16,
					   &u8_w_data[u32_data_index++]);
			if (i32_ret < 0)
				goto exit_error;
			u32_data_count--;
		}
		mutex_lock(&g_raydium_ts->lock);
		i32_ret = raydium_i2c_pda_write(client, g_u32_addr,
						u8_w_data, g_u32_length);
		mutex_unlock(&g_raydium_ts->lock);
	}

exit_error:
	kfree(free_token);
	kfree(free_temp_buf);
	return count;
}
static ssize_t raydium_i2c_pda_access_via_pda2_store(struct device *dev,
		struct device_attribute *attr,
		const char *p_i8_buf, size_t count)
{
	int i32_ret = 0;
	char *temp_buf, *token, *free_temp_buf, *free_token;
	const char *delim = " ,";
	unsigned char u8_w_data[MAX_WRITE_PACKET_SIZE];
	unsigned int u32_data_count = 0;
	unsigned int u32_data_index = 0;
	struct i2c_client *client = container_of(dev, struct i2c_client, dev);

	/* receive command line arguments string */
	if (count < 2)
		return -EINVAL;

	temp_buf = kzalloc(count + 1, GFP_KERNEL);
	if (temp_buf == NULL)
		return -ENOMEM;

	token = kzalloc(count + 1, GFP_KERNEL);
	if (token == NULL) {
		kfree(temp_buf);
		return -ENOMEM;
	}

	free_temp_buf = temp_buf;
	free_token = token;

	strscpy(temp_buf, p_i8_buf, count);

	token = strsep(&temp_buf, delim);

	i32_ret = kstrtoul(token, 16, &g_u32_addr);

	token = strsep(&temp_buf, delim);
	if (token)
		i32_ret = kstrtouint(token, 16, &u32_data_count);
	else
		goto exit_error;
	if (g_u32_length > MAX_WRITE_PACKET_SIZE)
		return -EINVAL;
	g_u32_length = u32_data_count;

	memset(u8_w_data, 0x00, MAX_WRITE_PACKET_SIZE);

	if (temp_buf && u32_data_count) {
		u32_data_index = 0;
		while (u32_data_count) {
			token = strsep(&temp_buf, delim);
			i32_ret = kstrtou8(token, 16,
					   &u8_w_data[u32_data_index++]);
			if (i32_ret < 0)
				goto exit_error;
			u32_data_count--;
		}
		mutex_lock(&g_raydium_ts->lock);
		i32_ret = handle_i2c_pda_write(client, g_u32_addr,
					       u8_w_data, g_u32_length);
		mutex_unlock(&g_raydium_ts->lock);
	}

exit_error:
	kfree(free_token);
	kfree(free_temp_buf);
	return count;
}

static ssize_t raydium_i2c_pda2_mode_store(struct device *dev,
		struct device_attribute *attr,
		const char *p_i8_buf, size_t count)
{
	int i32_ret = 0;
	unsigned char u8_mode;

	struct i2c_client *client = container_of(dev, struct i2c_client, dev);

	if (g_raydium_ts->is_suspend)
		LOGD("RAD is_suspend at %s\n", __func__);

	/* receive command line arguments string */
	if (count > 2)
		return -EINVAL;

	i32_ret = kstrtou8(p_i8_buf, 16, &u8_mode);
	if (i32_ret < 0)
		return i32_ret;
	i32_ret = raydium_i2c_mode_control(client, u8_mode);
	if (i32_ret < 0)
		return i32_ret;

	return count;
}

static ssize_t raydium_i2c_pda2_access_show(struct device *dev,
		struct device_attribute *attr,
		char *p_i8_buf)
{
	unsigned char u8_rbuffer[4];
	int i32_ret = -1;
	struct i2c_client *client = container_of(dev, struct i2c_client, dev);

	if (g_u32_length > 4)
		return -EINVAL;
	memset(u8_rbuffer, 0x00, 4);

	mutex_lock(&g_raydium_ts->lock);
	i32_ret = raydium_i2c_pda2_read(client, g_u8_addr,
					u8_rbuffer, g_u32_length);
	mutex_unlock(&g_raydium_ts->lock);
	if (i32_ret < 0)
		return i32_ret;

	return scnprintf(p_i8_buf, PAGE_SIZE, "0x%04X : 0x%02X%02X%02X%02X\n", g_u8_addr,
			 u8_rbuffer[3], u8_rbuffer[2], u8_rbuffer[1], u8_rbuffer[0]);
}

static ssize_t raydium_i2c_pda2_access_store(struct device *dev,
		struct device_attribute *attr,
		const char *p_i8_buf, size_t count)
{
	int i32_ret = 0;
	char *temp_buf, *token, *free_temp_buf, *free_token;
	const char *delim = " ,";
	unsigned char u8_w_data[MAX_WRITE_PACKET_SIZE];
	unsigned int u32_data_count = 0;
	unsigned int u32_data_index = 0;
	struct i2c_client *client = container_of(dev, struct i2c_client, dev);

	/* receive command line arguments string */
	if (count < 2)
		return -EINVAL;
	temp_buf = kzalloc(count + 1, GFP_KERNEL);
	if (temp_buf  == NULL) {
		LOGE("kzalloc temp_buf failed\n");
		return -ENOMEM;
	}

	token = kzalloc(count + 1, GFP_KERNEL);
	if (token  == NULL) {
		kfree(temp_buf);
		return -ENOMEM;
	}

	free_temp_buf = temp_buf;
	free_token = token;

	strscpy(temp_buf, p_i8_buf, count);

	token = strsep(&temp_buf, delim);

	i32_ret = kstrtou8(token, 16, &g_u8_addr);

	token = strsep(&temp_buf, delim);
	if (token)
		i32_ret = kstrtouint(token, 16, &u32_data_count);
	else {
		i32_ret = -EINVAL;
		goto exit_error;
	}

	if (u32_data_count > MAX_WRITE_PACKET_SIZE) {
		i32_ret = -EINVAL;
		goto exit_error;
	}

	memset(u8_w_data, 0x00, MAX_WRITE_PACKET_SIZE);

	g_u32_length = u32_data_count;

	if (temp_buf && u32_data_count) {
		u32_data_index = 0;
		while (u32_data_count) {
			token = strsep(&temp_buf, delim);
			i32_ret = kstrtou8(token, 16,
					   &u8_w_data[u32_data_index++]);
			if (i32_ret < 0)
				goto exit_error;
			u32_data_count--;
		}

		mutex_lock(&g_raydium_ts->lock);
		i32_ret = raydium_i2c_pda2_write(client, g_u8_addr,
						 u8_w_data, g_u32_length);
		mutex_unlock(&g_raydium_ts->lock);
		if (i32_ret < 0)
			goto exit_error;
	}

exit_error:
	kfree(free_token);
	kfree(free_temp_buf);
	return count;
}

static ssize_t raydium_receive_fw_control_store(struct device *dev,
					struct device_attribute *attr,
					const char *p_i8_buf, size_t count)
{
	int i32_ret = 0;
	const char *delim = " ,";
	char *token, *temp_buf, *free_token, *free_temp_buf;
	static unsigned char *p_u8_firmware_data;

	unsigned char u8_cmd;
	unsigned long u32_len;
	static unsigned char u8_type;
	static unsigned int u32_index;

	if (count == 20) { /*check FW type*/
		temp_buf = kzalloc(32, GFP_KERNEL);
		if (temp_buf  == NULL) {
			LOGE("kzalloc temp_buf failed\n");
			return -ENOMEM;
		}

		token = kzalloc(32, GFP_KERNEL);
		if (token == NULL) {
			kfree(temp_buf);
			return -ENOMEM;
		}

		free_token = token;
		free_temp_buf = temp_buf;

		snprintf(temp_buf, PAGE_SIZE, "%s", p_i8_buf);
		token = strsep(&temp_buf, delim);
		i32_ret = kstrtou8(token, 16, &u8_cmd);
		if (i32_ret < 0) {
			LOGE("kstrtou8 failed\n");
			kfree(free_token);
			kfree(free_temp_buf);
		}

		token = strsep(&temp_buf, delim);
		i32_ret = kstrtou8(token, 16, &u8_type);
		if (i32_ret < 0) {
			LOGE("kstrtou8 failed\n");
			kfree(temp_buf);
			kfree(token);
		}

		token = strsep(&temp_buf, delim);
		i32_ret = kstrtoul(token, 16, &u32_len);
		if (i32_ret < 0) {
			LOGE("kstrtou8 failed\n");
			kfree(temp_buf);
			kfree(token);
		}

		LOGI("uc_cmd=0x%x, uc_type=0x%x, u16_len=0x%x\n", u8_cmd, u8_type,
		     (unsigned int)u32_len);

		if (u8_cmd == RAD_CMD_UPDATE_BIN) { /*check FW length*/
			u32_index = 0;
			if (u8_type == RAYDIUM_BOOTLOADER) {
				memset(g_rad_boot_image, 0, u32_len);
				p_u8_firmware_data = g_rad_boot_image;
			} else if (u8_type == RAYDIUM_INIT) {
				memset(g_rad_init_image, 0, u32_len);
				p_u8_firmware_data = g_rad_init_image;
			} else if (u8_type == RAYDIUM_TB_EXE) {
				memset(g_rad_tb_exe_image, 0, u32_len);
				p_u8_firmware_data = g_rad_tb_exe_image;
			} else if (u8_type == RAYDIUM_PARA) {
				memset(g_rad_para_image, 0, u32_len);
				p_u8_firmware_data = g_rad_para_image;
			} else if (u8_type == RAYDIUM_FIRMWARE) {
				memset(g_rad_fw_image, 0, u32_len);
				p_u8_firmware_data = g_rad_fw_image;
			} else if (u8_type == RAYDIUM_TEST_PARA) {
				memset(g_rad_testpara_image, 0, u32_len);
				p_u8_firmware_data = g_rad_testpara_image;
			} else if (u8_type == RAYDIUM_TEST_FW) {
				memset(g_rad_testfw_image, 0, u32_len);
				p_u8_firmware_data = g_rad_testfw_image;
			}

		} else if (u8_cmd == RAD_CMD_UPDATE_END) { /*set buffer finish*/
			if (u8_type == RAYDIUM_TEST_FW) {
				memcpy((g_rad_testfw_image + RAD_FW_3X_SIZE),
				       g_rad_testpara_image, RAD_PARA_3X_SIZE + 4);
			}

			u32_index = 0;
			g_u8_table_setting = 0;

		} else if (u8_cmd == RAD_CMD_BURN_FINISH) { /*free buffer*/
			u8_type = 0;
			u32_index = 0;
			g_u8_table_setting = 1;
		}

		kfree(free_temp_buf);
		kfree(free_token);
	} else  if (count > 10) {	/*start copy FW to array*/
		memcpy((p_u8_firmware_data + u32_index), p_i8_buf, count);
		u32_index += count;
	} else
		LOGE("other case, count=%zu\n", count);

	return count;
}

static ssize_t raydium_log_level_store(struct device *dev,
				       struct device_attribute *attr,
				       const char *p_i8_buf, size_t count)
{
	int i32_ret = 0;
	unsigned char u8_level = 0;

	/* receive command line arguments string */
	if (count > 2)
		return -EINVAL;
	i32_ret = kstrtou8(p_i8_buf, 16, &u8_level);
	if (i32_ret < 0)
		return i32_ret;
	g_u8_log_level = u8_level;
	LOGE("g_u8_log_level = %d\n", g_u8_log_level);
	return count;
}

static ssize_t raydium_request_firmware_show(struct device *dev,
				      struct device_attribute *attr,
				      char *p_i8_buf)
{
	int i32_ret;
	const struct firmware *fw = NULL;

	if (strlen(g_raydium_ts->customized_fw_name) == 0) {
		if (!g_raydium_ts->fw_name_dt) {
			LOGE("No firmware path\n");
			return -ENOENT;
		}
		scnprintf(g_raydium_ts->customized_fw_name,
			  sizeof(g_raydium_ts->customized_fw_name), "%s", g_raydium_ts->fw_name_dt);
	}
	LOGI("request firmware name = %s\n", g_raydium_ts->customized_fw_name);

	i32_ret = request_firmware(&fw, g_raydium_ts->customized_fw_name, dev);
	if (fw == NULL) {
		LOGE("failed to request touch firmware %d\n", i32_ret);
		return i32_ret;
	}
	LOGI("get firmware success size:%zx\n", fw->size);
	firmware_update_cb(fw, NULL);
	release_firmware(fw);

	return i32_ret;
}
static ssize_t raydium_request_firmware_store(struct device *dev,
		struct device_attribute *attr,
		const char *p_i8_buf, size_t count)
{
	if (strlen(p_i8_buf) < 1)
		return -EINVAL;

	scnprintf(g_raydium_ts->customized_fw_name,
		  sizeof(g_raydium_ts->customized_fw_name), "%s", p_i8_buf);
	// remove trailing newline
	g_raydium_ts->customized_fw_name[strcspn(g_raydium_ts->customized_fw_name, "\n")] = '\0';
	return count;
}

static ssize_t
raydium_edge_rejection_read_parameter(char *buf, size_t buf_size,
				      enum raydium_edge_rejection_parameter_types type)
{
	int i32_ret;
	unsigned char u8_rbuffer[1] = { 0 };

	mutex_lock(&g_raydium_ts->lock);

	i32_ret = handle_i2c_pda_read(g_raydium_ts->client, edge_rejection_address[type],
				      u8_rbuffer, 1);
	mutex_unlock(&g_raydium_ts->lock);
	if (i32_ret < 0)
		return i32_ret;

	LOGI("Read %x = %d\n", edge_rejection_address[type], u8_rbuffer[0]);
	return scnprintf(buf, buf_size, "%d", u8_rbuffer[0]);
}

static int raydium_edge_rejection_write_parameter(const char *buf,
						  enum raydium_edge_rejection_parameter_types type)
{
	int i32_ret;
	uint8_t value;

	if (strlen(buf) < 1)
		return -EINVAL;
	i32_ret = kstrtou8(buf, 10, &value);
	if (i32_ret < 0)
		return i32_ret;
	mutex_lock(&g_raydium_ts->lock);
	i32_ret = raydium_adjust_edge_rejection_locked(type, value);
	mutex_unlock(&g_raydium_ts->lock);

	return i32_ret;
}

static ssize_t raydium_deadzone_show(struct device *dev, struct device_attribute *attr,
				     char *p_i8_buf)
{
	return raydium_edge_rejection_read_parameter(p_i8_buf, PAGE_SIZE, DEADZONE_IN_PIXELS);
}
static ssize_t raydium_deadzone_store(struct device *dev, struct device_attribute *attr,
				      const char *p_i8_buf, size_t count)
{
	int i32_ret;

	i32_ret = raydium_edge_rejection_write_parameter(p_i8_buf, DEADZONE_IN_PIXELS);
	if (i32_ret < 0) {
		LOGE("%s: Failed to set up edge rejection parameters\n", __func__);
		return i32_ret;
	}

	return count;
}

static ssize_t raydium_edgezone_show(struct device *dev, struct device_attribute *attr,
				     char *p_i8_buf)
{
	return raydium_edge_rejection_read_parameter(p_i8_buf, PAGE_SIZE, EDGEZONE_IN_PIXELS);
}
static ssize_t raydium_edgezone_store(struct device *dev, struct device_attribute *attr,
				      const char *p_i8_buf, size_t count)
{
	int i32_ret;

	i32_ret = raydium_edge_rejection_write_parameter(p_i8_buf, EDGEZONE_IN_PIXELS);
	if (i32_ret < 0) {
		LOGE("%s: Failed to set up edge rejection parameters\n", __func__);
		return i32_ret;
	}

	return count;
}

static ssize_t raydium_edge_framecounter_show(struct device *dev, struct device_attribute *attr,
					      char *p_i8_buf)
{
	return raydium_edge_rejection_read_parameter(p_i8_buf, PAGE_SIZE, FRAMECOUNTER_IN_FRAMES);
}
static ssize_t raydium_edge_framecounter_store(struct device *dev, struct device_attribute *attr,
					       const char *p_i8_buf, size_t count)
{
	int i32_ret;

	i32_ret = raydium_edge_rejection_write_parameter(p_i8_buf, FRAMECOUNTER_IN_FRAMES);
	if (i32_ret < 0) {
		LOGE("%s: Failed to set up edge rejection parameters\n", __func__);
		return i32_ret;
	}

	return count;
}

static ssize_t raydium_landlock_show(struct device *dev, struct device_attribute *attr,
				     char *p_i8_buf)
{
	return raydium_edge_rejection_read_parameter(p_i8_buf, PAGE_SIZE, LANDLOCK_IN_PIXELS);
}
static ssize_t raydium_landlock_store(struct device *dev, struct device_attribute *attr,
				      const char *p_i8_buf, size_t count)
{
	int i32_ret;

	i32_ret = raydium_edge_rejection_write_parameter(p_i8_buf, LANDLOCK_IN_PIXELS);
	if (i32_ret < 0) {
		LOGE("%s: Failed to set up edge rejection parameters\n", __func__);
		return i32_ret;
	}

	return count;
}

static ssize_t raydium_check_error_report_show_with_size(struct device *dev,
							 struct device_attribute *attr,
							 char *p_i8_buf, ssize_t size)
{
	int i32_ret = -1;
	unsigned char u8_buf[RAYDIUM_PDA2_ERROR_RPT_ADDR_DATA_SIZE];
	struct i2c_client *client = container_of(dev, struct i2c_client, dev);

	mutex_lock(&g_raydium_ts->lock);
	i32_ret = raydium_i2c_pda2_read(client, RAYDIUM_PDA2_ERROR_RPT_ADDR, u8_buf,
					RAYDIUM_PDA2_ERROR_RPT_ADDR_DATA_SIZE);
	mutex_unlock(&g_raydium_ts->lock);
	if (i32_ret < 0)
		return i32_ret;
	else
		return scnprintf(p_i8_buf, size,
										 "RAD Error Report: %02X%02X%02X%02X%02X%02X%02X%02X\n",
				 						 u8_buf[7], u8_buf[6], u8_buf[5], u8_buf[4],
										u8_buf[3], u8_buf[2], u8_buf[1], u8_buf[0]);
}

static ssize_t raydium_check_error_report_show(struct device *dev, struct device_attribute *attr,
					       char *p_i8_buf)
{
	return raydium_check_error_report_show_with_size(dev, attr, p_i8_buf, PAGE_SIZE);
}

static ssize_t raydium_report_mode_control_store(struct device *dev, struct device_attribute *attr,
						 const char *p_i8_buf, size_t count)
{
	unsigned char u8_rbuffer[4] = { 0 };
	int i32_ret = -1;
	unsigned char u8_mode;
	unsigned char u8_data_type;
	struct i2c_client *client = container_of(dev, struct i2c_client, dev);

	if (g_raydium_ts->is_suspend)
		LOGD("RAD is_suspend at %s\n", __func__);

	if (!g_raydium_ts->streaming_mode_enable) {
		LOGE("This panel does not support streaming mode.\n");
		return -EINVAL;
	}

	i32_ret = sscanf(p_i8_buf, "%hhx %hhx", &u8_mode, &u8_data_type);
	if (i32_ret != 2) {
		LOGE("Failed to parse input string\n");
		return -EINVAL;
	}

	if (u8_mode > 0x02) {
		LOGD("report mode set error %d\n", u8_mode);
		u8_mode = REPORT_WATER_STREAMING_MODE;
	}

	if (u8_data_type != RAYDIUM_TOUCH_RAW_DATA && u8_data_type != RAYDIUM_TOUCH_DIFF_DATA) {
		LOGD("data type set error %d\n", u8_data_type);
		u8_data_type = RAYDIUM_TOUCH_DIFF_DATA;
	}

	mutex_lock(&g_raydium_ts->lock);
	u8_rbuffer[0] = RAYDIUM_HOST_CMD_REPORT_MODE;
	u8_rbuffer[1] = 0;
	u8_rbuffer[2] = u8_mode;
	u8_rbuffer[3] = u8_data_type;
	i32_ret = raydium_i2c_pda2_set_page(client, g_raydium_ts->is_suspend, RAYDIUM_PDA2_PAGE_0);
	if (i32_ret < 0)
		goto exit_i2c_error;
	i32_ret = raydium_i2c_pda2_write(client, RAYDIUM_PDA2_HOST_CMD_ADDR, u8_rbuffer, 4);
	if (i32_ret < 0)
		goto exit_i2c_error;
	g_u8_report_mode = u8_mode;
	LOGI("report mode set to %d\n", u8_mode);
exit_i2c_error:
	mutex_unlock(&g_raydium_ts->lock);
	return count;
}

static ssize_t raydium_print_image_data(short *u8_rbuffer, char *p_i8_buf, ssize_t u8_rbuffer_size,
					ssize_t size, unsigned char x_channel,
					unsigned char y_channel)
{
	char buf[HEATMAP_DATA_BUFFER_SIZE] = { 0 };
	unsigned char u8_j, u8_i;
	unsigned int buf_offset = 0;
	int i32_ret = 0;
	ssize_t image_size = x_channel * y_channel;

	if (image_size * sizeof(short) > u8_rbuffer_size) {
		LOGE("Given dimensions are not match with data.\n");
		i32_ret = -EINVAL;
		goto exit_error;
	}

	for (u8_j = 0; u8_j < y_channel; u8_j++) {
		buf_offset = 0;
		for (u8_i = 0; u8_i < x_channel; u8_i++)
			buf_offset += scnprintf(buf + buf_offset,
						HEATMAP_DATA_BUFFER_SIZE - buf_offset, "%4d, ",
						u8_rbuffer[u8_i + u8_j * x_channel]);
		i32_ret += scnprintf(p_i8_buf + i32_ret, size - i32_ret, "%s\n", buf);
	}
exit_error:
	return i32_ret;
}

static ssize_t raydium_dump_data_by_data_type(struct device *dev, struct device_attribute *attr,
					      char *p_i8_buf, ssize_t size, unsigned char data_type)
{
	int i32_ret = -1;
	int i32_retry = 0;
	unsigned char u8_data[RAD_FT_CMD_LENGTH];
	unsigned char u8_rbuffer[MAX_READ_PACKET_SIZE];
	unsigned char u8_retry_limit = (g_raydium_ts->is_suspend) ? 100 : 30;

	struct i2c_client *client = container_of(dev, struct i2c_client, dev);

	/* Set data type */
	memset(u8_data, 0x00, sizeof(u8_data));

	u8_data[RAD_HOST_CMD_POS] = RAYDIUM_HOST_CMD_NO_OP;
	u8_data[RAD_FT_CMD_POS] = data_type;
	u8_data[RAD_FT_STATUS0_POS] = g_u8_raw_data_pure;

	mutex_lock(&g_raydium_ts->lock);
	i32_ret = raydium_i2c_pda2_write(client, RAYDIUM_PDA2_HOST_CMD_ADDR, u8_data,
					 RAD_FT_CMD_LENGTH);
	if (i32_ret < 0)
		goto exit_error;

	/* Read the data by i2c transaction and dump those data */

	memset(u8_data, 0x00, sizeof(u8_data));
	memset(u8_rbuffer, 0x00, sizeof(u8_rbuffer));

	/* make sure update flag was set */
	for (i32_retry = 0; i32_retry < u8_retry_limit; i32_retry++) {
		i32_ret = raydium_i2c_pda2_set_page(client, g_raydium_ts->is_suspend,
						    RAYDIUM_PDA2_PAGE_0);
		if (i32_ret < 0)
			goto exit_error;

		i32_ret = raydium_i2c_pda2_read(client, RAYDIUM_PDA2_HOST_CMD_ADDR, u8_data,
						RAD_FT_CMD_LENGTH);
		if (i32_ret < 0)
			goto exit_error;

		if ((u8_data[RAD_FT_CMD_POS] & RAYDIUM_FT_UPDATE) == RAYDIUM_FT_UPDATE)
			break;

		usleep_range(500, 1500);
	}
	if (i32_retry == u8_retry_limit) {
		LOGE("%s: Failed to get update flag after %d retries\n", __func__, i32_retry);
		i32_ret = -EAGAIN;
		goto exit_error;
	}

	i32_ret =
		handle_i2c_pda_read(client, RAD_READ_FT_DATA_CMD, u8_rbuffer, MAX_READ_PACKET_SIZE);
	if (i32_ret < 0)
		goto exit_error;

	/* clear update flag to get next one */
	memset(u8_data, 0x00, sizeof(u8_data));

	i32_ret = raydium_i2c_pda2_write(client, RAYDIUM_PDA2_HOST_CMD_ADDR, u8_data,
					 RAD_FT_CMD_LENGTH);
	if (i32_ret < 0)
		goto exit_error;
	mutex_unlock(&g_raydium_ts->lock);

	return raydium_print_image_data((short *)u8_rbuffer, p_i8_buf, sizeof(u8_rbuffer), size,
					g_raydium_ts->x_channel, g_raydium_ts->y_channel);
exit_error:
	mutex_unlock(&g_raydium_ts->lock);
	return i32_ret;
}

static ssize_t raydium_baseline_data_show_with_size(struct device *dev,
						    struct device_attribute *attr, char *p_i8_buf,
						    ssize_t size)
{
	int i32_ret = 0;
	int i32_dump_ret = scnprintf(p_i8_buf, size, "------ Baseline Data ------\n");

	i32_ret = raydium_dump_data_by_data_type(dev, attr, p_i8_buf + i32_dump_ret,
						 size - i32_dump_ret, RAYDIUM_TOUCH_BASELINE_DATA);
	if (i32_ret < 0) {
		LOGE("Fail to dump baseline\n");
		goto exit_error;
	}
	i32_ret += i32_dump_ret;
exit_error:
	return i32_ret;
}

static ssize_t raydium_baseline_data_show(struct device *dev, struct device_attribute *attr,
					  char *p_i8_buf)
{
	return raydium_baseline_data_show_with_size(dev, attr, p_i8_buf, PAGE_SIZE);
}

static ssize_t raydium_diff_data_show_with_size(struct device *dev, struct device_attribute *attr,
						char *p_i8_buf, ssize_t size)
{
	int i32_ret = 0;
	int i32_dump_ret = scnprintf(p_i8_buf, size, "------ Diff Data ------\n");

	i32_ret = raydium_dump_data_by_data_type(dev, attr, p_i8_buf + i32_dump_ret,
						 size - i32_dump_ret, RAYDIUM_TOUCH_DIFF_DATA);
	if (i32_ret < 0) {
		LOGE("Fail to dump diff\n");
		goto exit_error;
	}
	i32_ret += i32_dump_ret;
exit_error:
	return i32_ret;
}

static ssize_t raydium_diff_data_show(struct device *dev, struct device_attribute *attr,
				      char *p_i8_buf)
{
	return raydium_diff_data_show_with_size(dev, attr, p_i8_buf, PAGE_SIZE);
}

/**
 * raydium_change_handler_locked - change the IRQ handler.
 * @irq:		The number of IRQ whose handler is to be changed.
 * @new_action:	The `irqaction` struct containing the new handler.
 *
 * Context:		Get and release the `factory_test_mode_lock` from the caller first.
 *				to avoid multiple threads trying to release the handler.
 *
 * Return:
 * * 0			- Successfully register the handler for IRQ.
 * * Others		- Failed to register the handler
 */
static int raydium_change_handler_locked(unsigned int irq, struct irqaction *new_action)
{
	int ret;
	unsigned char u8_i;

	if (irq <= 0 || !new_action)
		return -EINVAL;

	// Release all touches since we will not read the touch value during test
	for (u8_i = 0; u8_i < g_raydium_ts->pdata->num_max_touches; u8_i++) {
		input_mt_slot(g_raydium_ts->input_dev, u8_i);
		input_mt_report_slot_state(g_raydium_ts->input_dev, MT_TOOL_FINGER, false);
	}
	input_mt_report_pointer_emulation(g_raydium_ts->input_dev, false);
	input_sync(g_raydium_ts->input_dev);

	// Free IRQ will wait for any running handler so need not to disable it first.
	free_irq(irq, g_raydium_ts);

	// Request a new one with given struct `irqaction`
	ret = request_threaded_irq(irq, new_action->handler, new_action->thread_fn,
				   new_action->flags, new_action->name, new_action->dev_id);
	if (ret < 0)
		LOGE("Failed to request handler for irq-%d, ret=%d\n", irq, ret);

	return ret;
}

/**
 * raydium_toggle_test_mode_locked - change the IRQ handler according to the state.
 * @enable:		Enable or disable the test.
 *
 * Note that this function will try to switch back to the original handler when
 * failed to register the new one but does not guarantee. So, in the worst case
 * where we failed to register the new one as well as recover the old one, the IRQ
 * will be ignored.
 *
 * Context:		Get and release the `factory_test_mode_lock` from the caller first.
 *				to avoid multiple threads trying to release the handler.
 *
 * Return:
 * * 0			- Changes the handlers successfully
 * * Others		- Failed to change the handlers
 */
static int raydium_toggle_test_mode_locked(bool enable)
{
	int ret;
	struct irq_desc *irq_desc, *irq2_desc;
	struct irqaction irq_action, irq2_action;
	irq_handler_t original_irq_thread_fn, original_irq2_thread_fn;

	// Read the original struct to obtain the setup
	irq_desc = irq_to_desc(g_raydium_ts->client->irq);
	if (!irq_desc || !irq_desc->action) {
		LOGE("Failed to obtain original data for irq-%d\n", g_raydium_ts->client->irq);
		return -ENOENT;
	}
	memcpy(&irq_action, irq_desc->action, sizeof(irq_action));
	if (g_raydium_ts->irq2 > 0) {
		irq2_desc = irq_to_desc(g_raydium_ts->irq2);
		if (!irq2_desc || !irq2_desc->action) {
			LOGE("Failed to obtain original data for irq-%d\n",
			     g_raydium_ts->client->irq);
			return -ENOENT;
		}
		memcpy(&irq2_action, irq2_desc->action, sizeof(irq2_action));
	}

	// Swap the threaded handler
	original_irq_thread_fn = irq_action.thread_fn;
	original_irq2_thread_fn = irq2_action.thread_fn;
	if (enable) {
		irq_action.thread_fn = raydium_ts_interrupt_test;
		irq2_action.thread_fn = raydium_ts_interrupt2_test;
	} else {
		irq_action.thread_fn = raydium_ts_interrupt;
		irq2_action.thread_fn = raydium_ts_interrupt2;
	}

	// Request a new threaded IRQ
	ret = raydium_change_handler_locked(g_raydium_ts->client->irq, &irq_action);
	if (ret < 0)
		goto exit_irq_error;
	if (g_raydium_ts->irq2 > 0) {
		ret = raydium_change_handler_locked(g_raydium_ts->irq2, &irq2_action);
		if (ret < 0)
			goto exit_irq2_error;
	}

	return 0;

exit_irq2_error:
	LOGE("Try to reset irq-%d's handler\n", g_raydium_ts->client->irq);
	irq2_action.thread_fn = original_irq2_thread_fn;
	raydium_change_handler_locked(g_raydium_ts->irq2, &irq2_action);
exit_irq_error:
	LOGE("Try to reset irq-%d's handler\n", g_raydium_ts->client->irq);
	irq_action.thread_fn = original_irq_thread_fn;
	raydium_change_handler_locked(g_raydium_ts->client->irq, &irq_action);

	return ret;
}

static ssize_t raydium_factory_setup_store(struct device *dev, struct device_attribute *attr,
					   const char *p_i8_buf, size_t count)
{
	int ret;
	unsigned int new_mode, original_mode;

	ret = kstrtouint(p_i8_buf, 10, &new_mode);
	if (ret < 0) {
		LOGE("Failed to parse value: %s\n", p_i8_buf);
		return ret;
	}

	if (new_mode >= TEST_MAX) {
		LOGE("Unknown test mode %d\n", new_mode);
		return -EINVAL;
	}

	mutex_lock(&g_raydium_ts->factory_test_mode_lock);
	original_mode = g_raydium_ts->factory_test_mode;
	if (new_mode == original_mode) {
		LOGW("Already in test mode %d\n", new_mode);
		ret = count;
		goto exit;
	}

	if (new_mode == OFF) {
		ret = raydium_toggle_test_mode_locked(DISABLE);
		disable_irq(g_raydium_ts->irq2);
	} else if (original_mode == OFF) {
		ret = raydium_toggle_test_mode_locked(ENABLE);
	}

	if (ret < 0) {
		g_raydium_ts->factory_test_mode = OFF;
		LOGE("Failed to toggle test mode from %d to %d, ret=%d\n", original_mode, new_mode,
		     ret);
		goto exit;
	}
	ret = count;
	g_raydium_ts->factory_test_mode = new_mode;
	LOGI("Test mode toggled from %d to %d\n", original_mode, new_mode);

exit:
	mutex_unlock(&g_raydium_ts->factory_test_mode_lock);
	return ret;
}

static ssize_t raydium_factory_setup_show_with_size(struct device *dev,
						    struct device_attribute *attr, char *p_i8_buf,
						    ssize_t size)
{
	return scnprintf(p_i8_buf, size, "RAD Factory Test Mode: %d\n",
			 g_raydium_ts->factory_test_mode);
}

static ssize_t raydium_factory_setup_show(struct device *dev, struct device_attribute *attr,
					  char *p_i8_buf)
{
	return raydium_factory_setup_show_with_size(dev, attr, p_i8_buf, PAGE_SIZE);
}

#ifdef RAD_SELFTEST
static ssize_t raydium_reset_control_store(struct device *dev,
		struct device_attribute *attr,
		const char *p_i8_buf, size_t count)
{
	int i32_ret = 0;
	unsigned char u8_high;

	if (g_raydium_ts->is_suspend)
		LOGD("RAD is_suspend at %s\n", __func__);

	/* receive command line arguments string */
	if (count > 2)
		return -EINVAL;

	i32_ret = kstrtou8(p_i8_buf, 16, &u8_high);
	if (i32_ret < 0)
		return i32_ret;

	g_u8_i2c_mode = PDA2_MODE;
	g_u8_resetflag = true;

	if (u8_high) {
		LOGI("RAD %s set reset gpio to high!!\n", __func__);
		gpio_set_value(g_raydium_ts->pdata->reset_gpio, 1);
	} else {
		LOGI("RAD %s set reset gpio to low!!\n", __func__);
		gpio_set_value(g_raydium_ts->pdata->reset_gpio, 0);
	}
	return count;
}

static ssize_t raydium_irq_state_show_with_size(struct device *dev, struct device_attribute *attr,
						char *p_i8_buf, ssize_t size)
{
	unsigned int u32_irq_value;

	u32_irq_value = gpio_get_value(g_raydium_ts->pdata->irq_gpio);

	return scnprintf(p_i8_buf, size, "RAD IRQ State: %d\n", u32_irq_value);
}

static ssize_t raydium_irq_state_show(struct device *dev, struct device_attribute *attr,
				      char *p_i8_buf)
{
	return raydium_irq_state_show_with_size(dev, attr, p_i8_buf, PAGE_SIZE);
}

static ssize_t raydium_int_flag_show_with_size(struct device *dev, struct device_attribute *attr,
					       char *p_i8_buf, ssize_t size)
{
	return scnprintf(p_i8_buf, size, "RAD INT Flag: %d\n", g_u8_raydium_flag);
}

static ssize_t raydium_int_flag_show(struct device *dev,
				 struct device_attribute *attr,
				 char *p_i8_buf)
{
	return raydium_int_flag_show_with_size(dev, attr, p_i8_buf, PAGE_SIZE);
}

static ssize_t raydium_int_flag_store(struct device *dev,
				  struct device_attribute *attr,
				  const char *p_i8_buf, size_t count)
{
	int i32_ret = 0;
	unsigned char u8_flag = 0;

	/* receive command line arguments string */
	if (count > 2)
		return -EINVAL;
	i32_ret = kstrtou8(p_i8_buf, 16, &u8_flag);
	if (i32_ret < 0)
		return i32_ret;
	g_u8_raydium_flag = u8_flag;
	return count;
}

static ssize_t raydium_selftest_show(struct device *dev,
				     struct device_attribute *attr,
				     char *p_i8_buf)
{
	int i32_ret = SUCCESS;

	LOGI("do selftest\n");

	i32_ret = raydium_do_selftest(g_raydium_ts, p_i8_buf, PAGE_SIZE);

	return strlen(p_i8_buf);
}

static ssize_t raydium_sensor_test_setup_show(struct device *dev, struct device_attribute *attr,
					      char *p_i8_buf)
{
	int i32_ret = SUCCESS;
	struct i2c_client *client = container_of(dev, struct i2c_client, dev);

	if (!g_raydium_ts->use_wrapped_i2c)
		return scnprintf(p_i8_buf, PAGE_SIZE,
				 "Sensor test can only be executed with wrapped I2C, skipped\n");

	if (atomic_read(&g_raydium_ts->sensor_test_setup_done))
		return scnprintf(p_i8_buf, PAGE_SIZE, "Sensor test already setup, skipped\n");

	/* Disable the IRQ first to ignore any touches during test */
	raydium_irq_control(DISABLE, IRQ_MODE_CONTROL);
	i32_ret = raydium_do_sensor_test_setup(client);
	if (i32_ret) {
		raydium_irq_control(ENABLE, IRQ_MODE_CONTROL);
		return scnprintf(p_i8_buf, PAGE_SIZE, "Failed with ret=%d\n", i32_ret);
	} else {
		atomic_set(&g_raydium_ts->sensor_test_setup_done, true);
		return scnprintf(p_i8_buf, PAGE_SIZE, "Pass\n");
	}
}

static ssize_t raydium_stimulus_test_show(struct device *dev, struct device_attribute *attr,
					  char *p_i8_buf)
{
	int i32_ret;
	struct i2c_client *client = container_of(dev, struct i2c_client, dev);

	if (!atomic_read(&g_raydium_ts->sensor_test_setup_done))
		return scnprintf(p_i8_buf, PAGE_SIZE, "Sensor test not setup yet, skipped\n");

	i32_ret = raydium_do_stimulus_test(client, p_i8_buf, PAGE_SIZE);

	atomic_set(&g_raydium_ts->sensor_test_setup_done, false);
	raydium_irq_control(ENABLE, IRQ_MODE_CONTROL);
	return i32_ret;
}

static ssize_t raydium_noise_test_show(struct device *dev, struct device_attribute *attr,
				       char *p_i8_buf)
{
	int i32_ret;
	struct i2c_client *client = container_of(dev, struct i2c_client, dev);

	if (!atomic_read(&g_raydium_ts->sensor_test_setup_done))
		return scnprintf(p_i8_buf, PAGE_SIZE, "Sensor test not setup yet, skipped\n");

	i32_ret = raydium_do_noise_test(client, p_i8_buf, PAGE_SIZE);

	atomic_set(&g_raydium_ts->sensor_test_setup_done, false);
	raydium_irq_control(ENABLE, IRQ_MODE_CONTROL);
	return i32_ret;
}

static ssize_t raydium_update_sensor_impact_store(struct device *dev, struct device_attribute *attr,
						  const char *p_i8_buf, size_t count)
{
	int idx, i32_ret;
	char *temp_buf, *token, *free_temp_buf;
	const char *delim = " ,";
	u16 value, u16_new_sensor_impact[RM6D031_MAX_SENSING_PIN_NUM];

	temp_buf = kzalloc(count + 1, GFP_KERNEL);
	if (temp_buf == NULL)
		return -ENOMEM;
	strscpy(temp_buf, p_i8_buf, count);
	free_temp_buf = temp_buf;

	for (idx = 0; idx < RM6D031_MAX_SENSING_PIN_NUM; idx++) {
		// Find next value
		token = strsep(&temp_buf, delim);
		if (!token) {
			LOGE("Try to get %d value but only %d are found\n",
			     RM6D031_MAX_SENSING_PIN_NUM, idx);
			i32_ret = -EINVAL;
			goto exit;
		}

		// Parse to u16 and save
		i32_ret = kstrtou16(token, 10, &value);
		if (i32_ret < 0) {
			LOGE("Failed to parse value: %s\n", token);
			goto exit;
		}
		u16_new_sensor_impact[idx] = value;
	}

	// Check if there's other unparsed string
	if (temp_buf) {
		LOGE("Unknown parameter: %s\n", temp_buf);
		i32_ret = -EINVAL;
		goto exit;
	}

	i32_ret = update_sensor_impact(u16_new_sensor_impact, sizeof(u16_new_sensor_impact));
	if (i32_ret) {
		LOGE("%s: failed to update sensor impact value, ret=%d\n", __func__, i32_ret);
		goto exit;
	}

	i32_ret = count;
	LOGI("Updated sensor impact data successfully.\n");

exit:
	kfree(free_temp_buf);
	return i32_ret;
}
#endif

static ssize_t raydium_offload_store(struct device *dev, struct device_attribute *attr,
				     const char *buf, size_t count)
{
	int rc;
	bool enable;

	rc = kstrtobool(buf, &enable);
	if (rc) {
		LOGE("kstrtobool failed, rc = %d\n", rc);
		return -EINVAL;
	}

	raydium_set_offload(enable, MANUAL_CMD);
	return count;
}

static ssize_t raydium_offload_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	ssize_t count;

	mutex_lock(&g_raydium_ts->offload_lock);
	LOGI("offload mode %s\n", g_raydium_ts->offloading ? "enabled" : "disabled");
	count = scnprintf(buf, PAGE_SIZE, "offload mode=%d\n", g_raydium_ts->offloading);
	mutex_unlock(&g_raydium_ts->offload_lock);
	return count;
}

/* panel calibration cmd (R)
 *  example:cat raydium_ic_verion
 */
static DEVICE_ATTR_RO(raydium_touch_calibration);

/* check the i2c (R)
 *  example:cat raydium_check_i2c
 */
static DEVICE_ATTR_RO(raydium_check_i2c);

/* upgrade configurate and algo firmware from app.bin (W)
 *  example:echo "offset num_of_bin length *_app.bin [length *_app.bin]"
 *  > raydium_fw_upgrade_mode
 */
static DEVICE_ATTR_RW(raydium_fw_upgrade);

/* change I2C communication mode (W)
 *  example:echo 1 > raydium_i2c_pda2_mode ==> enable pda2 mode
 *        echo 0 > raydium_i2c_pda2_mode ==> disable pda2 mode
 */
static DEVICE_ATTR_WO(raydium_i2c_pda2_mode);

/* I2C pda mode (R/W)
 *  example:    cat raydium_i2c_pda_access ==> read pda address provided by the
 *		following cmd
 *		echo ADDRinHEX [DATAinHEX] > raydium_i2c_pda_access ==> write
 *		pda address [data]
 */
static DEVICE_ATTR_RW(raydium_i2c_pda_access);
/* I2C pda mode via pda2(R/W)
 *  example:    cat raydium_i2c_pda_access ==> read pda address provided by the
 *		following cmd
 *		echo ADDRinHEX [DATAinHEX] > raydium_i2c_pda_access ==> write
 *		pda address [data]
 */
static DEVICE_ATTR_RW(raydium_i2c_pda_access_via_pda2);

/* I2C pda2 mode (R/W)
 *  example:    cat raydium_i2c_pda2_access ==> read pda2 address provided by
 *		the following cmd
 *		echo ADDRinHEX [DATAinHEX] > raydium_i2c_pda2_access ==>
 *		write pda2 address [data]
 */
static DEVICE_ATTR_RW(raydium_i2c_pda2_access);

/* I2C pda2 mode page (W)
 *  example:    echo PAGEinHEX > raydium_i2c_pda2_page ==> write pda2 page
 */
static DEVICE_ATTR_WO(raydium_i2c_pda2_page);

/* I2C read/set FT raw data (R/W)
 *  example:    cat raydium_i2c_raw_data ==> read raw data with specific length
 *		of corresponding type provided by the following cmd
 *              echo DataTypeinHEX RawDataLengthinHEX > raydium_i2c_raw_data
 *		==> set raw data type and its length
 */
static DEVICE_ATTR_RW(raydium_i2c_raw_data);

static DEVICE_ATTR_RO(raydium_baseline_data);

static DEVICE_ATTR_RO(raydium_diff_data);

/* Touch lock (W)
 *  example:    echo 1 > raydium_i2c_touch_lock ==> enable touch lock
 *            echo 0 > raydium_i2c_touch_lock ==> disable touch lock
 */
static DEVICE_ATTR_WO(raydium_i2c_touch_lock);

/* Log level (W)
 *  example:    echo 1 > raydium_log_level ==> modify log level
 */
static DEVICE_ATTR_WO(raydium_log_level);
/* show the fw version (R)
 *  example:cat raydium_fw_version
 */
static DEVICE_ATTR_RO(raydium_check_fw_version);

/* show the driver version (R)
 *  example:cat raydium_check_driver_version
 */
static DEVICE_ATTR_RO(raydium_check_driver_version);
/* show the panel version (R)
 *  example:cat raydium_panel_version
 */
static DEVICE_ATTR_RO(raydium_check_panel_version);

static DEVICE_ATTR_RO(raydium_hw_reset);

static DEVICE_ATTR_RO(raydium_palm_status);

static DEVICE_ATTR_WO(raydium_receive_fw_control);

static DEVICE_ATTR_RO(raydium_check_error_report);

/* request firmware show (RW)
 *  example:cat raydium_request_firmware
 *  example:echo file_name > raydium_request_firmware
 */
static DEVICE_ATTR_RW(raydium_request_firmware);

static DEVICE_ATTR_WO(raydium_report_mode_control);

static DEVICE_ATTR_RW(raydium_offload);
/* Factory testing (R/W)
 *  example:
 *    echo 1 > raydium_factory_setup => Start the FATP1 test
 *    cat raydium_factory_setup => Read the current test mode
 */
static DEVICE_ATTR_RW(raydium_factory_setup);

#ifdef RAD_SELFTEST

/* Read interrupt flag cmd (R)
 *  example:cat raydium_int_flag
 */
static DEVICE_ATTR_RW(raydium_int_flag);

static DEVICE_ATTR_WO(raydium_reset_control);
static DEVICE_ATTR_RO(raydium_irq_state);
static DEVICE_ATTR_RO(raydium_selftest);
static DEVICE_ATTR_RO(raydium_sensor_test_setup);
static DEVICE_ATTR_RO(raydium_stimulus_test);
static DEVICE_ATTR_RO(raydium_noise_test);

/* Update sensor impact (W)
 *  usage:		update the sensor impact value for each sensor, which
 *				accept 48 decimal values
 *  example:    echo 100 200 300 ... > raydium_update_sensor_impact
 */
static DEVICE_ATTR_WO(raydium_update_sensor_impact);
#endif

static ssize_t raydium_dump_data_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	unsigned short u16_count = 0;
	unsigned short u16_ret = 0;
	mutex_lock(&g_raydium_ts->offload_lock);
	if (g_raydium_ts->offloading) {
		mutex_unlock(&g_raydium_ts->offload_lock);
		LOGW("Cancel dumping data on AP due to offloading\n");
		return 0;
	}
	u16_ret = raydium_check_error_report_show_with_size(
		dev, &dev_attr_raydium_check_error_report, buf + u16_count, PAGE_SIZE - u16_count);
	if (u16_ret <= 0)
		LOGW("Read check_error_report failed\n");
	else
		u16_count += u16_ret;

	u16_ret = raydium_check_i2c_show_with_size(dev, &dev_attr_raydium_check_i2c,
						   buf + u16_count, PAGE_SIZE - u16_count);
	if (u16_ret <= 0)
		LOGW("Read check_i2c failed\n");
	else
		u16_count += u16_ret;

	u16_ret = raydium_check_fw_version_show_with_size(dev, &dev_attr_raydium_check_fw_version,
							  buf + u16_count, PAGE_SIZE - u16_count);
	if (u16_ret <= 0)
		LOGW("Read check_fw_version failed\n");
	else
		u16_count += u16_ret;

	u16_ret = raydium_check_driver_version_show_with_size(
		dev, &dev_attr_raydium_check_driver_version, buf + u16_count,
		PAGE_SIZE - u16_count);
	if (u16_ret <= 0)
		LOGW("Read check_driver_version failed\n");
	else
		u16_count += u16_ret;

	u16_ret = raydium_check_panel_version_show_with_size(
		dev, &dev_attr_raydium_check_panel_version, buf + u16_count, PAGE_SIZE - u16_count);
	if (u16_ret <= 0)
		LOGW("Read check_panel_version failed\n");
	else
		u16_count += u16_ret;

	u16_ret = raydium_factory_setup_show_with_size(dev, &dev_attr_raydium_factory_setup,
						       buf + u16_count, PAGE_SIZE - u16_count);
	if (u16_ret <= 0)
		LOGW("Read factory_setup failed\n");
	else
		u16_count += u16_ret;

#ifdef RAD_SELFTEST

	u16_ret = raydium_int_flag_show_with_size(dev, &dev_attr_raydium_int_flag, buf + u16_count,
						  PAGE_SIZE - u16_count);
	if (u16_ret <= 0)
		LOGW("Read int_flag failed\n");
	else
		u16_count += u16_ret;

	u16_ret = raydium_irq_state_show_with_size(dev, &dev_attr_raydium_irq_state,
						   buf + u16_count, PAGE_SIZE - u16_count);
	if (u16_ret <= 0)
		LOGW("Read irq_state failed\n");
	else
		u16_count += u16_ret;

#endif

	u16_ret = raydium_baseline_data_show_with_size(dev, &dev_attr_raydium_baseline_data,
						       buf + u16_count, PAGE_SIZE - u16_count);
	if (u16_ret <= 0)
		LOGW("Read baseline data failed\n");
	else
		u16_count += u16_ret;

	u16_ret = raydium_diff_data_show_with_size(dev, &dev_attr_raydium_diff_data,
						   buf + u16_count, PAGE_SIZE - u16_count);
	if (u16_ret <= 0)
		LOGW("Read diff data failed\n");
	else
		u16_count += u16_ret;

	mutex_unlock(&g_raydium_ts->offload_lock);
	return u16_count;
}

/* dump touch data (R)
 *  example:cat raydium_dump_data
 */
static DEVICE_ATTR_RO(raydium_dump_data);

/* Enable/Disable touch STB (W)
 *  example: echo 1 > raydium_i2c_touch_sleep => enable touch STB
 *           echo 0 > raydium_i2c_touch_sleep => disable touch STB
 */
static DEVICE_ATTR_WO(raydium_i2c_touch_sleep);

static DEVICE_ATTR_RW(raydium_deadzone);
static DEVICE_ATTR_RW(raydium_edgezone);
static DEVICE_ATTR_RW(raydium_edge_framecounter);
static DEVICE_ATTR_RW(raydium_landlock);
/*add your attr in here*/
struct attribute *raydium_attributes[] = { &dev_attr_raydium_touch_calibration.attr,
					   &dev_attr_raydium_check_i2c.attr,
					   &dev_attr_raydium_i2c_pda2_mode.attr,
					   &dev_attr_raydium_i2c_pda_access.attr,
					   &dev_attr_raydium_i2c_pda_access_via_pda2.attr,
					   &dev_attr_raydium_i2c_pda2_access.attr,
					   &dev_attr_raydium_i2c_pda2_page.attr,
					   &dev_attr_raydium_i2c_raw_data.attr,
					   &dev_attr_raydium_i2c_touch_lock.attr,
					   &dev_attr_raydium_fw_upgrade.attr,
					   &dev_attr_raydium_check_fw_version.attr,
					   &dev_attr_raydium_check_panel_version.attr,
					   &dev_attr_raydium_hw_reset.attr,
					   &dev_attr_raydium_palm_status.attr,
					   &dev_attr_raydium_check_driver_version.attr,
					   &dev_attr_raydium_receive_fw_control.attr,
					   &dev_attr_raydium_request_firmware.attr,
					   &dev_attr_raydium_check_error_report.attr,
					   &dev_attr_raydium_log_level.attr,
					   &dev_attr_raydium_report_mode_control.attr,
					   &dev_attr_raydium_dump_data.attr,
					   &dev_attr_raydium_i2c_touch_sleep.attr,
					   &dev_attr_raydium_factory_setup.attr,
					   &dev_attr_raydium_deadzone.attr,
					   &dev_attr_raydium_edgezone.attr,
					   &dev_attr_raydium_edge_framecounter.attr,
					   &dev_attr_raydium_landlock.attr,
					   &dev_attr_raydium_baseline_data.attr,
					   &dev_attr_raydium_diff_data.attr,
#ifdef RAD_SELFTEST
					   &dev_attr_raydium_irq_state.attr,
					   &dev_attr_raydium_int_flag.attr,
					   &dev_attr_raydium_reset_control.attr,
					   &dev_attr_raydium_selftest.attr,
					   &dev_attr_raydium_sensor_test_setup.attr,
					   &dev_attr_raydium_stimulus_test.attr,
					   &dev_attr_raydium_noise_test.attr,
					   &dev_attr_raydium_update_sensor_impact.attr,
#endif
					   &dev_attr_raydium_offload.attr,
					   NULL };
