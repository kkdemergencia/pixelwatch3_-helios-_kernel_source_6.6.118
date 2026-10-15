/* drivers/input/misc/pat9xxx.c
 *
 * Copyright (c) 2016, The Linux Foundation. All rights reserved.
 *
 */

#define pr_fmt(fmt) "%s: " fmt, KBUILD_MODNAME

#include <linux/input.h>
#include <linux/pm.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/of_gpio.h>
#include <linux/of_device.h>
#include <linux/delay.h>
#include <linux/regulator/consumer.h>
#include <linux/pinctrl/consumer.h>
#include <linux/fs.h>
#include <linux/file.h>
#include <linux/mm.h>
#include <asm/uaccess.h>
#include <linux/notifier.h>
#include <linux/reboot.h>
#include <linux/time.h>
#include <linux/rtc.h>
#include <linux/string.h>
#include <linux/soc/qcom/panel_event_notifier.h>
#include <i2c_arbitration_module.h>
#include <mcu_transport_interface.h>

#include "pat9xxx.h"
#include "rsb_comms.h"

#define WAKE_SENSITIVITY    1
#define REDUCED_SENSITIVITY(s) ((s)/10) /* 10% of sensitivity */
#define SENSITIVITY_LEN     4 /* range "0" - "255" */
#define DISPLAY_BLANKED     0
#define DISPLAY_UNBLANKED   1
#define MAX_PROP_SIZE       32

enum pat9xxx_supplies {
	PAT9XXX_SUPPLY_VLD,
	PAT9XXX_SUPPLY_VDD,
	PAT9XXX_NUM_SUPPLIES,
};

static const char *pat9xxx_supply_names[PAT9XXX_NUM_SUPPLIES] = {
	[PAT9XXX_SUPPLY_VLD] = "pixart,vld",
	[PAT9XXX_SUPPLY_VDD] = "pixart,vdd",
};

// Module parameter
static bool offload_enabled;

#define REGISTER_MCU_TRANSPORT_DELAY_JIFFIES msecs_to_jiffies(100)
static struct delayed_work register_mcu_transport_work;
static bool i2c_arb_init;
static bool mcu_transport_init;

struct pixart_pat9xxx_data {
	struct i2c_client *client;
	struct input_dev *input;
	int irq;
	u32	irq_flags;
	struct device *pat9xxx_device;
	u32 press_keycode;
	bool press_en;
	bool inverse_x;
	bool inverse_y;
	int state;
	int display_mode;
	u16 crown_sensitivity;
	u16 current_sensitivity;
	u8  wake_sensitivity;
	u8 slop;
	u8 motion_interval;
	u8 motion_threshold;
	u8 current_threshold;
	u8 delay_interval;
	u8 sleep_state;
	u8 sleep_freq;
	bool wake_handled;
	bool offload;
	struct delayed_work polling_work;
	struct delayed_work resume_work;
	struct drm_panel *active_panel;
	void *notifier_cookie;
	struct mutex mtx;
	struct regulator_bulk_data supplies[PAT9XXX_NUM_SUPPLIES];
	u32 active_load[PAT9XXX_NUM_SUPPLIES];
	u32 sleep_load[PAT9XXX_NUM_SUPPLIES];
	u32 voltage[PAT9XXX_NUM_SUPPLIES];
	u8 product_id2;
};

static struct pixart_pat9xxx_data *pat9xxx_offld_data;

#define PAT9XXX_STATE_SUSPEND  0
#define PAT9XXX_STATE_ON       1
#define PAT9XXX_STATE_RESUMING 2

struct rw_reg_info {
	char flag; /*R/W char*/
	long w_addr;
	long r_addr;
	long r_data;
};

struct rw_reg_info pat9xxx_reg_info;

/* Declaration of suspend and resume functions */
static void pat9xxx_drm_panel_notifier_callback(enum panel_event_notifier_tag tag,
                struct panel_event_notification *notification, void *data);

static int pat9xxx_i2c_arbitration_notifier_callback(void *client_data,
						     enum i2c_arbitration_event_type type);

static int pat9xxx_display_suspend(struct device *dev);
static int pat9xxx_display_resume(struct device *dev);

/* Helper routines */

static bool is_pat9126(struct pixart_pat9xxx_data *data)
{
	return data->product_id2 == PIXART_PAT9126_SENSOR_ID2;
}

static bool is_pat9401(struct pixart_pat9xxx_data *data)
{
	return data->product_id2 == PIXART_PAT9401_SENSOR_ID2;
}

static int pat9xxx_set_loads_active(struct pixart_pat9xxx_data *data)
{
	int ret, i;

	for (i=0; i<ARRAY_SIZE(data->supplies); i++) {
		ret = regulator_set_load(data->supplies[i].consumer, data->active_load[i]);
		if (ret) {
			pr_err("Failed to set regulator load\n");
			return ret;
		}
	}

	return 0;
}

static int pat9xxx_set_loads_sleep(struct pixart_pat9xxx_data *data)
{
	int ret, i;

	for (i=0; i<ARRAY_SIZE(data->supplies); i++) {
		ret = regulator_set_load(data->supplies[i].consumer, data->sleep_load[i]);
		if (ret) {
			pr_err("Failed to set regulator load\n");
			return ret;
		}
	}

	return 0;
}

static int pat9xxx_enable_regulators(struct pixart_pat9xxx_data *data)
{
	int ret;

	ret = regulator_bulk_enable(ARRAY_SIZE(data->supplies), data->supplies);
	if (ret) {
		pr_err("Failed to enable regulators\n");
		return ret;
	}

	return 0;
}

static void pat9xxx_disable_regulators(struct pixart_pat9xxx_data *data)
{
	int ret;

	ret = regulator_bulk_disable(ARRAY_SIZE(data->supplies), data->supplies);
	if (ret) {
		pr_err("Failed to disable vld regulator\n");
	}
}

static int pat9xxx_write(struct i2c_client *client, u8 addr, u8 data)
{
	struct pixart_pat9xxx_data *patdata = i2c_get_clientdata(client);

	if (patdata->offload) {
		pr_err("Write operation not allowed in offload mode\n");
		goto exit_write;
	}

	u8 buf[BUF_SIZE];
	struct device *dev = &client->dev;

	buf[0] = addr;
	buf[1] = data;

	/* Returns negative errno, or else the number of bytes written. */
	if (i2c_master_send(client, buf, BUF_SIZE) < 0) {
		dev_err(dev, "%s Failed: writing to reg 0x%x\n", __func__, addr);
		return -EIO;
	}

exit_write:
	return 0;
}

static int pat9xxx_read(struct i2c_client *client, u8 addr, u8 *data)
{
	struct pixart_pat9xxx_data *patdata = i2c_get_clientdata(client);

	if (patdata->offload) {
		pr_err("Read operation not allowed in offload mode\n");
		goto exit_read;
	}

	struct device *dev = &client->dev;
	struct i2c_msg msg[] = {
		{
		 .addr = client->addr,
		 .flags = 0,
		 .len = 1,
		 .buf = &addr,
		}, {
		 .addr = client->addr,
		 .flags = 1,
		 .len = 1,
		 .buf = data,
		}
	};

	if (i2c_transfer(client->adapter, msg, 2) != 2) {
		dev_err(dev, "%s Failed: reading reg 0x%x\n", __func__,
			addr);
		return -EIO;
	}

exit_read:
	return 0;
}

static int pat9xxx_write_verified(struct i2c_client *client, u8 address, u8 data)
{
	int i, ret;
	u8 read_value;

	for (i = 0; i < PAT9150_I2C_RETRY; i++) {
		ret = pat9xxx_write(client, address, data);
		if (ret < 0)
			return ret;
		ret = pat9xxx_read(client, address, &read_value);
		if (ret < 0)
			return ret;

		if (read_value == data)
			return 0;

		msleep(1);
	}

	return -EIO;
}

static int pat9xxx_enable_writeprotect(struct i2c_client *client)
{
	return pat9xxx_write_verified(client, PIXART_PAT9126_WRITE_PROTECT_REG,
				PIXART_PAT9126_ENABLE_WRITE_PROTECT);
}

static int pat9xxx_disable_writeprotect(struct i2c_client *client)
{
	return pat9xxx_write_verified(client, PIXART_PAT9126_WRITE_PROTECT_REG,
				PIXART_PAT9126_DISABLE_WRITE_PROTECT);
}

