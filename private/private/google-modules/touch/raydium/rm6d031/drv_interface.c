// SPDX-License-Identifier: GPL-2.0+
/*
 * Raydium TouchScreen driver.
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.

 * Copyright (c) 2021  Raydium tech Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * BSD LICENSE
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *  * Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *  * Neither the name of Google Inc. or Linaro Ltd. nor the names of
 *    its contributors may be used to endorse or promote products
 *    derived from this software without specific prior written
 *    permission.
 * * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
 * IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifdef __KERNEL__
#include <linux/delay.h>
#include <linux/mutex.h>
#include <linux/unistd.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/timer.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#else
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#endif
#include "hal_interface.h"
#include "drv_interface.h"
#include "ic_drv_global.h"
#include "ic_drv_interface.h"

/*I2C */
unsigned char g_u8_i2c_tx_buf[256]; /* I2C Primary Data Buffer */
unsigned char g_u8_enable_i2c_retry;
unsigned char g_u8_mute_i2c_err_log;
unsigned char g_u8_i2c_nid;
unsigned char g_u8_i2c_try_nid[2];

/*SPI*/
unsigned char g_u8_spi_m_buf[2][140]; /* SPI Primary Data Buffer */
unsigned char g_u8_enable_spi_retry;
unsigned char g_u8_initial_code_buf[56];
unsigned long g_u32_save_config[64];


unsigned char i2c_set_pda_address(unsigned int u32_addr, unsigned char u8_mode)
{
	int tmp = 0;
	unsigned char u8_retry_count = 3;

	if (g_u8_enable_i2c_retry)
		u8_retry_count = I2C_RETRY_TIME;
	else
		u8_retry_count = 1;

	g_u8_i2c_tx_buf[0] = (unsigned char) u32_addr;
	g_u8_i2c_tx_buf[1] = (unsigned char)(u32_addr >> 8);
	g_u8_i2c_tx_buf[2] = (unsigned char)(u32_addr >> 16);
	g_u8_i2c_tx_buf[3] = u8_mode;

	while (u8_retry_count > 0) {
		tmp = hal_i2c_write(I2C_EID, (const uint8_t *)g_u8_i2c_tx_buf, I2C_PDA_LEN);
		if (tmp == I2C_PDA_LEN)
			break;
		if (!g_u8_mute_i2c_err_log)
			DEBUGOUT("[I2CSPDA] error retry (%d)!! Addr=0x%04X, Mode=0x%02X\r\n",
				 u8_retry_count, u32_addr, u8_mode);
		u8_retry_count--;
	}

	if (tmp != I2C_PDA_LEN) {
		if (!g_u8_mute_i2c_err_log)
			DEBUGOUT("[I2CSPDA] Set PDA Fail !! Addr=0x%04X, Mode=0x%02X\r\n", u32_addr, u8_mode);
		return ERROR;
	}
	return SUCCESS;
}

unsigned char i2c_read_pda(unsigned int u32_addr, unsigned char u16_read_len, unsigned char *p_u8_output_buf)
{
	unsigned char u8_result = ERROR;
	unsigned short u16_rx_sz = 0;
	unsigned char u8_retry_count = 3;
	unsigned char u8_i2c_tx_cmd;
	/* Start I2C secondary device first */

	if (p_u8_output_buf == NULL) {
		DEBUGOUT("[I2CRPDA] I2C read buffer is null!!\r\n");
		return ERROR;
	}
	if (u16_read_len == 0 || u16_read_len > 255) {
		DEBUGOUT("[I2CRPDA] Length(%d) of I2C read is error!!\r\n", u16_read_len);
		return ERROR;
	}

	u8_i2c_tx_cmd = (unsigned char)u32_addr;

	u8_result = i2c_set_pda_address((u32_addr >> 8), I2C_BYTE_MODE);

	if (g_u8_enable_i2c_retry)
		u8_retry_count = I2C_RETRY_TIME;
	else
		u8_retry_count = 1;

	if (u8_result == SUCCESS) {
		while (u8_retry_count > 0) {
			u16_rx_sz = hal_i2c_cmd_read(g_u8_i2c_nid, &u8_i2c_tx_cmd, 1,
						     p_u8_output_buf, u16_read_len);

			if (u16_rx_sz == u16_read_len)
				break;
			DEBUGOUT("[I2CRPDA] hal_i2c_cmd_read Retry=%d,All=0x%x\r\n", u8_retry_count, u32_addr);
			u8_retry_count--;
		}

		u8_result = (u16_rx_sz == u16_read_len) ? SUCCESS : ERROR;

		if (u8_result == ERROR)
			DEBUGOUT("[I2CRPDA] Read (0x%x:) error:%d,T:%d\r\n", u32_addr, u16_rx_sz, u16_read_len);

	} else {
		DEBUGOUT("[I2CRPDA] Set PDA address [0x%x] error !\r\n", u32_addr);
	}

	if (u8_result == ERROR) {
		p_u8_output_buf[0] = 0;
		DEBUGOUT("[I2CRPDA] Read ERR [%x:%d]\r\n", u32_addr, u16_read_len);
	}

	return u8_result;
}

/*
 *   PDA burst write
 */
