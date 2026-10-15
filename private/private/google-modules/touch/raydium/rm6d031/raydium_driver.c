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
#include <drm/drm_panel.h>
#include <linux/bitops.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/firmware.h>
#include <linux/gpio.h>
#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/input/mt.h>
#include <linux/interrupt.h>
#include <linux/jiffies.h>
#include <linux/kernel.h>
#include <linux/kprobes.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of_gpio.h>
#include <linux/pinctrl/consumer.h>
#include <linux/regulator/consumer.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/timer.h>
#include <linux/uaccess.h>
#include <linux/unistd.h>
#include <wearable_touch_interface.h>
#include <wearable_touch_mcu.h>
#include <i2c_arbitration_module.h>

#if defined(CONFIG_FB)
#include <linux/notifier.h>
#include <linux/fb.h>
#elif defined(CONFIG_HAS_EARLYSUSPEND)
#include <linux/earlysuspend.h>
#elif IS_ENABLED(CONFIG_QCOM_PANEL_EVENT_NOTIFIER)
#include <linux/soc/qcom/panel_event_notifier.h>
#endif /*end of CONFIG_FB*/

#include "raydium_driver.h"
#include "raydium_error.h"
#include "tpselftest.h"

struct raydium_slot_status {
	unsigned char pt_id;		/*Occupied point ID*/
	unsigned char need_update;	/*Mark as info need to be updated*/
	unsigned char pt_report_offset;	/*point info offset in report*/
};
/*The first 3 elements are currently occupied. therest is new coming points*/
struct raydium_slot_status gst_slot[MAX_TOUCH_NUM * 2];
struct raydium_slot_status gst_slot_init = {0xFF, 0, 0};

static const char *const offload_source_names[] = {
	[MANUAL_CMD] = "MANUAL_CMD",
	[TOUCH_SERVICE] = "TOUCH_SERVICE",
	[ARBITRATION_SERVICE] = "ARBITRATION_SERVICE",
};

static const char *const irq2_enable_reason_desc[] = {
	[TOUCH_WATCHDOG] = "TOUCH_WATCHDOG",
};

#if (defined(CONFIG_RM_SYSFS_DEBUG))
const struct attribute_group raydium_attr_group;
#endif /*end of CONFIG_RM_SYSFS_DEBUG*/

unsigned char g_u8_addr;
unsigned char g_u8_raydium_flag;
unsigned char g_u8_i2c_mode;
unsigned char g_u8_upgrade_type;
unsigned char g_u8_raw_data_type;
unsigned int g_u32_raw_data_len;	/* 128 bytes*/
unsigned char g_u8_raw_data_pure;
unsigned long g_u32_addr;
unsigned int g_u32_length;
unsigned int g_u32_driver_version;
unsigned char *g_rad_fw_image, *g_rad_init_image, *g_rad_tb_exe_image;
unsigned char *g_rad_boot_image, *g_rad_para_image;
unsigned char *g_rad_testfw_image, *g_rad_testpara_image;
unsigned char g_u8_table_setting, g_u8_table_init;
unsigned char g_rad_cc_image[RAD_CC_3X_SIZE];
unsigned char g_u8_resetflag;
unsigned char g_u8_cc_ready;
unsigned char g_u8_cc_info;
unsigned char g_u8_i2c_retry_cnt;
unsigned char g_u8_report_mode;
unsigned char u8_test_info[TEST_INFO_SIZE];
signed char i8_ft_test_thd[FT_TEST_THD_SIZE];
unsigned char u8_test_para[TEST_PARA_SIZE];
unsigned char u8_raw_data_3_cc[RAW_DATA_3_CC_SIZE];
unsigned char u8_raw_uc_cc[RAW_UC_CC_SIZE];
static void raydium_panel_notifier_callback(enum panel_event_notifier_tag tag,
					    struct panel_event_notification *event,
					    void *client_data);
static void raydium_send_key(struct input_dev *dev, uint32_t keycode);
static void raydium_ts_display_resume_locked(void);
static void raydium_ts_display_resume(void);
static void raydium_setup_edge_rejection_parameters_locked(void);
static void raydium_irq2_control(bool enable, enum irq2_enable_reason reason);
unsigned char g_u8_checkflag;
unsigned char g_u8_log_level;
struct raydium_ts_data *g_raydium_ts;
struct raydium_ts_display_data g_display_data;
struct raydium_ts_input_event touch_event_list_head;
static struct workqueue_struct *raydium_touch_event_delay_wq;
static struct wearable_touch_interface *wti;

static bool runtime_offload_enable;
// TODO: b/489559384 to refactor the register mechanism
static struct delayed_work register_mcu_transport_work;
static bool dispatcher_init;
static bool i2c_arb_init;
#define DEFER_CNT			     25
#define REGISTER_MCU_TRANSPORT_DELAY_JIFFIES msecs_to_jiffies(100)

/* Only support read access to turn it on in bootloader stage */
module_param(runtime_offload_enable, bool, 0400);

/* Helper function to get the name of offload request source */
static const char *get_offload_source_str(enum offload_request_source id)
{
	if (id < 0 || id >= ARRAY_SIZE(offload_source_names))
		return "UNKNOWN";

	return offload_source_names[id];
}

static int get_fw_version(void *unused_param, struct wti_fw_version *fw_ver)
{
	if (mutex_lock_interruptible(&g_raydium_ts->lock))
		return -ERESTARTSYS;

	scnprintf(fw_ver->buffer, sizeof(fw_ver->buffer), "%02X%02X%02X%02X",
		  (g_raydium_ts->fw_version >> 24) & 0xFF, (g_raydium_ts->fw_version >> 16) & 0xFF,
		  (g_raydium_ts->fw_version >> 8) & 0xFF, (g_raydium_ts->fw_version) & 0xFF);

	mutex_unlock(&g_raydium_ts->lock);
	return 0;
}

static int get_hardware_identifier(void *private_data,
				   struct wti_hardware_identifier *hardware_identifier)
{
	if (mutex_lock_interruptible(&g_raydium_ts->lock))
		return -ERESTARTSYS;

	scnprintf(hardware_identifier->buffer, sizeof(hardware_identifier->buffer), "%s",
		  g_raydium_ts->hw_id);

	mutex_unlock(&g_raydium_ts->lock);
	return 0;
}

static int get_display_resolution(void *private_data,
				  struct display_resolution_pixels *disp_resolution)
{
	if (mutex_lock_interruptible(&g_raydium_ts->lock))
		return -ERESTARTSYS;

	disp_resolution->x = g_raydium_ts->pdata->x_max;
	disp_resolution->y = g_raydium_ts->pdata->y_max;

	mutex_unlock(&g_raydium_ts->lock);
	return 0;
}

static int get_grid_spec(void *private_data, enum TOUCH_GRID_SPEC *grid_spec)
{
	if (mutex_lock_interruptible(&g_raydium_ts->lock))
		return -ERESTARTSYS;

	*grid_spec = g_raydium_ts->grid_spec;

	mutex_unlock(&g_raydium_ts->lock);
	return 0;
}

static void handle_input_event(uint16_t type, uint16_t code, int32_t value, ktime_t timestamp)
{
	if (type == EV_SYNC_FRAME)
		input_mt_sync_frame(g_raydium_ts->input_dev);
	else if (type == EV_TIMESTAMP)
		input_set_timestamp(g_raydium_ts->input_dev, timestamp);
	else if (type == EV_SLOT_STATE)
		input_mt_report_slot_state(g_raydium_ts->input_dev, code, value);
	else
		input_event(g_raydium_ts->input_dev, type, code, value);
}

static void queue_input_event(uint16_t type, uint16_t code, int32_t value, ktime_t timestamp)
{
	struct raydium_ts_input_event *input_report_event =
		kmalloc(sizeof(struct raydium_ts_input_event), GFP_KERNEL);
	input_report_event->type = type;
	input_report_event->code = code;
	input_report_event->value = value;
	input_report_event->timestamp = timestamp;
	list_add_tail(&input_report_event->list, &touch_event_list_head.list);
}

static void switch_driver_report_mode_locked(enum raydium_ts_driver_report_mode report_mode)
{
	LOGI("Driver switch report mode to: %d\n", report_mode);
	switch (report_mode) {
	case NORMAL:
		g_raydium_ts->report_input_event = handle_input_event;
		break;
	case QUEUEING:
		g_raydium_ts->report_input_event = queue_input_event;
		break;
	default:
		LOGW("Try to switch to an unknown driver report mode: %d\n", report_mode);
		break;
	}
}

static void switch_driver_report_mode(enum raydium_ts_driver_report_mode report_mode)
{
	mutex_lock(&g_raydium_ts->report_input_event_lock);
	switch_driver_report_mode_locked(report_mode);
	mutex_unlock(&g_raydium_ts->report_input_event_lock);
}

static void raydium_ts_release_queued_input_event(struct work_struct *unused)
{
	struct raydium_ts_input_event *event;
	struct raydium_ts_input_event *next_event;

	mutex_lock(&g_raydium_ts->report_input_event_lock);
	switch_driver_report_mode_locked(NORMAL);
	LOGI("Start to release queued input events\n");
	list_for_each_entry_safe(event, next_event, &touch_event_list_head.list, list) {
		handle_input_event(event->type, event->code, event->value, event->timestamp);
		list_del(&event->list);
		kfree(event);
	}
	mutex_unlock(&g_raydium_ts->report_input_event_lock);
}

/*******************************************************************************
 *  Name: raydium_variable_init
 *  Brief:
 *  Input:
 *  Output:
 *  Return:
 *******************************************************************************/
static void raydium_variable_init(void)
{
	g_u8_addr = RAYDIUM_PDA2_PDA_CFG_ADDR;
	g_u8_raydium_flag = NORMAL_MODE;
	g_u8_i2c_mode = PDA2_MODE;
	g_u8_upgrade_type = 0;
	g_u8_raw_data_pure = 0;
	g_u8_raw_data_type = RAYDIUM_FT_UPDATE;
	g_u32_raw_data_len = 64 * 2;	/* 128 bytes*/
	g_u32_addr = RAYDIUM_CHK_I2C_CMD;
	g_u32_length = 1;
	g_u8_table_setting = 0;
	g_u8_table_init = 0;
	g_rad_fw_image = NULL;
	g_rad_init_image = NULL;
	g_rad_tb_exe_image = NULL;
	g_rad_boot_image = NULL;
	g_rad_para_image = NULL;
	g_rad_testfw_image = NULL;
	g_rad_testpara_image = NULL;
	g_u32_driver_version = ((RAD_MAIN_VERSION <<  24) |
				(RAD_MINOR_VERSION << 16) |
				(RAD_CUSTOMER_VERSION));
	g_u8_resetflag = false;
	g_u8_cc_ready = false;
	g_u8_checkflag = false;

	g_u8_log_level = LOG_INFO;
	g_u8_i2c_retry_cnt = true;
	g_u8_report_mode = REPORT_WATER_STREAMING_MODE;
}

/*******************************************************************************
 *  Name: raydium_gpio_configure
 *  Brief:
 *  Input:
 *  Output:
 *  Return:
 *******************************************************************************/

static int raydium_gpio_configure(bool on)
{
	int i32_err = 0;

	if (on) {
		if (gpio_is_valid(g_raydium_ts->pdata->irq_gpio)) {
			i32_err = gpio_request(g_raydium_ts->pdata->irq_gpio, "raydium_irq_gpio");
			if (i32_err) {
				LOGE("irq gpio request failed");
				goto err_irq_gpio_req;
			}

			i32_err = gpio_direction_input(g_raydium_ts->pdata->irq_gpio);
			if (i32_err) {
				LOGE("set_direction for irq gpio failed\n");
				goto err_irq_gpio_dir;
			}
		}
		if (gpio_is_valid(g_raydium_ts->pdata->irq2_gpio)) {
			i32_err = gpio_request(g_raydium_ts->pdata->irq2_gpio, "raydium_irq2_gpio");
			if (i32_err) {
				LOGE("irq2 gpio request failed");
				goto err_irq2_gpio_req;
			}

			i32_err = gpio_direction_input(g_raydium_ts->pdata->irq2_gpio);
			if (i32_err) {
				LOGE("set_direction for irq2 gpio failed\n");
				goto err_irq2_gpio_dir;
			}
		}
		if (gpio_is_valid(g_raydium_ts->pdata->reset_gpio)) {
			i32_err = gpio_request(g_raydium_ts->pdata->reset_gpio, "raydium_rst_gpio");
			if (i32_err) {
				LOGE("rst gpio request failed");
				goto err_rst_gpio_req;
			}

			i32_err = gpio_direction_output(g_raydium_ts->pdata->reset_gpio, 1);
			if (i32_err) {
				LOGE("set_direction for rst gpio failed\n");
				goto err_rst_gpio_dir;
			}
		}
	} else {
		if (gpio_is_valid(g_raydium_ts->pdata->irq_gpio))
			gpio_free(g_raydium_ts->pdata->irq_gpio);
		if (gpio_is_valid(g_raydium_ts->pdata->irq2_gpio))
			gpio_free(g_raydium_ts->pdata->irq2_gpio);
		if (gpio_is_valid(g_raydium_ts->pdata->reset_gpio))
			gpio_free(g_raydium_ts->pdata->reset_gpio);
	}
	return 0;

err_rst_gpio_dir:
	if (gpio_is_valid(g_raydium_ts->pdata->reset_gpio))
		gpio_free(g_raydium_ts->pdata->reset_gpio);
err_rst_gpio_req:
err_irq2_gpio_dir:
	if (gpio_is_valid(g_raydium_ts->pdata->irq2_gpio))
		gpio_free(g_raydium_ts->pdata->irq2_gpio);
err_irq2_gpio_req:
err_irq_gpio_dir:
	if (gpio_is_valid(g_raydium_ts->pdata->irq_gpio))
		gpio_free(g_raydium_ts->pdata->irq_gpio);
err_irq_gpio_req:
	return i32_err;
}

/*******************************************************************************
 *  Name: raydium_ts_pinctrl_init
 *  Brief:
 *  Input:
 *  Output:
 *  Return:
 *******************************************************************************/
#ifdef MSM_NEW_VER
static int raydium_ts_pinctrl_init(void)
{
	int i32_ret;

	/* Get pinctrl if target uses pinctrl */
	g_raydium_ts->ts_pinctrl = devm_pinctrl_get(&(g_raydium_ts->client->dev));
	if (IS_ERR_OR_NULL(g_raydium_ts->ts_pinctrl)) {
		i32_ret = PTR_ERR(g_raydium_ts->ts_pinctrl);
		LOGE("target does not use pinctrl %d\n", i32_ret);
		goto err_pinctrl_get;
	}

	g_raydium_ts->pinctrl_state_active
		= pinctrl_lookup_state(g_raydium_ts->ts_pinctrl, PINCTRL_STATE_ACTIVE);
	if (IS_ERR_OR_NULL(g_raydium_ts->pinctrl_state_active)) {
		i32_ret = PTR_ERR(g_raydium_ts->pinctrl_state_active);
		LOGE("Can not lookup %s pinstate %d\n", PINCTRL_STATE_ACTIVE, i32_ret);
		goto err_pinctrl_lookup;
	}

	g_raydium_ts->pinctrl_state_suspend
		= pinctrl_lookup_state(g_raydium_ts->ts_pinctrl,
				       PINCTRL_STATE_SUSPEND);
	if (IS_ERR_OR_NULL(g_raydium_ts->pinctrl_state_suspend)) {
		i32_ret = PTR_ERR(g_raydium_ts->pinctrl_state_suspend);
		LOGE("Can not lookup %s pinstate %d\n", PINCTRL_STATE_SUSPEND, i32_ret);
		goto err_pinctrl_lookup;
	}

	g_raydium_ts->pinctrl_state_release
		= pinctrl_lookup_state(g_raydium_ts->ts_pinctrl,
				       PINCTRL_STATE_RELEASE);
	if (IS_ERR_OR_NULL(g_raydium_ts->pinctrl_state_release)) {
		i32_ret = PTR_ERR(g_raydium_ts->pinctrl_state_release);
		LOGE("Can not lookup %s pinstate %d\n", PINCTRL_STATE_RELEASE, i32_ret);
	}

	return 0;

err_pinctrl_lookup:
	devm_pinctrl_put(g_raydium_ts->ts_pinctrl);
err_pinctrl_get:
	g_raydium_ts->ts_pinctrl = NULL;
	return i32_ret;
}
#endif /*end of MSM_NEW_VER*/
static int raydium_hw_reset_fun(struct i2c_client *client)
{
	int i32_ret = SUCCESS;

	LOGI("HW reset\n");

	g_u8_raydium_flag |= ENG_MODE;
	/*HW reset*/
	gpio_set_value(g_raydium_ts->pdata->reset_gpio, 1);
	gpio_set_value(g_raydium_ts->pdata->reset_gpio, 0);
	msleep(RAYDIUM_RESET_INTERVAL_MSEC);
	gpio_set_value(g_raydium_ts->pdata->reset_gpio, 1);
	msleep(20);

	g_u8_i2c_mode = PDA2_MODE;

	i32_ret = wait_irq_state(client, 300, 2000);
	if (i32_ret != ERROR)
		msleep(35);

	// After triggering reset, need to set up the parameters of edge rejection again
	raydium_setup_edge_rejection_parameters_locked();
	g_u8_raydium_flag &= ~ENG_MODE;

	LOGI("Raydium HW reset : %d\n", i32_ret);
	return i32_ret;
}

static inline int raydium_i2c_transfer_with_constraint(struct i2c_adapter *adap,
						       struct i2c_msg *msgs, int num)
{
	int i32_ret;

	// TODO(b/468471918): We should avoid triggering any i2c_tranfser when we are in offload
	cpu_latency_qos_update_request(&g_raydium_ts->pm_qos_req, CPU_LATENCY_UPPER_BOUND);
	i32_ret = i2c_transfer(adap, msgs, num);
	cpu_latency_qos_update_request(&g_raydium_ts->pm_qos_req, PM_QOS_CPU_LATENCY_DEFAULT_VALUE);

	return i32_ret;
}

