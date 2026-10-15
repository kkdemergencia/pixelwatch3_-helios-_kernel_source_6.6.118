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

#include <linux/cdev.h>
#include <linux/delay.h>
#include <linux/firmware.h>
#include <linux/fs.h>
#include <linux/iio/iio.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of_gpio.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/poll.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <linux/sched.h>
#include <linux/sched/rt.h>
#include <linux/sched/signal.h>
#include <linux/sched/types.h>
#include <linux/slab.h>
#include <linux/spi/spi.h>
#include <linux/spinlock.h>
#include <linux/time.h>
#include <linux/uaccess.h>
#include <linux/vmalloc.h>
#include <uapi/linux/sched/types.h>

#include "autobahn/transport.h"
#include "autobahn_drv.h"
#include "hw_reset.h"
#include "main.h"
#include "spi.h"
#include "time_sync.h"

#define GPIO05_INT_SET_TYPE      0x00008C11
#define GPIO05_INT_POLARITY_HIGH 0x00008C12
#define GPIO05_INT_EN_SET        0x00008C15
#define GPIO05_INT_EN_CLR        0x00008C16
#define GPIO05_INT_MID_SEL       0x00008C1A
#define GPIO05_OUTPUT_CTRL       0x00008C44

#define GPIO_OPT_HAS_IRQ         0x0001
#define GPIO_OPT_OPTIONAL        0x8000

#define DT_MAX_PROP_SIZE	32

