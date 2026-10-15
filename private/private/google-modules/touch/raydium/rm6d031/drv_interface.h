/* SPDX-License-Identifier: GPL-2.0+ */
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

/*I2C */
#define I2C_PDA_CMD		0x66
#define I2C_PDA_LEN		4
#define I2C_RETRY_TIME		2
#define I2C_RID			(0xB6 >> 1)
#define I2C_EID			0x5a

/*SPI*/
#define SPI_PDA_CMD		0x66
#define SPI_PDA_LEN		5
#define SPI_RETRY_TIME		3
#define SPI_RID			(0x1 << 7)
#define SPI_EID			0x5a
#define SPI_NID			0x39
#define SPI_DUMMY_LEN		2
#define SPI_M_RX_BUF		1
#define SPI_M_TX_BUF		0

#define INITIAL_CODE_READ_LENGTH	56

/*I2C */
extern unsigned char g_u8_i2c_tx_buf[256];
extern unsigned char g_u8_enable_i2c_retry;
extern unsigned char g_u8_mute_i2c_err_log;
extern unsigned char g_u8_i2c_nid;
extern unsigned char g_u8_i2c_try_nid[2];

/*SPI*/
extern unsigned char g_u8_spi_m_buf[2][140];
extern unsigned char g_u8_enable_spi_retry;
extern unsigned char g_u8_initial_code_buf[56];
extern unsigned long g_u32_save_config[64];

extern unsigned char i2c_set_pda_address(unsigned int u32_addr, unsigned char u8_mode);
extern unsigned char i2c_write_pda(unsigned int u32_addr, unsigned char u8_write_len, unsigned char *bValue, unsigned char u8_trans_mode);
extern unsigned char i2c_read_pda(unsigned int u32_addr, unsigned char u8_read_len, unsigned char *p_u8_output_buf);
extern unsigned char handle_i2c_eng_init(void);

extern unsigned char handle_ic_read(
	unsigned int u32_addr,
	unsigned short u8_read_len,
	unsigned char *p_u8_output_buf,
	unsigned char u8_interface,
	unsigned char u8_trans_mode);

extern unsigned char handle_ic_write(
	unsigned int u32_addr,
	unsigned char u8_write_len,
	unsigned char *bValue,
	unsigned char u8_interface,
	unsigned char u8_trans_mode);

extern unsigned char handle_ic_set_initial_code(void);

//extern void hal_delay_ms(unsigned int u32_ms);
//extern void delay_us(unsigned int u32_us);
