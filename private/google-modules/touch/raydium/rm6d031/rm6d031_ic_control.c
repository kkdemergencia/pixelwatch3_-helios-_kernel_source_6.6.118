// SPDX-License-Identifier: GPL-2.0+
#ifdef __KERNEL__
#include <linux/delay.h>
#include <linux/mutex.h>
#include <linux/unistd.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#else
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#endif

#include "drv_interface.h"
#include "ic_drv_global.h"
#include "ic_drv_interface.h"
#include "rm6d031_ic_control.h"
#include "rm6d031_ic_reg.h"
#include "hal_interface.h"
#if !SELFTEST
#include "ic_drv_interface_ft.h"
#endif

unsigned char check_dev_id_rm6d031(unsigned short u16_dev_id)
{
	unsigned int u32_read = 0;

	if (handle_ic_read(REG_FLASHCTL_DEVID_ADDR, 4, (unsigned char *)(&u32_read), g_u8_drv_interface, I2C_WORD_MODE) == ERROR)
		return ERROR;

	if (((u32_read & 0xFFFF0000) >> 16) == u16_dev_id)
		return SUCCESS;

	DEBUGOUT("Device ID NG! 0x%x:0x%x\r\n", ((u32_read & 0xFFFF0000) >> 16), u16_dev_id);
	return ERROR;
}

unsigned char check_dev_sub_version_rm6d031(unsigned char u8_version)
{
	unsigned int u32_read = 0;

	if (handle_ic_read(REG_FLASHCTL_DEVID_ADDR, 4, (unsigned char *)(&u32_read), g_u8_drv_interface, I2C_WORD_MODE) == ERROR)
		return ERROR;

	DEBUGOUT("Device Sub Version 0x%x\r\n", u32_read);
	if ((u32_read & 0x000000FF) == u8_version)
		return SUCCESS;

	DEBUGOUT("Device Sub Version NG! 0x%x:0x%x\r\n", (u32_read & 0x000000FF), u8_version);
	return ERROR;
}

unsigned char check_dev_touch_id_rm6d031(unsigned int *p_u32_touch_id)
{
	unsigned int u32_read = 0;

	if (handle_ic_read(REG_DRIVER_TP_PANEL_ID_30, 4, (unsigned char *)(&u32_read), g_u8_drv_interface, I2C_WORD_MODE) == ERROR)
		return ERROR;

	DEBUGOUT("Device Touch ID_30! 0x%x\r\n", u32_read);
	if (u32_read != p_u32_touch_id[0]) {
		DEBUGOUT("Device Touch ID_30 NG! 0x%x:0x%x\r\n", (u32_read), p_u32_touch_id[0]);
		return ERROR;
	}

	if (handle_ic_read(REG_DRIVER_TP_PANEL_ID_74, 4, (unsigned char *)(&u32_read), g_u8_drv_interface, I2C_WORD_MODE) == ERROR)
		return ERROR;

	DEBUGOUT("Device Touch ID_74! 0x%x\r\n", u32_read);
	if (u32_read != p_u32_touch_id[1]) {
		DEBUGOUT("Device Touch ID_74 NG! 0x%x:0x%x\r\n", (u32_read), p_u32_touch_id[1]);
		return ERROR;
	}

	if (handle_ic_read(REG_DRIVER_TP_PANEL_ID_98, 4, (unsigned char *)(&u32_read), g_u8_drv_interface, I2C_WORD_MODE) == ERROR)
		return ERROR;

	DEBUGOUT("Device Touch ID_98! 0x%x\r\n", u32_read);
	if ((u32_read & 0x0000ffff) != p_u32_touch_id[2]) {
		DEBUGOUT("Device Touch ID_98 NG! 0x%x:0x%x\r\n", (u32_read),  p_u32_touch_id[2]);
		return ERROR;
	}

	return SUCCESS;
}


unsigned char check_display_mode_rm6d031(void)
{
	unsigned int u32_read = 0;

	if (handle_ic_read(REG_DRIVERMODE_ADDR, 4, (unsigned char *)(&u32_read), g_u8_drv_interface, I2C_WORD_MODE) == ERROR)
		return ERROR;


	DEBUGOUT("Display mode: 0x%x\r\n", u32_read);
	if ((u32_read & 0x700) != 0x400) {
		DEBUGOUT("Display mode NG! 0x%x\r\n", (u32_read));
		return ERROR;
	}

	return SUCCESS;
}

unsigned char check_i2c_nid_rm6d031(unsigned char u8_nid)
{
	unsigned int u32_read = 0;

	if (handle_ic_read(REG_TP_TRYTRIM_CFG_3, 4, (unsigned char *)(&u32_read), g_u8_drv_interface, I2C_WORD_MODE) == ERROR)
		return ERROR;

	DEBUGOUT("I2C NID: 0x%x\r\n", ((u32_read & 0x00007F00) >> 8));
	if (((u32_read & 0x00007F00) >> 8) == u8_nid)
		return SUCCESS;

	DEBUGOUT("I2C NID NG! 0x%x:0x%x\r\n", ((u32_read & 0x00007F00) >> 8), u8_nid);
	return ERROR;
}


unsigned char enable_ic_block_rm6d031(void)
{
	unsigned int u32_read = 0;

	if (handle_ic_read(REG_SYSCON_BLKEN_ADDR, 4, (unsigned char *)(&u32_read), g_u8_drv_interface, I2C_WORD_MODE) == ERROR)
		return ERROR;

	u32_read |= 0xc8000000;
	//u32_read |= (BLKEN_FIC_RB_EN | BLKEN_GPIO_RB_EN | BLKEN_SYS_RB_EN);
	if (handle_ic_write(REG_SYSCON_BLKEN_ADDR, 4, (unsigned char *)&u32_read, g_u8_drv_interface, I2C_WORD_MODE) == ERROR)
		return ERROR;

	if (handle_ic_read(REG_SYSCON_MISCIER_ADDR, 4, (unsigned char *)(&u32_read), g_u8_drv_interface, I2C_WORD_MODE) == ERROR)
		return ERROR;

	u32_read |= 0x00000404;
	//u32_read |= (MISCIER_RB_MCU_INTO | MISCIER_RB_MCU_INT_EN);
	if (handle_ic_write(REG_SYSCON_MISCIER_ADDR, 4, (unsigned char *)&u32_read, g_u8_drv_interface, I2C_WORD_MODE) == ERROR)
		return ERROR;
	return SUCCESS;
}

unsigned char interface_init_rm6d031(void)
{
	unsigned int u32_buf = 0;

	if (g_u8_drv_interface == I2C_INTERFACE) {
		if (handle_i2c_eng_init() == ERROR) {
			DEBUGOUT("[%s] I2C INIT NG!\r\n", __func__);
			return ERROR;
		}
		handle_ic_read(REG_FLASHCTL_DEVID_ADDR, 4, (unsigned char *)(&u32_buf), g_u8_drv_interface, I2C_WORD_MODE);
		//DEBUGOUT("Device:0x%x\r\n", (u32_buf & 0xFFFF0000));
		if ((u32_buf & 0xFFFF0000) != 0xF3030000) {
			DEBUGOUT("[%s] I2C INIT NG! 0x%x\r\n",  __func__, u32_buf);
			return ERROR;
		}
	} else if (g_u8_drv_interface == SPI_INTERFACE) {
		handle_ic_read(REG_FLASHCTL_DEVID_ADDR, 4, (unsigned char *)(&u32_buf), g_u8_drv_interface, I2C_WORD_MODE);
		//DEBUGOUT("Device:0x%x\r\n", (u32_buf & 0xFFFF0000));
		if ((u32_buf & 0xFFFF0000) != 0xF3030000) {
			DEBUGOUT("[%s] SPI INIT NG! 0x%x\r\n", __func__, u32_buf);
			return ERROR;
		}
	}
	return SUCCESS;

}

unsigned char check_register_status_rm6d031(unsigned int u32_addr, unsigned int u32_mask, unsigned int u32_target_status, unsigned char *p_u8_result)
{
	unsigned int u32_read;

	if (handle_ic_read(u32_addr, 4, (unsigned char *)(&u32_read), g_u8_drv_interface, I2C_WORD_MODE) == ERROR)
		return ERROR;

	if ((u32_read & u32_mask) == u32_target_status) {
		*p_u8_result = 1;
		DEBUGOUT("[CREGS] Register STATUS (%x:%x)\r\n", u32_target_status, (u32_read & u32_mask));
	} else {
		DEBUGOUT("[CREGS] Register is not Ready!! (%x:%x)\r\n", u32_target_status, (u32_read & u32_mask));
		*p_u8_result = 0;
	}
	return SUCCESS;
}

void touch_lock_rm6d031(void)
{
	unsigned int u32_write = 0;

	u32_write = (0x00000030);
	handle_ic_write(FW_SYS_CMD_ADDR, 4, (unsigned char *)&u32_write, g_u8_drv_interface, I2C_WORD_MODE);
}

