/* SPDX-License-Identifier: GPL-2.0+ */
#include "ic_drv_global.h"

#define HEADER_LENGTH	(4)//length + checksum

extern unsigned char enable_ic_block_rm6d031(void);
extern unsigned char stop_mcu_rm6d031(unsigned char u8_is_tp_reset);
extern unsigned char hardware_reset_rm6d031(unsigned char u8_enable_ic_block);
extern unsigned char check_dev_id_rm6d031(unsigned short u16_dev_id);
extern unsigned char check_dev_sub_version_rm6d031(unsigned char u8_version);
extern unsigned char check_dev_touch_id_rm6d031(unsigned int *p_u32_touch_id);
extern unsigned char set_fw_system_cmd_rm6d031(unsigned int u32_sysm_cmd);
extern unsigned char  wait_fw_state_rm6d031(unsigned int u32_addr, unsigned int u32_state, unsigned short u16_delay, unsigned short u16_retry);
extern unsigned char interface_init_rm6d031(void);
extern unsigned char write_driver_by_touch_if_rm6d031(const unsigned char *p_u8_data, uint16_t u16_data_length);
extern unsigned char read_ic_ext_flash_rm6d031(unsigned int u32_addr, unsigned int *p_u32_data);
extern unsigned char baseline_update_control_rm6d031(unsigned char u8_enable_baseline_update);
extern unsigned char unlock_spi_wrt_to_pram_rm6d031(unsigned char u8_interface);
extern unsigned char burn_ext_flash_from_image_rm6d031(void);
extern unsigned char burn_pram_from_image_rm6d031(void);
extern unsigned char do_calibration_rm6d031(unsigned char u8_do_calibration_cmd, unsigned char u8_burn_flash);
extern unsigned char do_bl_calibration_rm6d031(void);
extern unsigned char check_register_status_rm6d031(unsigned int u32_addr, unsigned int u32_mask, unsigned int u32_target_status, unsigned char *p_u8_result);
extern unsigned char check_pram_crc32_rm6d031(unsigned int u32_addr, unsigned int u32_len);
extern void touch_lock_rm6d031(void);
extern unsigned char check_i2c_nid_rm6d031(unsigned char u8_nid);
extern unsigned char check_display_mode_rm6d031(void);