int raydium_i2c_write_pda_via_pda2(struct i2c_client *client,
				   unsigned int u32_addr, unsigned char *u8_w_data,
				   unsigned short u16_length)
{
	int i32_ret = -1;
	unsigned char u8_retry;
	unsigned char u8_retry_cnt;
	unsigned char u8_mode = 0x00;
	unsigned char u8_buf[MAX_WRITE_PACKET_SIZE + 6];

	struct i2c_msg msg[] = {
		{
			.addr = RAYDIUM_I2C_NID,
			.flags = RAYDIUM_I2C_WRITE,
			.len = u16_length + 6,
			.buf = u8_buf,
		},
	};
	if (u16_length > MAX_WRITE_PACKET_SIZE)
		return -EINVAL;


	/*I2C access for register need to word mode*/

	if ((u16_length == 4) &&
	    ((u32_addr & 0x50000000) || (u32_addr & 0x40000000)))
		u8_mode = I2C_PDA2_WORD_MODE;
	else
		u8_mode = I2C_PDA2_BYTE_MODE;

	if (u16_length > MAX_WRITE_PACKET_SIZE)
		return -EINVAL;
	g_u8_i2c_mode = PDA2_MODE;

	u8_buf[0] = RAYDIUM_I2C_PDA_CMD;
	u8_buf[1] = (unsigned char)u32_addr;
	u8_buf[2] = (unsigned char)(u32_addr >> 8);
	u8_buf[3] = (unsigned char)(u32_addr >> 16);
	u8_buf[4] = (unsigned char)(u32_addr >> 24);
	u8_buf[5] = u8_mode;

	memcpy(&u8_buf[6], u8_w_data, u16_length);
	if (g_u8_i2c_retry_cnt)
		u8_retry_cnt = SYN_I2C_RETRY_TIMES;
	else
		u8_retry_cnt = 1;

	for (u8_retry = 0; u8_retry < u8_retry_cnt; u8_retry++) {
		if (raydium_i2c_transfer_with_constraint(client->adapter, msg, 1) == 1) {
			i32_ret = u16_length;
			break;
		}
		i32_ret = u16_length;
		usleep_range(500, 1500);
	}

	if (u8_retry == SYN_I2C_RETRY_TIMES) {
		LOGE("%s: I2C write over retry limit\n", __func__);
		i32_ret = -EIO;
	}

	return i32_ret;

}
int raydium_i2c_read_pda_via_pda2(struct i2c_client *client,
				  unsigned int u32_addr, unsigned char *u8_r_data,
				  unsigned short u16_length)
{
	int i32_ret;
	unsigned char u8_mode = 0x00;
	unsigned char u8_buf[6];
	unsigned char u8_retry = 0;
	unsigned char u8_retry_cnt;

	struct i2c_msg msg[] = {
		{
			.addr = RAYDIUM_I2C_NID,
			.flags = RAYDIUM_I2C_WRITE,
			.len = 6,
			.buf = u8_buf,
		},
		{
			.addr = RAYDIUM_I2C_NID,
			.flags = RAYDIUM_I2C_READ,
			.len = u16_length,
			.buf = u8_r_data,
		},
	};

	if ((u32_addr & 0x50000000) || (u32_addr & 0x40000000))
		u8_mode = I2C_PDA2_WORD_MODE;
	else
		u8_mode = I2C_PDA2_BYTE_MODE;

	g_u8_i2c_mode = PDA2_MODE;
	u8_buf[0] = RAYDIUM_I2C_PDA_CMD;
	u8_buf[1] = (unsigned char)u32_addr;
	u8_buf[2] = (unsigned char)(u32_addr >> 8);
	u8_buf[3] = (unsigned char)(u32_addr >> 16);
	u8_buf[4] = (unsigned char)(u32_addr >> 24);
	u8_buf[5] = u8_mode;
	if (g_u8_i2c_retry_cnt)
		u8_retry_cnt = SYN_I2C_RETRY_TIMES;
	else
		u8_retry_cnt = 1;

	for (u8_retry = 0; u8_retry < u8_retry_cnt; u8_retry++) {
		if (raydium_i2c_transfer_with_constraint(g_raydium_ts->client->adapter, msg, 2) ==
		    2) {
			i32_ret = u16_length;
			break;
		}
		i32_ret = u16_length;
		usleep_range(500, 1500);
	}

	if (u8_retry == SYN_I2C_RETRY_TIMES) {
		LOGE("%s: I2C read over retry limit\n", __func__);
		i32_ret = -EIO;
	}

	return i32_ret;
}
int raydium_i2c_pda_set_address(unsigned int u32_address,
				unsigned char u8_mode)
{
	int i32_ret = 0;
	unsigned char u8_retry;
	unsigned char u8_buf[RAD_I2C_PDA_ADDRESS_LENGTH];
	struct i2c_client *client = g_raydium_ts->client;

	client->addr = RAYDIUM_I2C_EID;
	u8_buf[0] = (u32_address & 0x0000FF00) >> 8;
	u8_buf[1] = (u32_address & 0x00FF0000) >> 16;
	u8_buf[2] = (u32_address & 0xFF000000) >> 24;
	u8_buf[3] = u8_mode;

	for (u8_retry = 0; u8_retry < SYN_I2C_RETRY_TIMES; u8_retry++) {
		i32_ret = i2c_master_send(client, u8_buf,
					  RAD_I2C_PDA_ADDRESS_LENGTH);
		if (i32_ret != RAD_I2C_PDA_ADDRESS_LENGTH) {
			LOGE("%s: I2C retry %d\n", __func__, u8_retry + 1);
			usleep_range(500, 1500);
		} else {
			break;
		}
	}

	return (i32_ret == RAD_I2C_PDA_ADDRESS_LENGTH) ? i32_ret : -EIO;
}

/*device attribute raydium_i2c_pda2_mode used*/
int raydium_i2c_pda_read(struct i2c_client *client,
			 unsigned int u32_addr, unsigned char *u8_r_data,
			 unsigned short u16_length)
{
	int i32_ret;
	unsigned char u8_retry;
	unsigned char u8_mode = 0x00;
	unsigned char u8_buf;

	struct i2c_msg msg[] = {
		{
			.addr = RAYDIUM_I2C_NID,
			.flags = RAYDIUM_I2C_WRITE,
			.len = 1,
			.buf = &u8_buf,
		},
		{
			.addr = RAYDIUM_I2C_NID,
			.flags = RAYDIUM_I2C_READ,
			.len = u16_length,
			.buf = u8_r_data,
		},
	};

	if (u16_length == 4)
		u8_mode |= RAD_I2C_PDA_MODE_ENABLE |
			   RAD_I2C_PDA_2_MODE_DISABLE |
			   RAD_I2C_PDA_MODE_WORD_MODE;
	else
		u8_mode |= RAD_I2C_PDA_MODE_ENABLE |
			   RAD_I2C_PDA_2_MODE_DISABLE;

	u8_mode |= 0x03;

	u8_buf = u32_addr & MASK_8BIT;

	i32_ret = raydium_i2c_pda_set_address(u32_addr, u8_mode);
	if (i32_ret != RAD_I2C_PDA_ADDRESS_LENGTH)
		goto exit;
	usleep_range(50, 80);

	for (u8_retry = 0; u8_retry < SYN_I2C_RETRY_TIMES; u8_retry++) {
		if (raydium_i2c_transfer_with_constraint(g_raydium_ts->client->adapter, msg, 2) ==
		    2) {
			i32_ret = u16_length;
			break;
		}
		LOGE("%s: I2C retry %d\n", __func__, u8_retry + 1);
		usleep_range(500, 1500);
	}

	if (u8_retry == SYN_I2C_RETRY_TIMES) {
		LOGE("%s: I2C read over retry limit\n", __func__);
		i32_ret = -EIO;
	}
exit:
	return i32_ret;
}

int raydium_i2c_pda_write(struct i2c_client *client,
			  unsigned int u32_addr, unsigned char *u8_w_data,
			  unsigned short u16_length)
{
	int i32_ret;
	unsigned char u8_retry;
	unsigned char u8_mode = 0x00;
	unsigned char u8_buf[MAX_WRITE_PACKET_SIZE + 1];

	struct i2c_msg msg[] = {
		{
			.addr = RAYDIUM_I2C_NID,
			.flags = RAYDIUM_I2C_WRITE,
			.len = u16_length + 1,
			.buf = u8_buf,
		},
	};

	if (u16_length > MAX_WRITE_PACKET_SIZE)
		return -EINVAL;

	if (u16_length == 4)
		u8_mode |= RAD_I2C_PDA_MODE_ENABLE |
			   RAD_I2C_PDA_2_MODE_DISABLE |
			   RAD_I2C_PDA_MODE_WORD_MODE;
	else
		u8_mode |= RAD_I2C_PDA_MODE_ENABLE |
			   RAD_I2C_PDA_2_MODE_DISABLE;

	u8_buf[0] = u32_addr & MASK_8BIT;
	memcpy(&u8_buf[1], u8_w_data, u16_length);

	i32_ret = raydium_i2c_pda_set_address(u32_addr, u8_mode);
	if (i32_ret != RAD_I2C_PDA_ADDRESS_LENGTH)
		goto exit;
	usleep_range(50, 80);

	for (u8_retry = 0; u8_retry < SYN_I2C_RETRY_TIMES; u8_retry++) {
		if (raydium_i2c_transfer_with_constraint(client->adapter, msg, 1) == 1) {
			i32_ret = u16_length;
			break;
		}
		LOGE("%s: I2C retry %d\n", __func__, u8_retry + 1);
		usleep_range(500, 1500);
	}

	if (u8_retry == SYN_I2C_RETRY_TIMES) {
		LOGE("%s: I2C write over retry limit\n", __func__);
		i32_ret = -EIO;
	}
exit:
	return i32_ret;
}

int handle_i2c_pda_read(struct i2c_client *client,
			unsigned int u32_addr, unsigned char *u8_r_data,
			unsigned short u16_length)
{
	int i32_ret;

	i32_ret = raydium_i2c_read_pda_via_pda2(client, u32_addr, u8_r_data, u16_length);
	if (i32_ret < 0) {
		LOGE("handle_ic_write I2C via_pda2 NG, ret = %d\n", i32_ret);
		return ERROR;
	}
	return SUCCESS;
}

int handle_i2c_pda_write(struct i2c_client *client,
			 unsigned int u32_addr, unsigned char *u8_w_data,
			 unsigned short u16_length)
{
	int i32_ret;

	i32_ret = raydium_i2c_write_pda_via_pda2(client, u32_addr, u8_w_data, u16_length);
	if (i32_ret < 0) {
		LOGE("handle_ic_write I2C via_pda2 NG, ret = %d\n", i32_ret);
		return ERROR;
	}
	return SUCCESS;
}
int raydium_i2c_pda2_set_page(struct i2c_client *client,
			      unsigned int is_suspend,
			      unsigned char u8_page)
{
	int i32_ret = -1;
	unsigned char u8_retry;
	unsigned int u8_ret = (is_suspend) ? 10 : 2;
	unsigned char u8_buf[RAYDIUM_I2C_PDA2_PAGE_LENGTH];

	struct i2c_msg msg[] = {
		{
			.addr = RAYDIUM_I2C_NID,
			.flags = RAYDIUM_I2C_WRITE,
			.len = RAYDIUM_I2C_PDA2_PAGE_LENGTH,
			.buf = u8_buf,
		},
	};

	u8_buf[0] = RAYDIUM_PDA2_PAGE_ADDR;
	u8_buf[1] = u8_page;
	for (; u8_ret > 0; u8_ret--) {
		for (u8_retry = 0; u8_retry < SYN_I2C_RETRY_TIMES; u8_retry++) {
			if (raydium_i2c_transfer_with_constraint(client->adapter, msg, 1) == 1) {
				i32_ret = RAYDIUM_I2C_PDA2_PAGE_LENGTH;
				break;
			}
			usleep_range(500, 1500);
		}
		if (i32_ret == RAYDIUM_I2C_PDA2_PAGE_LENGTH)
			break;
		usleep_range(2000, 5000);
	}

	if (u8_ret == 0) {
		LOGE("%s: I2C write over retry limit\n", __func__);
		i32_ret = -EIO;
	}

	return i32_ret;
}

int raydium_i2c_pda2_read(struct i2c_client *client,
			  unsigned char u8_addr,
			  unsigned char *u8_r_data,
			  unsigned short u16_length)
{
	int i32_ret = -1;
	unsigned char u8_retry;
	unsigned char u8_i2c_retry = SYN_I2C_RETRY_TIMES;

	struct i2c_msg msg[] = {
		{
			.addr = RAYDIUM_I2C_NID,
			.flags = RAYDIUM_I2C_WRITE,
			.len = 1,
			.buf = &u8_addr,
		},
		{
			.addr = RAYDIUM_I2C_NID,
			.flags = RAYDIUM_I2C_READ,
			.len = u16_length,
			.buf = u8_r_data,
		},
	};
	g_u8_i2c_mode = PDA2_MODE;
	if (g_raydium_ts->is_suspend == 1)
		u8_i2c_retry = 50;

	for (u8_retry = 0; u8_retry < u8_i2c_retry; u8_retry++) {
		if (raydium_i2c_transfer_with_constraint(g_raydium_ts->client->adapter, msg, 2) ==
		    2) {
			i32_ret = u16_length;
			break;
		}
		usleep_range(500, 1500);
	}

	if (u8_retry == u8_i2c_retry) {
		LOGE("%s: I2C read over retry limit\n", __func__);
		i32_ret = -EIO;
	}

	return i32_ret;
}

int raydium_i2c_pda2_write(struct i2c_client *client,
			   unsigned char u8_addr,
			   unsigned char *u8_w_data,
			   unsigned short u16_length)
{
	int i32_ret = -1;
	unsigned char u8_retry;
	unsigned char u8_buf[MAX_WRITE_PACKET_SIZE + 1];

	struct i2c_msg msg[] = {
		{
			.addr = RAYDIUM_I2C_NID,
			.flags = RAYDIUM_I2C_WRITE,
			.len = u16_length + 1,
			.buf = u8_buf,
		},
	};

	if (u16_length > MAX_WRITE_PACKET_SIZE)
		return -EINVAL;
	g_u8_i2c_mode = PDA2_MODE;
	u8_buf[0] = u8_addr;
	memcpy(&u8_buf[1], u8_w_data, u16_length);

	for (u8_retry = 0; u8_retry < SYN_I2C_RETRY_TIMES; u8_retry++) {
		if (raydium_i2c_transfer_with_constraint(client->adapter, msg, 1) == 1) {
			i32_ret = u16_length;
			break;
		}
		usleep_range(500, 1500);
	}

	if (u8_retry == SYN_I2C_RETRY_TIMES) {
		LOGE("%s: I2C write over retry limit\n", __func__);
		i32_ret = -EIO;
	}

	return i32_ret;
}

/*
 * Acquire `g_raydium_ts->lock` before calling this
 * function and release it afterwards.
 */
static int raydium_hw_reset_helper_func_locked(struct i2c_client *client)
{
	int i32_ret = SUCCESS;

	LOGI("HW reset\n");
	g_u8_raydium_flag |= ENG_MODE;
	g_u8_i2c_mode = PDA2_MODE;

	/*HW reset*/
	if (gpio_is_valid(g_raydium_ts->pdata->reset_gpio)) {
		gpio_set_value(g_raydium_ts->pdata->reset_gpio, 1);
		gpio_set_value(g_raydium_ts->pdata->reset_gpio, 0);
		msleep(RAYDIUM_RESET_INTERVAL_MSEC);
		gpio_set_value(g_raydium_ts->pdata->reset_gpio, 1);
		msleep(RAYDIUM_RESET_DELAY_MSEC);
	}

	i32_ret = wait_irq_state(client, 1000, 2000);
	if (i32_ret != ERROR)
		msleep(35);

	// After triggering reset, need to set up the parameters of edge rejection again
	raydium_setup_edge_rejection_parameters_locked();
	g_u8_raydium_flag &= ~ENG_MODE;
	g_raydium_ts->is_sleep_bits = 0;

	return i32_ret;
}

int raydium_hw_reset_helper_func(struct i2c_client *client)
{
	int i32_ret = SUCCESS;

	mutex_lock(&g_raydium_ts->lock);
	i32_ret = raydium_hw_reset_helper_func_locked(client);
	mutex_unlock(&g_raydium_ts->lock);
	return i32_ret;
}

/*
 * Acquire `g_raydium_ts->lock` before calling this
 * function and release it afterwards.
 */
static int raydium_i2c_touch_sleep_helper_func_locked(void)
{
	int i32_ret = 0;
	unsigned char u8_wbuffer[RAYDIUM_STB_MAX_DATA_SIZE];
	unsigned int u32_rbuffer;

	// Check if the size is larger than the largest length for w/r
	static_assert(RAYDIUM_STB_MAX_DATA_SIZE >= 4);

	/* MCU hold */
	u8_wbuffer[0] = 0x20;
	i32_ret = handle_i2c_pda_write(g_raydium_ts->client, RAYDIUM_PDA_BOOTREG, u8_wbuffer, 1);
	if (i32_ret < 0) {
		LOGE("Failed to enter STB during MCU hold\n");
		return i32_ret;
	}

	/* SW reset, note that there will be I2C error since the IC is resetting */
	u8_wbuffer[0] = 0x1;
	handle_i2c_pda_write(g_raydium_ts->client, RAYDIUM_PDA_BLKRST, u8_wbuffer, 1);
	msleep(RAYDIUM_RESET_DELAY_MSEC);

	/* Pram close */
	u8_wbuffer[0] = 0xF0;
	u8_wbuffer[1] = 0xF0;
	u8_wbuffer[2] = 0xF0;
	u8_wbuffer[3] = 0xF0;
	i32_ret = handle_i2c_pda_write(g_raydium_ts->client, RAYDIUM_PDA_PRAM, u8_wbuffer, 4);
	if (i32_ret < 0) {
		LOGE("Failed to enter STB during Pram close\n");
		return i32_ret;
	}

	/* Data ram close */
	u8_wbuffer[0] = 0x40;
	i32_ret = handle_i2c_pda_write(g_raydium_ts->client, RAYDIUM_PDA_DATA_RAM, u8_wbuffer, 1);
	if (i32_ret < 0) {
		LOGE("Failed to enter STB during data RAM close\n");
		return i32_ret;
	}

	/* Keep INT high */
	u8_wbuffer[0] = 0x04;
	u8_wbuffer[1] = 0x04;
	u8_wbuffer[2] = 0x00;
	u8_wbuffer[3] = 0x00;
	i32_ret = handle_i2c_pda_write(g_raydium_ts->client, RAYDIUM_PDA_MISCIER, u8_wbuffer, 4);
	if (i32_ret < 0) {
		LOGE("Failed to enter STB when trying to pull INT high\n");
		return i32_ret;
	}

	/* 24M stop */
	u8_wbuffer[0] = 0x02;
	u8_wbuffer[1] = 0x00;
	u8_wbuffer[2] = 0x01;
	u8_wbuffer[3] = 0x00;
	i32_ret = handle_i2c_pda_write(g_raydium_ts->client, RAYDIUM_PDA_24M, u8_wbuffer, 4);
	if (i32_ret < 0) {
		LOGE("Failed to enter STB during 24M stop\n");
		return i32_ret;
	}

	/* Enter STB */
	u8_wbuffer[0] = 0x00;
	u8_wbuffer[1] = 0x01;
	u8_wbuffer[2] = 0x00;
	u8_wbuffer[3] = 0x00;
	i32_ret = handle_i2c_pda_write(g_raydium_ts->client, RAYDIUM_PDA_STB_ADDR, u8_wbuffer, 4);
	if (i32_ret < 0) {
		LOGE("Failed to enter STB\n");
		return i32_ret;
	}

	/* Check result */
	i32_ret = handle_i2c_pda_read(g_raydium_ts->client, RAYDIUM_CHK_I2C_CMD,
				      (unsigned char *)&u32_rbuffer, 4);
	if (i32_ret < 0 || u32_rbuffer == RAD_DEVICE_ID) {
		LOGE("STB mode check failed, ret=%d, value=0x%08X\n", i32_ret, u32_rbuffer);
		return -EIO;
	}

	return i32_ret;
}

