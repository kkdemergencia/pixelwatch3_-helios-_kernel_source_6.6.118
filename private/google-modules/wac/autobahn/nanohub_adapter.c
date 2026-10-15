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

#include <linux/compiler_attributes.h>
#include <linux/completion.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/jiffies.h>
#include <linux/mutex.h>
#include <linux/poll.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/types.h>
#include <linux/uaccess.h>
#include <linux/wait.h>
#include <linux/workqueue.h>

#include "nanohub_adapter.h"

#include "abn_msg_queue.h"
#include "autobahn/autobahn.h"
#include "autobahn/llc/frame.h"
#include "autobahn/streaming_channel.h"
#include "autobahn/sync/mutex_backend/mutex_init.h"
#include "autobahn/platform/linux/kunit.h"
#include "channel_specific_cfg.h"
#include "hw_reset.h"
#include "main.h"
#include "main_bl.h"
#include "nanohub_exports.h"
#include "time_sync.h"

/**
 * DEV_STATIC_ATTR_INIT - Initialize a device_attribute for attribute_group usage.
 *
 * This macro simplifies the creation of 'struct device_attribute' structures,
 * making them suitable for inclusion in an 'struct attribute_group'. It
 * combines the attribute name, mode, show function, and store function into
 * a single expression that yields a pointer to a 'struct attribute'.
 *
 * @name: The name of the attribute (file name in sysfs).
 * @mode: The file permissions (e.g., 0444, 0660).
 * @show: The function to read the attribute value (NULL for write-only).
 * @store: The function to write the attribute value (NULL for read-only).
 *
 * Returns:
 * A pointer to a 'struct attribute' suitable for use in a 'struct attribute_group'.
 */
#define DEV_STATIC_ATTR_INIT(_name, _mode, _show, _store)				\
	(&(struct device_attribute) __ATTR(_name, _mode, _show, _store).attr)

/**
 * DEV_DYNAMIC_ATTR_INIT - Initialize a dynamic_sysfs_attr structure.
 *
 * This macro simplifies the creation of sysfs attributes that require a dynamic
 * validity check. It initializes a standard 'struct device_attribute' along with
 * an offset to a boolean member within 'struct autobahn_platform_data'. This
 * boolean member determines whether the attribute is valid for the current device.
 *
 * @name: The name of the attribute (file name in sysfs).
 * @mode: The file permissions (e.g., 0444, 0660).
 * @show: The function to read the attribute value (NULL for write-only).
 * @store: The function to write the attribute value (NULL for read-only).
 * @attr_valid_member_name: The name of the boolean member in
 * 'struct autobahn_platform_data' that indicates the attribute's validity.
 *
 * The resulting structure is intended for use in arrays of
 * 'struct dynamic_sysfs_attr', allowing for management of dynamically valid
 * sysfs attributes.
 */
#define DEV_DYNAMIC_ATTR_INIT(_name, _mode, _show, _store, attr_valid_member_name) {	\
	__ATTR(_name, _mode, _show, _store),						\
	offsetof(struct autobahn_platform_data, attr_valid_member_name)			\
}

#define NANOHUB_CHANNEL_CLOSE_TIMEOUT_MS 1000
#define NANOHUB_ADAPTER_READ_TIMEOUT_MS 2000
#define NANOHUB_ADAPTER_WRITE_TIMEOUT_MS 2000

#define MSG_READ_FREE_POOL_DEPTH   8
#define MSG_WRITE_FREE_POOL_DEPTH  8
#define NANOHUB_DATAGRAM_SIZE 255
#define NANOHUB_HDR_SIZE      (sizeof(((struct nanohub_packet *)(0))->size))

#define SEND_MESSAGE_TIMEOUT_MS 1000

#define NANOHUB_DEV_CLASS "nanohub"

#define NANOHUB_COMMS_DEVICE_NAME "nanohub_comms"
#define NANOHUB_COMMS_CHANNEL_ID 0

typedef void (*message_listener_func_t)(const char *buffer, size_t length);

enum nanohub_ch_alloc_status {
	NANOHUB_CHANNEL_FREE = 0,    /* Not allocated. */
	NANOHUB_CHANNEL_ALLOC_USER,  /* Allocated for userspace api. */
	NANOHUB_CHANNEL_ALLOC_KERNEL /* Allocated for kernel api. */
};

enum nanohub_ch_conn_status {
	NANOHUB_CHANNEL_CLOSED_STATUS,   /* Channel unused or closed. */
	NANOHUB_CHANNEL_OPENING_STATUS,  /* Pending open request. */
	NANOHUB_CHANNEL_OPENED_STATUS,   /* Channel opened. */
	NANOHUB_CHANNEL_FINISHING_STATUS /* To be set only during cleanup. */
};

enum nanohub_ch_read_status {
	NANOHUB_READ_IDLE_STATUS,            /* No read operation in progress. */
	NANOHUB_READ_FULL_STATUS,            /* No read buffers available. */
	NANOHUB_READ_READING_HEADER_STATUS,  /* Waiting packet header. */
	NANOHUB_READ_READING_PAYLOAD_STATUS, /* Waiting for packet payload. */
};

enum nanohub_ch_write_status {
	NANOHUB_WRITE_IDLE_STATUS,
	NANOHUB_WRITE_WRITING_STATUS,
	NANOHUB_WRITE_RETRY_STATUS,   /* Received write error and we want
				       * to retry it.
				       */
};

struct __packed nanohub_packet {
	uint8_t size;
	uint8_t payload[NANOHUB_DATAGRAM_SIZE];
};

struct nanohub_adapt_msg_pool {
	void *pool_alloc;
	struct abn_msg_queue *free_queue;
	struct abn_msg_queue *msg_queue;
};

struct nanohub_adapt_ch {
	atomic_t allocated; /* Used to check whether the channel has been
			     * allocated.
			     */
	atomic_t busy;      /* Used to mark dynamically allocated channels as
			     * used.
			     */
	struct nanohub_adapt_ch_data *pch_data; /* Channel struct allocated on
						 * first use.
						 */
};

struct nanohub_adapt_ch_data {
	uint8_t channel;
	struct abn_StreamingChannel_Client client;
	struct abn_StreamingChannel streaming_ch;

	/* Variables that can only be changed after acquiring "status_spinlock". */
	enum nanohub_ch_conn_status conn_status;

	/* Variable that can only be changed after acquiring
	 * "read_status_spinlock".
	 */
	enum nanohub_ch_read_status read_status;

	spinlock_t status_spinlock;
	spinlock_t read_status_spinlock;
	spinlock_t write_status_spinlock;

	struct mutex read_mutex;

	struct completion close_completion;

	struct nanohub_adapt_msg_pool read_pool;
	struct nanohub_adapt_msg_pool write_pool;
	struct abn_msg_elem *pcurrent_read_msg;

	enum nanohub_ch_write_status write_status;
	bool last_write_status;
	uint8_t write_buffer[ABN_MAX_DATAGRAM_SIZE];
	size_t write_buffer_size;

	wait_queue_head_t poll_wait;

	spinlock_t message_listener_spinlock;
	message_listener_func_t on_message_received;
	struct work_struct message_listener_work;
	bool message_listener_work_pending;
	bool has_message_listener;
};

struct nanohub_adapt_priv {
	struct class *pdev_class;
	int major;
	struct autobahn_data *pautobahn_data;

	struct abn_client_interface_config *pinterface_cfg;

	u8 offset_first_ch;
	u8 num_channels;
	struct nanohub_adapt_ch pchannels[];
};

struct dev_nodes_cfg {
	const char *node_name;
	const uint8_t channel;
};

struct dynamic_sysfs_attr {
	struct device_attribute dev_attr;
	ptrdiff_t attr_is_valid_bool_offset;
};

static ssize_t
nanohub_adapter_read(struct file *file, char __user *buffer,
		     size_t count, loff_t *offset);
static ssize_t
nanohub_adapter_write(struct file *file, const char __user *buffer,
		      size_t count, loff_t *offset);
static unsigned int nanohub_adapter_poll(struct file *file, poll_table *wait);
static int nanohub_adapter_open(struct inode *inode, struct file *file);
static int nanohub_adapter_release(struct inode *inode, struct file *file);
static void nanohub_adapter_deinit(struct abn_client_interface *interface);

static void nanohub_adapter_handle_connection_status(void *client_handle,
		bool open);
static void nanohub_adapter_handle_read_result(void *client_handle,
		bool success, const uint8_t *buffer, size_t buffer_size_bytes);
static void nanohub_adapter_handle_write_result(void *client_handle,
		bool success, const uint8_t *data, size_t data_size_bytes);

static int nanohub_adapter_create_sysfs_nodes(void *pdata, int major);
static void nanohub_adapter_destroy_sysfs_nodes(void);

