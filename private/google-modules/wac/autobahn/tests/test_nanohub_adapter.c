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

#include <kunit/test.h>
#include <linux/compiler_attributes.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/poll.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/workqueue.h>

#include "abn_client_interface.h"
#include "autobahn/streaming_channel.h"
#include "autobahn/sync/mutex_backend/mutex_init.h"
#include "channel_specific_cfg.h"
#include "nanohub_adapter.h"
#include "nanohub_exports.h"

/* Constants that need to be kept in sync with implementation. */
#define NANOHUB_ADAPTER_HEADER_SIZE   1
#define NANOHUB_ADAPTER_DATAGRAM_SIZE 255
#define NANOHUB_ADAPTER_QUEUE_DEPTH   8

struct __packed nanohub_packet {
	uint8_t size;
	uint8_t payload[NANOHUB_ADAPTER_DATAGRAM_SIZE];
};

#define CLIENT_INTERFACE_FOPS(op, ...) \
	(g_abn_client_interface->ops->fops->op(__VA_ARGS__))
#define NANOHUB_ADAPTER_RANGE_START 0
#define NANOHUB_ADAPTER_RANGE_END 30
#define NANOHUB_ADAPTER_LLC ((struct abn_llc_Llc_Interface *)0xdeadbeef)

#define NANOHUB_ADAPTER_MAJOR 0
#define NANOHUB_ADAPTER_READ_TEST_CHANNEL 20

#define STREAMING_CLIENT_CHANNEL_OPS(op, ...) \
	(g_streaming_channel->client->functions->op( \
	 g_streaming_channel->client->client_handle, __VA_ARGS__))

struct nanohub_adapt_ch_data *pch;

static struct abn_client_interface *g_abn_client_interface;
static struct inode g_inode;
static struct file g_file;

static struct inode g_nanohub_client_inodes[NANOHUB_USF_CHANNEL_COUNT];
static struct file g_nanohub_client_files[NANOHUB_USF_CHANNEL_COUNT];

static const struct abn_StreamingChannel *g_streaming_channel;

/* Unit test stubs and fakes. */

unsigned long copy_to_user_wrapper(void *to, const void *from, unsigned long n)
{
	memcpy(to, from, n);
	return 0;
}

unsigned long copy_from_user_wrapper(void *to, const void *from, unsigned long n)
{
	memcpy(to, from, n);
	return 0;
}

int abn_sync_Mutex_Create(abn_sync_Mutex **mutex) { return 0; };
void abn_sync_Mutex_Destroy(abn_sync_Mutex *mutex) {  }

void abn_StreamingChannel_Init(struct abn_StreamingChannel *channel)
{
	/* Save pointer to streaming channel callback functions so that we may
	 * call them later.
	 */
	g_streaming_channel = channel;
}

void abn_StreamingChannel_Deinit(struct abn_StreamingChannel *channel)
{}

void abn_StreamingChannel_Open(struct abn_StreamingChannel *channel,
		const struct abn_StreamingChannel_Client *client)
{};

int nanohub_adapter_wait_for_channel_closed(struct nanohub_adapt_ch_data *pch)
{
	return 0;
}

ssize_t autobahn_download_firmware_request(struct device *dev,
					   struct device_attribute *attr,
					   const char *buf, size_t count)
{
	return 0;
}

ssize_t autobahn_firmware_name_query(struct device *dev,
				     struct device_attribute *attr,
				     char *buf)
{
	return 0;
}

ssize_t autobahn_pin_boot0_get(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	return 0;
}

ssize_t autobahn_pin_boot0_set(struct device *dev,
			       struct device_attribute *attr,
			       const char *buf, size_t count)
{
	return 0;
}

ssize_t autobahn_pin_boot2_get(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	return 0;
}

ssize_t autobahn_pin_blstatus_get(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	return 0;
}

ssize_t autobahn_pin_boot2_set(struct device *dev,
			       struct device_attribute *attr,
			       const char *buf, size_t count)
{
	return 0;
}

ssize_t autobahn_pin_nreset_get(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	return 0;
}

ssize_t autobahn_pin_nreset_set(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	return 0;
}

ssize_t autobahn_try_hw_reset(struct device *dev,
			      struct device_attribute *attr,
			      const char *buf, size_t count)
{
	return 0;
}

ssize_t time_sync_show(struct device *dev, struct device_attribute *attr,
		       char *buf)
{
	return 0;
}

/* Unit test mocks. */

static struct abn_StreamingChannel_Close_Mock {
	bool called;
	struct abn_StreamingChannel *channel;
} g_streaming_channel_close_mock;

void abn_StreamingChannel_Close(struct abn_StreamingChannel *channel)
{
	g_streaming_channel_close_mock.called = true;
	g_streaming_channel_close_mock.channel = channel;
}

static struct abn_StreamingChannel_Read_Mock {
	bool ret;    /* Set to alter return value. */
	bool called; /* Set when abn_StreamingChannel_Read is called.
		      * Must be manually reset.
		      */

	/* Following variables are set on abn_StreamingChannel_Read call. */
	struct abn_StreamingChannel *channel;
	uint8_t *buffer;
	size_t size;
} g_streaming_channel_read_mock;

bool abn_StreamingChannel_Read(struct abn_StreamingChannel *channel,
		uint8_t *buffer,
		size_t size)
{
	g_streaming_channel_read_mock.called = true;
	g_streaming_channel_read_mock.channel = channel;
	g_streaming_channel_read_mock.buffer = buffer;
	g_streaming_channel_read_mock.size = size;
	return g_streaming_channel_read_mock.ret;
};

/* Issue a connection status callback with a mocked streaming channel read
 * result.
 */
void mock_streaming_channel_read_connection_status(bool conn_status,
		bool read_ret)
{
	memset(&g_streaming_channel_read_mock, 0,
	       sizeof(g_streaming_channel_read_mock));
	g_streaming_channel_read_mock.ret = read_ret;

	STREAMING_CLIENT_CHANNEL_OPS(handle_connection_status, conn_status);
}

/* Issue read status callbacks and mock receiving a datagram. Expects that
 * read ahead is ongoing when calling this function.
 */
void mock_streaming_channel_read_packet(void *payload, size_t size)
{
	/* Datagram header read result. */
	*((uint8_t *)g_streaming_channel_read_mock.buffer) = size;

	g_streaming_channel_read_mock.ret = true;

	STREAMING_CLIENT_CHANNEL_OPS(handle_read_result, true,
				     g_streaming_channel_read_mock.buffer,
				     g_streaming_channel_read_mock.size);

	/* Payload read result. */
	memcpy(((uint8_t *)g_streaming_channel_read_mock.buffer), payload,
	       g_streaming_channel_read_mock.size);

	/* Reset called flag in case caller wants to inspect last result. */
	g_streaming_channel_read_mock.called = false;

	STREAMING_CLIENT_CHANNEL_OPS(handle_read_result, true,
				     g_streaming_channel_read_mock.buffer,
				     g_streaming_channel_read_mock.size);
}

static struct abn_StreamingChannel_Write_Mock {
	bool ret;    /* Set to alter return value. */
	bool called; /* Set when abn_StreamingChannel_Read is called.
		      * Must be manually reset.
		      */

	/* Following variables are set on abn_StreamingChannel_Read call. */
	struct abn_StreamingChannel *channel;
	const uint8_t *data;
	size_t size;
} g_streaming_channel_write_mock;

bool abn_StreamingChannel_Write(struct abn_StreamingChannel *channel,
		const uint8_t *data,
		size_t data_size_bytes)
{
	g_streaming_channel_write_mock.called = true;
	g_streaming_channel_write_mock.channel = channel;
	g_streaming_channel_write_mock.data = data;
	g_streaming_channel_write_mock.size = data_size_bytes;
	return g_streaming_channel_write_mock.ret;
};

static struct poll_wait_mock {
	struct file *filp;
	wait_queue_head_t *wait_address;
	poll_table *table;
} g_poll_wait_mock;

void poll_wait_wrapper(struct file *filp, wait_queue_head_t *wait_address,
		       poll_table *table)
{
	g_poll_wait_mock.filp = filp;
	g_poll_wait_mock.wait_address = wait_address;
	g_poll_wait_mock.table = table;
}