static int pat9xxx_set_motioninterval(struct i2c_client *client, u8 interval)
{
	struct pixart_pat9xxx_data *data = i2c_get_clientdata(client);
	data->motion_interval = interval;
	return pat9xxx_write(client, PIXART_PAT9401_MOTION_INTERVAL_REG,
			data->motion_interval);
}

static int pat9xxx_set_motionthreshold(struct i2c_client *client, u8 threshold)
{
	struct pixart_pat9xxx_data *data = i2c_get_clientdata(client);
	data->current_threshold = threshold;
	return pat9xxx_write(client, PIXART_PAT9401_MOTION_THRESHOLD_REG,
			data->current_threshold);
}

void delay(int ms)
{
	msleep(ms);
}

static int pat9126_sensor_init(struct i2c_client *client)
{
	/* Data sheet: PAT9126JA-TKIT_DS_V1.2_25102019
	 * Section 7.1.3: Initial setting
	 */
	int ret = 0;
	struct pixart_pat9xxx_data *data = i2c_get_clientdata(client);
	/*
	 * PAT9126 sensor recommended settings:
	 * switch to bank0, not allowed to perform pat9xxx_write_verified
	 */
	pat9xxx_write(client, PIXART_PAT9126_SELECT_BANK_REG,
			PIXART_PAT9126_BANK0);
	/*
	 * software reset (i.e. set bit7 to 1).
	 * It will reset to 0 automatically
	 * so perform OTS_RegWriteRead is not allowed.
	 */
	pat9xxx_write(client, PIXART_PAT9126_CONFIG_REG,
			PIXART_PAT9126_RESET);

	/* delay 1ms */
	usleep_range(RESET_DELAY_US, RESET_DELAY_US + 1);

	/* disable write protect */
	if (pat9xxx_disable_writeprotect(client) < 0)
		return (-1);
	/* set X-axis resolution (depends on application) */
	if (pat9xxx_write_verified(client, PIXART_PAT9126_SET_CPI_RES_X_REG,
		PIXART_PAT9126_CPI_RESOLUTION_X) < 0)
		return (-1);
	/* set Y-axis resolution (depends on application) */
	if (pat9xxx_write_verified(client, PIXART_PAT9126_SET_CPI_RES_Y_REG,
				PIXART_PAT9126_CPI_RESOLUTION_Y) < 0)
		return (-1);
	/* set 12-bit X/Y data format (depends on application) */
	if (pat9xxx_write_verified(client, PIXART_PAT9126_ORIENTATION_REG,
				PIXART_PAT9126_MOTION_DATA_LENGTH) < 0)
		return (-1);
	/* ONLY for VDD=VDDA=1.7~1.9V: for power saving */
	if (pat9xxx_write_verified(client, PIXART_PAT9126_VOLTAGE_SEGMENT_SEL_REG,
				PIXART_PAT9126_LOW_VOLTAGE_SEGMENT) < 0)
		return (-1);

	pat9xxx_write_verified(client, PIXART_PAT9126_SENSOR_MODE_SELECT_REG,
			PIXART_PAT9126_SENSOR_SET_MODE2);

	pat9xxx_write_verified(client, PIXART_PAT9126_AE_ENABLE, PIXART_PAT9126_AE_ENABLE_VAL);
	pat9xxx_write_verified(client, PIXART_PAT9126_NY_MIN, PIXART_PAT9126_NY_MIN_VAL);

	data->crown_sensitivity   = PIXART_PAT9126_CPI_RESOLUTION_X;
	data->current_sensitivity = PIXART_PAT9126_CPI_RESOLUTION_X;
	data->wake_sensitivity    = WAKE_SENSITIVITY;

	data->sleep_state = SLEEP_STATE_2;
	data->sleep_freq  = SLEEP_STATE2_FREQ_1024;

	ret = pat9xxx_enable_mot(client);
	if (ret < 0) {
		pr_err("Enable Motion FAIL.");
	}

	/* enable write protect */
	pat9xxx_enable_writeprotect(client);
	return ret;
}

static int pat9401_sensor_init(struct i2c_client *client)
{
	u16 sensitivity = (((u16)(PIXART_PAT9401_CPI_RES_X_HI_DEFAULT_VAL
				& PIXART_PAT9401_RESX_HI_MASK)) << 4)
				| PIXART_PAT9401_CPI_RES_X_LO_DEFAULT_VAL;
	struct pixart_pat9xxx_data *data = i2c_get_clientdata(client);
	/* Data sheet: PAT9401E1-CVBB_DS_V0.8_22082023
	 * Section 7.1.3: Initial setting
	 */
	pat9xxx_write(client, PIXART_PAT9126_SELECT_BANK_REG,
                      PIXART_PAT9126_BANK0);
	pat9xxx_write(client, PIXART_PAT9401_SW_RESET_REG,
                      PIXART_PAT9401_RESET);
	usleep_range(RESET_DELAY_US, RESET_DELAY_US + 1);
	pat9xxx_disable_writeprotect(client);
	pat9xxx_write(client, PIXART_PAT9401_CPI_RES_X_LO_REG,
                      PIXART_PAT9401_CPI_RES_X_LO_DEFAULT_VAL);
	pat9xxx_write(client, PIXART_PAT9401_CPI_RES_X_HI_REG,
                      PIXART_PAT9401_CPI_RES_X_HI_DEFAULT_VAL);
	pat9xxx_write(client, PIXART_PAT9401_CONFIG_SLEEP_MODE_REG,
                      PIXART_PAT9401_CONFIG_SLEEP_MODE_VAL);
	pat9xxx_set_motioninterval(client, data->motion_interval);
	pat9xxx_set_motionthreshold(client, data->motion_threshold);

	data->crown_sensitivity   = sensitivity;
	data->current_sensitivity = sensitivity;
	data->wake_sensitivity    = REDUCED_SENSITIVITY(data->crown_sensitivity);

	data->sleep_state = SLEEP_STATE_2;
	data->sleep_freq  = SLEEP_STATE2_FREQ_64;

	pat9xxx_enable_writeprotect(client);
	return 0;
}

int pat9xxx_sensor_init(struct i2c_client *client)
{
	u8 id = 0;
	u8 product_id2;
	struct pixart_pat9xxx_data *data = i2c_get_clientdata(client);

	/*
	 * Read sensor_pid in address 0x00 to check if the
	 * serial link is valid, read value should be 0x31.
	 */
	pat9xxx_read(client, PIXART_PAT9126_PRODUCT_ID1_REG, &id);
	if (id != PIXART_PAT9126_SENSOR_ID) {
		pr_err("PID mismatch (id = 0x%x)", id);
		return (-1);
	}

	pat9xxx_read(client, PIXART_PAT9126_PRODUCT_ID2_REG, &product_id2);
	if (data->product_id2 != product_id2) {
		pr_err("SubPID not compatible (id = 0x%x)", product_id2);
		return (-1);
	}
	if (!is_pat9126(data) && !is_pat9401(data)) {
		pr_err("SubPID mismatch (id = 0x%x)", data->product_id2);
		return (-1);
	}

	if (is_pat9126(data)) {
		pr_info("PAT9126 detected");
		return pat9126_sensor_init(client);
	}
	else if (is_pat9401(data)) {
		pr_info("PAT9401 detected");
		return pat9401_sensor_init(client);
	}

	return (-1);
}

int pat9xxx_disable_mot(struct i2c_client *client, int16_t detect_freq)
{
	uint8_t tmp_1 = 0;
	uint8_t sensor_pid = 0;
        uint8_t sleep_reg, sleep_mode;
	struct pixart_pat9xxx_data *data = i2c_get_clientdata(client);

	pat9xxx_read(client, PIXART_PAT9126_PRODUCT_ID1_REG, &sensor_pid);
	if (sensor_pid != PIXART_PAT9126_SENSOR_ID) {
		return (-1);
	}

	if (is_pat9401(data)) {
		/* Set threshold for consistent scroll to wake */
		pat9xxx_set_motionthreshold(client, data->motion_threshold);
		switch (data->sleep_state) {
			case SLEEP_STATE_1:
				sleep_reg = PIXART_PAT9401_SLEEP1_MODE_FREQ_REG;
				sleep_mode = PIXART_PAT9401_FORCE_ENTER_SLEEP1_MODE;
				break;
			case SLEEP_STATE_2:
			default:
				sleep_reg = PIXART_PAT9401_SLEEP2_MODE_FREQ_REG;
				sleep_mode = PIXART_PAT9401_FORCE_ENTER_SLEEP2_MODE;
				break;
			case SLEEP_STATE_3:
				sleep_reg = PIXART_PAT9401_SLEEP3_MODE_FREQ_REG;
				sleep_mode = PIXART_PAT9401_FORCE_ENTER_SLEEP3_MODE;
				break;
		}

		pat9xxx_write_verified(client, sleep_reg,
			detect_freq);

		pat9xxx_write(client, PIXART_PAT9401_SLEEP_MODE_SELECT_REG,
			sleep_mode);
	}
	else if (is_pat9126(data)) {
		/* Set motion to open drain */
		pat9xxx_write_verified(client, PIXART_PAT9126_SENSOR_MODE_SELECT_REG,
			PIXART_PAT9126_SENSOR_DEFAULT_MODE2);
		pat9xxx_read(client, PIXART_PAT9126_SENSOR_MODE_SELECT_REG, &tmp_1);
		pr_debug("Open drain mode motion: 0x%2x. \n", tmp_1);

		pat9xxx_write_verified(client, PIXART_PAT9126_SLEEP2_MODE_FREQ_REG,
			detect_freq);

		pat9xxx_write(client, PIXART_PAT9126_SLEEP_MODE_SELECT_REG,
			PIXART_PAT9126_FORCE_ENTER_SLEEP2_MODE);
	}

	return 0;
}