/*
 * Acquire `g_raydium_ts->lock` before calling this
 * function and release it afterwards.
 */
int raydium_i2c_touch_sleep_locked(bool enable)
{
	int i32_ret = 0;

	if (enable) {
		/* Send commands to enter touch STB*/
		i32_ret = raydium_i2c_touch_sleep_helper_func_locked();
		if (i32_ret < 0)
			LOGW("Failed to send i2c command to enter touch STB, rc = %d\n", i32_ret);
	} else {
		/* Trigger hardware reset to exit touch STB*/
		i32_ret = raydium_hw_reset_helper_func_locked(g_raydium_ts->client);
		LOGI("Raydium HW Reset : %d\n", i32_ret);
	}

	return i32_ret;
}

/**
 * raydium_irq_control - enable/disable the IRQ and IRQ wake
 * @enable:		Enable or disable
 * @reason:		The reason to enable/disable
 *
 * When any bits are set to 1, the IRQ and IRQ wake will be disabled.
 * Note that not every reason will disable IRQ wake. Also be aware
 * of the sequence when calling with raydium_stb_control. In the
 * normal case, when trying to enter STB mode, disable the IRQ and
 * IRQ wake first. On the other hand, exit STB mode first then enable
 * IRQ and IRQ wake.
 *
 * Both IRQ and IRQ wake are default to be enabled.
 */
void raydium_irq_control(bool enable, enum irq_disable_reason reason)
{
	int i32_ret = 0;
	bool original_is_irq_disabled, original_is_irq_wake_disabled;

	LOGI("%s(%s, %s)\n", __func__, enable ? "enable" : "disable",
	     irq_disable_reason_desc[reason].name);

	// If there are any bits set to 1, the IRQ is disabled
	mutex_lock(&g_raydium_ts->irq_lock);
	original_is_irq_disabled = !!(g_raydium_ts->irq_disable_bits);
	original_is_irq_wake_disabled = !!(g_raydium_ts->irq_wake_disable_bits);

	if (enable) {
		// Only enable IRQ when turning from disabled to enabled
		clear_bit(reason, &g_raydium_ts->irq_disable_bits);
		if (original_is_irq_disabled && !g_raydium_ts->irq_disable_bits) {
			enable_irq(g_raydium_ts->client->irq);
			LOGI("IRQ enabled\n");
		}

		// Clear the IRQ wake bits only if the reason is allowed to do so
		if (!irq_disable_reason_desc[reason].can_disable_irq_wake)
			goto exit;
		clear_bit(reason, &g_raydium_ts->irq_wake_disable_bits);

		// Enable IRQ wake only if not enabled before
		if (!original_is_irq_wake_disabled || !!(g_raydium_ts->irq_wake_disable_bits))
			goto exit;
		i32_ret = enable_irq_wake(g_raydium_ts->client->irq);

		// Revert the operation if failed
		if (i32_ret) {
			set_bit(reason, &g_raydium_ts->irq_wake_disable_bits);
			LOGW("Failed to enable IRQ wake\n");
		} else {
			LOGI("IRQ wake enabled\n");
		}
	} else {
		// Only disable it when turning from enabled to disabled
		set_bit(reason, &g_raydium_ts->irq_disable_bits);
		if (!original_is_irq_disabled) {
			disable_irq(g_raydium_ts->client->irq);
			LOGI("IRQ disabled\n");
		}

		// Set the IRQ wake bits olny if the reason is allowed to do so
		if (!irq_disable_reason_desc[reason].can_disable_irq_wake)
			goto exit;
		set_bit(reason, &g_raydium_ts->irq_wake_disable_bits);

		// Disable IRQ wake only if not disabled before
		if (original_is_irq_wake_disabled)
			goto exit;
		i32_ret = disable_irq_wake(g_raydium_ts->client->irq);

		// Revert the operation if failed
		if (i32_ret) {
			clear_bit(reason, &g_raydium_ts->irq_wake_disable_bits);
			LOGW("Failed to disable IRQ wake\n");
		} else {
			LOGI("IRQ wake disabled\n");
		}
	}

exit:
	mutex_unlock(&g_raydium_ts->irq_lock);
}

/**
 * raydium_stb_control - enable/disable the standby (STB) mode
 * @enable:		Enable to enter, disable to exit the STB mode
 * @reason:		The reason to enable/disable STB mode
 *
 * When any bits are set to 1, the touch IC stays in STB mode. Be
 * aware of the sequence when calling with raydium_irq_control. In
 * the normal case, when trying to enter STB mode, disable the IRQ
 * and IRQ wake first. On the other hand, exit STB mode first then
 * enable IRQ and IRQ wake.
 *
 * Default to be disabled, i.e., not in STB.
 */
void raydium_stb_control(bool enable, enum stb_reason reason)
{
	int i32_ret = 0;
	bool original_is_in_stb;

	// If there are any bits set to 1, the IC is in STB mode
	mutex_lock(&g_raydium_ts->offload_lock);
	mutex_lock(&g_raydium_ts->lock);
	original_is_in_stb = !!(g_raydium_ts->is_sleep_bits);

	// Only allow STB mode if touch screen supports it
	if (!g_raydium_ts->touch_deep_sleep_enable)
		goto exit;
	LOGI("%s(%s, %s)\n", __func__, enable ? "enable" : "disable", stb_reason_desc[reason].name);

	if (enable) {
		set_bit(reason, &g_raydium_ts->is_sleep_bits);
		if (original_is_in_stb)
			goto exit;
		if (g_raydium_ts->offloading) {
			// Sync the STB mode information to MCU
			wti_synchronizer_set_touch_lock(enable);
		} else {
			i32_ret = raydium_i2c_touch_sleep_locked(enable);
		}
		if (i32_ret < 0) {
			LOGE("Failed to enter STB mode\n");
			clear_bit(reason, &g_raydium_ts->is_sleep_bits);
		} else {
			LOGI("STB entry\n");
		}
	} else {
		clear_bit(reason, &g_raydium_ts->is_sleep_bits);
		if (!original_is_in_stb || g_raydium_ts->is_sleep_bits)
			goto exit;

		if (g_raydium_ts->offloading)
			wti_synchronizer_set_touch_lock(enable);

		// TODO b/466948863: Move this function to MCU when we implement Power controller.
		i32_ret = raydium_i2c_touch_sleep_locked(enable);
		if (i32_ret < 0) {
			LOGE("Failed to exit STB mode\n");
			set_bit(reason, &g_raydium_ts->is_sleep_bits);
		} else {
			LOGI("STB exit\n");
		}
	}

exit:
	mutex_unlock(&g_raydium_ts->lock);
	mutex_unlock(&g_raydium_ts->offload_lock);
}

unsigned char raydium_i2c_init(void)
{
	unsigned int u32_buf = 0;
	unsigned char u8_retry = 3;
	unsigned char u8_check = 0;
	unsigned char u8_buf[4];

	while (u8_retry--) {
		u32_buf = 0;
		handle_i2c_pda_read(g_raydium_ts->client, RAYDIUM_CHK_I2C_CMD,
				    (unsigned char *)(&u32_buf), 4);
		if ((u32_buf & 0xFFFF0000) == 0xF3030000)
			u8_check++;
	}
	if (u8_check == 3) {
		LOGI("PDA2 OK\n");
		return SUCCESS;
	}

	g_u8_i2c_mode = PDA_MODE;
	/*Disable PDA*/
	handle_i2c_pda_read(g_raydium_ts->client, RAYDIUM_PDA_I2CREG, u8_buf, 4);
	u8_buf[0] |= RAD_ENABLE_PDA2 | RAD_ENABLE_SI2;
	handle_i2c_pda_write(g_raydium_ts->client, RAYDIUM_PDA_I2CREG, u8_buf, 4);
	raydium_i2c_pda_set_address(0x50000628, DISABLE);

	g_u8_i2c_mode = PDA2_MODE;

	return SUCCESS;
}

#ifdef CONFIG_RM_SYSFS_DEBUG

int raydium_i2c_mode_control(struct i2c_client *client,
			     unsigned char u8_mode)
{
	unsigned char u8_buf[4] = {0};

	/* Case 0 and 1 are deprecated and removed */
	switch (u8_mode) {
	case 2: /* Disable INT by raydium_irq_control */
		raydium_irq_control(DISABLE, IRQ_MODE_CONTROL);
		break;
	case 3: /* Enable INT by raydium_irq_control */
		raydium_irq_control(ENABLE, IRQ_MODE_CONTROL);
		break;
	case 4: /* Show RAD INT depth */
		LOGI("RAD INT depth : %d\n", g_raydium_ts->irq_desc->depth);
		break;
	case 7:
		raydium_i2c_pda2_set_page(client,
					  g_raydium_ts->is_suspend, RAYDIUM_PDA2_2_PDA);
		g_u8_i2c_mode = PDA_MODE;
		LOGI("Disable PDA2_MODE\n");
		break;
	case 8:
		raydium_i2c_pda_read(client, RAYDIUM_PDA_I2CREG, u8_buf, 4);
		u8_buf[0] |= RAD_ENABLE_PDA2 | RAD_ENABLE_SI2;
		raydium_i2c_pda_write(client, RAYDIUM_PDA_I2CREG, u8_buf, 4);
		raydium_i2c_pda_set_address(RAYDIUM_PDA_I2CREG, 0x43);

		g_u8_i2c_mode = PDA2_MODE;
		LOGI("Enable PDA2_MODE\n");
		break;
	default:
		LOGW("Unknown value %d\n", u8_mode);
		break;
	}
	return 0;
}


const struct attribute_group raydium_attr_group = {
	.attrs = raydium_attributes
};

/*create sysfs for debug update firmware*/
static int raydium_create_sysfs(struct i2c_client *client)
{
	int ret = -1;

	ret = sysfs_create_link(g_raydium_ts->input_dev->dev.kobj.parent, &client->dev.kobj,
				"raydium_ts");
	if (ret != 0) {
		LOGE("raydium_ts sysfs create link %s failed. ret=%d", "raydium_ts", ret);
		return ret;
	}

	ret = sysfs_create_group(&(client->dev.kobj), &raydium_attr_group);
	if (ret) {
		LOGE("failed to register sysfs\n");
		sysfs_remove_group(&client->dev.kobj, &raydium_attr_group);
		ret = -EIO;
	} else {
		LOGI("create raydium sysfs attr_group successful\n");
	}
	return ret;
}

static void raydium_release_sysfs(struct i2c_client *client)
{
	sysfs_remove_group(&client->dev.kobj, &raydium_attr_group);
}

static void raydium_dump_image_data(short *p_image_buf, unsigned char buf_size,
				    unsigned char x_channel, unsigned char y_channel)
{
	char buf[HEATMAP_DATA_BUFFER_SIZE] = { 0 };
	unsigned char u8_j, u8_i;
	unsigned int buf_offset = 0;

	if (x_channel == 0 || y_channel == 0 || x_channel * y_channel > buf_size) {
		LOGE("%s: dump data failed, buf_size:%d, x_channel:%d, y_channel:%d\n", __func__,
		     buf_size, x_channel, y_channel);
		return;
	}
	capturer_insert_grid_data(&wti->capturer, p_image_buf, buf_size);
	for (u8_j = 0; u8_j < y_channel; u8_j++) {
		buf_offset = 0;
		for (u8_i = 0; u8_i < x_channel; u8_i++)
			buf_offset += scnprintf(buf + buf_offset,
						HEATMAP_DATA_BUFFER_SIZE - buf_offset, "%4d, ",
						p_image_buf[u8_i + u8_j * x_channel]);
		LOGD("%s\n", buf);
	}
	LOGD("\n");
}
unsigned int raydium_rc_checksum32(int *buf, unsigned int u32_len)
{
	unsigned int u32_i = 0, u8_j;

	for (u8_j = 0; u8_j < u32_len; u8_j++)
		u32_i += buf[u8_j];

	return u32_i;
}

#endif /*end of CONFIG_RM_SYSFS_DEBUG*/

static void raydium_parse_error_flag(unsigned char error_flag)
{
	switch (error_flag & TOUCH_INT_TIMEOUT_MASK) {
	case TOUCH_NO_TIMEOUT_FLAG:
		break;
	case TOUCH_DOWN_MOVE_TIMEOUT_FLAG:
		LOGE("Down/Move Timeout\n");
		break;
	case TOUCH_TP_UP_TIMEOUT_FLAG:
		LOGE("TP Up Timeout\n");
		break;
	case TOUCH_WAKEUP_TIMEOUT_FLAG:
		LOGE("Wakeup Timeout\n");
		break;
	}

	switch (error_flag & TOUCH_TOUCH_MODE_MASK) {
	case TOUCH_ACTIVE_FLAG:
		LOGI("Touch Mode Active\n");
		break;
	case TOUCH_IDLE_FLAG:
		LOGI("Touch Mode Idle\n");
		break;
	case TOUCH_GESTURE_FLAG:
		LOGI("Touch Mode Gesture\n");
		break;
	case TOUCH_OTHERS_FLAG:
		LOGE("Touch Mode Others\n");
		break;
	}

	switch (error_flag & TOUCH_FW_STATE_MASK) {
	case TOUCH_NORMAL_STATE_FLAG:
		LOGI("FW State Normal\n");
		break;
	case TOUCH_INITIAL_STATE_FLAG:
		LOGI("FW State Initial\n");
		break;
	default:
		LOGE("FW State Unknown\n");
		break;
	}
}

static void raydium_parse_error_report(unsigned char *error_report)
{
	unsigned char u8_report_byte;

	u8_report_byte = error_report[POS_INT_TIMEOUT];
	if (u8_report_byte) {
		LOGE("INT Timeout:\n");
		if (u8_report_byte & TOUCH_INT_TIMEOUT_DOWN)
			LOGE("\tDown\n");
		if (u8_report_byte & TOUCH_INT_TIMEOUT_MOVE)
			LOGE("\tMove\n");
		if (u8_report_byte & TOUCH_INT_TIMEOUT_UP)
			LOGE("\tUp\n");
		if (u8_report_byte & TOUCH_INT_TIMEOUT_GESTURE)
			LOGE("\tGesture\n");
	}

	LOGI("Touch Count: %d\n", error_report[POS_TOUCH_COUNT]);
	LOGE("Timeout Count: %d\n", error_report[POS_TIMEOUT_COUNT]);

	u8_report_byte = error_report[POS_ALG_STATUS];
	if (u8_report_byte) {
		LOGI("ALG Status:\n");
		if (u8_report_byte & TOUCH_ALG_STATUS_WATER_TAG)
			LOGI("\tWater\n");
		if (u8_report_byte & TOUCH_ALG_STATUS_PALM_TAG)
			LOGI("\tPalm\n");
		if (u8_report_byte & TOUCH_ALG_STATUS_GESTURE_TAG)
			LOGI("\tGesture\n");
		if (u8_report_byte & TOUCH_ALG_STATUS_REBOOT_TAG)
			LOGI("\tReboot\n");
	}

	u8_report_byte = error_report[POS_BASELINE_STATUS];
	if (u8_report_byte & ~TOUCH_BASELINE_STATUS_BS_INFO) {
		LOGI("Baseline Status:\n");
		if (u8_report_byte & TOUCH_BASELINE_STATUS_INITIAL)
			LOGI("\tInitial\n");
		if (u8_report_byte & TOUCH_BASELINE_STATUS_CALIBRATION)
			LOGI("\tCalibration\n");
		if (u8_report_byte & TOUCH_BASELINE_STATUS_NOT_YET)
			LOGI("\tNot Yet\n");
		if (u8_report_byte & TOUCH_BASELINE_STATUS_DONE)
			LOGI("\tDone\n");
	}
	LOGI("BS Info: %d\n", (u8_report_byte & TOUCH_BASELINE_STATUS_BS_INFO));

	u8_report_byte = error_report[POS_NOISE_STATUS];
	LOGI("NS Tag: %d\n", (u8_report_byte & TOUCH_NOISE_STATUS_NS_TAG));
	LOGI("NS Info: %d\n", (u8_report_byte & TOUCH_NOISE_STATUS_NS_INFO));
	LOGI("NS Count: %d\n", (u8_report_byte & TOUCH_NOISE_STATUS_NS_COUNT));

	u8_report_byte = error_report[POS_POWER_STATUS] & TOUCH_POWER_STATUS_TOUCH_MODE;
	switch (u8_report_byte) {
	case TOUCH_POWER_TOUCH_ACTIVE:
		LOGI("Touch Mode: Active\n");
		break;
	case TOUCH_POWER_TOUCH_IDLE:
		LOGI("Touch Mode: Idle\n");
		break;
	case TOUCH_POWER_TOUCH_GESTURE:
		LOGI("Touch Mode: Gesture\n");
		break;
	case TOUCH_POWER_TOUCH_OTHERS:
		LOGI("Touch Mode: Others\n");
		break;
	default:
		LOGE("Touch Mode: Unknown\n");
		break;
	}

	u8_report_byte = error_report[POS_POWER_STATUS] & TOUCH_POWER_STATUS_DISPLAY_MODE;
	switch (u8_report_byte) {
	case TOUCH_POWER_DISPLAY_ON:
		LOGI("Display Mode: On\n");
		break;
	case TOUCH_POWER_DISPLAY_IDLE:
		LOGI("Display Mode: Idle\n");
		break;
	case TOUCH_POWER_DISPLAY_SLEEP_OUT:
		LOGI("Display Mode: Sleep Out\n");
		break;
	case TOUCH_POWER_DISPLAY_SLEEP_IN:
		LOGI("Display Mode: Sleep in\n");
		break;
	case TOUCH_POWER_DISPLAY_DSTB:
		LOGI("Display Mode: DSTB\n");
		break;
	default:
		LOGE("Display Mode: Unknown\n");
		break;
	}

	u8_report_byte = error_report[POS_SCAN_STATUS];
	if (u8_report_byte & 0x03) {
		LOGI("Scan Status:\n");
		if (u8_report_byte & TOUCH_SCAN_STATUS_START)
			LOGI("\tStart\n");
		if (u8_report_byte & TOUCH_SCAN_STATUS_STOP)
			LOGI("\tStop\n");
	}
	if (u8_report_byte & TOUCH_SCAN_STATUS_HV_FLAG)
		LOGI("Scan HV Flag\n");
	if (u8_report_byte & TOUCH_SCAN_STATUS_AUTO_FLAG)
		LOGI("Scan Auto Flag\n");
	LOGI("Scan Type: %d\n", (error_report[POS_SCAN_STATUS] & TOUCH_SCAN_STATUS_TYPE));
}

