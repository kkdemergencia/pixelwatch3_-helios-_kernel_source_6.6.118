// SPDX-License-Identifier: GPL-2.0
/*
 * I2C Arbitration module for input offload
 *
 * Copyright 2026 Google LLC.
 */
#include <linux/completion.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of_gpio.h>
#include <linux/platform_device.h>
#include <linux/workqueue.h>
#include <linux/soc/qcom/panel_event_notifier.h>
#include <linux/version.h>

#include "i2c_arbitration_device_tree.h"
#include "i2c_arbitration_log.h"
#include "i2c_arbitration_mcu_interface.h"
#include "i2c_arbitration_module.h"
#include "i2c_arbitration_ops.h"

/* TODO: This number should be adjusted after getting the telemetry data */
#define HEALTH_CHECK_TIMEOUT_MS		 50
/* TODO: This number should be defined after getting the telemetry data */
#define MCU_YIELD_I2C_CONTROL_TIMEOUT_MS 200

struct i2c_arbitration_event_notifier {
	int (*callback)(void *client_data, enum i2c_arbitration_event_type);
	void *client_data;
	struct list_head list;
};

enum i2c_arbitration_module_state {
	MODULE_PROBING,
	MODULE_PROBED,
	MODULE_REMOVED,
};

enum display_state {
	ON,
	AOD,
	OFF,
};

struct i2c_arbitration_host {
	struct i2c_arbitration_event_notifier notifier_head;
	/* This lock is protecting */
	struct mutex lock;
	atomic_t control_on_ap;
	enum i2c_arbitration_module_state state;
	/* End of protecting */
	uint32_t seq_num;
	struct mutex ack_lock; // Protects seq_num + completion logic
	struct completion mcu_ack_completion;
	struct work_struct take_control_back_ap_work;
	struct mcu_i2c_arbitration_callbacks *mcu_callbacks;
	struct work_struct yield_control_to_mcu_work;
	struct i2c_arbitration_dt_data dt_data;
	void *panel_notifier_cookie;
	atomic_t display_state;
	// TODO(b/479654798): Remove this flag after unified the services in MCU
	bool mcu_arb_service_ready;
};

static struct i2c_arbitration_host arbitration_host = {
	.lock = __MUTEX_INITIALIZER(arbitration_host.lock),
	.state = MODULE_REMOVED,
};

static void request_take_control_back_ap_handler(struct work_struct *work);
static void request_yield_control_to_mcu_handler(struct work_struct *work);
static void request_take_control_back_ap(void);
static void request_yield_control_to_mcu(void);
static int register_arbitration_event_notifier(void *client_data,
					       int (*callback)(void *client_data,
							       enum i2c_arbitration_event_type));
static void unregister_arbitration_event_notifier(
	int (*callback)(void *client_data, enum i2c_arbitration_event_type type));

static bool recovery_enabled;
/* Only support read access to turn it on in bootloader stage */
module_param(recovery_enabled, bool, 0400);

static const struct i2c_arbitration_ops arbitration_ops = {
	.take_control_back_ap = request_take_control_back_ap,
	.yield_control_to_mcu = request_yield_control_to_mcu,
	.register_event_notifier = register_arbitration_event_notifier,
	.unregister_event_notifier = unregister_arbitration_event_notifier,
};
/* Must hold arbitration_host.lock before calling this function */
static int notify_i2c_arbitration_decision_locked(enum i2c_arbitration_event_type type)
{
	struct i2c_arbitration_event_notifier *notifier;
	int i32_ret = 0;

	list_for_each_entry(notifier, &arbitration_host.notifier_head.list, list) {
		i32_ret = notifier->callback(notifier->client_data, type);
		if (i32_ret) {
			LOGE("Received error : %d\n", i32_ret);
			return i32_ret;
		}
	}
	return i32_ret;
}

static void handle_ack_from_mcu(uint32_t seq_num)
{
	mutex_lock(&arbitration_host.ack_lock);
	if (seq_num != arbitration_host.seq_num)
		LOGW("Received an outdated ack seq_num = %u\n", seq_num);
	else
		complete(&arbitration_host.mcu_ack_completion);
	mutex_unlock(&arbitration_host.ack_lock);
}