int pat9xxx_enable_mot(struct i2c_client *client)
{
	uint8_t tmp_1 = 0;
	uint8_t sensor_pid = 0;
	struct pixart_pat9xxx_data *data = i2c_get_clientdata(client);

	pat9xxx_read(client, PIXART_PAT9126_PRODUCT_ID1_REG, &sensor_pid);
	if (sensor_pid != PIXART_PAT9126_SENSOR_ID) {
		return (-1);
	}

	pat9xxx_write_verified(client, PIXART_PAT9126_SENSOR_MODE_SELECT_REG,
			PIXART_PAT9126_SENSOR_SET_MODE2); // Set motion to drive mode
	pat9xxx_read(client, PIXART_PAT9126_SENSOR_MODE_SELECT_REG, &tmp_1);
	pr_debug("Drive mode motion: 0x%2x. \n", tmp_1);

	/*Read Register for Pulling Up Motion IRQ*/
	pat9xxx_read(client, PIXART_PAT9126_MOTION_STATUS_REG, &tmp_1);
	if (is_pat9401(data)) {
		pat9xxx_read(client, PIXART_PAT9401_DELTA_X_LO_REG, &tmp_1);
		pat9xxx_read(client, PIXART_PAT9401_DELTA_X_HI_REG, &tmp_1);
	} else if (is_pat9126(data)) {
		pat9xxx_read(client, PIXART_PAT9126_DELTA_X_LO_REG, &tmp_1);
		pat9xxx_read(client, PIXART_PAT9126_DELTA_Y_LO_REG, &tmp_1);
		pat9xxx_read(client, PIXART_PAT9126_DELTA_XY_HI_REG, &tmp_1);
	}

	delay(1);				  /* delay 1ms */

	if (is_pat9401(data)) {
		pat9xxx_write(client, PIXART_PAT9401_SLEEP_MODE_SELECT_REG,
			PIXART_PAT9401_WAKEUP_MODE);
		pat9xxx_read(client, PIXART_PAT9401_SLEEP_MODE_SELECT_REG, &tmp_1);
		/* Remove threshold, generate immediate event */
		pat9xxx_set_motionthreshold(client, 0);
	} else if (is_pat9126(data)) {
		pat9xxx_write_verified(client, PIXART_PAT9126_SLEEP_MODE_SELECT_REG,
			PIXART_PAT9126_WAKEUP_MODE);
		pat9xxx_read(client, PIXART_PAT9126_SLEEP_MODE_SELECT_REG, &tmp_1);
	}
	pr_debug("Enable sleep1 and disable sleep2 mode: 0x%2x. \n", tmp_1);
	return 0;
}

int pat9xxx_enable_mot_write_protected(struct i2c_client *client) {
	int result;

	pat9xxx_disable_writeprotect(client);
	result = pat9xxx_enable_mot(client);
	pat9xxx_enable_writeprotect(client);

	if (result) {
		pr_err("%s: failed to enable sensor\n", __func__);
	}

	return result;
}

int pat9xxx_disable_mot_write_protected(struct i2c_client *client) {
	int result;
	struct pixart_pat9xxx_data *data = i2c_get_clientdata(client);

	pat9xxx_disable_writeprotect(client);
	/* Slp_Freq bits 7:4 */
	result = pat9xxx_disable_mot(client, SLEEP_FREQ(data->sleep_freq));
	pat9xxx_enable_writeprotect(client);

	if (result) {
		pr_err("%s: failed to disable sensor\n", __func__);
	}

	return result;
}

static void pat9401_read_motion(struct i2c_client *client, int16_t *dx16, ktime_t *timestamp)
{
	uint8_t deltaX_l = 0, deltaX_h = 0;
	uint8_t motion = 0;
	uint16_t deltaX = 0;

	pat9xxx_read(client, PIXART_PAT9126_MOTION_STATUS_REG, &motion);
	pr_debug("Motion BIT: 0x%2x\n", motion);

	if (motion & PIXART_PAT9126_VALID_MOTION_DATA) {
		*timestamp = ktime_get();
		pat9xxx_read(client, PIXART_PAT9401_DELTA_X_LO_REG, &deltaX_l);
		pat9xxx_read(client, PIXART_PAT9401_DELTA_X_HI_REG, &deltaX_h);
		deltaX = (deltaX_h << 8) | deltaX_l;
		*dx16 = (int16_t)deltaX;
	}
}

/* Read motion */
void pat9xxx_read_motion(struct i2c_client *client, int16_t *dx16, int16_t *dy16, ktime_t *timestamp)
{
	/* Be sure to use unsigned values for the low bytes to
	   avoid unintended sign extension (b/234361169) */
	uint8_t deltaX_l = 0, deltaY_l = 0, deltaXY_h = 0;
	int16_t deltaX_h = 0, deltaY_h = 0;
	uint8_t motion = 0;
	struct pixart_pat9xxx_data *data = i2c_get_clientdata(client);

	/* In case of PAT9401, run different data collection. */
	if (is_pat9401(data)) {
		return pat9401_read_motion(client, dx16, timestamp);
	}

	pat9xxx_read(client, PIXART_PAT9126_MOTION_STATUS_REG, &motion);
	pr_debug("Motion BIT: 0x%2x\n", motion);

	if (motion & PIXART_PAT9126_VALID_MOTION_DATA) {
		*timestamp = ktime_get();
		pat9xxx_read(client, PIXART_PAT9126_DELTA_X_LO_REG, &deltaX_l);
		pat9xxx_read(client, PIXART_PAT9126_DELTA_Y_LO_REG, &deltaY_l);
		pat9xxx_read(client, PIXART_PAT9126_DELTA_XY_HI_REG, &deltaXY_h);

		deltaX_h = (deltaXY_h & 0xF0);
		deltaX_h <<= 4;
		/* 12-bit data convert to 16-bit */
		if (deltaX_h & 0x800)
			deltaX_h |= 0xf000;

		deltaY_h = (deltaXY_h & 0xF);
		deltaY_h <<= 8;
		/* 12-bit data convert to 16-bit */
		if (deltaY_h & 0x800)
			deltaY_h |= 0xf000;
	}

	*dx16 = deltaX_h | (int16_t) deltaX_l;
	*dy16 = deltaY_h | (int16_t) deltaY_l;
}

/* Clear all pending events */
static void pat9xxx_clear_motion(struct i2c_client *client)
{
	uint8_t motion = 0, motion_data = 0;
	struct pixart_pat9xxx_data *data = i2c_get_clientdata(client);

	pat9xxx_read(client, PIXART_PAT9126_MOTION_STATUS_REG, &motion);
	while (motion & PIXART_PAT9126_VALID_MOTION_DATA) {
		pr_info("Clearing motion\n");
		if (is_pat9401(data)) {
			pat9xxx_read(client, PIXART_PAT9401_DELTA_X_LO_REG, &motion_data);
			pat9xxx_read(client, PIXART_PAT9401_DELTA_X_HI_REG, &motion_data);
		} else if (is_pat9126(data)) {
			pat9xxx_read(client, PIXART_PAT9126_DELTA_X_LO_REG, &motion_data);
			pat9xxx_read(client, PIXART_PAT9126_DELTA_Y_LO_REG, &motion_data);
			pat9xxx_read(client, PIXART_PAT9126_DELTA_XY_HI_REG, &motion_data);
		}
		motion = 0;
		pat9xxx_read(client, PIXART_PAT9126_MOTION_STATUS_REG, &motion);
	}
}