static inline bool raydium_ts_fw_has_error(unsigned char tp_status)
{
	return (tp_status & TOUCH_FW_STATE_MASK) != TOUCH_NORMAL_STATE_FLAG;
}

static int raydium_esd_check_locked(void)
{
	int i32_ret = 0;
	unsigned char u8_esd_status[MAX_TCH_STATUS_PACKET_SIZE];
	unsigned char u8_buf[4];

	if (!g_raydium_ts->esd_protect_enable)
		return i32_ret;

	if (g_u8_i2c_mode == PDA2_MODE) {
		/*read esd status*/
		i32_ret = handle_i2c_pda_read(g_raydium_ts->client, g_raydium_ts->esd_status_addr,
					      u8_esd_status, MAX_TCH_STATUS_PACKET_SIZE);
		if (i32_ret < 0) {
			LOGE("%s: failed to read data: %d\n", __func__, __LINE__);
			goto exit;
		}

		if (raydium_ts_fw_has_error(u8_esd_status[POS_FW_STATE])) {
			handle_i2c_pda_read(g_raydium_ts->client, RAYDIUM_PDA_BOOTREG, u8_buf, 4);
			if (g_u8_resetflag == true || u8_buf[1] == 0x40) {
				LOGE("%s -> filter abnormal irq\n", __func__);
				g_u8_resetflag = false;
				goto exit;
			}
			LOGE("%s -> abnormal irq, FW state = 0x%x\n", __func__,
			     u8_esd_status[POS_FW_STATE]);
			raydium_parse_error_flag(u8_esd_status[POS_FW_STATE]);
			i32_ret = -1;
			goto exit;
		}
		g_u8_resetflag = false;
	}
exit:
	LOGI("%s\n", __func__);
	return i32_ret;
}

int raydium_esd_check(void)
{
	int i32_ret;

	mutex_lock(&g_raydium_ts->lock);
	i32_ret = raydium_esd_check_locked();
	mutex_unlock(&g_raydium_ts->lock);
	return i32_ret;
}

int32_t raydium_set_offload(bool enable, enum offload_request_source source)
{
	// Early return if the offload feature is not enabled
	if (!g_raydium_ts->offload_enable) {
		LOGW("offload feature is not enabled\n");
		return -EOPNOTSUPP;
	}

	if (!g_raydium_ts->finish_fw_update) {
		LOGW("Give up this offload request due to fw update is not finished\n");
		return -EBUSY;
	}
	LOGI("Source %s requested switching control to %s\n", get_offload_source_str(source),
	     enable ? "MCU" : "AP");
	mutex_lock(&g_raydium_ts->offload_lock);
	g_raydium_ts->offloading = enable;

	/* When we start to offload to MCU from the request of arbitration module,
	 * turn on irq2 for touch watchdog
	 */
	if (source == ARBITRATION_SERVICE)
		raydium_irq2_control(enable, TOUCH_WATCHDOG);

	raydium_irq_control(!enable, TOUCH_OFFLOAD);

	/* Send the info to ask for offloading to MCU
	 * This may be the request from touch service or user
	 * Note: manual_cmd doesn't ensure the control bus is on which master
	 */
	if (source == TOUCH_SERVICE || source == MANUAL_CMD)
		wti_mcu_send_offload_settings(enable);

	// If we already offloaded to MCU, sync up the information to MCU
	if (enable)
		wti_synchronizer_on_sync();
	mutex_unlock(&g_raydium_ts->offload_lock);
	return 0;
}

static inline void raydium_display_change_mode_locked(enum raydium_ts_display_power_state state)
{
	g_display_data.power_state = state;
}

static void raydium_display_change_mode(enum raydium_ts_display_power_state state)
{
	mutex_lock(&g_display_data.lock);
	raydium_display_change_mode_locked(state);
	mutex_unlock(&g_display_data.lock);
}

// Acquire `g_raydium_ts->lock` before calling this function
static int raydium_touch_report_motion_locked(struct raydium_report_finger_data *report_finger_data)
{
	unsigned char u8_i;
	/* number of touch points */
	unsigned char u8_touch_count = 0;

	mutex_lock(&g_display_data.lock);
	// Wake up device if we receive touch motion events under false interactive
	if (g_display_data.power_state == DISPLAY_OFF ||
	    g_display_data.power_state == DISPLAY_AOD) {
		LOGI("Device woke due to touch events under false interactive\n");
		if (g_display_data.power_state == DISPLAY_OFF)
			switch_driver_report_mode(QUEUEING);
		raydium_send_key(g_raydium_ts->input_dev, KEY_WAKEUP);
		raydium_ts_display_resume_locked();
		raydium_display_change_mode_locked(DISPLAY_INTERACTIVE);
	}
	mutex_unlock(&g_display_data.lock);

	mutex_lock(&g_raydium_ts->report_input_event_lock);
	// Report timestamp of the motion event
	g_raydium_ts->report_input_event(EV_TIMESTAMP, 0, 0, g_raydium_ts->timestamp);
	for (u8_i = 0; u8_i < report_finger_data->u8_touch_points; u8_i++) {
		/* skip if the reported position is out of range */
		if (report_finger_data->pt[u8_i].i16_x_p >= g_raydium_ts->pdata->x_max ||
		    report_finger_data->pt[u8_i].i16_y_p >= g_raydium_ts->pdata->y_max) {
			LOGW("Reported position (%d, %d) out of range (%d, %d).",
			     report_finger_data->pt[u8_i].i16_x_p,
			     report_finger_data->pt[u8_i].i16_y_p, g_raydium_ts->pdata->x_max,
			     g_raydium_ts->pdata->y_max);
			atomic_inc(&g_raydium_ts->stats.out_of_range_count);
			continue;
		}
		if (report_finger_data->pt[u8_i].u8_touch_status == TOUCH_PRESS ||
		    report_finger_data->pt[u8_i].u8_touch_status == TOUCH_MOVE) {
			atomic_inc(&g_raydium_ts->stats.motion_count);
			g_raydium_ts->report_input_event(EV_ABS, ABS_MT_SLOT,
							 report_finger_data->pt[u8_i].u8_id, 0);
			g_raydium_ts->report_input_event(EV_SLOT_STATE, MT_TOOL_FINGER, true, 0);
			g_raydium_ts->report_input_event(EV_ABS, ABS_MT_POSITION_X,
							 report_finger_data->pt[u8_i].i16_x_p, 0);
			g_raydium_ts->report_input_event(EV_ABS, ABS_MT_POSITION_Y,
							 report_finger_data->pt[u8_i].i16_y_p, 0);
			g_raydium_ts->report_input_event(
				EV_ABS, ABS_MT_TOUCH_MAJOR,
				max(report_finger_data->pt[u8_i].i8_x_shape,
				    report_finger_data->pt[u8_i].i8_y_shape),
				0);
			g_raydium_ts->report_input_event(
				EV_ABS, ABS_MT_TOUCH_MINOR,
				min(report_finger_data->pt[u8_i].i8_x_shape,
				    report_finger_data->pt[u8_i].i8_y_shape),
				0);
			g_raydium_ts->report_input_event(EV_ABS, ABS_MT_PRESSURE,
							 report_finger_data->pt[u8_i].i16_z_w, 0);
			capturer_insert_touch_coordinates(&wti->capturer,
							  report_finger_data->pt[u8_i].i16_x_p,
							  report_finger_data->pt[u8_i].i16_y_p,
							  u8_i);
			u8_touch_count++;
		} else if (report_finger_data->pt[u8_i].u8_touch_status == TOUCH_RELEASE) {
			atomic_inc(&g_raydium_ts->stats.release_count);
			g_raydium_ts->report_input_event(EV_ABS, ABS_MT_SLOT,
							 report_finger_data->pt[u8_i].u8_id, 0);
			g_raydium_ts->report_input_event(EV_SLOT_STATE, MT_TOOL_FINGER, false, 0);
		} else
			LOGW("Received unknown touch status: %d\n",
			     report_finger_data->pt[u8_i].u8_touch_status);
	}

	g_raydium_ts->report_input_event(EV_KEY, BTN_TOUCH, u8_touch_count > 0, 0);
	g_raydium_ts->report_input_event(EV_KEY, BTN_TOOL_FINGER, u8_touch_count > 0, 0);
	g_raydium_ts->report_input_event(EV_SYN, SYN_REPORT, 0, 0);
	mutex_unlock(&g_raydium_ts->report_input_event_lock);

	capturer_insert_num_touches(&wti->capturer, u8_touch_count);
	return 0;
}
static int raydium_read_touch_sensing_data_locked(uint8_t *heatmap_data)
{
	int i32_ret = 0;
	unsigned char u8_buf[ACK_PACKET_SIZE_FOR_STREAMING] = { 0 };

	if (g_u8_report_mode == REPORT_NORMAL_MODE) {
		LOGD("%s: Normal mode doesn't support dumping sensing data\n", __func__);
		return 0;
	}
	// Read heatmap data for water streaming mode by i2c transaction and dump those data
	i32_ret = raydium_i2c_pda2_read(g_raydium_ts->client, RAYDIUM_PDA2_RAWDATA_ADDR,
					(unsigned char *)(heatmap_data), HEATMAP_DATA_BUFFER_SIZE);
	if (i32_ret < 0) {
		LOGE("%s: failed to read data: %d\n", __func__, __LINE__);
		return i32_ret;
	}
	// Send Ack to Touch IC
	i32_ret = raydium_i2c_pda2_write(g_raydium_ts->client, RAYDIUM_PDA2_TCH_RPT_STATUS_ADDR,
					 u8_buf, ACK_PACKET_SIZE_FOR_STREAMING);

	return i32_ret;
}

static inline bool raydium_ts_has_error(unsigned char tp_status)
{
	return (tp_status & TOUCH_INT_TIMEOUT_MASK) != 0;
}

static bool raydium_check_fw_ready(void)
{
	int i32_ret = 0;
	unsigned char u8_buf[4];

	/* Check if touch FW is ready */
	i32_ret = handle_i2c_pda_read(g_raydium_ts->client, RAYDIUM_PDA_BOOTREG, u8_buf, 4);
	if (i32_ret < 0 || u8_buf[1] != 0x40) {
		LOGW("%s -> filter irq due to touch FW is not ready, u8_buf=%hhu, rc=%d\n",
		     __func__, u8_buf[1], i32_ret);
		return false;
	}

	return true;
}

int raydium_adjust_edge_rejection_locked(enum raydium_edge_rejection_parameter_types type,
					 unsigned char value)
{
	int i32_ret = 0;
	unsigned char u8_wdata[4] = { 0 };

	/* Early return if edge rejection is not configured to adjust the customized data*/
	if (!g_raydium_ts->edge_rejection_data.enable) {
		LOGI("Customized edge rejection is disabled, request type: %d, value %u", type,
		     value);
		return 0;
	}

	g_raydium_ts->edge_rejection_data.parameters[type] = value;
	u8_wdata[0] = edge_rejection_commands[type];
	u8_wdata[2] = value;

	LOGI("edge rejection: write %d to 0x%02X register\n", value, u8_wdata[0]);
	i32_ret = raydium_i2c_pda2_write(g_raydium_ts->client, RAYDIUM_PDA2_HOST_CMD_ADDR, u8_wdata,
					 4);
	if (i32_ret < 0)
		LOGE("Failed to change the parameters of edge rejection, %d to 0x%02X register",
		     value, u8_wdata[0]);
	return i32_ret;
}

static void raydium_setup_edge_rejection_parameters_locked(void)
{
	enum raydium_edge_rejection_parameter_types type;

	/* Early return if edge rejection is not configured to adjust the customized data*/
	if (!g_raydium_ts->edge_rejection_data.enable) {
		LOGI("Customized edge rejection is disabled");
		return;
	}

	for (type = 0; type < PARAMETERS_MAX; type++)
		raydium_adjust_edge_rejection_locked(
			type, g_raydium_ts->edge_rejection_data.parameters[type]);
}

static int raydium_check_esd_reset_locked(uint8_t u8_error_state)
{
	uint8_t u8_i;

	/* Early return because esd prorection is not enabled*/
	if (!g_raydium_ts->esd_protect_enable)
		return 0;

	// check whether this interrupt is caused by ESD reset, which will not be
	// in normal state
	if (raydium_ts_fw_has_error(u8_error_state)) {
		atomic_inc(&g_raydium_ts->stats.reset_count);
		msleep(g_raydium_ts->pdata->hard_rst_dly);
		if (raydium_check_fw_ready() || g_u8_resetflag) {
			LOGI("%s -> filter irq due to reset, FW state = 0x%x\n", __func__,
			     u8_error_state);
			/* Send the parameters of edge rejection again due to HW reset*/
			raydium_setup_edge_rejection_parameters_locked();
			g_u8_resetflag = false;
		} else {
			LOGE("%s -> abnormal irq, FW state = 0x%x\n", __func__, u8_error_state);
			/* release all touches*/
			for (u8_i = 0; u8_i < g_raydium_ts->pdata->num_max_touches; u8_i++) {
				input_mt_slot(g_raydium_ts->input_dev, u8_i);
				input_mt_report_slot_state(g_raydium_ts->input_dev, MT_TOOL_FINGER,
							   false);
			}
			input_mt_report_pointer_emulation(g_raydium_ts->input_dev, false);
			input_sync(g_raydium_ts->input_dev);
			raydium_hw_reset_helper_func_locked(g_raydium_ts->client);
		}
		raydium_parse_error_flag(u8_error_state);
		return -EFAULT;
	}
	return 0;
}

static void raydium_check_error_flag_locked(struct raydium_report_touch_data *report_touch_data)
{
	/* Check Error flag */
	if (raydium_ts_has_error(report_touch_data->touch_points.u8_error_state)) {
		LOGE("%s, Error_Flag = 0x%x\n", __func__,
		     report_touch_data->touch_points.u8_error_state);
		raydium_parse_error_flag(report_touch_data->touch_points.u8_error_state);
		LOGE("Error_Report: %02X%02X%02X%02X%02X%02X%02X%02X\n",
		     ((unsigned char *)&report_touch_data->error_status)[7],
		     ((unsigned char *)&report_touch_data->error_status)[6],
		     ((unsigned char *)&report_touch_data->error_status)[5],
		     ((unsigned char *)&report_touch_data->error_status)[4],
		     ((unsigned char *)&report_touch_data->error_status)[3],
		     ((unsigned char *)&report_touch_data->error_status)[2],
		     ((unsigned char *)&report_touch_data->error_status)[1],
		     ((unsigned char *)&report_touch_data->error_status)[0]);
		raydium_parse_error_report((unsigned char *)&report_touch_data->error_status);
	}
}

// Acquire `g_raydium_ts->lock` before calling this function
int raydium_read_touchdata_locked(struct raydium_report_touch_data *report_touch_data)
{
	int i32_ret = 0;
	/*
	 * A touch report consists of 3 parts,
	 *   1. Report status
	 *   2. Data of first touch point
	 *   3. Data of second touch point
	 *
	 * Always read all status and two points' data even if only one finger is detected
	 * since we need the byte from 1st finger and 2nd finger to get frame index data.
	 */
	const unsigned char u8_read_size = LEN_STATUS_SIZE + MAX_TOUCH_NUM * LEN_PT_SIZE;
	struct raydium_report_finger_data *report_finger_data = &report_touch_data->touch_points;

	static_assert(u8_read_size <= MAX_REPORT_PACKET_SIZE);
	static_assert(u8_read_size <= sizeof(struct raydium_report_finger_data));

	i32_ret = raydium_i2c_pda2_set_page(g_raydium_ts->client, g_raydium_ts->is_suspend,
					    RAYDIUM_PDA2_PAGE_0);
	if (i32_ret < 0) {
		LOGE("%s: failed to set page in work_pending\n", __func__);
		atomic_inc(&g_raydium_ts->stats.fw_i2c_error_count);
		goto exit_error;
	}

	/*read touch point information*/
	i32_ret = raydium_i2c_pda2_read(g_raydium_ts->client, RAYDIUM_PDA2_TCH_RPT_STATUS_ADDR,
					(unsigned char *)report_finger_data, u8_read_size);
	if (i32_ret < 0) {
		LOGE("%s: failed to read data: %d\n", __func__, __LINE__);
		atomic_inc(&g_raydium_ts->stats.fw_i2c_error_count);
		goto exit_error;
	}

	/*read error status*/
	i32_ret = raydium_i2c_pda2_read(g_raydium_ts->client, RAYDIUM_PDA2_ERROR_RPT_ADDR,
					(unsigned char *)&report_touch_data->error_status,
					sizeof(struct raydium_report_error_status));
	if (i32_ret < 0) {
		LOGE("%s: failed to read error report: %d\n", __func__, __LINE__);
		atomic_inc(&g_raydium_ts->stats.fw_i2c_error_count);
		goto exit_error;
	}

	/* Check if touch FW is ready
	 * This condition is to filter the false interrupt when touch FW is not ready
	 * to prepare the "p_u8_tp_status" data such that we cannot know whether it's
	 * an interrupt triggered by reset.
	 */
	if (!raydium_check_fw_ready()) {
		atomic_inc(&g_raydium_ts->stats.fw_i2c_error_count);
		i32_ret = -EFAULT;
		goto exit_error;
	}

	i32_ret = raydium_read_touch_sensing_data_locked(report_touch_data->heatmap_data);
	if (i32_ret < 0) {
		LOGE("%s: failed to dump touch sensing data\n", __func__);
		goto exit_error;
	}

exit_error:
	return i32_ret;
}

// Acquire `g_raydium_ts->lock` before calling this function
int raydium_read_touchdata_wrapping_locked(struct raydium_report_touch_data *report_touch_data)
{
	int i32_ret = 0;
	unsigned char u8_buf[ACK_PACKET_SIZE_FOR_STREAMING] = { 0 };

	/* Read all data at once */
	i32_ret = raydium_i2c_pda2_read(g_raydium_ts->client, RAYDIUM_PDA2_TCH_RPT_STATUS_ADDR,
					(unsigned char *)report_touch_data,
					sizeof(struct raydium_report_touch_data));
	if (i32_ret < 0) {
		LOGE("%s: failed to read data: %d\n", __func__, __LINE__);
		atomic_inc(&g_raydium_ts->stats.fw_i2c_error_count);
		goto exit_error;
	}

	// Send ACK to Touch IC
	i32_ret = raydium_i2c_pda2_write(g_raydium_ts->client, RAYDIUM_PDA2_TCH_RPT_STATUS_ADDR,
					 u8_buf, ACK_PACKET_SIZE_FOR_STREAMING);

exit_error:

	return i32_ret;
}

