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

#include <linux/build_bug.h>
#include <linux/cdev.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/gpio.h>
#include <linux/gpio/consumer.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/mutex.h>
#include <linux/pm_wakeup.h>
#include <linux/poll.h>
#include <linux/printk.h>
#include <linux/rwsem.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/sysfs.h>
#include <linux/types.h>
#include <linux/wait.h>

#include "autobahn_drv.h"

#include "abn_client_interface.h"
#include "autobahn/llc/channel.h"
#include "autobahn/llc/llc.h"
#include "autobahn/llc/llc_interface.h"
#include "autobahn/llc/proxy.h"
#include "autobahn/mac/initiator.h"
#include "autobahn/platform/linux/checksum.h"
#include "autobahn/platform/linux/signal_in.h"
#include "autobahn/platform/linux/signal_out.h"
#include "autobahn/platform/linux/thread_notification.h"
#include "autobahn/platform/linux/timer.h"
#include "autobahn/sync/mutex_backend/mutex_init.h"
#include "channel_specific_cfg.h"
#include "nanohub_adapter.h"
#include "nanohub_exports.h"

#define AUTOBAHN_FIRST_CHANNEL      0
#define AUTOBAHN_MAX_CHANNELS       255

#define AUTOBAHN_SUSPEND_TIMEOUT_JIFFIES (msecs_to_jiffies(150))

#define AUTOBAHN_TIMEOUT_TRANSFER_DONE_MS	800
#define AUTOBAHN_TIMEOUT_RESPONDER_READY_MS	800
#define AUTOBAHN_MAX_CONSECUTIVE_RESPONDER_TIMEOUT_MS	10000
#define AUTOBAHN_MAX_CONSECUTIVE_RESPONDER_TIMEOUTS		\
	((AUTOBAHN_MAX_CONSECUTIVE_RESPONDER_TIMEOUT_MS) /	\
		(AUTOBAHN_TIMEOUT_RESPONDER_READY_MS))

#define AUTOBAHN_REINIT_WINDOW_EMPTY_FRAME_COUNT 10

struct autobahn_interface {
	abn_client_interface_init init;
	u8 channel_range[2];
};

/* Autobahn client interfaces. Each interface defines distinct channel range
 * that it is bound to. Cdev file operations defined here will route calls
 * according to the channel number.
 */
static const struct autobahn_interface g_autobahn_interfaces[] = {
	{ nanohub_adapter_init, {0, 64} },
};

struct autobahn_dev {
	struct device *dev;
	dev_t autobahn_devt;
	int    dev_major;
	struct cdev cdev;

	bool force_suspend;

	bool abn_tasks_should_stop;
	struct task_struct *abn_initiator_task;
	struct task_struct *abn_proxy_task;

	/* Synchronization for autobahn_drv_pause/unpause. */
	struct mutex autobahn_pause_mutex;
	int autobahn_pause_ready;
	wait_queue_head_t autobahn_pause_wait;

	wait_queue_head_t autobahn_idle_wait;
	int signal_in_irq;

	/* Synchronization rw_semaphore for access to client interfaces.
	 * Access to character device operations is synchronized only against
	 * init/deinit. Any further synchnization between operations must be
	 * implemented by the interface itself.
	 */
	struct rw_semaphore client_interfaces_rwsem;
	struct abn_client_interface
		*client_interfaces[ARRAY_SIZE(g_autobahn_interfaces)];

	struct abn_mac_Initiator_Config abn_initiator_config;
	struct abn_mac_Initiator abn_initiator;

	struct abn_mac_Client client;

	struct abn_mac_Mac abn_mac;
	struct abn_mac_Pm abn_mac_pm;

	struct abn_llc_LLc_Config abn_llc_config;
	struct abn_llc_Llc abn_llc;
	struct abn_llc_Llc_Interface abn_llc_interface;
	struct abn_llc_Llc_Callback_Interface abn_llc_callback_interface;
	struct abn_llc_Llc_ApplicationCallback abn_llc_app_cb;

	struct abn_llc_Llc_Proxy_Client abn_proxy_client;
	struct abn_llc_Llc_Proxy_Config abn_proxy_config;
	struct abn_llc_Llc_Proxy abn_proxy;

	struct abn_llc_Llc_Node abn_llc_nodes[AUTOBAHN_MAX_CHANNELS];
};

static ssize_t autobahn_drv_read(struct file *, char __user *, size_t,
		loff_t *);
static ssize_t autobahn_drv_write(struct file *, const char __user *, size_t,
		loff_t *);
static unsigned int autobahn_drv_poll(struct file *, poll_table *);
static int autobahn_drv_open(struct inode *, struct file *);
static int autobahn_drv_release(struct inode *, struct file *);
static void
autobahn_pm_idle_callback(void *context, enum abn_llc_Pm_IdleState idle_state);

static const struct file_operations autobahn_fops = {
	.owner    = THIS_MODULE,
	.read     = autobahn_drv_read,
	.write    = autobahn_drv_write,
	.poll     = autobahn_drv_poll,
	.open     = autobahn_drv_open,
	.release  = autobahn_drv_release
};

/* The channels with non default characteristics (priority and RX buffer size)
 * have to be added to the following constant array sorted in ascending order
 * by the channel indexes. The default and custom values/characteristics of the channels
 * can be found in "channel_specific_cfg.h".
 */
