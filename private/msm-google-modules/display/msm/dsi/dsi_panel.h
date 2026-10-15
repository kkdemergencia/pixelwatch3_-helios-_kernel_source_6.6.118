/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2022-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 * Copyright (c) 2016-2021, The Linux Foundation. All rights reserved.
 */

#ifndef _DSI_PANEL_H_
#define _DSI_PANEL_H_

#include <linux/of_device.h>
#include <linux/types.h>
#include <linux/bitops.h>
#include <linux/errno.h>
#include <linux/backlight.h>
#include <drm/drm_panel.h>
#include <drm/msm_drm.h>
#include <drm/msm_drm_pp.h>

#include "dsi_defs.h"
#include "dsi_ctrl_hw.h"
#include "dsi_clk.h"
#include "dsi_pwr.h"
#include "dsi_parser.h"
#include "msm_drv.h"

#define MAX_BL_LEVEL 4096
#define MAX_BL_SCALE_LEVEL 1024
#define MAX_SV_BL_SCALE_LEVEL 65535
#define SV_BL_SCALE_CAP (MAX_SV_BL_SCALE_LEVEL * 4)
#define DSI_CMD_PPS_SIZE 135

#define DSI_CMD_PPS_HDR_SIZE 7
#define DSI_MODE_MAX 32

/*
 * Defining custom dsi msg flag.
 * Using upper byte of flag field for custom DSI flags.
 * Lower byte flags specified in drm_mipi_dsi.h.
 */
#define MIPI_DSI_MSG_ASYNC_OVERRIDE BIT(4)
#define MIPI_DSI_MSG_CMD_DMA_SCHED BIT(5)
#define MIPI_DSI_MSG_BATCH_COMMAND BIT(6)
#define MIPI_DSI_MSG_UNICAST_COMMAND BIT(7)

enum dsi_panel_rotation {
	DSI_PANEL_ROTATE_NONE = 0,
	DSI_PANEL_ROTATE_HV_FLIP,
	DSI_PANEL_ROTATE_H_FLIP,
	DSI_PANEL_ROTATE_V_FLIP
};

enum dsi_backlight_type {
	DSI_BACKLIGHT_PWM = 0,
	DSI_BACKLIGHT_WLED,
	DSI_BACKLIGHT_DCS,
	DSI_BACKLIGHT_EXTERNAL,
	DSI_BACKLIGHT_UNKNOWN,
	DSI_BACKLIGHT_MAX,
};

enum bl_update_flag {
	BL_UPDATE_DELAY_UNTIL_FIRST_FRAME,
	BL_UPDATE_NONE,
};

enum {
	MODE_GPIO_NOT_VALID = 0,
	MODE_SEL_DUAL_PORT,
	MODE_SEL_SINGLE_PORT,
	MODE_GPIO_HIGH,
	MODE_GPIO_LOW,
};

enum dsi_dms_mode {
	DSI_DMS_MODE_DISABLED = 0,
	DSI_DMS_MODE_RES_SWITCH_IMMEDIATE,
};

enum dsi_panel_physical_type {
	DSI_DISPLAY_PANEL_TYPE_LCD = 0,
	DSI_DISPLAY_PANEL_TYPE_OLED,
	DSI_DISPLAY_PANEL_TYPE_MAX,
};

struct dsi_dfps_capabilities {
	enum dsi_dfps_type type;
	u32 min_refresh_rate;
	u32 max_refresh_rate;
	u32 *dfps_list;
	u32 dfps_list_len;
	bool dfps_support;
};

struct dsi_qsync_capabilities {
	bool qsync_support;
	u32 qsync_min_fps;
	u32 *qsync_min_fps_list;
	int qsync_min_fps_list_len;
	bool hwfence_sw_override_always;
};

struct dsi_avr_capabilities {
	u32 avr_step_fps;
	u32 *avr_step_fps_list;
	u32 avr_step_fps_list_len;
};

struct dsi_esync_capabilities {
	bool esync_support;
	u32 milli_skew;
	u32 hsync_milli_pulse_width;
	u32 emsync_milli_pulse_width;
	u32 emsync_fps;
};