unsigned char stop_mcu_rm6d031(unsigned char u8_is_tp_reset)
{
	unsigned short u16_time_out = 100;
	unsigned int u32_read_data = 0;
	unsigned int u32_write = 0;

	if (handle_ic_read(REG_FLASHCTL_FLASH_STATE_REG_ADDR, 4, (unsigned char *)(&u32_read_data), g_u8_drv_interface, I2C_WORD_MODE) == ERROR)
		goto EXIT_ERROR;

	DEBUGOUT("Stop MCU=0x%X(0x%x)(%d)!!\r\n", u32_read_data, (u32_read_data & MCU_HOLD_STATUS), u16_time_out);
	if ((u32_read_data & MCU_HOLD_STATUS) == MCU_HOLD_STATUS)
		DEBUGOUT("[%s]1.  STOP MCU Already!\r\n", __func__);
	else {
		g_u8_mute_i2c_err_log = TRUE;
		if (u8_is_tp_reset) {
			hal_gpio_touch_hw_reset();
			hal_delay_ms(35);

			//Set Display Initial & Normal On
			if (interface_init_rm6d031() == ERROR) {
				DEBUGOUT("[%s] Interface Init NG!\r\n", __func__);
				goto EXIT_ERROR;
			}
		}

		//DEBUGOUT("[stop_mcu_rm6d031] 1\r\n");

		//Stop MCU
		//memset(wData, 0, sizeof(wData));
		u32_write = (MCU_HOLD | SKIP_LOAD);
		if (handle_ic_write(REG_FLASHCTL_FLASH_STATE_REG_ADDR, 4, (unsigned char *)&u32_write, g_u8_drv_interface, I2C_WORD_MODE) == ERROR)
			goto EXIT_ERROR;

		u32_write = BLKRST_SW_RST;
		if (handle_ic_write(REG_SYSCON_BLKRST_ADDR, 4, (unsigned char *)&u32_write, g_u8_drv_interface, I2C_WORD_MODE) == ERROR)
			goto EXIT_ERROR;

		hal_delay_ms(20);

		if (interface_init_rm6d031() == ERROR) {
			DEBUGOUT("[%s] 3. Interface Init NG!\r\n", __func__);
			goto EXIT_ERROR;
		}
		//DEBUGOUT("[stop_mcu_rm6d031] 2\r\n");

		if (handle_ic_read(REG_FLASHCTL_FLASH_STATE_REG_ADDR, 4, (unsigned char *)(&u32_read_data), g_u8_drv_interface, I2C_WORD_MODE) == ERROR) {
			DEBUGOUT("[%s] 4.Flash State NG!\r\n", __func__);
			goto EXIT_ERROR;
		}

		while ((u32_read_data & MCU_HOLD_STATUS) == 0 && u16_time_out-- > 0) {
			hal_delay_ms(10);
			if (handle_ic_read(REG_FLASHCTL_FLASH_STATE_REG_ADDR, 4, (unsigned char *)(&u32_read_data), g_u8_drv_interface, I2C_WORD_MODE) == ERROR)
				goto EXIT_ERROR;
		}

		DEBUGOUT("Stop MCU=0x%X(0x%x)(%d)!!\r\n", u32_read_data, (u32_read_data & MCU_HOLD_STATUS), u16_time_out);

		if ((u32_read_data & MCU_HOLD_STATUS) == 0) {
			DEBUGOUT("[%s] 4.STOP MCU NG!\r\n", __func__);
			goto EXIT_ERROR;
		}

		g_u8_mute_i2c_err_log = FALSE;
	}
	return SUCCESS;

EXIT_ERROR:
	g_u8_mute_i2c_err_log = FALSE;
	return ERROR;
}
unsigned char check_cc_bl_flag_spi_rm6d031(void)
{
	unsigned char u8_read_buf[4];
	unsigned char u8_is_cc_ready = 0;
	unsigned char u8_fw_version[4];
	short i16_time_out;

	DEBUGOUT("[CCBF_SPI] Check CC BL Flag SPI\r\n");

	if (handle_ic_read(FW_FT_FW_VERSION, 4, u8_fw_version, g_u8_drv_interface, I2C_BYTE_MODE) == ERROR)
		return ERROR;

	i16_time_out = 500;
	while (i16_time_out--) {
		if (!u8_is_cc_ready) {
			if (handle_ic_read(PRAM_ADDR_CC_INFO, 4, u8_read_buf, g_u8_drv_interface, I2C_BYTE_MODE) == ERROR)
				return ERROR;
			if ((u8_read_buf[0] >= 1) && (u8_read_buf[2] == u8_fw_version[2]) && (u8_read_buf[3] == u8_fw_version[3]))
				u8_is_cc_ready = 1;
		}
		if (u8_is_cc_ready)
			break;
		hal_delay_ms(1);
	}

	//Check flag
	if (i16_time_out == IC_TEST_TIME_OUT) {
		DEBUGOUT("SPI CC Flag=%d:%d NG!!\r\n", u8_read_buf[0], u8_is_cc_ready);
		DEBUGOUT("FW Ver=%X.%X:%X.%X NG!!\r\n", u8_fw_version[2], u8_fw_version[3], u8_read_buf[2], u8_read_buf[3]);
		return ERROR;
	}

	return SUCCESS;
}

unsigned char hardware_reset_rm6d031(unsigned char u8_enable_ic_block)
{
	unsigned char u8_time_out = 200;

	DEBUGOUT("HW Resetting...\r\n");

	if ((g_st_test_info.u16_ft_eng_item & IC_TEST_ENG_ITEMS_SET_DSP_INIT_AF_TP_RESET)) {
		//Set Display Initial & Normal On
		DEBUGOUT("Set Display Initial & Normal On\r\n");
		if (!handle_ic_set_initial_code())
			DEBUGOUT("Set Display Initial Fail\r\n");
	}

	if ((g_u8_drv_interface == SPI_INTERFACE) || (g_st_test_info.u16_ft_eng_item & IC_TEST_ENG_ITEMS_I2C_FLASHLESS)) {
		//flashless

		hal_gpio_touch_hw_reset();
		hal_delay_ms(70);

		if (burn_pram_from_image_rm6d031() == ERROR)
			return ERROR;

		check_cc_bl_flag_spi_rm6d031();

	} else {
		hal_gpio_touch_hw_reset();
		hal_delay_ms(70);
		interface_init_rm6d031();
		g_u8_mute_i2c_err_log = FALSE;
	}

	if (u8_enable_ic_block) {
		if (enable_ic_block_rm6d031() == ERROR) {
			DEBUGOUT("HW Reset NG!!\r\n");
			return ERROR;
		}
	}
	while (u8_time_out--) {
		if (hal_gpio_touch_int_pin_state_access())
			break;

		if (u8_time_out == 0) {
			DEBUGOUT("TP INT state access NG!!\r\n");
			return ERROR;
		}
		hal_delay_ms(1);
	}
	return SUCCESS;
}

unsigned char set_fw_system_cmd_rm6d031(unsigned int u32_sysm_cmd)
{
	unsigned char u8_time_out = 100;
	unsigned char u8_value = 0;

	if (handle_ic_write(FW_SYS_CMD_ADDR, 4, (unsigned char *)&u32_sysm_cmd, g_u8_drv_interface, I2C_WORD_MODE) == ERROR)
		return ERROR;

	// Wait Test Command ready
	while (--u8_time_out) {
		hal_delay_ms(1);
		if (handle_ic_read(FW_SYS_CMD_ADDR, 1, &u8_value, g_u8_drv_interface, I2C_BYTE_MODE) == ERROR)
			return ERROR;
		if (u8_value == 0)
			break;
	}

	if (u8_time_out == 0)
		return ERROR;

	return SUCCESS;
}

unsigned char wait_fw_state_rm6d031(unsigned int u32_addr, unsigned int u32_state, unsigned short u16_delay, unsigned short u16_retry)
{
	unsigned int u32_read_data = 0;

	do {
		if (handle_ic_read(u32_addr, 4, (unsigned char *)(&u32_read_data), g_u8_drv_interface, I2C_WORD_MODE) == ERROR)
			return ERROR;

		hal_delay_ms(u16_delay);
		u16_retry--;
	} while ((u32_read_data != u32_state) && (u16_retry != 0));

	if (u32_read_data != u32_state)
		return ERROR;
	return SUCCESS;
}

unsigned char wait_T2D_done_state_rm6d031(unsigned int u32_addr, unsigned short u16_delay, unsigned short u16_retry)
{
	unsigned int u32_read_data = 0;

	do {
		if (handle_ic_read(u32_addr, 4, (unsigned char *)(&u32_read_data), g_u8_drv_interface, I2C_WORD_MODE) == ERROR)
			return ERROR;
		//DEBUGOUT("[T2DW] ready 0x%x\r\n", u32_read_data);

		hal_delay_ms(u16_delay);
		u16_retry--;
	} while (((u32_read_data & 0x01) == 0) && (u16_retry != 0));

	if ((u32_read_data & 0x01) == 0)
		return ERROR;
	return SUCCESS;
}