static void nanohub_adapter_message_listener_work(struct work_struct *work);

static struct nanohub_adapt_priv *g_nanohub_adapt_ppriv;

static const struct file_operations g_nanohub_adapter_fops = {
	.read    = nanohub_adapter_read,
	.write   = nanohub_adapter_write,
	.poll    = nanohub_adapter_poll,
	.open    = nanohub_adapter_open,
	.release = nanohub_adapter_release,
};

static const struct abn_client_interface_ops g_nanohub_adapter_ops = {
	.deinit = nanohub_adapter_deinit,
	.create_sysfs_nodes = nanohub_adapter_create_sysfs_nodes,
	.destroy_sysfs_nodes = nanohub_adapter_destroy_sysfs_nodes,
	.fops = &g_nanohub_adapter_fops,
};

static const struct abn_StreamingChannel_Client_Functions g_ch_functions = {
	.handle_connection_status = nanohub_adapter_handle_connection_status,
	.handle_read_result = nanohub_adapter_handle_read_result,
	.handle_write_result = nanohub_adapter_handle_write_result,
};

static const struct dev_nodes_cfg g_dev_nodes_cfg[] = {
	// Channel number 0 is reserved for comms testing.
	// Channels number 1-15 are reserved for USF.
	{ "nanohub", NANOHUB_USF_FIRST_CHANNEL_ID },
	// Channel number 16 is reserved for audio.
	{ "nanohub_display", NANOHUB_DISPLAY_SERVICE_CHANNEL_ID },
	{ "nanohub_render", NANOHUB_RENDER_SERVICE_CHANNEL_ID },
	{ "nanohub_debug_log", NANOHUB_BUFFERED_AP_SENDER_SERVICE_CHANNEL_ID },
	{ "nanohub_metrics", NANOHUB_METRICS_SERVICE_CHANNEL_ID },
	{ "nanohub_console", NANOHUB_CONSOLE_SERVICE_CHANNEL_ID },
	{ "nanohub_rpc0", NANOHUB_PW_RPC0_SERVICE_CHANNEL_ID },
	{ "nanohub_rpc1", NANOHUB_PW_RPC1_SERVICE_CHANNEL_ID },
	{ "nanohub_brightness", NANOHUB_BRIGHTNESS_SERVICE_CHANNEL_ID },
	{ "nanohub_touch", NANOHUB_TOUCH_SERVICE_CHANNEL_ID },
	// Channel number 26 is reserved for display.
	{ "nanohub_pele", NANOHUB_KORU_RENDER_SERVICE_CHANNEL_ID },
	{ "nanohub_bt", NANOHUB_BT_OFFLOAD_SERVICE_CHANNEL_ID },
	// Channel number 29 is reserved for touch.
	{ "nanohub_wac_bootloader", NANOHUB_BOOTLOADER_SERVICE_CHANNEL_ID },
	// Channel number 31 is reserved for time sync.
	{ "nanohub_debug_log_data_link", NANOHUB_BUFFERED_AP_SENDER_DATA_LINK_SERVICE_CHANNEL_ID },
	{ "nanohub_logs", NANOHUB_LOG_SERVICE_CHANNEL_ID },
};

/* Sysfs attributes that apply to specific platforms or hardware revisions. */
static const struct dynamic_sysfs_attr g_dynamic_sysfs_attrs[] = {
	DEV_DYNAMIC_ATTR_INIT(blstatus, 0440, autobahn_pin_blstatus_get, NULL,
			      blstatus_gpio_valid),
};

/* Sysfs attributes common to all platforms and hardware revisions. */
static struct attribute *g_static_sysfs_attrs[] = {
	DEV_STATIC_ATTR_INIT(hw_reset, 0220, NULL, autobahn_try_hw_reset),
	DEV_STATIC_ATTR_INIT(nreset, 0660, autobahn_pin_nreset_get, autobahn_pin_nreset_set),
	DEV_STATIC_ATTR_INIT(boot0, 0660, autobahn_pin_boot0_get, autobahn_pin_boot0_set),
	DEV_STATIC_ATTR_INIT(boot2, 0660, autobahn_pin_boot2_get, autobahn_pin_boot2_set),
	DEV_STATIC_ATTR_INIT(time_sync, 0440, time_sync_show, NULL),
	DEV_STATIC_ATTR_INIT(firmware_name, 0440, autobahn_firmware_name_query, NULL),

	/* The init process in Android waits up to 5s for the availability of
	 * the `download_firmware` file before starting the MCU management
	 * daemon app. As a consequence we have to leave this attribute at the
	 * end of the list of attributes. Even though download firmware is not
	 * available for all platforms and hardware revisions, the associated
	 * attribute is always exposed and -ENOSYS error code is returned when
	 * the feature does not apply to the platform/hardware revision.
	 */
	DEV_STATIC_ATTR_INIT(download_firmware, 0220, NULL, autobahn_download_firmware_request),
	NULL /* Must be NULL-terminated */
};

static const struct attribute_group g_static_sysfs_attr_group = {
	.attrs = g_static_sysfs_attrs,
};

unsigned long __mockable __must_check copy_to_user_wrapper(
		void __user *to, const void *from, unsigned long n)
{
	return copy_to_user(to, from, n);
}

unsigned long __mockable __must_check
copy_from_user_wrapper(void *to, const void __user *from, unsigned long n)
{
	return copy_from_user(to, from, n);
}

void __mockable
poll_wait_wrapper(struct file *filp, wait_queue_head_t *wait_address, poll_table *p)
{
	poll_wait(filp, wait_address, p);
}

void __mockable wake_up_wrapper(struct wait_queue_head *wq_head)
{
	wake_up(wq_head);
}

static const struct channel_specific_cfg *
get_specific_cfg(struct nanohub_adapt_priv *pnanohub_priv, size_t channel_idx)
{
	const struct channel_specific_cfg *pchannel_specific_cfg =
		pnanohub_priv->pinterface_cfg->channels_specific_cfg;
	size_t ch_cfg_size;
	size_t i;

	if (pchannel_specific_cfg == NULL)
		return NULL;

	ch_cfg_size = pnanohub_priv->pinterface_cfg->channels_specific_cfg_size;
	for (i = 0; i < ch_cfg_size; i++) {
		if (pchannel_specific_cfg->index == channel_idx) {
			return pchannel_specific_cfg;
		} else if (pchannel_specific_cfg->index > channel_idx) {
			/* NOTE: Assumed the specific configuration is ordered
			 * by channel index.
			 */
			break;
		}
		pchannel_specific_cfg++;
	}
	return NULL;
}

/* Allocate a pool of messages. */
static int
nanohub_adapter_alloc_msg_pool(struct nanohub_adapt_msg_pool *pool,
			       size_t depth)
{
	int i;
	int ret;
	uint8_t *buf;
	struct abn_msg_queue *pmsg_queue;
	struct abn_msg_queue *pfree_queue;

	buf = kzalloc(depth * (sizeof(struct abn_msg_elem) +
		      sizeof(struct nanohub_packet)), GFP_KERNEL);
	if (buf == NULL)
		return -ENOMEM;

	ret = abn_msg_queue_create(&pmsg_queue);
	if (ret != 0) {
		pr_err("Couldn't allocate pmsg_queue %d\n", ret);
		ret = -ENOMEM;
		goto out_with_alloc;
	}

	ret = abn_msg_queue_create(&pfree_queue);
	if (ret != 0) {
		pr_err("Couldn't allocate pfree_queue %d\n", ret);
		ret = -ENOMEM;
		goto out_with_msg_queue;
	}

	pool->pool_alloc = buf;
	pool->msg_queue  = pmsg_queue;
	pool->free_queue = pfree_queue;

	for (i = 0; i < depth; i++) {
		abn_msg_queue_push(pfree_queue, (struct abn_msg_elem *)buf);
		buf += sizeof(struct abn_msg_elem) + sizeof(struct nanohub_packet);
	}

	return 0;

out_with_msg_queue:
	abn_msg_queue_destroy(pmsg_queue);
out_with_alloc:
	kfree(buf);

	return ret;
}

static void nanohub_adapter_free_msg_pool(struct nanohub_adapt_msg_pool *pool)
{
	abn_msg_queue_destroy(pool->msg_queue);
	abn_msg_queue_destroy(pool->free_queue);
	kfree(pool->pool_alloc);
}

/* Drain all message pool messages, moving buffers back to the free pool */
static unsigned int nanohub_adapter_drain_msg_pool(struct nanohub_adapt_msg_pool *pool)
{
	struct abn_msg_elem *pmsg;
	unsigned int drop_count = 0;

	while (!IS_ERR_VALUE(pmsg =
			abn_msg_queue_pop(pool->msg_queue, /* timeout = */ 0))) {
		abn_msg_queue_push(pool->free_queue, pmsg);
		drop_count++;
	}

	return drop_count;
}