static void report_touch_stats(void)
{
	unsigned int touch_event_sum, gesture_event_sum, water_mode_event;
	struct raydium_ts_touch_stats local;

	local = g_raydium_ts->stats;
	memset(&g_raydium_ts->stats, 0, sizeof(struct raydium_ts_touch_stats));

	if (local.event_count.counter) {
		touch_event_sum = local.motion_count.counter + local.release_count.counter;
		gesture_event_sum = local.palm_count.counter + local.wakeup_count.counter +
				    local.swipe_left_count.counter +
				    local.swipe_right_count.counter + local.swipe_up_count.counter +
				    local.swipe_down_count.counter;
		water_mode_event =
			local.water_mode_enter_count.counter + local.water_mode_exit_count.counter;

		LOG_STAT("Total events: %u\n", local.event_count.counter);
		if (local.fw_i2c_error_count.counter)
			LOG_STAT("\t%u fw_i2c_error\n", local.fw_i2c_error_count.counter);
		if (local.wrong_finger_num_count.counter)
			LOG_STAT("\t%u wrong_finger_num\n", local.wrong_finger_num_count.counter);
		if (local.empty_report_count.counter)
			LOG_STAT("\t%u empty_report\n", local.empty_report_count.counter);
		if (local.suppress_count.counter)
			LOG_STAT("\t%u suppress\n", local.suppress_count.counter);
		if (local.reset_count.counter)
			LOG_STAT("\t%u reset\n", local.reset_count.counter);
		if (local.seq_num_dup_count.counter)
			LOG_STAT("\t%u duplicate\n", local.seq_num_dup_count.counter);
		if (touch_event_sum)
			LOG_STAT("\t%u touch\n", touch_event_sum);
		if (local.motion_count.counter)
			LOG_STAT("\t\t%u motion\n", local.motion_count.counter);
		if (local.release_count.counter)
			LOG_STAT("\t\t%u release\n", local.release_count.counter);
		if (local.out_of_range_count.counter)
			LOG_STAT("\t%u out_of_range\n", local.out_of_range_count.counter);
		if (gesture_event_sum)
			LOG_STAT("\t%u gesture\n", gesture_event_sum);
		if (local.palm_count.counter)
			LOG_STAT("\t\t%u palm\n", local.palm_count.counter);
		if (local.wakeup_count.counter)
			LOG_STAT("\t\t%u wakeup\n", local.wakeup_count.counter);
		if (local.swipe_left_count.counter)
			LOG_STAT("\t\t%u swipe_left\n", local.swipe_left_count.counter);
		if (local.swipe_right_count.counter)
			LOG_STAT("\t\t%u swipe_right\n", local.swipe_right_count.counter);
		if (local.swipe_up_count.counter)
			LOG_STAT("\t\t%u swipe_up\n", local.swipe_up_count.counter);
		if (local.swipe_down_count.counter)
			LOG_STAT("\t\t%u swipe_down\n", local.swipe_down_count.counter);
		if (water_mode_event)
			LOG_STAT("\t%u water\n", water_mode_event);
		if (local.water_mode_enter_count.counter)
			LOG_STAT("\t\t%u water_mode_enter\n", local.water_mode_enter_count.counter);
		if (local.water_mode_exit_count.counter)
			LOG_STAT("\t\t%u water_mode_exit\n", local.water_mode_exit_count.counter);
		if (local.ignored_count.counter)
			LOG_STAT("%u ignored due to re-entry\n", local.ignored_count.counter);
		if (local.mcu_event_count.counter)
			LOG_STAT("\t%u mcu events\n", local.mcu_event_count.counter);
		LOG_STAT("Time: %u ms (last was %u ms ago)\n",
			 jiffies_to_msecs((long)local.last.counter - (long)local.first.counter),
			 jiffies_to_msecs((long)jiffies - (long)local.last.counter));
	}
}

static void stats_timer_func(struct timer_list *arg)
{
	report_touch_stats();
}

static void raydium_send_key(struct input_dev *dev, uint32_t keycode)
{
	input_report_key(dev, keycode, true);
	input_sync(dev);
	input_report_key(dev, keycode, false);
	input_sync(dev);
}
static void raydium_report_one_tap_gesture(struct raydium_report_finger_data *report_touch_data)
{
	LOGI("Tap to Wake, coordinates (%d,%d) instead of keycode.\n",
	     report_touch_data->pt[0].i16_x_p, report_touch_data->pt[0].i16_y_p);
	/* Send finger down */
	input_mt_slot(g_raydium_ts->input_dev, 0);
	input_mt_report_slot_state(g_raydium_ts->input_dev, MT_TOOL_FINGER, true);
	input_report_abs(g_raydium_ts->input_dev, ABS_MT_POSITION_X,
			 report_touch_data->pt[0].i16_x_p);
	input_report_abs(g_raydium_ts->input_dev, ABS_MT_POSITION_Y,
			 report_touch_data->pt[0].i16_y_p);
	/* Placeholder pressure value, without this the touch is interpreted as a hover. */
	input_report_abs(g_raydium_ts->input_dev, ABS_MT_PRESSURE, 0x300);
	input_mt_sync_frame(g_raydium_ts->input_dev);
	input_sync(g_raydium_ts->input_dev);
	/* Send finger up */
	input_mt_slot(g_raydium_ts->input_dev, 0);
	input_report_abs(g_raydium_ts->input_dev, ABS_MT_TOUCH_MAJOR, 0);
	input_mt_report_slot_state(g_raydium_ts->input_dev, MT_TOOL_FINGER, false);
	input_mt_sync_frame(g_raydium_ts->input_dev);
	input_sync(g_raydium_ts->input_dev);
}
static void raydium_report_gesture(struct raydium_report_touch_data *report_touch_data)
{
	unsigned char u8_i;
	struct raydium_report_finger_data *report_finger_data = &report_touch_data->touch_points;
	struct raydium_report_gesture_info *report_gesture_info = &report_touch_data->gesture_info;

	// Report timestamp of the gesture event
	mutex_lock(&g_raydium_ts->report_input_event_lock);
	g_raydium_ts->report_input_event(EV_TIMESTAMP, 0, 0, g_raydium_ts->timestamp);
	mutex_unlock(&g_raydium_ts->report_input_event_lock);

	switch (report_finger_data->u8_gesture_state) {
	case RAD_PALM_ENABLE: {
		/* release all touches*/
		for (u8_i = 0; u8_i < g_raydium_ts->pdata->num_max_touches; u8_i++) {
			input_mt_slot(g_raydium_ts->input_dev, u8_i);
			input_mt_report_slot_state(g_raydium_ts->input_dev, MT_TOOL_FINGER, false);
		}
		input_mt_report_pointer_emulation(g_raydium_ts->input_dev, false);

		capturer_insert_gesture(&wti->capturer, TOUCH_GESTURE_PALM_SCREEN_OFF);

		mutex_lock(&g_display_data.lock);
		if (g_display_data.power_state == DISPLAY_AOD ||
		    g_display_data.power_state == DISPLAY_OFF) {
			wti_mcu_send_key(KEY_SLEEP);
		}
		mutex_unlock(&g_display_data.lock);
		/*press sleep key*/
		raydium_send_key(g_raydium_ts->input_dev, KEY_SLEEP);
		LOGI("Palm On\n");
		atomic_inc(&g_raydium_ts->stats.palm_count);
		break;
	}
	case RAD_WAKE_UP: {
		capturer_insert_gesture(&wti->capturer, TOUCH_GESTURE_TOUCH_TO_WAKE);
		LOGI("Tap to Wake up\n");
		atomic_inc(&g_raydium_ts->stats.wakeup_count);
		raydium_report_one_tap_gesture(report_finger_data);
		break;
	}
	case RAD_SWIPE_LEFT: {
		capturer_insert_gesture(&wti->capturer, TOUCH_GESTURE_SWIPE_TO_WAKE_LEFT);
		raydium_send_key(g_raydium_ts->input_dev, KEY_RIGHT);
		if (g_raydium_ts->use_wrapped_i2c)
			LOGI("Swipe left from (%d, %d)\n", report_gesture_info->i16_x_first_p,
			     report_gesture_info->i16_y_first_p);
		else
			LOGI("Swipe left\n");
		atomic_inc(&g_raydium_ts->stats.swipe_left_count);
		break;
	}
	case RAD_SWIPE_RIGHT: {
		capturer_insert_gesture(&wti->capturer, TOUCH_GESTURE_SWIPE_TO_WAKE_RIGHT);
		raydium_send_key(g_raydium_ts->input_dev, KEY_LEFT);
		if (g_raydium_ts->use_wrapped_i2c)
			LOGI("Swipe right from (%d, %d)\n", report_gesture_info->i16_x_first_p,
			     report_gesture_info->i16_y_first_p);
		else
			LOGI("Swipe right\n");
		atomic_inc(&g_raydium_ts->stats.swipe_right_count);
		break;
	}
	case RAD_SWIPE_UP: {
		capturer_insert_gesture(&wti->capturer, TOUCH_GESTURE_SWIPE_TO_WAKE_UP);
		raydium_send_key(g_raydium_ts->input_dev, KEY_DOWN);
		if (g_raydium_ts->use_wrapped_i2c)
			LOGI("Swipe up from (%d, %d)\n", report_gesture_info->i16_x_first_p,
			     report_gesture_info->i16_y_first_p);
		else
			LOGI("Swipe up\n");
		atomic_inc(&g_raydium_ts->stats.swipe_up_count);
		break;
	}
	case RAD_SWIPE_DOWN: {
		capturer_insert_gesture(&wti->capturer, TOUCH_GESTURE_SWIPE_TO_WAKE_DOWN);
		raydium_send_key(g_raydium_ts->input_dev, KEY_UP);
		if (g_raydium_ts->use_wrapped_i2c)
			LOGI("Swipe down from (%d, %d)\n", report_gesture_info->i16_x_first_p,
			     report_gesture_info->i16_y_first_p);
		else
			LOGI("Swipe down\n");
		atomic_inc(&g_raydium_ts->stats.swipe_down_count);
		break;
	}
	case RAD_WATER_MODE_ENTER: {
		capturer_insert_gesture(&wti->capturer, TOUCH_FLAG_WATER_MODE_ENTER);
		LOGI("Water Mode Enter\n");
		atomic_inc(&g_raydium_ts->stats.water_mode_enter_count);
		break;
	}
	case RAD_WATER_MODE_EXIT: {
		capturer_insert_gesture(&wti->capturer, TOUCH_FLAG_WATER_MODE_EXIT);
		LOGI("Water Mode Exit\n");
		atomic_inc(&g_raydium_ts->stats.water_mode_exit_count);
		break;
	}
	default:
		LOGW("Received unknown gesture: %d\n", report_finger_data->u8_gesture_state);
		break;
	}
}

static inline bool raydium_ic_in_gesture_mode(unsigned char u8_power_status)
{
	/*
	 * To find out any possible FW issues, the condition here is stricter
	 * so that we can check the mismatch between touch and display mode
	 */
	return ((u8_power_status & TOUCH_POWER_STATUS_TOUCH_MODE) == TOUCH_POWER_TOUCH_GESTURE &&
		((u8_power_status & TOUCH_POWER_STATUS_DISPLAY_MODE) == TOUCH_POWER_DISPLAY_IDLE ||
		 (u8_power_status & TOUCH_POWER_STATUS_DISPLAY_MODE) == TOUCH_POWER_DISPLAY_DSTB));
}

static void raydium_report_touch_event(struct raydium_report_touch_data *report_touch_data)
{
	/*
	 * There may be some fingers when the gesture is `WATER_MODE_ENTER`,
	 * so we need to report both.
	 */
	if (report_touch_data->touch_points.u8_gesture_state) {
		if (!raydium_ic_in_gesture_mode(report_touch_data->error_status.u8_power_status) &&
		    report_touch_data->touch_points.u8_gesture_state != RAD_WATER_MODE_ENTER &&
		    report_touch_data->touch_points.u8_gesture_state != RAD_WATER_MODE_EXIT &&
		    report_touch_data->touch_points.u8_gesture_state != RAD_PALM_ENABLE) {
			LOGW("%s: receive non-palm gesture in active mode, logging error report\n",
			     __func__);
			raydium_parse_error_report((char *)&report_touch_data->error_status);
		}
		raydium_report_gesture(report_touch_data);
	}
	if (report_touch_data->touch_points.u8_touch_points)
		raydium_touch_report_motion_locked(&report_touch_data->touch_points);

	if (g_u8_report_mode == REPORT_WATER_STREAMING_MODE)
		raydium_dump_image_data((short *)(report_touch_data->heatmap_data),
					HEATMAP_DATA_BUFFER_SIZE, g_raydium_ts->x_channel,
					g_raydium_ts->y_channel);

	// Finish one frame to report the heatmap data
	capturer_frame_finish(&wti->capturer, g_raydium_ts->timestamp_real);
}

// Acquire `g_raydium_ts->lock` before calling this function
static void raydium_handle_touch_event_locked(void)
{
	int i32_ret = 0;
	struct raydium_report_touch_data report_touch_data;
	static unsigned char u8_seq_no;
	unsigned long current_time;

	/* Start the timer for statistic the touch events*/
	current_time = jiffies;
	if (atomic_read(&g_raydium_ts->stats.event_count) == 0) {
		atomic_long_set(&g_raydium_ts->stats.first, current_time);
		mod_timer(&g_raydium_ts->stats_timer, jiffies + TOUCH_CLUSTER_GAP_JIFFIES);
	}
	atomic_long_set(&g_raydium_ts->stats.last, current_time);
	atomic_inc(&g_raydium_ts->stats.event_count);

	// Read touch data based on different protocols
	if (g_raydium_ts->use_wrapped_i2c)
		i32_ret = raydium_read_touchdata_wrapping_locked(&report_touch_data);
	else
		i32_ret = raydium_read_touchdata_locked(&report_touch_data);
	if (i32_ret < 0) {
		// clean up the seq number if the failure is caused by reset interrupt
		if (i32_ret == -EFAULT) {
			LOGW("clean up the sequence number due to reset interrupt\n");
			u8_seq_no = 0;
		}
		LOGE("%s, read_touchdata error, ret:%d\n", __func__, i32_ret);
		return;
	}

	i32_ret = raydium_check_esd_reset_locked(report_touch_data.touch_points.u8_error_state);
	if (i32_ret == -EFAULT) {
		// This touch event is from reset, set sequence number back to 0 because TDDIC reset
		u8_seq_no = 0;
		return;
	}

	/*
	 * If mode is normal mode / water streaming mode, checking whether there is error happened.
	 * Streaming mode may just report touch sensing data, no need to check whether it's a error
	 * touch event.
	 */
	if (g_u8_report_mode != REPORT_STREAMING_MODE)
		raydium_check_error_flag_locked(&report_touch_data);

	/*
	 * Parse the touch data to ensure how many fingers we may want to parse for this frame.
	 * Note that there may also be some fingers w/ `RAD_WATER_MODE_ENTER` gesture.
	 */
	if (report_touch_data.touch_points.u8_touch_points > MAX_TOUCH_NUM) {
		atomic_inc(&g_raydium_ts->stats.wrong_finger_num_count);
		LOGE("%s: Parse the unexpected number of fingers, fingers_num = %d\n", __func__,
		     report_touch_data.touch_points.u8_touch_points);
		return;
	} else if (g_u8_report_mode != REPORT_STREAMING_MODE &&
		   !report_touch_data.touch_points.u8_gesture_state &&
		   !report_touch_data.touch_points.u8_touch_points) {
		atomic_inc(&g_raydium_ts->stats.empty_report_count);
		LOGE("%s: No finger or gesture found\n", __func__);
		return;
	}

	// Checkout if this touch event is duplicated or not
	if (u8_seq_no == report_touch_data.touch_points.u8_seq_num ||
	    report_touch_data.touch_points.u8_seq_num == 0) {
		atomic_inc(&g_raydium_ts->stats.seq_num_dup_count);
		LOGW("%s -> report not updated. FW state = 0x%x\n", __func__,
		     report_touch_data.touch_points.u8_error_state);
		raydium_parse_error_flag(report_touch_data.touch_points.u8_error_state);
		return;
	}

	// Update to the latest sequence number
	u8_seq_no = report_touch_data.touch_points.u8_seq_num;

	capturer_insert_frame_index(
		&wti->capturer, report_touch_data.touch_points.pt[0].u8_frame_index |
					report_touch_data.touch_points.pt[1].u8_frame_index << 8);

	// Discard this touch data when affected by NFC to avoid ghost touch
	if (time_is_after_jiffies64((u64)atomic64_read(&g_raydium_ts->touch_inactive_time))) {
		if (atomic_xchg(&g_raydium_ts->log_skipped_touch, false))
			LOGI("Touch events are discarded from suppress\n");

		atomic_inc(&g_raydium_ts->stats.suppress_count);
		return;
	}

	raydium_report_touch_event(&report_touch_data);
}

static int convert_mcu_touch_points(const struct mcu_touch_input_event_callback_data *touch_points,
				    struct raydium_report_touch_data *report_touch_data)
{
	struct raydium_report_finger_data *touch_point_data = &report_touch_data->touch_points;
	int i;

	touch_point_data->u8_gesture_state = touch_points->gesture;
	touch_point_data->u8_touch_points = touch_points->touch_cnt;

	for (i = 0; i < touch_points->touch_cnt; i++) {
		touch_point_data->pt[i].u8_id = touch_points->point_id[i];
		touch_point_data->pt[i].u8_touch_status = touch_points->touch_status[i];
		touch_point_data->pt[i].i16_x_p = touch_points->x_pos[i];
		touch_point_data->pt[i].i16_y_p = touch_points->y_pos[i];
		touch_point_data->pt[i].i8_x_shape = touch_points->wx[i];
		touch_point_data->pt[i].i8_y_shape = touch_points->wy[i];
		touch_point_data->pt[i].i16_z_w = touch_points->pressure[i];
	}

	if (touch_points->heatmap_len != HEATMAP_DATA_BUFFER_SIZE) {
		LOGE("The data size of heatmap from MCU is not matched: size = %d\n",
		     touch_points->heatmap_len);
		return -EINVAL;
	}
	memcpy(report_touch_data->heatmap_data, touch_points->heatmap_data,
	       touch_points->heatmap_len);
	return 0;
}