static const struct channel_specific_cfg g_channels_specific_cfg[] = {
	{
		NANOHUB_COMMS_SERVICE_CHANNEL_ID,
		NANOHUB_COMMS_SERVICE_CHANNEL_PRIORITY,
		DEFAULT_RX_BUFF_SIZE
	},
	{
		NANOHUB_AUDIO_SERVICE_CHANNEL_ID,
		NANOHUB_AUDIO_SERVICE_CHANNEL_PRIORITY,
		DEFAULT_RX_BUFF_SIZE
	},
	{
		NANOHUB_DISPLAY_SERVICE_CHANNEL_ID,
		NANOHUB_DISPLAY_SERVICE_CHANNEL_PRIORITY,
		DEFAULT_RX_BUFF_SIZE
	},
	{
		NANOHUB_RENDER_SERVICE_CHANNEL_ID,
		NANOHUB_RENDER_SERVICE_CHANNEL_PRIORITY,
		DEFAULT_RX_BUFF_SIZE
	},
	{
		NANOHUB_BUFFERED_AP_SENDER_SERVICE_CHANNEL_ID,
		NANOHUB_BUFFERED_AP_SENDER_SERVICE_CHANNEL_PRIORITY,
		DEFAULT_RX_BUFF_SIZE
	},
	{
		NANOHUB_METRICS_SERVICE_CHANNEL_ID,
		NANOHUB_METRICS_SERVICE_CHANNEL_PRIORITY,
		DEFAULT_RX_BUFF_SIZE
	},
	{
		NANOHUB_CONSOLE_SERVICE_CHANNEL_ID,
		NANOHUB_CONSOLE_SERVICE_CHANNEL_PRIORITY,
		DEFAULT_RX_BUFF_SIZE
	},
	{
		NANOHUB_TOUCH_SERVICE_CHANNEL_ID,
		NANOHUB_TOUCH_SERVICE_CHANNEL_PRIORITY,
		DEFAULT_RX_BUFF_SIZE
	},
	{
		NANOHUB_DISPLAY_KERNEL_SERVICE_CHANNEL_ID,
		NANOHUB_DISPLAY_SERVICE_CHANNEL_PRIORITY,
		DEFAULT_RX_BUFF_SIZE
	},
	{
		NANOHUB_KORU_RENDER_SERVICE_CHANNEL_ID,
		NANOHUB_RENDER_SERVICE_CHANNEL_PRIORITY,
		DEFAULT_RX_BUFF_SIZE
	},
	{
		NANOHUB_BT_OFFLOAD_SERVICE_CHANNEL_ID,
		NANOHUB_BT_OFFLOAD_SERVICE_CHANNEL_PRIORITY,
		DEFAULT_RX_BUFF_SIZE
	},
	{
		NANOHUB_TOUCH_KERNEL_SERVICE_CHANNEL_ID,
		NANOHUB_TOUCH_SERVICE_CHANNEL_PRIORITY,
		DEFAULT_RX_BUFF_SIZE
	},
	{
		NANOHUB_TIME_SYNC_SERVICE_CHANNEL_ID,
		NANOHUB_TIME_SYNC_SERVICE_CHANNEL_PRIORITY,
		DEFAULT_RX_BUFF_SIZE
	},
	{
		NANOHUB_BUFFERED_AP_SENDER_DATA_LINK_SERVICE_CHANNEL_ID,
		NANOHUB_BUFFERED_AP_SENDER_SERVICE_CHANNEL_PRIORITY,
		DEFAULT_RX_BUFF_SIZE
	},
	{
		NANOHUB_LOG_SERVICE_CHANNEL_ID,
		NANOHUB_LOG_SERVICE_CHANNEL_PRIORITY,
		DEFAULT_RX_BUFF_SIZE
	},
};

static struct autobahn_dev g_autobahn_dev;
static bool
abn_Llc_App_Callback_Notification(void *client_handle,
				  enum abn_llc_ClientEvent event);

static const struct abn_llc_Llc_Functions llc_functions = {
	.add_channel = abn_llc_Llc_AddChannel,
	.remove_channel = abn_llc_Llc_RequestRemoveChannel,
	.signal_has_tx_data = abn_llc_Llc_TxDataAvailable,
	.signal_rx_space_available = abn_llc_Llc_RxSpaceAvailable,
	.signal_callback_notification = abn_llc_Llc_CallbackNotification,
};

static const struct abn_mac_Client_Functions mac_client_functions = {
	.is_tx_data_available = abn_llc_Llc_IsTxDataAvailable,
	.prepare_buffers = abn_llc_Llc_PrepareBuffers,
	.process_rx_data = abn_llc_Llc_ProcessRxData,
	.handle_transaction_failure = abn_llc_Llc_HandleTransactionFailure,
};

static const struct abn_mac_Mac_Functions mac_functions = {
	.signal_tx_data_available = abn_mac_Initiator_EventNotification,
};

static const struct abn_mac_Pm_Functions mac_pm_functions = {
	.is_idle = abn_mac_Initiator_IsIdle,
	.set_idle_callback = abn_mac_Initiator_SetIdleCallback,
};

static const struct abn_llc_Llc_ApplicationCallback_Functions
llc_app_cb_functions = {
	.notification = abn_Llc_App_Callback_Notification,
};

static const struct abn_llc_Llc_Callbacks_Functions llc_callback_functions = {
	.execute_callbacks = abn_llc_Llc_ExecuteCallbacks,
};

static const struct abn_llc_Llc_Proxy_Client_Functions llc_proxy_functions = {
	.trigger_event_notification = abn_llc_Llc_Proxy_TriggerEventNotification,
};

/* force_suspend:
 *   Sysfs property that controls driver behavior on system suspend.
 *   When set to 1, the driver will not abort system suspend, even if it is in
 *   a busy state - e.g. responder signalled a request to transmit.
 *
 *   Access: Read/Write.
 */
static ssize_t force_suspend_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	return sysfs_emit(buf, "%d\n", g_autobahn_dev.force_suspend);
}

static ssize_t force_suspend_store(struct device *dev,
				   struct device_attribute *attr,
				   const char *buf, size_t count)
{
	bool enable;
	int ret;

	ret = kstrtobool(buf, &enable);
	if (ret)
		return ret;

	g_autobahn_dev.force_suspend = enable;

	pr_info("Set force_supend: %d", enable);
	return count;
}