static int
nanohub_adapter_alloc_channel(struct nanohub_adapt_priv *pnanohub_priv,
			      size_t local_idx, uint8_t channel,
			      bool is_kernel)
{
	struct nanohub_adapt_ch_data *pnanohub_ch;
	struct abn_StreamingChannel_Config *pstreaming_cfg;
	const struct channel_specific_cfg *pchannel_specific_cfg;
	u8 *rx_buffer;
	size_t rx_buffer_size;
	u8 priority;
	int ret;

	pstreaming_cfg = kzalloc(sizeof(*pstreaming_cfg), GFP_KERNEL);
	if (pstreaming_cfg == NULL)
		return -ENOMEM;

	ret = abn_sync_Mutex_Create(&pstreaming_cfg->lock);
	if (ret != 0) {
		pr_err("Couldn't allocate channel mutex idx [%u]\n", channel);
		goto out_with_streaming_cfg;
	}

	if (channel >= NANOHUB_USF_FIRST_CHANNEL_ID &&
		channel < NANOHUB_USF_FIRST_CHANNEL_ID + NANOHUB_USF_CHANNEL_COUNT) {
		pchannel_specific_cfg = get_specific_cfg(pnanohub_priv,
							 NANOHUB_USF_FIRST_CHANNEL_ID);
	} else {
		pchannel_specific_cfg = get_specific_cfg(pnanohub_priv, channel);
	}

	if (pchannel_specific_cfg == NULL) {
		rx_buffer_size = DEFAULT_RX_BUFF_SIZE;
		priority = DEFAULT_PRIORITY;
	} else {
		rx_buffer_size = pchannel_specific_cfg->rx_buff_size;
		priority = pchannel_specific_cfg->priority;
	}

	rx_buffer = kmalloc(sizeof(*rx_buffer) * rx_buffer_size, GFP_KERNEL);
	if (rx_buffer == NULL) {
		ret = -ENOMEM;
		goto out_with_streaming_mutex;
	}

	pnanohub_ch = kzalloc(sizeof(*pnanohub_ch), GFP_KERNEL);
	if (pnanohub_ch == NULL) {
		ret = -ENOMEM;
		goto out_with_streaming_rx_buffer;
	}

	ret = nanohub_adapter_alloc_msg_pool(&pnanohub_ch->read_pool,
					     MSG_READ_FREE_POOL_DEPTH);
	if (ret != 0)
		goto out_with_pnanohub_ch;

	ret = nanohub_adapter_alloc_msg_pool(&pnanohub_ch->write_pool,
					     MSG_WRITE_FREE_POOL_DEPTH);
	if (ret != 0)
		goto out_with_read_pool;

	pstreaming_cfg->index = channel;
	pstreaming_cfg->priority = priority;
	pstreaming_cfg->rx_queue_buffer = rx_buffer;
	pstreaming_cfg->rx_queue_buffer_size = rx_buffer_size;
	pstreaming_cfg->llc = pnanohub_priv->pinterface_cfg->llc_interface;

	pnanohub_ch->channel = channel;
	pnanohub_ch->client.functions = &g_ch_functions;
	pnanohub_ch->client.client_handle = &pnanohub_priv->pchannels[local_idx];

	pnanohub_ch->streaming_ch.config = pstreaming_cfg;
	pnanohub_ch->streaming_ch.client = &pnanohub_ch->client;
	abn_StreamingChannel_Init(&pnanohub_ch->streaming_ch);

	spin_lock_init(&pnanohub_ch->status_spinlock);
	spin_lock_init(&pnanohub_ch->read_status_spinlock);
	spin_lock_init(&pnanohub_ch->write_status_spinlock);
	spin_lock_init(&pnanohub_ch->message_listener_spinlock);

	pnanohub_ch->conn_status = NANOHUB_CHANNEL_CLOSED_STATUS;
	pnanohub_priv->pchannels[local_idx].pch_data = pnanohub_ch;

	pnanohub_ch->read_status = NANOHUB_READ_IDLE_STATUS;
	pnanohub_ch->write_status = NANOHUB_WRITE_IDLE_STATUS;

	mutex_init(&pnanohub_ch->read_mutex);

	init_completion(&pnanohub_ch->close_completion);
	init_waitqueue_head(&pnanohub_ch->poll_wait);

	if (is_kernel)
		INIT_WORK(&pnanohub_ch->message_listener_work,
			  nanohub_adapter_message_listener_work);

	return 0;

out_with_read_pool:
	nanohub_adapter_free_msg_pool(&pnanohub_ch->read_pool);
out_with_pnanohub_ch:
	kfree(pnanohub_ch);
out_with_streaming_rx_buffer:
	kfree(rx_buffer);
out_with_streaming_mutex:
	abn_sync_Mutex_Destroy(pstreaming_cfg->lock);
out_with_streaming_cfg:
	kfree(pstreaming_cfg);

	return ret;
}

/* Try to enter a reading state, grabbing a new buffer, when needed, and
 * executing the appropriate read operation.
 *
 * On errors, return to NANOHUB_READ_IDLE_STATUS or NANOHUB_READ_FULL_STATUS.
 */
static void
nanohub_adapter_try_enter_read_state(struct nanohub_adapt_ch_data *pnanohub_ch,
				     enum nanohub_ch_read_status state)
{
	bool abn_ret;
	size_t read_size;
	uint8_t *read_ptr;
	struct nanohub_packet *pnanohub_packet;

	if (state != NANOHUB_READ_READING_HEADER_STATUS &&
	    state != NANOHUB_READ_READING_PAYLOAD_STATUS) {
		WARN(1, "Invalid state: %d", state);
		return;
	}

	/* New packet. Grab a new buffer. */
	if (state == NANOHUB_READ_READING_HEADER_STATUS) {
		pnanohub_ch->pcurrent_read_msg =
			abn_msg_queue_pop(pnanohub_ch->read_pool.free_queue,
					  /* timeout = */ 0);
		if (IS_ERR(pnanohub_ch->pcurrent_read_msg)) {
			pnanohub_ch->read_status = NANOHUB_READ_FULL_STATUS;
			return;
		}
	}

	pnanohub_packet =
		(struct nanohub_packet *)pnanohub_ch->pcurrent_read_msg->buff;

	read_size = (state == NANOHUB_READ_READING_HEADER_STATUS) ?
		sizeof(pnanohub_packet->size) : pnanohub_packet->size;

	if (read_size == 0) {
		/* Attempt to recover by closing the channel and await
		 * reopening.
		 */
		WARN(1, "Read size is 0 for channel [%u]\n", pnanohub_ch->channel);
		abn_StreamingChannel_Close(&pnanohub_ch->streaming_ch);
		return;
	}

	read_ptr = (state == NANOHUB_READ_READING_HEADER_STATUS) ?
		&pnanohub_packet->size : pnanohub_packet->payload;

	abn_ret = abn_StreamingChannel_Read(&pnanohub_ch->streaming_ch,
					    read_ptr, read_size);
	if (!abn_ret) {
		abn_msg_queue_push(pnanohub_ch->read_pool.free_queue,
				   pnanohub_ch->pcurrent_read_msg);
		pnanohub_ch->read_status = NANOHUB_READ_IDLE_STATUS;
		return;
	}

	pnanohub_ch->read_status = state;
}

/* Stage one or multiple nanohub message in the write buffer.
 * Returns true if at least one message was staged in pnanohub_ch->write_buffer.
 * This function will fail and return false if there are no messages
 * to stage in the write queue.
 */
static bool
nanohub_adapter_stage_write_buffer(struct nanohub_adapt_ch_data *pnanohub_ch)
{
	struct abn_msg_elem *pmsg;
	struct nanohub_packet *pnanohub_packet;

	pnanohub_ch->write_buffer_size = 0;

	while (true) {

		pmsg = abn_msg_queue_peek(pnanohub_ch->write_pool.msg_queue);

		if (IS_ERR_VALUE(pmsg))
			break;

		pnanohub_packet = (struct nanohub_packet *)pmsg->buff;
		if (pnanohub_ch->write_buffer_size + NANOHUB_HDR_SIZE +
			pnanohub_packet->size > ABN_MAX_DATAGRAM_SIZE)
			break;

		(void)abn_msg_queue_pop(pnanohub_ch->write_pool.msg_queue,
				 /* timeout = */ 0);

		memcpy(pnanohub_ch->write_buffer + pnanohub_ch->write_buffer_size,
			   pnanohub_packet, NANOHUB_HDR_SIZE + pnanohub_packet->size);

		pnanohub_ch->write_buffer_size += (NANOHUB_HDR_SIZE + pnanohub_packet->size);

		abn_msg_queue_push(pnanohub_ch->write_pool.free_queue, pmsg);
	}

	return (pnanohub_ch->write_buffer_size > 0);
}