static void on_mcu_touch_event_received(const struct mcu_touch_callback_data *data)
{
	struct raydium_report_touch_data report_touch_data = { 0 };
	unsigned long current_time;
	int ret;

	/* Start the timer for statistic the touch events*/
	current_time = jiffies;
	if (atomic_read(&g_raydium_ts->stats.event_count) == 0) {
		atomic_long_set(&g_raydium_ts->stats.first, current_time);
		mod_timer(&g_raydium_ts->stats_timer, jiffies + TOUCH_CLUSTER_GAP_JIFFIES);
	}
	atomic_long_set(&g_raydium_ts->stats.last, current_time);
	atomic_inc(&g_raydium_ts->stats.event_count);
	atomic_inc(&g_raydium_ts->stats.mcu_event_count);

	g_raydium_ts->timestamp = ktime_get();
	g_raydium_ts->timestamp_real = ktime_get_real();

	ret = convert_mcu_touch_points(&data->input_event_data, &report_touch_data);
	if (ret < 0) {
		LOGE("%s: Failed to convert MCU touch points, ret=%d\n", __func__, ret);
		return;
	}
	raydium_report_touch_event(&report_touch_data);
}
static void on_mcu_request_control(const struct mcu_touch_callback_data *data)
{
	// If get the message from Touch offload service, using the new data format
	wti_mcu_set_message_format(MCU_TOUCH_HEADER_DATA_FORMAT);

	/* Start to enable/disable interrupt and send ack back to MCU
	 * Two cases:
	 *     runtime_offload_enable = true, allow to enable/disable offload
	 *     runtime_offload_enable = false, refuse to enable offload, send Disable to MCU
	 */
	if (runtime_offload_enable)
		raydium_set_offload(data->offload_data.enable, TOUCH_SERVICE);
	else
		raydium_set_offload(false, TOUCH_SERVICE);
}

void raydium_ts_touch_suprress(unsigned int latency_jiffies)
{
	atomic64_set(&g_raydium_ts->touch_inactive_time, jiffies_64 + latency_jiffies);
	atomic_set(&g_raydium_ts->log_skipped_touch, latency_jiffies > 0);
}

static irqreturn_t raydium_ts_nfc_irq_handler(int irq, void *data)
{
	/* Ignore any ghost touches introduced by NFC */
	if (gpio_get_value(g_raydium_ts->pdata->nfc_gpio)) {
		unsigned int nfc_active_ms =
			jiffies_to_msecs(g_raydium_ts->pdata->nfc_active_jiffies);

		LOGI("NFC active, touch suppressed for up to %u ms\n", nfc_active_ms);
		raydium_ts_touch_suprress(g_raydium_ts->pdata->nfc_active_jiffies);
	} else {
		if (time_is_after_jiffies64(
			    (u64)atomic64_read(&g_raydium_ts->touch_inactive_time))) {
			/* If touch reporting has not already resumed, do so now */
			LOGI("NFC inactive, touch reporting will resume in %u ms\n",
			     jiffies_to_msecs(g_raydium_ts->pdata->nfc_debounce_jiffies));
			raydium_ts_touch_suprress(g_raydium_ts->pdata->nfc_debounce_jiffies);
		}
	}

	return IRQ_HANDLED;
}

irqreturn_t raydium_ts_top_handler(int irq, void *data)
{
	unsigned long flags;
	struct irq_desc *desc = irq_to_desc(irq);
	irqreturn_t ret = IRQ_NONE;

	raw_spin_lock_irqsave(&desc->lock, flags);
	ret = (desc->threads_oneshot) ? IRQ_HANDLED : IRQ_WAKE_THREAD;
	raw_spin_unlock_irqrestore(&desc->lock, flags);

	if (ret == IRQ_WAKE_THREAD) {
		g_raydium_ts->timestamp = ktime_get();
		g_raydium_ts->timestamp_real = ktime_get_real();
	} else {
		atomic_inc(&g_raydium_ts->stats.ignored_count);
	}

	return ret;
}

/*The raydium device will signal the host about TRIGGER_FALLING.
 *Processed when the interrupt is asserted.
 */
irqreturn_t raydium_ts_interrupt(int irq, void *dev_id)
{
	int ret = 0;

	mutex_lock(&g_raydium_ts->lock);
	/* pm_stay_awake call must be balanced by a pm_relax call */
	pm_stay_awake(&g_raydium_ts->client->dev);
	/*For bootloader wrt/erase flash and software reset interrupt*/
	if ((g_u8_raydium_flag & ENG_MODE) != 0) {
		LOGI("RAD_ENG_MODE\n");
		g_u8_raydium_flag |= INT_FLAG;
	} else {
		/* Waiting for the I2C bus to resume; otherwise, an I2C error will occur */
		ret = wait_for_completion_interruptible_timeout(&g_raydium_ts->resume_done,
								PM_WAKEUP_TIMEOUT);
		if (ret == 0) {
			LOGE("%s: PM resume is not handled\n", __func__);
			goto exit_irq_handled;
		}

		if (ret < 0) {
			LOGE("%s: PM return -ERESTARTSYS if interrupted, %d\n", __func__, ret);
			goto exit_irq_handled;
		}
		raydium_handle_touch_event_locked();
	}
exit_irq_handled:
	pm_relax(&g_raydium_ts->client->dev);
	mutex_unlock(&g_raydium_ts->lock);
	return IRQ_HANDLED;
}

irqreturn_t raydium_ts_interrupt_test(int irq, void *dev_id)
{
	int i32_ret = 0;
	unsigned char u8_buf[ACK_PACKET_SIZE_FOR_STREAMING] = { 0 };

	/*
	 * We don't acquire the lock here for two reason.
	 * 1. It might cause deadlock when `free_irq` is called when the factory
	 * test mode is changing.
	 * 2. It's fine to finish this handler when the above scenario happen.
	 */
	switch (g_raydium_ts->factory_test_mode) {
	case FATP1:
		i32_ret = raydium_i2c_pda2_write(g_raydium_ts->client,
						 RAYDIUM_PDA2_TCH_RPT_STATUS_ADDR, u8_buf,
						 ACK_PACKET_SIZE_FOR_STREAMING);
		if (i32_ret < 0)
			LOGE("Failed to ACK in INT1\n");

		fallthrough;
	case FATP2:
		LOGI("AP touch INT1\n");

		fallthrough;
	default:
		return IRQ_HANDLED;
	}
}

irqreturn_t raydium_ts_top_handler2(int irq, void *data)
{
	return IRQ_WAKE_THREAD;
}

irqreturn_t raydium_ts_interrupt2(int irq, void *dev_id)
{
	// TODO(b/365324370): Implement the watchdog mechanism
	LOGI("INT2 triggered\n");
	// disable IRQ to avoid nested interrupt
	raydium_irq2_control(false, TOUCH_WATCHDOG);
	offloading_request_take_control_back_ap();
	return IRQ_HANDLED;
}

irqreturn_t raydium_ts_interrupt2_test(int irq, void *dev_id)
{
	int i32_ret = 0;
	unsigned char u8_buf[ACK_PACKET_SIZE_FOR_STREAMING] = { 0 };

	LOGI("AP touch INT2\n");

	/*
	 * We don't acquire the lock here for two reason.
	 * 1. It might cause deadlock when `free_irq` is called when the factory
	 * test mode is changing.
	 * 2. It's fine to finish this handler when the above scenario happen.
	 */
	if (g_raydium_ts->factory_test_mode == FATP2) {
		i32_ret =
			raydium_i2c_pda2_set_page(g_raydium_ts->client, false, RAYDIUM_PDA2_PAGE_0);
		if (i32_ret < 0)
			LOGE("%s: failed to set page in work_pending\n", __func__);

		i32_ret = raydium_i2c_pda2_write(g_raydium_ts->client,
						 RAYDIUM_PDA2_TCH_RPT_STATUS_ADDR, u8_buf,
						 ACK_PACKET_SIZE_FOR_STREAMING);
		if (i32_ret < 0)
			LOGE("Failed to ACK in INT2\n");
	}

	// Enable the IRQ after sending ACK and set page which pull backs the GPIO
	enable_irq(irq);
	return IRQ_HANDLED;
}

static int raydium_check_pid(void)
{
	unsigned char u8_buf[4] = {0};
	int i32_ret = ERROR;

	mutex_lock(&g_raydium_ts->lock);

	i32_ret = handle_i2c_pda_read(g_raydium_ts->client,
				RAYDIUM_PDA_PID, u8_buf, 4);
	if (i32_ret < 0)
		goto exit_error;

	LOGI("RAD PID : 0x%02X\n", u8_buf[0]);

exit_error:
	mutex_unlock(&g_raydium_ts->lock);
	return i32_ret;
}

static int raydium_check_i2c_ready(unsigned short *u16_i2c_data)
{
	unsigned char u8_buf[4] = {0};
	int i32_ret = ERROR;

	mutex_lock(&g_raydium_ts->lock);

	i32_ret = handle_i2c_pda_read(g_raydium_ts->client, RAYDIUM_CHK_I2C_CMD, u8_buf, 4);
	if (i32_ret < 0)
		goto exit_error;

	if (u8_buf[3] != 0xF3)
		i32_ret = ERROR;

	*u16_i2c_data = u8_buf[3] << 8 | u8_buf[2];

	LOGI("RAD check I2C : 0x%02X%02X\n", u8_buf[3], u8_buf[2]);

exit_error:
	mutex_unlock(&g_raydium_ts->lock);
	return i32_ret;
}

/* This function is called when we get the notification of display OFF/AOD */
static void raydium_ts_display_suspend(void)
{
	unsigned char u8_i = 0;

	mutex_lock(&g_raydium_ts->lock);
	if (g_u8_raw_data_type == 0)
		g_u8_resetflag = false;
	if (g_raydium_ts->is_suspend == 1) {
		LOGW("Already in suspend state\n");
		mutex_unlock(&g_raydium_ts->lock);
		return;
	}

	/* Clear unreported touch stats */
	if (try_to_del_timer_sync(&g_raydium_ts->stats_timer) > 0)
		report_touch_stats();

	LOGI("%s.\n", __func__);

	/* release all touches */
	for (u8_i = 0; u8_i < g_raydium_ts->pdata->num_max_touches; u8_i++) {
		input_mt_slot(g_raydium_ts->input_dev, u8_i);
		input_mt_report_slot_state(g_raydium_ts->input_dev,
					   MT_TOOL_FINGER,
					   false);
	}
	input_sync(g_raydium_ts->input_dev);

	g_u8_checkflag = true;
	g_raydium_ts->is_suspend = 1;
	mutex_unlock(&g_raydium_ts->lock);
}

/* This function is called when we get the notification of display NBM */
static void raydium_ts_display_resume_locked(void)
{
	int i32_ret = 0;
	unsigned char u8_i;
	unsigned char u8_retry = RAYDIUM_HARD_RESET_RETRY_TIMES;

	LOGI("%s, %d.\n", __func__, g_raydium_ts->is_suspend);
	if (g_raydium_ts->is_suspend == 0) {
		LOGW("Already in resume state\n");
		return;
	}

	if (g_raydium_ts->is_sleep_bits) {
		// We don't need to do i2c commands to check esd when touch controller is sleeping
		// We will trigger touch reset automatically after exiting touch sleep mode.
		LOGI("Touch controller is in sleep mode\n");
		goto exit_check_esd;
	}
	/* Only do the esd check when offload is not enabled
	 * TODO(b/467224216): Move the esd status check to MCU side when offload is started
	 */
	if (g_u8_checkflag == true && !g_raydium_ts->offload_enable) {
		i32_ret = raydium_esd_check_locked();
		if (i32_ret < 0) {
			/* release all touches*/
			for (u8_i = 0; u8_i < g_raydium_ts->pdata->num_max_touches; u8_i++) {
				input_mt_slot(g_raydium_ts->input_dev, u8_i);
				input_mt_report_slot_state(g_raydium_ts->input_dev, MT_TOOL_FINGER, false);
			}
			input_sync(g_raydium_ts->input_dev);

			while (u8_retry != 0) {
				i32_ret = raydium_hw_reset_fun(g_raydium_ts->client);
				if (i32_ret < 0) {
					msleep(100);
					u8_retry--;
				} else
					break;
			}
		}
		g_u8_checkflag = false;
	}
exit_check_esd:
	g_raydium_ts->is_suspend = 0;
}

static void raydium_ts_display_resume(void)
{
	mutex_lock(&g_raydium_ts->lock);
	raydium_ts_display_resume_locked();
	mutex_unlock(&g_raydium_ts->lock);
}

static void raydium_panel_notifier_callback(enum panel_event_notifier_tag tag,
					    struct panel_event_notification *notification,
					    void *client_data)
{
	if (!notification) {
		LOGE("Invalid notification\n");
		return;
	}

	if (client_data) {
		LOGE("Invalid client data\n");
		return;
	}
	LOGI("Notification type:%d, early_trigger:%d\n", notification->notif_type,
	     notification->notif_data.early_trigger);

	switch (notification->notif_type) {
	case DRM_PANEL_EVENT_UNBLANK:
		if (notification->notif_data.early_trigger) {
			LOGD("resume notification pre commit\n");
		} else {
			LOGI("resume notification post commit\n");
			raydium_ts_display_resume();
			raydium_display_change_mode(DISPLAY_INTERACTIVE);
			queue_delayed_work(raydium_touch_event_delay_wq,
					   &g_raydium_ts->delay_touch_event_work,
					   REPORT_DELAY_SYSTEM_OFF_JIFFIES);
		}
		break;

	case DRM_PANEL_EVENT_BLANK:
		if (notification->notif_data.early_trigger) {
			LOGI("suspend notification pre commit\n");
			raydium_ts_display_suspend();
			raydium_display_change_mode(DISPLAY_OFF);
		} else {
			LOGD("suspend notification post commit\n");
		}
		break;

	case DRM_PANEL_EVENT_BLANK_LP:
		LOGI("display idle notification\n");
		raydium_ts_display_suspend();
		raydium_display_change_mode(DISPLAY_AOD);
		break;

	case DRM_PANEL_EVENT_FPS_CHANGE:
		LOGD("Received fps change old fps:%d new fps:%d\n",
		     notification->notif_data.old_fps, notification->notif_data.new_fps);
		break;
	default:
		LOGW("notification serviced :%d\n", notification->notif_type);
		break;
	}
}

/* This function is called when PM is trying to suspend i2c driver */
static int raydium_ts_suspend(struct device *dev)
{
	reinit_completion(&g_raydium_ts->resume_done);
	if (try_to_del_timer_sync(&g_raydium_ts->stats_timer) > 0)
		report_touch_stats();
	return 0;
}
/* This function is called when PM resumes i2c driver */
static int raydium_ts_resume(struct device *dev)
{
	complete_all(&g_raydium_ts->resume_done);
	return 0;
}

static SIMPLE_DEV_PM_OPS(raydium_ts_pm_ops, raydium_ts_suspend, raydium_ts_resume);

static int raydium_ts_open(struct input_dev *input_dev)
{
	LOGI("%s()+\n", __func__);

#if defined(CONFIG_FB)
	LOGI("ts->blank:%x\n", g_raydium_ts->blank);
#endif

	// Exit STB first to avoid additional interrupt
	raydium_stb_control(DISABLE, STB_FILE_OPS);
	raydium_irq_control(ENABLE, IRQ_FILE_OPS);
	return 0;
}

static void raydium_ts_close(struct input_dev *input_dev)
{
	unsigned char u8_i = 0;

	LOGI("%s()+\n", __func__);

	// Disable IRQ first to avoid I2C ops error
	raydium_irq_control(DISABLE, IRQ_FILE_OPS);
	raydium_stb_control(ENABLE, STB_FILE_OPS);

	// Release all touches
	for (u8_i = 0; u8_i < g_raydium_ts->pdata->num_max_touches; u8_i++) {
		input_mt_slot(g_raydium_ts->input_dev, u8_i);
		input_mt_report_slot_state(g_raydium_ts->input_dev,
					   MT_TOOL_FINGER,
					   false);
	}
	input_mt_report_pointer_emulation(g_raydium_ts->input_dev, false);
	input_sync(g_raydium_ts->input_dev);
	return;
}

#ifdef CONFIG_OF
static int raydium_parse_selftest_dt(struct device_node *np)
{
	struct device_node *child = NULL;
	int rc = 0;

	child = of_parse_phandle(np, "touch,selftest-data", 0);
	if (child == NULL) {
		LOGE("Not found compatible node \"touch,selftest-data\"!\n");
		return -ENODATA;
	}

	rc = of_property_read_u8_array(child, "TEST_INFO", u8_test_info, TEST_INFO_SIZE);
	if (rc) {
		LOGE("error reading TEST_INFO. ret=%d\n", rc);
		goto exit_parse_selftest_dt;
	}
	rc = of_property_read_u8_array(child, "FT_TEST_THD", i8_ft_test_thd, FT_TEST_THD_SIZE);
	if (rc) {
		LOGE("error reading FT_TEST_THD. ret=%d\n", rc);
		goto exit_parse_selftest_dt;
	}
	rc = of_property_read_u8_array(child, "TEST_PARA", u8_test_para, TEST_PARA_SIZE);
	if (rc) {
		LOGE("error reading TEST_PARA. ret=%d\n", rc);
		goto exit_parse_selftest_dt;
	}
	rc = of_property_read_u8_array(child, "RAW_DATA_3_CC", u8_raw_data_3_cc,
				       RAW_DATA_3_CC_SIZE);
	if (rc) {
		LOGE("error reading RAW_DATA_3_CC. ret=%d\n", rc);
		goto exit_parse_selftest_dt;
	}
	rc = of_property_read_u8_array(child, "RAW_DATA_UC_CC", u8_raw_uc_cc, RAW_UC_CC_SIZE);
	if (rc) {
		LOGE("error reading RAW_DATA_UC_CC. ret=%d\n", rc);
		goto exit_parse_selftest_dt;
	}

exit_parse_selftest_dt:
	of_node_put(child);
	return rc;
}

static int raydium_parse_display_dt(struct device *dev, struct raydium_ts_platform_data *pdata,
				    struct raydium_ts_devicetree_data *dt_data)
{
	u32 coords[COORDS_ARR_SIZE];
	struct property *prop;
	struct device_node *np = dev->of_node;
	int coords_size, rc;

	int i;
	int count;
	struct device_node *node;
	struct drm_panel *panel;

	count = of_count_phandle_with_args(np, "panel", NULL);
	if (count <= 0)
		return -ENODATA;