static DEVICE_ATTR_RW(force_suspend);

static struct attribute *autobahn_drv_attrs[] = {
	&dev_attr_force_suspend.attr,
	NULL,
};

static const struct attribute_group autobahn_drv_group = {
	.attrs = autobahn_drv_attrs,
};

/* Get the channel interface associated to the channel, if it exists. Caller
 * must ensure that the client_interfaces_rwsem is acquired for read or write
 * when calling this function.
 */
static struct abn_client_interface *autobahn_drv_get_interface(int dev_minor)
{
	size_t i;

	for (i = 0; i < ARRAY_SIZE(g_autobahn_dev.client_interfaces); i++) {
		struct abn_client_interface *interface =
			g_autobahn_dev.client_interfaces[i];

		if (interface != NULL &&
		    interface->config->channel_range[0] <= dev_minor &&
		    interface->config->channel_range[1] >= dev_minor) {
			return g_autobahn_dev.client_interfaces[i];
		}
	}

	return NULL;
}

static ssize_t autobahn_drv_read(struct file *file, char __user *buffer,
		size_t count, loff_t *offset)
{
	int ret;
	int dev_minor = iminor(file->f_inode);
	struct abn_client_interface *interface;

	ret = down_read_interruptible(&g_autobahn_dev.client_interfaces_rwsem);
	if (ret < 0)
		return ret;

	interface = autobahn_drv_get_interface(dev_minor);

	if (interface == NULL) {
		ret = -EFAULT;
		goto out;
	}

	if (interface->ops->fops->read == NULL) {
		ret = -ENOTSUPP;
		goto out;
	}

	ret = interface->ops->fops->read(file, buffer, count, offset);

out:
	up_read(&g_autobahn_dev.client_interfaces_rwsem);
	return ret;
}

static ssize_t
autobahn_drv_write(struct file *file, const char __user *buffer, size_t count,
		   loff_t *offset)
{
	int ret;
	int dev_minor = iminor(file->f_inode);
	struct abn_client_interface *interface;

	ret = down_read_interruptible(&g_autobahn_dev.client_interfaces_rwsem);
	if (ret < 0)
		return ret;

	interface = autobahn_drv_get_interface(dev_minor);

	if (interface == NULL) {
		ret = -EFAULT;
		goto out;
	}

	if (interface->ops->fops->write == NULL) {
		ret = -ENOTSUPP;
		goto out;
	}

	ret = interface->ops->fops->write(file, buffer, count, offset);

out:
	up_read(&g_autobahn_dev.client_interfaces_rwsem);
	return ret;
}

static unsigned int autobahn_drv_poll(struct file *file, poll_table *ptable)
{
	int ret;
	int dev_minor = iminor(file->f_inode);
	struct abn_client_interface *interface;

	/* TODO b/329613808: Change to down_read_interruptible after updating
	 * the ABI symbol list.
	 */
	down_read(&g_autobahn_dev.client_interfaces_rwsem);

	interface = autobahn_drv_get_interface(dev_minor);

	if (interface == NULL) {
		ret = -EFAULT;
		goto out;
	}

	if (interface->ops->fops->poll == NULL) {
		ret = -ENOTSUPP;
		goto out;
	}

	ret =  interface->ops->fops->poll(file, ptable);

out:
	up_read(&g_autobahn_dev.client_interfaces_rwsem);
	return ret;
}

static int autobahn_drv_open(struct inode *inode, struct file *file)
{
	int ret;
	int dev_minor = iminor(inode);
	struct abn_client_interface *interface;

	/* TODO b/329613808: Change to down_read_interruptible after updating
	 * the ABI symbol list.
	 */
	down_read(&g_autobahn_dev.client_interfaces_rwsem);

	interface = autobahn_drv_get_interface(dev_minor);

	if (interface == NULL) {
		ret = -EFAULT;
		goto out;
	}

	if (interface->ops->fops->open == NULL) {
		ret = -ENOTSUPP;
		goto out;
	}

	ret =  interface->ops->fops->open(inode, file);

out:
	up_read(&g_autobahn_dev.client_interfaces_rwsem);
	return ret;
}

static int autobahn_drv_release(struct inode *inode, struct file *file)
{
	int ret;
	int dev_minor = iminor(inode);
	struct abn_client_interface *interface;

	/* TODO b/329613808: Change to down_read_interruptible after updating
	 * the ABI symbol list.
	 */
	down_read(&g_autobahn_dev.client_interfaces_rwsem);

	interface = autobahn_drv_get_interface(dev_minor);

	if (interface == NULL) {
		ret = -EFAULT;
		goto out;
	}

	if (interface->ops->fops->release == NULL) {
		ret = -ENOTSUPP;
		goto out;
	}

	ret =  interface->ops->fops->release(inode, file);

out:
	up_read(&g_autobahn_dev.client_interfaces_rwsem);
	return ret;
}

static void autobahn_client_init(void)
{
	g_autobahn_dev.client.functions = &mac_client_functions;
	g_autobahn_dev.client.client_handle = &g_autobahn_dev.abn_llc;
}

static void autobahn_mac_init(void)
{
	g_autobahn_dev.abn_mac.functions = &mac_functions;
	g_autobahn_dev.abn_mac.mac_handle = &g_autobahn_dev.abn_initiator;
	g_autobahn_dev.abn_mac_pm.functions = &mac_pm_functions;
	g_autobahn_dev.abn_mac_pm.mac_handle = &g_autobahn_dev.abn_initiator;
}