static struct wake_up_mock {
	wait_queue_head_t *wq_head;
} g_wake_up_mock;

void wake_up_wrapper(struct wait_queue_head *wq_head)
{
	g_wake_up_mock.wq_head = wq_head;
}

static struct nanohub_message_listener_mock {
	unsigned int call_count;
	const char *buffer;
	size_t length;
} g_nanohub_message_listener_mock;

static void nanohub_message_listener(const char *buffer, size_t length)
{
	g_nanohub_message_listener_mock.call_count += 1;
	g_nanohub_message_listener_mock.buffer = buffer;
	g_nanohub_message_listener_mock.length = length;
}

static struct mock_queue_work_on {
	bool ret;
	bool called;
	struct work_struct *work;
	/* Ignore other parameters */
} g_mock_queue_work_on;

/* Used to mock call to schedule_work, which is defined in workqueue.h and
 * calls queue_work_on.
 */
bool mock_queue_work_on(int cpu, struct workqueue_struct *wq,
			struct work_struct *work)
{
	g_mock_queue_work_on.called = true;
	g_mock_queue_work_on.work = work;
	return g_mock_queue_work_on.ret;
}

/* Nanohub adapter nanohub client test suite. */

static void open_invalid_nanohub_client(struct kunit *test)
{
	size_t i;
	int ret;
	uint8_t test_channels[] = {
		NANOHUB_USF_FIRST_CHANNEL_ID + 1,
		NANOHUB_USF_FIRST_CHANNEL_ID + (NANOHUB_USF_CHANNEL_COUNT / 2),
		NANOHUB_USF_FIRST_CHANNEL_ID + NANOHUB_USF_CHANNEL_COUNT - 1
	};

	memset(&g_inode, 0, sizeof(g_inode));
	memset(&g_file, 0, sizeof(g_file));
	g_file.f_inode = &g_inode;

	for (i = 0; i < sizeof(test_channels); i++) {
		/* Try to open a channel from within the nanohub client
		 * range.
		 */
		g_inode.i_rdev = MKDEV(NANOHUB_ADAPTER_MAJOR, test_channels[i]);
		ret = CLIENT_INTERFACE_FOPS(open, &g_inode, &g_file);
		KUNIT_EXPECT_EQ(test, ret, -EINVAL);
	}
}

static void open_all_nanohub_clients(struct kunit *test)
{
	size_t i;
	int ret;

	memset(&g_nanohub_client_inodes, 0, sizeof(g_nanohub_client_inodes));
	memset(&g_nanohub_client_files, 0, sizeof(g_nanohub_client_files));

	for (i = 0; i < NANOHUB_USF_CHANNEL_COUNT; i++) {
		g_nanohub_client_files[i].f_inode = &g_nanohub_client_inodes[i];
		g_nanohub_client_inodes[i].i_rdev =
			MKDEV(NANOHUB_ADAPTER_MAJOR, NANOHUB_USF_FIRST_CHANNEL_ID);
		ret = CLIENT_INTERFACE_FOPS(open, &g_nanohub_client_inodes[i],
					    &g_nanohub_client_files[i]);
		KUNIT_EXPECT_EQ(test, ret, 0);
		KUNIT_EXPECT_EQ(test, g_streaming_channel->config->index,
				NANOHUB_USF_FIRST_CHANNEL_ID + i);
	}

	/* Try to open one more channel. */

	g_file.f_inode = &g_inode;
	g_inode.i_rdev = MKDEV(NANOHUB_ADAPTER_MAJOR, NANOHUB_USF_FIRST_CHANNEL_ID);
	ret = CLIENT_INTERFACE_FOPS(open, &g_inode, &g_file);
	KUNIT_EXPECT_EQ(test, ret, -EBUSY);

	/* Cleanup. */

	for (i = 0; i < NANOHUB_USF_CHANNEL_COUNT; i++) {
		ret = CLIENT_INTERFACE_FOPS(release, &g_nanohub_client_inodes[i],
					    &g_nanohub_client_files[i]);
		KUNIT_EXPECT_EQ(test, ret, 0);
	}
}

static void reuses_nanohub_client(struct kunit *test)
{
	size_t i;
	int ret;

	memset(&g_nanohub_client_inodes, 0, sizeof(g_nanohub_client_inodes));
	memset(&g_nanohub_client_files, 0, sizeof(g_nanohub_client_files));

	for (i = 0; i < NANOHUB_USF_CHANNEL_COUNT; i++) {
		g_nanohub_client_files[i].f_inode = &g_nanohub_client_inodes[i];
		g_nanohub_client_inodes[i].i_rdev =
			MKDEV(NANOHUB_ADAPTER_MAJOR, NANOHUB_USF_FIRST_CHANNEL_ID);
		CLIENT_INTERFACE_FOPS(open, &g_nanohub_client_inodes[i],
				      &g_nanohub_client_files[i]);
	}

	/* Release one channel. */

	i = NANOHUB_USF_FIRST_CHANNEL_ID + (NANOHUB_USF_CHANNEL_COUNT / 2);
	ret = CLIENT_INTERFACE_FOPS(release, &g_nanohub_client_inodes[i],
				    &g_nanohub_client_files[i]);
	KUNIT_EXPECT_EQ(test, ret, 0);

	/* Try to open one more channel. */

	g_nanohub_client_inodes[i].i_rdev =
		MKDEV(NANOHUB_ADAPTER_MAJOR, NANOHUB_USF_FIRST_CHANNEL_ID);
	ret = CLIENT_INTERFACE_FOPS(open, &g_nanohub_client_inodes[i],
				    &g_nanohub_client_files[i]);
	KUNIT_EXPECT_EQ(test, ret, 0);

	/* Cleanup. */

	for (i = 0; i < NANOHUB_USF_CHANNEL_COUNT; i++) {
		ret = CLIENT_INTERFACE_FOPS(release, &g_nanohub_client_inodes[i],
					    &g_nanohub_client_files[i]);
		KUNIT_EXPECT_EQ(test, ret, 0);
	}
}

static struct kunit_case nanohub_adapter_nanohub_client_test_cases[] = {
	KUNIT_CASE(open_invalid_nanohub_client),
	KUNIT_CASE(open_all_nanohub_clients),
	KUNIT_CASE(reuses_nanohub_client),
	{}
};

/* Nanohub adapter read tests suite. */

static void read_with_invalid_size(struct kunit *test)
{
	int ret;
	char buf[1];

	ret = CLIENT_INTERFACE_FOPS(read, &g_file, buf, 0, 0);
	KUNIT_EXPECT_EQ(test, ret, -EINVAL);

	ret = CLIENT_INTERFACE_FOPS(read, &g_file, buf,
			NANOHUB_ADAPTER_DATAGRAM_SIZE + 1, 0);
	KUNIT_EXPECT_EQ(test, ret, -EINVAL);
}

static void starts_read_ahead_on_open(struct kunit *test)
{
	mock_streaming_channel_read_connection_status(true, true);

	KUNIT_EXPECT_PTR_NE(test, g_streaming_channel_read_mock.buffer, NULL);
	KUNIT_EXPECT_EQ(test, g_streaming_channel_read_mock.size,
			NANOHUB_ADAPTER_HEADER_SIZE);
}

static void starts_read_ahead_on_open_after_close(struct kunit *test)
{
	/* Open, then close the channel. */
	mock_streaming_channel_read_connection_status(true, true);
	STREAMING_CLIENT_CHANNEL_OPS(handle_connection_status, false);

	STREAMING_CLIENT_CHANNEL_OPS(handle_read_result, false,
				     g_streaming_channel_read_mock.buffer,
				     g_streaming_channel_read_mock.size);

	/* Re-open the channel. */
	mock_streaming_channel_read_connection_status(true, true);

	/* Expect read ahead to start. */
	KUNIT_EXPECT_PTR_NE(test, g_streaming_channel_read_mock.buffer, NULL);
	KUNIT_EXPECT_EQ(test, g_streaming_channel_read_mock.size,
			NANOHUB_ADAPTER_HEADER_SIZE);
}

