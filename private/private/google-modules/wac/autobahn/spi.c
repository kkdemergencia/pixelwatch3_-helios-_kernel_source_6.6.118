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

#include <linux/delay.h>
#include <linux/spi/spi.h>
#include <linux/iio/iio.h>
#include <linux/gpio.h>
#include <linux/module.h>

#include "main.h"

#include "autobahn/platform/linux/transport.h"
#include "bl_nxp.h"
#include "spi.h"
#include "spi_bl.h"

#define SPI_MAX_SPEED_HZ    10000000
#define SPI_BITS_PER_WORD    8


// Set cs if it is under autobahn control.
void cs_set_value(int cs, int value)
{
	if (gpio_is_valid(cs))
		gpio_set_value(cs, value);
}

void autobahn_spi_bl_init(struct autobahn_spi_data *spi_data)
{
	struct autobahn_bl *bl = &spi_data->data.bl;
	const struct autobahn_platform_data *pdata = spi_data->data.pdata;

	bl->open = spi_bl_open;
	bl->write_ack = spi_bl_write_ack;
	bl->write_cmd = spi_bl_write_cmd;
	bl->write_data = spi_bl_write_data;
	bl->read_ack = spi_bl_read_ack;
	bl->read_response = spi_bl_read_response;
	bl->close = spi_bl_close;

	bl->irq1_gpio = pdata->irq1_gpio;
	bl->cs = spi_data->cs;
	bl->device = spi_data->spi_dev;
}

static int autobahn_spi_probe(struct spi_device *spi)
{
	struct autobahn_spi_data *spi_data;
	struct abn_Transport *abn_transport;
	struct spi_message msg;
	struct spi_transfer xfer = {
		.len = 0,
		.tx_buf = NULL,
		.rx_buf = NULL
	};
	int error;

	spi_data = devm_kzalloc(&spi->dev, sizeof(*spi_data), GFP_KERNEL);
	if (spi_data == NULL)
		return -ENOMEM;

	error = autobahn_probe(&spi->dev, &spi_data->data);
	if (error != 0)
		return error;

	spi_set_drvdata(spi, spi_data);

	spi_data->cs = spi_data->data.pdata->spi_cs_gpio;
	if (gpio_is_valid(spi_data->cs)) {
		error = gpio_request(spi_data->cs, "nanohub_spi_cs");
		if (error) {
			spi_data->cs = -1;
			pr_err("autobahn: spi_cs_gpio request failed\n");
		} else {
			gpio_direction_output(spi_data->cs, 1);
		}
	}

	spi_data->spi_dev = spi;

	autobahn_spi_bl_init(spi_data);

	switch (spi_data->data.pdata->spi_driver_impl) {
	case NXP_FD:
		error = abn_transport_fd_spi_create(&abn_transport, spi);
		break;
	case AMBIQ_HD:
		error = abn_transport_amhd_spi_create(&abn_transport, spi);
		break;
	case AMBIQ_FD:
		error = abn_transport_amfd_spi_create(&abn_transport, spi);
		break;
	default:
		pr_err("autobahn: SPI type %d not supported\n",
			spi_data->data.pdata->spi_driver_impl);
		error = -EINVAL;
		break;
	}

	if (error) {
		autobahn_remove(&spi_data->data);
		return error;
	}

	autobahn_framework_init(&spi_data->data, abn_transport);

	// Set up the SPI bus and run an empty message to set the idle state.
	// This setup can overridden in autobahn_spi_open and spi_bl_open.
	spi_data->spi_dev->max_speed_hz = spi->max_speed_hz ? : SPI_MAX_SPEED_HZ;
	spi_data->spi_dev->bits_per_word = SPI_BITS_PER_WORD;
	spi_setup(spi_data->spi_dev);
	spi_message_init_with_transfers(&msg, &xfer, 1);
	spi_sync_locked(spi_data->spi_dev, &msg);

	autobahn_start(&spi_data->data);

	return 0;
}

static void autobahn_spi_remove(struct spi_device *spi)
{
	struct autobahn_spi_data *spi_data = spi_get_drvdata(spi);

	if (gpio_is_valid(spi_data->cs)) {
		gpio_direction_output(spi_data->cs, 1);
		gpio_free(spi_data->cs);
	}

	autobahn_remove(&spi_data->data);
}

static int autobahn_spi_suspend(struct device *dev)
{
	struct autobahn_spi_data *spi_data = spi_get_drvdata(to_spi_device(dev));

	return autobahn_suspend(&spi_data->data);
}

static int autobahn_spi_suspend_noirq(struct device *dev)
{
	return autobahn_suspend_noirq();
}

static int autobahn_spi_resume(struct device *dev)
{
	struct autobahn_spi_data *spi_data = spi_get_drvdata(to_spi_device(dev));

	return autobahn_resume(&spi_data->data);
}

static void autobahn_spi_shutdown(struct spi_device *spi)
{
	struct autobahn_spi_data *spi_data = spi_get_drvdata(spi);

	autobahn_shutdown(&spi_data->data);
}

static struct spi_device_id autobahn_spi_id[] = {
	{AUTOBAHN_NAME, 0},
	{},
};

static const struct dev_pm_ops autobahn_spi_pm_ops = {
	.suspend = autobahn_spi_suspend,
	.suspend_noirq = autobahn_spi_suspend_noirq,
	.resume = autobahn_spi_resume,
};

static struct spi_driver autobahn_spi_driver = {
	.driver = {
		.name = AUTOBAHN_NAME,
		.owner = THIS_MODULE,
		.pm = &autobahn_spi_pm_ops,
	},
	.probe = autobahn_spi_probe,
	.remove = autobahn_spi_remove,
	.shutdown = autobahn_spi_shutdown,
	.id_table = autobahn_spi_id,
};

int __init autobahn_spi_init(void)
{
	return spi_register_driver(&autobahn_spi_driver);
}

void autobahn_spi_cleanup(void)
{
	spi_unregister_driver(&autobahn_spi_driver);
}

MODULE_DEVICE_TABLE(spi, autobahn_spi_id);