/* Stage a nanohub message and initiate transfer with
 * abn_StreamingChannel_Write, and update the write_status accordingly.
 *
 * Must be called with pnanohub_ch->write_status_spinlock locked.
 */
static void
nanohub_adapter_stage_and_write_message(struct nanohub_adapt_ch_data *pnanohub_ch)
{
	bool ret;

	if (!nanohub_adapter_stage_write_buffer(pnanohub_ch))
		return;

	ret = abn_StreamingChannel_Write(&pnanohub_ch->streaming_ch,
			(uint8_t *)(&pnanohub_ch->write_buffer), pnanohub_ch->write_buffer_size);
	if (ret) {
		pnanohub_ch->write_status = NANOHUB_WRITE_WRITING_STATUS;
	} else {
		/* Session is not yet connected. This write call will
		 * retry on the open connection callback.
		 */
		pnanohub_ch->write_status = NANOHUB_WRITE_RETRY_STATUS;
	}
}

static void
nanohub_adapter_handle_connection_status(void *client_handle, bool open)
{
	struct nanohub_adapt_ch_data *pnanohub_ch;
	enum nanohub_ch_conn_status conn_status;
	bool ret;

	if (client_handle == NULL) {
		WARN(1, "No client handle!");
		return;
	}

	pnanohub_ch = ((struct nanohub_adapt_ch *)client_handle)->pch_data;

	if (pnanohub_ch == NULL) {
		WARN(1, "Missing channel data!");
		return;
	}

	spin_lock(&pnanohub_ch->status_spinlock);
	conn_status = pnanohub_ch->conn_status;
	pr_info("Channel [%u] open %d state %d\n",
		pnanohub_ch->channel, open, conn_status);
	if (open) {
		if (conn_status == NANOHUB_CHANNEL_OPENING_STATUS) {
			pnanohub_ch->conn_status = NANOHUB_CHANNEL_OPENED_STATUS;

			wake_up_wrapper(&pnanohub_ch->poll_wait);
		} else {
			pr_err("Unexpected open for channel [%u] state %d\n",
			       pnanohub_ch->channel, conn_status);
		}
	} else if (conn_status == NANOHUB_CHANNEL_FINISHING_STATUS) {
		complete(&pnanohub_ch->close_completion);
	} else {
		WARN_ON(conn_status != NANOHUB_CHANNEL_OPENED_STATUS);

		/* Reopening the channel */

		pnanohub_ch->conn_status = NANOHUB_CHANNEL_OPENING_STATUS;
		abn_StreamingChannel_Open(&pnanohub_ch->streaming_ch,
					  &pnanohub_ch->client);
	}
	spin_unlock(&pnanohub_ch->status_spinlock);

	/* Nothing to handle for read or write on close callback. */
	if (!open)
		return;

	spin_lock(&pnanohub_ch->read_status_spinlock);

	if (pnanohub_ch->read_status == NANOHUB_READ_IDLE_STATUS)
		nanohub_adapter_try_enter_read_state(pnanohub_ch,
				NANOHUB_READ_READING_HEADER_STATUS);

	spin_unlock(&pnanohub_ch->read_status_spinlock);

	spin_lock(&pnanohub_ch->write_status_spinlock);

	if (pnanohub_ch->write_status == NANOHUB_WRITE_RETRY_STATUS) {
		/* There's a failed write staged in the write buffer that we
		 * want to retry.
		 */
		ret = abn_StreamingChannel_Write(&pnanohub_ch->streaming_ch,
				(uint8_t *)(&pnanohub_ch->write_buffer),
				pnanohub_ch->write_buffer_size);
		if (ret)
			pnanohub_ch->write_status = NANOHUB_WRITE_WRITING_STATUS;
	} else if (pnanohub_ch->write_status == NANOHUB_WRITE_IDLE_STATUS) {
		/* Start writing if we have queued messages.
		 *
		 * Another thread calling write or nanohub_send_message may
		 * beat us to it, so it's not guaranteed we're in
		 * NANOHUB_WRITE_IDLE_STATUS here.
		 */

		nanohub_adapter_stage_and_write_message(pnanohub_ch);
	}

	spin_unlock(&pnanohub_ch->write_status_spinlock);
}

/* Process a received datagram, stored in pcurrent_read_msg. */
static void nanohub_adapter_process_datagram(struct nanohub_adapt_ch *pnanohub_ch)
{
	struct nanohub_adapt_ch_data *pnanohub_ch_data = pnanohub_ch->pch_data;

	if (atomic_read(&pnanohub_ch->allocated) == NANOHUB_CHANNEL_ALLOC_USER) {
		if (atomic_read(&pnanohub_ch->busy) != 1) {
			abn_msg_queue_push(pnanohub_ch_data->read_pool.free_queue,
					   pnanohub_ch_data->pcurrent_read_msg);
			pr_info_ratelimited("Channel [%u] dropping packet\n",
					    pnanohub_ch_data->channel);
			return;
		}

		abn_msg_queue_push(pnanohub_ch_data->read_pool.msg_queue,
				   pnanohub_ch_data->pcurrent_read_msg);

		wake_up_wrapper(&pnanohub_ch_data->poll_wait);
	} else { /* Kernel channel */
		spin_lock(&pnanohub_ch_data->message_listener_spinlock);

		if (!pnanohub_ch_data->has_message_listener) {
			abn_msg_queue_push(pnanohub_ch_data->read_pool.free_queue,
					   pnanohub_ch_data->pcurrent_read_msg);
			spin_unlock(&pnanohub_ch_data->message_listener_spinlock);
			return;
		}

		abn_msg_queue_push(pnanohub_ch_data->read_pool.msg_queue,
				   pnanohub_ch_data->pcurrent_read_msg);
		if (!pnanohub_ch_data->message_listener_work_pending) {
			pnanohub_ch_data->message_listener_work_pending = true;
			/* The work pending bit is cleared before the work
			 * item is executed, so as long as
			 * message_listener_work_pending is cleared, then it
			 * is safe to call schedule_work.
			 */
			schedule_work(&pnanohub_ch_data->message_listener_work);
		}

		spin_unlock(&pnanohub_ch_data->message_listener_spinlock);
	}
}

static void
nanohub_adapter_handle_read_result(void *client_handle,
				   bool success, const uint8_t *buffer,
				   size_t buffer_size_bytes)
{
	struct nanohub_adapt_ch *pnanohub_ch =
		(struct nanohub_adapt_ch *)client_handle;
	struct nanohub_adapt_ch_data *pnanohub_ch_data;
	struct nanohub_packet *pnanohub_packet;

	/* No callback expected as _deinit closes the channels. */

	if (client_handle == NULL) {
		WARN(1, "No client handle!");
		return;
	}

	pnanohub_ch_data = pnanohub_ch->pch_data;

	if (pnanohub_ch == NULL) {
		WARN(1, "Missing channel data!");
		return;
	}

	pr_debug("Channel [%u] read %zu bytes result: %d",
		 pnanohub_ch_data->channel, buffer_size_bytes, success);

	spin_lock(&pnanohub_ch_data->read_status_spinlock);

	if (pnanohub_ch_data->read_status == NANOHUB_READ_IDLE_STATUS ||
	    pnanohub_ch_data->read_status == NANOHUB_READ_FULL_STATUS) {
		WARN(1, "Unexpected read callback for channel [%u], status=%d, success=%d\n",
		     pnanohub_ch_data->channel, pnanohub_ch_data->read_status, success);

		/* Attempt to recover by closing the channel and await
		 * reopening.
		 */
		abn_StreamingChannel_Close(&pnanohub_ch_data->streaming_ch);
		spin_unlock(&pnanohub_ch_data->read_status_spinlock);
		return;
	}

	pnanohub_packet =
		(struct nanohub_packet *)pnanohub_ch_data->pcurrent_read_msg->buff;

	if (!success) {
		/* The channel closed before read completed. Drop the ongoing operation
		 * and wait for re-open.
		 */
		pnanohub_ch_data->read_status = NANOHUB_READ_IDLE_STATUS;
		abn_msg_queue_push(pnanohub_ch_data->read_pool.free_queue,
				   pnanohub_ch_data->pcurrent_read_msg);

		spin_unlock(&pnanohub_ch_data->read_status_spinlock);
		return;
	}

	switch (pnanohub_ch_data->read_status) {
	case NANOHUB_READ_READING_HEADER_STATUS:
		nanohub_adapter_try_enter_read_state(pnanohub_ch_data,
				NANOHUB_READ_READING_PAYLOAD_STATUS);
		break;
	case NANOHUB_READ_READING_PAYLOAD_STATUS:
		nanohub_adapter_process_datagram(pnanohub_ch);
		nanohub_adapter_try_enter_read_state(pnanohub_ch_data,
				NANOHUB_READ_READING_HEADER_STATUS);
		break;

	default:
		WARN(1, "Channel [%u] invalid read_status: %d\n",
		      pnanohub_ch_data->channel, pnanohub_ch_data->read_status);
	}

	spin_unlock(&pnanohub_ch_data->read_status_spinlock);
}