	for (i = 0; i < count; i++) {
		node = of_parse_phandle(np, "panel", i);
		panel = of_drm_find_panel(node);
		if (!IS_ERR(panel)) {
			dt_data->panel = panel;

			g_raydium_ts->fw_name_dt = of_get_property(node, "touch,firmware", NULL);

			if (!g_raydium_ts->fw_name_dt) {
				of_node_put(node);
				return -ENODATA;
			}
			LOGI("touch,firmware=%s\n", g_raydium_ts->fw_name_dt);

			prop = of_find_property(node, "touch,window-coords", NULL);
			if (!prop) {
				of_node_put(node);
				return -EINVAL;
			}
			if (!prop->value) {
				of_node_put(node);
				return -ENODATA;
			}
			coords_size = prop->length / sizeof(u32);
			if (coords_size != COORDS_ARR_SIZE) {
				LOGE("invalid touch,window-coords\n");
				of_node_put(node);
				return -EINVAL;
			}

			rc = of_property_read_u32_array(node, "touch,window-coords", coords,
							coords_size);
			if (rc && (rc != -EINVAL)) {
				LOGE("unable to read touch,window-coords\n");
				of_node_put(node);
				return rc;
			}

			pdata->x_min = coords[0];
			pdata->y_min = coords[1];
			pdata->x_max = coords[2];
			pdata->y_max = coords[3];

			rc = raydium_parse_selftest_dt(node);
			if (rc) {
				LOGE("unable to read selftest parameters\n");
				of_node_put(node);
				return rc;
			}

			rc = of_property_read_u32(node, "touch,grid-spec",
						  &g_raydium_ts->grid_spec);
			if (rc) {
				LOGE("unable to read touch grid spec\n");
				of_node_put(node);
				return rc;
			}
			g_raydium_ts->edge_rejection_data.enable =
				of_property_read_bool(node, "touch,edge-rejection-enable");
			LOGI("%s: touch,edge-rejection-enable=%d\n", __func__,
			     g_raydium_ts->edge_rejection_data.enable);

			if (of_property_read_u8(node, "touch,edge-zone-in-pixels",
						&g_raydium_ts->edge_rejection_data
							 .parameters[EDGEZONE_IN_PIXELS])) {
				LOGW("Failed to read edge zone pixels\n");
			}
			if (of_property_read_u8(node, "touch,dead-zone-in-pixels",
						&g_raydium_ts->edge_rejection_data
							 .parameters[DEADZONE_IN_PIXELS])) {
				LOGW("Failed to read dead zone pixels\n");
			}
			if (of_property_read_u8(node, "touch,framecounter-in-frames",
						&g_raydium_ts->edge_rejection_data
							 .parameters[FRAMECOUNTER_IN_FRAMES])) {
				LOGW("Failed to read frame counter\n");
			}
			if (of_property_read_u8(node, "touch,landlock-in-pixels",
						&g_raydium_ts->edge_rejection_data
							 .parameters[LANDLOCK_IN_PIXELS])) {
				LOGW("Failed to read landlock pixels\n");
			}

			if (of_property_read_bool(node, "touch,i2c-wrapping-enable")) {
				LOGI("Using i2c wrapping protocol to read touch data\n");
				g_raydium_ts->use_wrapped_i2c = true;
			} else {
				LOGI("Using multiple i2c operations to read touch data\n");
				g_raydium_ts->use_wrapped_i2c = false;
			}

			g_raydium_ts->streaming_mode_enable =
				of_property_read_bool(node, "touch,streaming-mode-enable");

			if (of_property_read_u32(node, "touch,esd-status-address",
						 &g_raydium_ts->esd_status_addr)) {
				LOGI("Failed to read the esd status addr, using default value %x\n",
				     RAYDIUM_PDA_TCH_RPT_STATUS);
				g_raydium_ts->esd_status_addr = RAYDIUM_PDA_TCH_RPT_STATUS;
			}
			of_node_put(node);
			return 0;
		}
		of_node_put(node);
	}

	return PTR_ERR(panel);
}

static int raydium_parse_dt(struct device *dev, struct raydium_ts_platform_data *pdata,
			    struct raydium_ts_devicetree_data *dt_data)
{
	struct device_node *np = dev->of_node;
	int rc = 0;
	u32 temp_val = 0;

	pdata->name = RAYDIUM_NAME;

	rc = raydium_parse_display_dt(dev, pdata, dt_data);
	if (rc)
		return rc;

	/* reset, irq gpio info */
	pdata->reset_gpio = of_get_named_gpio(np,
			    "raydium,reset-gpio",
			    0);
	if (pdata->reset_gpio < 0)
		return pdata->reset_gpio;


	pdata->irq_gpio = of_get_named_gpio(np,
			  "raydium,irq-gpio",
			  0);
	if (pdata->irq_gpio < 0)
		return pdata->irq_gpio;

	pdata->irq2_gpio = of_get_named_gpio(np, "raydium,irq2-gpio", 0);
	if (pdata->irq2_gpio >= 0)
		LOGI("Second IRQ found: %d\n", pdata->irq2_gpio);

	g_raydium_ts->touch_nfc_filter_enable =
		of_property_read_bool(np, "raydium,nfc_filter_enable");
	LOGI("%s: raydium,nfc_filter_enable=%d\n", __func__, g_raydium_ts->touch_nfc_filter_enable);

	// Initialize the variable first to ensure the NFC filter won't be enabled by default
	pdata->nfc_gpio = -EINVAL;
	pdata->nfc_active_jiffies = 0;
	if (g_raydium_ts->touch_nfc_filter_enable) {
		pdata->nfc_gpio = of_get_named_gpio(np, "raydium,nfc-active-gpio", 0);
		if (pdata->nfc_gpio < 0)
			return pdata->nfc_gpio;

		rc = of_property_read_u32(np, "raydium,nfc-active-ms", &temp_val);
		if (!rc)
			pdata->nfc_active_jiffies = msecs_to_jiffies(temp_val);
		else
			return rc;

		rc = of_property_read_u32(np, "raydium,nfc-debounce-ms", &temp_val);
		if (!rc)
			pdata->nfc_debounce_jiffies = msecs_to_jiffies(temp_val);
		else
			return rc;
	}

	rc = of_property_read_u32(np,
				  "raydium,hard-reset-delay-ms", &temp_val);
	if (!rc)
		pdata->hard_rst_dly = temp_val;
	else
		return rc;


	rc = of_property_read_u32(np,
				  "raydium,soft-reset-delay-ms", &temp_val);
	if (!rc)
		pdata->soft_rst_dly = temp_val;
	else
		return rc;


	rc = of_property_read_u32(np, "raydium,num-max-touches", &temp_val);
	if (!rc)
		pdata->num_max_touches = temp_val;
	else
		return rc;

	g_raydium_ts->hw_id = of_get_property(np, "touch,identifier", NULL);
	if (!g_raydium_ts->hw_id) {
		LOGE("%s: failed to parse touch hardware identifier\n", __func__);
		return -ENODATA;
	}

	g_raydium_ts->touch_deep_sleep_enable =
		of_property_read_bool(np, "raydium,deep-sleep-enable");
	LOGI("%s: raydium,deep-sleep-enable=%d\n", __func__, g_raydium_ts->touch_deep_sleep_enable);

	g_raydium_ts->esd_protect_enable = of_property_read_bool(np, "raydium,esd-protect-enable");
	LOGI("%s: raydium,esd-protect-enable=%d\n", __func__, g_raydium_ts->esd_protect_enable);

	g_raydium_ts->offload_enable = of_property_read_bool(np, "offload-enable");
	LOGI("%s: offload-enable=%d\n", __func__, g_raydium_ts->offload_enable);
	return 0;
}
#else
static int raydium_parse_dt(struct device *dev,
			    struct raydium_ts_platform_data *pdata)
{
	return -ENODEV;
}
#endif /*end of CONFIG_OF*/

static void raydium_input_set(struct input_dev *input_dev)
{
	int ret = 0;
	unsigned char i;

	input_dev->name = "raydium_ts";/*name need same with .idc*/
	input_dev->id.bustype = BUS_I2C;
	input_dev->open = raydium_ts_open;/*touch lock*/
	input_dev->close = raydium_ts_close;
	input_set_drvdata(input_dev, g_raydium_ts);

	__set_bit(EV_KEY, input_dev->evbit);
	__set_bit(EV_ABS, input_dev->evbit);
	__set_bit(BTN_TOUCH, input_dev->keybit);
	__set_bit(INPUT_PROP_DIRECT, input_dev->propbit);
	LOGI("set abs prarams x[%d], y[%d]\n", g_raydium_ts->pdata->x_max - 1,
	     g_raydium_ts->pdata->y_max - 1);

	/* Multitouch input params setup */
	input_set_abs_params(input_dev, ABS_MT_POSITION_X, 0, g_raydium_ts->pdata->x_max - 1, 0, 0);
	input_set_abs_params(input_dev, ABS_MT_POSITION_Y, 0, g_raydium_ts->pdata->y_max - 1, 0, 0);
	input_set_abs_params(input_dev, ABS_MT_PRESSURE, 0, PRESS_MAX, 0, 0);
	input_set_abs_params(input_dev,
			     ABS_MT_TOUCH_MAJOR, 0, WIDTH_MAX, 0, 0);
	input_set_abs_params(input_dev,
			     ABS_MT_TOUCH_MINOR, 0, WIDTH_MAX, 0, 0);

	ret = input_mt_init_slots(input_dev, MAX_TOUCH_NUM,
				  INPUT_MT_DIRECT | INPUT_MT_DROP_UNUSED);
	if (ret)
		LOGE("failed to initialize MT slots: %d\n", ret);

	for (i = 0; i < (MAX_TOUCH_NUM * 2); i++)
		gst_slot[i] = gst_slot_init;

}
static int raydium_set_resolution(void)
{
	unsigned char u8_buf[4] = {0};
	int i32_ret = -1;
	unsigned int u32_x, u32_y;

	mutex_lock(&g_raydium_ts->lock);

	i32_ret = raydium_i2c_pda2_set_page(g_raydium_ts->client,
					    g_raydium_ts->is_suspend,
					    RAYDIUM_PDA2_PAGE_0);
	if (i32_ret < 0)
		goto exit_error;

	i32_ret = raydium_i2c_pda2_read(g_raydium_ts->client,
					RAYDIUM_PDA2_DISPLAY_INFO_ADDR,
					u8_buf, 4);
	if (i32_ret < 0)
		goto exit_error;

	u32_x = u8_buf[3] << 8 | u8_buf[2];
	u32_y = u8_buf[1] << 8 | u8_buf[0];

	LOGI("RAD display info x:%d, y:%d\n", u32_x, u32_y);

	if (u32_x > 100 && u32_y > 100 &&
	    u32_x < 600 && u32_y < 600) {
		g_raydium_ts->pdata->x_max = u32_x;
		g_raydium_ts->pdata->y_max = u32_y;
	}

	i32_ret = handle_i2c_pda_read(g_raydium_ts->client, RAYDIUM_PDA__CHANNEL_ADDR, u8_buf, 4);
	if (i32_ret < 0)
		goto exit_error;
	u32_x = u8_buf[0];
	u32_y = u8_buf[1];
	LOGI("RAD channel x:%d, y:%d\n", u32_x, u32_y);
	if (u32_x > 4 && u32_y > 4 && u32_x < 12 && u32_y < 12) {
		g_raydium_ts->x_channel = u32_x;
		g_raydium_ts->y_channel = u32_y;
	}

exit_error:
	mutex_unlock(&g_raydium_ts->lock);
	return i32_ret;
}

/**
 * raydium_irq2_control - enable/disable the IRQ2
 * @enable:		Enable or disable
 * @reason:		The reason to enable/disable
 *
 * When any bits are set to 1, the IRQ2 and IRQ wake for IRQ2 will be disabled.
 * When touch offload is disabled(on AP), IRQ2 will be disabled.
 * When touch offload is enabled(on MCU), IRQ2 will be enabled for touch watchdog usage.
 *
 * Both IRQ and IRQ wake are default to be disabled.
 */
static void raydium_irq2_control(bool enable, enum irq2_enable_reason reason)
{
	bool original_is_irq2_enabled;

	LOGI("%s(%s, %s)\n", __func__, enable ? "enable" : "disable",
	     irq2_enable_reason_desc[reason]);

	// If there are any bits set to 1, the IRQ2 is disabled
	mutex_lock(&g_raydium_ts->irq2_lock);
	original_is_irq2_enabled = !!(g_raydium_ts->irq2_enable_bits);

	if (enable) {
		// Only enable it when turning from disabled to enabled
		set_bit(reason, &g_raydium_ts->irq2_enable_bits);
		if (!original_is_irq2_enabled) {
			enable_irq(g_raydium_ts->irq2);
			enable_irq_wake(g_raydium_ts->irq2);
			LOGI("IRQ2 enabled\n");
		}
	} else {
		// Only disable IRQ2 when turning from enabled to disabled
		clear_bit(reason, &g_raydium_ts->irq2_enable_bits);
		if (original_is_irq2_enabled && !g_raydium_ts->irq2_enable_bits) {
			disable_irq_nosync(g_raydium_ts->irq2);
			disable_irq_wake(g_raydium_ts->irq2);
			LOGI("IRQ2 disabled\n");
		}
	}
	mutex_unlock(&g_raydium_ts->irq2_lock);
}

static int raydium_i2c_arbitration_notifier_callback(void *client_data,
						     enum i2c_arbitration_event_type request)
{
	wti_mcu_set_message_format(MCU_TOUCH_HEADER_DATA_FORMAT);

	if (request == I2C_OWNER_AP) {
		// touch driver change the offload flag to false and
		// handle the following touch events by AP
		LOGI("%s: Got the request to transfer the i2c bus to AP", __func__);
		return raydium_set_offload(false, ARBITRATION_SERVICE);
	} else if (request == I2C_OWNER_MCU) {
		// touch driver change the offload flag to true and
		// handle the following touch events by MCU
		// Return 0 to confirm the module won't use i2c bus anymore
		LOGI("%s: Got the request to transfer the i2c bus to MCU", __func__);
		if (!runtime_offload_enable) {
			LOGW("Arbiter requested to offload, but driver didn't support");
			return -EOPNOTSUPP;
		}
		return raydium_set_offload(true, ARBITRATION_SERVICE);
	}
	return 0;
}

static void register_offload_notifier(void)
{
	if (g_raydium_ts->offload_enable) {
		mcu_touch_register_listener(on_mcu_request_control,
					    MCU_TOUCH_COMMAND_MCU_REQUEST_CONTROL);
		mcu_touch_register_listener(on_mcu_touch_event_received,
					    MCU_TOUCH_COMMAND_POINT_DATA);
	}
}

static void register_mcu_transport(struct work_struct *work)
{
	static int cnt;

	// Ensure mcu_transport is ready and registered
	if (!dispatcher_init) {
		dispatcher_init = mcu_touch_register_dispatcher();
		if (dispatcher_init) {
			// Register the related mcu touch data handler.
			register_offload_notifier();
		}
	}

	// Ensure i2c_arbitration is ready and registered
	if (!i2c_arb_init)
		i2c_arb_init = !register_i2c_arbitration_event_notifier(
			NULL, raydium_i2c_arbitration_notifier_callback);

	cnt++;
	LOGI("DEFER cnt %d\n", cnt);
	// Give up after DEFER_CNT tries (== recovery mode)
	if (cnt > DEFER_CNT) {
		LOGW("Give up mcu/i2c arb registration, dispatcher_init=%d, i2c_arb_init=%d\n",
		     dispatcher_init, i2c_arb_init);
		return;
	}

	if (dispatcher_init && i2c_arb_init) {
		LOGI("mcu transport and i2c arb is registered\n");
		return;
	}

	// Try to register again
	schedule_delayed_work(to_delayed_work(work), REGISTER_MCU_TRANSPORT_DELAY_JIFFIES);
}

static void unregister_offload_notifier(void)
{
	if (g_raydium_ts->offload_enable) {
		cancel_delayed_work_sync(&register_mcu_transport_work);
		if (dispatcher_init) {
			mcu_touch_unregister_listener(on_mcu_request_control,
						      MCU_TOUCH_COMMAND_MCU_REQUEST_CONTROL);
			mcu_touch_unregister_listener(on_mcu_touch_event_received,
						      MCU_TOUCH_COMMAND_POINT_DATA);
		}
		if (i2c_arb_init) {
			unregister_i2c_arbitration_event_notifier(
				raydium_i2c_arbitration_notifier_callback);
		}
		dispatcher_init = false;
		i2c_arb_init = false;
	}
}

static int raydium_ts_probe(struct i2c_client *client)
{
	struct raydium_ts_devicetree_data dt_data;
	struct input_dev *input_dev;
	unsigned short u16_i2c_data;
	int ret = 0;
	struct wti_common_apis wti_apis = { 0 };

	LOGI("probe\n");

	g_raydium_ts = devm_kzalloc(&client->dev,
				    sizeof(struct raydium_ts_data),
				    GFP_KERNEL);
	if (!g_raydium_ts) {
		LOGE("failed to allocate input driver data\n");
		return -ENOMEM;
	}

	if (client->dev.of_node) {
		g_raydium_ts->pdata = devm_kzalloc(
			&client->dev, sizeof(struct raydium_ts_platform_data), GFP_KERNEL);
		if (!g_raydium_ts->pdata) {
			LOGE("failed to allocate memory\n");
			ret = -ENOMEM;
			goto allocate_pdata_failed;
		}

		ret = raydium_parse_dt(&client->dev, g_raydium_ts->pdata, &dt_data);
		if (ret) {
			LOGE("device tree parsing failed\n");
			goto parse_dt_failed;
		}
	} else
		g_raydium_ts->pdata = client->dev.platform_data;

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		ret = -ENODEV;
		goto exit_check_functionality_failed;
	}

	raydium_variable_init();

	timer_setup(&g_raydium_ts->stats_timer, stats_timer_func, 0);
	mutex_init(&g_raydium_ts->lock);
	mutex_init(&g_raydium_ts->sensor_impact_lock);
	mutex_init(&g_raydium_ts->irq_lock);
	mutex_init(&g_raydium_ts->irq2_lock);
	mutex_init(&g_raydium_ts->report_input_event_lock);
	mutex_init(&g_display_data.lock);
	mutex_init(&g_raydium_ts->factory_test_mode_lock);
	mutex_init(&g_raydium_ts->offload_lock);

	init_completion(&g_raydium_ts->resume_done);
	complete_all(&g_raydium_ts->resume_done);

	i2c_set_clientdata(client, g_raydium_ts);
	cpu_latency_qos_add_request(&g_raydium_ts->pm_qos_req, PM_QOS_CPU_LATENCY_DEFAULT_VALUE);
	client->irq = gpio_to_irq(g_raydium_ts->pdata->irq_gpio);
	if (client->irq < 0) {
		LOGE("get irq failed\n");
		ret = -EINVAL;
		goto err_get_irq_failed;
	}
	g_raydium_ts->client = client;
	g_raydium_ts->is_suspend = 0;
	g_raydium_ts->is_sleep_bits = 0;
	g_raydium_ts->irq_disable_bits = 0;
	g_raydium_ts->irq_wake_disable_bits = 0;
	/* IRQ2 is disabled by default, only enable it when we offload to MCU */
	g_raydium_ts->irq2_enable_bits = 0;
	g_raydium_ts->fw_version = 0;
	g_raydium_ts->factory_test_mode = OFF;
	g_display_data.power_state = DISPLAY_INTERACTIVE;
	device_init_wakeup(&client->dev, 1);

	INIT_LIST_HEAD(&touch_event_list_head.list);
	switch_driver_report_mode(NORMAL);

	INIT_DELAYED_WORK(&g_raydium_ts->delay_touch_event_work,
			  raydium_ts_release_queued_input_event);
	raydium_touch_event_delay_wq =
		alloc_workqueue("raydium_touch_event_delay_wq", WQ_UNBOUND | WQ_MEM_RECLAIM, 1);
	if (!raydium_touch_event_delay_wq) {
		LOGE("raydium_touch_event_delay_wq create workqueue failed\n");
		ret = -ENOMEM;
		goto err_create_raydium_touch_event_delay_wq_failed;
	}