#define PROP_READ_U32_OPTIONAL(reg)                                     \
	do {								\
		if (of_property_read_u32(dt, "sensorhub," #reg,		\
						&pdata->reg##_reg))	\
			pdata->reg##_reg = 0;				\
	} while (0)

/**
 * struct gpio_config - this is a binding between platform data and driver data
 * @label:     for diagnostics
 * @flags:     to pass to gpio_request_one()
 * @options:   one or more of GPIO_OPT_* flags, below
 * @pdata_off: offset of u32 field in platform data with gpio #
 * @data_off:  offset of int field in driver data with irq # (optional)
 */
struct gpio_config {
	const char *label;
	u16 flags;
	u16 options;
	u16 pdata_off;
	u16 data_off;
};

#define PLAT_GPIO_DEF(name, _flags)                    \
	.pdata_off = offsetof(struct autobahn_platform_data, name ## _gpio),    \
	.label = "nanohub_" #name,                        \
	.flags = _flags                            \

#define PLAT_GPIO_DEF_IRQ(name, _flags, _opts)        \
	PLAT_GPIO_DEF(name, _flags),                \
	.data_off = offsetof(struct autobahn_data, name),    \
	.options = GPIO_OPT_HAS_IRQ | (_opts)        \

#define PLAT_GPIOD_DEF(name, _flags)                    \
	.label = "sensorhub," #name,                        \
	.pdata_off = offsetof(struct autobahn_platform_data, name ## _gpio),    \
	.flags = _flags                            \

#define PLAT_GPIOD_DEF_IRQ(name, _flags, _opts)        \
	PLAT_GPIOD_DEF(name, _flags),                \
	.data_off = offsetof(struct autobahn_data, name),    \
	.options = GPIO_OPT_HAS_IRQ | (_opts)        \

static int autobahn_pmic_irq_config(struct regmap *pmic_regmap);

static const struct gpio_config gconf[] = {
	{ PLAT_GPIO_DEF(nreset, GPIOF_OUT_INIT_HIGH) },
	{ PLAT_GPIO_DEF(boot0, GPIOF_OUT_INIT_HIGH) },
	{ PLAT_GPIO_DEF(boot2, GPIOF_OUT_INIT_HIGH) },
};

static const struct gpio_config gpiod_conf[] = {
	{ PLAT_GPIOD_DEF_IRQ(irq1, GPIOD_IN, 0) },
	{ PLAT_GPIOD_DEF(wakeup, GPIOD_OUT_LOW) },
};

static const struct iio_info autobahn_iio_info = {
};

static inline bool gpio_is_optional(const struct gpio_config *_cfg)
{
	return _cfg->options & GPIO_OPT_OPTIONAL;
}

static inline bool gpio_has_irq(const struct gpio_config *_cfg)
{
	return _cfg->options & GPIO_OPT_HAS_IRQ;
}

void autobahn_notify_thread(struct autobahn_data *data)
{
	WRITE_ONCE(data->kthread_run, 1);
	/* wake_up implementation works as memory barrier */
	wake_up_interruptible_sync(&data->kthread_wait);
}

static inline int plat_gpio_get(struct autobahn_data *data,
		const struct gpio_config *_cfg)
{
	const struct autobahn_platform_data *pdata = data->pdata;

	return *(u32 *)(((char *)pdata) + (_cfg)->pdata_off);
}

static inline struct gpio_desc **plat_gpiod_get(struct autobahn_data *data,
						const struct gpio_config *_cfg)
{
	const struct autobahn_platform_data *pdata = data->pdata;

	return (struct gpio_desc **)(((char *)pdata) + (_cfg)->pdata_off);
}

static inline void autobahn_set_irq_data(struct autobahn_data *data,
		const struct gpio_config *_cfg, int val)
{
	int *data_addr = ((int *)(((char *)data) + _cfg->data_off));

	if ((void *)data_addr > (void *)data &&
			(void *)data_addr < (void *)(data + 1))
		*data_addr = val;
	else
		WARN(1, "No data binding defined for %s", _cfg->label);
}

// Configure PM_GPIO_05 to function as PMIC_IRQ
static int autobahn_pmic_irq_config(struct regmap *pmic_regmap)
{
	int ret;
	// Command seqeuence provided by QCOM in Case #06209787
	// GPIO05 kernel write permission enabled by gpar/529000
	ret = regmap_write(pmic_regmap, GPIO05_INT_SET_TYPE, 0x1);
	ret |= regmap_write(pmic_regmap, GPIO05_INT_POLARITY_HIGH, 0x1);
	ret |= regmap_write(pmic_regmap, GPIO05_INT_EN_SET, 0x1);
	ret |= regmap_write(pmic_regmap, GPIO05_INT_EN_CLR, 0x1);
	ret |= regmap_write(pmic_regmap, GPIO05_OUTPUT_CTRL, 0x82);
	if (ret)
		pr_warn("nanohob: a PMIC_IRQ write operation failed, ret=%x\n", ret);

	return ret;
}

struct device_config {
	const char *name;
	const uint8_t channel;
};

static int autobahn_mcu_download_kthread(void *arg)
{
	struct autobahn_data *data = (struct autobahn_data *)arg;
	struct device *dev = data->nanohub_comms_dev;
	static const struct sched_param param = {
		.sched_priority = (MAX_RT_PRIO/2)-1,
	};

	pr_info("autobahn MCU download thread starting...");

	sched_setscheduler(current, SCHED_FIFO, &param);
	autobahn_reset(data);

	while (READ_ONCE(data->thread_to_exit) == 0) {
		wait_event_interruptible(data->kthread_wait,
				READ_ONCE(data->kthread_run));
		WRITE_ONCE(data->kthread_run, 0);

		if (atomic_read(&data->firmware_request.state) == FW_REQUESTED) {
			autobahn_download_firmware(dev);
			pr_info("autobahn - finished downloading firmware!");
		}
	}

	data->thread = NULL;
	kthread_complete_and_exit(NULL, 0);
}

static int autobahn_parse_vreg(struct device *dev, struct regulator **supply,
			       const char *name)
{
	int ret = 0;
	char prop_name[DT_MAX_PROP_SIZE];
	u32 val;
	u32 vals[2];

	snprintf(prop_name, DT_MAX_PROP_SIZE, "sensorhub,%s", name);
	*supply = devm_regulator_get(dev, prop_name);
	if (IS_ERR(*supply)) {
		pr_err("failed to get %s\n", name);
		goto out;
	}

	snprintf(prop_name, DT_MAX_PROP_SIZE, "sensorhub,%s-current", name);
	ret = of_property_read_u32(dev->of_node, prop_name, &val);
	if (!ret) {
		ret = regulator_set_load(*supply, val);
		if (ret < 0) {
			pr_err("error setting %s load\n", name);
			goto out;
		}
	}
	snprintf(prop_name, DT_MAX_PROP_SIZE, "sensorhub,%s-voltage", name);
	ret = of_property_read_u32_array(dev->of_node, prop_name, vals, 2);
	if (!ret) {
		ret = regulator_set_voltage(*supply, vals[0], vals[1]);
		if (ret < 0) {
			pr_err("error setting %s voltage\n", name);
			goto out;
		}
	}
	ret = regulator_enable(*supply);
	if (ret < 0) {
		pr_err("error enabling %s\n", name);
		goto out;
	}
out:
	return ret;
}

static int autobahn_parse_supplies(struct device *dev, struct regulator *supplies[],
				   const char *list, int cnt)
{
	int i, ret = 0;
	const char *name = NULL;

	for (i = 0; i < cnt; i++) {
		ret = of_property_read_string_index(dev->of_node, list, i, &name);
		if (ret)
			goto out;

		ret = autobahn_parse_vreg(dev, &supplies[i], name);
		if (ret < 0)
			goto out;
	}
out:
	return ret;
}

static struct autobahn_platform_data *autobahn_parse_dt(struct device *dev)
{
	struct autobahn_platform_data *pdata;
	struct device_node *dt = dev->of_node;
	struct device_node *spmi_node;
	struct platform_device *spmi_dev;
	int ret;
	u32 val = 0;

	if (!dt)
		return ERR_PTR(-ENODEV);

	pdata = devm_kzalloc(dev, sizeof(*pdata), GFP_KERNEL);
	if (!pdata)
		return ERR_PTR(-ENOMEM);

	ret = pdata->nreset_gpio =
		of_get_named_gpio(dt, "sensorhub,nreset-gpio", 0);
	if (ret < 0) {
		pr_err
			("autobahn: missing sensorhub,nreset-gpio in device tree\n");
		goto free_pdata;
	}

	ret = pdata->boot0_gpio =
		of_get_named_gpio(dt, "sensorhub,boot0-gpio", 0);
	if (ret < 0) {
		pr_err("autobahn: missing sensorhub,boot0-gpio in device tree\n");
		goto free_pdata;
	}

	ret = pdata->boot2_gpio =
		of_get_named_gpio(dt, "sensorhub,boot2-gpio", 0);
	if (ret < 0) {
		pr_err("autobahn: missing sensorhub,boot2-gpio in device tree\n");
		goto free_pdata;
	}

	pdata->firmware_download_valid = of_property_read_bool(dt,
		"sensorhub,firmware-download-enabled");

	pdata->blstatus_gpio = of_get_named_gpio(dt, "sensorhub,blstatus-gpio", 0);
	pdata->blstatus_gpio_valid = gpio_is_valid(pdata->blstatus_gpio);

	/* optional (spi) */
	pdata->spi_cs_gpio = of_get_named_gpio(dt, "sensorhub,spi-cs-gpio", 0);

	/* required spmi controller info */
	spmi_node = of_parse_phandle(dt, "sensorhub,spmi", 0);
	if (!spmi_node) {
		pr_err("autobahn: unable to find sensorhub,spmi node\n");
		ret = -EINVAL;
		goto free_pdata;
	}
	spmi_dev = of_find_device_by_node(spmi_node);
	of_node_put(spmi_node);
	if (!spmi_dev) {
		pr_err("autobahn: unable to find sensorhub,spmi device\n");
		ret = -EINVAL;
		goto free_pdata;
	}
	pdata->pmic_regmap = dev_get_regmap(spmi_dev->dev.parent, NULL);
	if (!pdata->pmic_regmap) {
		pr_err("autobahn: unable to get spmi controller regmap\n");
		ret = -EINVAL;
		goto free_pdata;
	}

	/* required afe_tx_sup control register */
	ret = of_property_read_u32(dt, "sensorhub,afe_control",
			&pdata->afe_control_reg);
	if (ret < 0) {
		pr_err("autobahn: missing sensorhub,afe_control in device tree\n");
		goto free_pdata;
	}

	/* mag control register */
	ret = of_property_read_u32(dt, "sensorhub,mag_control",
				   &pdata->mag_control_reg);
	if (ret < 0)
		pdata->mag_control_reg = 0;

	/* BCL (battery current limiting) control registers (for mag coex) */
	PROP_READ_U32_OPTIONAL(bclbig_comp_mode_ctl);
	PROP_READ_U32_OPTIONAL(bclbig_comp_en_ctl1);
	PROP_READ_U32_OPTIONAL(bclbig_comp_iadc_interval_hpm);
	PROP_READ_U32_OPTIONAL(bclbig_comp_iadc_interval_lpm);
	PROP_READ_U32_OPTIONAL(bclbig_comp_iadc_conv_req);

	/* required vddcore control register, enable register, and voltage */
	ret = of_property_read_u32(dt, "sensorhub,vddcore_control",
			&pdata->vddcore_control_reg);
	if (ret < 0) {
		pr_err("autobahn: missing sensorhub,vddcore_control in device tree\n");
		goto free_pdata;
	}

	ret = of_property_read_u32(dt, "sensorhub,vddcore_enable",
			&pdata->vddcore_enable_reg);
	if (ret < 0) {
		pr_err("autobahn: missing sensorhub,vddcore_enable in device tree\n");
		goto free_pdata;
	}

	ret = of_property_read_u32(dt, "sensorhub,vddcore_voltage",
			&pdata->vddcore_voltage);
	if (ret < 0) {
		pr_err("autobahn: missing sensorhub,vddcore_voltage in device tree\n");
		goto free_pdata;
	}

	/* required MCU 32KHz clock control register */
	ret = of_property_read_u32(dt, "sensorhub,clk32_control",
			&pdata->clk32_control_reg);
	if (ret < 0) {
		pr_err("autobahn: missing sensorhub,clk32_control in device tree\n");
		goto free_pdata;
	}

	/* required bob ext ctrl1 control register */
	ret = of_property_read_u32(dt, "sensorhub,bob_ext_ctrl1_control",
			&pdata->bob_ext_ctrl1_control_reg);
	if (ret < 0) {
		pr_err("autobahn: missing sensorhub,bob_ext_ctrl1_control in device tree\n");
		goto free_pdata;
	}

	/* required S5A PGOOD State control register */
	ret = of_property_read_u32(dt, "sensorhub,s5a_pgood_state_control",
			&pdata->s5a_pgood_state_reg);
	if (ret < 0) {
		pr_err("autobahn: missing sensorhub,s5a_pgood_state_control in device tree\n");
		goto free_pdata;
	}

	ret = of_property_read_u32(dt, "sensorhub,pmic_irq_enable", &val);
	if (!ret && val)
		autobahn_pmic_irq_config(pdata->pmic_regmap);

	pdata->supplies_cnt =
		of_property_count_strings(dt, "sensorhub,static-supplies");

	if (pdata->supplies_cnt < 0) {
		pr_err("failed to get static supplies(%d)\n", pdata->supplies_cnt);
		goto free_pdata;
	} else if (pdata->supplies_cnt > 0) {
		pdata->supplies = devm_kcalloc(dev, pdata->supplies_cnt, sizeof(struct regulator *),
					       GFP_KERNEL);
		if (!pdata->supplies) {
			ret = -ENOMEM;
			goto free_pdata;
		}
		ret = autobahn_parse_supplies(dev, pdata->supplies, "sensorhub,static-supplies",
					     pdata->supplies_cnt);
		if (ret < 0)
			goto free_supplies;
	} else {
		pdata->supplies = NULL;
	}

	pdata->firmware_name = of_get_property(dt, "sensorhub,firmware", NULL);
	if (!pdata->firmware_name) {
		pr_err("autobahn: missing sensorhub,firmware in device tree\n");
		ret = -EINVAL;
		goto free_supplies;
	}

	/* optional (external regulator enable) */
	pdata->reg_en_gpio = of_get_named_gpio(dt, "sensorhub,reg-en-gpio", 0);

	/* optional sets SPI transport type for Autobahn */
	pdata->spi_driver_impl = NXP_FD;
	ret = of_property_read_u32(dt, "sensorhub,spi_driver_impl", &val);
	if (!ret)
		pdata->spi_driver_impl = val;

	return pdata;

free_supplies:
	devm_kfree(dev, pdata->supplies);
free_pdata:
	devm_kfree(dev, pdata);
	return ERR_PTR(ret);
}

static int autobahn_request_legacy_gpios(struct autobahn_data *data)
{
	int i, ret = 0;

	for (i = 0; i < ARRAY_SIZE(gconf); ++i) {
		const struct gpio_config *cfg = &gconf[i];
		unsigned int gpio = plat_gpio_get(data, cfg);
		const char *label;
		bool optional = gpio_is_optional(cfg);

		ret = 0; /* clear errors on optional pins, if any */

		if (!gpio_is_valid(gpio) && optional)
			continue;

		label = cfg->label;
		ret = gpio_request_one(gpio, cfg->flags, label);
		if (ret && !optional) {
			pr_err("autobahn: gpio %d[%s] request failed;err=%d\n", gpio, label, ret);
			break;
		}
		if (gpio_has_irq(cfg)) {
			int irq = gpio_to_irq(gpio);

			if (irq > 0) {
				autobahn_set_irq_data(data, cfg, irq);
			} else if (!optional) {
				ret = -EINVAL;
				pr_err("autobahn: no irq; gpio %d[%s];err=%d\n",
						gpio, label, irq);
				break;
			}
		}
	}
	if (i < ARRAY_SIZE(gconf)) {
		for (--i; i >= 0; --i)
			gpio_free(plat_gpio_get(data, &gconf[i]));
	}

	return ret;
}

static void autobahn_release_legacy_gpios(struct autobahn_data *data)
{
	const struct autobahn_platform_data *pdata = data->pdata;

	gpio_free(pdata->nreset_gpio);
	gpio_set_value(pdata->boot0_gpio, 1);
	gpio_free(pdata->boot0_gpio);
	gpio_free(pdata->boot2_gpio);
}

static int autobahn_gpiod_get(struct autobahn_data *data)
{
	size_t i;
	int ret = 0;

	for (i = 0; i < ARRAY_SIZE(gpiod_conf); ++i) {
		const struct gpio_config *cfg = &gpiod_conf[i];
		struct gpio_desc **gpio = plat_gpiod_get(data, cfg);

		*gpio = devm_gpiod_get(data->dev, cfg->label, cfg->flags);
		if (IS_ERR(gpio)) {
			ret = PTR_ERR(gpio);
			pr_err("autobahn: gpio [%s] get failed;err=%d\n", cfg->label, ret);
			break;
		}

		if (gpio_has_irq(cfg)) {
			int irq = gpiod_to_irq(*gpio);

			if (irq > 0) {
				autobahn_set_irq_data(data, cfg, irq);
			} else {
				ret = -EINVAL;
				pr_err("autobahn: no irq; gpio [%s];err=%d\n",
				       cfg->label, irq);
				break;
			}
		}
	}

	return ret;
}

static int autobahn_request_gpios(struct autobahn_data *data)
{
	int ret;

	ret = autobahn_request_legacy_gpios(data);
	if (ret < 0)
		return ret;

	ret = autobahn_gpiod_get(data);
	if (ret < 0)
		autobahn_release_legacy_gpios(data);

	return ret;
}

int autobahn_probe(struct device *dev, struct autobahn_data *data)
{
	int ret;
	const struct autobahn_platform_data *pdata = data->pdata;

	pr_info("Autobahn probe\n");

	data->dev = dev;

	if (pdata == NULL) {
		pdata = autobahn_parse_dt(dev);
		if (IS_ERR(pdata))
			return PTR_ERR(pdata);
	}
	data->pdata = pdata;
	data->nanohub_comms_dev = NULL;

	ret = autobahn_request_gpios(data);
	if (ret)
		return ret;

	gpio_set_value(pdata->boot0_gpio, 0);

	/* Start the firmware download thread only when the
	 * "sensorhub,firmware-download-enabled" device tree property is set in
	 * the device tree settings (the `firmware_download_valid` member in
	 * struct `autobahn_platform_data` reflects this configuration from
	 * device tree).
	 */
	if (!pdata->firmware_download_valid)
		return 0;

	init_waitqueue_head(&data->kthread_wait);
	atomic_set(&data->firmware_request.state, FW_IDLE);
	init_completion(&data->firmware_request.fw_complete);
	WRITE_ONCE(data->kthread_run, 0);
	data->thread_to_exit = 0;
	data->thread = kthread_create(autobahn_mcu_download_kthread, data,
				      "ABN_MCU_Download");
	if (IS_ERR(data->thread)) {
		autobahn_release_legacy_gpios(data);
		return PTR_ERR(data->thread);
	}

	pr_info("Autobahn exiting probe\n");
	return 0;
}

int autobahn_framework_init(struct autobahn_data *data,
			    struct abn_Transport *abn_transport)
{
	int ret = autobahn_drv_core_init(data->dev, abn_transport,
					 data->pdata->irq1_gpio,
					 data->pdata->wakeup_gpio);

	if (ret != 0)
		return ret;

	ret = autobahn_drv_client_interfaces_init(data);
	if (ret != 0)
		autobahn_drv_core_deinit();

	return ret;
}

void autobahn_start(const struct autobahn_data *data)
{
	if (data->pdata->firmware_download_valid) {
		pr_debug("Waking up the MCU download thread\n");
		wake_up_process(data->thread);
	}
	time_sync_initialize(data->nanohub_comms_dev);
}

int autobahn_reset(const struct autobahn_data *data)
{
	const struct autobahn_platform_data *pdata = data->pdata;

	gpio_set_value(pdata->nreset_gpio, 1);
	usleep_range(650000, 700000);
	return 0;
}

void autobahn_remove(struct autobahn_data *data)
{
	autobahn_drv_client_interfaces_deinit();
	autobahn_drv_core_deinit();

	if (data->pdata->firmware_download_valid) {
		WRITE_ONCE(data->thread_to_exit, 1);
		autobahn_notify_thread(data);
		/* We don't call `kthread_stop(data->thread)` as the thread should
		 * not outlive the module and the cleanup is done through
		 * `kthread_complete_and_exit.
		 */
	}

	autobahn_release_legacy_gpios(data);
}

void autobahn_shutdown(struct autobahn_data *data)
{
	const struct autobahn_platform_data *pdata = data->pdata;

	/* power off sequence - common to all devices */
	autobahn_pmic_afe_tx_sup_ctrl(data, false);
	autobahn_pmic_mag_ctrl(data, false);
	autobahn_pmic_clk32_ctrl(data, false);

	// Hold the MCU in reset during shutdown. See b/313042435.
	gpio_set_value(pdata->nreset_gpio, 0);
}

int autobahn_suspend(const struct autobahn_data *data)
{
	const struct autobahn_platform_data *pdata = data->pdata;

	if (atomic_read(&data->firmware_request.state) != FW_IDLE)
		return -EBUSY;

	gpio_set_value(pdata->boot0_gpio, 1);

	return autobahn_drv_suspend();
}

int autobahn_suspend_noirq(void)
{
	return autobahn_drv_suspend_noirq();
}

int autobahn_resume(const struct autobahn_data *data)
{
	const struct autobahn_platform_data *pdata = data->pdata;

	gpio_set_value(pdata->boot0_gpio, 0);
	return autobahn_drv_resume();
}

static int __init autobahn_init(void)
{
	int ret;

	ret = autobahn_drv_init();
	if (ret < 0) {
		pr_err("failed to register abn drv; err=%d\n", ret);
		goto fail_drv_init;
	}

	ret = autobahn_spi_init();
	if (ret != 0) {
		pr_err("failed to resgister SPI drv; err=%d\n", ret);
		goto fail_spi_init;
	}
	pr_debug("autobahn: loaded; err=%d\n", ret);
	return ret;

fail_spi_init:
	autobahn_drv_exit();
fail_drv_init:
	pr_err("failed to init autoban; err=%d\n", ret);

	return ret;
}

static void __exit autobahn_cleanup(void)
{
	autobahn_spi_cleanup();

	autobahn_drv_exit();
}

module_init(autobahn_init);
module_exit(autobahn_cleanup);

MODULE_AUTHOR("Eugen Pirvu");
MODULE_LICENSE("GPL");