static void nanohub_adapter_handle_write_result(void *client_handle,
		bool success, const uint8_t *data, size_t data_size_bytes)
{
	struct nanohub_adapt_ch_data *pnanohub_ch;

	if (client_handle == NULL) {
		WARN(1, "No client handle!");
		return;
	}

	pnanohub_ch = ((struct nanohub_adapt_ch *)client_handle)->pch_data;

	if (pnanohub_ch == NULL) {
		WARN(1, "Missing channel data!");
		return;
	}

	pr_debug("Channel [%u] write %zu bytes result: %d",
		 pnanohub_ch->channel, data_size_bytes, success);

	spin_lock(&pnanohub_ch->write_status_spinlock);

	if (pnanohub_ch->write_status != NANOHUB_WRITE_WRITING_STATUS) {
		WARN(1, "Unexpected write callback for channel [%u], status=%d, success=%d\n",
		     pnanohub_ch->channel, pnanohub_ch->write_status, success);

		pnanohub_ch->write_status = NANOHUB_WRITE_IDLE_STATUS;
		abn_StreamingChannel_Close(&pnanohub_ch->streaming_ch);
		spin_unlock(&pnanohub_ch->write_status_spinlock);
		return;
	}

	pnanohub_ch->write_status = NANOHUB_WRITE_IDLE_STATUS;

	if (success)
		nanohub_adapter_stage_and_write_message(pnanohub_ch);

	spin_unlock(&pnanohub_ch->write_status_spinlock);
}

static ssize_t
nanohub_adapter_read(struct file *file, char __user *buffer,
		     size_t count, loff_t *offset)
{
	ssize_t ret;
	long msg_queue_pop_timeout;
	struct nanohub_adapt_ch_data *pnanohub_ch;
	struct nanohub_packet *pnanohub_packet;
	struct abn_msg_elem *pmsg;

	if (file->private_data == NULL) {
		WARN(1, "No private_data!");
		return -EBADF;
	}

	pnanohub_ch = ((struct nanohub_adapt_ch *)file->private_data)->pch_data;

	if (pnanohub_ch == NULL) {
		WARN(1, "No channel data!");
		return -EBADF;
	}

	pr_debug("Reading channel [%u]: %zu bytes", pnanohub_ch->channel, count);

	if (count == 0 || count > NANOHUB_DATAGRAM_SIZE)
		return -EINVAL;

	mutex_lock(&pnanohub_ch->read_mutex);

	if (abn_msg_queue_is_empty(pnanohub_ch->read_pool.msg_queue) &&
	    (file->f_flags & O_NONBLOCK)) {
		mutex_unlock(&pnanohub_ch->read_mutex);
		return -EAGAIN;
	}
	msg_queue_pop_timeout = (file->f_flags & O_NONBLOCK) ? 0 :
		msecs_to_jiffies(NANOHUB_ADAPTER_READ_TIMEOUT_MS);

	pmsg = abn_msg_queue_pop(pnanohub_ch->read_pool.msg_queue,
				 msg_queue_pop_timeout);
	if (IS_ERR(pmsg)) {
		mutex_unlock(&pnanohub_ch->read_mutex);
		return PTR_ERR(pmsg);
	}

	pnanohub_packet = (struct nanohub_packet *)pmsg->buff;
	if (pnanohub_packet->size > count) {
		/* By design, each read operation returns a distinct nanohub
		 * datagram. If the user did not provide a buffer large enough
		 * to hold a full datagram, the datagram gets truncated to the
		 * buffer size and the remainder is dropped.
		 */
		pnanohub_packet->size = count;
		pr_warn("Channel [%u] reading less than packet size. Dropping data.",
			pnanohub_ch->channel);
	}

	ret = copy_to_user_wrapper(buffer, pnanohub_packet->payload,
			pnanohub_packet->size);
	if (ret < 0)
		ret = -EFAULT;
	else
		ret = pnanohub_packet->size;

	spin_lock(&pnanohub_ch->read_status_spinlock);

	abn_msg_queue_push(pnanohub_ch->read_pool.free_queue, pmsg);
	if (pnanohub_ch->read_status == NANOHUB_READ_FULL_STATUS) {
		/* Resume read ahead. */
		nanohub_adapter_try_enter_read_state(pnanohub_ch,
				NANOHUB_READ_READING_HEADER_STATUS);
	}

	spin_unlock(&pnanohub_ch->read_status_spinlock);

	mutex_unlock(&pnanohub_ch->read_mutex);

	pr_debug("Read channel [%u] ret: %zd", pnanohub_ch->channel, ret);

	return ret;
}

static ssize_t
nanohub_adapter_write(struct file *file, const char __user *buffer,
		      size_t count, loff_t *offset)
{
	ssize_t ret;
	struct nanohub_packet *pnanohub_packet;
	struct nanohub_adapt_ch_data *pnanohub_ch;
	struct abn_msg_elem *pmsg;

	if (file->private_data == NULL) {
		WARN(1, "No private_data!");
		return -EBADF;
	}

	pnanohub_ch = ((struct nanohub_adapt_ch *)file->private_data)->pch_data;

	if (pnanohub_ch == NULL) {
		WARN(1, "No channel data!");
		return -EBADF;
	}

	pr_debug("Writing channel [%u]: %zu bytes", pnanohub_ch->channel, count);

	if (count == 0 || count > NANOHUB_DATAGRAM_SIZE) {
		pr_warn("Invalid write count on channel [%u]: %zu\n",
			pnanohub_ch->channel, count);
		ret = -EINVAL;
		goto out;
	}

	/* O_NONBLOCK is not supported, so always block waiting on a write
	 * buffer.
	 */
	pmsg = abn_msg_queue_pop(pnanohub_ch->write_pool.free_queue,
				 msecs_to_jiffies(NANOHUB_ADAPTER_WRITE_TIMEOUT_MS));
	if (IS_ERR_VALUE(pmsg)) {
		ret = PTR_ERR(pmsg);
		goto out;
	}

	pnanohub_packet = (struct nanohub_packet *)pmsg->buff;
	pnanohub_packet->size = count;
	ret = copy_from_user_wrapper(&pnanohub_packet->payload, buffer, count);
	if (ret != 0) {
		pr_err("Couldn't copy_from_user: %zd", ret);
		abn_msg_queue_push(pnanohub_ch->write_pool.free_queue, pmsg);
		ret = -EFAULT;
		goto out;
	}

	abn_msg_queue_push(pnanohub_ch->write_pool.msg_queue, pmsg);

	spin_lock(&pnanohub_ch->write_status_spinlock);

	if (pnanohub_ch->write_status == NANOHUB_WRITE_IDLE_STATUS)
		nanohub_adapter_stage_and_write_message(pnanohub_ch);

	spin_unlock(&pnanohub_ch->write_status_spinlock);

out:
	pr_debug("Write channel [%u] ret: %zd", pnanohub_ch->channel, ret);
	return ret < 0 ? ret : count;
}

static unsigned int nanohub_adapter_poll(struct file *file, poll_table *wait)
{
	struct nanohub_adapt_ch_data *pnanohub_ch;
	unsigned int mask = 0;

	if (file->private_data == NULL) {
		WARN(1, "No private_data!");
		return -EBADF;
	}

	pnanohub_ch = ((struct nanohub_adapt_ch *)file->private_data)->pch_data;

	if (pnanohub_ch == NULL) {
		WARN(1, "No channel data!");
		return -EBADF;
	}

	poll_wait_wrapper(file, &pnanohub_ch->poll_wait, wait);

	/* Non blocking write is not actually supported, but nanohub_poll
	 * sets POLLOUT flag, so maintaing this for compatibility.
	 */
	mask |= POLLOUT | POLLWRNORM;

	if (!abn_msg_queue_is_empty(pnanohub_ch->read_pool.msg_queue))
		mask |= POLLIN | POLLRDNORM;

	pr_debug("Poll channel [%u] mask %u", pnanohub_ch->channel, mask);

	return mask;
}

/* Allocate channel data, if not already allocated. Ownership of
 * userspace/kernel interface of the channel is enforced based on the
 * is_kernel flag.
 */
