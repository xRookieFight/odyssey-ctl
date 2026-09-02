// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * VCP feature table for the Samsung Odyssey G5 (G53F).
 *
 * Copyright (C) 2026 xRookieFight
 */

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "vcp.h"

static const struct vcp_enum input_source_values[] = {
	{ "hdmi-1", 0x11 },
	{ "hdmi-2", 0x12 },
	{ "dp-1",   0x0f },
	{ NULL,     0 },
};

static const struct vcp_enum color_preset_values[] = {
	{ "4000k", 0x03 },
	{ "5000k", 0x04 },
	{ "6500k", 0x05 },
	{ "8200k", 0x07 },
	{ "9300k", 0x08 },
	{ "user1", 0x0b },
	{ NULL,    0 },
};

/*
 * MCCS defines 0x01 through 0x05 for this feature, but the G53F only lists
 * 0x04 and 0x05 as writable.  The remaining entries are kept so a value read
 * back from the display still resolves to a name.
 */
static const struct vcp_enum power_mode_values[] = {
	{ "on",       0x01 },
	{ "standby",  0x02 },
	{ "suspend",  0x03 },
	{ "off",      0x04 },
	{ "hard-off", 0x05 },
	{ NULL,       0 },
};

static const struct vcp_enum gamma_values[] = {
	{ "1.0", 0x64 },
	{ "1.2", 0x78 },
	{ "1.4", 0x8c },
	{ NULL,  0 },
};

static const struct vcp_feature features[] = {
	{
		.name = "brightness",
		.opcode = 0x10,
		.summary = "Panel luminance",
	},
	{
		.name = "contrast",
		.opcode = 0x12,
		.summary = "Panel contrast",
	},
	{
		.name = "color-preset",
		.opcode = 0x14,
		.summary = "Colour temperature preset",
		.values = color_preset_values,
	},
	{
		.name = "red",
		.opcode = 0x16,
		.summary = "Video gain, red channel",
	},
	{
		.name = "green",
		.opcode = 0x18,
		.summary = "Video gain, green channel",
	},
	{
		.name = "blue",
		.opcode = 0x1a,
		.summary = "Video gain, blue channel",
	},
	{
		.name = "input",
		.opcode = 0x60,
		.summary = "Active input source",
		.values = input_source_values,
	},
	{
		.name = "volume",
		.opcode = 0x62,
		.summary = "Headphone output level",
	},
	{
		.name = "gamma",
		.opcode = 0x72,
		.summary = "Gamma preset",
		.values = gamma_values,
	},
	{
		.name = "h-frequency",
		.opcode = 0xac,
		.summary = "Horizontal frequency of the active timing",
		.read_only = 1,
	},
	{
		.name = "v-frequency",
		.opcode = 0xae,
		.summary = "Vertical frequency of the active timing",
		.read_only = 1,
	},
	{
		.name = "usage-time",
		.opcode = 0xc0,
		.summary = "Panel power-on hours",
		.read_only = 1,
	},
	{
		.name = "firmware",
		.opcode = 0xc9,
		.summary = "Display firmware level",
		.read_only = 1,
	},
	{
		.name = "power",
		.opcode = 0xd6,
		.summary = "DPMS power mode",
		.values = power_mode_values,
	},
};

const struct vcp_feature *vcp_table(void)
{
	return features;
}

size_t vcp_table_size(void)
{
	return sizeof(features) / sizeof(features[0]);
}

const struct vcp_feature *vcp_lookup(const char *name)
{
	size_t i;

	for (i = 0; i < vcp_table_size(); i++) {
		if (!strcmp(features[i].name, name))
			return &features[i];
	}

	return NULL;
}

const struct vcp_feature *vcp_lookup_opcode(uint8_t opcode)
{
	size_t i;

	for (i = 0; i < vcp_table_size(); i++) {
		if (features[i].opcode == opcode)
			return &features[i];
	}

	return NULL;
}

/**
 * vcp_parse_value() - turn command line text into a raw VCP value
 * @feature:	feature the value belongs to
 * @text:	either a named value or a number accepted by strtoul
 * @out:	receives the raw value
 *
 * Return: 0 on success, or a negative errno.
 */
int vcp_parse_value(const struct vcp_feature *feature, const char *text, uint16_t *out)
{
	unsigned long parsed;
	char *end;

	if (feature->values) {
		const struct vcp_enum *value;

		for (value = feature->values; value->name; value++) {
			if (!strcmp(value->name, text)) {
				*out = value->value;
				return 0;
			}
		}
	}

	errno = 0;
	parsed = strtoul(text, &end, 0);
	if (errno || end == text || *end || parsed > UINT16_MAX)
		return -EINVAL;

	*out = (uint16_t)parsed;

	return 0;
}

/**
 * vcp_format_value() - resolve a raw value to its name
 * @feature:	feature the value belongs to
 * @value:	raw value as reported by the display
 *
 * Return: the matching name, or NULL when the feature is continuous or the
 * value is not one the display documents.
 */
const char *vcp_format_value(const struct vcp_feature *feature, uint16_t value)
{
	const struct vcp_enum *entry;

	if (!feature->values)
		return NULL;

	for (entry = feature->values; entry->name; entry++) {
		if (entry->value == value)
			return entry->name;
	}

	return NULL;
}