unsigned char i2c_write_pda(unsigned int u32_addr, unsigned char u8_write_len, unsigned char *p_u8_data_buf, unsigned char u8_trans_mode)
{
	unsigned char u8_result = ERROR;
	unsigned char u8_tx_len = u8_write_len + 1;
	unsigned char u8_tmp = 255, u8_retry_count = 3;

	if (u8_write_len == 0 || u8_write_len > 124) {
		DEBUGOUT("[I2CBW] Length(%d) of I2C write is error!!\r\n", u8_write_len);
		return ERROR;
	}

	//Parameter take BYTE MODE
	if (u8_trans_mode != I2C_BYTE_MODE) {
		//I2C access for register need to word mode
		if ((u32_addr & 0x40000000) && ((u8_write_len < 4) || (u8_write_len & 0x03) || (u32_addr & 0x00000003))) {
			DEBUGOUT("[I2CBW] I2C write of register is not word align(0x%x,%d,0x%x)!!\r\n", u32_addr, u8_write_len, u8_trans_mode);
			return ERROR;
		}

		if (u8_write_len >= 4) {
			if ((u32_addr & 0x00000003) || (u8_write_len & 0x03)) {
				DEBUGOUT("[I2CBW] Length >= 4 and not word align!! (0x%x,%d,0x%x) \r\n", u32_addr, u8_write_len, u8_trans_mode);
				return ERROR;
			}
			//Force to I2C WORD mode
			u8_trans_mode = I2C_WORD_MODE;
		}
	}

	/* Start I2C secondary device first */
	u8_result = i2c_set_pda_address((u32_addr >> 8), u8_trans_mode);

	if (g_u8_enable_i2c_retry)
		u8_retry_count = I2C_RETRY_TIME;
	else
		u8_retry_count = 1;

	if (u8_result == SUCCESS) {
		g_u8_i2c_tx_buf[0] = (unsigned char)(u32_addr); // last byte of address
		memcpy((void *)(g_u8_i2c_tx_buf + 1), p_u8_data_buf, u8_write_len);
		while (u8_retry_count > 0) {
			u8_tmp = hal_i2c_write(g_u8_i2c_nid, (const uint8_t *)g_u8_i2c_tx_buf, u8_tx_len);

			if (u8_tmp == u8_tx_len)
				break;

			if (!g_u8_mute_i2c_err_log)
				DEBUGOUT("[I2CBW] Write Retry=%d,All=0x%x[0x%x]{%x}\r\n", u8_retry_count, u32_addr, g_u8_i2c_nid, u8_trans_mode);
			u8_retry_count--;
		}

		u8_result = (u8_tmp == u8_tx_len) ? SUCCESS : ERROR;
	} else {
		if (!g_u8_mute_i2c_err_log)
			DEBUGOUT("[I2CBW] Set PDA address [0x%x] error !\r\n", u32_addr);
	}

	return u8_result;
}

unsigned char i2c_read_pda_via_pda2(unsigned int u32_addr, unsigned short u16_read_len, unsigned char *p_u8_output_buf)
{
	unsigned char u8_result = ERROR;
	unsigned short u16_rx_sz = 0;
	unsigned char u8_retry_count = 3;
	/* Start I2C secondary device first */

	if (p_u8_output_buf == NULL) {
		DEBUGOUT("[[I2CR@PDA2] I2C read buffer is null!!\r\n");
		return ERROR;
	}

	if (u16_read_len == 0 || u16_read_len > 255) {
		DEBUGOUT("[I2CR@PDA2] Length(%d) of I2C write is error!!\r\n", u16_read_len);
		return ERROR;
	}

	g_u8_i2c_tx_buf[0] = I2C_PDA_CMD;			//pda_addr_cmd
	g_u8_i2c_tx_buf[1] = (unsigned char)u32_addr;
	g_u8_i2c_tx_buf[2] = (unsigned char)(u32_addr >> 8);
	g_u8_i2c_tx_buf[3] = (unsigned char)(u32_addr >> 16);
	g_u8_i2c_tx_buf[4] = (unsigned char)(u32_addr >> 24);

	if (u32_addr & 0x40000000)	//Force to I2C WORD mode
		g_u8_i2c_tx_buf[5] = I2C_PDA2_WORD_MODE;	//pda setting
	else
		g_u8_i2c_tx_buf[5] = I2C_PDA2_BYTE_MODE;	//pda setting


	if (g_u8_enable_i2c_retry)
		u8_retry_count = I2C_RETRY_TIME;
	else
		u8_retry_count = 1;

	while (u8_retry_count > 0) {
		u16_rx_sz = hal_i2c_cmd_read(g_u8_i2c_nid, (uint8_t *)g_u8_i2c_tx_buf, 6,
					     p_u8_output_buf, u16_read_len);

		if (u16_rx_sz == u16_read_len)
			break;

		if (!g_u8_mute_i2c_err_log)
			DEBUGOUT("[I2CR@PDA2] hal_i2c_cmd_read Retry=%d,All=0x%x\r\n", u8_retry_count, u32_addr);

		//g_u8_i2c_nid = g_u8_i2c_try_nid[u8_retry_count % 2];

		u8_retry_count--;
	}

	u8_result = (u16_rx_sz == u16_read_len) ? SUCCESS : ERROR;

	if (u8_result == ERROR) {
		p_u8_output_buf[0] = 0;
		if (!g_u8_mute_i2c_err_log)
			DEBUGOUT("[I2CR@PDA2] Read (0x%x:) error:%d,T:%d\r\n", u32_addr, u16_rx_sz, u16_read_len);
	}
	return u8_result;
}