unsigned char write_to_driver_rm6d031(unsigned char *p_u8_data, unsigned char u8_data_length)
{
	unsigned int u32_write = 0;

	if (p_u8_data != NULL) {

		if (p_u8_data[0] == 0xFE) {
			g_u8_PAGE_ADDR = p_u8_data[1];
			return SUCCESS;
		}

		if (p_u8_data[0] == 0x35) {
			u32_write = ((0x00 << 24) | (0x00 << 16) | (p_u8_data[0] << 8) | g_u8_PAGE_ADDR);
			if (handle_ic_write(REG_T2D_CONFIG_2, 4, (unsigned char *)&u32_write, g_u8_drv_interface, I2C_WORD_MODE) == ERROR)
				return ERROR;
			u32_write  = 0x0000015A;
			if (handle_ic_write(REG_T2D_CONFIG_3, 4, (unsigned char *)&u32_write, g_u8_drv_interface, I2C_WORD_MODE) == ERROR)
				return ERROR;
		}

		if (u8_data_length == 1)  //cmd mode
			u32_write = ((0x00 << 24) | (0x00 << 16) | (p_u8_data[0] << 8) | g_u8_PAGE_ADDR);
		else if (u8_data_length == 2)
			u32_write = ((0x01 << 24) | (p_u8_data[1] << 16) | (p_u8_data[0] << 8) | g_u8_PAGE_ADDR);
		else if (u8_data_length == 5) {
			// 2A 2B

			u32_write = ((0x01 << 24) | (p_u8_data[1] << 16) | (p_u8_data[0] << 8) | 0x00);
			if (handle_ic_write(REG_T2D_CONFIG_2, 4, (unsigned char *)&u32_write, g_u8_drv_interface, I2C_WORD_MODE) == ERROR)
				return ERROR;
			u32_write  = 0x0000015A;
			if (handle_ic_write(REG_T2D_CONFIG_3, 4, (unsigned char *)&u32_write, g_u8_drv_interface, I2C_WORD_MODE) == ERROR)
				return ERROR;

			u32_write = ((0x01 << 24) | (p_u8_data[2] << 16) | (p_u8_data[0] << 8) | 0x01);
			if (handle_ic_write(REG_T2D_CONFIG_2, 4, (unsigned char *)&u32_write, g_u8_drv_interface, I2C_WORD_MODE) == ERROR)
				return ERROR;
			u32_write  = 0x0000015A;
			if (handle_ic_write(REG_T2D_CONFIG_3, 4, (unsigned char *)&u32_write, g_u8_drv_interface, I2C_WORD_MODE) == ERROR)
				return ERROR;

			u32_write = ((0x01 << 24) | (p_u8_data[3] << 16) | (p_u8_data[0] << 8) | 0x02);
			if (handle_ic_write(REG_T2D_CONFIG_2, 4, (unsigned char *)&u32_write, g_u8_drv_interface, I2C_WORD_MODE) == ERROR)
				return ERROR;
			u32_write  = 0x0000015A;
			if (handle_ic_write(REG_T2D_CONFIG_3, 4, (unsigned char *)&u32_write, g_u8_drv_interface, I2C_WORD_MODE) == ERROR)
				return ERROR;

			u32_write = ((0x01 << 24) | (p_u8_data[4] << 16) | (p_u8_data[0] << 8) | 0x03);
			if (handle_ic_write(REG_T2D_CONFIG_2, 4, (unsigned char *)&u32_write, g_u8_drv_interface, I2C_WORD_MODE) == ERROR)
				return ERROR;
			u32_write  = 0x0000015A;
			if (handle_ic_write(REG_T2D_CONFIG_3, 4, (unsigned char *)&u32_write, g_u8_drv_interface, I2C_WORD_MODE) == ERROR)
				return ERROR;

			return SUCCESS;
		}

		if (handle_ic_write(REG_T2D_CONFIG_2, 4, (unsigned char *)&u32_write, g_u8_drv_interface, I2C_WORD_MODE) == ERROR)
			return ERROR;
		u32_write  = 0x0000015A;
		if (handle_ic_write(REG_T2D_CONFIG_3, 4, (unsigned char *)&u32_write, g_u8_drv_interface, I2C_WORD_MODE) == ERROR)
			return ERROR;
	}
	return SUCCESS;
}

unsigned char read_from_driver_rm6d031(unsigned char *p_u8_addr, unsigned char u8_read_len, unsigned char *p_u8_output_buf)
{
	unsigned int u32_write = 0;

	if ((p_u8_addr[0] == 0xFE) || (p_u8_addr[0] == 0xFF) || (u8_read_len > 1)) {
		DEBUGOUT("[%s] no use\r\n", __func__);
		return FALSE;
	}

	u32_write = ((p_u8_addr[0] << 24) | (g_u8_PAGE_ADDR << 16) | (0x01 << 8) | 0x82);
	DEBUGOUT("read address 0x%x\r\n", u32_write);
	if (handle_ic_write(REG_T2D_R_CONFIG_1, 4, (unsigned char *)&u32_write, g_u8_drv_interface, I2C_WORD_MODE) == ERROR)
		return ERROR;
	if (handle_ic_read(REG_T2D_R_CONFIG_2, 4, p_u8_output_buf, g_u8_drv_interface, I2C_WORD_MODE) == ERROR)
		return ERROR;

	DEBUGOUT("read data 0x%x\r\n", p_u8_output_buf[0]);
	return TRUE;

}

unsigned char write_driver_by_touch_if_rm6d031(const unsigned char *p_u8_data, uint16_t u16_data_length)
{
	unsigned short TotalDataLen = (p_u8_data[3] << 8) + p_u8_data[4] - 7;//Valid data not include 7 byte header
	unsigned short u16_i = 0;
	unsigned char  u8_CurCmdLen = 0;
	unsigned int u32_write = 0;

	for (u16_i = 0; u16_i < (u16_data_length - 7) && u16_i < sizeof(g_u8_data_buf); u16_i++)
		g_u8_data_buf[u16_i] = p_u8_data[u16_i + 7];

	if (p_u8_data[0] == 0xF3 && p_u8_data[1] == 0x01) {
		u32_write  = 0x015AFABC;
		if (handle_ic_write(REG_T2D_CONFIG_2, 4, (unsigned char *)&u32_write, g_u8_drv_interface, I2C_WORD_MODE) == ERROR)
			return ERROR;
		u32_write  = 0x0000015A;
		if (handle_ic_write(REG_T2D_CONFIG_3, 4, (unsigned char *)&u32_write, g_u8_drv_interface, I2C_WORD_MODE) == ERROR)
			return ERROR;

		if (p_u8_data[2] == 0x01 ||
		    p_u8_data[2] == 0x10) {			//SPI short multiple command
			for (u16_i = 0; u16_i < TotalDataLen; u16_i += (u8_CurCmdLen + 1)) {
				u8_CurCmdLen = g_u8_data_buf[u16_i];

				//Check is Delay command
				if (g_u8_data_buf[u16_i + 1] != 0xFF) { //Check Command ID

					if (wait_T2D_done_state_rm6d031(0x50001220, 1, 255) == ERROR) {
						DEBUGOUT("[T2DW] Check T2D idle Fail\r\n");
						return ERROR;
					}

					write_to_driver_rm6d031((unsigned char *)&g_u8_data_buf[u16_i + 1], u8_CurCmdLen);
				} else {
					hal_delay_ms(g_u8_data_buf[u16_i + 2]);
				}
			}
		} else {
			DEBUGOUT("[%s] command type not support\r\n", __func__);
			return ERROR;
		}
		u32_write  = 0x0100FABC;
		if (handle_ic_write(REG_T2D_CONFIG_2, 4, (unsigned char *)&u32_write, g_u8_drv_interface, I2C_WORD_MODE) == ERROR)
			return ERROR;
		u32_write  = 0x0000015A;
		if (handle_ic_write(REG_T2D_CONFIG_3, 4, (unsigned char *)&u32_write, g_u8_drv_interface, I2C_WORD_MODE) == ERROR)
			return ERROR;
	}
	return SUCCESS;
}

unsigned char turn_on_flash_rm6d031(void)
{
	unsigned int u32_read = 0;

	//Turn on Flash
	if (handle_ic_write(REG_I2C_I2CFLASHPRO, 4, (unsigned char *)&u32_read, g_u8_drv_interface, I2C_WORD_MODE) == ERROR)
		return ERROR;

	u32_read = FLH_RELEASE_PD;
	if (handle_ic_write(REG_FLASHCTL_FLASH_ISPCTL, 4, (unsigned char *)&u32_read, g_u8_drv_interface, I2C_WORD_MODE) == ERROR)
		return ERROR;
	return SUCCESS;
}