static void starts_payload_read_on_read_callback(struct kunit *test)
{
	uint8_t test_packet_size = 5;

	/* Open channel to start read ahead. */
	mock_streaming_channel_read_connection_status(true, true);

	/* Inject the datagram size. */
	*((uint8_t *)g_streaming_channel_read_mock.buffer) = test_packet_size;

	/* Notify read callback. */
	memset(&g_streaming_channel_read_mock, 0,
	       sizeof(g_streaming_channel_read_mock));
	g_streaming_channel_read_mock.ret = true;

	STREAMING_CLIENT_CHANNEL_OPS(handle_read_result, true,
				     g_streaming_channel_read_mock.buffer,
				     g_streaming_channel_read_mock.size);

	KUNIT_EXPECT_PTR_NE(test, g_streaming_channel_read_mock.buffer, NULL);
	KUNIT_EXPECT_EQ(test, g_streaming_channel_read_mock.size,
			test_packet_size);
}

static void stops_reading_when_queue_is_full(struct kunit *test)
{
	int i;
	char test_packet[] = "test data";

	/* Open channel to start read ahead. */
	mock_streaming_channel_read_connection_status(true, true);

	/* Fill the read queue. */
	for (i = 0; i < NANOHUB_ADAPTER_QUEUE_DEPTH - 1; i++)
		mock_streaming_channel_read_packet(test_packet,
						   sizeof(test_packet));

	/* One more slot should be left in the queue, so a header read should be in
	 * progress.
	 */

	KUNIT_EXPECT_PTR_NE(test, g_streaming_channel_read_mock.buffer, NULL);
	KUNIT_EXPECT_EQ(test, g_streaming_channel_read_mock.size,
			NANOHUB_ADAPTER_HEADER_SIZE);

	/* Receive one more datagram and check that read ahead is paused. */
	mock_streaming_channel_read_packet(test_packet, sizeof(test_packet));

	KUNIT_EXPECT_FALSE(test, g_streaming_channel_read_mock.called);
}

static void read_with_nonblock_and_no_data(struct kunit *test)
{
	int ret;
	char buffer[NANOHUB_ADAPTER_DATAGRAM_SIZE];

	g_file.f_flags |= O_NONBLOCK;

	ret = CLIENT_INTERFACE_FOPS(read, &g_file, buffer, sizeof(buffer), 0);
	KUNIT_EXPECT_EQ(test, ret, -EAGAIN);

	/* Retest after a connected callback. */
	mock_streaming_channel_read_connection_status(true, true);

	ret = CLIENT_INTERFACE_FOPS(read, &g_file, buffer, sizeof(buffer), 0);
	KUNIT_EXPECT_EQ(test, ret, -EAGAIN);
}

static void read_with_queued_datagram(struct kunit *test)
{
	int ret;
	char read_buf[NANOHUB_ADAPTER_DATAGRAM_SIZE];
	char test_payload[] = "test data";

	mock_streaming_channel_read_connection_status(true, true);

	mock_streaming_channel_read_packet(test_payload, sizeof(test_payload));

	ret = CLIENT_INTERFACE_FOPS(read, &g_file, read_buf, sizeof(read_buf), 0);
	KUNIT_EXPECT_EQ(test, ret, sizeof(test_payload));
	KUNIT_EXPECT_STREQ(test, read_buf, test_payload);
}

static void read_with_nonblock_with_queued_datagram(struct kunit *test)
{
	int ret;
	char read_buf[NANOHUB_ADAPTER_DATAGRAM_SIZE];
	char test_payload[] = "test data";

	g_file.f_flags |= O_NONBLOCK;

	mock_streaming_channel_read_connection_status(true, true);

	mock_streaming_channel_read_packet(test_payload, sizeof(test_payload));

	ret = CLIENT_INTERFACE_FOPS(read, &g_file, read_buf, sizeof(read_buf), 0);
	KUNIT_EXPECT_EQ(test, ret, sizeof(test_payload));
	KUNIT_EXPECT_STREQ(test, read_buf, test_payload);
}

static void read_queued_datagrams_in_order(struct kunit *test)
{
	int i;
	int ret;
	char read_buf[NANOHUB_ADAPTER_DATAGRAM_SIZE];
	char test_payload[][NANOHUB_ADAPTER_DATAGRAM_SIZE] = {
		"first test payload",
		"second test payload",
		"third test payload",
		"last test payload",
	};

	mock_streaming_channel_read_connection_status(true, true);

	for (i = 0; i < ARRAY_SIZE(test_payload); i++)
		mock_streaming_channel_read_packet(test_payload[i],
				strlen(test_payload[i]) + 1);

	for (i = 0; i < ARRAY_SIZE(test_payload); i++) {
		ret = CLIENT_INTERFACE_FOPS(read, &g_file, read_buf,
					    sizeof(read_buf), 0);
		KUNIT_EXPECT_EQ(test, ret, strlen(test_payload[i]) + 1);
		KUNIT_EXPECT_STREQ(test, read_buf, test_payload[i]);
	}
}

static void read_when_queue_is_full_unblocks_readahead(struct kunit *test)
{
	int i;
	int ret;
	char read_buf[NANOHUB_ADAPTER_DATAGRAM_SIZE];
	char fill_payload[] = "fill payload";
	char test_payload[] = "look for this message";

	mock_streaming_channel_read_connection_status(true, true);

	/* Fill the queue. */
	for (i = 0; i < NANOHUB_ADAPTER_QUEUE_DEPTH; i++)
		mock_streaming_channel_read_packet(fill_payload,
						   strlen(fill_payload) + 1);

	/* Read one message to unblock read ahead. */
	ret = CLIENT_INTERFACE_FOPS(read, &g_file, read_buf, sizeof(read_buf), 0);
	KUNIT_EXPECT_GT(test, ret, 0);

	/* Receive one more different message. */
	mock_streaming_channel_read_packet(test_payload, strlen(test_payload) + 1);

	/* Drain the queue. */
	for (i = 0; i < NANOHUB_ADAPTER_QUEUE_DEPTH; i++) {
		ret = CLIENT_INTERFACE_FOPS(read, &g_file, read_buf,
					    sizeof(read_buf), 0);
		KUNIT_EXPECT_GT(test, ret, 0);
	}

	/* Check the final message. */
	KUNIT_EXPECT_STREQ(test, read_buf, test_payload);
}

static void release_fd_when_queue_is_full_unblocks_readahead(struct kunit *test)
{
	int i;
	int ret;
	char read_buf[NANOHUB_ADAPTER_DATAGRAM_SIZE];
	char fill_payload[] = "fill payload";
	char test_payload[] = "look for this message";

	g_file.f_flags |= O_NONBLOCK;

	mock_streaming_channel_read_connection_status(true, true);

	/* Fill the queue. */
	for (i = 0; i < NANOHUB_ADAPTER_QUEUE_DEPTH; i++)
		mock_streaming_channel_read_packet(fill_payload,
						   strlen(fill_payload) + 1);

	/* Release the fd to unblock read ahead. */
	CLIENT_INTERFACE_FOPS(release, &g_inode, &g_file);

	/* Re-open the fd. */
	ret = CLIENT_INTERFACE_FOPS(open, &g_inode, &g_file);
	KUNIT_EXPECT_EQ(test, ret, 0);

	/* Receive one more different message. */
	mock_streaming_channel_read_packet(test_payload, strlen(test_payload) + 1);

	/* Read the message. */
	ret = CLIENT_INTERFACE_FOPS(read, &g_file, read_buf,
				    sizeof(read_buf), 0);
	KUNIT_EXPECT_GT(test, ret, 0);

	/* Queue was drained on release, so it should now be empty. */
	ret = CLIENT_INTERFACE_FOPS(read, &g_file, read_buf,
				    sizeof(read_buf), 0);
	KUNIT_EXPECT_EQ(test, ret, -EAGAIN);

	/* Check the final message. */
	KUNIT_EXPECT_STREQ(test, read_buf, test_payload);
}

static void read_with_queued_data_while_channel_closed(struct kunit *test)
{
	int ret;
	char read_buf[NANOHUB_ADAPTER_DATAGRAM_SIZE];
	char test_payload[] = "test payload";

	mock_streaming_channel_read_connection_status(true, true);

	mock_streaming_channel_read_packet(test_payload,
					   strlen(test_payload) + 1);

	mock_streaming_channel_read_connection_status(false, false);

	ret = CLIENT_INTERFACE_FOPS(read, &g_file, read_buf,
				    sizeof(read_buf), 0);
	KUNIT_EXPECT_EQ(test, ret, strlen(test_payload) + 1);
	KUNIT_EXPECT_STREQ(test, read_buf, test_payload);
}