static bool
abn_Llc_App_Callback_Notification(void *client_handle,
				  enum abn_llc_ClientEvent event)
{
	switch (event) {
	case ABN_LLC_APPLICATION_CALLBACK_EVENT_INIT_SENT:
		pr_info_ratelimited("ABN_LLC_APPLICATION_CALLBACK_EVENT_INIT_SENT\n");
		break;
	case ABN_LLC_APPLICATION_CALLBACK_EVENT_INIT_RECEIVED:
		pr_info_ratelimited("ABN_LLC_APPLICATION_CALLBACK_EVENT_INIT_RECEIVED\n");
		break;
	case ABN_LLC_APPLICATION_CALLBACK_EVENT_INVALID_FRAME_RECEIVED:
		pr_err_ratelimited("ABN_LLC_APPLICATION_CALLBACK_EVENT_INVALID_FRAME_RECEIVED\n");
		break;
	case ABN_LLC_APPLICATION_CALLBACK_EVENT_INVALID_VERSION:
		pr_err_ratelimited("ABN_LLC_APPLICATION_CALLBACK_EVENT_INVALID_VERSION\n");
		break;
	case ABN_LLC_APPLICATION_CALLBACK_EVENT_NACK_RECEIVED:
		pr_err_ratelimited("ABN_LLC_APPLICATION_CALLBACK_EVENT_NACK_RECEIVED\n");
		break;
	default:
		/* TODO Process Autobahn stats. */
		break;
	}

	return true;
}

static int autobahn_proxy_init(void)
{
	int ret;
	struct abn_llc_Llc_Proxy_Config *config =
		&g_autobahn_dev.abn_proxy_config;

	ret = abn_sync_ThreadNotification_Create(&config->event_notification);
	if (ret < 0)
		return ret;

	g_autobahn_dev.abn_proxy_config.llc_callback =
		&g_autobahn_dev.abn_llc_callback_interface;

	g_autobahn_dev.abn_proxy_client.functions =
		&llc_proxy_functions;
	g_autobahn_dev.abn_proxy_client.proxy_handle =
		&g_autobahn_dev.abn_proxy;

	abn_llc_Llc_Proxy_Init(&g_autobahn_dev.abn_proxy,
			       &g_autobahn_dev.abn_proxy_config);
	return 0;
}

static void autobahn_proxy_deinit(void)
{
	abn_llc_Llc_Proxy_Deinit(&g_autobahn_dev.abn_proxy);
	abn_sync_ThreadNotification_Destroy(g_autobahn_dev.abn_proxy_config.event_notification);
}

/* Initialize the llc instance, along with its dependencies. */
static int autobahn_llc_init(void)
{
	int ret;
	int i;
	struct abn_llc_LLc_Config *config =
		&g_autobahn_dev.abn_llc_config;

	memset(config, 0, sizeof(struct abn_llc_LLc_Config));

	/* Init llc interface */
	g_autobahn_dev.abn_llc_interface.functions = &llc_functions;
	g_autobahn_dev.abn_llc_interface.llc_handle = &g_autobahn_dev.abn_llc;

	g_autobahn_dev.abn_llc_callback_interface.functions = &llc_callback_functions;
	g_autobahn_dev.abn_llc_callback_interface.llc_handle = &g_autobahn_dev.abn_llc;

	/* Init llc application callback */
	g_autobahn_dev.abn_llc_app_cb.functions = &llc_app_cb_functions;
	/* TODO: init to actual handle. For now this is unused */
	g_autobahn_dev.abn_llc_app_cb.application_handle = NULL;

	/* Init llc config */
#ifdef CONFIG_AUTOBAHN_INTEGRITY_CHECK_CRC16_CCITT
	config->integrity_checker = &abn_crc16_ccitt;
#elif CONFIG_AUTOBAHN_INTEGRITY_CHECK_CRC32
	config->integrity_checker = &abn_crc32;
#endif
	config->client = &g_autobahn_dev.abn_llc_app_cb;

	config->reinit_window_empty_frame_count =
		AUTOBAHN_REINIT_WINDOW_EMPTY_FRAME_COUNT;

	ret = abn_sync_Mutex_Create(&config->mutex);
	if (ret < 0) {
		pr_err("abn_sync_Mutex_Create: %d\n", ret);
		return ret;
	}

	ret = abn_sync_Mutex_Create(&config->mutex_callbacks);
	if (ret < 0) {
		pr_err("abn_sync_Mutex_Create: %d\n", ret);
		goto out_with_mutex;
	}

	for (i = 0; i < ABN_LLC_LLC_MAX_RETRANSMIT_WINDOW; i++) {
		config->tx_frames_queue[i] =
			kzalloc(sizeof(*config->tx_frames_queue[0]),
				GFP_KERNEL | GFP_DMA);
		if (config->tx_frames_queue[i] == NULL) {
			i--;
			while (i >= 0) {
				kfree(config->tx_frames_queue[i]);
				i--;
			}
			ret = -ENOMEM;
			goto out_with_mutex_callback;
		}
	}

	config->rx_frame = kzalloc(sizeof(*config->rx_frame), GFP_KERNEL | GFP_DMA);
	if (config->rx_frame == NULL) {
		ret = -ENOMEM;
		goto out_with_tx_frame;
	}

	config->channel_status_update_available =
		kzalloc(sizeof(*config->channel_status_update_available), GFP_KERNEL);
	if (config->channel_status_update_available == NULL) {
		pr_err("Failed to allocate channel_status_update_available\n");
		ret = -ENOMEM;
		goto out_with_rx_frame;
	}

	config->channel_data_available =
		kzalloc(sizeof(*config->channel_data_available), GFP_KERNEL);
	if (config->channel_data_available == NULL) {
		pr_err("Failed to allocate channel_data_available\n");
		ret = -ENOMEM;
		goto out_with_status_update_available;
	}

	for (i = 0; i < ARRAY_SIZE(g_autobahn_dev.abn_llc_nodes) - 1; i++) {
		g_autobahn_dev.abn_llc_nodes[i].next =
			&g_autobahn_dev.abn_llc_nodes[i+1];
	}
	g_autobahn_dev.abn_llc_nodes[i].next = NULL;

	config->head_pool_nodes = g_autobahn_dev.abn_llc_nodes;

	abn_llc_Llc_Init(&g_autobahn_dev.abn_llc,
			config,
			&g_autobahn_dev.abn_mac,
			&g_autobahn_dev.abn_proxy_client,
			&g_autobahn_dev.abn_mac_pm);

	abn_llc_Llc_Pm_SetIdleCallback(&g_autobahn_dev.abn_llc,
				       autobahn_pm_idle_callback, NULL);

	return 0;

out_with_status_update_available:
	kfree(config->channel_status_update_available);
out_with_rx_frame:
	kfree(config->rx_frame);
out_with_tx_frame:
	for (i = 0; i < ABN_LLC_LLC_MAX_RETRANSMIT_WINDOW; i++)
		kfree(config->tx_frames_queue[i]);
out_with_mutex_callback:
	abn_sync_Mutex_Destroy(config->mutex_callbacks);
out_with_mutex:
	abn_sync_Mutex_Destroy(config->mutex);

	return ret;
}