static void request_take_control_back_ap_handler(struct work_struct *work)
{
	long timeout;
	int i32_ret;

	mutex_lock(&arbitration_host.lock);
	if (arbitration_host.state == MODULE_REMOVED) {
		LOGE("Arbitration module was unloaded\n");
		goto exit;
	}

	/* 1. Receive the request from rsb or touch drivers to take control i2c bus
	 * Possible reasons:
	 *         a. Touch INT2 triggered
	 *         b. RSB button pressing
	 */

	if (atomic_read(&arbitration_host.control_on_ap)) {
		LOGI("I2C control is still on AP\n");
		goto exit;
	}

	LOGI("%s: starts to take i2c bus control back to AP\n", __func__);
	// 2. Communicate with MCU to wait for the MCU giving up i2c bus
	mutex_lock(&arbitration_host.ack_lock);
	arbitration_host.seq_num++;
	reinit_completion(&arbitration_host.mcu_ack_completion);
	mutex_unlock(&arbitration_host.ack_lock);
	i32_ret = mcu_send_i2c_control(I2C_OWNER_AP, arbitration_host.seq_num);
	if (i32_ret) {
		LOGW("%s: failed to send the message to take i2c control", __func__);
		// We should keep going to the following process to use out-of-band signaling
	}

	// 3. Wait MCU ACK or timeout
	timeout = wait_for_completion_interruptible_timeout(
		&arbitration_host.mcu_ack_completion,
		msecs_to_jiffies(MCU_YIELD_I2C_CONTROL_TIMEOUT_MS));
	if (timeout <= 0) {
		LOGE("%s: failed to take control timeout = %ld", __func__, timeout);
		// TODO: I2C Arbitration Phase 2:
		// trigger out-of-band signal and have a timer to wait for the ACK
	}

	// 4. Notify each module to start i2c operations
	atomic_set(&arbitration_host.control_on_ap, 1);
	notify_i2c_arbitration_decision_locked(I2C_OWNER_AP);

exit:
	mutex_unlock(&arbitration_host.lock);
}

static void request_take_control_back_ap(void)
{
	if (!recovery_enabled) {
		LOGW("Recovery of switching the control of i2c bus back to AP is not enabled");
		return;
	}
	if (!arbitration_host.mcu_arb_service_ready) {
		LOGW("%s: MCU arb service is not ready\n", __func__);
		return;
	}
	schedule_work(&arbitration_host.take_control_back_ap_work);
}

static int register_arbitration_event_notifier(void *client_data,
					       int (*callback)(void *client_data,
							       enum i2c_arbitration_event_type))
{
	struct i2c_arbitration_event_notifier *notifier;
	int i32_ret = 0;

	mutex_lock(&arbitration_host.lock);
	if (arbitration_host.state == MODULE_REMOVED) {
		LOGE("Arbitration module was unloaded\n");
		i32_ret = -EPROBE_DEFER;
		goto exit_error;
	}
	LOGI("%s: register the i2c arbitration callback\n", __func__);

	notifier = kzalloc(sizeof(struct i2c_arbitration_event_notifier), GFP_KERNEL);
	if (unlikely(!notifier)) {
		i32_ret = -ENOMEM;
		LOGE("Failed to allocate memory for notifier\n");
		goto exit_error;
	}
	notifier->callback = callback;
	notifier->client_data = client_data;
	list_add_tail(&notifier->list, &arbitration_host.notifier_head.list);
exit_error:
	mutex_unlock(&arbitration_host.lock);
	return i32_ret;
}

static void unregister_arbitration_event_notifier(
	int (*callback)(void *client_data, enum i2c_arbitration_event_type type))
{
	struct i2c_arbitration_event_notifier *notifier;
	struct i2c_arbitration_event_notifier *next_notifier;

	mutex_lock(&arbitration_host.lock);

	if (arbitration_host.state == MODULE_REMOVED) {
		LOGE("Arbitration module was unloaded\n");
		goto exit;
	}

	LOGI("%s: unregister the i2c arbitration callback\n", __func__);

	list_for_each_entry_safe(notifier, next_notifier, &arbitration_host.notifier_head.list,
				 list) {
		if (notifier->callback == callback) {
			list_del(&notifier->list);
			kfree(notifier);
		}
	}
exit:
	mutex_unlock(&arbitration_host.lock);
}