struct dsi_dyn_clk_caps {
	bool dyn_clk_support;
	enum dsi_dyn_clk_feature_type type;
	bool maintain_const_fps;
};

struct dsi_pinctrl_info {
	struct pinctrl *pinctrl;
	struct pinctrl_state *active;
	struct pinctrl_state *active_with_esync;
	struct pinctrl_state *suspend;
	struct pinctrl_state *pwm_pin;
};

struct dsi_panel_phy_props {
	u32 panel_width_mm;
	u32 panel_height_mm;
	enum dsi_panel_rotation rotation;
};

struct dsi_bl_dyn_ramp {
	bool enable;
	struct dsi_panel_cmd_set pre_cmd;
	struct dsi_panel_cmd_set fixed_time_cmd;
	struct dsi_panel_cmd_set fixed_slope_cmd;
	struct dsi_panel_cmd_set post_cmd;
	u32 ramp_time_reg;
	u32 ramp_up_reg;
	u32 ramp_down_reg;
	int ramp_time_ms;
};

struct dsi_bl_fake_aod_config {
	u32 transition_point;
	u32 nbm_transition_point;
	int daily_max_uptime_secs;
	int uptime_secs;
	bool is_timer_enabled;
	unsigned long enabled_time;
	unsigned long refresh_time;
};

struct dsi_backlight_config {
	enum dsi_backlight_type type;
	enum bl_update_flag bl_update;

	u32 bl_min_level;
	u32 bl_max_level;
	u32 brightness_max_level;
	u32 brightness_default_level;
	/* current brightness value */
	u32 brightness;
	u32 bl_level;
	u32 doze_bl_level;
	u32 bl_scale;
	u32 bl_scale_sv;
	u32 bl_dcs_subtype;
	bool bl_inverted_dbv;
	/* digital dimming backlight LUT */
	struct drm_msm_dimming_bl_lut *dimming_bl_lut;
	u32 dimming_min_bl;
	u32 dimming_status;
	bool user_disable_notification;

	int en_gpio;
	/* PWM params */
	struct pwm_device *pwm_bl;
	bool pwm_enabled;
	u32 pwm_period_usecs;

	/* WLED params */
	struct led_trigger *wled;
	struct backlight_device *raw_bd;

	/* DCS params */
	bool lp_mode;

	/* AoD brightness params */
	u16 aod_dbv_reg;
	u16 inter_dbv_reg;
	u32 aod_bl_min_level;
	u32 aod_bl_max_level;
	bool is_fake_aod_on;
	struct dsi_bl_fake_aod_config fake_aod_cfg;

	/* HBM params */
	u16 hbm_dbv_reg;
	u32 hbm_bl_min_level;
	u32 hbm_bl_max_level;
	u32 hbm_transition_point;
	bool is_hbm_on;

	/* LBM params */
	u16 lbm_dbv_reg;
	u16 lbm_frame_transition_reg;
	u32 *lbm_frame_transition;
	u32 lbm_frame_transition_count;
	u32 lbm_transition_point;
	u32 *lbm_alpha_masks;
	bool is_lbm_on;

	struct dsi_bl_dyn_ramp dyn_ramp;
};

struct dsi_reset_seq {
	u32 level;
	u32 sleep_ms;
};

struct dsi_panel_reset_config {
	struct dsi_reset_seq *sequence;
	u32 count;

	int reset_gpio;
	int disp_en_gpio;
	int lcd_mode_sel_gpio;
	u32 mode_sel_state;
	u32 esd_post_reset_time;
};

enum esd_check_status_mode {
	ESD_MODE_REG_READ,
	ESD_MODE_SW_BTA,
	ESD_MODE_PANEL_TE,
	ESD_MODE_SW_SIM_SUCCESS,
	ESD_MODE_SW_SIM_FAILURE,
	ESD_MODE_MAX
};

struct drm_panel_esd_config {
	bool esd_enabled;

	enum esd_check_status_mode status_mode;
	struct dsi_panel_cmd_set status_cmd;
	u32 *status_cmds_rlen;
	u32 *status_valid_params;
	u32 *status_value;
	u8 *return_buf;
	u8 *status_buf;
	u32 groups;
};