static void channel_closed_during_connection_status_callback(struct kunit *test)
{
	int i;
	int ret;
	char read_buf[NANOHUB_ADAPTER_DATAGRAM_SIZE];
	char test_payload[] = "test payload";

	/* Invoke channel open callback and return error in
	 * abn_StreamingChannel_Read.
	 */
	mock_streaming_channel_read_connection_status(true, false);

	/* Channel closed unexpectedly, so Autobahn should invoke the channel
	 * closed callback.
	 */
	mock_streaming_channel_read_connection_status(false, false);

	/* Re-open the channel and succeed calling abn_StreamingChannel_Read. */
	mock_streaming_channel_read_connection_status(true, true);


	/* Should now be able to receive and read a packet. */
	mock_streaming_channel_read_packet(test_payload,
					   strlen(test_payload) + 1);

	ret = CLIENT_INTERFACE_FOPS(read, &g_file, read_buf, sizeof(read_buf), 0);
	KUNIT_EXPECT_EQ(test, ret, strlen(test_payload) + 1);
	KUNIT_EXPECT_STREQ(test, read_buf, test_payload);

	/* Test that no buffer is leaked. */
	for (i = 0; i < NANOHUB_ADAPTER_QUEUE_DEPTH; i++)
		mock_streaming_channel_read_packet(test_payload,
						   strlen(test_payload) + 1);

	for (i = 0; i < NANOHUB_ADAPTER_QUEUE_DEPTH; i++) {
		ret = CLIENT_INTERFACE_FOPS(read, &g_file, read_buf,
					    sizeof(read_buf), 0);
		KUNIT_EXPECT_GT(test, ret, 0);
	}
}

static void channel_closed_during_header_read(struct kunit *test)
{
	int i;
	int ret;
	char read_buf[NANOHUB_ADAPTER_DATAGRAM_SIZE];
	char test_payload[] = "test payload";

	mock_streaming_channel_read_connection_status(true, true);

	/* Receive header. */
	*((uint8_t *)g_streaming_channel_read_mock.buffer) =
		strlen(test_payload) + 1;

	STREAMING_CLIENT_CHANNEL_OPS(handle_read_result, true,
				     g_streaming_channel_read_mock.buffer,
				     g_streaming_channel_read_mock.size);

	/* Payload read result. Force error in abn_StreamingChannel_Read while
	 * reading header.
	 */
	memcpy(((uint8_t *)g_streaming_channel_read_mock.buffer), test_payload,
	       g_streaming_channel_read_mock.size);

	memset(&g_streaming_channel_read_mock, 0,
	       sizeof(g_streaming_channel_read_mock));
	g_streaming_channel_read_mock.ret = false;

	STREAMING_CLIENT_CHANNEL_OPS(handle_read_result, true,
				     g_streaming_channel_read_mock.buffer,
				     g_streaming_channel_read_mock.size);

	/* Status update for closed channel */
	mock_streaming_channel_read_connection_status(false, false);

	/* Re-open channel and receive a new packet. */
	mock_streaming_channel_read_connection_status(true, true);
	mock_streaming_channel_read_packet(test_payload,
					   strlen(test_payload) + 1);

	/* Expect to have initial packet and second packet after re-open in queue. */
	for (i = 0; i < 2; i++) {
		ret = CLIENT_INTERFACE_FOPS(read, &g_file, read_buf,
					    sizeof(read_buf), 0);
		KUNIT_EXPECT_EQ(test, ret, strlen(test_payload) + 1);
		KUNIT_EXPECT_STREQ(test, read_buf, test_payload);
	}

	/* Test that no buffer is leaked. */
	for (i = 0; i < NANOHUB_ADAPTER_QUEUE_DEPTH; i++)
		mock_streaming_channel_read_packet(test_payload,
						   strlen(test_payload) + 1);

	for (i = 0; i < NANOHUB_ADAPTER_QUEUE_DEPTH; i++) {
		ret = CLIENT_INTERFACE_FOPS(read, &g_file, read_buf,
					    sizeof(read_buf), 0);
		KUNIT_EXPECT_GT(test, ret, 0);
	}
}

static void channel_closed_during_payload_read(struct kunit *test)
{
	int i;
	int ret;
	char read_buf[NANOHUB_ADAPTER_DATAGRAM_SIZE];
	char test_payload[] = "test payload";

	mock_streaming_channel_read_connection_status(true, true);

	/* Return error in abn_StreamingChannel_Read while reading payload. */
	memset(&g_streaming_channel_read_mock, 0,
			sizeof(g_streaming_channel_read_mock));
	g_streaming_channel_read_mock.ret = false;

	STREAMING_CLIENT_CHANNEL_OPS(handle_read_result, true,
				     g_streaming_channel_read_mock.buffer,
				     g_streaming_channel_read_mock.size);

	mock_streaming_channel_read_connection_status(false, false);

	/* Re-open channel. */
	mock_streaming_channel_read_connection_status(true, true);

	/* Should be able to receive and read packet. */
	mock_streaming_channel_read_packet(test_payload,
					   strlen(test_payload) + 1);

	ret = CLIENT_INTERFACE_FOPS(read, &g_file, read_buf,
				    sizeof(read_buf), 0);
	KUNIT_EXPECT_EQ(test, ret, strlen(test_payload) + 1);
	KUNIT_EXPECT_STREQ(test, read_buf, test_payload);

	/* Test that no buffer is leaked. */
	for (i = 0; i < NANOHUB_ADAPTER_QUEUE_DEPTH; i++)
		mock_streaming_channel_read_packet(test_payload,
						   strlen(test_payload) + 1);

	for (i = 0; i < NANOHUB_ADAPTER_QUEUE_DEPTH; i++) {
		ret = CLIENT_INTERFACE_FOPS(read, &g_file, read_buf,
					    sizeof(read_buf), 0);
		KUNIT_EXPECT_GT(test, ret, 0);
	}
}

static void channel_closed_while_queue_is_full(struct kunit *test)
{
	int i;
	int ret;
	char read_buf[NANOHUB_ADAPTER_DATAGRAM_SIZE];
	char test_payload[] = "test payload";

	mock_streaming_channel_read_connection_status(true, true);

	/* Fill read queue. */
	for (i = 0; i < NANOHUB_ADAPTER_QUEUE_DEPTH; i++)
		mock_streaming_channel_read_packet(test_payload,
						   strlen(test_payload) + 1);

	/* Close the channel. */
	mock_streaming_channel_read_connection_status(false, false);

	g_streaming_channel_read_mock.called = false;
	/* If the channel is closed, abn_StreamingChannel_Read will return error. */
	g_streaming_channel_read_mock.ret = false;

	/* Read one packet. */
	ret = CLIENT_INTERFACE_FOPS(read, &g_file, read_buf,
				    sizeof(read_buf), 0);
	KUNIT_EXPECT_GT(test, ret, 0);

	/* Read ahead tries to start. */
	KUNIT_EXPECT_TRUE(test, g_streaming_channel_read_mock.called);

	/* Re-open the channel. */
	mock_streaming_channel_read_connection_status(true, true);

	/* Receive one more packet. */
	mock_streaming_channel_read_packet(test_payload,
					   strlen(test_payload) + 1);

	/* Queue should now be full again. */
	for (i = 0; i < NANOHUB_ADAPTER_QUEUE_DEPTH; i++) {
		ret = CLIENT_INTERFACE_FOPS(read, &g_file, read_buf,
					    sizeof(read_buf), 0);
		KUNIT_EXPECT_GT(test, ret, 0);
	}
}