static void unregister_all_arbitration_event_notifier(void)
{
	struct i2c_arbitration_event_notifier *notifier;
	struct i2c_arbitration_event_notifier *next_notifier;

	LOGI("%s: unregister all the i2c arbitration callbacks\n", __func__);

	mutex_lock(&arbitration_host.lock);
	list_for_each_entry_safe(notifier, next_notifier, &arbitration_host.notifier_head.list,
				 list) {
		list_del(&notifier->list);
		kfree(notifier);
	}
	mutex_unlock(&arbitration_host.lock);
}

static inline bool display_is_active(void)
{
	return atomic_read(&arbitration_host.display_state) == ON;
}

static void request_yield_control_to_mcu_handler(struct work_struct *work)
{
	int i32_ret;
	long timeout;

	mutex_lock(&arbitration_host.lock);

	if (arbitration_host.state == MODULE_REMOVED) {
		LOGE("Arbitration module was unloaded\n");
		goto exit;
	}

	if (!atomic_read(&arbitration_host.control_on_ap)) {
		LOGI("I2C bus control is still on MCU, send the msg to MCU\n");
		goto notify_MCU_start_control;
	}
	// If display is NBM and control access is on AP, give up the request of yielding control
	// to improve the user experience not to aware of any laggy behavior.
	if (display_is_active()) {
		LOGI("Give up the yield control request because display is ON\n");
		goto exit;
	}

	LOGI("%s: starts to yield i2c bus control to MCU\n", __func__);
	/* 2. Healthy ping MCU, should get the ACK back from MCU within a specific time window
	 * we should check the sequence number to know if we get the correct ACK back
	 */
	mutex_lock(&arbitration_host.ack_lock);
	arbitration_host.seq_num++;
	reinit_completion(&arbitration_host.mcu_ack_completion);
	mutex_unlock(&arbitration_host.ack_lock);
	i32_ret = mcu_send_healthy_check(arbitration_host.seq_num);
	if (i32_ret) {
		LOGE("%s: Failed to send the health check message to MCU\n", __func__);
		// MCU is not healthy, drop the switch this time
		goto exit;
	}
	timeout = wait_for_completion_interruptible_timeout(
		&arbitration_host.mcu_ack_completion, msecs_to_jiffies(HEALTH_CHECK_TIMEOUT_MS));
	if (timeout <= 0) {
		LOGE("%s: MCU unhealthy timeout ACK = %ld, cancel this switching", __func__,
		     timeout);
		goto exit;
	}

	// 3. Notify rsb/touch drivers to disable interrupt and yield i2c control
	i32_ret = notify_i2c_arbitration_decision_locked(I2C_OWNER_MCU);
	if (i32_ret) {
		LOGE("%s: failed to yield control\n", __func__);
		// failed to yield i2c control, notify each modules again to get i2c control
		notify_i2c_arbitration_decision_locked(I2C_OWNER_AP);
		goto exit;
	}
	// Rsb and touch drivers already give up the i2c control on AP
	atomic_set(&arbitration_host.control_on_ap, 0);

notify_MCU_start_control:
	// 5. Notify MCU can start to do i2c transaction
	mutex_lock(&arbitration_host.ack_lock);
	arbitration_host.seq_num++;
	reinit_completion(&arbitration_host.mcu_ack_completion);
	mutex_unlock(&arbitration_host.ack_lock);
	i32_ret = mcu_send_i2c_control(I2C_OWNER_MCU, arbitration_host.seq_num);
	if (i32_ret) {
		LOGE("%s: Failed to send the message to yield i2c control to MCU\n", __func__);
		/* If recovery_enabled is false, then we should take the control back to AP
		 * if we failed to send the yield message to MCU
		 */
		schedule_work(&arbitration_host.take_control_back_ap_work);
		goto exit;
	}
exit:
	mutex_unlock(&arbitration_host.lock);
}

