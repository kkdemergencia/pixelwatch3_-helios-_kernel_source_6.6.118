// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2019, 2021 The Linux Foundation. All rights reserved.
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include <linux/err.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/reboot.h>
#include <linux/panic_notifier.h>
#include <linux/pm.h>
#include <linux/of_address.h>
#include <linux/nvmem-consumer.h>
#include <linux/input/qpnp-power-on.h>

struct qcom_reboot_reason {
	struct device *dev;
	struct notifier_block reboot_nb;
	struct nvmem_cell *nvmem_cell;
};

static struct qcom_reboot_reason *reboot;

struct poweroff_reason {
	const char *cmd;
	unsigned char pon_reason;
};

static struct poweroff_reason reasons[] = {
	{ "recovery",			0x01 },
	{ "bootloader",			0x02 },
	{ "rtc",			0x03 },
	{ "dm-verity device corrupted",	0x04 },
	{ "dm-verity enforcing",	0x05 },
	{ "keys clear",			0x06 },
	{ "shutdown",			0x0F },
	{ "kernel panic",		0x10 },
	{ "shipmode",			0x20 },
	{}
};

static int panic_notify(struct notifier_block *this,
                        unsigned long event, void *ptr)
{
	struct poweroff_reason *reason;

	for (reason = reasons; reason->cmd; reason++) {
		if (!strcmp("kernel panic", reason->cmd)) {
			nvmem_cell_write(reboot->nvmem_cell,
					 &reason->pon_reason,
					 sizeof(reason->pon_reason));
			break;
		}
	}

	return NOTIFY_OK;
}

static struct notifier_block panic_nb = {
	.notifier_call = panic_notify,
};

static int qcom_reboot_reason_reboot(struct notifier_block *this,
				     unsigned long event, void *ptr)
{
	char *cmd = ptr;
	struct poweroff_reason *reason;

	switch (event) {
		case SYS_RESTART:
			if (!cmd)
				return NOTIFY_OK;
			for (reason = reasons; reason->cmd; reason++) {
				if (!strcmp(cmd, reason->cmd)) {
					if (reboot->nvmem_cell)
						nvmem_cell_write(reboot->nvmem_cell,
								&reason->pon_reason,
								sizeof(reason->pon_reason));
					else
						qpnp_pon_set_restart_reason(
									(enum pon_restart_reason)reason->pon_reason);
					break;
				}
			}
			break;
		case SYS_POWER_OFF:
			for (reason = reasons; reason->cmd; reason++) {
				if (!strcmp("shutdown", reason->cmd)) {
					if (reboot->nvmem_cell)
						nvmem_cell_write(reboot->nvmem_cell,
								&reason->pon_reason,
								sizeof(reason->pon_reason));
					else
						qpnp_pon_set_restart_reason(
									(enum pon_restart_reason)reason->pon_reason);
					break;
				}
			}
			break;
	}

	return NOTIFY_OK;
}

static int qcom_reboot_reason_probe(struct platform_device *pdev)
{
	reboot = devm_kzalloc(&pdev->dev, sizeof(*reboot), GFP_KERNEL);
	if (!reboot)
		return -ENOMEM;

	reboot->dev = &pdev->dev;

	if (of_property_read_bool(pdev->dev.of_node, "qcom,no-nvmem-cell-support"))
		reboot->nvmem_cell = NULL;
	else {
		reboot->nvmem_cell = nvmem_cell_get(reboot->dev, "restart_reason");
		if (IS_ERR(reboot->nvmem_cell))
			return PTR_ERR(reboot->nvmem_cell);
	}

	reboot->reboot_nb.notifier_call = qcom_reboot_reason_reboot;
	reboot->reboot_nb.priority = 255;
	register_reboot_notifier(&reboot->reboot_nb);
	atomic_notifier_chain_register(&panic_notifier_list, &panic_nb);

	platform_set_drvdata(pdev, reboot);

	return 0;
}

static int qcom_reboot_reason_remove(struct platform_device *pdev)
{
	unregister_reboot_notifier(&reboot->reboot_nb);

	return 0;
}

static const struct of_device_id of_qcom_reboot_reason_match[] = {
	{ .compatible = "qcom,reboot-reason", },
	{},
};
MODULE_DEVICE_TABLE(of, of_qcom_reboot_reason_match);

static struct platform_driver qcom_reboot_reason_driver = {
	.probe = qcom_reboot_reason_probe,
	.remove = qcom_reboot_reason_remove,
	.driver = {
		.name = "qcom-reboot-reason",
		.of_match_table = of_match_ptr(of_qcom_reboot_reason_match),
	},
};

module_platform_driver(qcom_reboot_reason_driver);

MODULE_DESCRIPTION("MSM Reboot Reason Driver");
MODULE_LICENSE("GPL");