static void channel_reopen_while_queue_is_full(struct kunit *test)
{
	int i;
	int ret;
	char read_buf[NANOHUB_ADAPTER_DATAGRAM_SIZE];
	char test_payload[] = "test payload";

	mock_streaming_channel_read_connection_status(true, true);

	/* Fill read queue. */
	for (i = 0; i < NANOHUB_ADAPTER_QUEUE_DEPTH; i++)
		mock_streaming_channel_read_packet(test_payload,
						   strlen(test_payload) + 1);

	/* Close the channel. */
	mock_streaming_channel_read_connection_status(false, false);

	/* Re-open the channel. */
	g_streaming_channel_read_mock.called = false;
	mock_streaming_channel_read_connection_status(true, true);

	/* Queue is still full, read should not be called. */
	KUNIT_EXPECT_FALSE(test, g_streaming_channel_read_mock.called);

	/* Read one packet. Read-ahead restarts. */
	g_streaming_channel_read_mock.called = false;
	ret = CLIENT_INTERFACE_FOPS(read, &g_file, read_buf,
				    sizeof(read_buf), 0);

	KUNIT_EXPECT_TRUE(test, g_streaming_channel_read_mock.called);

	/* Receive one more packet. */
	mock_streaming_channel_read_packet(test_payload,
					   strlen(test_payload) + 1);

	/* Queue should now be full again. */
	for (i = 0; i < NANOHUB_ADAPTER_QUEUE_DEPTH; i++) {
		ret = CLIENT_INTERFACE_FOPS(read, &g_file, read_buf,
					    sizeof(read_buf), 0);
		KUNIT_EXPECT_GT(test, ret, 0);
	}
}

static void read_result_error_while_reading_header(struct kunit *test)
{
	int i;
	int ret;
	char read_buf[NANOHUB_ADAPTER_DATAGRAM_SIZE];
	char test_payload[] = "test payload";

	mock_streaming_channel_read_connection_status(true, true);

	STREAMING_CLIENT_CHANNEL_OPS(handle_read_result, false,
				     g_streaming_channel_read_mock.buffer,
				     g_streaming_channel_read_mock.size);

	mock_streaming_channel_read_connection_status(false, false);

	mock_streaming_channel_read_connection_status(true, true);

	/* Check that we can read and no buffers were leaked. */
	/* Test that no buffer is leaked. */
	for (i = 0; i < NANOHUB_ADAPTER_QUEUE_DEPTH; i++)
		mock_streaming_channel_read_packet(test_payload,
						   strlen(test_payload) + 1);

	for (i = 0; i < NANOHUB_ADAPTER_QUEUE_DEPTH; i++) {
		ret = CLIENT_INTERFACE_FOPS(read, &g_file, read_buf,
					    sizeof(read_buf), 0);
		KUNIT_EXPECT_EQ(test, ret, strlen(test_payload) + 1);
		KUNIT_EXPECT_STREQ(test, read_buf, test_payload);
	}
}

static void read_result_error_while_reading_payload(struct kunit *test)
{
	int i;
	int ret;
	char read_buf[NANOHUB_ADAPTER_DATAGRAM_SIZE];
	char test_payload[] = "test payload";

	mock_streaming_channel_read_connection_status(true, true);

	/* Receive header. */
	*((uint8_t *)g_streaming_channel_read_mock.buffer) = strlen(test_payload) + 1;

	STREAMING_CLIENT_CHANNEL_OPS(handle_read_result, true,
				     g_streaming_channel_read_mock.buffer,
				     g_streaming_channel_read_mock.size);

	/* Receive error while reading payload. */
	STREAMING_CLIENT_CHANNEL_OPS(handle_read_result, false,
				     g_streaming_channel_read_mock.buffer,
				     g_streaming_channel_read_mock.size);

	mock_streaming_channel_read_connection_status(false, false);

	mock_streaming_channel_read_connection_status(true, true);

	/* Check that we can read and no buffers were leaked. */
	/* Test that no buffer is leaked. */
	for (i = 0; i < NANOHUB_ADAPTER_QUEUE_DEPTH; i++)
		mock_streaming_channel_read_packet(test_payload,
						   strlen(test_payload) + 1);

	for (i = 0; i < NANOHUB_ADAPTER_QUEUE_DEPTH; i++) {
		ret = CLIENT_INTERFACE_FOPS(read, &g_file, read_buf,
					    sizeof(read_buf), 0);
		KUNIT_EXPECT_EQ(test, ret, strlen(test_payload) + 1);
		KUNIT_EXPECT_STREQ(test, read_buf, test_payload);
	}
}

static void does_not_read_more_than_queue_depth(struct kunit *test)
{
	int i;
	int ret;
	char read_buf[NANOHUB_ADAPTER_DATAGRAM_SIZE];
	char test_payload[] = "test payload";

	g_file.f_flags |= O_NONBLOCK;

	mock_streaming_channel_read_connection_status(true, true);

	for (i = 0; i < NANOHUB_ADAPTER_QUEUE_DEPTH; i++)
		mock_streaming_channel_read_packet(test_payload,
						   strlen(test_payload) + 1);

	for (i = 0; i < NANOHUB_ADAPTER_QUEUE_DEPTH; i++) {
		ret = CLIENT_INTERFACE_FOPS(read, &g_file, read_buf,
					    sizeof(read_buf), 0);
		KUNIT_EXPECT_GT(test, ret, 0);
	}

	ret = CLIENT_INTERFACE_FOPS(read, &g_file, read_buf,
				    sizeof(read_buf), 0);
	KUNIT_EXPECT_EQ(test, ret, -EAGAIN);
}

static void read_full_datagram(struct kunit *test)
{
	int ret;
	char read_buf[NANOHUB_ADAPTER_DATAGRAM_SIZE];
	uint8_t test_payload[NANOHUB_ADAPTER_DATAGRAM_SIZE] = { 0xba };

	mock_streaming_channel_read_connection_status(true, true);
	mock_streaming_channel_read_packet(test_payload, sizeof(test_payload));

	ret = CLIENT_INTERFACE_FOPS(read, &g_file, read_buf, sizeof(read_buf), 0);
	KUNIT_EXPECT_EQ(test, ret, sizeof(test_payload));
	KUNIT_EXPECT_EQ(test, memcmp(test_payload, read_buf,
				sizeof(test_payload)), 0);
}

static void read_with_count_less_than_datagram_size(struct kunit *test)
{
	int ret;
	char read_buf[NANOHUB_ADAPTER_DATAGRAM_SIZE];
	char test_payload[] = "payload gets truncated";

	mock_streaming_channel_read_connection_status(true, true);
	mock_streaming_channel_read_packet(test_payload, sizeof(test_payload) / 2);

	ret = CLIENT_INTERFACE_FOPS(read, &g_file, read_buf, sizeof(read_buf), 0);
	KUNIT_EXPECT_EQ(test, ret, sizeof(test_payload) / 2);
	KUNIT_EXPECT_EQ(test, memcmp(test_payload, read_buf,
				sizeof(test_payload) / 2), 0);

}

static void read_unexpected_callback(struct kunit *test)
{
	memset(&g_streaming_channel_close_mock, 0,
	       sizeof(g_streaming_channel_close_mock));
	STREAMING_CLIENT_CHANNEL_OPS(handle_read_result, true,
				     g_streaming_channel_read_mock.buffer,
				     g_streaming_channel_read_mock.size);

	KUNIT_EXPECT_TRUE(test, g_streaming_channel_close_mock.called);
}

static struct kunit_case nanohub_adapter_read_test_cases[] = {
	KUNIT_CASE(read_with_invalid_size),
	KUNIT_CASE(starts_read_ahead_on_open),
	KUNIT_CASE(starts_read_ahead_on_open_after_close),
	KUNIT_CASE(starts_payload_read_on_read_callback),
	KUNIT_CASE(stops_reading_when_queue_is_full),
	KUNIT_CASE(read_with_nonblock_and_no_data),
	KUNIT_CASE(read_with_queued_datagram),
	KUNIT_CASE(read_with_nonblock_with_queued_datagram),
	KUNIT_CASE(read_queued_datagrams_in_order),
	KUNIT_CASE(read_when_queue_is_full_unblocks_readahead),
	KUNIT_CASE(release_fd_when_queue_is_full_unblocks_readahead),
	KUNIT_CASE(read_with_queued_data_while_channel_closed),
	KUNIT_CASE(channel_closed_during_connection_status_callback),
	KUNIT_CASE(channel_closed_during_header_read),
	KUNIT_CASE(channel_closed_during_payload_read),
	KUNIT_CASE(channel_closed_while_queue_is_full),
	KUNIT_CASE(channel_reopen_while_queue_is_full),
	KUNIT_CASE(read_result_error_while_reading_header),
	KUNIT_CASE(read_result_error_while_reading_payload),
	KUNIT_CASE(does_not_read_more_than_queue_depth),
	KUNIT_CASE(read_full_datagram),
	KUNIT_CASE(read_with_count_less_than_datagram_size),
	KUNIT_CASE(read_unexpected_callback),
	{}
};