static void pat9xxx_work_handler(struct work_struct *work)
{
	int8_t slop = 0;
	int16_t delta_x = 0, delta_y = 0;
	struct delayed_work *dw = to_delayed_work(work);
	struct pixart_pat9xxx_data *data = \
			container_of(dw, struct pixart_pat9xxx_data, polling_work);
	struct input_dev *ipdev = data->input;
	struct device *dev = &data->client->dev;
	ktime_t timestamp = ktime_set(0, 0);

	mutex_lock(&data->mtx);

	if (data->state == PAT9XXX_STATE_RESUMING) {
		pr_warn("%s: work handler run before resume complete\n", __func__);
		goto end_work_handler;
	}

	/* check if MOTION bit is set or not */
	pat9xxx_read_motion(data->client, &delta_x, &delta_y, &timestamp);
	dev_dbg(dev, "delta_x: %d, delta_y: %d\n", delta_x, delta_y);

	/* Inverse x depending upon the device orientation */
	delta_x = (data->inverse_x) ? -delta_x : delta_x;
	/* Inverse y depending upon the device orientation */
	delta_y = (data->inverse_y) ? -delta_y : delta_y;

	dev_dbg(dev, "delta_x = 0x%2x, delta_y = 0x%2x\n",
				delta_x, delta_y);

	if (delta_x != 0) {
		/* Send delta_x as REL_WHEEL for rotation */
		input_set_timestamp(ipdev, timestamp);
		slop = (delta_x < 0) ? -(data->slop) : data->slop;
		if (data->wake_sensitivity == data->current_sensitivity &&
			!data->wake_handled) {
			input_report_rel(ipdev, REL_WHEEL, slop);
			pr_info("WHEEL distance : %d\n", slop);
		}
		else {
			input_report_rel(ipdev, REL_WHEEL, delta_x);
			pr_info("WHEEL distance : %d\n", delta_x);
		}
		input_sync(ipdev);
	}

end_work_handler:
	if (!data->offload)
		enable_irq(data->client->irq);
	mutex_unlock(&data->mtx);
}

/* Offload enable - disable RSB interrupt on AP
 */
static void handle_offload_enable(struct device *dev)
{
	struct pixart_pat9xxx_data *data =
		(struct pixart_pat9xxx_data *) dev_get_drvdata(dev);
	struct i2c_client *client = data->client;

	mutex_lock(&data->mtx);
	pat9xxx_offld_data->offload = false;

	if (offload_enabled) {
		pr_info("Enabling RSB offload\n");

		disable_irq(data->client->irq);
		disable_irq_wake(data->client->irq);
		cancel_delayed_work_sync(&data->polling_work);
		cancel_delayed_work_sync(&data->resume_work);
		pat9xxx_clear_motion(client);

		/* Notify the MCU */
		rsb_comms_enable_offload(data->crown_sensitivity,
						data->wake_sensitivity,
						data->motion_threshold);

		pat9xxx_offld_data->offload = true;
	} else {
		pr_info("Ignoring offload enable request, feature configured as disabled!");
	}
	mutex_unlock(&data->mtx);
}

/* Offload disable - enable RSB interrupt on AP
 */
static void handle_offload_disable(struct device *dev)
{
	struct pixart_pat9xxx_data *data =
		(struct pixart_pat9xxx_data *) dev_get_drvdata(dev);
	struct i2c_client *client = data->client;

	mutex_lock(&data->mtx);
	if (offload_enabled) {
		pat9xxx_offld_data->offload = false;
		pr_info("Disabling RSB offload\n");

		enable_irq(data->client->irq);
		enable_irq_wake(data->client->irq);
		pat9xxx_clear_motion(client);
	} else {
		pr_info("Ignoring offload disable request, feature configured as disabled!");
	}
	mutex_unlock(&data->mtx);
}

static void on_comms_incoming_msg(const char *in_buffer, size_t len)
{
	int8_t slop = 0;
	int16_t delta_x = 0, delta_y = 0;
	struct input_dev *in_dev = pat9xxx_offld_data->input;
	struct device *dev = &pat9xxx_offld_data->client->dev;
	ktime_t ts = ktime_set(0, 0);
	const struct rsb_generic_cmd_msg *msg = (const struct rsb_generic_cmd_msg *) in_buffer;

	mutex_lock(&pat9xxx_offld_data->mtx);

	switch (msg->header.cmd) {
	case RSB_EVENT_COORDS:
	{
		uint32_t ack_id = 0;
		const struct rsb_event_coords_msg *coords_msg =
			(const struct rsb_event_coords_msg *) in_buffer;

		if (pat9xxx_offld_data->state == PAT9XXX_STATE_RESUMING) {
			pr_warn("%s: work handler run before resume complete\n", __func__);
			goto coords_exit;
		}

		ts = ktime_get();

		memcpy(&delta_x, &coords_msg->delta_x, sizeof(int16_t));
		delta_x = (pat9xxx_offld_data->inverse_x)
				? -delta_x
				: delta_x;
		memcpy(&delta_y, &coords_msg->delta_y, sizeof(int16_t));
		delta_y = (pat9xxx_offld_data->inverse_y)
				? -delta_y
				: delta_y;

		dev_dbg(dev, "delta_x = 0x%2x, delta_y = 0x%2x\n",
					delta_x, delta_y);

		if (delta_x != 0) {
			input_set_timestamp(in_dev, ts);
			slop = (delta_x < 0)
				? -(pat9xxx_offld_data->slop)
				: pat9xxx_offld_data->slop;
			if (!pat9xxx_offld_data->wake_handled) {
				input_report_rel(in_dev, REL_WHEEL, slop);
				pat9xxx_offld_data->wake_handled = true;
				pr_info("WHEEL distance: %d\n", slop);
			} else {
				input_report_rel(in_dev, REL_WHEEL, delta_x);
				pr_info("WHEEL distance: %d\n", delta_x);
			}
			input_sync(in_dev);
		}

coords_exit:
		// Notify ACK
		memcpy(&ack_id, &coords_msg->id, sizeof(uint32_t));
		rsb_comms_event_ack(ack_id);
	}
	break;

	case RSB_OFFLOAD_HANDSHAKE:
	{
		handle_offload_enable(dev);
	}
	break;
	}

	mutex_unlock(&pat9xxx_offld_data->mtx);
}

static irqreturn_t pat9xxx_irq(int irq, void *dev_data)
{
	struct pixart_pat9xxx_data *data = dev_data;
	bool result = false;

	disable_irq_nosync(irq);
	if (!delayed_work_pending(&data->polling_work)) {
		result = schedule_delayed_work(&data->polling_work,
						msecs_to_jiffies(data->delay_interval));
		if (result == false) {
			/* queue_work fail */
			pr_err("%s:queue_work fail.\n",__func__);
		}
	} else {
		/* work pending */
		pr_err("%s:queue_work pending.\n",__func__);
	}

	return IRQ_HANDLED;
}

static void pat9xxx_sensitivity_write(struct pixart_pat9xxx_data *data,
                                      u16 sensitivity) {
	struct i2c_client *client = data->client;

	pr_info("set sensitivity: %d\n", sensitivity);

	if (is_pat9401(data)) {
		uint8_t resx_lo = sensitivity & 0x00FF,
			resx_hi = (sensitivity >> 4) & PIXART_PAT9401_RESX_HI_MASK;

		pat9xxx_write(client, PIXART_PAT9401_CPI_RES_X_LO_REG,
                              resx_lo);
		pat9xxx_write(client, PIXART_PAT9401_CPI_RES_X_HI_REG,
                              resx_hi);
		data->current_sensitivity = sensitivity;
	} else if (is_pat9126(data)) {
		pat9xxx_write(client, PIXART_PAT9126_SET_CPI_RES_X_REG, sensitivity);
		data->current_sensitivity = sensitivity;
	}
}

static void pat9xxx_sensitivity_read(struct pixart_pat9xxx_data *data,
                                      u16 *sensitivity) {
	uint8_t  val1, val2;
	struct   i2c_client *client = data->client;

	if (is_pat9401(data)) {
		pat9xxx_read(client, PIXART_PAT9401_CPI_RES_X_LO_REG, &val1);
		pat9xxx_read(client, PIXART_PAT9401_CPI_RES_X_HI_REG, &val2);

		*sensitivity = (((uint16_t)(val2 & PIXART_PAT9401_RESX_HI_MASK)) << 4) | val1;
	}
	else if (is_pat9126(data)) {
		pat9xxx_read(client, PIXART_PAT9126_SET_CPI_RES_X_REG, &val1);
		*sensitivity = val1;
	}
}