static int
nanohub_adapter_try_alloc_channel(struct nanohub_adapt_priv *ppriv,
				 uint8_t channel, bool is_kernel)
{
	int ret;
	uint8_t local_idx = channel - ppriv->offset_first_ch;
	enum nanohub_ch_alloc_status alloc_status =
		is_kernel ? NANOHUB_CHANNEL_ALLOC_KERNEL : NANOHUB_CHANNEL_ALLOC_USER;

	if (atomic_cmpxchg(&ppriv->pchannels[local_idx].allocated,
			   NANOHUB_CHANNEL_FREE, alloc_status) == NANOHUB_CHANNEL_FREE) {
		pr_debug("Allocating streaming channel for channel [%hhu]\n", channel);

		ret = nanohub_adapter_alloc_channel(ppriv, local_idx, channel,
						    is_kernel);
		if (ret != 0)
			return ret;
	} else if (atomic_read(&ppriv->pchannels[local_idx].allocated) !=
		   alloc_status) {
		return -EPERM;
	}

	return 0;
}

/* Request opening a channel, if not already open. If the channel was not
 * allocated, allocate it. Ownership of userspace/kernel interface of the
 * channel is enforced based on the is_kernel flag.
 */
static int
nanohub_adapter_try_open(struct nanohub_adapt_priv *ppriv, uint8_t channel,
			 bool is_kernel)
{
	int ret;
	enum nanohub_ch_conn_status conn_status;
	size_t local_idx = channel - ppriv->offset_first_ch;
	struct nanohub_adapt_ch_data *pnanohub_ch;

	ret = nanohub_adapter_try_alloc_channel(ppriv, channel, is_kernel);
	if (ret < 0) {
		pr_err("Failed to allocate channel %u: %d", channel, ret);
		return ret;
	}

	pnanohub_ch = ppriv->pchannels[local_idx].pch_data;

	spin_lock(&pnanohub_ch->status_spinlock);
	conn_status = pnanohub_ch->conn_status;

	if (conn_status == NANOHUB_CHANNEL_FINISHING_STATUS) {
		WARN(1, "Should not open channel during teardown!");
		spin_unlock(&pnanohub_ch->status_spinlock);
		return -EPERM;
	}

	if (conn_status == NANOHUB_CHANNEL_CLOSED_STATUS) {
		pr_info("Opening state for channel [%d]\n", channel);
		pnanohub_ch->conn_status = NANOHUB_CHANNEL_OPENING_STATUS;
		spin_unlock(&pnanohub_ch->status_spinlock);
		abn_StreamingChannel_Open(&pnanohub_ch->streaming_ch,
				&pnanohub_ch->client);
	} else {
		spin_unlock(&pnanohub_ch->status_spinlock);
	}

	return ret;
}

static int nanohub_adapter_open(struct inode *inode, struct file *file)
{
	struct nanohub_adapt_priv *pnanohub_priv;
	uint8_t channel;
	size_t dev_minor = iminor(inode);
	size_t local_idx;
	int ret;

	pnanohub_priv = READ_ONCE(g_nanohub_adapt_ppriv);
	if (pnanohub_priv == NULL) {
		WARN(1, "Nanohub adapter not initialized!");
		return -EBADF;
	}

	if ((dev_minor < pnanohub_priv->offset_first_ch) ||
	    (dev_minor > pnanohub_priv->offset_first_ch +
	     pnanohub_priv->num_channels)) {
		pr_err("Invalid minor: %zu", dev_minor);
		return -EINVAL;
	}

	local_idx = dev_minor - pnanohub_priv->offset_first_ch;

	if (local_idx > NANOHUB_USF_FIRST_CHANNEL_ID &&
	    local_idx < (NANOHUB_USF_FIRST_CHANNEL_ID + NANOHUB_USF_CHANNEL_COUNT))
		return -EINVAL;

	if (local_idx == NANOHUB_USF_FIRST_CHANNEL_ID) {
		size_t i;
		bool channel_available = false;

		/* Channels 1-15 are accessed through a single minor number
		 * and are dynamically allocated on each open call.
		 */

		for (i = NANOHUB_USF_FIRST_CHANNEL_ID;
		     i < NANOHUB_USF_FIRST_CHANNEL_ID + NANOHUB_USF_CHANNEL_COUNT;
		     i++) {
			if (atomic_cmpxchg(&pnanohub_priv->pchannels[i].busy, 0, 1) == 0) {
				channel_available = true;
				local_idx = i;
				break;
			}
		}

		if (!channel_available) {
			pr_warn("Tried to allocate nanohub channel but all are busy!\n");
			return -EBUSY;
		}
	} else if (atomic_cmpxchg(&pnanohub_priv->pchannels[local_idx].busy, 0, 1) != 0) {
		return -EBUSY;
	}

	channel = local_idx + pnanohub_priv->offset_first_ch;
	ret = nanohub_adapter_try_open(pnanohub_priv, channel,
				       /* is_kernel = */ false);
	if (ret < 0)
		return ret;

	file->private_data = &pnanohub_priv->pchannels[local_idx];
	nonseekable_open(inode, file);
	pr_debug("Opened minor [%zu]\n", dev_minor);
	return 0;
}

static int nanohub_adapter_release(struct inode *inode, struct file *file)
{
	struct nanohub_adapt_priv *pnanohub_priv =
		READ_ONCE(g_nanohub_adapt_ppriv);
	struct nanohub_adapt_ch *pnanohub_ch =
		(struct nanohub_adapt_ch *)file->private_data;
	struct nanohub_adapt_ch_data *pnanohub_ch_data = pnanohub_ch->pch_data;
	unsigned int drop_count = 0;

	if (pnanohub_priv == NULL || pnanohub_ch == NULL) {
		WARN(1, "Nanohub adapter not initialized!");
		return -EBADF;
	}

	spin_lock(&pnanohub_ch_data->read_status_spinlock);

	atomic_set(&pnanohub_ch->busy, 0);

	drop_count = nanohub_adapter_drain_msg_pool(&pnanohub_ch_data->read_pool);
	if (pnanohub_ch_data->read_status == NANOHUB_READ_FULL_STATUS)
		nanohub_adapter_try_enter_read_state(pnanohub_ch_data,
				NANOHUB_READ_READING_HEADER_STATUS);

	spin_unlock(&pnanohub_ch_data->read_status_spinlock);

	if (drop_count > 0)
		pr_info("Channel [%u] dropped %u unread packets\n",
			pnanohub_ch_data->channel, drop_count);

	pr_debug("Closed channel [%u]\n", pnanohub_ch_data->channel);

	return 0;
}

ssize_t nanohub_send_message(int channel_id, const char *buffer, size_t length)
{
	int ret;
	size_t local_idx;
	struct nanohub_adapt_ch_data *pnanohub_ch;
	struct nanohub_adapt_priv *ppriv = READ_ONCE(g_nanohub_adapt_ppriv);
	struct nanohub_packet *pnanohub_packet;
	struct abn_msg_elem *pmsg;

	pr_debug("channel_id: %d", channel_id);

	if (ppriv == NULL) {
		pr_err("Nanohub adapter interface not initialized!");
		return -1;
	}

	if (channel_id < ppriv->offset_first_ch ||
	    channel_id >= ppriv->offset_first_ch + ppriv->num_channels) {
		pr_err("Nanohub adapter invalid channel: %d", channel_id);
		return -EINVAL;
	}

	if (buffer == NULL)
		return -EINVAL;

	if (length == 0 || length > NANOHUB_DATAGRAM_SIZE) {
		pr_warn("Invalid write length on channel [%d]: %zu\n",
			channel_id, length);
		return -EINVAL;
	}

	ret = nanohub_adapter_try_open(ppriv, channel_id,
				       /* is_kernel = */ true);
	if (ret < 0)
		return ret;

	local_idx = channel_id - ppriv->offset_first_ch;
	pnanohub_ch = ppriv->pchannels[local_idx].pch_data;

	pmsg = abn_msg_queue_pop(pnanohub_ch->write_pool.free_queue,
				 msecs_to_jiffies(NANOHUB_ADAPTER_WRITE_TIMEOUT_MS));
	if (IS_ERR_VALUE(pmsg)) {
		pr_err("abn_msg_queue_pop error: %ld\n", PTR_ERR(pmsg));
		return PTR_ERR(pmsg);
	}

	pnanohub_packet = (struct nanohub_packet *)pmsg->buff;
	pnanohub_packet->size = length;
	memcpy(pnanohub_packet->payload, buffer, length);

	abn_msg_queue_push(pnanohub_ch->write_pool.msg_queue, pmsg);

	spin_lock(&pnanohub_ch->write_status_spinlock);

	if (pnanohub_ch->write_status == NANOHUB_WRITE_IDLE_STATUS)
		nanohub_adapter_stage_and_write_message(pnanohub_ch);

	spin_unlock(&pnanohub_ch->write_status_spinlock);

	return length;
}
#if !IS_ENABLED(CONFIG_AUTOBAHN_TESTS)
EXPORT_SYMBOL_GPL(nanohub_send_message);
#endif