struct dsi_panel_spr_info {
	bool enable;
	enum msm_display_spr_pack_type pack_type;
	enum msm_display_spr_pack_type_mode pack_type_mode;
};

struct dsi_panel;

struct dsi_panel_ops {
	int (*pinctrl_init)(struct dsi_panel *panel);
	int (*gpio_request)(struct dsi_panel *panel);
	int (*pinctrl_deinit)(struct dsi_panel *panel);
	int (*gpio_release)(struct dsi_panel *panel);
	int (*bl_register)(struct dsi_panel *panel);
	int (*bl_unregister)(struct dsi_panel *panel);
	int (*parse_gpios)(struct dsi_panel *panel);
	int (*parse_power_cfg)(struct dsi_panel *panel);
	int (*trigger_esd_attack)(struct dsi_panel *panel);
};

struct dsi_panel_smooth_br {
	// Duration (in jiffies) needed for a smooth transition
	// of brightness levels after entering or exiting AOD mode.
	unsigned long mode_transition_req_time;

	// Timestamp (in jiffies) marking the moment when AOD/HBM mode
	// was either entered or exited.
	unsigned long last_mode_transition_time;
};

struct dsi_panel_mcu_display_panel_cmd_types {
	u32 dsi_cmd_types;
	u32 mcu_panel_cmd_types;
};

struct dsi_panel_sn_config {
	u8 *sn_number;
	struct dsi_panel_cmd_set pre_cmd;
	struct dsi_panel_cmd_set post_cmd;
	u32 reg;
	u32 readback_size;
};

struct dsi_panel {
	const char *name;
	const char *type;
	struct device_node *panel_of_node;
	struct mipi_dsi_device mipi_device;
	bool panel_ack_disabled;

	struct mutex panel_lock;
	struct drm_panel drm_panel;
	struct mipi_dsi_host *host;
	struct device *parent;

	struct dsi_host_common_cfg host_config;
	struct dsi_video_engine_cfg video_config;
	struct dsi_cmd_engine_cfg cmd_config;
	enum dsi_op_mode panel_mode;
	bool panel_mode_switch_enabled;
	bool poms_align_vsync;

	struct dsi_dfps_capabilities dfps_caps;
	struct dsi_dyn_clk_caps dyn_clk_caps;
	struct dsi_panel_phy_props phy_props;
	bool dsc_switch_supported;

	struct dsi_display_mode *cur_mode;
	u32 num_timing_nodes;
	u32 num_display_modes;

	struct dsi_regulator_info power_info;
	struct dsi_backlight_config bl_config;
	struct dsi_panel_reset_config reset_config;
	struct dsi_pinctrl_info pinctrl;
	struct drm_panel_hdr_properties hdr_props;
	struct drm_panel_esd_config esd_config;

	struct dsi_parser_utils utils;

	bool lp11_init;
	bool ulps_feature_enabled;
	bool ulps_suspend_enabled;
	bool allow_phy_power_off;
	bool en_gpio_always_on;
	bool reset_gpio_always_on;
	atomic_t esd_recovery_pending;

	bool skip_panel_off;
	bool skip_panel_on_from_off;
	bool panel_initialized;
	bool panel_mux_offloaded;
	bool te_using_watchdog_timer;
	bool disable_cesta_hw_sleep;
	struct dsi_qsync_capabilities qsync_caps;
	struct dsi_avr_capabilities avr_caps;
	struct dsi_esync_capabilities esync_caps;
	struct msm_vrr_capabilities vrr_caps;

	struct dsi_vreg **shutdown_seq_vregs;
	u32 shutdown_seq_vregs_count;

	struct regulator **always_enabled_vregs;
	u32 always_enabled_vregs_count;

	bool event_notification_disabled;

	char dce_pps_cmd[DSI_CMD_PPS_SIZE];
	enum dsi_dms_mode dms_mode;

	struct dsi_panel_spr_info spr_info;

	bool sync_broadcast_en;
	u32 dsc_count;
	u32 lm_count;

	bool ctl_op_sync;

