/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Discovery of DDC capable displays through sysfs.
 *
 * Copyright (C) 2026 xRookieFight
 */

#ifndef ODYSSEY_DISPLAY_H
#define ODYSSEY_DISPLAY_H

#include <stddef.h>
#include <stdint.h>

#define DISPLAY_MAX		16

/**
 * struct display - a connected DRM output with a usable DDC bus
 * @connector:	DRM connector name, for example card1-HDMI-A-1
 * @i2c_path:	i2c-dev node carrying the connector's DDC lines
 * @mfg:	three letter PnP manufacturer id decoded from the EDID
 * @model:	model name from EDID descriptor 0xfc, empty if absent
 * @serial:	serial number from EDID descriptor 0xff, empty if absent
 * @product:	numeric product code from the EDID header
 */
struct display {
	char connector[64];
	char i2c_path[64];
	char mfg[4];
	char model[16];
	char serial[16];
	uint16_t product;
};

int display_enumerate(struct display *out, size_t max);
int display_find(struct display *out, const char *connector);

#endif /* ODYSSEY_DISPLAY_H */