unsigned char i2c_write_pda_via_pda2(unsigned int u32_addr, unsigned char u8_write_len, unsigned char *p_u8_data_buf, unsigned char u8_trans_mode)
{
	unsigned char u8_result = ERROR;
	unsigned char u8_tx_len = u8_write_len + 6;
	unsigned char u8_tmp = 255, u8_retry_count = 3;

	if (u8_write_len == 0 || u8_write_len > 249) {
		DEBUGOUT("[I2CW@PDA2] Length(%d) of I2C write is error!!\r\n", u8_write_len);
		return ERROR;
	}

	//Parameter take BYTE MODE
	if (u8_trans_mode != I2C_BYTE_MODE) {
		//I2C access for register need to word mode
		if ((u32_addr & 0x40000000) && ((u8_write_len < 4) || (u8_write_len & 0x03) || (u32_addr & 0x00000003))) {
			DEBUGOUT("[I2CW@PDA2] I2C write of register is not word align(0x%x,%d,0x%x)!!\r\n", u32_addr, u8_write_len, u8_trans_mode);
			return ERROR;
		}
		if (u8_write_len >= 4) {
			if ((u32_addr & 0x00000003) || (u8_write_len & 0x03)) {
				DEBUGOUT("[I2CW@PDA2] Length >= 4 and not word align!! (0x%x,%d,0x%x) \r\n", u32_addr, u8_write_len, u8_trans_mode);
				return ERROR;
			}
			//Force to I2C WORD mode
			u8_trans_mode = I2C_PDA2_WORD_MODE;
		}
	} else {
		if ((u32_addr & 0x40000000)) {
			if ((u8_write_len >= 4) && ((u8_write_len & 0x03) == 0)) {
				u8_trans_mode = I2C_PDA2_WORD_MODE;
			} else {
				DEBUGOUT("[I2CW@PDA2] I2C write of register is not word align(0x%x,%d,0x%x)!!\r\n", u32_addr, u8_write_len, u8_trans_mode);
				return ERROR;
			}
		} else {
			u8_trans_mode = I2C_PDA2_BYTE_MODE;
		}
	}

	g_u8_i2c_tx_buf[0] = I2C_PDA_CMD;	//pda_addr_cmd
	g_u8_i2c_tx_buf[1] = (unsigned char)u32_addr;
	g_u8_i2c_tx_buf[2] = (unsigned char)(u32_addr >> 8);
	g_u8_i2c_tx_buf[3] = (unsigned char)(u32_addr >> 16);
	g_u8_i2c_tx_buf[4] = (unsigned char)(u32_addr >> 24);
	g_u8_i2c_tx_buf[5] = u8_trans_mode;

	if (g_u8_enable_i2c_retry)
		u8_retry_count = I2C_RETRY_TIME;
	else
		u8_retry_count = 1;
	//DEBUGOUT("[I2CW@PDA2][0x%x:%d]\r\n", u32_addr, u8_write_len);
	memcpy((void *)(g_u8_i2c_tx_buf + 6), p_u8_data_buf, u8_write_len);
	while (u8_retry_count > 0) {
		u8_tmp = hal_i2c_write(g_u8_i2c_nid, (const uint8_t *)g_u8_i2c_tx_buf, u8_tx_len);

		if (u8_tmp == u8_tx_len)
			break;

		DEBUGOUT("[I2CW@PDA2] Write Retry=%d,All=0x%x[0x%x]{%x}\r\n", u8_retry_count, u32_addr, g_u8_i2c_nid, u8_trans_mode);

		//g_u8_i2c_nid = g_u8_i2c_try_nid[u8_retry_count % 2];

		u8_retry_count--;
	}

	u8_result = (u8_tmp == u8_tx_len) ? SUCCESS : ERROR;

	return u8_result;
}

unsigned char i2c_burst_write_pda2(
	unsigned char u8_addr,
	unsigned char u8_write_len,
	unsigned char *p_u8_data_buf)
{
	unsigned char u8_result;
	unsigned char u8_tx_sz = 0;
	unsigned char u8_tmp = 0;
	unsigned char u8_retry_count = 3;

	if (u8_write_len <= 0 || u8_write_len > 254)
		return ERROR;

	u8_tx_sz = u8_write_len + 1;
	//memset((void *)g_u8_i2c_tx_buf, 0 , sizeof(g_u8_i2c_tx_buf));
	g_u8_i2c_tx_buf[0] = u8_addr;
	memcpy((void *)(g_u8_i2c_tx_buf + 1), (p_u8_data_buf), u8_write_len);

	while (u8_retry_count) {
		u8_tmp = hal_i2c_write(g_u8_i2c_nid, (const uint8_t *)g_u8_i2c_tx_buf, u8_tx_sz);
		if (u8_tmp == u8_tx_sz)
			break;
		//g_u8_i2c_nid = g_u8_i2c_try_nid[u8_retry_count % 2];
		DEBUGOUT("PDA2 Write Error, Try NID: %x\r\n", g_u8_i2c_nid);
		u8_retry_count--;
	}

	u8_result = (u8_tmp == u8_tx_sz) ? SUCCESS : ERROR;

	return u8_result;
}

