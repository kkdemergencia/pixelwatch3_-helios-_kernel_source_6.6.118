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

#ifndef _SPI_AMBIQ_H
#define _SPI_AMBIQ_H

#define AMBIQ_HD_SPIHEADER_SIZE	(1)
#define AMBIQ_HD_WRITE_FLAG	(0x80)
#define AMBIQ_HD_READ_FLAG	(0x00)
#define AMBIQ_HD_FIFO_ADDR	(0x7F)
#define AMBIQ_HD_PADDING_VALUE	(0xFF)
#define AMBIQ_HD_REGSTART_ADDR	(0x0)
#define AMBIQ_HD_READ_CMD	((AMBIQ_HD_READ_FLAG)|(AMBIQ_HD_FIFO_ADDR))
#define AMBIQ_HD_WRITE_CMD	((AMBIQ_HD_WRITE_FLAG)|(AMBIQ_HD_REGSTART_ADDR))
#define AMBIQ_HD_PAYLOAD_PAD		(3)
#define AMBIQ_HD_PAYLOAD_ALIGNMENT	(4)

#define AMBIQ_HD_MAX_DATAGRAM	((AMBIQ_HD_MAX_PAYLOAD)+(AMBIQ_HD_SPIHEADER_SIZE))

/* chosen arbitrarily, but as a multiple of 4 */
#define AMBIQ_HD_MAX_PAYLOAD	(80)

#define AMBIQ_HD_DELAY_TRANSACTION_US	(50)
#define AMBIQ_HD_DELAY_RESPONSE_US	(200)


#endif // _SPI_AMBIQ_H