static void autobahn_llc_deinit(void)
{
	int i;
	struct abn_llc_LLc_Config *config =
		&g_autobahn_dev.abn_llc_config;

	kfree(config->channel_data_available);
	kfree(config->channel_status_update_available);
	kfree(config->rx_frame);
	for (i = 0; i < ABN_LLC_LLC_MAX_RETRANSMIT_WINDOW; i++)
		kfree(config->tx_frames_queue[i]);

	abn_sync_Mutex_Destroy(config->mutex);
	abn_sync_Mutex_Destroy(config->mutex_callbacks);
}

/* Mark the Autobahn processing loop state as pausable. If another thread
 * called autobahn_pause(), this call will block until autobahn_unpause() is
 * called.
 */
static inline void autobahn_drv_check_pause(void)
{
	WRITE_ONCE(g_autobahn_dev.autobahn_pause_ready, 1);
	wake_up_interruptible(&g_autobahn_dev.autobahn_pause_wait);

	/* Will block here if pause was requested. */
	mutex_lock(&g_autobahn_dev.autobahn_pause_mutex);

	WRITE_ONCE(g_autobahn_dev.autobahn_pause_ready, 0);

	mutex_unlock(&g_autobahn_dev.autobahn_pause_mutex);
}

/* Autobahn MAC initiator thread. Runs
 * abn_mac_Initiator_ProcessAllEventsAndWaitForNextEvent() in a loop.
 *
 * Processing can be paused by calling autobahn_drv_pause(). This can be used
 * if the driver needs to run operations which are unsafe while Autobahn is
 * running.
 *
 * Thread can be stopped with kthread_stop().
 */
static int abn_initiator_kthread(void *data)
{
	pr_debug("Starting ABN initiator kthread\n");

	while (!kthread_should_stop() &&
			!READ_ONCE(g_autobahn_dev.abn_tasks_should_stop)) {
		abn_mac_Initiator_ProcessAllEventsAndWaitForNextEvent(
				&g_autobahn_dev.abn_initiator);
		autobahn_drv_check_pause();
	}

	pr_debug("Exit ABN initiator kthread\n");

	kthread_complete_and_exit(NULL, 0);
}

static int abn_proxy_kthread(void *data)
{
	pr_debug("Starting ABN proxy kthread\n");

	while (!kthread_should_stop() &&
	       !READ_ONCE(g_autobahn_dev.abn_tasks_should_stop)) {
		abn_llc_Llc_Proxy_ProcessAllNotificationsAndWaitForNextEvent(
				&g_autobahn_dev.abn_proxy);
	}

	pr_debug("Exit ABN proxy kthread\n");

	kthread_complete_and_exit(NULL, 0);
}

/* Autobahn MAC initiator callback. MAC layer errors are reported through
 * this callback.
 */
static void
abn_initiator_callback(void *context,
		       enum abn_mac_Initiator_CallbackOpcode opcode,
		       enum abn_mac_Initiator_State state,
		       uint32_t flags)
{
	switch (opcode) {
	case ABN_MAC_INITIATOR_CALLBACK_OPCODE_TRANSACTION_ERROR:
		pr_err_ratelimited("MAC transaction error: state %d, flags %u\n",
				   state, flags);
		break;
	case ABN_MAC_INITIATOR_CALLBACK_OPCODE_TIMER_EXPIRED:
		pr_info("MAC timer expired: state %d, flags %u\n", state, flags);
		break;
	case ABN_MAC_INITIATOR_CALLBACK_OPCODE_INVALID_STATE:
		pr_warn_ratelimited("MAC invalid state: state %d, flags %u\n",
				   state, flags);
		break;
	case ABN_MAC_INITIATOR_CALLBACK_OPCODE_RESPONDER_NOT_RESPONDING:
		abn_llc_Llc_ReInit(&g_autobahn_dev.abn_llc);
		pr_warn("MAC responder is unresponsive! Reinitializing LLC.\n");
		break;
	default:
		/* TODO integrate statistics gathering on non-error opcodes. */
		break;
	}
}

/* Initialize the abn_mac_Initiator instance, along with its dependencies. */
static int
autobahn_initiator_init(struct abn_Transport *abn_transport,
			struct gpio_desc *signal_in_gpio,
			struct gpio_desc *signal_out_gpio)
{
	int ret;
	struct abn_mac_Initiator_Config *config =
		&g_autobahn_dev.abn_initiator_config;

	config->transport = abn_transport;

	ret = abn_SignalOut_Create(&config->signal_to_responder, signal_out_gpio);
	if (ret < 0) {
		pr_err("abn_SignalOut_Create: %d\n", ret);
		return ret;
	}

