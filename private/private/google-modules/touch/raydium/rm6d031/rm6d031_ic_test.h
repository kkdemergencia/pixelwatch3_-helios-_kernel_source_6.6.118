/* SPDX-License-Identifier: GPL-2.0+ */
#include "ic_drv_global.h"

/****************************************************************************
 *                            Defined Const Value
 ****************************************************************************/
#define FT_CMD_DO_FT_TEST	0x14
#define CUSID_AUO_rm6d031	0x02

extern unsigned char set_test_info_thd_para_rm6d031(void);
extern unsigned char check_test_fw_status_rm6d031(unsigned char u8_target_status, unsigned char *p_u8_result);
extern unsigned char ft_test_do_fw_test_rm6d031(unsigned short u16_test_items);
extern unsigned char enter_fw_test_mode_rm6d031(void);
extern unsigned char system_test_rm6d031(void);
extern unsigned char ft_test_ctrl_mbist_fun_rm6d031(unsigned char u8_enable);
extern unsigned char ft_test_ram_test_rm6d031(unsigned char u8_is_stop_mcu);
extern unsigned char ft_test_connect_test_rm6d031(void);
extern unsigned char ft_test_reset_pin_test_rm6d031(void);
extern unsigned char ft_raw_data_checksum_check_rm6d031(unsigned short *u16_buffer);
extern unsigned char ft_test_result_checksum_check_rm6d031(unsigned char *u8_buffer);
extern unsigned char check_cc_bl_flag_rm6d031(void);
extern unsigned char check_cc_bl_flag_spi_rm6d031(void);

extern unsigned char read_test_fw_data_rm6d031(unsigned short u16_test_items);
extern unsigned char load_test_fw_rm6d031(void);
extern unsigned char fw_version_check_rm6d031(void);

extern void dump_image_data_rm6d031(short *p_image_buf, unsigned char u8_remap);
extern void dump_image_hex_data_rm6d031(short *p_image_buf);
extern unsigned char ft_test_read_used_pin_infor_rm6d031(unsigned char *p_u8_infor);
extern void ft_raw_data_checksum_cal_rm6d031(unsigned short *u16_buffer);
extern void ft_test_result_checksum_cal_rm6d031(unsigned char *u8_buffer);
extern unsigned char enter_normal_fw_rm6d031(void);
extern unsigned char hw_int_pin_Test_rm6d031(void);
extern void test_item_message_rm6d031(void);
extern unsigned char burn_cc_rm6d031(unsigned short u16_test_items);
extern void do_ic_test_rm6d031(void);
extern unsigned char burn_data_log_to_flash_rm6d031(void);
extern unsigned char burn_header_log_to_flash_rm6d031(bool is_test_finish, bool is_in_header_data_page);

//-----------------------------extern FT function ------------------------------------------
extern unsigned char load_test_fw_ft_rm6d031(void);
extern void do_ic_panel_test_rm6d031(void);
extern unsigned char burn_fw_rm6d031(void);
extern void test_log_raw_data_remap_rm6d031(short *p_i16_source_buf, short *p_i16_result_buf, unsigned char u8_data_remap);
extern unsigned char ft_test_I2C_JIG_verify_rm6d031(void);
extern unsigned char ft_test_I2C_500K_VIH_rm6d031(void);
extern unsigned char ft_test_500K_VIH_Experiment_rm6d031(void);


