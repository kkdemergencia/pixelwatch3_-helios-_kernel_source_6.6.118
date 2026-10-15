/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2024 Google, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#ifndef _MAIN_H
#define _MAIN_H

#include <linux/kernel.h>
#include <linux/cdev.h>
#include <linux/gpio.h>
#include <linux/gpio/consumer.h>
#include <linux/list.h>
#include <linux/spinlock.h>
#include <linux/semaphore.h>

#include "autobahn/transport.h"
#include "autobahn_drv.h"
#include "main_bl.h"
#include "time_sync.h"

enum fw_state {
	FW_IDLE,
	FW_PENDING,
	FW_REQUESTED,
	FW_IN_PROGRESS,
	FW_COMPLETE,
};

enum spi_driver_impl_type {
	NXP_FD,
	AMBIQ_HD,
	AMBIQ_FD
};

struct autobahn_platform_data {
	u32 nreset_gpio;
	u32 boot0_gpio;
	u32 boot2_gpio;
	u32 spi_cs_gpio;
	s32 blstatus_gpio;
	struct gpio_desc *irq1_gpio;
	struct gpio_desc *wakeup_gpio;
	struct regmap *pmic_regmap;
	u32 afe_control_reg;
	u32 mag_control_reg;
	u32 bclbig_comp_mode_ctl_reg;
	u32 bclbig_comp_en_ctl1_reg;
	u32 bclbig_comp_iadc_interval_hpm_reg;
	u32 bclbig_comp_iadc_interval_lpm_reg;
	u32 bclbig_comp_iadc_conv_req_reg;
	u32 vddcore_control_reg;
	u32 vddcore_enable_reg;
	u32 vddcore_voltage;
	u32 reg_en_gpio;
	u32 clk32_control_reg;
	u32 bob_ext_ctrl1_control_reg;
	uint supplies_cnt;
	struct regulator **supplies;
	u32 s5a_pgood_state_reg;
	enum spi_driver_impl_type spi_driver_impl;
	const char *firmware_name;

	/*
	 * Flags indicating device-specific feature applicability based on
	 * device tree configuration.
	 */

	/* Flag that maps to the "sensorhub,firmware-download-enabled" device
	 * tree property. When true, a thread is created for managing the
	 * firmware download requests come from the user-space apps. When false,
	 * the requests are replied with -ENOSYS code.
	 */
	bool firmware_download_valid;
	/* Flag that specifies if the blstatus_gpio is valid. When true,
	 * it enables reading the blstatus pin state via a sysfs attribute.
	 */
	bool blstatus_gpio_valid;
};

struct autobahn_data {
	struct device *dev;
	struct device *nanohub_comms_dev;

	struct autobahn_bl bl;
	const struct autobahn_platform_data *pdata;
	int irq1;

	int kthread_run;
	wait_queue_head_t kthread_wait;
	struct task_struct *thread;
	int thread_to_exit;
	struct firmware_request firmware_request;

	struct time_sync_data *time_sync_data;
};

int autobahn_probe(struct device *dev, struct autobahn_data *data);
int autobahn_framework_init(struct autobahn_data *data,
			    struct abn_Transport *abn_transport);
void autobahn_start(const struct autobahn_data *data);
int autobahn_reset(const struct autobahn_data *data);
void autobahn_remove(struct autobahn_data *data);
int autobahn_suspend(const struct autobahn_data *data);
int autobahn_suspend_noirq(void);
int autobahn_resume(const struct autobahn_data *data);
void autobahn_shutdown(struct autobahn_data *data);
void autobahn_notify_thread(struct autobahn_data *data);

#endif