	g_autobahn_dev.signal_in_irq = gpiod_to_irq(signal_in_gpio);
	ret = abn_SignalIn_Create(&config->signal_from_responder, signal_in_gpio);
	if (ret < 0)
		goto out_with_signal_out;

	ret = abn_sync_ThreadNotification_Create(&config->event_notification);
	if (ret < 0) {
		pr_err("abn_sync_ThreadNotification_Create: %d\n", ret);
		goto out_with_signal_in;
	}

	config->handling_responder_signal_changed_event =
		kzalloc(sizeof(*config->handling_responder_signal_changed_event), GFP_KERNEL);
	if (config->handling_responder_signal_changed_event == NULL) {
		pr_err("Failed to allocate handling_responder_signal_changed_event\n");
		goto out_with_event_notification;
	}

	config->transaction_result =
		kzalloc(sizeof(*config->transaction_result), GFP_KERNEL);
	if (config->transaction_result == NULL) {
		pr_err("Failed to allocate transaction_result\n");
		goto out_with_responder_signal_transitioned;
	}

	config->timer_expired =
		kzalloc(sizeof(*config->timer_expired), GFP_KERNEL);
	if (config->timer_expired == NULL) {
		pr_err("Failed to allocate timer_expired\n");
		goto out_with_transaction_result;
	}

	config->state =
		kzalloc(sizeof(*config->state), GFP_KERNEL);
	if (config->state == NULL) {
		pr_err("Failed to allocate state\n");
		goto out_with_timer_expired;
	}

	ret = abn_Timer_Create(&config->timer);
	if (ret < 0) {
		pr_err("abn_Timer_Create: %d\n", ret);
		goto out_with_state;
	}

	ret = abn_sync_Mutex_Create(&config->mutex_callback);
	if (ret < 0) {
		pr_err("abn_sync_Mutex_Create: %d\n", ret);
		goto out_with_timer;
	}

	config->processing_event =
		kzalloc(sizeof(*config->processing_event), GFP_KERNEL);
	if (config->processing_event == NULL) {
		pr_err("Failed to allocate processing_event\n");
		goto out_with_mutex_callback;
	}

	config->client_callback = abn_initiator_callback;
	config->client_callback_context = &g_autobahn_dev;
	config->timeout_transaction_done = msecs_to_jiffies(AUTOBAHN_TIMEOUT_TRANSFER_DONE_MS);
	config->timeout_responder_ready = msecs_to_jiffies(AUTOBAHN_TIMEOUT_RESPONDER_READY_MS);
	config->max_consecutive_responder_timeout_retries =
		AUTOBAHN_MAX_CONSECUTIVE_RESPONDER_TIMEOUTS;

	abn_mac_Initiator_Init(&g_autobahn_dev.abn_initiator,
			       config,
			       &g_autobahn_dev.client);
	autobahn_client_init();
	autobahn_mac_init();

	return 0;

out_with_mutex_callback:
	abn_sync_Mutex_Destroy(config->mutex_callback);
out_with_timer:
	kfree(config->timer);
out_with_state:
	kfree(config->state);
out_with_timer_expired:
	kfree(config->timer_expired);
out_with_transaction_result:
	kfree(config->transaction_result);
out_with_responder_signal_transitioned:
	kfree(config->handling_responder_signal_changed_event);
out_with_event_notification:
	abn_sync_ThreadNotification_Destroy(config->event_notification);
out_with_signal_in:
	abn_SignalIn_Destroy(config->signal_from_responder);
out_with_signal_out:
	abn_SignalOut_Destroy(config->signal_to_responder);

	return ret;
}

/* Deinitialize the abn_mac_Initiator instance and its dependencies. */
static void autobahn_initiator_deinit(void)
{
	struct abn_mac_Initiator_Config *config =
		&g_autobahn_dev.abn_initiator_config;

	abn_Timer_Destroy(config->timer);
	abn_mac_Initiator_Deinit(&g_autobahn_dev.abn_initiator);
	abn_SignalOut_Destroy(config->signal_to_responder);
	abn_SignalIn_Destroy(config->signal_from_responder);
	abn_sync_ThreadNotification_Destroy(config->event_notification);
	kfree(config->handling_responder_signal_changed_event);
	kfree(config->transaction_result);
	kfree(config->timer_expired);
	kfree(config->state);
	kfree(config->processing_event);
	abn_sync_Mutex_Destroy(config->mutex_callback);
}

/* Start the Autobahn mac initiator and proxy threads. Must be called after
 * successful call to autobahn_drv_core_init().
 */
static void autobahn_drv_start_abn_threads(void)
{
	WRITE_ONCE(g_autobahn_dev.abn_tasks_should_stop, false);

	g_autobahn_dev.abn_proxy_task =
		kthread_run(abn_proxy_kthread, NULL, "autobahn_proxy");
	if (IS_ERR(g_autobahn_dev.abn_proxy_task)) {
		pr_err("Couldn't create and run proxy kthread: %ld\n",
		       PTR_ERR(g_autobahn_dev.abn_initiator_task));
		g_autobahn_dev.abn_initiator_task = NULL;
		return;
	}

	g_autobahn_dev.abn_initiator_task =
		kthread_run(abn_initiator_kthread, NULL, "autobahn_mac_initiator");
	if (IS_ERR(g_autobahn_dev.abn_initiator_task)) {
		pr_err("Couldn't create and run initiator kthread: %ld\n",
		       PTR_ERR(g_autobahn_dev.abn_initiator_task));
		g_autobahn_dev.abn_initiator_task = NULL;
		return;
	}

	sched_set_fifo(g_autobahn_dev.abn_initiator_task);
	sched_set_fifo(g_autobahn_dev.abn_proxy_task);
}