	int panel_test_gpio;
	int power_mode;
	// Updated by both the microcontroller unit (MCU) and the application processor (AP)
	// to ensure it reflects the actual power state of the panel
	int actual_power_mode;
	// Used for actual_power_mode change notification
	struct kobject *conn_dev_kobj;
	bool powered;
	enum dsi_panel_physical_type panel_type;

	struct dsi_panel_ops panel_ops;

	struct dsi_panel_smooth_br smooth_br;

	// Delay for DDIC reset in panel power-on-to-power-on scenarios
	// (used for specific panels needing additional reset time)
	u32 dsi_panel_reset_delay_on_to_on;

	/* Panel cmd types for MCU display cmds synchronization */
	struct dsi_panel_mcu_display_panel_cmd_types *panel_cmd_types;
	u32 panel_cmd_types_len;

	/* Cfg for reading panel SN from DDIC */
	struct dsi_panel_sn_config sn_cfg;
};

static inline bool dsi_panel_ulps_feature_enabled(struct dsi_panel *panel)
{
	return panel->ulps_feature_enabled;
}

static inline bool dsi_panel_initialized(struct dsi_panel *panel)
{
	return panel->panel_initialized;
}

static inline bool dsi_panel_mux_offloaded(struct dsi_panel *panel)
{
	return panel->panel_mux_offloaded;
}

static inline void dsi_panel_acquire_panel_lock(struct dsi_panel *panel)
{
	mutex_lock(&panel->panel_lock);
}

static inline void dsi_panel_release_panel_lock(struct dsi_panel *panel)
{
	mutex_unlock(&panel->panel_lock);
}

static inline bool dsi_panel_is_type_oled(struct dsi_panel *panel)
{
	return (panel->panel_type == DSI_DISPLAY_PANEL_TYPE_OLED);
}

void dsi_panel_assign_sde_conn_sysfs_dev_kobj(struct dsi_panel *panel, struct kobject *kobj);

struct dsi_panel *dsi_panel_get(struct device *parent,
				struct device_node *of_node,
				struct device_node *parser_node,
				const char *type,
				int topology_override,
				bool trusted_vm_env);

void dsi_panel_put(struct dsi_panel *panel);

void dsi_panel_shutdown(struct dsi_panel *panel);

void dsi_panel_get_fps_switch_cmd(struct dsi_panel *panel,
			struct dsi_display_mode *mode, u32 refresh_rate);

int dsi_panel_drv_init(struct dsi_panel *panel, struct mipi_dsi_host *host);

int dsi_panel_drv_deinit(struct dsi_panel *panel);

int dsi_panel_get_mode_count(struct dsi_panel *panel);

void dsi_panel_put_mode(struct dsi_display_mode *mode);

int dsi_panel_get_mode(struct dsi_panel *panel,
		       u32 index,
		       struct dsi_display_mode *mode,
		       int topology_override);

int dsi_panel_validate_mode(struct dsi_panel *panel,
			    struct dsi_display_mode *mode);

int dsi_panel_get_host_cfg_for_mode(struct dsi_panel *panel,
				    struct dsi_display_mode *mode,
				    struct dsi_host_config *config);

int dsi_panel_get_phy_props(struct dsi_panel *panel,
			    struct dsi_panel_phy_props *phy_props);
int dsi_panel_get_dfps_caps(struct dsi_panel *panel,
			    struct dsi_dfps_capabilities *dfps_caps);

int dsi_panel_pre_prepare(struct dsi_panel *panel);

int dsi_panel_set_lp1(struct dsi_panel *panel);

int dsi_panel_set_lp2(struct dsi_panel *panel);

/**
 * dsi_panel_set_lp2_load() -	Add or remove LP2 load on DSI pannel supplies
 * @panel:			DSI panel handle.
 * @enable:			Boolean to control whether to add or remove
 * the LP2 load.
 *
 * Return: error code.
 */
int dsi_panel_set_lp2_load(struct dsi_panel *panel, bool enable);

int dsi_panel_set_nolp(struct dsi_panel *panel);

int dsi_panel_set_lp1_sub_locked(struct dsi_panel *panel);