unsigned char i2c_burst_read_pda2(unsigned char u8_addr, unsigned short u16_read_len, unsigned char *p_u8_output_buf)
{
	unsigned char u8_result;
	unsigned short u16_rx_sz;
	unsigned char u8_cmd_len;
	unsigned char u8_retry_count = 3;

	/* Start I2C secondary device first */
	u8_cmd_len = 1;
	g_u8_i2c_tx_buf[0] = u8_addr;

	while (u8_retry_count) {
		u16_rx_sz = hal_i2c_cmd_read(g_u8_i2c_nid, (uint8_t *)g_u8_i2c_tx_buf, u8_cmd_len,
				     p_u8_output_buf, u16_read_len);
		if (u16_rx_sz == u16_read_len)
			break;
		//g_u8_i2c_nid = g_u8_i2c_try_nid[u8_retry_count % 2];
		DEBUGOUT("PDA2 Read Error, Try NID: %x\r\n", g_u8_i2c_nid);
		u8_retry_count--;
	}
	u8_result = (u16_rx_sz == u16_read_len) ? SUCCESS : ERROR;

	return u8_result;
}

unsigned char spi_read_pda(unsigned int u32_addr, unsigned short u16_read_len, unsigned char *p_u8_output_buf)
{
	unsigned char u8_result = ERROR;
	unsigned short u16_rx_sz = 0;
	unsigned char u8_retry_count = 3;
	unsigned char u8_spi_tx_cmd;

	/* Start SPI secondary device first */
	if (p_u8_output_buf == NULL) {
		DEBUGOUT("[SPIRPDA] SPI read buffer is null!!\r\n");
		return ERROR;
	}

	if (u16_read_len == 0 || u16_read_len > 256) {
		DEBUGOUT("[SPIRPDA] Length(%d) of SPI write is error!!\r\n", u16_read_len);
		return ERROR;
	}

	u8_spi_tx_cmd = (unsigned char)u32_addr;

	u8_result = hal_spi_set_pda_address(SPI_EID, (u32_addr >> 8), SPI_BYTE_MODE);

	if (g_u8_enable_spi_retry)
		u8_retry_count = SPI_RETRY_TIME;
	else
		u8_retry_count = 1;

	if (u8_result == SUCCESS) {
		while (u8_retry_count > 0) {
			u16_rx_sz = hal_spi_m_cmd_read(SPI_NID, &u8_spi_tx_cmd, 2, p_u8_output_buf, u16_read_len);

			if (u16_rx_sz == u16_read_len)
				break;

			DEBUGOUT("[SPIRPDA] spi_m_cmd_read Retry=%d,All=0x%x\r\n", u8_retry_count, u32_addr);
			u8_retry_count--;
		}

		u8_result = (u16_rx_sz == u16_read_len) ? SUCCESS : ERROR;

		if (u8_result == ERROR)
			DEBUGOUT("[SPIRPDA] Read (0x%x:) error:%d,T:%d\r\n", u32_addr, u16_rx_sz, u16_read_len);
	} else {
		DEBUGOUT("[SPIRPDA] Set PDA address [0x%x] error !\r\n", u32_addr);
	}

	if (u8_result == ERROR) {
		if (p_u8_output_buf == NULL)
			g_u8_spi_m_buf[SPI_M_RX_BUF][0] = 0;
		else
			p_u8_output_buf[0] = 0;
		DEBUGOUT("[SPIRPDA] Read ERR [%x:%d]\r\n", u32_addr, u16_read_len);
	} else {
		//DEBUGOUT("[SPIRPDA] Read [%x:%x:%d:%d][%x:%x][%x:%x]\r\n",u32_addr,u32_spi_read_addr,u8_spi_read_lens,u16_read_len,
		//	g_u8_spi_m_buf[SPI_M_RX_BUF][0],g_u8_spi_m_buf[SPI_M_RX_BUF][1],
		//	p_u8_output_buf[0],p_u8_output_buf[1]);
	}

	return u8_result;
}

/*
 *   PDA burst write
 */