/* Nanohub adapter write tests suite. */

static void write_with_invalid_size(struct kunit *test)
{
	int ret;
	void *buf;

	ret = CLIENT_INTERFACE_FOPS(write, &g_file, buf, 0, 0);
	KUNIT_EXPECT_EQ(test, ret, -EINVAL);

	ret = CLIENT_INTERFACE_FOPS(write, &g_file, buf,
				    NANOHUB_ADAPTER_DATAGRAM_SIZE + 1, 0);
	KUNIT_EXPECT_EQ(test, ret, -EINVAL);
}

static void write_while_disconnected(struct kunit *test)
{
	int ret;
	char test_payload[] = "test payload";

	/* abn_StreamingChannel_Write returns error. */
	memset(&g_streaming_channel_write_mock, 0,
			sizeof(g_streaming_channel_write_mock));
	g_streaming_channel_write_mock.ret = false;

	ret = CLIENT_INTERFACE_FOPS(write, &g_file, test_payload,
			strlen(test_payload) + 1, 0);

	/* Data gets queued. */
	KUNIT_EXPECT_EQ(test, ret, strlen(test_payload) + 1);
}

static void write_with_success(struct kunit *test)
{
	int ret;
	char test_payload[] = "test payload";
	struct nanohub_packet *packet;

	memset(&g_streaming_channel_write_mock, 0,
			sizeof(g_streaming_channel_write_mock));
	g_streaming_channel_write_mock.ret = true;

	ret = CLIENT_INTERFACE_FOPS(write, &g_file, test_payload,
				    strlen(test_payload) + 1, 0);

	packet = (struct nanohub_packet *)g_streaming_channel_write_mock.data;

	KUNIT_EXPECT_EQ(test, ret, strlen(test_payload) + 1);
	KUNIT_EXPECT_EQ(test, g_streaming_channel_write_mock.size,
			strlen(test_payload) + 1 + NANOHUB_ADAPTER_HEADER_SIZE);
	KUNIT_EXPECT_EQ(test, packet->size, strlen(test_payload) + 1);
	KUNIT_EXPECT_STREQ(test, (char *)packet->payload, test_payload);
}

static void retries_write_after_connect(struct kunit *test)
{
	int ret;
	char test_payload[] = "test payload";
	struct nanohub_packet *packet;

	memset(&g_streaming_channel_write_mock, 0,
			sizeof(g_streaming_channel_read_mock));
	g_streaming_channel_write_mock.ret = false;

	ret = CLIENT_INTERFACE_FOPS(write, &g_file, test_payload,
			strlen(test_payload) + 1, 0);

	/* Data gets queued. */
	KUNIT_EXPECT_EQ(test, ret, strlen(test_payload) + 1);

	memset(&g_streaming_channel_write_mock, 0,
			sizeof(g_streaming_channel_write_mock));
	g_streaming_channel_write_mock.ret = true;

	/* After open connection status. */
	STREAMING_CLIENT_CHANNEL_OPS(handle_connection_status,
				     /* conn_status = */ true);

	/* Write call is retried. */
	packet = (struct nanohub_packet *)g_streaming_channel_write_mock.data;
	KUNIT_EXPECT_EQ(test, g_streaming_channel_write_mock.size,
			strlen(test_payload) + 1 + NANOHUB_ADAPTER_HEADER_SIZE);
	KUNIT_EXPECT_EQ(test, packet->size, strlen(test_payload) + 1);
	KUNIT_EXPECT_STREQ(test, (char *)packet->payload, test_payload);
}

static void queues_second_write(struct kunit *test)
{
	int ret;
	struct nanohub_packet *packet;
	char test_payload[][NANOHUB_ADAPTER_DATAGRAM_SIZE] = {
		"first test payload",
		"second test payload",
	};

	STREAMING_CLIENT_CHANNEL_OPS(handle_connection_status,
				     /* conn_status = */ true);

	memset(&g_streaming_channel_write_mock, 0,
			sizeof(g_streaming_channel_write_mock));
	g_streaming_channel_write_mock.ret = true;

	ret = CLIENT_INTERFACE_FOPS(write, &g_file, test_payload[0],
				    strlen(test_payload[0]) + 1, 0);

	packet = (struct nanohub_packet *)g_streaming_channel_write_mock.data;

	memset(&g_streaming_channel_write_mock, 0,
			sizeof(g_streaming_channel_write_mock));

	ret = CLIENT_INTERFACE_FOPS(write, &g_file, test_payload[1],
				    strlen(test_payload[1]) + 1, 0);

	/* Second message is not staged. */
	KUNIT_EXPECT_FALSE(test, g_streaming_channel_write_mock.called);

	memset(&g_streaming_channel_write_mock, 0,
			sizeof(g_streaming_channel_write_mock));

	STREAMING_CLIENT_CHANNEL_OPS(handle_write_result, true,
				     (unsigned char *)packet,
				     packet->size + 1);

	packet = (struct nanohub_packet *)g_streaming_channel_write_mock.data;

	/* Second message is now staged. */
	KUNIT_EXPECT_EQ(test, g_streaming_channel_write_mock.size,
			strlen(test_payload[1]) + 1 + NANOHUB_ADAPTER_HEADER_SIZE);
	KUNIT_EXPECT_EQ(test, packet->size, strlen(test_payload[1]) + 1);
	KUNIT_EXPECT_STREQ(test, (char *)packet->payload, test_payload[1]);
}

static void stages_write_when_idle(struct kunit *test)
{
	int ret;
	struct nanohub_packet *packet;
	char test_payload[][NANOHUB_ADAPTER_DATAGRAM_SIZE] = {
		"first test payload",
		"second test payload",
	};

	STREAMING_CLIENT_CHANNEL_OPS(handle_connection_status,
				     /* conn_status = */ true);

	memset(&g_streaming_channel_write_mock, 0,
			sizeof(g_streaming_channel_write_mock));
	g_streaming_channel_write_mock.ret = true;

	ret = CLIENT_INTERFACE_FOPS(write, &g_file, test_payload[0],
				    strlen(test_payload[0]) + 1, 0);

	STREAMING_CLIENT_CHANNEL_OPS(handle_write_result, true,
				     g_streaming_channel_write_mock.data,
				     g_streaming_channel_write_mock.size);

	memset(&g_streaming_channel_write_mock, 0,
			sizeof(g_streaming_channel_write_mock));

	ret = CLIENT_INTERFACE_FOPS(write, &g_file, test_payload[1],
				    strlen(test_payload[1]) + 1, 0);


	KUNIT_EXPECT_TRUE(test, g_streaming_channel_write_mock.called);

	packet = (struct nanohub_packet *)g_streaming_channel_write_mock.data;

	KUNIT_EXPECT_EQ(test, g_streaming_channel_write_mock.size,
			strlen(test_payload[1]) + 1 + NANOHUB_ADAPTER_HEADER_SIZE);
	KUNIT_EXPECT_EQ(test, packet->size, strlen(test_payload[1]) + 1);
	KUNIT_EXPECT_STREQ(test, (char *)packet->payload, test_payload[1]);
}

static void write_unexpected_callback(struct kunit *test)
{
	memset(&g_streaming_channel_close_mock, 0,
	       sizeof(g_streaming_channel_close_mock));
	STREAMING_CLIENT_CHANNEL_OPS(handle_write_result, true,
				     g_streaming_channel_read_mock.buffer,
				     g_streaming_channel_read_mock.size);

	KUNIT_EXPECT_TRUE(test, g_streaming_channel_close_mock.called);
}

static struct kunit_case nanohub_adapter_write_test_cases[] = {
	KUNIT_CASE(write_with_invalid_size),
	KUNIT_CASE(write_while_disconnected),
	KUNIT_CASE(write_with_success),
	KUNIT_CASE(retries_write_after_connect),
	KUNIT_CASE(queues_second_write),
	KUNIT_CASE(stages_write_when_idle),
	KUNIT_CASE(write_unexpected_callback),
	{}
};