unsigned char read_ic_ext_flash_rm6d031(unsigned int u32_addr, unsigned int *p_u32_data)
{
	unsigned int u32_read = 0;
	unsigned int u32_write = 0;

	//Turn on Flash
	if (turn_on_flash_rm6d031() == ERROR)
		return ERROR;

	if (handle_ic_read(REG_SYSCON_BLKEN_ADDR, 4, (unsigned char *)(&u32_read), g_u8_drv_interface, I2C_WORD_MODE) == ERROR)
		return ERROR;

	u32_read |= 0x40;
	if (handle_ic_write(REG_SYSCON_BLKEN_ADDR, 4, (unsigned char *)&u32_read, g_u8_drv_interface, I2C_WORD_MODE) == ERROR)
		return ERROR;

	u32_read = 0x00000000;
	if (handle_ic_write(REG_I2C_I2CFLASHPRO, 4, (unsigned char *)&u32_read, g_u8_drv_interface, I2C_WORD_MODE) == ERROR)
		return ERROR;

	if (handle_ic_write(REG_FLASHCTL_FLASH_ADDR, 4, (unsigned char *)&u32_addr, g_u8_drv_interface, I2C_WORD_MODE) == ERROR)
		return ERROR;

	u32_read = 0x40;
	if (handle_ic_write(REG_FLASHCTL_FLASH_ISPCTL, 4, (unsigned char *)&u32_read, g_u8_drv_interface, I2C_WORD_MODE) == ERROR)
		return ERROR;
	hal_delay_ms(1);

	if (handle_ic_read(REG_FLASHCTL_FLASH_DATA, 4, (unsigned char *)(&u32_read), g_u8_drv_interface, I2C_WORD_MODE) == ERROR)
		return ERROR;

	//Turn off Flash
	u32_write = 0;
	if (handle_ic_write(REG_I2C_I2CFLASHPRO, 4, (unsigned char *)&u32_write, g_u8_drv_interface, I2C_WORD_MODE) == ERROR)
		return ERROR;

	u32_write = FLH_ENTER_PD;
	if (handle_ic_write(REG_FLASHCTL_FLASH_ISPCTL, 4, (unsigned char *)&u32_write, g_u8_drv_interface, I2C_WORD_MODE) == ERROR)
		return ERROR;

	*p_u32_data = u32_read;
	return SUCCESS;
}

unsigned char baseline_update_control_rm6d031(unsigned char u8_enable_baseline_update)
{
	unsigned int u32_write_buf;
	unsigned char u8_read;
	short i16_time_out = 2000;

	if (u8_enable_baseline_update) {
		u32_write_buf = (SYS_CMD_FUNC_DIS_BS_UPDATE);
		DEBUGOUT("[BUC] Enable Baseline update\r\n");
	} else {
		u32_write_buf = (DIS_BASELINE_UPDATE | SYS_CMD_FUNC_DIS_BS_UPDATE);
		DEBUGOUT("[BUC] Disable Baseline update\r\n");
	}

	if (handle_ic_write(FW_SYS_CMD_ADDR, 4, (unsigned char *)&u32_write_buf, g_u8_drv_interface, I2C_WORD_MODE) == ERROR)
		return ERROR;

	hal_delay_ms(5);
	while (i16_time_out--) {
		if (handle_ic_read(FW_SYS_CMD_ADDR, 1, &u8_read, g_u8_drv_interface, I2C_BYTE_MODE) == SUCCESS) {
			if (u8_read == 0x00)
				break;
		} else {
			DEBUGOUT("[BUC] I2C Read NG\r\n");
			return ERROR;
		}
		hal_delay_ms(2);
	}

	if (i16_time_out == IC_TEST_TIME_OUT) {
		DEBUGOUT("[BUC] Baseline Update Control (%d) NG\r\n", u8_enable_baseline_update);
		return ERROR;
	}

	return SUCCESS;
}

unsigned char unlock_spi_wrt_to_pram_rm6d031(unsigned char	u8_interface)
{
	unsigned int u32_write = 0x00000000;

	//Unlock SPI wrt to PRAM
	if (u8_interface == SPI_INTERFACE) {
		DEBUGOUT("%s write wrt to pram", __func__);
		if (handle_ic_write(REG_SPI_SECONDARY_SPIFLASHPRO, 4, (unsigned char *)&u32_write, g_u8_drv_interface, I2C_WORD_MODE) == ERROR)
			return ERROR;
	}
	return SUCCESS;
}

unsigned char clear_pram_cc_bl_rm6d031(unsigned int u32_start_addr, unsigned int u32_data_length)
{
	uint32_t u32_end_addr;
	uint16_t u16_data_buf_length;
	uint8_t u8_buf[56];

	memset(u8_buf, 0, sizeof(u8_buf));

	// i2c tx buffer size = 0x38 (56)
	u16_data_buf_length = 0x38;

	u32_end_addr = u32_start_addr + u32_data_length;

	while (u32_start_addr < u32_end_addr) {

		if ((u32_start_addr + u16_data_buf_length) > u32_end_addr)
			u16_data_buf_length = u32_end_addr - u32_start_addr;

		if (handle_ic_write(u32_start_addr, u16_data_buf_length, u8_buf, g_u8_drv_interface, I2C_WORD_MODE) == ERROR)
			return ERROR;

		u32_start_addr += u16_data_buf_length;
	}

	return SUCCESS;
}

void arrange_addr_info_rm6d031(unsigned char u8_type)
{
	unsigned int u32_write = 0;

	switch (u8_type) {

	case COMP_AREA://Comp
		u32_write = PRAM_COMP_START;
		handle_ic_write(REG_FLASHCTL_FLASH_PRAM_ADDR, 4, (unsigned char *)&u32_write, g_u8_drv_interface, I2C_WORD_MODE);
		break;

	case USEFW_AREA://FW + parameter
		u32_write = PRAM_FW_START;
		handle_ic_write(REG_FLASHCTL_FLASH_PRAM_ADDR, 4, (unsigned char *)&u32_write, g_u8_drv_interface, I2C_WORD_MODE);
		break;

	case BOTLR_AREA:
		u32_write = PRAM_RESERVE_START;
		handle_ic_write(REG_FLASHCTL_FLASH_PRAM_ADDR, 4, (unsigned char *)&u32_write, g_u8_drv_interface, I2C_WORD_MODE);
		break;

	default:
		break;
	}

}

