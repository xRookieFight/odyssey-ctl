// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Profile file handling for the apply command.
 *
 * A profile is a sequence of "feature = value" lines using the same feature and
 * value names the command line accepts.  Blank lines and lines starting with #
 * are ignored.
 *
 * Copyright (C) 2026 xRookieFight
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "config.h"
#include "log.h"

#define CONFIG_RELATIVE_PATH	"odyssey-ctl/profile.conf"

static char *trim(char *text)
{
	char *end;

	while (*text == ' ' || *text == '\t')
		text++;

	end = text + strlen(text);
	while (end > text) {
		char c = end[-1];

		if (c != ' ' && c != '\t' && c != '\n' && c != '\r')
			break;
		end--;
	}
	*end = '\0';

	return text;
}

/**
 * config_default_path() - build the per user profile path
 * @out:	receives the path
 * @len:	size of @out
 *
 * Return: 0 on success, or a negative errno when neither XDG_CONFIG_HOME nor
 * HOME is set.
 */
int config_default_path(char *out, size_t len)
{
	const char *base = getenv("XDG_CONFIG_HOME");

	if (base && *base) {
		snprintf(out, len, "%s/%s", base, CONFIG_RELATIVE_PATH);
		return 0;
	}

	base = getenv("HOME");
	if (!base || !*base)
		return -ENOENT;

	snprintf(out, len, "%s/.config/%s", base, CONFIG_RELATIVE_PATH);

	return 0;
}

/**
 * config_load() - parse a profile file
 * @path:	file to read
 * @out:	array receiving the parsed entries
 * @max:	capacity of @out
 *
 * Unknown feature names and malformed values are reported and skipped so a
 * single stale line does not prevent the rest of a profile from being applied.
 *
 * Return: number of entries parsed, or a negative errno.
 */
int config_load(const char *path, struct config_entry *out, size_t max)
{
	char line[256];
	unsigned int lineno = 0;
	size_t count = 0;
	FILE *fp;

	fp = fopen(path, "re");
	if (!fp)
		return -errno;

	while (fgets(line, sizeof(line), fp) && count < max) {
		const struct vcp_feature *feature;
		char *key, *value, *separator;

		lineno++;

		key = trim(line);
		if (!*key || *key == '#')
			continue;

		separator = strchr(key, '=');
		if (!separator) {
			log_warn("%s:%u: missing '='", path, lineno);
			continue;
		}

		*separator = '\0';
		value = trim(separator + 1);
		key = trim(key);

		feature = vcp_lookup(key);
		if (!feature) {
			log_warn("%s:%u: unknown feature '%s'", path, lineno, key);
			continue;
		}

		if (feature->read_only) {
			log_warn("%s:%u: '%s' is read-only", path, lineno, key);
			continue;
		}

		if (vcp_parse_value(feature, value, &out[count].value)) {
			log_warn("%s:%u: invalid value '%s'", path, lineno, value);
			continue;
		}

		out[count].feature = feature;
		count++;
	}

	fclose(fp);

	return (int)count;
}