/* Nanohub adapter poll tests suite. */

static void poll_no_data_not_connected(struct kunit *test)
{
	int ret;
	poll_table *wait = NULL;

	memset(&g_poll_wait_mock, 0, sizeof(g_poll_wait_mock));

	ret = CLIENT_INTERFACE_FOPS(poll, &g_file, wait);

	KUNIT_EXPECT_EQ(test, ret, POLLOUT | POLLWRNORM);

	/* Check that poll wait was set up correctly. */
	KUNIT_EXPECT_PTR_EQ(test, g_poll_wait_mock.filp, &g_file);
	KUNIT_EXPECT_PTR_EQ(test, g_poll_wait_mock.table, wait);
}

static void poll_no_data_connected(struct kunit *test)
{
	int ret;
	poll_table *wait = NULL;

	STREAMING_CLIENT_CHANNEL_OPS(handle_connection_status, true);

	ret = CLIENT_INTERFACE_FOPS(poll, &g_file, wait);

	KUNIT_EXPECT_EQ(test, ret, POLLOUT | POLLWRNORM);
}

static void poll_data_connected(struct kunit *test)
{
	int ret;
	poll_table *wait = NULL;
	char test_payload[] = "test payload";

	mock_streaming_channel_read_connection_status(true, true);
	mock_streaming_channel_read_packet(test_payload, sizeof(test_payload));

	ret = CLIENT_INTERFACE_FOPS(poll, &g_file, wait);

	KUNIT_EXPECT_EQ(test, ret, POLLIN | POLLRDNORM | POLLOUT | POLLWRNORM);
}

static void poll_read_wakes_up_poller(struct kunit *test)
{
	int ret;
	poll_table *wait = NULL;
	char test_payload[] = "test payload";

	memset(&g_wake_up_mock, 0, sizeof(g_wake_up_mock));
	mock_streaming_channel_read_connection_status(true, true);

	ret = CLIENT_INTERFACE_FOPS(poll, &g_file, wait);

	memset(&g_wake_up_mock, 0, sizeof(g_wake_up_mock));
	mock_streaming_channel_read_packet(test_payload, sizeof(test_payload));

	/* Check that packet read wakes up the correct wait queue. */
	KUNIT_EXPECT_PTR_EQ(test, g_wake_up_mock.wq_head,
			    g_poll_wait_mock.wait_address);
}

static void poll_connected_wakes_up_poller(struct kunit *test)
{
	int ret;
	poll_table *wait = NULL;

	memset(&g_poll_wait_mock, 0, sizeof(g_poll_wait_mock));
	ret = CLIENT_INTERFACE_FOPS(poll, &g_file, wait);

	memset(&g_wake_up_mock, 0, sizeof(g_wake_up_mock));
	mock_streaming_channel_read_connection_status(true, true);

	/* Check that the connected status wakes up the correct wait queue. */
	KUNIT_EXPECT_PTR_EQ(test, g_wake_up_mock.wq_head,
			    g_poll_wait_mock.wait_address);
}

static void poll_data_not_connected(struct kunit *test)
{
	int ret;
	poll_table *wait = NULL;
	char test_payload[] = "test payload";

	mock_streaming_channel_read_connection_status(true, true);
	mock_streaming_channel_read_packet(test_payload, sizeof(test_payload));
	STREAMING_CLIENT_CHANNEL_OPS(handle_connection_status, false);

	ret = CLIENT_INTERFACE_FOPS(poll, &g_file, wait);

	KUNIT_EXPECT_EQ(test, ret, POLLIN | POLLRDNORM | POLLOUT | POLLWRNORM);
}

static struct kunit_case nanohub_adapter_poll_test_cases[] = {
	KUNIT_CASE(poll_no_data_not_connected),
	KUNIT_CASE(poll_no_data_connected),
	KUNIT_CASE(poll_data_connected),
	KUNIT_CASE(poll_read_wakes_up_poller),
	KUNIT_CASE(poll_connected_wakes_up_poller),
	KUNIT_CASE(poll_data_not_connected),
	{}
};

static void kernel_exports_register_listener(struct kunit *test)
{
	bool ret;

	memset(&g_nanohub_message_listener_mock, 0,
	       sizeof(g_nanohub_message_listener_mock));

	ret = nanohub_register_listener(NANOHUB_ADAPTER_READ_TEST_CHANNEL,
					nanohub_message_listener);
	KUNIT_EXPECT_TRUE(test, ret);
	KUNIT_EXPECT_EQ(test, g_streaming_channel->config->index,
			NANOHUB_ADAPTER_READ_TEST_CHANNEL);
}

static void kernel_exports_receive_packet(struct kunit *test)
{
	char test_packet[] = "test data";

	memset(&g_nanohub_message_listener_mock, 0,
	       sizeof(g_nanohub_message_listener_mock));
	memset(&g_mock_queue_work_on, 0, sizeof(g_mock_queue_work_on));

	nanohub_register_listener(NANOHUB_ADAPTER_READ_TEST_CHANNEL,
				  nanohub_message_listener);

	/* Update connection status and receive a packet. */
	mock_streaming_channel_read_connection_status(true, true);
	mock_streaming_channel_read_packet(test_packet,
					   sizeof(test_packet));

	/* Work to deliver listener should be scheduled. */
	KUNIT_EXPECT_TRUE(test, g_mock_queue_work_on.called);

	/* Execute work. */
	g_mock_queue_work_on.work->func(g_mock_queue_work_on.work);

	KUNIT_EXPECT_EQ(test, g_nanohub_message_listener_mock.call_count, 1);
	KUNIT_EXPECT_STREQ(test, g_nanohub_message_listener_mock.buffer,
			   test_packet);
	KUNIT_EXPECT_EQ(test, g_nanohub_message_listener_mock.length,
			sizeof(test_packet));
}

static void kernel_exports_receive_multiple_packets(struct kunit *test)
{
	int i;
	char test_packet[] = "test data";
	const unsigned int num_packets = 4;

	memset(&g_nanohub_message_listener_mock, 0,
	       sizeof(g_nanohub_message_listener_mock));
	memset(&g_mock_queue_work_on, 0, sizeof(g_mock_queue_work_on));

	nanohub_register_listener(NANOHUB_ADAPTER_READ_TEST_CHANNEL,
				  nanohub_message_listener);

	/* Update connection status and receive a packet. */
	mock_streaming_channel_read_connection_status(true, true);
	mock_streaming_channel_read_packet(test_packet,
					   strlen(test_packet) + 1);

	KUNIT_EXPECT_TRUE(test, g_mock_queue_work_on.called);

	/* Receive more packets before worker executes. */
	g_mock_queue_work_on.called = false;
	for (i = 1; i < num_packets; i++) {
		mock_streaming_channel_read_packet(test_packet,
						   strlen(test_packet) + 1);

		/* Work should not be rescheduled. */
		KUNIT_EXPECT_FALSE(test, g_mock_queue_work_on.called);
	}

	/* Execute work. */
	g_mock_queue_work_on.work->func(g_mock_queue_work_on.work);

	KUNIT_EXPECT_EQ(test, g_nanohub_message_listener_mock.call_count,
			num_packets);
}

static void kernel_exports_unregister_listener(struct kunit *test)
{
	char test_packet[] = "test data";

	memset(&g_nanohub_message_listener_mock, 0,
	       sizeof(g_nanohub_message_listener_mock));
	memset(&g_mock_queue_work_on, 0, sizeof(g_mock_queue_work_on));

	nanohub_register_listener(NANOHUB_ADAPTER_READ_TEST_CHANNEL,
				  nanohub_message_listener);

	nanohub_unregister_listener(NANOHUB_ADAPTER_READ_TEST_CHANNEL);

	/* Update connection status and receive a packet. */
	mock_streaming_channel_read_connection_status(true, true);
	mock_streaming_channel_read_packet(test_packet,
					   sizeof(test_packet));

	/* Message should not be scheduled or delivered. */
	KUNIT_EXPECT_FALSE(test, g_mock_queue_work_on.called);
	KUNIT_EXPECT_EQ(test, g_nanohub_message_listener_mock.call_count, 0);
}