/* Message listener work. Calls the message listener for queued data.
 *
 * message_listener_work_pending is used to determine whether work needs to be
 * scheduled when queueing more data.
 *
 * It is safe to remove the message listener while this is executing, but
 * caller must wait for work execution to finish.
 */
static void nanohub_adapter_message_listener_work(struct work_struct *work)
{
	struct nanohub_adapt_ch_data *pnanohub_ch_data =
		container_of(work, struct nanohub_adapt_ch_data, message_listener_work);
	struct abn_msg_elem *pmsg;
	struct nanohub_packet *pnanohub_packet;

	pr_debug("channel_id: %u\n", pnanohub_ch_data->channel);

	spin_lock(&pnanohub_ch_data->message_listener_spinlock);

	while (!IS_ERR_VALUE(pmsg =
			abn_msg_queue_pop(pnanohub_ch_data->read_pool.msg_queue,
					  /* timeout = */ 0))) {
		message_listener_func_t on_message_received =
			pnanohub_ch_data->on_message_received;

		if (!pnanohub_ch_data->has_message_listener)
			break;

		spin_unlock(&pnanohub_ch_data->message_listener_spinlock);

		pnanohub_packet = (struct nanohub_packet *)pmsg->buff;
		on_message_received(pnanohub_packet->payload,
				    pnanohub_packet->size);

		spin_lock(&pnanohub_ch_data->read_status_spinlock);

		abn_msg_queue_push(pnanohub_ch_data->read_pool.free_queue, pmsg);
		if (pnanohub_ch_data->read_status == NANOHUB_READ_FULL_STATUS)
			nanohub_adapter_try_enter_read_state(pnanohub_ch_data,
					NANOHUB_READ_READING_HEADER_STATUS);

		spin_unlock(&pnanohub_ch_data->read_status_spinlock);

		spin_lock(&pnanohub_ch_data->message_listener_spinlock);
	}

	pnanohub_ch_data->message_listener_work_pending = false;

	spin_unlock(&pnanohub_ch_data->message_listener_spinlock);
}

/* Set a channel message listener. Safe to call if the message listener work is
 * currently executing.
 */
static void
nanohub_adapter_set_message_listener(struct nanohub_adapt_ch_data *pnanohub_ch,
				     message_listener_func_t on_message_received)
{
	spin_lock(&pnanohub_ch->message_listener_spinlock);

	pnanohub_ch->has_message_listener = false;

	if (pnanohub_ch->message_listener_work_pending) {
		spin_unlock(&pnanohub_ch->message_listener_spinlock);
		flush_work(&pnanohub_ch->message_listener_work);
		spin_lock(&pnanohub_ch->message_listener_spinlock);
	}

	pnanohub_ch->on_message_received = on_message_received;

	if (on_message_received != NULL) {
		pnanohub_ch->has_message_listener = true;
		if (!abn_msg_queue_is_empty(pnanohub_ch->read_pool.msg_queue))
			schedule_work(&pnanohub_ch->message_listener_work);
	}

	spin_unlock(&pnanohub_ch->message_listener_spinlock);
}

/* Register a message listener for a nanohub channel.
 * If the channel is not already open, request opening the channel.
 */
bool nanohub_register_listener(int channel_id, message_listener_func_t on_message_received)
{
	int ret;
	size_t local_idx;
	struct nanohub_adapt_ch_data *pnanohub_ch;
	struct nanohub_adapt_priv *ppriv = READ_ONCE(g_nanohub_adapt_ppriv);

	pr_info("channel_id: %d", channel_id);

	if (ppriv == NULL) {
		pr_err("Nanohub adapter interface not initialized!");
		return false;
	}

	if (channel_id < ppriv->offset_first_ch ||
	    channel_id >= ppriv->offset_first_ch + ppriv->num_channels) {
		pr_err("Nanohub adapter invalid channel: %d", channel_id);
		return false;
	}

	ret = nanohub_adapter_try_open(ppriv, channel_id,
				       /* is_kernel = */ true);
	if (ret < 0)
		return false;

	local_idx = channel_id - ppriv->offset_first_ch;
	pnanohub_ch = ppriv->pchannels[local_idx].pch_data;

	nanohub_adapter_set_message_listener(pnanohub_ch, on_message_received);

	return true;
}
#if !IS_ENABLED(CONFIG_AUTOBAHN_TESTS)
EXPORT_SYMBOL_GPL(nanohub_register_listener);
#endif

/**
 * Unregister the message listener of a nanohub channel.
 */
void nanohub_unregister_listener(int channel_id)
{
	size_t local_idx;
	struct nanohub_adapt_ch_data *pnanohub_ch;
	struct nanohub_adapt_priv *ppriv = READ_ONCE(g_nanohub_adapt_ppriv);

	pr_info("channel_id: %d", channel_id);

	if (ppriv == NULL) {
		pr_err("Nanohub adapter interface not initialized!");
		return;
	}

	if (channel_id < ppriv->offset_first_ch ||
	    channel_id >= ppriv->offset_first_ch + ppriv->num_channels) {
		pr_err("Nanohub adapter invalid channel: %d", channel_id);
		return;
	}

	local_idx = channel_id - ppriv->offset_first_ch;

	if (!atomic_read(&ppriv->pchannels[local_idx].allocated))
		return;

	pnanohub_ch = ppriv->pchannels[local_idx].pch_data;

	nanohub_adapter_set_message_listener(pnanohub_ch, NULL);

	spin_lock(&pnanohub_ch->read_status_spinlock);

	/* The listener was removed, so drop messages. */
	nanohub_adapter_drain_msg_pool(&pnanohub_ch->read_pool);

	/* Resume reading to avoid queuing on the other endpoint. */
	if (pnanohub_ch->read_status == NANOHUB_READ_FULL_STATUS)
		nanohub_adapter_try_enter_read_state(pnanohub_ch,
				NANOHUB_READ_READING_HEADER_STATUS);

	spin_unlock(&pnanohub_ch->read_status_spinlock);

}
#if !IS_ENABLED(CONFIG_AUTOBAHN_TESTS)
EXPORT_SYMBOL_GPL(nanohub_unregister_listener);
#endif

int __mockable
nanohub_adapter_wait_for_channel_closed(struct nanohub_adapt_ch_data *pch)
{
	int ret = wait_for_completion_timeout(&pch->close_completion,
			msecs_to_jiffies(NANOHUB_CHANNEL_CLOSE_TIMEOUT_MS));
	if (ret == 0)
		return -ETIME;

	return 0;
}

static inline bool check_attr_is_valid(const struct autobahn_platform_data *pplatform_data,
				       const struct dynamic_sysfs_attr *pdynamic_sysfs_attr)
{
	return *((const bool *)((const char *)pplatform_data +
				pdynamic_sysfs_attr->attr_is_valid_bool_offset));
}

static inline int nanohub_adapter_create_attributes(struct device *pdev,
			const struct autobahn_platform_data *pplatform_data)
{
	int i;
	int ret;

	/* Platform/hardware revision specifc sysfs attributes. */
	for (i = 0; i < ARRAY_SIZE(g_dynamic_sysfs_attrs); i++) {
		if (check_attr_is_valid(pplatform_data, &g_dynamic_sysfs_attrs[i])) {
			ret = device_create_file(pdev, &g_dynamic_sysfs_attrs[i].dev_attr);
			if (ret) {
				pr_err("Failed to create sysfs attr [%s] err=%d\n",
					g_dynamic_sysfs_attrs[i].dev_attr.attr.name, ret);
				goto cleanup_sysfs_attrs;
			}
		}
	}

	/* Sysfs attributes common to all platforms and hardware revisions. */
	ret = sysfs_create_group(&pdev->kobj, &g_static_sysfs_attr_group);
	if (ret != 0) {
		pr_err("Failed to create sysfs attr group\n");
		goto cleanup_sysfs_attrs;
	}

	return 0;

cleanup_sysfs_attrs:
	while (--i >= 0) {
		if (check_attr_is_valid(pplatform_data, &g_dynamic_sysfs_attrs[i]))
			device_remove_file(pdev, &g_dynamic_sysfs_attrs[i].dev_attr);
	}

	return ret;
}