unsigned char spi_write_pda(unsigned int u32_addr, unsigned char u8_write_len, unsigned char *p_u8_data_buf, unsigned char u8_trans_mode)
{
	unsigned char u8_result = ERROR;
	unsigned char u8_tx_len = u8_write_len + 2;
	unsigned char u8_tmp = 255, u8_retry_count = 3;

	if (u8_write_len == 0 || u8_write_len > 124) {
		DEBUGOUT("[SPIBW] Length(%d) of SPI write is error!!\r\n", u8_write_len);
		return ERROR;
	}

	//Parameter take BYTE MODE
	if (u8_trans_mode != SPI_BYTE_MODE) {
		//I2C access for register need to word mode
		if ((u32_addr & 0x40000000) && ((u8_write_len < 4) || (u8_write_len & 0x03) || (u32_addr & 0x00000003))) {
			DEBUGOUT("[SPIBW] SPI write of register is not word align(0x%x,%d,0x%x)!!\r\n", u32_addr, u8_write_len, u8_trans_mode);
			return ERROR;
		}

		if (u8_write_len >= 4) {
			if ((u32_addr & 0x00000003) || (u8_write_len & 0x03)) {
				DEBUGOUT("[SPIBW] Length >= 4 and not word align!! (0x%x,%d,0x%x) \r\n", u32_addr, u8_write_len, u8_trans_mode);
				return ERROR;
			}
			//Force to SPI WORD mode
			u8_trans_mode = SPI_WORD_MODE;
		}
	} else {
		if ((u32_addr & 0x50000000) && ((u8_write_len < 4) || (u8_write_len & 0x03) || (u32_addr & 0x00000003))) {
			DEBUGOUT("[SPIBW] SPI write of register is not word align(0x%x,%d,0x%x)!!\r\n", u32_addr, u8_write_len, u8_trans_mode);
			return ERROR;
		}
	}

	/* Start I2C secondary device first */
	u8_result = hal_spi_set_pda_address(SPI_EID, (u32_addr >> 8), u8_trans_mode);

	if (g_u8_enable_spi_retry)
		u8_retry_count = SPI_RETRY_TIME;
	else
		u8_retry_count = 1;

	if (u8_result == SUCCESS) {
		memset(g_u8_spi_m_buf[SPI_M_TX_BUF], 0, sizeof(g_u8_spi_m_buf[SPI_M_TX_BUF]));
		g_u8_spi_m_buf[SPI_M_TX_BUF][1] = (unsigned char)(u32_addr); // last byte of address
		memcpy(g_u8_spi_m_buf[SPI_M_TX_BUF] + 2, p_u8_data_buf, u8_write_len);
		while (u8_retry_count > 0) {
			u8_tmp = hal_spi_m_send(SPI_NID, g_u8_spi_m_buf[SPI_M_TX_BUF], u8_tx_len);

			if (u8_tmp == u8_tx_len)
				break;

			DEBUGOUT("[SPIBW] Write Retry=%d,All=0x%x[0x%x]{%x}\r\n", u8_retry_count, u32_addr, g_u8_i2c_nid, u8_trans_mode);
			u8_retry_count--;
		}

		u8_result = (u8_tmp == u8_tx_len) ? SUCCESS : ERROR;
	} else {
		DEBUGOUT("[SPIBW] Set PDA address [0x%x] error !\r\n", u32_addr);
	}

	//DEBUGOUT("[SPIBW] Set PDA address [0x%x:%d][[%x:%x:%x] error !\r\n", u32_addr, u8_write_len,
	//	g_u8_spi_m_buf[SPI_M_TX_BUF][0],g_u8_spi_m_buf[SPI_M_TX_BUF][1],g_u8_spi_m_buf[SPI_M_TX_BUF][2]);
	return u8_result;
}

unsigned char spi_read_pda_via_pda2(unsigned int u32_addr, unsigned short u16_read_len, unsigned char *p_u8_output_buf)
{
	unsigned char u8_result = ERROR;
	unsigned short u16_rx_sz = 0;
	unsigned char u8_retry_count = 3;
	/* Start I2C secondary device first */

	if (p_u8_output_buf == NULL) {
		DEBUGOUT("[[SPIR@PDA2] SPI read buffer is null!!\r\n");
		return ERROR;
	}

	if (u16_read_len == 0 || u16_read_len > 128) {
		DEBUGOUT("[SPIR@PDA2] Length(%d) of SPI read is error!!\r\n", u16_read_len);
		return ERROR;
	}

	memset(g_u8_spi_m_buf[SPI_M_TX_BUF], 0, sizeof(g_u8_spi_m_buf[SPI_M_TX_BUF]));
	g_u8_spi_m_buf[SPI_M_TX_BUF][0] = (SPI_RID | g_u8_i2c_nid);
	g_u8_spi_m_buf[SPI_M_TX_BUF][1] = SPI_PDA_CMD;
	g_u8_spi_m_buf[SPI_M_TX_BUF][2] = (unsigned char)u32_addr;
	g_u8_spi_m_buf[SPI_M_TX_BUF][3] = (unsigned char)(u32_addr >> 8);
	g_u8_spi_m_buf[SPI_M_TX_BUF][4] = (unsigned char)(u32_addr >> 16);
	g_u8_spi_m_buf[SPI_M_TX_BUF][5] = (unsigned char)(u32_addr >> 24);

	if (u32_addr & 0x40000000)	//Force to I2C WORD mode
		g_u8_spi_m_buf[SPI_M_TX_BUF][6] = SPI_PDA2_WORD_MODE;	//pda setting
	else
		g_u8_spi_m_buf[SPI_M_TX_BUF][6] = SPI_PDA2_BYTE_MODE;	//pda setting

	if (g_u8_enable_spi_retry)
		u8_retry_count = SPI_RETRY_TIME;
	else
		u8_retry_count = 1;

	while (u8_retry_count > 0) {
		u16_rx_sz = hal_spi_m_read(7, p_u8_output_buf, u16_read_len);
		if (u16_rx_sz == u16_read_len)
			break;

		DEBUGOUT("[SPIRPDA] spi_m_cmd_read Retry=%d,All=0x%x\r\n", u8_retry_count, u32_addr);
		u8_retry_count--;
	}

	u8_result = (u16_rx_sz == u16_read_len) ? SUCCESS : ERROR;


	if (u8_result == ERROR) {
		DEBUGOUT("[SPIRPDA] Read (0x%x:) error:%d,T:%d\r\n", u32_addr, u16_rx_sz, u16_read_len);
		if (p_u8_output_buf == NULL)
			g_u8_spi_m_buf[SPI_M_RX_BUF][0] = 0;
		else
			p_u8_output_buf[0] = 0;
	}

	return u8_result;
}