static void kernel_exports_unregister_listener_while_scheduled(struct kunit *test)
{
	char test_packet[] = "test data";

	memset(&g_nanohub_message_listener_mock, 0,
	       sizeof(g_nanohub_message_listener_mock));
	memset(&g_mock_queue_work_on, 0, sizeof(g_mock_queue_work_on));

	nanohub_register_listener(NANOHUB_ADAPTER_READ_TEST_CHANNEL,
				  nanohub_message_listener);


	/* Update connection status and receive a packet. */
	mock_streaming_channel_read_connection_status(true, true);
	mock_streaming_channel_read_packet(test_packet,
					   sizeof(test_packet));

	KUNIT_EXPECT_TRUE(test, g_mock_queue_work_on.called);

	/* Unregister the listener before work gets exected. */
	nanohub_unregister_listener(NANOHUB_ADAPTER_READ_TEST_CHANNEL);

	g_mock_queue_work_on.work->func(g_mock_queue_work_on.work);

	/* Message should not be delivered. */
	KUNIT_EXPECT_EQ(test, g_nanohub_message_listener_mock.call_count, 0);

}

static void kernel_exports_send_message(struct kunit *test)
{
	ssize_t ret;
	char test_payload[] = "test data";
	struct nanohub_packet *packet;

	/* Register listener just to open the channel so we don't block. */
	nanohub_register_listener(NANOHUB_ADAPTER_READ_TEST_CHANNEL,
				  nanohub_message_listener);
	mock_streaming_channel_read_connection_status(true, true);

	/* Prime the result handler so we don't block. */
	STREAMING_CLIENT_CHANNEL_OPS(handle_write_result, true, NULL, 0);

	memset(&g_streaming_channel_write_mock, 0,
			sizeof(g_streaming_channel_read_mock));
	g_streaming_channel_write_mock.ret = true;

	ret = nanohub_send_message(NANOHUB_ADAPTER_READ_TEST_CHANNEL,
				   test_payload, sizeof(test_payload));
	packet = (struct nanohub_packet *)g_streaming_channel_write_mock.data;

	KUNIT_EXPECT_EQ(test, ret, sizeof(test_payload));
	KUNIT_EXPECT_EQ(test, g_streaming_channel_write_mock.size,
			sizeof(test_payload) + NANOHUB_ADAPTER_HEADER_SIZE);
	KUNIT_EXPECT_EQ(test, packet->size, sizeof(test_payload));
	KUNIT_EXPECT_STREQ(test, (char *)packet->payload, test_payload);
}

static void kernel_exports_stops_reading_when_queue_is_full(struct kunit *test)
{
	int i;
	char test_packet[] = "test data";

	nanohub_register_listener(NANOHUB_ADAPTER_READ_TEST_CHANNEL,
				  nanohub_message_listener);

	/* Open channel to start read ahead. */
	mock_streaming_channel_read_connection_status(true, true);

	/* Fill the queue before running the worker. */
	for (i = 0; i < NANOHUB_ADAPTER_QUEUE_DEPTH - 1; i++)
		mock_streaming_channel_read_packet(test_packet,
						   sizeof(test_packet));

	/* One buffer left in the queue, read should be pending. */
	KUNIT_EXPECT_TRUE(test, g_streaming_channel_read_mock.called);

	mock_streaming_channel_read_packet(test_packet, sizeof(test_packet));

	memset(&g_streaming_channel_read_mock, 0,
	       sizeof(g_streaming_channel_read_mock));

	/* Queue is full, so read should not have been called. */
	KUNIT_EXPECT_FALSE(test, g_streaming_channel_read_mock.called);

	/* Drains the queue. */
	g_mock_queue_work_on.work->func(g_mock_queue_work_on.work);

	/* Now read should have resumed. */
	KUNIT_EXPECT_TRUE(test, g_streaming_channel_read_mock.called);
}

static struct kunit_case nanohub_adapter_kernel_exports_test_cases[] = {
	KUNIT_CASE(kernel_exports_register_listener),
	KUNIT_CASE(kernel_exports_receive_packet),
	KUNIT_CASE(kernel_exports_receive_multiple_packets),
	KUNIT_CASE(kernel_exports_unregister_listener),
	KUNIT_CASE(kernel_exports_unregister_listener_while_scheduled),
	KUNIT_CASE(kernel_exports_send_message),
	KUNIT_CASE(kernel_exports_stops_reading_when_queue_is_full),
	{}
};

static int nanohub_adapter_read_write_test_init(struct kunit *test)
{
	int ret;
	static struct abn_client_interface_config config = {
		.channel_range = {NANOHUB_ADAPTER_RANGE_START, NANOHUB_ADAPTER_RANGE_END},
		.llc_interface = NANOHUB_ADAPTER_LLC,
		.channels_specific_cfg = NULL
	};

	ret = nanohub_adapter_init(&config, &g_abn_client_interface);
	if (ret < 0)
		return ret;

	/* Open test channel. */
	memset(&g_inode, 0, sizeof(g_inode));
	memset(&g_file, 0, sizeof(g_file));
	g_inode.i_rdev = MKDEV(NANOHUB_ADAPTER_MAJOR, NANOHUB_ADAPTER_READ_TEST_CHANNEL);
	g_file.f_inode = &g_inode;
	ret = CLIENT_INTERFACE_FOPS(open, &g_inode, &g_file);
	if (ret < 0)
		g_abn_client_interface->ops->deinit(g_abn_client_interface);

	return ret;
}

static void nanohub_adapter_read_write_test_exit(struct kunit *test)
{
	CLIENT_INTERFACE_FOPS(release, &g_inode, &g_file);
	g_abn_client_interface->ops->deinit(g_abn_client_interface);
}

static int nanohub_adapter_open_test_init(struct kunit *test)
{
	static struct abn_client_interface_config config = {
		.channel_range = {NANOHUB_ADAPTER_RANGE_START, NANOHUB_ADAPTER_RANGE_END},
		.llc_interface = NANOHUB_ADAPTER_LLC,
		.channels_specific_cfg = NULL
	};

	return nanohub_adapter_init(&config, &g_abn_client_interface);
}

static void nanohub_adapter_open_test_exit(struct kunit *test)
{
	g_abn_client_interface->ops->deinit(g_abn_client_interface);
}

static struct kunit_suite nanohub_adapter_nanohub_client_test_suite = {
	.name = "nanohub_adapter_nanohub_client_test_suite",
	.test_cases = nanohub_adapter_nanohub_client_test_cases,
	.init = nanohub_adapter_open_test_init,
	.exit = nanohub_adapter_open_test_exit,
};

static struct kunit_suite nanohub_adapter_read_test_suite = {
	.name = "nanohub_adapter_read_test_suite",
	.test_cases = nanohub_adapter_read_test_cases,
	.init = nanohub_adapter_read_write_test_init,
	.exit = nanohub_adapter_read_write_test_exit,
};

static struct kunit_suite nanohub_adapter_write_test_suite = {
	.name = "nanohub_adapter_write_test_suite",
	.test_cases = nanohub_adapter_write_test_cases,
	.init = nanohub_adapter_read_write_test_init,
	.exit = nanohub_adapter_read_write_test_exit,
};

static struct kunit_suite nanohub_adapter_poll_test_suite = {
	.name = "nanohub_adapter_poll_test_suite",
	.test_cases = nanohub_adapter_poll_test_cases,
	.init = nanohub_adapter_read_write_test_init,
	.exit = nanohub_adapter_read_write_test_exit,
};

static struct kunit_suite nanohub_adapter_kernel_exports_test_suite = {
	.name = "nanohub_adapter_kernel_exports_test_suite",
	.test_cases = nanohub_adapter_kernel_exports_test_cases,
	.init = nanohub_adapter_open_test_init,
	.exit = nanohub_adapter_open_test_exit,
};

kunit_test_suites(&nanohub_adapter_read_test_suite,
		  &nanohub_adapter_write_test_suite,
		  &nanohub_adapter_poll_test_suite,
		  &nanohub_adapter_nanohub_client_test_suite,
		  &nanohub_adapter_kernel_exports_test_suite);

MODULE_AUTHOR("Vlad Urziceanu");
MODULE_LICENSE("GPL");