static ssize_t pat9xxx_sensitivity_store(struct device *dev,
				    struct device_attribute *attr,
				    const char *buf,
				    size_t count) {
	struct pixart_pat9xxx_data *data =
		(struct pixart_pat9xxx_data *) dev_get_drvdata(dev);

	if (!kstrtou16(buf, 0, &data->crown_sensitivity)) {
		if (is_pat9401(data)) {
			data->wake_sensitivity =
				REDUCED_SENSITIVITY(data->crown_sensitivity);
		}
	}

	return count;
}

static ssize_t pat9xxx_sensitivity_show(struct device *dev,
					struct device_attribute *attr,
					char *buf) {
	int count = 0;

	struct pixart_pat9xxx_data *data =
		(struct pixart_pat9xxx_data *) dev_get_drvdata(dev);

	count += sprintf(buf, "0x%03x\n", data->crown_sensitivity);

	return count;
}

static ssize_t pat9xxx_current_sensitivity_show(struct device *dev,
					struct device_attribute *attr,
					char *buf) {
	int count = 0;
	uint16_t sensitivity;

	struct pixart_pat9xxx_data *data =
		(struct pixart_pat9xxx_data *) dev_get_drvdata(dev);

	pat9xxx_sensitivity_read(data, &sensitivity);
	count += sprintf(buf, "0x%03x\n", sensitivity);

	return count;
}

static ssize_t pat9xxx_wake_sensitivity_store(struct device *dev,
				    struct device_attribute *attr,
				    const char *buf,
				    size_t count) {
	struct pixart_pat9xxx_data *data =
		(struct pixart_pat9xxx_data *) dev_get_drvdata(dev);

	if (!kstrtou8(buf, 0, &data->wake_sensitivity)) {
		return count;
	}

	return count;
}

static ssize_t pat9xxx_wake_sensitivity_show(struct device *dev,
					struct device_attribute *attr,
					char *buf) {
	int count = 0;

	struct pixart_pat9xxx_data *data =
		(struct pixart_pat9xxx_data *) dev_get_drvdata(dev);

	count += sprintf(buf, "0x%02x\n", data->wake_sensitivity);

	return count;
}

static ssize_t pat9xxx_slop_store(struct device *dev,
				    struct device_attribute *attr,
				    const char *buf,
				    size_t count) {
	struct pixart_pat9xxx_data *data =
		(struct pixart_pat9xxx_data *) dev_get_drvdata(dev);

	if (!kstrtou8(buf, 0, &data->slop)) {
		return count;
	}

	return count;
}

static ssize_t pat9xxx_slop_show(struct device *dev,
					struct device_attribute *attr,
					char *buf) {
	int count = 0;

	struct pixart_pat9xxx_data *data =
		(struct pixart_pat9xxx_data *) dev_get_drvdata(dev);

	count += sprintf(buf, "0x%2x\n", data->slop);

	return count;
}

static ssize_t offload_show(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	int count = 0;

	struct pixart_pat9xxx_data *data =
		(struct pixart_pat9xxx_data *) dev_get_drvdata(dev);

	count += sprintf(buf, "0x%2x\n", data->offload);

	return count;
}

static ssize_t pat9xxx_motion_interval_store(struct device *dev,
				    struct device_attribute *attr,
				    const char *buf,
				    size_t count) {
	u8 interval;
	struct pixart_pat9xxx_data *data =
		(struct pixart_pat9xxx_data *) dev_get_drvdata(dev);
	struct i2c_client *client = data->client;

	if (!is_pat9401(data))
		return -ENODEV;

	if (!kstrtou8(buf, 0, &interval)) {
		if (0 == pat9xxx_disable_writeprotect(client)) {
			if (0 != pat9xxx_set_motioninterval(client, interval)) {
				count = 0;
			}
			pat9xxx_enable_writeprotect(client);
		}
	}

	return count;
}

static ssize_t pat9xxx_motion_interval_show(struct device *dev,
					struct device_attribute *attr,
					char *buf) {
	int count = 0;

	struct pixart_pat9xxx_data *data =
		(struct pixart_pat9xxx_data *) dev_get_drvdata(dev);

	if (is_pat9401(data)) {
		count += sprintf(buf, "0x%02x\n", data->motion_interval);
	}
	return count;
}

static ssize_t pat9xxx_motion_threshold_store(struct device *dev,
				    struct device_attribute *attr,
				    const char *buf,
				    size_t count) {
	u8 threshold;
	struct pixart_pat9xxx_data *data =
		(struct pixart_pat9xxx_data *) dev_get_drvdata(dev);

	if (!is_pat9401(data))
		return -ENODEV;

	if (!kstrtou8(buf, 0, &threshold)) {
		data->motion_threshold = threshold;
	}

	return count;
}

static ssize_t pat9xxx_motion_threshold_show(struct device *dev,
					struct device_attribute *attr,
					char *buf) {
	int count = 0;

	struct pixart_pat9xxx_data *data =
		(struct pixart_pat9xxx_data *) dev_get_drvdata(dev);

	if (is_pat9401(data)) {
		count += sprintf(buf, "0x%02x\n", data->current_threshold);
	}
	return count;
}

static ssize_t pat9xxx_delay_interval_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf,
				size_t count) {
	struct pixart_pat9xxx_data *data =
		(struct pixart_pat9xxx_data *) dev_get_drvdata(dev);

	if (!kstrtou8(buf, 0, &data->delay_interval))
		return count;
	return 0;
}

static ssize_t pat9xxx_delay_interval_show(struct device *dev,
					struct device_attribute *attr,
					char *buf) {
	int count = 0;

	struct pixart_pat9xxx_data *data =
		(struct pixart_pat9xxx_data *) dev_get_drvdata(dev);

	count += sprintf(buf, "0x%02x\n", data->delay_interval);
	return count;
}

static ssize_t pat9xxx_id_show(struct device *dev,
					struct device_attribute *attr,
					char *buf) {
	int count = 0;
	uint8_t maj, min;

	struct pixart_pat9xxx_data *data =
		(struct pixart_pat9xxx_data *) dev_get_drvdata(dev);
	struct i2c_client *client = data->client;

	pat9xxx_read(client, PIXART_PAT9126_PRODUCT_ID1_REG, &maj);
	pat9xxx_read(client, PIXART_PAT9126_PRODUCT_ID2_REG, &min);
	count += sprintf(buf, "0x%2x 0x%2x\n", maj, min);

	return count;
}

static ssize_t pat9xxx_sleep_freq_store(struct device *dev,
				    struct device_attribute *attr,
				    const char *buf,
				    size_t count) {
	struct pixart_pat9xxx_data *data =
		(struct pixart_pat9xxx_data *) dev_get_drvdata(dev);

	if (kstrtou8(buf, 0, &data->sleep_freq)) {
		return count;
	}

	dev_dbg(dev, "Setting sleep freq: %u\n", data->sleep_freq);

	return count;
}

static ssize_t pat9xxx_sleep_freq_show(struct device *dev,
					struct device_attribute *attr,
					char *buf) {
	int count = 0;

	struct pixart_pat9xxx_data *data =
		(struct pixart_pat9xxx_data *) dev_get_drvdata(dev);

	count += sprintf(buf, "0x%02x\n", data->sleep_freq);
	return count;
}

static ssize_t pat9xxx_sleep_state_store(struct device *dev,
				    struct device_attribute *attr,
				    const char *buf,
				    size_t count) {
	struct pixart_pat9xxx_data *data =
		(struct pixart_pat9xxx_data *) dev_get_drvdata(dev);

	if (kstrtou8(buf, 0, &data->sleep_state)) {
		return count;
	}

	dev_dbg(dev, "Setting sleep level: %u\n", data->sleep_state);

	return count;
}

static ssize_t pat9xxx_sleep_state_show(struct device *dev,
					struct device_attribute *attr,
					char *buf) {
	int count = 0;

	struct pixart_pat9xxx_data *data =
		(struct pixart_pat9xxx_data *) dev_get_drvdata(dev);

	count += sprintf(buf, "0x%02x\n", data->sleep_state);
	return count;
}

