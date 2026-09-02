/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * VCP feature table for the Samsung Odyssey G5 (G53F).
 *
 * The codes below are the ones the panel advertises in its capability string.
 * Features the display does not implement are rejected by it at runtime, so
 * the table stays descriptive rather than authoritative.
 *
 * Copyright (C) 2026 xRookieFight
 */

#ifndef ODYSSEY_VCP_H
#define ODYSSEY_VCP_H

#include <stddef.h>
#include <stdint.h>

/**
 * struct vcp_enum - a named value of a non-continuous feature
 * @name:	lower case identifier accepted on the command line
 * @value:	raw value written to the display
 */
struct vcp_enum {
	const char *name;
	uint16_t value;
};

/**
 * struct vcp_feature - a controllable monitor setting
 * @name:	lower case identifier accepted on the command line
 * @opcode:	VCP feature code
 * @summary:	one line description shown by the help and list output
 * @values:	NULL terminated table of named values, NULL if continuous
 * @read_only:	set for features the display reports but does not accept writes for
 */
struct vcp_feature {
	const char *name;
	uint8_t opcode;
	const char *summary;
	const struct vcp_enum *values;
	int read_only;
};

const struct vcp_feature *vcp_lookup(const char *name);
const struct vcp_feature *vcp_lookup_opcode(uint8_t opcode);
const struct vcp_feature *vcp_table(void);
size_t vcp_table_size(void);

int vcp_parse_value(const struct vcp_feature *feature, const char *text, uint16_t *out);
const char *vcp_format_value(const struct vcp_feature *feature, uint16_t value);

#endif /* ODYSSEY_VCP_H */
