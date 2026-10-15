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
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/kthread.h>
#include <linux/spi/spi.h>

#include "autobahn/platform/linux/transport.h"

#define FAKE_HANDLER_CONTEXT ((void *)0xf0cacc1a)
#define FAKE_TX_DATA ((uint8_t *)0xcafebadd)
#define FAKE_RX_DATA ((uint8_t *)0xdeadc0de)
#define FAKE_OTHER_TX_DATA ((uint8_t *)0xfeedc0de)
#define FAKE_OTHER_RX_DATA ((uint8_t *)0xdabbad00)
#define FAKE_DATA_LEN 256

static struct spi_device g_fake_spi_device;

/* Mocking variables. */
static int spi_sync_ret;
static struct spi_device *spi_sync_spi;
static struct spi_message *spi_sync_message;

static struct spi_message *spi_message_init_with_transfers_m;
static struct spi_transfer *spi_message_init_with_transfers_xfers;
static unsigned int spi_message_init_with_transfers_num_xfers;

static void *spi_transfer_result_handler_handler_context;
static bool spi_transfer_result_handler_success;

/* Mock spi functions. Works because wrappers are __weak symbols. */
int fd_spi_spi_sync_wrapper(struct spi_device *spi,
			    struct spi_message *message)
{
	spi_sync_spi = spi;
	spi_sync_message = message;

	return spi_sync_ret;
}

void fd_spi_spi_message_init_with_transfers_wrapper(
		struct spi_message *m,
		struct spi_transfer *xfers,
		unsigned int num_xfers)
{
	spi_message_init_with_transfers_m = m;
	spi_message_init_with_transfers_xfers = xfers;
	spi_message_init_with_transfers_num_xfers = num_xfers;
}

static void autobahn_transport_result_handler(void *handler_context,
					      bool success)
{
	spi_transfer_result_handler_handler_context = handler_context;
	spi_transfer_result_handler_success = success;
}

/* Fake device used to enable use of devm_... API in code under test. */
static void fake_spi_device_release(struct device *dev) {}

static int fake_spi_device_register(struct spi_device *device)
{
	memset(device, 0, sizeof(*device));
	dev_set_name(&device->dev, "%s", "fake_spi_device");
	device->dev.release = fake_spi_device_release;

	return device_register(&g_fake_spi_device.dev);
}

static void transport_create(struct kunit *test)
{
	struct abn_Transport *abn_transport;
	struct spi_device *spi_device = &g_fake_spi_device;

	KUNIT_EXPECT_EQ(test, 0, fake_spi_device_register(&g_fake_spi_device));

	KUNIT_EXPECT_EQ(test, 0, abn_transport_fd_spi_create(&abn_transport, spi_device));

	KUNIT_EXPECT_TRUE(test, abn_transport->functions->set_result_handler != NULL);
	KUNIT_EXPECT_TRUE(test, abn_transport->functions->prepare_transfer != NULL);
	KUNIT_EXPECT_TRUE(test, abn_transport->functions->initiate_transfer != NULL);
	KUNIT_EXPECT_TRUE(test, abn_transport->functions->abort_transfer != NULL);

	device_unregister(&g_fake_spi_device.dev);
}

struct kunit_case autobahn_transport_create_test_cases[] = {
	KUNIT_CASE(transport_create),
	{}
};

struct kunit_suite autobahn_transport_create_test_suite = {
	.name = "autobahn_transport_create_test_suite",
	.test_cases = autobahn_transport_create_test_cases
};

static int autobahn_transport_transfer_init(struct kunit *test)
{
	int ret;
	struct abn_Transport *transport;

	ret = fake_spi_device_register(&g_fake_spi_device);
	if (ret < 0)
		return ret;

	spi_sync_ret = 0;
	spi_sync_spi = NULL;
	spi_sync_message = NULL;

	spi_message_init_with_transfers_m = NULL;
	spi_message_init_with_transfers_xfers = NULL;
	spi_message_init_with_transfers_num_xfers = 0;

	spi_transfer_result_handler_handler_context = NULL;
	spi_transfer_result_handler_success = true;

	ret = abn_transport_fd_spi_create(&transport, &g_fake_spi_device);
	if (ret < 0)
		return ret;

	transport->functions->set_result_handler(transport->transport_handle,
			autobahn_transport_result_handler,
			FAKE_HANDLER_CONTEXT);
	test->priv = transport;

	return 0;
}

static void autobahn_transport_transfer_exit(struct kunit *test)
{
	device_unregister(&g_fake_spi_device.dev);
}

static void initiate_transfer(struct kunit *test)
{
	struct abn_Transport *transport = (struct abn_Transport *)test->priv;

	transport->functions->prepare_transfer(transport->transport_handle,
					       FAKE_TX_DATA, FAKE_RX_DATA,
					       FAKE_DATA_LEN);

	/* Check that the transfer was set up correctly. */
	KUNIT_EXPECT_PTR_EQ(test, FAKE_TX_DATA,
			    spi_message_init_with_transfers_xfers->tx_buf);
	KUNIT_EXPECT_PTR_EQ(test, FAKE_RX_DATA,
			    spi_message_init_with_transfers_xfers->rx_buf);
	KUNIT_EXPECT_EQ(test, FAKE_DATA_LEN,
			spi_message_init_with_transfers_xfers->len);

	transport->functions->initiate_transfer(transport->transport_handle);

	/* Check that the transfer was submitted correctly. */
	KUNIT_EXPECT_PTR_EQ(test, &g_fake_spi_device, spi_sync_spi);
	KUNIT_EXPECT_PTR_EQ(test, spi_message_init_with_transfers_m,
			    spi_sync_message);
}

