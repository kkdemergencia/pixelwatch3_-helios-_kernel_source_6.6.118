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

#include <linux/kernel.h>
#include <linux/gpio.h>
#include <linux/regmap.h>

#include "autobahn_drv.h"
#include "main.h"
#include "hw_reset.h"

#define BOOT_MODE_ARG_SIZE (10)

#define AUTOBAHN_REGMAP_WRITE(reg, val)					\
	autobahn_regmap_write(pdata->pmic_regmap, #reg, enable,		\
			pdata->reg##_reg, val)

static int autobahn_regmap_write(struct regmap *map,
				const char *regname, bool enable,
				unsigned int reg, unsigned int val)
{
	int ret = regmap_write(map, reg, val);
	const char *op = enable ? "en" : "dis";

	if (ret != 0)
		pr_err("autobahn: failed to %sable %s: %d\n", op, regname, ret);
	return ret;
}

void autobahn_pmic_mag_ctrl(struct autobahn_data *data, bool enable)
{
	const struct autobahn_platform_data *pdata = data->pdata;

	if (pdata->mag_control_reg) {
		/* Enable/Disable MAG LDO */
		int ret = regmap_write(pdata->pmic_regmap, pdata->mag_control_reg, enable);

		if (ret != 0)
			pr_warn("autobahn: failed to %s mag_control ret=%x\n",
					enable ? "enable":"disable",
					ret);
	}
	msleep(10);
}

void autobahn_pmic_bcl_ctrl(struct autobahn_data *data, bool enable)
{
	const struct autobahn_platform_data *pdata = data->pdata;

	/* We only want to do this on devices that require it. */
	if (enable &&
		pdata->bclbig_comp_mode_ctl_reg &&
		pdata->bclbig_comp_en_ctl1_reg &&
		pdata->bclbig_comp_iadc_interval_hpm_reg &&
		pdata->bclbig_comp_iadc_interval_lpm_reg &&
		pdata->bclbig_comp_iadc_conv_req_reg) {
		AUTOBAHN_REGMAP_WRITE(bclbig_comp_mode_ctl, 0x00);
		AUTOBAHN_REGMAP_WRITE(bclbig_comp_en_ctl1, 0x80);
		AUTOBAHN_REGMAP_WRITE(bclbig_comp_iadc_interval_hpm, 0x06);
		AUTOBAHN_REGMAP_WRITE(bclbig_comp_iadc_interval_lpm, 0x06);
		AUTOBAHN_REGMAP_WRITE(bclbig_comp_iadc_conv_req, 0x04);
	}
}

void autobahn_pmic_afe_tx_sup_ctrl(struct autobahn_data *data, bool enable)
{
	const struct autobahn_platform_data *pdata = data->pdata;

	if (pdata->afe_control_reg) {
		/* Enable/Disable AFE_TX_SUP */
		int ret = regmap_write(pdata->pmic_regmap, pdata->afe_control_reg, enable);

		if (ret != 0)
			pr_warn("autobahn: failed to %s afe_tx_sup ret=%x\n",
					enable ? "enable":"disable",
					ret);
	}
}

void autobahn_pmic_clk32_ctrl(struct autobahn_data *data, bool enable)
{
	const struct autobahn_platform_data *pdata = data->pdata;

	if (pdata->clk32_control_reg) {
		/* Enable/Disable 32kHz clock */
		int ret = regmap_write(pdata->pmic_regmap, pdata->clk32_control_reg, enable);

		if (ret != 0)
			pr_warn("autobahn: failed to %s 32kHz clock ret=%x\n",
					enable ? "enable":"disable",
					ret);
	}
}

ssize_t autobahn_try_hw_reset(struct device *dev,
			      struct device_attribute *attr,
			      const char *buf, size_t count)
{
	struct autobahn_data *data = dev_get_drvdata(dev);
	char boot_mode_buf[BOOT_MODE_ARG_SIZE + 1];
	int boot_mode;
	int ret;

	// Read the boot mode argument. (ascii, possibly unterminated)
	if (count == 0 || count > BOOT_MODE_ARG_SIZE) {
		pr_err("%s: invalid input\n", __func__);
		return -EINVAL;
	}
	memcpy(boot_mode_buf, buf, count);
	boot_mode_buf[count] = 0;
	if (kstrtoint(boot_mode_buf, 0, &boot_mode) != 0) {
		pr_err("%s: invalid boot mode %s\n", __func__, boot_mode_buf);
		return -EINVAL;
	}

	ret = autobahn_drv_pause();
	if (ret < 0)
		return ret;

	autobahn_hw_reset(data, boot_mode);

	autobahn_drv_unpause();

	return count;
}

// boot_mode represents the ISP mode for the NXP MCU.
void autobahn_hw_reset(struct autobahn_data *data, int boot_mode)
{
	const struct autobahn_platform_data *pdata = data->pdata;
	int isp0 = boot_mode & 0x1;
	int isp1 = (boot_mode & 0x2) >> 1;
	int isp2 = (boot_mode & 0x4) >> 2;
	int ret;

	// Set the ISP mode
	if (isp1 != 1) {
		// ISP1 is not connected and is pulled high in ISP mode.
		pr_err("%s: invalid boot mode (%d), ISP pin1 must be high\n",
				__func__, boot_mode);
		return;
	}
	pr_info("%s: boot_mode=%d\n", __func__, boot_mode);
	gpio_set_value(pdata->boot0_gpio, isp0);
	gpio_set_value(pdata->boot2_gpio, isp2);

	/* power off sequence for NXP MCUs */
	autobahn_pmic_afe_tx_sup_ctrl(data, false);
	autobahn_pmic_mag_ctrl(data, false);
	autobahn_pmic_bcl_ctrl(data, true);
	autobahn_pmic_clk32_ctrl(data, false);

	// Reset MCU
	gpio_set_value(pdata->nreset_gpio, 0);
	usleep_range(1000, 2000);

	// Reset MCU vddcore voltage after reset, but prior to MCU boot
	ret = regmap_write(pdata->pmic_regmap, pdata->vddcore_control_reg,
			pdata->vddcore_voltage / 1000 & 0xff);
	if (ret == 0)
		ret = regmap_write(pdata->pmic_regmap,
				pdata->vddcore_control_reg + 1,
				pdata->vddcore_voltage / 1000 >> 8);
	if (ret != 0)
		pr_warn("autobahn: failed to reset vddcore ret=%x\n", ret);
	// Make sure MCU vddcore rail is enabled since the MCU can disable it
	ret = regmap_write(pdata->pmic_regmap, pdata->vddcore_enable_reg, 0x80);
	if (ret != 0)
		pr_warn("autobahn: failed to enable MCU vddcore ret=%x\n", ret);
	usleep_range(1000, 2000);

	// Force S5A PGOOD high prior to MCU boot (Work-around for QCOM Case #06525336)
	ret = regmap_write(pdata->pmic_regmap, pdata->s5a_pgood_state_reg, 0x80);
	if (ret != 0)
		pr_warn("autobahn: failed to set S5A PGOOD state ret=%x\n", ret);

	// Enable external regulator (if present)
	if (gpio_is_valid(pdata->reg_en_gpio))
		gpio_set_value(pdata->reg_en_gpio, 1);

	gpio_set_value(pdata->nreset_gpio, 1);
	usleep_range(14000, 24000);
}

ssize_t autobahn_pin_nreset_get(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct autobahn_data *data = dev_get_drvdata(dev);
	const struct autobahn_platform_data *pdata = data->pdata;

	return scnprintf(buf, PAGE_SIZE, "%d\n",
			 gpio_get_value(pdata->nreset_gpio));
}

ssize_t autobahn_pin_boot0_get(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	struct autobahn_data *data = dev_get_drvdata(dev);
	const struct autobahn_platform_data *pdata = data->pdata;

	return scnprintf(buf, PAGE_SIZE, "%d\n", gpio_get_value(pdata->boot0_gpio));
}

ssize_t autobahn_pin_boot2_get(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	struct autobahn_data *data = dev_get_drvdata(dev);
	const struct autobahn_platform_data *pdata = data->pdata;

	return scnprintf(buf, PAGE_SIZE, "%d\n", gpio_get_value(pdata->boot2_gpio));
}

ssize_t autobahn_pin_blstatus_get(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	struct autobahn_data *data = dev_get_drvdata(dev);
	const struct autobahn_platform_data *pdata = data->pdata;

	return gpio_is_valid(pdata->blstatus_gpio) ?
		scnprintf(buf, PAGE_SIZE, "%d\n",
			  gpio_get_value(pdata->blstatus_gpio)) : -ENODEV;
}

ssize_t autobahn_pin_nreset_set(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	struct autobahn_data *data = dev_get_drvdata(dev);
	const struct autobahn_platform_data *pdata = data->pdata;

	if (count >= 1 && buf[0] == '0') {

		/* power off sequence - common to all devices that use nreset interface */
		autobahn_pmic_afe_tx_sup_ctrl(data, false);
		autobahn_pmic_mag_ctrl(data, false);
		autobahn_pmic_bcl_ctrl(data, true);
		autobahn_pmic_clk32_ctrl(data, false);

		gpio_set_value(pdata->nreset_gpio, 0);
	} else if (count >= 1 && buf[0] == '1')
		gpio_set_value(pdata->nreset_gpio, 1);

	return count;
}

ssize_t autobahn_pin_boot0_set(struct device *dev,
			       struct device_attribute *attr,
			       const char *buf, size_t count)
{
	struct autobahn_data *data = dev_get_drvdata(dev);
	const struct autobahn_platform_data *pdata = data->pdata;

	if (count >= 1 && buf[0] == '0')
		gpio_set_value(pdata->boot0_gpio, 0);
	else if (count >= 1 && buf[0] == '1')
		gpio_set_value(pdata->boot0_gpio, 1);

	return count;
}

ssize_t autobahn_pin_boot2_set(struct device *dev,
			       struct device_attribute *attr,
			       const char *buf, size_t count)
{
	struct autobahn_data *data = dev_get_drvdata(dev);
	const struct autobahn_platform_data *pdata = data->pdata;

	if (count >= 1 && buf[0] == '0')
		gpio_set_value(pdata->boot2_gpio, 0);
	else if (count >= 1 && buf[0] == '1')
		gpio_set_value(pdata->boot2_gpio, 1);

	return count;
}