static int pat9xxx_pd_write(struct device *dev, u8 val) {
	u8 config;
	struct pixart_pat9xxx_data *data =
		(struct pixart_pat9xxx_data *) dev_get_drvdata(dev);
	struct i2c_client *client = data->client;

	pat9xxx_read(client, PIXART_PAT9126_CONFIG_REG, &config);
	config &= ~POWER_DOWN_ENABLE_BIT;

	if (val) {
		config |= POWER_DOWN_ENABLE_BIT;
	}

	pat9xxx_write(client, PIXART_PAT9126_CONFIG_REG, config);
	return 0;
}

static ssize_t pat9xxx_pd_store(struct device *dev,
				    struct device_attribute *attr,
				    const char *buf,
				    size_t count) {
	struct pixart_pat9xxx_data *data =
		(struct pixart_pat9xxx_data *) dev_get_drvdata(dev);
	u8 tmp;

	if (!kstrtou8(buf, 0, &tmp)) {
		dev_dbg(dev, "power down: %d\n", (tmp ? 1 : 0));

		if (!tmp) {
			pat9xxx_set_loads_active(data);
		}

		pat9xxx_pd_write(dev, tmp);

		if (tmp) {
			pat9xxx_set_loads_sleep(data);
		}
	} else {
		dev_warn(dev, "failed to parse sysfs arg: '%s'\n", buf);
	}

	return count;
}

static ssize_t pat9xxx_pd_show(struct device *dev,
					struct device_attribute *attr,
					char *buf) {
	int count = 0;
	uint8_t tmp;

	struct pixart_pat9xxx_data *data =
		(struct pixart_pat9xxx_data *) dev_get_drvdata(dev);
	struct i2c_client *client = data->client;

	pat9xxx_read(client, PIXART_PAT9126_CONFIG_REG, &tmp);
	tmp = ((POWER_DOWN_ENABLE_BIT & tmp) ? 1 : 0);
	count += sprintf(buf, "0x%x\n", tmp);

	return count;
}

static void pat9xxx_complete_resume(struct work_struct *work) {
    struct delayed_work *dw = to_delayed_work(work);
	struct pixart_pat9xxx_data *data =
			container_of(dw, struct pixart_pat9xxx_data, resume_work);
	int ret = 0;

	mutex_lock(&data->mtx);

	if (data->state != PAT9XXX_STATE_RESUMING) {
		if (data->state == PAT9XXX_STATE_ON) {
			pr_warn("%s: Already resumed\n", __func__);
		} else {
			// If suspend is called before resume is complete
			// resume will be aborted
			pr_info("%s: Resume aborted\n", __func__);
		}

		goto end_complete_resume;
	}

	if (!data->offload) {
		ret = pat9xxx_set_loads_active(data);
		if (ret)
			goto end_complete_resume;
		pat9xxx_enable_mot_write_protected(data->client);
	}

	data->state = PAT9XXX_STATE_ON;

end_complete_resume:
	mutex_unlock(&data->mtx);
}

module_param(offload_enabled, bool, 0644);
MODULE_PARM_DESC(offload_enabled, "Enable offloading to MCU");

static DEVICE_ATTR
	(crown_sensitivity, S_IRUGO | S_IWUSR | S_IWGRP,
	pat9xxx_sensitivity_show, pat9xxx_sensitivity_store);

static DEVICE_ATTR
	(current_sensitivity, S_IRUGO,
	pat9xxx_current_sensitivity_show, NULL);

static DEVICE_ATTR
	(wake_sensitivity, S_IRUGO | S_IWUSR | S_IWGRP,
	pat9xxx_wake_sensitivity_show, pat9xxx_wake_sensitivity_store);

static DEVICE_ATTR
	(id, S_IRUGO,
	pat9xxx_id_show, NULL);

static DEVICE_ATTR
	(sleep_freq, S_IRUGO | S_IWUSR | S_IWGRP,
	pat9xxx_sleep_freq_show, pat9xxx_sleep_freq_store);

static DEVICE_ATTR
	(sleep_state, S_IRUGO | S_IWUSR | S_IWGRP,
	pat9xxx_sleep_state_show, pat9xxx_sleep_state_store);

static DEVICE_ATTR
	(pd, S_IRUGO | S_IWUSR | S_IWGRP,
	pat9xxx_pd_show, pat9xxx_pd_store);

static DEVICE_ATTR
	(slop, S_IRUGO | S_IWUSR | S_IWGRP,
	pat9xxx_slop_show, pat9xxx_slop_store);

static DEVICE_ATTR
	(motion_interval, S_IRUGO | S_IWUSR | S_IWGRP,
	pat9xxx_motion_interval_show, pat9xxx_motion_interval_store);

static DEVICE_ATTR
	(motion_threshold, S_IRUGO | S_IWUSR | S_IWGRP,
	pat9xxx_motion_threshold_show, pat9xxx_motion_threshold_store);

static DEVICE_ATTR
	(delay_interval, S_IRUGO | S_IWUSR | S_IWGRP,
	pat9xxx_delay_interval_show, pat9xxx_delay_interval_store);

static DEVICE_ATTR_RO(offload);

static struct attribute *pat9xxx_attr_list[] = {
	&dev_attr_crown_sensitivity.attr,
	&dev_attr_current_sensitivity.attr,
	&dev_attr_wake_sensitivity.attr,
	&dev_attr_id.attr,
	&dev_attr_sleep_freq.attr,
	&dev_attr_sleep_state.attr,
	&dev_attr_pd.attr,
	&dev_attr_slop.attr,
	&dev_attr_motion_interval.attr,
	&dev_attr_motion_threshold.attr,
	&dev_attr_delay_interval.attr,
	&dev_attr_offload.attr,
	NULL,
};

static struct attribute_group pat9xxx_attr_grp = {
	.attrs = pat9xxx_attr_list,
};

static int pat9xxx_parse_dt(struct device *dev,
		struct pixart_pat9xxx_data *data)
{
	struct device_node *np = dev->of_node;
	char prop[MAX_PROP_SIZE];
	u32 temp_val;
	int ret, i;

	data->inverse_x = of_property_read_bool(np, "pixart,inverse-x");
	data->inverse_y = of_property_read_bool(np, "pixart,inverse-y");
	data->press_en = of_property_read_bool(np, "pixart,press-enabled");
	if (data->press_en) {
		ret = of_property_read_u32(np, "pixart,press-keycode",
						&temp_val);
		if (!ret) {
			data->press_keycode = temp_val;
		} else {
			dev_err(dev, "Unable to parse press-keycode\n");
			return ret;
		}
	}

	ret = of_property_read_u32(np, "pixart,motion-interval", &temp_val);
	if (!ret) {
		data->motion_interval = temp_val;
	} else if (is_pat9401(data)) {
		dev_err(dev, "Unable to parse motion-interval\n");
		return ret;
	}

	ret = of_property_read_u32(np, "pixart,motion-threshold", &temp_val);
	if (!ret) {
		data->motion_threshold = temp_val;
	} else if (is_pat9401(data)) {
		dev_err(dev, "Unable to parse motion-threshold\n");
		return ret;
	}

	ret = of_property_read_u32(np, "pixart,delay-interval", &temp_val);
	if (!ret) {
		data->delay_interval = temp_val;
	} else {
		dev_err(dev, "Unable to parse delay-interval\n");
		return ret;
	}

	ret = of_property_read_u32(np, "pixart,slop", &temp_val);
	if (!ret) {
		data->slop = temp_val;
	} else {
		dev_err(dev, "Unable to parse slop\n");
		return ret;
	}

	for (i = 0; i < PAT9XXX_NUM_SUPPLIES; i++) {
		data->supplies[i].supply = pat9xxx_supply_names[i];

		snprintf(prop, MAX_PROP_SIZE, "%s-active-load", pat9xxx_supply_names[i]);
		ret = of_property_read_u32(np, prop, &temp_val);
		if (!ret) {
			data->active_load[i] = temp_val;
		} else {
			dev_err(dev, "Unable to parse %s\n", prop);
			return ret;
		}

		snprintf(prop, MAX_PROP_SIZE, "%s-sleep-load", pat9xxx_supply_names[i]);
		ret = of_property_read_u32(np, prop, &temp_val);
		if (!ret) {
			data->sleep_load[i] = temp_val;
		} else {
			dev_err(dev, "Unable to parse %s\n", prop);
			return ret;
		}

		snprintf(prop, MAX_PROP_SIZE, "%s-voltage", pat9xxx_supply_names[i]);
		ret = of_property_read_u32(np, prop, &temp_val);
		if (!ret) {
			data->voltage[i] = temp_val;
		} else {
			dev_err(dev, "Unable to parse %s\n", prop);
			return ret;
		}
	}