/* Initialize the Autobahn core library. This should be called by the
 * driver's probe callback.
 *
 *   dev             - Pointer to the device associated with the Autobahn driver.
 *   abn_transport   - Pointer to an initialized Autobahn transport.
 *   signal_in_gpio  - Signal in GPIO handle. The GPIO must be requested.
 *   signal_out_gpio - Signal out GPIO handle. The GPIO must be requested.
 */
int autobahn_drv_core_init(struct device *dev,
			   struct abn_Transport *abn_transport,
			   struct gpio_desc *signal_in_gpio,
			   struct gpio_desc *signal_out_gpio)
{
	int ret;

	g_autobahn_dev.dev = dev;

	ret = autobahn_initiator_init(abn_transport, signal_in_gpio, signal_out_gpio);
	if (ret < 0)
		return ret;

	ret = autobahn_proxy_init();
	if (ret < 0)
		goto out_with_initiator;

	ret = autobahn_llc_init();
	if (ret < 0)
		goto out_with_proxy;

	ret = sysfs_create_group(&dev->kobj, &autobahn_drv_group);
	if (ret != 0)
		goto out_with_llc;

	device_init_wakeup(dev, true);

	autobahn_drv_start_abn_threads();

	return 0;

out_with_llc:
	autobahn_llc_deinit();
out_with_proxy:
	autobahn_proxy_deinit();
out_with_initiator:
	autobahn_initiator_deinit();

	return ret;
}

/* Stop execution of the Autobahn initiator thread and execute
 * deinitialization. This can be called from the driver's remove callback.
 */
void autobahn_drv_core_deinit(void)
{
	/* Stop the Autobahn initiator and proxy threads first. */
	WRITE_ONCE(g_autobahn_dev.abn_tasks_should_stop, true);

	if (g_autobahn_dev.abn_proxy_task != NULL) {
		abn_llc_Llc_Proxy_TriggerEventNotification(&g_autobahn_dev.abn_proxy);
		kthread_stop(g_autobahn_dev.abn_proxy_task);
	}

	if (g_autobahn_dev.abn_initiator_task != NULL) {
		abn_mac_Initiator_EventNotification(&g_autobahn_dev.abn_initiator);
		kthread_stop(g_autobahn_dev.abn_initiator_task);
	}

	autobahn_proxy_deinit();
	autobahn_initiator_deinit();
	autobahn_llc_deinit();
}

/* Initialize client interfaces defined in g_autobahn_interfaces.
 * Must be called after autobahn_drv_core_init.
 */
int autobahn_drv_client_interfaces_init(void *pdata)
{
	int major = g_autobahn_dev.dev_major;
	size_t i;
	bool at_least_one_interface = false;
	int ret = -ENOENT;

	if (major <= 0) {
		return -ENODEV;
	}

	down_write(&g_autobahn_dev.client_interfaces_rwsem);

	for (i = 0; i < ARRAY_SIZE(g_autobahn_interfaces); i++) {
		struct abn_client_interface_config *pconfig;
		struct abn_client_interface *pclient_interface;
		int res;

		pconfig = kzalloc(sizeof(*pconfig), GFP_KERNEL);
		if (pconfig == NULL)
			continue;

		pconfig->channel_range[0] =
			g_autobahn_interfaces[i].channel_range[0];
		pconfig->channel_range[1] =
			g_autobahn_interfaces[i].channel_range[1];
		if (sizeof(g_channels_specific_cfg) == 0) {
			pconfig->channels_specific_cfg = NULL;
			pconfig->channels_specific_cfg_size = 0;
		} else {
			pconfig->channels_specific_cfg =
				&g_channels_specific_cfg[0];
			pconfig->channels_specific_cfg_size =
				ARRAY_SIZE(g_channels_specific_cfg);
		}
		pconfig->llc_interface = &g_autobahn_dev.abn_llc_interface;

		res = g_autobahn_interfaces[i].init(pconfig,
			&g_autobahn_dev.client_interfaces[i]);
		if (res < 0) {
			pr_err("Failed to initialize interf %zu: %d", i, res);
			kfree(pconfig);
			ret = res;
			continue;
		}

		pclient_interface = g_autobahn_dev.client_interfaces[i];
		res = pclient_interface->ops->create_sysfs_nodes(pdata, major);
		if (res < 0) {
			pr_err("Failed to create nodes interf %zu: %d", i, res);
			pclient_interface->ops->deinit(pclient_interface);
			g_autobahn_dev.client_interfaces[i] = NULL;
			kfree(pconfig);
			ret = res;
		}

		at_least_one_interface = true;
	}

	up_write(&g_autobahn_dev.client_interfaces_rwsem);

	/* Return OK if there is at least one interface properly initialized. */
	return (at_least_one_interface) ? 0 : ret;
}

/* Deinitialize client interfaces defined in g_autobahn_interfaces.
 * Must be called before autobahn_drv_core_init.
 */
void autobahn_drv_client_interfaces_deinit(void)
{
	size_t i;

	down_write(&g_autobahn_dev.client_interfaces_rwsem);

	for (i = 0; i < ARRAY_SIZE(g_autobahn_dev.client_interfaces); i++) {
		if (g_autobahn_dev.client_interfaces[i] != NULL) {
			struct abn_client_interface *pclient_interface =
				g_autobahn_dev.client_interfaces[i];
			struct abn_client_interface_config *pconfig =
				pclient_interface->config;

			pclient_interface->ops->destroy_sysfs_nodes();
			pclient_interface->ops->deinit(pclient_interface);
			g_autobahn_dev.client_interfaces[i] = NULL;

			kfree(pconfig);
		}
	}

	up_write(&g_autobahn_dev.client_interfaces_rwsem);
}

