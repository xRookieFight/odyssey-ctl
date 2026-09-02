/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Minimal levelled logging to stderr.
 *
 * Copyright (C) 2026 xRookieFight
 */

#ifndef ODYSSEY_LOG_H
#define ODYSSEY_LOG_H

enum log_level {
	LOG_ERROR,
	LOG_WARN,
	LOG_INFO,
	LOG_DEBUG,
};

void log_set_level(enum log_level level);
void log_message(enum log_level level, const char *fmt, ...)
	__attribute__((format(printf, 2, 3)));

#define log_error(...)	log_message(LOG_ERROR, __VA_ARGS__)
#define log_warn(...)	log_message(LOG_WARN, __VA_ARGS__)
#define log_info(...)	log_message(LOG_INFO, __VA_ARGS__)
#define log_debug(...)	log_message(LOG_DEBUG, __VA_ARGS__)

#endif /* ODYSSEY_LOG_H */