	ret = devm_regulator_bulk_get(dev, ARRAY_SIZE(data->supplies), data->supplies);
	if (ret) {
		dev_err(dev, "Failed to get supplies, ret=%d\n", ret);
		return ret;
	}

	for (i = 0; i < PAT9XXX_NUM_SUPPLIES; i++)
		regulator_set_voltage(data->supplies[i].consumer, data->voltage[i], data->voltage[i]);

	return 0;
}

static int pat9xxx_register_panel_notifier(struct pixart_pat9xxx_data *data)
{
	struct device_node *np = data->client->dev.of_node;
	struct device_node *pnode;
	struct drm_panel *panel;
	void *cookie = NULL;
	int i, count, rc;

	count = of_count_phandle_with_args(np, "display-panels", NULL);
	if (count <= 0)
		return 0;

	for (i = 0; i < count; i++) {
		pnode = of_parse_phandle(np, "display-panels", i);
		if (!pnode)
			return -ENODEV;

		panel = of_drm_find_panel(pnode);
		of_node_put(pnode);
		if (!IS_ERR(panel)) {
			data->active_panel = panel;
			break;
		}
	}

	if (!data->active_panel) {
		rc = PTR_ERR(panel);
		if (rc != -EPROBE_DEFER)
			pr_err("failed to find active panel, rc=%d\n", rc);

		return rc;
	}

	cookie = panel_event_notifier_register(
			PANEL_EVENT_NOTIFICATION_PRIMARY,
			PANEL_EVENT_NOTIFIER_CLIENT_SECONDARY_TOUCH,
			data->active_panel,
			&pat9xxx_drm_panel_notifier_callback,
			data);
	if (IS_ERR(cookie)) {
		rc = PTR_ERR(cookie);
		pr_err("failed to register panel event notifier, rc=%d\n", rc);
		return rc;
	}

	pr_debug("register panel notifier successful\n");
	data->notifier_cookie = cookie;
	return 0;
}

static void pat9xxx_unregister_panel_notifier(struct pixart_pat9xxx_data *data)
{
	if (data->notifier_cookie) {
		panel_event_notifier_unregister(data->notifier_cookie);
		data->notifier_cookie = NULL;
	}
}

static int pixart_input_open(struct input_dev *dev) {
	struct pixart_pat9xxx_data *data =
		(struct pixart_pat9xxx_data *) input_get_drvdata(dev);

	mutex_lock(&data->mtx);

	if (data->display_mode == DISPLAY_UNBLANKED) {
		// Display On
		if (!data->offload) {
			pat9xxx_sensitivity_write(data, data->crown_sensitivity);
			pat9xxx_enable_mot_write_protected(data->client);
		}
		data->state = PAT9XXX_STATE_ON;
	} else {
		// Display Suspended
		if (!data->offload) {
			pat9xxx_sensitivity_write(data, data->wake_sensitivity);
			enable_irq_wake(data->client->irq);
			pat9xxx_disable_mot_write_protected(data->client);
		}
		data->state = PAT9XXX_STATE_SUSPEND;
		data->wake_handled = false;
	}

	if (data->offload) {
		// Notify MCU
		rsb_comms_msg(RSB_DRIVER_OPENED);
	}

	mutex_unlock(&data->mtx);

	return 0;
}

static void pixart_input_close(struct input_dev *dev) {
	struct pixart_pat9xxx_data *data =
		(struct pixart_pat9xxx_data *) input_get_drvdata(dev);

	pr_info("disabling wake on irq %d", data->client->irq);
	mutex_lock(&data->mtx);

	if (data->state == PAT9XXX_STATE_SUSPEND) {
		// Wake irq was enabled in pat9xxx_display_suspend so we need to disable the irq
		if (!data->offload)
			disable_irq_wake(data->client->irq);
	} else {
		// Wake irq wasn't enabled in pat9xxx_display_suspend nor was the device disabled
		// so we need to put the pat9xxx device into sleep2 mode
		if (!data->offload)
			pat9xxx_disable_mot_write_protected(data->client);
	}

	if (data->offload) {
		// Notify MCU
		rsb_comms_msg(RSB_DRIVER_CLOSED);
	}

	mutex_unlock(&data->mtx);
}

static void register_mcu_transport(struct work_struct *work)
{
	// Ensure mcu_transport is ready and registered
	if (!mcu_transport_init)
		mcu_transport_init = !rsb_comms_register_listener(on_comms_incoming_msg);

	// Ensure i2c_arbitration is ready and registered
	if (!i2c_arb_init)
		i2c_arb_init = !register_i2c_arbitration_event_notifier(NULL,
			pat9xxx_i2c_arbitration_notifier_callback);

	if (mcu_transport_init && i2c_arb_init) {
		pr_info("mcu transport and i2c arb is registered");
		return;
	}

	// Retry
	pr_info("reschedule mcu transport registration");
	schedule_delayed_work(to_delayed_work(work), REGISTER_MCU_TRANSPORT_DELAY_JIFFIES);
}

static int pat9xxx_i2c_probe(struct i2c_client *client)
{
	int ret = 0;
	struct pixart_pat9xxx_data *data;
	struct input_dev *input;
	struct device *dev = &client->dev;
	const struct of_device_id *match;

	struct device_node *dp = NULL;
	dp = client->dev.of_node;

	ret = i2c_check_functionality(client->adapter, I2C_FUNC_SMBUS_BYTE);
	if (ret < 0) {
		dev_err(dev, "I2C not supported\n");
		return -ENXIO;
	}

	pr_info("Loading PAT9xxx driver with offload capabilities - With Recovery enabled\n");

	data = devm_kzalloc(dev, sizeof(struct pixart_pat9xxx_data),
			GFP_KERNEL);
	if (!data)
		return -ENOMEM;
	match = of_match_device(dev->driver->of_match_table, dev);
	if (match)
		data->product_id2 = (uintptr_t)match->data;
	ret = pat9xxx_parse_dt(dev, data);
	if (ret) {
		dev_err(dev, "DT parsing failed, errno:%d\n", ret);
		return ret;
	}
	data->client = client;

	i2c_set_clientdata(client, data);

	ret = pinctrl_pm_select_default_state(dev);
	if (ret < 0) {
		dev_err(dev, "Could not set pin to active state %d\n", ret);
		return ret;
	}
	ret = pat9xxx_set_loads_active(data);
	if (ret < 0) {
		return ret;
	}

	ret = pat9xxx_enable_regulators(data);
	if (ret < 0) {
		goto err_regulators;
	}

	usleep_range(DELAY_BETWEEN_REG_US, DELAY_BETWEEN_REG_US + 1);

	/*
		* Initialize pixart sensor after some delay, when vdd
		* regulator is enabled
	*/
	if (pat9xxx_sensor_init(data->client) < 0) {
		ret = -ENODEV;
		dev_err(dev, "Failed to initialize sensor %d\n", ret);
		goto err_regulators;
	}

	mutex_init(&data->mtx);
	data->state = PAT9XXX_STATE_ON;
	data->display_mode = DISPLAY_UNBLANKED;
	data->wake_handled = false;
	data->offload = false;

	input = devm_input_allocate_device(dev);
	if (!input) {
		dev_err(dev, "Failed to alloc input device\n");
		ret = -ENOMEM;
		goto err_regulators;
	}
	input_set_drvdata(input, data);
	input_set_capability(input, EV_REL, REL_WHEEL);
	if (data->press_en)
		input_set_capability(input, EV_KEY, data->press_keycode);
	input->name = PAT9XXX_DEV_NAME;
	input->phys = PAT9XXX_DEV_PHYS;
	input->open = pixart_input_open;
	input->close = pixart_input_close;
	data->input = input;

	INIT_DELAYED_WORK(&data->polling_work, pat9xxx_work_handler);
	INIT_DELAYED_WORK(&data->resume_work, pat9xxx_complete_resume);

	ret = pat9xxx_register_panel_notifier(data);
	if (ret) {
		dev_err(dev, "register panel_notifier failed, errno:%d\n", ret);
		goto err_cancel_delayed_work;
	}

	if (offload_enabled) {
		INIT_DELAYED_WORK(&register_mcu_transport_work, register_mcu_transport);
		// Start to register the mcu transport and i2c arbiter
		schedule_delayed_work(&register_mcu_transport_work, 0);
	}

	pr_err("Probe Enable Irq. \n");
	ret = devm_request_threaded_irq(dev, client->irq, NULL, pat9xxx_irq,
			 IRQF_ONESHOT | IRQF_TRIGGER_LOW,
			"pixart_pat9xxx_irq", data);
	if (ret) {
		dev_err(dev, "Req irq %d failed, errno:%d\n", client->irq, ret);
		goto err_register_panel_notifier;
	}

	ret = sysfs_create_group(&client->dev.kobj, &pat9xxx_attr_grp);
	if (ret) {
		dev_err(dev, "Failed to create sysfs group, errno:%d\n", ret);
		goto err_request_threaded_irq;
	}

	/*
	 * Register input device
	 */
	ret = input_register_device(data->input);
	if (ret < 0) {
		dev_err(dev, "Failed to register input device\n");
		goto err_sysfs_create;
	}

	/*
	 * Register offload service listener
	 */
	pat9xxx_offld_data = data;
	return 0;

err_sysfs_create:
	sysfs_remove_group(&client->dev.kobj, &pat9xxx_attr_grp);
err_request_threaded_irq:
	disable_irq(client->irq);
err_register_panel_notifier:
	pat9xxx_unregister_panel_notifier(data);
err_cancel_delayed_work:
	cancel_delayed_work_sync(&data->polling_work);
	cancel_delayed_work_sync(&data->resume_work);
err_regulators:
	pat9xxx_disable_regulators(data);
	return ret;
}

