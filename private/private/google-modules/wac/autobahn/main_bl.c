// SPDX-License-Identifier: GPL-2.0-only
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

#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/slab.h>
#include <linux/iio/iio.h>
#include <linux/firmware.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/list.h>
#include <linux/of_gpio.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/poll.h>
#include <linux/regmap.h>
#include <linux/spinlock.h>
#include <linux/semaphore.h>
#include <linux/sched.h>
#include <linux/sched/rt.h>
#include <linux/sched/signal.h>
#include <linux/sched/types.h>
#include <linux/time.h>
#include <linux/uaccess.h>
#include <linux/vmalloc.h>
#include <uapi/linux/sched/types.h>
//-----------------------
#include <linux/spi/spi.h>
//-----------------------

#include "hw_reset.h"
#include "main.h"
#include "spi.h"
#include "bl_nxp.h"

ssize_t
autobahn_firmware_name_query(struct device *dev,
			     struct device_attribute *attr,
			     char *buf)
{
	struct autobahn_data *data = dev_get_drvdata(dev);
	const struct autobahn_platform_data *pdata = data->pdata;

	if (pdata->firmware_name) {
		strcpy(buf, pdata->firmware_name);
		return strlen(pdata->firmware_name);
	}
	return 0;
}

ssize_t
autobahn_download_firmware_request(struct device *dev,
				   struct device_attribute *attr,
				   const char *buf, size_t count)
{
	struct autobahn_data *data = dev_get_drvdata(dev);
	const struct autobahn_platform_data *pdata = data->pdata;
	struct firmware_request *fw_request = &data->firmware_request;
	char *fw_name = fw_request->fw_name;
	uint32_t *fw_image_offset = &fw_request->fw_image_offset;
	int i, ret;

	if (!pdata->firmware_download_valid) {
		ret = -ENOSYS;
		goto out;
	}

	if (atomic_cmpxchg(&fw_request->state, FW_IDLE, FW_PENDING) != FW_IDLE) {
		ret = -EBUSY;
		goto out;
	}

	if (count == 0 || count >= FW_NAME_SIZE) {
		ret = -ENAMETOOLONG;
		goto out;
	}

	// Handle \n or unterminated name.
	memcpy(fw_name, buf, count);
	if (fw_name[count - 1] == '\n')
		fw_name[count - 1] = 0;
	fw_name[count] = 0;

	// Parse firmware name then firmware image offset which are expected
	// to be separated by a space. Offset is optional.
	*fw_image_offset = 0;
	for (i = 0; i < strlen(fw_name); i++) {
		if (fw_name[i] == ' ') {
			ret = kstrtou32(fw_name + i + 1, 10, fw_image_offset);
			fw_name[i] = 0;
			break;
		}
	}

	atomic_set(&fw_request->state, FW_REQUESTED);
	autobahn_notify_thread(data);
	ret = wait_for_completion_killable(&fw_request->fw_complete);
	if (ret != 0)
		goto out;

	ret = fw_request->result;

out:
	if (ret < 0)
		pr_err("autobahn: firmware request failed: %s err=%d\n",
				fw_name, ret);
	atomic_set(&fw_request->state, FW_IDLE);

	return ret < 0 ? ret : count;
}

void autobahn_download_firmware(struct device *dev)
{
	struct autobahn_data *data = dev_get_drvdata(dev);
	struct firmware_request *fw_request = &data->firmware_request;
	const struct firmware *fw_entry;
	int ret;

	autobahn_drv_pause();

	pr_info("autobahn: firmware download: %s\n", fw_request->fw_name);
	pr_info("autobahn: firmware image offset: %u\n",
			fw_request->fw_image_offset);

	ret = autobahn_bl_open(data);
	if (ret < 0) {
		pr_err("%s: autobahn_bl_open failed: %d\n", __func__, ret);
		goto out;
	}

	atomic_set(&fw_request->state, FW_IN_PROGRESS);
	ret = request_firmware(&fw_entry, fw_request->fw_name, dev);
	if (ret < 0)
		goto out;

	// Disable irq1 as its GPIO is used by the bootloader.
	if (data->irq1)
		disable_irq(data->irq1);

	// FW download uses SERIAL_ISP mode 110.
	autobahn_hw_reset(data, 0b110);

	if (fw_request->fw_image_offset != 0) {
		ret = autobahn_bl_download_firmware_buffer(
				data, fw_entry->data + fw_request->fw_image_offset,
				fw_entry->size - fw_request->fw_image_offset);
	} else {
		ret = autobahn_bl_download_firmware(data, fw_entry->data,
				fw_entry->size);
	}

	if (data->irq1)
		enable_irq(data->irq1);

	release_firmware(fw_entry);
out:
	pr_info("autobahn: firmware download complete: %s err=%d\n",
			fw_request->fw_name, ret);
	fw_request->result = ret;
	atomic_set(&fw_request->state, FW_COMPLETE);
	complete(&fw_request->fw_complete);
	autobahn_bl_close(data);
	autobahn_drv_unpause();
}