unsigned char burn_to_ic_flash_rm6d031(unsigned char u8_type)
{
	uint8_t u8_ret = SUCCESS;
	unsigned short u16_retry_time = 1000;
	unsigned int u32_data;
	unsigned char u8_boot_ver;
	unsigned int u32_write = 0;

	//read bootloader version
	if (handle_ic_read(0x80, 4, (unsigned char *)(&u32_data), g_u8_drv_interface, I2C_WORD_MODE) == ERROR)
		return ERROR;

	u8_boot_ver = ((u32_data & 0x00FF0000) >> 16);

	DEBUGOUT("bootloader ver: 0x%x.%x.%x\r\n", (u32_data & 0xFFFF), u8_boot_ver,
		 ((u32_data & 0xFF000000) >> 24));

	if (u8_type == BOTLR_AREA || u8_type == USEFW_AREA)
		if (stop_mcu_rm6d031(0) == ERROR)
			return ERROR;

	//Write data to pram & Cal Pram data crc32
	if (u8_type == USEFW_AREA) {
		u8_ret = ERROR;
		//unlock pram
		u32_write = (BOTLR_LOCK | COMP_LOCK | BASEL_LOCK | INICO_LOCK);
		if (handle_ic_write(REG_FLASHCTL_FLASH_PRAM_LOCK, 4, (unsigned char *)&u32_write, g_u8_drv_interface, I2C_WORD_MODE) == ERROR)
			return ERROR;
		//write FW & PARA to PRAM
		if ((hal_read_image_fw_to_pram(TEST_USER_FW_IMG, PRAM_FW_LENGTH, PRAM_FW_START) == SUCCESS)
		    && (hal_read_image_fw_to_pram(TEST_USER_PARA_IMG, PRAM_PARA_LENGTH, PRAM_PARA_START) == SUCCESS)) {
			//Check pram fw & PARA crc32
			if (check_pram_crc32_rm6d031(PRAM_FW_START, PRAM_FW_CRC_LENGTH) == SUCCESS)
				u8_ret = 1;
		}
	} else if (u8_type == BOTLR_AREA) {
		u8_ret = ERROR;
		//unlock pram
		u32_write = (BOTLR_LOCK | CONFIG_LOCK | COMP_LOCK | BASEL_LOCK | INICO_LOCK);
		if (handle_ic_write(REG_FLASHCTL_FLASH_PRAM_LOCK, 4, (unsigned char *)&u32_write, g_u8_drv_interface, I2C_WORD_MODE) == ERROR)
			return ERROR;
		//write bootloader to PRAM
		if ((hal_read_image_fw_to_pram(TEST_BL_IMG, PRAM_BOOT_LENGTH, PRAM_RESERVE_START) == SUCCESS)
		    && (hal_read_image_fw_to_pram(TEST_TB_EXE_IMG, PRAM_TB_EXE_LENGTH, PRAM_RESERVE_START + PRAM_BOOT_LENGTH) == SUCCESS)
		    && (hal_read_image_fw_to_pram(TEST_INIT_IMG, PRAM_INIT_LENGTH, PRAM_RESERVE_START + PRAM_BOOT_LENGTH + PRAM_TB_EXE_LENGTH) == SUCCESS)) {
			//Check pram bootloader & initial code crc32
			if ((check_pram_crc32_rm6d031(PRAM_RESERVE_START, PRAM_BOOT_CRC_LENGTH) == SUCCESS)
			    && (check_pram_crc32_rm6d031(PRAM_RESERVE_START + PRAM_BOOT_LENGTH, PRAM_TB_EXE_CRC_LENGTH) == SUCCESS)
			    && (check_pram_crc32_rm6d031(PRAM_RESERVE_START + PRAM_BOOT_LENGTH + PRAM_TB_EXE_LENGTH, PRAM_INIT_CRC_LENGTH) == SUCCESS)) {
				u8_ret = 1;
			}
		}
	}
	if (u8_ret == 0) {
		DEBUGOUT("confirm crc error\r\n");
		return ERROR;
	}

	if (u8_type == BOTLR_AREA || u8_type == USEFW_AREA) {
		//set BL_CRC_CHK & SKIP_LOAD
		u32_write = BL_CRC_CHK | SKIP_LOAD;
		if (handle_ic_write(REG_FLASHCTL_FLASH_STATE_REG_ADDR, 4, (unsigned char *)&u32_write, g_u8_drv_interface, I2C_WORD_MODE) == ERROR)
			return ERROR;

		u32_write = BLKRST_SW_RST;
		if (handle_ic_write(REG_SYSCON_BLKRST_ADDR, 4, (unsigned char *)&u32_write, g_u8_drv_interface, I2C_WORD_MODE) == ERROR)
			return ERROR;
		hal_delay_ms(35);
		//hal_delay_ms(2);

		if (wait_fw_state_rm6d031(BOOT_RET_DATA_ADDR, 0x82, 2, u16_retry_time) == ERROR) {
			DEBUGOUT("bootloader wrt empty flash fail\r\n");
			DEBUGOUT("bootloader Confirm g_u8_sync_data.ret_data at 0x20000214 is 0x82 fail\r\n");
			return ERROR;
		}
	}

	u32_write = MAIN_STATE_BURNING_MODE;
	if (handle_ic_write(BOOT_MAIN_STATE_ADDR, 4, (unsigned char *)&u32_write, g_u8_drv_interface, I2C_WORD_MODE) == ERROR)
		return ERROR;

	u32_write = NORMAL_STATE_CHECK;
	if (handle_ic_write(BOOT_NORMAL_STATE_ADDR, 4, (unsigned char *)&u32_write, g_u8_drv_interface, I2C_WORD_MODE) == ERROR)
		return ERROR;

	u32_write = BURNING_STATE_INIT;
	if (handle_ic_write(BOOT_BURNING_STATE_ADDR, 4, (unsigned char *)&u32_write, g_u8_drv_interface, I2C_WORD_MODE) == ERROR)
		return ERROR;

	// set test mode = 1, then bootloader is running
	u32_write = 0x00000001;
	if (handle_ic_write(BOOT_TEST_MODE_ADDR, 4, (unsigned char *)&u32_write, g_u8_drv_interface, I2C_WORD_MODE) == ERROR)
		return ERROR;

	if (wait_fw_state_rm6d031(BOOT_RET_DATA_ADDR, 0xFF, 2, u16_retry_time) == ERROR) {
		DEBUGOUT("bootloader wrt empty flash fail\r\n");
		DEBUGOUT("bootloader confirm in burn mode fail, Type=%d\r\n", u8_type);
		return ERROR;
	}

	// clear BL_CRC_CHK flag
	u32_write = SKIP_LOAD;
	if (handle_ic_write(REG_FLASHCTL_FLASH_STATE_REG_ADDR, 4, (unsigned char *)&u32_write, g_u8_drv_interface, I2C_WORD_MODE) == ERROR)
		return ERROR;

	// set PRAM type (at 0x50000904), wrt param code
	// init_code:0x01,baseline:0x02,COMP:0x04,param:0x08,FW:0x10,bootloader:0x20
	u32_write = u8_type;
	if (handle_ic_write(REG_FLASHCTL_FLASH_PRAM_STORE_TYPE, 4, (unsigned char *)&u32_write, g_u8_drv_interface, I2C_WORD_MODE) == ERROR)
		return ERROR;

	// set PRAM addr (at 'h5000_0908)
	// init_code:0x0800,Baseline:0x0A00,COMP:0x0CD4,para:0x0F1C,FW:0x1000,BOOT:0x5000
	// set PRAM length (at 'h5000_090C)
	arrange_addr_info_rm6d031(u8_type);

	// set sync_data(0x20000200) = 0 as WRT data finish
	u32_write = 0x00000000;
	if (handle_ic_write(BOOT_SYNC_DATA_ADDR, 4, (unsigned char *)&u32_write, g_u8_drv_interface, I2C_WORD_MODE) == ERROR)
		return ERROR;

	// wait for input unlock key
	if (wait_fw_state_rm6d031(BOOT_CMD_TYPE_ADDR, GET_WRT_UNLOCK, 2, u16_retry_time) == ERROR) {
		DEBUGOUT("bootloader wrt empty flash fail\r\n");
		DEBUGOUT("bootloader wait for input unlock key fail\r\n");
		return ERROR;
	}

	// unlock key
	u32_write = FLKEY2_KEY;
	if (handle_ic_write(REG_FLASHCTL_FLASH_FLKEY2, 4, (unsigned char *)&u32_write, g_u8_drv_interface, I2C_WORD_MODE) == ERROR)
		return ERROR;

	u32_write = FLKEY1_KEY;
	if (handle_ic_write(REG_FLASHCTL_FLASH_FLKEY1, 4, (unsigned char *)&u32_write, g_u8_drv_interface, I2C_WORD_MODE) == ERROR)
		return ERROR;

	u32_write = FLKEY1_LOCK;
	if (handle_ic_write(REG_FLASHCTL_FLASH_FLKEY1, 4, (unsigned char *)&u32_write, g_u8_drv_interface, I2C_WORD_MODE) == ERROR)
		return ERROR;

	u32_write = FLKEY1_KEY;
	if (handle_ic_write(REG_FLASHCTL_FLASH_FLKEY1, 4, (unsigned char *)&u32_write, g_u8_drv_interface, I2C_WORD_MODE) == ERROR)
		return ERROR;

	u32_write = FLKEY2_LOCK;
	if (handle_ic_write(REG_FLASHCTL_FLASH_FLKEY2, 4, (unsigned char *)&u32_write, g_u8_drv_interface, I2C_WORD_MODE) == ERROR)
		return ERROR;

	u32_write = 0x00000000;
	if (handle_ic_write(REG_I2C_I2CFLASHPRO, 4, (unsigned char *)&u32_write, g_u8_drv_interface, I2C_WORD_MODE) == ERROR)
		return ERROR;

	// wrt return data as unlock value
	u32_write = GET_WRT_UNLOCK;
	if (handle_ic_write(BOOT_RET_DATA_ADDR, 4, (unsigned char *)&u32_write, g_u8_drv_interface, I2C_WORD_MODE) == ERROR)
		return ERROR;

	// clr sync_data(0x20000200) = 0 as finish
	u32_write = 0x00000000;
	if (handle_ic_write(BOOT_SYNC_DATA_ADDR, 4, (unsigned char *)&u32_write, g_u8_drv_interface, I2C_WORD_MODE) == ERROR)
		return ERROR;

	// wait erase/wrt finish
	// comfirm burning_state reault(gu8I2CSyncData.burning_state=BURNING_WRT_FLASH_FINISH at 0x2000020C)
	if (wait_fw_state_rm6d031(BOOT_BURNING_STATE_ADDR, BURNING_WRT_FLASH_FINISH, 8, u16_retry_time) == ERROR) {
		DEBUGOUT("bootloader wrt empty flash fail\r\n");
		DEBUGOUT("bootloader comfirm burning_state reault(0x2000020C = 0x06) fail\r\n");
		return ERROR;
	}

	if (u8_type == USEFW_AREA || u8_type == COMP_AREA) {
		// clr sync_data(0x20000200) = 0 as finish
		u32_write = 0x00000000;
		if (handle_ic_write(BOOT_SYNC_DATA_ADDR, 4, (unsigned char *)&u32_write, g_u8_drv_interface, I2C_WORD_MODE) == ERROR)
			return ERROR;
		hal_delay_ms(35);

		if (wait_fw_state_rm6d031(BOOT_RET_DATA_ADDR, 0x82, 2, u16_retry_time) == ERROR) {
			DEBUGOUT("bootloader wrt empty flash fail\r\n");
			DEBUGOUT("bootloader Confirm g_u8_sync_data.ret_data at 0x20000214 is 0x82 fail\r\n");
			return ERROR;
		}

		// Set test_mode = 1;
		u32_write = 0x00000001;
		if (handle_ic_write(BOOT_TEST_MODE_ADDR, 4, (unsigned char *)&u32_write, g_u8_drv_interface, I2C_WORD_MODE) == ERROR)
			return ERROR;

		// wait crc check finish
		// comfirm gu8I2CSyncData.normal_state=NORMAL_CRC_NOTIFY at 0x20000208)
		if (wait_fw_state_rm6d031(BOOT_NORMAL_STATE_ADDR, NORMAL_CRC_NOTIFY, 2, u16_retry_time) == ERROR) {
			DEBUGOUT("bootloader wrt empty flash fail\r\n");
			DEBUGOUT("bootloader wait crc check finish fail\r\n");
			return ERROR;
		}

		//comfirm crc check result
		// comfirm gu8I2CSyncData.ret_data=CRC_CHECK_PASS at 0x20000214)
		if (wait_fw_state_rm6d031(BOOT_RET_DATA_ADDR, CRC_CHECK_PASS, 5, u16_retry_time) == ERROR) {
			DEBUGOUT("bootloader wrt empty flash fail\r\n");
			DEBUGOUT("bootloader comfirm crc check result fail\r\n");
			return ERROR;
		}
	}

	return SUCCESS;
}