unsigned char spi_write_pda_via_pda2(unsigned int u32_addr, unsigned char u8_write_len, unsigned char *p_u8_data_buf, unsigned char u8_trans_mode)
{
	unsigned char u8_result = ERROR;
	unsigned char u8_tx_len = u8_write_len + 7;
	unsigned char u8_tmp = 255, u8_retry_count = 3;

	if (u8_write_len == 0 || u8_write_len > 128) {
		DEBUGOUT("[SPIW@PDA2] Length(%d) of SPI write is error!!\r\n", u8_write_len);
		return ERROR;
	}

	//Parameter take BYTE MODE
	if (u8_trans_mode != SPI_BYTE_MODE) {
		//SPI access for register need to word mode
		if ((u32_addr & 0x40000000) && ((u8_write_len < 4) || (u8_write_len & 0x03) || (u32_addr & 0x00000003))) {
			DEBUGOUT("[SPIW@PDA2] SPI write of register is not word align(0x%x,%d,0x%x)!!\r\n", u32_addr, u8_write_len, u8_trans_mode);
			return ERROR;
		}

		if (u8_write_len >= 4) {
			if ((u32_addr & 0x00000003) || (u8_write_len & 0x03)) {
				DEBUGOUT("[SPIW@PDA2] Length >= 4 and not word align!! (0x%x,%d,0x%x) \r\n", u32_addr, u8_write_len, u8_trans_mode);
				return ERROR;
			}
			//Force to SPI WORD mode
			u8_trans_mode = SPI_PDA2_WORD_MODE;
		}
	} else {
		if ((u32_addr & 0x40000000)) {
			if ((u8_write_len >= 4) && ((u8_write_len & 0x03) == 0)) {
				u8_trans_mode = SPI_PDA2_WORD_MODE;
			} else {
				DEBUGOUT("[SPIW@PDA2] SPI write of register is not word align(0x%x,%d,0x%x)!!\r\n", u32_addr, u8_write_len, u8_trans_mode);
				return ERROR;
			}
		} else {
			u8_trans_mode = SPI_PDA2_BYTE_MODE;
		}
	}

	//memset(g_u8_spi_m_buf[SPI_M_TX_BUF], 0, sizeof(g_u8_spi_m_buf[SPI_M_TX_BUF]));
	g_u8_spi_m_buf[SPI_M_TX_BUF][1] = SPI_PDA_CMD;
	g_u8_spi_m_buf[SPI_M_TX_BUF][2] = (unsigned char)u32_addr;
	g_u8_spi_m_buf[SPI_M_TX_BUF][3] = (unsigned char)(u32_addr >> 8);
	g_u8_spi_m_buf[SPI_M_TX_BUF][4] = (unsigned char)(u32_addr >> 16);
	g_u8_spi_m_buf[SPI_M_TX_BUF][5] = (unsigned char)(u32_addr >> 24);
	g_u8_spi_m_buf[SPI_M_TX_BUF][6] = u8_trans_mode;

	if (g_u8_enable_spi_retry)
		u8_retry_count = SPI_RETRY_TIME;
	else
		u8_retry_count = 1;

	memcpy((void *)(g_u8_spi_m_buf[SPI_M_TX_BUF] + 7), p_u8_data_buf, u8_write_len);
	while (u8_retry_count > 0) {
		u8_tmp = hal_spi_m_send(g_u8_i2c_nid, (uint8_t *)g_u8_spi_m_buf[SPI_M_TX_BUF], u8_tx_len);

		if (u8_tmp == u8_tx_len)
			break;

		DEBUGOUT("[SPIW@PDA2] Write Retry=%d,All=0x%x[0x%x]{%x}\r\n", u8_retry_count, u32_addr, g_u8_i2c_nid, u8_trans_mode);
		u8_retry_count--;
	}

	u8_result = (u8_tmp == u8_tx_len) ? SUCCESS : ERROR;

	return u8_result;
}

unsigned char spi_burst_write_pda2(
	unsigned char u8_addr,
	unsigned char u8_write_len,
	unsigned char *p_u8_data_buf)
{
	unsigned char u8_result;
	unsigned char u8_tx_sz = 0;
	unsigned char u8_tmp = 0;
	unsigned char u8_retry_count = 3;

	if (u8_write_len <= 0 || u8_write_len > 128)
		return ERROR;


	u8_tx_sz = u8_write_len + 2;
	//memset(g_u8_spi_m_buf[SPI_M_TX_BUF], 0, sizeof(g_u8_spi_m_buf[SPI_M_TX_BUF]));
	g_u8_spi_m_buf[SPI_M_TX_BUF][1] = u8_addr;
	memcpy((void *)(g_u8_spi_m_buf[SPI_M_TX_BUF] + 2), (p_u8_data_buf), u8_write_len);


	while (u8_retry_count) {
		u8_tmp = hal_spi_m_send(g_u8_i2c_nid, (uint8_t *)g_u8_spi_m_buf[SPI_M_TX_BUF], u8_tx_sz);
		if (u8_tmp == u8_tx_sz)
			break;

		DEBUGOUT("PDA2 Write Error, Try NID: %x\r\n", g_u8_i2c_nid);
		u8_retry_count--;
	}

	u8_result = (u8_tmp == u8_tx_sz) ? SUCCESS : ERROR;

	return u8_result;
}

