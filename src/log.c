// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Minimal levelled logging to stderr.
 *
 * Copyright (C) 2026 xRookieFight
 */

#include <stdarg.h>
#include <stdio.h>
#include <unistd.h>

#include "log.h"

static enum log_level current_level = LOG_WARN;

static const char *const level_names[] = {
	[LOG_ERROR]	= "error",
	[LOG_WARN]	= "warning",
	[LOG_INFO]	= "info",
	[LOG_DEBUG]	= "debug",
};

void log_set_level(enum log_level level)
{
	current_level = level;
}

void log_message(enum log_level level, const char *fmt, ...)
{
	va_list args;

	if (level > current_level)
		return;

	fprintf(stderr, "odyssey-ctl: %s: ", level_names[level]);

	va_start(args, fmt);
	vfprintf(stderr, fmt, args);
	va_end(args);

	fputc('\n', stderr);
}