unsigned char check_cc_bl_flag_rm6d031(void)
{
	unsigned char u8_read_buf[4];
	unsigned char u8_is_cc_ready = 0;
	unsigned char u8_fw_version[4];
	short i16_time_out;

	DEBUGOUT("[CCBF] Check CC BL Flag\r\n");

	if (handle_ic_read(FW_FT_FW_VERSION, 4, u8_fw_version, g_u8_drv_interface, I2C_BYTE_MODE) == ERROR)
		return ERROR;

	i16_time_out = 20;
	while (i16_time_out--) {
		if (!u8_is_cc_ready) {
			if (handle_ic_read(PRAM_ADDR_CC_INFO, 4, u8_read_buf, g_u8_drv_interface, I2C_BYTE_MODE) == ERROR)
				return ERROR;
			if ((u8_read_buf[0] >= 1) && (u8_read_buf[2] == u8_fw_version[2]) && (u8_read_buf[3] == u8_fw_version[3]))
				u8_is_cc_ready = 1;
		}

		if (u8_is_cc_ready)
			break;
		hal_delay_ms(1);
	}

	//Check flag
	if (i16_time_out == IC_TEST_TIME_OUT) {
		DEBUGOUT("CC Flag=%d NG!!\r\n", u8_read_buf[0]);
		DEBUGOUT("FW Ver=%X.%X:%X.%X NG!!\r\n", u8_fw_version[2], u8_fw_version[3], u8_read_buf[2], u8_read_buf[3]);
		return ERROR;
	}

	return SUCCESS;
}

unsigned char burn_cc_to_ic_flash_rm6d031(void)
{
	short i16_time_out = 1000;
	unsigned int u32_cc_table;
	unsigned char u8_fw_value[4];
	unsigned int u32_write = 0;


#if ENABLE_TEST_TIME_MEASUREMENT_CC
	DEBUGOUT("[BCTF] Before Switch to Bootloader\r\n");
	g_u32_spend_time = hal_get_system_time();
#endif

	g_u32_pram_boot_addr = PRAM_BOOT_START;
	g_u32_pram_tb_exe_addr = PRAM_TB_EXE_START;
	g_u32_pram_init_addr = DG_EXT_FLASH_INIT_START;
	g_u32_pram_fw_addr = PRAM_FW_START;
	g_u32_pram_para_addr = PRAM_PARA_START;
	g_u32_test_fw_addr = PRAM_SIZE;


	//Set Skip_Load = 1
	u32_write = (BL_CRC_CHK | SKIP_LOAD);
	if (handle_ic_write(REG_FLASHCTL_FLASH_STATE_REG_ADDR, 4, (unsigned char *)&u32_write, g_u8_drv_interface, I2C_WORD_MODE) == ERROR)
		return ERROR;

	//wait sw rst finish
	u32_write = BLKRST_SW_RST;
	if (handle_ic_write(REG_SYSCON_BLKRST_ADDR, 4, (unsigned char *)&u32_write, g_u8_drv_interface, I2C_WORD_MODE) == ERROR)
		return ERROR;
	hal_delay_ms(2);

	if (wait_fw_state_rm6d031(BOOT_RET_DATA_ADDR, WAIT_TEST_MODE, 1, i16_time_out) == ERROR) {
		DEBUGOUT("[BCTF] Check Burn CC & BL Fail\r\n");
		return ERROR;
	}

#if ENABLE_TEST_TIME_MEASUREMENT_CC
	DEBUGOUT("[BCTF] End of Switch to Bootloader = %d\r\n", hal_get_system_time() - g_u32_spend_time);
	g_u32_spend_time = hal_get_system_time();
	DEBUGOUT("TICK=%d\r\n", hal_get_system_time());
#endif

		//Read Flash CC Table

		//Read PRAM FW Version


	if (burn_to_ic_flash_rm6d031(COMP_AREA) == ERROR) {
		DEBUGOUT("[BCTF] Burn CC & BL Fail\r\n");
		return ERROR;
	}

	u32_write = 0x00000000;
	if (handle_ic_write(BOOT_TEST_MODE_ADDR, 4, (unsigned char *)&u32_write, g_u8_drv_interface, I2C_WORD_MODE) == ERROR)
		return ERROR;

	// clr sync_data(0x20000200) = 0 as finish
	u32_write = 0x00000000;
	if (handle_ic_write(BOOT_SYNC_DATA_ADDR, 4, (unsigned char *)&u32_write, g_u8_drv_interface, I2C_WORD_MODE) == ERROR)
		return ERROR;
	hal_delay_ms(10);

	if (wait_fw_state_rm6d031(REG_FLASHCTL_FLASH_STATE_REG_ADDR, 0x4000, 2, i16_time_out) == ERROR) {
		DEBUGOUT("[BCTF] Check Burn CC & BL Fail\r\n");
		return ERROR;
	}

#if ENABLE_FW_UPDATE_VERIFY_LOG
	if (read_ic_ext_flash_rm6d031((unsigned int)(FLASH_FW_START + PRAM_FW_LENGTH - 4), &u32_i2c_read_data))
		DEBUGOUT("Normal FW CRC=0x%08X\r\n", u32_i2c_read_data);
#endif

#if ENABLE_TEST_TIME_MEASUREMENT_CC
	DEBUGOUT("BCTF] Burn to flash finish[Tick=%d]!\r\n", hal_get_system_time() - g_u32_spend_time);
#endif

	//Read Flash CC Table
	if (read_ic_ext_flash_rm6d031(FLASH_NORMAL_FW_CC_TABLE_ADDR, &u32_cc_table) == ERROR)
		return ERROR;
	DEBUGOUT("Flash Do CC FW Version=0x%04X\r\n", (u32_cc_table & 0xFFFF0000) >> 16);
	DEBUGOUT("Flash Do CC Flag=0x%X\r\n", (u32_cc_table & 0x0000FFFF));

	//Read PRAM FW Version
	if (handle_ic_read(FW_FT_SRAM_FW_VERSION, 4, u8_fw_value, g_u8_drv_interface, I2C_BYTE_MODE) == ERROR)
		return ERROR;
	DEBUGOUT("SRAM FW Version=0x%03x,0x%03x\r\n", u8_fw_value[3], u8_fw_value[2]);

	if (((u32_cc_table & 0xFF000000) >> 24) != u8_fw_value[3] || ((u32_cc_table & 0x00FF0000) >> 16) != u8_fw_value[2]) {
		DEBUGOUT("Flash CC Table FW Version is not match!\r\n");
		return ERROR;
	}


	return SUCCESS;

}