void request_yield_control_to_mcu(void)
{
	if (!arbitration_host.mcu_arb_service_ready) {
		LOGW("%s: MCU arb service is not ready\n", __func__);
		return;
	}
	schedule_work(&arbitration_host.yield_control_to_mcu_work);
}

static void i2c_arbitration_panel_notifier_callback(enum panel_event_notifier_tag tag,
						    struct panel_event_notification *notification,
						    void *client_data)
{
	if (!notification) {
		LOGE("Invalid notification\n");
		return;
	}

	if (client_data) {
		LOGE("Invalid client data\n");
		return;
	}
	LOGI("Panel notification type:%d, early_trigger:%d\n", notification->notif_type,
	     notification->notif_data.early_trigger);

	switch (notification->notif_type) {
	case DRM_PANEL_EVENT_UNBLANK:
		if (notification->notif_data.early_trigger) {
			LOGD("panel on notification pre commit\n");
		} else {
			atomic_set(&arbitration_host.display_state, ON);
			LOGD("panel on notification post commit\n");
		}
		break;
	case DRM_PANEL_EVENT_BLANK:
		if (notification->notif_data.early_trigger) {
			LOGD("panel off notification pre commit\n");
		} else {
			atomic_set(&arbitration_host.display_state, OFF);
			LOGI("panel off notification post commit\n");
			/* If the i2c bus control is on AP, then yield the i2c bus control to MCU
			 * Use atomic_read to check the i2c bus control status without lock to avoid
			 * blocking display driver too much time.
			 */
			if (atomic_read(&arbitration_host.control_on_ap))
				request_yield_control_to_mcu();
		}
		break;

	case DRM_PANEL_EVENT_BLANK_LP:
		if (notification->notif_data.early_trigger) {
			LOGD("panel aod notification pre commit\n");
		} else {
			atomic_set(&arbitration_host.display_state, AOD);
			LOGI("panel aod notification post commit\n");
			/* If the i2c bus control is on AP, then yield the i2c bus control to MCU
			 * Use atomic_read to check the i2c bus control status without lock to avoid
			 * blocking display driver too much time.
			 */
			if (atomic_read(&arbitration_host.control_on_ap))
				request_yield_control_to_mcu();
		}
		break;
	case DRM_PANEL_EVENT_FPS_CHANGE:
		LOGD("Received fps change old fps:%d new fps:%d\n",
		     notification->notif_data.old_fps, notification->notif_data.new_fps);
		break;
	default:
		LOGW("unsupported notification received :%d\n", notification->notif_type);
		break;
	}
}

static void handle_mcu_try_get_control(void)
{
	LOGI("MCU is asking for the control\n");
	/* Get the MCU message of trying to get i2c bus control, it's from MCU booting up
	 * TODO(b/479654798): Remove this function after unified the services in MCU
	 */
	arbitration_host.mcu_arb_service_ready = true;
	request_yield_control_to_mcu();
}