static int nanohub_adapter_create_sysfs_nodes(void *pabn_data, int major)
{
	struct nanohub_adapt_priv *plocal_nanohub_priv;
	struct class *pdev_class;
	struct autobahn_data *pautobahn_data = pabn_data;
	struct device *pdev;
	int i;
	int ret;

	plocal_nanohub_priv = READ_ONCE(g_nanohub_adapt_ppriv);
	if (plocal_nanohub_priv == NULL) {
		pr_warn("Unexpected call to create sysfs nodes\n");
		return -ENOMEM;
	}

	if (plocal_nanohub_priv->pdev_class != NULL) {
		pr_err("Nanohub adapter device class already created\n");
		return -EALREADY;
	}

	pdev_class = class_create(NANOHUB_DEV_CLASS);
	if (IS_ERR(pdev_class)) {
		pr_err("Failed to create nanohub adapter dev class\n");
		return PTR_ERR(pdev_class);
	}

	/* Create device nodes except the COMMS node. */
	for (i = 0; i < ARRAY_SIZE(g_dev_nodes_cfg); i++) {
		pdev = device_create(pdev_class, NULL,
				     MKDEV(major, g_dev_nodes_cfg[i].channel),
				     NULL, g_dev_nodes_cfg[i].node_name);
		if (IS_ERR(pdev)) {
			pr_err("Failed to create sysfs device %s, err=%ld\n",
				g_dev_nodes_cfg[i].node_name, PTR_ERR(pdev));
			ret = PTR_ERR(pdev);
			goto fail_nodes;
		}
	}

	/* Create the COMMS node. */
	pdev = device_create(pdev_class, NULL,
			     MKDEV(major, NANOHUB_COMMS_CHANNEL_ID),
			     pautobahn_data, NANOHUB_COMMS_DEVICE_NAME);
	if (IS_ERR(pdev)) {
		pr_err("Failed to create comms node, err=%ld\n", PTR_ERR(pdev));
		ret = PTR_ERR(pdev);
		goto fail_nodes;
	}

	ret = nanohub_adapter_create_attributes(pdev, pautobahn_data->pdata);
	if (ret != 0)
		goto fail_attr;

	plocal_nanohub_priv->pdev_class = pdev_class;
	plocal_nanohub_priv->major = major;
	pautobahn_data->nanohub_comms_dev = pdev;
	plocal_nanohub_priv->pautobahn_data = pautobahn_data;
	return 0;

fail_attr:
	device_destroy(pdev_class, MKDEV(major, NANOHUB_COMMS_CHANNEL_ID));

fail_nodes:
	while (i-- >= 0)
		device_destroy(pdev_class,
			       MKDEV(major, g_dev_nodes_cfg[i].channel));

	class_destroy(pdev_class);

	return ret;
}

static void nanohub_adapter_destroy_sysfs_nodes(void)
{
	struct nanohub_adapt_priv *plocal_nanohub_priv;
	const struct autobahn_platform_data *pplatform_data;
	struct class *pdev_class;
	struct device *pcomms_dev;
	int major;
	int i;

	plocal_nanohub_priv = READ_ONCE(g_nanohub_adapt_ppriv);
	if (plocal_nanohub_priv == NULL) {
		pr_warn("Unexpected call to destroy nanohub nodes\n");
		return;
	}

	pdev_class = plocal_nanohub_priv->pdev_class;
	if (pdev_class == NULL) {
		pr_warn("Unexpected call to destroy nodes while class NULL\n");
		return;
	}

	if (plocal_nanohub_priv->pautobahn_data == NULL) {
		WARN(1, "Missing driver data!");
		return;
	}

	pcomms_dev = plocal_nanohub_priv->pautobahn_data->nanohub_comms_dev;
	sysfs_remove_group(&pcomms_dev->kobj, &g_static_sysfs_attr_group);

	pplatform_data = plocal_nanohub_priv->pautobahn_data->pdata;
	for (i = 0; i < ARRAY_SIZE(g_dynamic_sysfs_attrs); i++) {
		if (check_attr_is_valid(pplatform_data, &g_dynamic_sysfs_attrs[i]))
			device_remove_file(pcomms_dev, &g_dynamic_sysfs_attrs[i].dev_attr);
	}

	major = plocal_nanohub_priv->major;
	if (major == 0) {
		WARN(1, "Major is 0!");
		return;
	}

	device_destroy(pdev_class, MKDEV(major, NANOHUB_COMMS_CHANNEL_ID));

	for (i = 0; i < ARRAY_SIZE(g_dev_nodes_cfg); i++) {
		device_destroy(pdev_class,
			       MKDEV(major, g_dev_nodes_cfg[i].channel));
	}

	plocal_nanohub_priv->pautobahn_data->nanohub_comms_dev = NULL;
	plocal_nanohub_priv->pautobahn_data = NULL;

	class_destroy(pdev_class);
	plocal_nanohub_priv->pdev_class = NULL;
	plocal_nanohub_priv->major = 0;
}

static void nanohub_adapter_deinit(struct abn_client_interface *interface)
{
	struct nanohub_adapt_priv *plocal_nanohub_priv;
	int i;

	kfree(interface);

	plocal_nanohub_priv = READ_ONCE(g_nanohub_adapt_ppriv);
	if (plocal_nanohub_priv == NULL) {
		pr_warn("Unexpected call to Nanohub adapter deinit\n");
		return;
	}

	/* Close all the channels. */
	for (i = 0; i < plocal_nanohub_priv->num_channels; i++) {
		struct nanohub_adapt_ch_data *pnanohub_ch =
			plocal_nanohub_priv->pchannels[i].pch_data;
		if (pnanohub_ch == NULL)
			continue;

		spin_lock(&pnanohub_ch->status_spinlock);
		pnanohub_ch->conn_status = NANOHUB_CHANNEL_FINISHING_STATUS;
		spin_unlock(&pnanohub_ch->status_spinlock);
		reinit_completion(&pnanohub_ch->close_completion);
		abn_StreamingChannel_Close(&pnanohub_ch->streaming_ch);
	}

	WRITE_ONCE(g_nanohub_adapt_ppriv, NULL);
	/* Deallocate the data  all the channels. */
	for (i = 0; i < plocal_nanohub_priv->num_channels; i++) {
		unsigned long ret;
		struct nanohub_adapt_ch *pnanohub_ch =
			&plocal_nanohub_priv->pchannels[i];
		struct nanohub_adapt_ch_data *pnanohub_ch_data =
			plocal_nanohub_priv->pchannels[i].pch_data;

		if (pnanohub_ch_data == NULL)
			continue;

		mutex_destroy(&pnanohub_ch_data->read_mutex);

		/* Wait to be notified that channels are closed. */
		ret = nanohub_adapter_wait_for_channel_closed(pnanohub_ch_data);
		if (ret < 0)
			pr_err("abn_StreamingChannel_Close timed out!");

		/* Might be dangerous in case we timed out above, but try. */
		if (atomic_read(&pnanohub_ch->allocated) == NANOHUB_CHANNEL_ALLOC_KERNEL)
			cancel_work_sync(&pnanohub_ch_data->message_listener_work);

		abn_StreamingChannel_Deinit(&pnanohub_ch_data->streaming_ch);

		nanohub_adapter_free_msg_pool(&pnanohub_ch_data->read_pool);
		nanohub_adapter_free_msg_pool(&pnanohub_ch_data->write_pool);

		abn_sync_Mutex_Destroy(pnanohub_ch_data->streaming_ch.config->lock);
		kfree(pnanohub_ch_data->streaming_ch.config->rx_queue_buffer);
		kfree(pnanohub_ch_data->streaming_ch.config);
		kfree(pnanohub_ch_data);
	}

	kfree(plocal_nanohub_priv);
	pr_info("Finished _deinit\n");
}

int nanohub_adapter_init(struct abn_client_interface_config *config,
		struct abn_client_interface **interface)
{
	struct nanohub_adapt_priv *plocal_nanohub_priv;
	u8 num_channels = (config->channel_range[1]  -
			config->channel_range[0]) + 1;

	pr_debug("Nanohub adapter init");
	if (READ_ONCE(g_nanohub_adapt_ppriv) != NULL) {
		pr_err("Nanohub adapter already in place at init\n");
		return -EALREADY;
	}

	*interface = kmalloc(sizeof(**interface), GFP_KERNEL);
	if (*interface == NULL)
		return -ENOMEM;

	plocal_nanohub_priv = kzalloc(sizeof(*plocal_nanohub_priv) +
			sizeof(struct nanohub_adapt_ch) * num_channels,
			GFP_KERNEL);
	if (plocal_nanohub_priv == NULL) {
		kfree(*interface);
		return -ENOMEM;
	}

	(*interface)->config = config;
	(*interface)->ops = &g_nanohub_adapter_ops;
	plocal_nanohub_priv->num_channels = num_channels;
	plocal_nanohub_priv->offset_first_ch = config->channel_range[0];
	plocal_nanohub_priv->pinterface_cfg = config;

	WRITE_ONCE(g_nanohub_adapt_ppriv, plocal_nanohub_priv);

	pr_debug("Finished nanohub adapter init\n");
	return 0;
}