unsigned char spi_burst_read_pda2(unsigned char u8_addr, unsigned short u16_read_len, unsigned char *p_u8_output_buf)
{
	unsigned char u8_result;
	unsigned short u16_rx_sz;
	unsigned char u8_cmd_len;
	unsigned char u8_retry_count = 3;

	/* Start SPI secondary device first */
	u8_cmd_len = 2;
	memset(g_u8_spi_m_buf[SPI_M_TX_BUF], 0, sizeof(g_u8_spi_m_buf[SPI_M_TX_BUF]));
	g_u8_spi_m_buf[SPI_M_TX_BUF][0] = (SPI_RID | g_u8_i2c_nid);
	g_u8_spi_m_buf[SPI_M_TX_BUF][1] = u8_addr;

	while (u8_retry_count) {
		u16_rx_sz = hal_spi_m_read(u8_cmd_len, p_u8_output_buf, u16_read_len);
		if (u16_rx_sz == u16_read_len)
			break;

		DEBUGOUT("PDA2 Read Error, Try NID: %x\r\n", g_u8_i2c_nid);
		u8_retry_count--;
	}
	u8_result = (u16_rx_sz == u16_read_len) ? SUCCESS : ERROR;

	return u8_result;
}

unsigned char handle_i2c_eng_init(void)
{
	int tmp = 0;
	unsigned char u8_retry_count = 3;

	if (g_u16_dev_id == DEVICE_ID_RM6DC10) {
		// send I2C buffer enable pda2 //
		// S 0x5A W 0x06 0x00 0x50 0xE0 P
		g_u8_i2c_tx_buf[0] = 0x06;
		g_u8_i2c_tx_buf[1] = 0x00;
		g_u8_i2c_tx_buf[2] = 0x50;
		g_u8_i2c_tx_buf[3] = I2C_WORD_MODE;
		tmp = hal_i2c_write(I2C_EID, (const uint8_t *)g_u8_i2c_tx_buf, I2C_PDA_LEN);
		if (tmp != I2C_PDA_LEN)
			return ERROR;

		while (u8_retry_count) {
			// S 0x39 W 0x28 0x06 0x00 0x00 0x00 p
			g_u8_i2c_tx_buf[0] = 0x28;
			g_u8_i2c_tx_buf[1] = 0x06;
			g_u8_i2c_tx_buf[2] = 0x00;
			g_u8_i2c_tx_buf[3] = 0x00;
			g_u8_i2c_tx_buf[4] = 0x00;
			tmp = hal_i2c_write(g_u8_i2c_nid, (const uint8_t *)g_u8_i2c_tx_buf, 5);
			if (tmp == 5)
				break;

			//g_u8_i2c_nid = g_u8_i2c_try_nid[u8_retry_count % 2];
			DEBUGOUT("PDA2 Write Error, Try NID: %x\r\n", g_u8_i2c_nid);
			u8_retry_count--;

		}
		// S 0x5A W 0x06 0x00 0x50 0x43 P
		g_u8_i2c_tx_buf[0] = 0x06;
		g_u8_i2c_tx_buf[1] = 0x00;
		g_u8_i2c_tx_buf[2] = 0x50;
		g_u8_i2c_tx_buf[3] = I2C_PDA2_WORD_MODE;
		tmp = hal_i2c_write(I2C_EID, (const uint8_t *)g_u8_i2c_tx_buf, I2C_PDA_LEN);
		if (tmp != I2C_PDA_LEN)
			return ERROR;
		return SUCCESS;
	}
	return SUCCESS;
}

/******************************************************************************
 ** Function name:		handle_ic_read
 **
 ** Descriptions:		handle read data from ic
 **
 ** parameters:			u32_addr,			Address
						u16_read_len,		Read data length
						p_u8_output_buf,	Data buffer
						u8_interface,		SPI or I2C
						u8_trans_mode		PDA2_MODE, PDA_WORD_MODE, PDA_BYTE_MODE, MCU_MODE

 ** Returned value:		ERROR, SUCCESS
 **
 ******************************************************************************/
unsigned char handle_ic_read(
	unsigned int u32_addr,
	unsigned short u16_read_len,
	unsigned char *p_u8_output_buf,
	unsigned char u8_interface,
	unsigned char u8_trans_mode)
{
	//PDA MODE
	if ((u8_trans_mode == I2C_WORD_MODE) && (u32_addr & 0x00000003)) {
		DEBUGOUT("[HRW] Handle Read Word ADDR Not Word Align!!\r\n");
		return ERROR;
	}
	if (u8_interface == I2C_INTERFACE) {
		if (u8_trans_mode == I2C_PDA2_MODE) {
			//PDA2 MODE
			if (i2c_burst_read_pda2((unsigned char)u32_addr, u16_read_len, p_u8_output_buf) == ERROR) {
				DEBUGOUT("[HWP] %s PDA2 I2C NG!\r\n", __func__);
				return ERROR;
			}
		} else {
			if (i2c_read_pda_via_pda2(u32_addr, u16_read_len, p_u8_output_buf) == ERROR) {
				if (!g_u8_mute_i2c_err_log)
					DEBUGOUT("[HRP] %s I2C via_pda2 NG!\r\n", __func__);
				return ERROR;
			}
		}
	} else { //SPI_INTERFACE
		if (u8_trans_mode == I2C_PDA2_MODE) {
			//PDA2 MODE
			if (spi_burst_read_pda2((unsigned char)u32_addr, u16_read_len, p_u8_output_buf) == ERROR) {
				DEBUGOUT("[HWP] %s PDA2 I2C NG!\r\n", __func__);
				return ERROR;
			}
		} else {
#if DEVICE_RM6D031
			if (spi_read_pda_via_pda2(u32_addr, u16_read_len, p_u8_output_buf) == ERROR) {
				DEBUGOUT("[HRP] %s SPI NG!\r\n", __func__);
				return ERROR;
			}
#else
			if (spi_read_pda(u32_addr, u16_read_len, p_u8_output_buf) == ERROR) {
				DEBUGOUT("[HRP] %s SPI NG!\r\n", __func__);
				return ERROR;
			}
#endif
		}
	}
	return SUCCESS;
}