unsigned char burn_ext_flash_from_image_rm6d031(void)
{
	short i16_retry_time = 1000;
	unsigned int u32_read_data = 0;
	unsigned short u16_retry;
	unsigned int u32_write = 0;
//	unsigned char read[4];
#if ENABLE_TEST_TIME_MEASUREMENT
	g_u32_spend_time = hal_get_system_time();
	DEBUGOUT("Burn FW Start Time= %d\r\n", g_u32_spend_time);
#else
	DEBUGOUT("start bootloader wrt empty flash(%llu)\r\n", hal_get_system_time());
#endif

	g_u32_pram_boot_addr = PRAM_BOOT_START;
	g_u32_pram_tb_exe_addr = PRAM_TB_EXE_START;
	g_u32_pram_init_addr = DG_EXT_FLASH_INIT_START;
	g_u32_pram_fw_addr = PRAM_FW_START;
	g_u32_pram_para_addr = PRAM_PARA_START;
	g_u32_test_fw_addr = PRAM_SIZE;

	if (stop_mcu_rm6d031(1) == ERROR)
		return ERROR;

	if ((g_st_test_info.u16_ft_eng_item & IC_TEST_ENG_ITEMS_SET_DSP_INIT_AF_TP_RESET)) {
		//Set Display Initial & Normal On
		DEBUGOUT("Set Display Initial & Normal On\r\n");
		if (!handle_ic_set_initial_code())
			DEBUGOUT("Set Display Initial Fail\r\n");
	}

	//WRT boot-loader to PRAM first
	u32_write = (USEFW_LOCK | CONFIG_LOCK | COMP_LOCK | BASEL_LOCK);
	if (handle_ic_write(REG_FLASHCTL_FLASH_PRAM_LOCK, 4, (unsigned char *)&u32_write, g_u8_drv_interface, I2C_WORD_MODE) == ERROR)
		return ERROR;

#if ENABLE_FW_UPDATE_VERIFY_LOG
	DEBUGOUT("Write Bootloader to PRAM\r\n");
#endif
	//read image data and write to pram (Bootloader)
	if (hal_read_image_fw_to_pram(TEST_BL_IMG, PRAM_BOOT_LENGTH, PRAM_BOOT_START) == ERROR) {
		DEBUGOUT("confirm crc error!!\r\n");
		return ERROR;
	}

	//check pram bootloader crc
	if (check_pram_crc32_rm6d031(PRAM_BOOT_START, PRAM_BOOT_CRC_LENGTH) == ERROR) {
		DEBUGOUT("confirm bootloader crc error!!\r\n");
		return ERROR;
	}

	//read image data and write to pram (TB execution)
	if (hal_read_image_fw_to_pram(TEST_TB_EXE_IMG, PRAM_TB_EXE_LENGTH, PRAM_TB_EXE_START) == ERROR) {
		DEBUGOUT("confirm crc error!!\r\n");
		return ERROR;
	}

	//check pram Initial code crc
	if (check_pram_crc32_rm6d031(PRAM_TB_EXE_START, PRAM_TB_EXE_CRC_LENGTH) == ERROR) {
		DEBUGOUT("confirm init crc error!!\r\n");
		return ERROR;
	}

	u32_write = (BL_CRC_CHK | SKIP_LOAD);
	if (handle_ic_write(REG_FLASHCTL_FLASH_STATE_REG_ADDR, 4, (unsigned char *)&u32_write, g_u8_drv_interface, I2C_WORD_MODE) == ERROR)
		return ERROR;

	//wait sw rst finish
	u32_write = BLKRST_SW_RST;
	if (handle_ic_write(REG_SYSCON_BLKRST_ADDR, 4, (unsigned char *)&u32_write, g_u8_drv_interface, I2C_WORD_MODE) == ERROR)
		return ERROR;
	hal_delay_ms(35);

	//DEBUGOUT("BOOT_RET_DATA_ADDR : %X\r\n", BOOT_RET_DATA_ADDR);

	if (wait_fw_state_rm6d031(BOOT_RET_DATA_ADDR, 0x82, 2, i16_retry_time) == ERROR) {
		DEBUGOUT("bootloader wrt empty flash fail\r\n");
		DEBUGOUT("bootloader Confirm g_u8_sync_data.ret_data at 0x20000214 is 0x82 fail\r\n");
		return ERROR;
	}

	// bootloader start
	// Start from bootloader
	if (burn_to_ic_flash_rm6d031(BOTLR_AREA) == ERROR) {
		DEBUGOUT("burn_to_ic_flash burn bootloade fail\r\n");
		return ERROR;
	}
	if (burn_to_ic_flash_rm6d031(USEFW_AREA) == ERROR) {
		DEBUGOUT("burn_to_ic_flash burn fw fail\r\n");
		return ERROR;
	}

	u32_write = 0x00000000;
	if (handle_ic_write(BOOT_TEST_MODE_ADDR, 4, (unsigned char *)&u32_write, g_u8_drv_interface, I2C_WORD_MODE) == ERROR)
		return ERROR;

	// clr sync_data(0x20000200) = 0 as finish
	u32_write = 0x00000000;
	if (handle_ic_write(BOOT_SYNC_DATA_ADDR, 4, (unsigned char *)&u32_write, g_u8_drv_interface, I2C_WORD_MODE) == ERROR)
		return ERROR;
	hal_delay_ms(10);

	/*wait bootloader jump to fw*/
	u16_retry = i16_retry_time;
	do {
		if (handle_ic_read(REG_FLASHCTL_FLASH_STATE_REG_ADDR, 4, (unsigned char *)(&u32_read_data), g_u8_drv_interface, I2C_WORD_MODE) == ERROR)
			return ERROR;

		hal_delay_ms(2);
		u16_retry--;
		u32_read_data &= 0x4000;

	} while ((u32_read_data != 0x4000) && (u16_retry != 0));

	if (u32_read_data != 0x4000) {
		DEBUGOUT("bootloader wrt empty flash fail\r\n");
		return ERROR;
	}

#if ENABLE_TEST_TIME_MEASUREMENT
	DEBUGOUT("Burn FW Spend Time= %d\r\n", hal_get_system_time() - g_u32_spend_time);
#else
	DEBUGOUT("bootloader wrt empty flash finish!(%llu)\r\n", hal_get_system_time());
#endif

	return SUCCESS;
}

unsigned char burn_pram_from_image_rm6d031(void)
{
	short i16_retry_time = 1000;
	unsigned int u32_data = 0;
	unsigned char u8_check = 0;

#if ENABLE_TEST_TIME_MEASUREMENT
	g_u32_spend_time = hal_get_system_time();
	DEBUGOUT("Burn FW to pram Start Time= %d\r\n", g_u32_spend_time);
#else
	DEBUGOUT("start Burn fw to pram (%llu)\r\n", hal_get_system_time());
#endif

	g_u32_pram_boot_addr = PRAM_BOOT_START;
	g_u32_pram_tb_exe_addr = PRAM_TB_EXE_START;
	g_u32_pram_init_addr = DG_EXT_FLASH_INIT_START;
	g_u32_pram_fw_addr = PRAM_FW_START;
	g_u32_pram_para_addr = PRAM_PARA_START;
	g_u32_test_fw_addr = PRAM_SIZE;

	if (stop_mcu_rm6d031(0) == ERROR)
		return ERROR;

	//Unlock PRAM
	u32_data = 0x00000000;
	if (handle_ic_write(REG_FLASHCTL_FLASH_PRAM_LOCK, 4, (unsigned char *)&u32_data, g_u8_drv_interface, I2C_WORD_MODE) == ERROR)
		return ERROR;

	//Unlock SPI wrt to PRAM
	if (unlock_spi_wrt_to_pram_rm6d031(g_u8_drv_interface) == ERROR)
		return ERROR;

	//write bootloader to PRAM
	if (hal_read_image_fw_to_pram(TEST_BL_IMG, PRAM_BOOT_LENGTH, PRAM_BOOT_START) == ERROR) {
		DEBUGOUT("write bootloader to pram error!!\n");
		return ERROR;
	}
	//check pram bootloader crc
	if (check_pram_crc32_rm6d031(PRAM_BOOT_START, PRAM_BOOT_CRC_LENGTH) == ERROR) {
		DEBUGOUT("confirm bootloader crc error!!\n");
		return ERROR;
	}

	//write FW & PARA to PRAM
	if ((hal_read_image_fw_to_pram(TEST_USER_FW_IMG, PRAM_FW_LENGTH, PRAM_FW_START) == ERROR) ||
	    (hal_read_image_fw_to_pram(TEST_USER_PARA_IMG, PRAM_PARA_LENGTH, PRAM_PARA_START) == ERROR)) {
		DEBUGOUT("write FW & PARA to PRAM error!!\r\n");
		return ERROR;
	}
	//check pram FW & PARA  crc
	if (check_pram_crc32_rm6d031(PRAM_FW_START, PRAM_FW_CRC_LENGTH) == ERROR) {
		DEBUGOUT("confirm FW & PARA crc error!!\r\n");
		return ERROR;
	}

	//read image data and write to pram (Initial_code)
	if (hal_read_image_fw_to_pram(TEST_TB_EXE_IMG, PRAM_TB_EXE_LENGTH, PRAM_TB_EXE_START) == ERROR) {
		DEBUGOUT("confirm crc error!!\r\n");
		return ERROR;
	}

	//check pram Initial code crc
	if (check_pram_crc32_rm6d031(PRAM_TB_EXE_START, PRAM_TB_EXE_CRC_LENGTH) == ERROR) {
		DEBUGOUT("confirm init crc error!!\r\n");
		return ERROR;
	}

	//clear Pram CC value
	if (clear_pram_cc_bl_rm6d031(PRAM_COMP_START, PRAM_COMP_LENGTH) == ERROR) {
		DEBUGOUT("clear pram CC error!!\r\n");
		return ERROR;
	}

	u32_data = 0x8F7B8A98;
	if (handle_ic_write(REG_FLASHCTL_FLASH_PRGCHKSUM_ADDR, 4, (unsigned char *)&u32_data, g_u8_drv_interface, I2C_WORD_MODE) == ERROR)
		return ERROR;

	if (handle_ic_read(REG_FLASHCTL_FLASH_ENG3, 4, (unsigned char *)(&u32_data), g_u8_drv_interface, I2C_WORD_MODE) == ERROR)
		return ERROR;

	u32_data |= 0x81000000;
	if (handle_ic_write(REG_FLASHCTL_FLASH_ENG3, 4, (unsigned char *)&u32_data, g_u8_drv_interface, I2C_WORD_MODE) == ERROR)
		return ERROR;

	while (check_register_status_rm6d031(REG_FLASHCTL_FLASH_ENG3, 0x81000000, 0x80000000, &u8_check) && i16_retry_time--) {
		if (u8_check)
			break;
		hal_delay_ms(1);
	}

	if (handle_ic_read(REG_FLASHCTL_FLASH_PRGCHKSUM_RESULT, 4, (unsigned char *)(&u32_data), g_u8_drv_interface, I2C_WORD_MODE) == ERROR)
		return ERROR;

	if (handle_ic_write(PRAM_CC_CRC_START, 4, (unsigned char *)&u32_data, g_u8_drv_interface, I2C_WORD_MODE) == ERROR)
		return ERROR;

	//set skip & FLASHLESS_MODE (bootloader skip check crc)
	u32_data = (FLASHLESS_MODE | SKIP_LOAD);
	if (handle_ic_write(REG_FLASHCTL_FLASH_STATE_REG_ADDR, 4, (unsigned char *)&u32_data, g_u8_drv_interface, I2C_WORD_MODE) == ERROR)
		return ERROR;

	//wait sw rst finish
	u32_data = BLKRST_SW_RST;
	if (handle_ic_write(REG_SYSCON_BLKRST_ADDR, 4, (unsigned char *)&u32_data, g_u8_drv_interface, I2C_WORD_MODE) == ERROR)
		return ERROR;
	hal_delay_ms(2);

	/*wait bootloader jump to fw*/
	i16_retry_time = 50;
	do {
		if (handle_ic_read(REG_FLASHCTL_FLASH_STATE_REG_ADDR, 4, (unsigned char *)(&u32_data), g_u8_drv_interface, I2C_WORD_MODE) == ERROR)
			return ERROR;

		hal_delay_ms(2);
		i16_retry_time--;
		u32_data &= 0x405B;

	} while ((u32_data != 0x405B) && (i16_retry_time != 0));

	if (u32_data != 0x405B) {
		DEBUGOUT("Burn fw to pram fail\r\n");
		return ERROR;
	}

#if ENABLE_TEST_TIME_MEASUREMENT
	DEBUGOUT("Burn FW to pram Spend Time= %d\r\n", hal_get_system_time() - g_u32_spend_time);
#else
	DEBUGOUT("Burn fw to pram finish!(%llu)\r\n", hal_get_system_time());
#endif

	return SUCCESS;
}