static void initiate_transfer_spi_sync_error(struct kunit *test)
{
	struct abn_Transport *transport = (struct abn_Transport *)test->priv;

	transport->functions->prepare_transfer(transport->transport_handle,
					       FAKE_TX_DATA, FAKE_RX_DATA,
					       FAKE_DATA_LEN);

	spi_sync_ret = -1;
	transport->functions->initiate_transfer(transport->transport_handle);

	/* Check that the result handler was called. */
	KUNIT_EXPECT_PTR_EQ(test, FAKE_HANDLER_CONTEXT,
			spi_transfer_result_handler_handler_context);
	KUNIT_EXPECT_EQ(test, false, spi_transfer_result_handler_success);
}

static void abort_idle(struct kunit *test)
{
	struct abn_Transport *transport = (struct abn_Transport *)test->priv;

	transport->functions->abort_transfer(transport->transport_handle);

	/* Try to set up a transfer. */
	transport->functions->prepare_transfer(transport->transport_handle,
			FAKE_TX_DATA, FAKE_RX_DATA,
			FAKE_DATA_LEN);

	/* Check that the transfer was set up correctly. */
	KUNIT_EXPECT_PTR_EQ(test, FAKE_TX_DATA,
			    spi_message_init_with_transfers_xfers->tx_buf);
	KUNIT_EXPECT_PTR_EQ(test, FAKE_RX_DATA,
			    spi_message_init_with_transfers_xfers->rx_buf);
	KUNIT_EXPECT_EQ(test, FAKE_DATA_LEN,
			spi_message_init_with_transfers_xfers->len);
}

static void abort_after_transfer_completes(struct kunit *test)
{
	struct abn_Transport *transport = (struct abn_Transport *)test->priv;

	transport->functions->prepare_transfer(transport->transport_handle,
					       FAKE_TX_DATA, FAKE_RX_DATA,
					       FAKE_DATA_LEN);

	transport->functions->initiate_transfer(transport->transport_handle);

	transport->functions->abort_transfer(transport->transport_handle);

	/* Try to set up a transfer. */
	transport->functions->prepare_transfer(transport->transport_handle,
					       FAKE_TX_DATA, FAKE_RX_DATA,
					       FAKE_DATA_LEN);

	/* Check that the result handler was called. */
	KUNIT_EXPECT_PTR_EQ(test, FAKE_HANDLER_CONTEXT,
			    spi_transfer_result_handler_handler_context);
	KUNIT_EXPECT_EQ(test, true, spi_transfer_result_handler_success);

	/* Check that the transfer was set up correctly. */
	KUNIT_EXPECT_PTR_EQ(test, FAKE_TX_DATA,
			    spi_message_init_with_transfers_xfers->tx_buf);
	KUNIT_EXPECT_PTR_EQ(test, FAKE_RX_DATA,
			    spi_message_init_with_transfers_xfers->rx_buf);
	KUNIT_EXPECT_EQ(test, FAKE_DATA_LEN,
			spi_message_init_with_transfers_xfers->len);
}

static void prepare_abort_initiate_transfer(struct kunit *test)
{
	struct abn_Transport *transport = (struct abn_Transport *)test->priv;

	transport->functions->prepare_transfer(transport->transport_handle,
					       FAKE_TX_DATA, FAKE_RX_DATA,
					       FAKE_DATA_LEN);

	/* Should not assert. */
	transport->functions->abort_transfer(transport->transport_handle);

	transport->functions->prepare_transfer(transport->transport_handle,
					       FAKE_OTHER_TX_DATA,
					       FAKE_OTHER_RX_DATA,
					       FAKE_DATA_LEN);

	/* Check that the new transfer was set up. */
	KUNIT_EXPECT_PTR_EQ(test, FAKE_OTHER_TX_DATA,
			    spi_message_init_with_transfers_xfers->tx_buf);
	KUNIT_EXPECT_PTR_EQ(test, FAKE_OTHER_RX_DATA,
			    spi_message_init_with_transfers_xfers->rx_buf);

	transport->functions->initiate_transfer(transport->transport_handle);
}

struct kunit_case autobahn_transport_transfer_test_cases[] = {
	KUNIT_CASE(initiate_transfer),
	KUNIT_CASE(initiate_transfer_spi_sync_error),
	KUNIT_CASE(abort_idle),
	KUNIT_CASE(abort_after_transfer_completes),
	KUNIT_CASE(prepare_abort_initiate_transfer),
	{}
};

struct kunit_suite autobahn_transport_transfer_test_suite = {
	.name = "autobahn_transport_transfer_test_suite",
	.test_cases = autobahn_transport_transfer_test_cases,
	.init = autobahn_transport_transfer_init,
	.exit = autobahn_transport_transfer_exit,
};

kunit_test_suites(&autobahn_transport_create_test_suite,
		&autobahn_transport_transfer_test_suite);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Vlad Urziceanu");