/******************************************************************************
 ** Function name:		handle_ic_write
 **
 ** Descriptions:		handle write data to ic
 **
 ** parameters:			u32_addr,			Address
						u8_write_len,		data length
						p_u8_data_buf,		Data
						u8_interface,		SPI or I2C
						u8_trans_mode		PDA2_MODE, PDA_WORD_MODE, PDA_BYTE_MODE, MCU_MODE

 ** Returned value:		ERROR, SUCCESS
 **
 ******************************************************************************/
unsigned char handle_ic_write(
	unsigned int u32_addr,
	unsigned char u8_write_len,
	unsigned char *p_u8_data_buf,
	unsigned char u8_interface,
	unsigned char u8_trans_mode)
{
	//PDA MODE
	if ((u8_trans_mode == I2C_WORD_MODE) && (u32_addr & 0x00000003)) {
		DEBUGOUT("[HWP] Handle Write Word ADDR Not Word Align!!\r\n");
		return ERROR;
	}
	if (u8_interface == I2C_INTERFACE) {
		if (u8_trans_mode == I2C_PDA2_MODE) {
			//PDA2 MODE
			if (i2c_burst_write_pda2((unsigned char)u32_addr, u8_write_len, p_u8_data_buf) == ERROR) {
				DEBUGOUT("[HWP] %s PDA2 I2C NG!\r\n", __func__);
				return ERROR;
			}
		} else {
			if (i2c_write_pda_via_pda2(u32_addr, u8_write_len, p_u8_data_buf, u8_trans_mode) == ERROR) {
				DEBUGOUT("[HWP] %s I2C via_pda2 NG!\r\n", __func__);
				return ERROR;
			}
		}
	} else { //SPI_INTERFACE
		switch (u8_trans_mode) {
		case I2C_BYTE_MODE:
			u8_trans_mode = SPI_BYTE_MODE;
			break;
		case I2C_WORD_MODE:
			u8_trans_mode = SPI_WORD_MODE;
			break;
		default:
			break;
		}

		if (u8_trans_mode == I2C_PDA2_MODE) {
			//PDA2 MODE
			if (spi_burst_write_pda2((unsigned char)u32_addr, u8_write_len, p_u8_data_buf) == ERROR) {
				DEBUGOUT("[HWP] %s PDA2 I2C NG!\r\n", __func__);
				return ERROR;
			}
		} else {
#if DEVICE_RM6D031
			if (spi_write_pda_via_pda2(u32_addr, u8_write_len, p_u8_data_buf, u8_trans_mode) == ERROR) {
				DEBUGOUT("[HWP] %s SPI NG!\r\n", __func__);
				return ERROR;
			}
#else
			if (spi_write_pda(u32_addr, u8_write_len, p_u8_data_buf, u8_trans_mode) == ERROR) {
				DEBUGOUT("[HWP] %s SPI NG!\r\n", __func__);
				return ERROR;
			}
#endif
		}
	}
	return SUCCESS;
}

unsigned char handle_ic_set_initial_code(void)
{
	unsigned int u32_index = 0;
	unsigned char u8_size = 1;

	DEBUGOUT("g_u8_display_interface %d\r\n", g_u8_display_interface);
	while (u8_size != 0) {
		memset(g_u8_initial_code_buf, 0, sizeof(g_u8_initial_code_buf));
		if (!hal_read_image_dsp_initial_code(u32_index, 5, g_u8_initial_code_buf)) //length is in the fifth byte
			goto EXIT;
		u8_size = g_u8_initial_code_buf[4];
		if ((u8_size != 0) && (u8_size <= INITIAL_CODE_READ_LENGTH)) {
			if (!hal_read_image_dsp_initial_code(u32_index, u8_size, g_u8_initial_code_buf))
				goto EXIT;
		} else {
			if (u8_size > INITIAL_CODE_READ_LENGTH)
				DEBUGOUT("Set initial code ERROR!!! %d\r\n", u8_size);
			break;
		}

		//memset(g_u8_initial_code_buf, 0, sizeof(g_u8_initial_code_buf));
		//memcpy(g_u8_initial_code_buf, g_u8_data_buf, u8_size);
		if (!handle_ic_display_write(g_u8_initial_code_buf, u8_size)) {
			DEBUGOUT("Set initial code ERROR!!!\r\n");
			return ERROR;
		}

		u32_index += INITIAL_CODE_READ_LENGTH;
	}
	return SUCCESS;
EXIT:
	DEBUGOUT("Set initial code ERROR!!!\r\n");
	return ERROR;
}