unsigned char do_bl_calibration_rm6d031(void)
{
	unsigned char u8_value[4] = {0};
	signed short i16_time_out = 0;
	signed char i8_retry = 3;

	while (i8_retry--) {
		u8_value[0] = SYS_CMD_DO_BL_CAL;
		if (handle_ic_write(FW_SYS_CMD_ADDR, 1, u8_value, g_u8_drv_interface, I2C_BYTE_MODE) == ERROR) { //Trigger Calibration
			DEBUGOUT("[DBLC] Set BL Cal CMD Retry (%d)\r\n", i8_retry);
			continue;
		}

		i16_time_out = 400;
		// Wait calibration ready
		while (i16_time_out--) {
			hal_delay_ms(10);
			if (handle_ic_read(FW_SYS_CMD_ADDR, 4, u8_value, g_u8_drv_interface, I2C_BYTE_MODE) == ERROR) {
				DEBUGOUT("[DBLC] Set BL Cal CMD Retry (%d)\r\n", i8_retry);
				i16_time_out = -1;
				break;
			} else if (u8_value[0] == 0) {
				break;
			}
		}

		if (i16_time_out == IC_TEST_TIME_OUT) {
			if (i8_retry) {
				DEBUGOUT("[DBLC] Do BL Cal Retry (%d)\r\n", i8_retry);
				hardware_reset_rm6d031(TRUE);
				hal_delay_ms(20);
				continue;
			} else {
				DEBUGOUT("[DBLC] Do BL Cal NG(%d)\r\n", i8_retry);
				continue;
			}
		} else {
			if (check_cc_bl_flag_rm6d031() == ERROR) {
				if (i8_retry) {
					DEBUGOUT("[DBLC] BL Flag NG Retry (%d)\r\b", i8_retry);
					hardware_reset_rm6d031(TRUE);
					hal_delay_ms(20);
					continue;
				} else {
					DEBUGOUT("[DBLC] BL Flag NG\r\n");
					continue;
				}
			} else //Calibration OK
				break;
		}
	}

	if (i8_retry == IC_TEST_TIME_OUT)
		return ERROR;
	DEBUGOUT("[DBLC] BL Cal Finished\r\n");
	return SUCCESS;
}

unsigned char do_calibration_rm6d031(unsigned char u8_do_calibration_cmd, unsigned char u8_burn_flash)
{
	unsigned char u8_value[4];
	short u8_time_out = 100;
	unsigned char u8_retry = IC_TEST_RETRY_TIME;

	g_u32_wearable_test_result &= ~(WEARABLE_FT_TEST_RESULT_CC_CALIBRATION_NG | WEARABLE_FT_TEST_RESULT_BURN_CC_NG);

	if (u8_do_calibration_cmd == TRUE) {

#if ENABLE_TEST_TIME_MEASUREMENT_CC
		DEBUGOUT("[DOCC] Start do calibration (%d)\r\n", hal_get_system_time());
		g_u32_spend_time = hal_get_system_time();
#endif
		while (u8_retry--) {
			//Trigger Calibration
			u8_value[0] = SYS_CMD_DO_CC_CAL;
			if (handle_ic_write(FW_SYS_CMD_ADDR, 1, u8_value, g_u8_drv_interface, I2C_BYTE_MODE) == ERROR)
				return ERROR;

			u8_time_out = 400;

			// Wait calibration ready
			while (u8_time_out--) {
				hal_delay_ms(10);//20
				if (handle_ic_read(FW_SYS_CMD_ADDR, 4, u8_value, g_u8_drv_interface, I2C_BYTE_MODE) == ERROR)
					return ERROR;
				if (u8_value[0] == 0)
					break;
			}

			if (u8_time_out == IC_TEST_TIME_OUT) {
				if (u8_retry) {
					DEBUGOUT("[DOCC] Do CC Retry (%d,%x)", u8_retry, *u8_value);
					hardware_reset_rm6d031(TRUE);
					hal_delay_ms(20);
					continue;
				} else {
					g_u32_wearable_test_result |= WEARABLE_FT_TEST_RESULT_CC_CALIBRATION_NG;
					DEBUGOUT("[DOCC] SYS CMD CC NG\r\n");
					return ERROR;
				}
			} else {
				if (check_cc_bl_flag_rm6d031() == ERROR) {
					if (u8_retry) {
						DEBUGOUT("[DOCC] CC Flag Retry (%d)", u8_retry);
						hardware_reset_rm6d031(TRUE);
						hal_delay_ms(20);
						continue;
					} else {
						g_u32_wearable_test_result |= WEARABLE_FT_TEST_RESULT_CC_CALIBRATION_NG;
						DEBUGOUT("[DOCC] CC Flag NG\r\n");
						return ERROR;
					}
				} else //Calibration OK
					break;
			}
		}
#if ENABLE_TEST_TIME_MEASUREMENT_CC
		DEBUGOUT("[DOCC] Do CC Finish! [%d:%d]\r\n", hal_get_system_time() - g_u32_spend_time, u8_time_out);
		g_u32_spend_time = hal_get_system_time();
#endif
	}

	if (u8_burn_flash == FALSE) {
		DEBUGOUT("[DOCC] Do Calibration Finish \r\n");
		return SUCCESS;
	}

	if (burn_cc_to_ic_flash_rm6d031() == ERROR) {
		DEBUGOUT("[DOCC] Burn to flash NG!\r\n");
		hardware_reset_rm6d031(TRUE);
		g_u32_wearable_test_result |= WEARABLE_FT_TEST_RESULT_BURN_CC_NG;
		return ERROR;
	}
	if ((g_st_test_info.u16_ft_test_info_1) == WEARBLE_FT_BURN_CC_SYSTEM_NG_CASE) {
		g_u32_wearable_test_result = WEARABLE_FT_TEST_RESULT_BURN_CC_NG;
		return ERROR;
	}

#if ENABLE_TEST_TIME_MEASUREMENT_CC
	DEBUGOUT("[DOCC] Burn CC Finish (%d)\r\n", hal_get_system_time() - g_u32_spend_time);
#endif

	DEBUGOUT("[DOCC] Burn CC Finish \r\n");
	return SUCCESS;
}

unsigned char check_pram_crc32_rm6d031(unsigned int u32_addr, unsigned int u32_len)
{
	short u16_time_out = 1000;
	unsigned int u32_end_addr = u32_addr + u32_len - 1;
	unsigned int u32_crc_addr = u32_addr + u32_len;
	unsigned int u32_read_data, u32_crc_result;
	unsigned char u8_check = 0;

	u32_addr |= (u32_end_addr << 16);

	//check pram bootloader crc
	if (handle_ic_write(REG_FLASHCTL_FLASH_PRGCHKSUM_ADDR, 4, (unsigned char *)&u32_addr, g_u8_drv_interface, I2C_WORD_MODE) == ERROR)
		return ERROR;

	if (handle_ic_read(REG_FLASHCTL_FLASH_ENG3, 4, (unsigned char *)(&u32_read_data), g_u8_drv_interface, I2C_WORD_MODE) == ERROR)
		return ERROR;

	u32_read_data |= 0x81000000;
	if (handle_ic_write(REG_FLASHCTL_FLASH_ENG3, 4, (unsigned char *)&u32_read_data, g_u8_drv_interface, I2C_WORD_MODE) == ERROR)
		return ERROR;

	while (check_register_status_rm6d031(REG_FLASHCTL_FLASH_ENG3, 0x81000000, 0x80000000, &u8_check) && u16_time_out--) {
		if (u8_check)
			break;
		hal_delay_ms(1);
	}

	if (handle_ic_read(REG_FLASHCTL_FLASH_PRGCHKSUM_RESULT, 4, (unsigned char *)(&u32_crc_result), g_u8_drv_interface, I2C_WORD_MODE) == ERROR)
		return ERROR;

	if (handle_ic_read(u32_crc_addr, 4, (unsigned char *)(&u32_read_data), g_u8_drv_interface, I2C_WORD_MODE) == ERROR)
		return ERROR;

	if (u32_read_data != u32_crc_result) {
		DEBUGOUT("Confirm pram crc fail. addr: %x, read crc : %x, crc result : %x\r\n",
			 u32_addr, u32_read_data, u32_crc_result);
		return ERROR;
	}

	return SUCCESS;
}