static void pat9xxx_i2c_remove(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct pixart_pat9xxx_data *data = i2c_get_clientdata(client);

	dev_dbg(dev, "%s\n", __func__);

	sysfs_remove_group(&client->dev.kobj, &pat9xxx_attr_grp);

	if (!data->offload) {
		disable_irq(client->irq);
	} else {
		rsb_comms_unregister_listener();
		pat9xxx_offld_data = NULL;
	}

	if (data) {
		if (!data->offload)
			cancel_delayed_work_sync(&data->polling_work);
		cancel_delayed_work_sync(&data->resume_work);

		pat9xxx_unregister_panel_notifier(data);
		if (offload_enabled && i2c_arb_init) {
			unregister_i2c_arbitration_event_notifier(
				pat9xxx_i2c_arbitration_notifier_callback);
		}
		pat9xxx_disable_regulators(data);

		input_unregister_device(data->input);
	}

	mcu_transport_init = false;
	i2c_arb_init = false;
}

static void pat9xxx_drm_panel_notifier_callback(enum panel_event_notifier_tag tag,
		struct panel_event_notification *notification, void *data)
{
	struct pixart_pat9xxx_data *pat9xxx = data;

	if (!notification) {
		return;
	}

	switch (notification->notif_type) {
	case DRM_PANEL_EVENT_UNBLANK:
		if (!notification->notif_data.early_trigger) {
			pat9xxx_display_resume(&pat9xxx->client->dev);
			pat9xxx->display_mode = DISPLAY_UNBLANKED;
		}
		break;

	case DRM_PANEL_EVENT_BLANK:
	case DRM_PANEL_EVENT_BLANK_LP:
		if (notification->notif_data.early_trigger) {
			pat9xxx_display_suspend(&pat9xxx->client->dev);
			pat9xxx->display_mode = DISPLAY_BLANKED;
		}
		break;
	default:
		break;
	}

	return;
}

static int pat9xxx_i2c_arbitration_notifier_callback(void *client_data,
						     enum i2c_arbitration_event_type type)
{
	if (type == I2C_OWNER_AP) {
		// i2c controlled by AP
		pr_info("Got AP I2C take-over request");
		handle_offload_disable(&pat9xxx_offld_data->client->dev);
	} else if (type == I2C_OWNER_MCU) {
		// i2c controlled by MCU
		pr_info("Got MCU I2C take-over request");
		handle_offload_enable(&pat9xxx_offld_data->client->dev);
	}
	return 0;
}

static int pat9xxx_display_suspend(struct device *dev)
{
	struct pixart_pat9xxx_data *data =
		(struct pixart_pat9xxx_data *) dev_get_drvdata(dev);

	mutex_lock(&data->mtx);

	pr_debug("%s\n", __func__);

	if (data->input->users == 0) {
		if (data->offload)
			rsb_comms_msg(RSB_DISPLAY_SUSPEND);
		goto end_suspend;
	}

	if (data->state != PAT9XXX_STATE_ON) {
		if (data->state == PAT9XXX_STATE_SUSPEND) {
			pr_warn("%s: Redundant call to suspend\n", __func__);
		} else {
			// PAT9XXX_STATE_RESUMING
			pr_info("%s: Aborting resume\n", __func__);
		}

		goto end_suspend;
	}

	/* Set low sensitivity */
	if (!data->offload) {
		pat9xxx_sensitivity_write(data, data->wake_sensitivity);
		enable_irq_wake(data->client->irq);
		pat9xxx_disable_mot_write_protected(data->client);
		pat9xxx_set_loads_sleep(data);
	} else {
		// Notify MCU
		rsb_comms_msg(RSB_DISPLAY_SUSPEND);
	}

	data->wake_handled = false;

end_suspend:
	data->state = PAT9XXX_STATE_SUSPEND;
	mutex_unlock(&data->mtx);
	return 0;
}

static int pat9xxx_display_resume(struct device *dev)
{
	struct pixart_pat9xxx_data *data =
		(struct pixart_pat9xxx_data *) dev_get_drvdata(dev);

	pr_debug("%s\n", __func__);

	mutex_lock(&data->mtx);
	if (data->state != PAT9XXX_STATE_SUSPEND) {
		pr_warn("%s: Redundant call to resume\n", __func__);
		mutex_unlock(&data->mtx);
		return 0;
	}

	/* Skip if input device is closed */
	if (data->input->users == 0) {
		if (data->offload)
			rsb_comms_msg(RSB_DISPLAY_RESUME);
		mutex_unlock(&data->mtx);
		return 0;
	}

	/* Set regular sensitivity */
	if (!data->offload) {
		pat9xxx_sensitivity_write(data, data->crown_sensitivity);
		disable_irq_wake(data->client->irq);
	} else {
		// Notify MCU
		rsb_comms_msg(RSB_DISPLAY_RESUME);
	}

	data->wake_handled = true;
	data->state = PAT9XXX_STATE_RESUMING;
	mutex_unlock(&data->mtx);

	if (!schedule_delayed_work(&data->resume_work, msecs_to_jiffies(10))) {
		pr_err("%s: Failed to schedule delayed resume\n", __func__);

		// Note that we must release the mutex (as above) before
		// calling this function.
		pat9xxx_complete_resume(&data->resume_work.work);
	}

	return 0;
}

static int pat9xxx_pm_suspend_late(struct device *dev)
{
	struct pixart_pat9xxx_data *data =
		(struct pixart_pat9xxx_data *) dev_get_drvdata(dev);

	mutex_lock(&data->mtx);

	return 0;
}

static int pat9xxx_pm_resume_early(struct device *dev)
{
	struct pixart_pat9xxx_data *data =
		(struct pixart_pat9xxx_data *) dev_get_drvdata(dev);

	mutex_unlock(&data->mtx);

	return 0;
}

static const struct i2c_device_id pat9xxx_device_id[] = {
	{PAT9XXX_DEV_NAME, 0},
	{}
};
MODULE_DEVICE_TABLE(i2c, pat9xxx_device_id);

static const struct dev_pm_ops pat9xxx_pm_ops = {
	.suspend_late = pat9xxx_pm_suspend_late,
	.resume_early  = pat9xxx_pm_resume_early
};

static const struct of_device_id pixart_pat9xxx_match_table[] = {
	{ .compatible = "pixart,pat9126", .data = (void *)PIXART_PAT9126_SENSOR_ID2 },
	{ .compatible = "pixart,pat9401", .data = (void *)PIXART_PAT9401_SENSOR_ID2 },
	{ },
};

static struct i2c_driver pat9xxx_i2c_driver = {
	.driver = {
		   .name = PAT9XXX_DEV_NAME,
		   .owner = THIS_MODULE,
		   .pm = &pat9xxx_pm_ops,
		   .of_match_table = pixart_pat9xxx_match_table,
		   },
	.probe = pat9xxx_i2c_probe,
	.remove = pat9xxx_i2c_remove,
	.id_table = pat9xxx_device_id,
};
module_i2c_driver(pat9xxx_i2c_driver);

MODULE_AUTHOR("pixart");
MODULE_DESCRIPTION("pixart pat9xxx driver");
MODULE_LICENSE("GPL");
MODULE_SOFTDEP("pre: mcu_transport_autobahn i2c_arbitration_interface");