#ifdef MSM_NEW_VER
	ret = raydium_ts_pinctrl_init();
	if (!ret && g_raydium_ts->ts_pinctrl) {
		/*
		 * Pinctrl handle is optional. If pinctrl handle is found
		 * let pins to be configured in active state. If not
		 * found continue further without error.
		 */
		ret = pinctrl_select_state(g_raydium_ts->ts_pinctrl,
					   g_raydium_ts->pinctrl_state_active);
		if (ret < 0)
			LOGE("failed to set pin to active state\n");
	}
#endif /*end of MSM_NEW_VER*/

	ret = raydium_gpio_configure(true);
	if (ret < 0) {
		LOGE("failed to configure the gpios\n");
		goto err_gpio_req;
	}
	/*modify dtsi to 360*/
	ret = raydium_hw_reset_fun(client);
	if (ret < 0) {
		// Touch FW is not initialized successfully, wait for a while to retry
		ret = -EPROBE_DEFER;
		goto exit_hw_reset_failed;
	}
	msleep(g_raydium_ts->pdata->soft_rst_dly);
	if (raydium_i2c_init() == ERROR) {
		LOGE("disable i2c deglicth NG!\n");
		ret = -ENODEV;
		goto exit_check_i2c;
	}

	/*print touch i2c ready*/
	ret = raydium_check_i2c_ready(&u16_i2c_data);
	if (ret < 0) {
		LOGE("Check I2C failed\n");
		ret = -ENODEV;
		goto exit_check_i2c;
	}

	ret = raydium_set_resolution();
	if (ret < 0)
		LOGE("get FW resolution failed\n");

	/*input device initialization*/
	input_dev = input_allocate_device();
	if (!input_dev) {
		ret = -ENOMEM;
		LOGE("failed to allocate input device\n");
		goto exit_input_dev_alloc_failed;
	}

	g_raydium_ts->input_dev = input_dev;
	raydium_input_set(input_dev);

	ret = input_register_device(input_dev);
	if (ret) {
		// input_free_device should be called only when the input_register_device is failed
		input_free_device(input_dev);
		LOGE("failed to register input device: %s\n", dev_name(&client->dev));
		goto exit_input_register_device_failed;
	}

	input_set_capability(input_dev, EV_KEY, KEY_SLEEP);
	input_set_capability(input_dev, EV_KEY, KEY_WAKEUP);
	input_set_capability(input_dev, EV_KEY, KEY_UP);
	input_set_capability(input_dev, EV_KEY, KEY_DOWN);
	input_set_capability(input_dev, EV_KEY, KEY_LEFT);
	input_set_capability(input_dev, EV_KEY, KEY_RIGHT);

	/*suspend/resume routine*/
#if defined(CONFIG_FB)
	raydium_register_notifier();
#elif defined(CONFIG_HAS_EARLYSUSPEND)
	/*Early-suspend level*/
	g_raydium_ts->early_suspend.level = EARLY_SUSPEND_LEVEL_BLANK_SCREEN + 1;
	g_raydium_ts->early_suspend.suspend = raydium_ts_early_suspend;
	g_raydium_ts->early_suspend.resume = raydium_ts_late_resume;
	register_early_suspend(&g_raydium_ts->early_suspend);
#elif IS_ENABLED(CONFIG_QCOM_PANEL_EVENT_NOTIFIER)
	g_raydium_ts->notifier_cookie = panel_event_notifier_register(
		PANEL_EVENT_NOTIFICATION_PRIMARY, PANEL_EVENT_NOTIFIER_CLIENT_PRIMARY_TOUCH,
		dt_data.panel, &raydium_panel_notifier_callback, NULL);
	if (IS_ERR(g_raydium_ts->notifier_cookie)) {
		ret = PTR_ERR(g_raydium_ts->notifier_cookie);
		LOGE("Failed to register panel events\n");
		goto err_register_suspend_resume_failed;
	}
#endif /*end of CONFIG_FB*/

#ifdef CONFIG_RM_SYSFS_DEBUG
	raydium_create_sysfs(client);
#endif/*end of CONFIG_RM_SYSFS_DEBUG*/

	if (g_raydium_ts->touch_nfc_filter_enable && gpio_is_valid(g_raydium_ts->pdata->nfc_gpio) &&
	    g_raydium_ts->pdata->nfc_active_jiffies > 0) {
		g_raydium_ts->nfc_irq = gpio_to_irq(g_raydium_ts->pdata->nfc_gpio);
		if (g_raydium_ts->nfc_irq < 0) {
			LOGE("get nfc irq failed\n");
			ret = -EINVAL;
			goto err_get_nfc_irq_failed;
		}
		ret = request_irq(g_raydium_ts->nfc_irq, raydium_ts_nfc_irq_handler,
				  IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING,
				  client->dev.driver->name, g_raydium_ts);
		if (ret != 0) {
			LOGE("request irq (NFC idle) failed. ret = %d\n", ret);
			goto exit_nfc_irq_request_failed;
		}
	}

	wti_apis.get_fw_version = get_fw_version;
	wti_apis.get_grid_spec = get_grid_spec;
	wti_apis.get_display_resolution = get_display_resolution;
	wti_apis.get_hardware_identifier = get_hardware_identifier;

	wti = wti_probe(g_raydium_ts, &g_raydium_ts->client->dev, input_dev, &wti_apis);
	if (!wti) {
		LOGE("failed to probe wear touch interface\n");
		goto err_probe_wear_touch_interface;
	}
	ret = init_capturer(&wti->capturer, TOUCH_STREAMING_MODE_DIFF_ON_EVENTS,
			    g_raydium_ts->grid_spec);
	if (ret) {
		LOGE("failed to init data capturer\n");
		goto err_init_data_capturer;
	}

	raydium_setup_edge_rejection_parameters_locked();

	ret = request_threaded_irq(client->irq, raydium_ts_top_handler, raydium_ts_interrupt,
				   IRQF_TRIGGER_FALLING | IRQF_ONESHOT, client->dev.driver->name,
				   g_raydium_ts);

	if (ret < 0) {
		LOGE("raydium_probe: request irq failed\n");
		goto exit_irq_request_failed;
	}

	if (gpio_is_valid(g_raydium_ts->pdata->irq2_gpio)) {
		g_raydium_ts->irq2 = gpio_to_irq(g_raydium_ts->pdata->irq2_gpio);
		if (g_raydium_ts->irq2 < 0) {
			LOGE("get irq2 failed\n");
			ret = -EINVAL;
			goto err_get_irq2_failed;
		}

		ret = request_threaded_irq(g_raydium_ts->irq2, raydium_ts_top_handler2,
					   raydium_ts_interrupt2, IRQF_TRIGGER_LOW | IRQF_ONESHOT,
					   client->dev.driver->name, g_raydium_ts);
		if (ret < 0) {
			LOGE("raydium_probe: request irq2 failed, ret = %d\n", ret);
			goto exit_irq2_request_failed;
		} else {
			disable_irq(g_raydium_ts->irq2);
			LOGI("IRQ2 request success but disable it for now\n");
		}
	}

	ret = enable_irq_wake(client->irq); // IRQ wake is disabled by default
	if (ret < 0) {
		LOGE("%s(): failed to enable IRQ wake\n", __func__);
		goto exit_enable_irq_wake_failed;
	}
	g_raydium_ts->irq_desc = irq_to_desc(client->irq);

	LOGI("RAD Touch driver ver :0x%X\n", g_u32_driver_version);

	/*read PID*/
	ret = raydium_check_pid();
	if (ret < 0) {
		LOGE("Check PID failed\n");
		ret = -ENODEV;
		goto exit_check_pid_failed;
	}

	/*fw update check*/
	g_raydium_ts->finish_fw_update = false;
	ret = raydium_fw_update_init(u16_i2c_data);
	if (ret < 0) {
		LOGE("FW update init failed\n");
		ret = -ENODEV;
		goto exit_fw_update_failed;
	}

	if (g_raydium_ts->offload_enable) {
		INIT_DELAYED_WORK(&register_mcu_transport_work, register_mcu_transport);
		// Start to register the mcu transport / i2c arbiter right now
		schedule_delayed_work(&register_mcu_transport_work, 0);
	}

	return 0;

exit_fw_update_failed:
exit_check_pid_failed:
exit_enable_irq_wake_failed:
	if (g_raydium_ts->irq2 > 0)
		free_irq(g_raydium_ts->irq2, g_raydium_ts);
exit_irq2_request_failed:
	g_raydium_ts->irq2 = 0;
err_get_irq2_failed:
	free_irq(client->irq, g_raydium_ts);
exit_irq_request_failed:
	client->irq = 0;
	try_to_del_timer_sync(&g_raydium_ts->stats_timer);
err_init_data_capturer:
	wti_remove(wti);
	wti = NULL;
err_probe_wear_touch_interface:
	free_irq(g_raydium_ts->nfc_irq, g_raydium_ts);
exit_nfc_irq_request_failed:
	g_raydium_ts->nfc_irq = 0;
err_get_nfc_irq_failed:
#ifdef CONFIG_RM_SYSFS_DEBUG
	raydium_release_sysfs(client);
#endif /*end of CONFIG_RM_SYSFS_DEBUG*/

#if defined(CONFIG_FB)
	raydium_unregister_notifier();
#elif IS_ENABLED(CONFIG_QCOM_PANEL_EVENT_NOTIFIER)
	if (!IS_ERR_OR_NULL(g_raydium_ts->notifier_cookie))
		panel_event_notifier_unregister(g_raydium_ts->notifier_cookie);
#endif /*end of CONFIG_FB*/
err_register_suspend_resume_failed:
	input_unregister_device(input_dev);
exit_input_register_device_failed:

exit_input_dev_alloc_failed:
exit_check_i2c:
exit_hw_reset_failed:
	if (gpio_is_valid(g_raydium_ts->pdata->reset_gpio))
		gpio_free(g_raydium_ts->pdata->reset_gpio);
	if (gpio_is_valid(g_raydium_ts->pdata->irq2_gpio))
		gpio_free(g_raydium_ts->pdata->irq2_gpio);
	if (gpio_is_valid(g_raydium_ts->pdata->irq_gpio))
		gpio_free(g_raydium_ts->pdata->irq_gpio);

err_gpio_req:
#ifdef MSM_NEW_VER
	if (g_raydium_ts->ts_pinctrl) {
		if (IS_ERR_OR_NULL(g_raydium_ts->pinctrl_state_release)) {
			devm_pinctrl_put(g_raydium_ts->ts_pinctrl);
			g_raydium_ts->ts_pinctrl = NULL;
		} else {
			if (pinctrl_select_state(g_raydium_ts->ts_pinctrl,
						 g_raydium_ts->pinctrl_state_release))
				LOGE("pinctrl_select_state failed\n");
		}
	}
#endif/*end of MSM_NEW_VER*/
	i2c_set_clientdata(client, NULL);

	if (raydium_touch_event_delay_wq) {
		cancel_delayed_work_sync(&g_raydium_ts->delay_touch_event_work);
		destroy_workqueue(raydium_touch_event_delay_wq);
		raydium_touch_event_delay_wq = NULL;
	}
err_create_raydium_touch_event_delay_wq_failed:
err_get_irq_failed:
	cpu_latency_qos_remove_request(&g_raydium_ts->pm_qos_req);
exit_check_functionality_failed:
parse_dt_failed:
allocate_pdata_failed:
	g_raydium_ts = NULL;
	return ret;
}

static void unregister_panel_event_notifier(void)
{
#if defined(CONFIG_FB)
	raydium_unregister_notifier();
#elif defined(CONFIG_HAS_EARLYSUSPEND)
	unregister_early_suspend(&g_raydium_ts->early_suspend);
#elif IS_ENABLED(CONFIG_QCOM_PANEL_EVENT_NOTIFIER)
	if (!IS_ERR_OR_NULL(g_raydium_ts->notifier_cookie))
		panel_event_notifier_unregister(g_raydium_ts->notifier_cookie);
#endif /*end of CONFIG_FB*/
}

/* raydium_ts_remove() is called when modules is detached.
 * raydium_ts_close() will be called to power down touch controller to save power followed by
 * input_unregister_device().
 */
static void raydium_ts_remove(struct i2c_client *client)
{
	unregister_panel_event_notifier();
	/* input_unregister_device will free "input_dev" if input_dev was registered successfully,
	 * there's no need to call input_free_device again
	 */
	input_unregister_device(g_raydium_ts->input_dev);

	unregister_offload_notifier();

	wti_remove(wti);
	wti = NULL;
#ifdef CONFIG_RM_SYSFS_DEBUG
	raydium_release_sysfs(client);
#endif /*end of CONFIG_RM_SYSFS_DEBUG*/

	free_irq(client->irq, g_raydium_ts);
	if (g_raydium_ts->irq2 > 0)
		free_irq(g_raydium_ts->irq2, g_raydium_ts);
	free_irq(g_raydium_ts->nfc_irq, g_raydium_ts);
	client->irq = 0;
	g_raydium_ts->irq2 = 0;
	g_raydium_ts->nfc_irq = 0;

	if (gpio_is_valid(g_raydium_ts->pdata->reset_gpio))
		gpio_free(g_raydium_ts->pdata->reset_gpio);

	if (gpio_is_valid(g_raydium_ts->pdata->irq2_gpio))
		gpio_free(g_raydium_ts->pdata->irq2_gpio);

	if (gpio_is_valid(g_raydium_ts->pdata->irq_gpio))
		gpio_free(g_raydium_ts->pdata->irq_gpio);

	if (raydium_touch_event_delay_wq) {
		cancel_delayed_work_sync(&g_raydium_ts->delay_touch_event_work);
		destroy_workqueue(raydium_touch_event_delay_wq);
		raydium_touch_event_delay_wq = NULL;
	}

	/* Clear unreported stats */
	if (try_to_del_timer_sync(&g_raydium_ts->stats_timer) > 0)
		report_touch_stats();
	cpu_latency_qos_remove_request(&g_raydium_ts->pm_qos_req);
	g_raydium_ts = NULL;
	i2c_set_clientdata(client, NULL);
}

/* The difference between raydium_ts_shutdown() and raydium_ts_remove() is that
 * we need to ensure touch reset pin is pulled down to low 10ms before powering down
 */
static void raydium_ts_shutdown(struct i2c_client *client)
{
	unregister_panel_event_notifier();
	/* input_unregister_device will free "input_dev" if input_dev was registered successfully,
	 * there's no need to call input_free_device again
	 */
	input_unregister_device(g_raydium_ts->input_dev);

#ifdef CONFIG_RM_SYSFS_DEBUG
	raydium_release_sysfs(client);
#endif /*end of CONFIG_RM_SYSFS_DEBUG*/

	unregister_offload_notifier();

	free_irq(client->irq, g_raydium_ts);
	if (g_raydium_ts->irq2 > 0)
		free_irq(g_raydium_ts->irq2, g_raydium_ts);
	free_irq(g_raydium_ts->nfc_irq, g_raydium_ts);
	client->irq = 0;
	g_raydium_ts->irq2 = 0;
	g_raydium_ts->nfc_irq = 0;

	if (gpio_is_valid(g_raydium_ts->pdata->reset_gpio)) {
		/* Pull reset pin to low 10ms before powering down to match hardware spec*/
		gpio_set_value(g_raydium_ts->pdata->reset_gpio, 0);
		msleep(RAYDIUM_POWEROFF_RESET_DELAY_MSEC);
		gpio_free(g_raydium_ts->pdata->reset_gpio);
	}

	if (gpio_is_valid(g_raydium_ts->pdata->irq2_gpio))
		gpio_free(g_raydium_ts->pdata->irq2_gpio);

	if (gpio_is_valid(g_raydium_ts->pdata->irq_gpio))
		gpio_free(g_raydium_ts->pdata->irq_gpio);

	/* Clear unreported stats */
	if (try_to_del_timer_sync(&g_raydium_ts->stats_timer) > 0)
		report_touch_stats();

	if (raydium_touch_event_delay_wq) {
		cancel_delayed_work_sync(&g_raydium_ts->delay_touch_event_work);
		destroy_workqueue(raydium_touch_event_delay_wq);
		raydium_touch_event_delay_wq = NULL;
	}
	cpu_latency_qos_remove_request(&g_raydium_ts->pm_qos_req);
	g_raydium_ts = NULL;
	i2c_set_clientdata(client, NULL);
}

static const struct i2c_device_id raydium_ts_id[] = {
	{RAYDIUM_NAME, 0},
	{}
};

MODULE_DEVICE_TABLE(i2c, raydium_ts_id);

#ifdef CONFIG_OF
static const struct of_device_id raydium_match_table[] = {
	{ .compatible = "raydium,raydium-ts",},
	{ },
};
#else
#define raydium_match_table NULL
#endif/*end of CONFIG_OF*/

static struct i2c_driver raydium_ts_driver = {
	.probe = raydium_ts_probe,
	.remove = raydium_ts_remove,
	.shutdown = raydium_ts_shutdown,
	.id_table = raydium_ts_id,
	.driver = {
		.name = RAYDIUM_NAME,
		.owner = THIS_MODULE,
		.of_match_table = raydium_match_table,
#if defined(CONFIG_PM)
		.pm    = &raydium_ts_pm_ops,
#endif/*end of CONFIG_PM*/
	},
};

module_i2c_driver(raydium_ts_driver);

MODULE_AUTHOR("<Rejion>");
MODULE_DESCRIPTION("Raydium TouchScreen driver");
MODULE_LICENSE("Dual BSD/GPL");
MODULE_SOFTDEP("pre: mcu_transport_autobahn i2c_arbitration_interface");
