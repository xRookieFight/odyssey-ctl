/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Profile file handling for the apply command.
 *
 * Copyright (C) 2026 xRookieFight
 */

#ifndef ODYSSEY_CONFIG_H
#define ODYSSEY_CONFIG_H

#include <stddef.h>
#include <stdint.h>

#include "vcp.h"

#define CONFIG_MAX_ENTRIES	32

/**
 * struct config_entry - one setting read from a profile
 * @feature:	feature the value applies to
 * @value:	raw value to write
 */
struct config_entry {
	const struct vcp_feature *feature;
	uint16_t value;
};

int config_default_path(char *out, size_t len);
int config_load(const char *path, struct config_entry *out, size_t max);

#endif /* ODYSSEY_CONFIG_H */