int autobahn_drv_init(void)
{
	dev_t devt;
	int dev_major;
	int ret;

	init_rwsem(&g_autobahn_dev.client_interfaces_rwsem);

	/* Allocate a region for the autobahn channels */
	ret = alloc_chrdev_region(&devt,
				  AUTOBAHN_FIRST_CHANNEL,
				  AUTOBAHN_MAX_CHANNELS,
				  AUTOBAHN_NAME);
	if (ret < 0) {
		pr_err("Failed to allocate device region \'%d\'n", ret);
		return ret;
	}

	dev_major = MAJOR(devt);
	pr_info("Major: %d\n", dev_major);

	cdev_init(&g_autobahn_dev.cdev, &autobahn_fops);
	g_autobahn_dev.cdev.owner = THIS_MODULE;

	ret = cdev_add(&g_autobahn_dev.cdev,
		       MKDEV(dev_major, AUTOBAHN_FIRST_CHANNEL),
		       AUTOBAHN_MAX_CHANNELS);
	if (ret < 0) {
		pr_err("Failed to add device range \'%d\'\n", ret);
		unregister_chrdev_region(devt, AUTOBAHN_MAX_CHANNELS);
		return ret;
	}

	g_autobahn_dev.autobahn_devt = devt;
	g_autobahn_dev.dev_major = dev_major;

	mutex_init(&g_autobahn_dev.autobahn_pause_mutex);
	WRITE_ONCE(g_autobahn_dev.autobahn_pause_ready, 0);
	init_waitqueue_head(&g_autobahn_dev.autobahn_pause_wait);
	init_waitqueue_head(&g_autobahn_dev.autobahn_idle_wait);

	return ret;
}

void autobahn_drv_exit(void)
{
	cdev_del(&g_autobahn_dev.cdev);
	unregister_chrdev_region(g_autobahn_dev.autobahn_devt,
				 AUTOBAHN_MAX_CHANNELS);

	memset(&g_autobahn_dev, 0, sizeof(g_autobahn_dev));
}

/* Pause the Autobahn processing loop. This call will return if the Autobahn
 * processing thread has returned from
 * abn_mac_Initiator_ProcessAllEventsAndWaitForNextEvent().
 * There will be no subsequent calls to
 * abn_mac_Initiator_ProcessAllEventsAndWaitForNextEvent() until
 * autobahn_unpause() is called.
 *
 * This function is safe to call concurrently from multiple threads.
 */
int autobahn_drv_pause(void)
{
	int ret;

	ret = mutex_lock_interruptible(&g_autobahn_dev.autobahn_pause_mutex);
	if (ret < 0)
		goto out;

	/* Signal the Autobahn thread to return early from
	 * abn_mac_Initiator_ProcessAllEventsAndWaitForNextEvent() and wait
	 * for it to block.
	 */
	abn_mac_Initiator_EventNotification(&g_autobahn_dev.abn_initiator);

	ret = wait_event_interruptible(g_autobahn_dev.autobahn_pause_wait,
			READ_ONCE(g_autobahn_dev.autobahn_pause_ready));

out:
	if (ret < 0)
		pr_err("ret: %d\n", ret);

	return ret;
}

/* Resume the Autobahn processing loop. If there is another task blocked in
 * an autobahn_pause() call, it may be unblocked instead.
 */
void autobahn_drv_unpause(void)
{
	mutex_unlock(&g_autobahn_dev.autobahn_pause_mutex);
}

/* Callback registered to Autobahn LLC PM to notify us when Autobah is idle
 * and it safe to suspend.
 */
static void
autobahn_pm_idle_callback(void *context, enum abn_llc_Pm_IdleState idle_state)
{
	if (idle_state == ABN_LLC_PM_STATE_ENTER_IDLE)
		wake_up(&g_autobahn_dev.autobahn_idle_wait);
}

/* Linux pm suspend callback. Waits for any ongoing work in Autobahn to
 * finish, before notifying that it is safe to suspend.
 */
int autobahn_drv_suspend(void)
{
	struct abn_SignalIn *signal_in =
		g_autobahn_dev.abn_initiator_config.signal_from_responder;

	if (!g_autobahn_dev.force_suspend) {
		/* TODO: b/368286913 - Need to clarify why we separately check the state
		 * of the GPIO interrupt signal from MCU instead of implementing this
		 * functionality within the "abn_llc_Llc_Pm_IsIdle()" call.
		 */
		int ret = wait_event_timeout(g_autobahn_dev.autobahn_idle_wait,
				(signal_in->functions->get(signal_in->signal_in_handle) !=
				ABN_SIGNAL_STATE_ACTIVE) &&
				abn_llc_Llc_Pm_IsIdle(&g_autobahn_dev.abn_llc),
				AUTOBAHN_SUSPEND_TIMEOUT_JIFFIES);
		if (ret == 0) {
			pr_info("Timed out waiting for abn_llc_Llc_Pm_IsIdle\n");
			return -EBUSY;
		}
	}

	if (device_may_wakeup(g_autobahn_dev.dev))
		enable_irq_wake(g_autobahn_dev.signal_in_irq);

	autobahn_drv_pause();

	return 0;
}

/* Linux pm suspend_noirq callback. Used to verify that we did not miss a signal
 * in interrupt when the pm subsystem disabled interrupts. After this check
 * passes, enable_irq_wake will ensure that the system wakes on a received
 * interrupt.
 */
int autobahn_drv_suspend_noirq(void)
{
	struct abn_SignalIn *signal_in =
		   g_autobahn_dev.abn_initiator_config.signal_from_responder;

	if (g_autobahn_dev.force_suspend)
		return 0;

	if (signal_in->functions->get(signal_in->signal_in_handle) ==
	    ABN_SIGNAL_STATE_ACTIVE)
		return -EBUSY;

	return 0;
}

/* Linux pm resume callback. Undo any setup done in autobahn_drv_suspend. */
int autobahn_drv_resume(void)
{
	autobahn_drv_unpause();
	if (device_may_wakeup(g_autobahn_dev.dev))
		disable_irq_wake(g_autobahn_dev.signal_in_irq);

	return 0;
}
