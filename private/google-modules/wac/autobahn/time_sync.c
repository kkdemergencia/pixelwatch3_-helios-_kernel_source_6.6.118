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

#define pr_fmt(fmt) "%s:%s: " fmt, KBUILD_MODNAME, __func__

#include <asm/page.h>
#include <linux/compiler_attributes.h>
#include <linux/completion.h>
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/jiffies.h>
#include <linux/ktime.h>
#include <linux/limits.h>
#include <linux/mutex.h>
#include <linux/sysfs.h>
#include <linux/types.h>

#include "main.h"
#include "nanohub_exports.h"

#include "time_sync.h"

#define TIME_SYNC_VERSION 1
#define TIME_SYNC_RESPONSE_TIMEOUT (msecs_to_jiffies(200))

struct time_sync_request {
	uint8_t version;
	uint64_t ap_boottime_ns;
} __packed;

struct time_sync_response {
	uint8_t version;
	uint64_t ap_boottime_ns;
	uint64_t mcu_boottime_ns;
} __packed;

struct time_sync_data {
	struct time_sync_response time_sync_response;
	uint64_t response_boottime_ns;
	struct completion response_completion;
	struct mutex mutex;
};

/* Used to provide context to the message listener. */
struct time_sync_data *g_time_sync_data;

static void time_sync_message_listener(const char *buf, size_t length)
{
	struct time_sync_data *data = READ_ONCE(g_time_sync_data);

	if (data == NULL) {
		pr_warn("Received message before time_sync_data was initialized!");
		return;
	}

	if (length != sizeof(struct time_sync_response)) {
		pr_err("Received unexpected response size: %ld\n", length);
		return;
	}

	data->response_boottime_ns =
		ktime_to_ns(ktime_get_boottime());
	memcpy(&data->time_sync_response, buf, length);
	complete(&data->response_completion);
}

static int time_sync_execute(struct time_sync_data *data,
			     struct time_sync_response *response,
			     uint64_t *latency)
{
	ssize_t send_ret;
	unsigned long wait_ret;
	struct time_sync_request packet;

	reinit_completion(&data->response_completion);

	packet.version = TIME_SYNC_VERSION;
	packet.ap_boottime_ns = ktime_to_ns(ktime_get_boottime());
	send_ret = nanohub_send_message(NANOHUB_TIME_SYNC_CHANNEL_ID,
					(const char *)&packet,
					sizeof(packet));

	if (send_ret < 0) {
		pr_err("Failed to transmit boottime: %zd\n", send_ret);
		return send_ret;
	}

	wait_ret = wait_for_completion_timeout(&data->response_completion,
					       TIME_SYNC_RESPONSE_TIMEOUT);
	if (wait_ret == 0) {
		pr_err("Timed out waiting for response\n");
		return -ETIME;
	}

	if (data->time_sync_response.version != TIME_SYNC_VERSION) {
		pr_err("Unexpected time sync protocol version: %d",
		       data->time_sync_response.version);
		return -EBADE;
	}

	if (data->time_sync_response.ap_boottime_ns != packet.ap_boottime_ns) {
		pr_err("Unexpected ap bootime received: received %llu, expected %llu",
		       data->time_sync_response.ap_boottime_ns,
		       packet.ap_boottime_ns);
		return -EBADE;
	}

	*latency = data->response_boottime_ns -
		   data->time_sync_response.ap_boottime_ns;
	memcpy(response, &data->time_sync_response, sizeof(*response));

	return 0;
}

ssize_t time_sync_show(struct device *dev,
		       struct device_attribute *attr, char *buf)
{
	int ret;
	size_t num_iterations = 3;
	uint64_t latency;
	uint64_t best_latency = U64_MAX;
	struct time_sync_response response;
	struct time_sync_response best_response;
	struct autobahn_data *autobahn_data = dev_get_drvdata(dev);
	struct time_sync_data *data = READ_ONCE(autobahn_data->time_sync_data);

	if (data == NULL)
		return -EAGAIN;

	ret = mutex_lock_interruptible(&data->mutex);
	if (ret < 0)
		return ret;

	/* Transport latency may vary - the peripheral may be busy, powered
	 * off, etc. Execute multiple iterations, measuring latency, and pick
	 * the best result.
	 */
	while (--num_iterations > 0) {
		ret = time_sync_execute(data, &response, &latency);
		if (ret < 0) {
			mutex_unlock(&data->mutex);
			return scnprintf(buf, PAGE_SIZE,
					 "time sync error: %d\n", ret);
		}

		if (latency < best_latency) {
			memcpy(&best_response, &response, sizeof(best_response));
			best_latency = latency;
		}
	}

	mutex_unlock(&data->mutex);

	pr_debug("ap time: %llu mcu time: %llu round trip time: %llu",
		 best_response.ap_boottime_ns, best_response.mcu_boottime_ns,
		 best_latency);

	return scnprintf(buf, PAGE_SIZE, "ap time: %llu mcu time: %llu\n",
			 best_response.ap_boottime_ns,
			 best_response.mcu_boottime_ns);
}

int time_sync_initialize(struct device *dev)
{
	struct autobahn_data *autobahn_data;
	struct time_sync_data *data;
	bool register_ret = false;

	autobahn_data = dev_get_drvdata(dev);

	data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
	if (data == NULL)
		return -ENOMEM;

	init_completion(&data->response_completion);
	mutex_init(&data->mutex);

	/* Do not reorder this write. */
	wmb();
	WRITE_ONCE(g_time_sync_data, data);

	register_ret = nanohub_register_listener(NANOHUB_TIME_SYNC_CHANNEL_ID,
						 time_sync_message_listener);
	if (!register_ret) {
		mutex_destroy(&data->mutex);
		return -EAGAIN;
	}

	WRITE_ONCE(autobahn_data->time_sync_data, data);

	return 0;
}