int dsi_panel_set_lp2_sub_locked(struct dsi_panel *panel);

int dsi_panel_set_nolp_sub_locked(struct dsi_panel *panel);

int dsi_panel_prepare(struct dsi_panel *panel);

int dsi_panel_reset(struct dsi_panel *panel);

int dsi_panel_enable(struct dsi_panel *panel);

int dsi_panel_post_enable(struct dsi_panel *panel);

int dsi_panel_pre_disable(struct dsi_panel *panel);

int dsi_panel_disable(struct dsi_panel *panel);

int dsi_panel_unprepare(struct dsi_panel *panel);

int dsi_panel_post_unprepare(struct dsi_panel *panel);

int dsi_panel_enable_brightness_ramp_locked(struct dsi_panel *panel);

int dsi_panel_disable_brightness_ramp_locked(struct dsi_panel *panel);

int dsi_panel_set_brightness_ramp_frame_locked(struct dsi_panel *panel, int ramp_frame,
					       int distance);

int dsi_panel_set_lbm_ramp_frame_locked(struct dsi_panel *panel, int ramp_frame);

int dsi_panel_set_backlight_locked(struct dsi_panel *panel, u32 bl_lvl);

int dsi_panel_set_doze_backlight_locked(struct dsi_panel *panel, u32 bl_lvl);

int dsi_panel_update_pps(struct dsi_panel *panel);

int dsi_panel_send_qsync_on_dcs(struct dsi_panel *panel,
		int ctrl_idx);
int dsi_panel_send_qsync_off_dcs(struct dsi_panel *panel,
		int ctrl_idx);

int dsi_panel_send_roi_dcs(struct dsi_panel *panel, int ctrl_idx,
		struct dsi_rect *roi);

int dsi_panel_dcs_cmd_tx(struct dsi_panel *panel, enum dsi_cmd_set_type cmd);

int dsi_panel_switch_video_mode_out(struct dsi_panel *panel);

int dsi_panel_switch_cmd_mode_out(struct dsi_panel *panel);

int dsi_panel_switch_cmd_mode_in(struct dsi_panel *panel);

int dsi_panel_switch_video_mode_in(struct dsi_panel *panel);

int dsi_panel_switch(struct dsi_panel *panel);

int dsi_panel_post_switch(struct dsi_panel *panel);

void dsi_dsc_pclk_param_calc(struct msm_display_dsc_info *dsc, int intf_width);

void dsi_panel_bl_handoff(struct dsi_panel *panel);

struct dsi_panel *dsi_panel_ext_bridge_get(struct device *parent,
				struct device_node *of_node,
				int topology_override);

int dsi_panel_parse_esd_reg_read_configs(struct dsi_panel *panel);

void dsi_panel_ext_bridge_put(struct dsi_panel *panel);

int dsi_panel_get_io_resources(struct dsi_panel *panel,
		struct msm_io_res *io_res);

void dsi_panel_calc_dsi_transfer_time(struct dsi_host_common_cfg *config,
		struct dsi_display_mode *mode, u32 frame_threshold_us);

int dsi_panel_get_cmd_pkt_count(const char *data, u32 length, u32 *cnt);

int dsi_panel_alloc_cmd_packets(struct dsi_panel_cmd_set *cmd,
		u32 packet_count);

int dsi_panel_create_cmd_packets(const char *data, u32 length, u32 count,
					struct dsi_cmd_desc *cmd);

void dsi_panel_destroy_cmd_packets(struct dsi_panel_cmd_set *set);

void dsi_panel_dealloc_cmd_packets(struct dsi_panel_cmd_set *set);

int dsi_panel_send_cmd(struct dsi_panel *panel,
		struct msm_display_conn_params *params, enum dsi_cmd_set_type type,
		bool last_command);

int dsi_panel_parse_freq_step_table(struct dsi_display_mode *mode,
				struct dsi_parser_utils *utils);
int dsi_panel_set_hbm_locked(struct dsi_panel *panel, int brightness);

int dsi_panel_set_lbm_locked(struct dsi_panel *panel, int brightness);
#endif /* _DSI_PANEL_H_ */