static int i2c_arbitration_probe(struct platform_device *pdev)
{
	int rc = 0;

	mutex_lock(&arbitration_host.lock);
	LOGI("Doing probing\n");

	rc = i2c_arbitration_register_mcu_listener();
	if (rc) {
		LOGI("DEFER probe\n");
		mutex_unlock(&arbitration_host.lock);
		return -EPROBE_DEFER;
	}

	i2c_arbitration_register_ops(&arbitration_ops);

	rc = i2c_arbitration_parse_dt(pdev, &arbitration_host.dt_data);
	if (rc) {
		LOGE("Failed to parse device tree, rc = %d\n", rc);
		goto error_parse_dt;
	}
	INIT_LIST_HEAD(&arbitration_host.notifier_head.list);

	/* Default i2c control is on AP, must align with MCU */
	atomic_set(&arbitration_host.control_on_ap, 1);
	arbitration_host.seq_num = 0;
	arbitration_host.mcu_arb_service_ready = false;

	init_completion(&arbitration_host.mcu_ack_completion);

	INIT_WORK(&arbitration_host.take_control_back_ap_work,
		  request_take_control_back_ap_handler);
	INIT_WORK(&arbitration_host.yield_control_to_mcu_work,
		  request_yield_control_to_mcu_handler);

	mutex_init(&arbitration_host.ack_lock);

	arbitration_host.mcu_callbacks =
		devm_kzalloc(&pdev->dev, sizeof(struct mcu_i2c_arbitration_callbacks), GFP_KERNEL);
	if (!arbitration_host.mcu_callbacks) {
		LOGE("Failed to allocate memory for mcu callbacks\n");
		rc = -ENOMEM;
		goto exit_mcu_callbacks_alloc_error;
	}
	arbitration_host.mcu_callbacks->handle_ack_from_mcu = handle_ack_from_mcu;
	arbitration_host.mcu_callbacks->handle_mcu_try_get_control = handle_mcu_try_get_control;
	i2c_arbitration_register_mcu_listener_callbacks(arbitration_host.mcu_callbacks);

#if defined(CONFIG_DRM_PANEL)
#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 10, 0)
#error "Registration with drm_panel_notifier_register not implemented"
#else
#if IS_ENABLED(CONFIG_QCOM_PANEL_EVENT_NOTIFIER)
	arbitration_host.panel_notifier_cookie = panel_event_notifier_register(
		PANEL_EVENT_NOTIFICATION_PRIMARY, PANEL_EVENT_NOTIFIER_CLIENT_I2C_ARBITRATION,
		arbitration_host.dt_data.panel, &i2c_arbitration_panel_notifier_callback, NULL);
	if (IS_ERR(arbitration_host.panel_notifier_cookie)) {
		rc = PTR_ERR(arbitration_host.panel_notifier_cookie);
		LOGE("Failed to register panel events\n");
		goto error_register_panel_notifier;
	}
#endif
#endif
#elif defined(_MSM_DRM_NOTIFY_H_)
#error "Registration with msm_drm_register_client not implemented"
#elif defined(CONFIG_FB)
#error "Registration with fb_register_client not implemented"
#elif defined(CONFIG_HAS_EARLYSUSPEND)
#error "Registration with register_early_suspend not implemented"
#else
#error "Unknown display panel notification registration method"
#endif

	arbitration_host.state = MODULE_PROBED;

	goto exit_normal;
error_register_panel_notifier:
	i2c_arbitration_unregister_mcu_listener();
exit_mcu_callbacks_alloc_error:
error_parse_dt:
exit_normal:
	mutex_unlock(&arbitration_host.lock);
	return rc;
}

static void i2c_arbitration_remove(struct platform_device *pdev)
{
	mutex_lock(&arbitration_host.lock);
	LOGI("Removing module\n");
	i2c_arbitration_unregister_ops();

#if IS_ENABLED(CONFIG_QCOM_PANEL_EVENT_NOTIFIER)
	if (!IS_ERR_OR_NULL(arbitration_host.panel_notifier_cookie))
		panel_event_notifier_unregister(arbitration_host.panel_notifier_cookie);
#endif

	i2c_arbitration_unregister_mcu_listener();

	unregister_all_arbitration_event_notifier();

	arbitration_host.state = MODULE_REMOVED;
	mutex_unlock(&arbitration_host.lock);
}

static const struct of_device_id i2c_arbitration_match_table[] = {
	{
		.compatible = "google,i2c-arbitration",
	},
	{},
};
MODULE_DEVICE_TABLE(of, i2c_arbitration_match_table);

static struct platform_driver i2c_arbitration_driver = {
	.probe  = i2c_arbitration_probe,
#if (LINUX_VERSION_CODE < KERNEL_VERSION(6, 10, 0))
	.remove_new = i2c_arbitration_remove,
#else
	.remove = i2c_arbitration_remove,
#endif
	.driver = {
		.name = "i2c_arbitration",
		.owner = THIS_MODULE,
		.of_match_table = i2c_arbitration_match_table,
	},
};

module_platform_driver(i2c_arbitration_driver);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Google Pixel Wear Touch Team");
MODULE_DESCRIPTION("I2C Arbitration module to ensure only one master can access the i2c bus");
MODULE_SOFTDEP("pre: mcu_transport_autobahn");
