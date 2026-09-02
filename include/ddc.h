/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * DDC/CI transport over a Linux i2c-dev character device.
 *
 * Copyright (C) 2026 xRookieFight
 */

#ifndef ODYSSEY_DDC_H
#define ODYSSEY_DDC_H

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

/* DDC/CI slave address on a display's DDC bus, 7-bit. */
#define DDC_SLAVE_ADDR		0x37

/*
 * Largest payload a single DDC/CI message may carry.  Feature replies fit in
 * eight bytes, but a capability fragment is a three byte header followed by up
 * to 32 bytes of string, so the bound has to sit above the 32 byte figure the
 * spec quotes for data alone.
 */
#define DDC_MAX_PAYLOAD		64

/* Capability strings are read in fragments; this bounds the reassembled result. */
#define DDC_CAPS_MAX		4096

/**
 * struct ddc_conn - an open DDC/CI connection
 * @fd:		file descriptor of the i2c-dev node, addressed at DDC_SLAVE_ADDR
 * @path:	device node the connection was opened from
 */
struct ddc_conn {
	int fd;
	char path[64];
};

/**
 * struct ddc_vcp - the result of reading a VCP feature
 * @opcode:	feature code that was queried
 * @type:	0 for continuous features, 1 for non-continuous ones
 * @current:	value currently in effect
 * @maximum:	largest value the display accepts
 */
struct ddc_vcp {
	uint8_t opcode;
	uint8_t type;
	uint16_t current;
	uint16_t maximum;
};

int ddc_open(struct ddc_conn *conn, const char *i2c_path);
void ddc_close(struct ddc_conn *conn);

int ddc_get_vcp(struct ddc_conn *conn, uint8_t opcode, struct ddc_vcp *out);
int ddc_set_vcp(struct ddc_conn *conn, uint8_t opcode, uint16_t value);
int ddc_save_settings(struct ddc_conn *conn);
ssize_t ddc_read_capabilities(struct ddc_conn *conn, char *buf, size_t len);

#endif /* ODYSSEY_DDC_H */
