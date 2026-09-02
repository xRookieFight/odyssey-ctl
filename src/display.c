// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Discovery of DDC capable displays through sysfs.
 *
 * Every DRM connector exposes its DDC bus as
 * /sys/class/drm/<connector>/ddc/i2c-dev/i2c-N, which is the node DDC/CI
 * transactions have to be issued on.  Connectors without that directory, such
 * as internal panels, are skipped.
 *
 * Copyright (C) 2026 xRookieFight
 */

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "display.h"
#include "log.h"

#define DRM_SYSFS_ROOT		"/sys/class/drm"
#define EDID_BLOCK_SIZE		128

/* EDID descriptor tags carrying printable text. */
#define EDID_TAG_SERIAL		0xff
#define EDID_TAG_MODEL		0xfc

static int read_sysfs_file(const char *path, void *buf, size_t len, size_t *got)
{
	FILE *fp;
	size_t n;

	fp = fopen(path, "rb");
	if (!fp)
		return -errno;

	n = fread(buf, 1, len, fp);
	fclose(fp);

	if (got)
		*got = n;

	return 0;
}

static int connector_is_connected(const char *connector)
{
	char path[256];
	char status[32] = { 0 };
	size_t got = 0;

	snprintf(path, sizeof(path), DRM_SYSFS_ROOT "/%s/status", connector);
	if (read_sysfs_file(path, status, sizeof(status) - 1, &got))
		return 0;

	return got >= 9 && !strncmp(status, "connected", 9);
}

static int connector_i2c_path(const char *connector, char *out, size_t len)
{
	char dir_path[256];
	struct dirent *entry;
	DIR *dir;
	int found = -ENOENT;

	snprintf(dir_path, sizeof(dir_path), DRM_SYSFS_ROOT "/%s/ddc/i2c-dev", connector);

	dir = opendir(dir_path);
	if (!dir)
		return -errno;

	while ((entry = readdir(dir))) {
		if (strncmp(entry->d_name, "i2c-", 4))
			continue;

		if (strlen(entry->d_name) + sizeof("/dev/") > len) {
			log_warn("i2c node name too long: %s", entry->d_name);
			continue;
		}

		memcpy(out, "/dev/", sizeof("/dev/") - 1);
		strcpy(out + sizeof("/dev/") - 1, entry->d_name);
		found = 0;
		break;
	}

	closedir(dir);

	return found;
}

/*
 * Copy an EDID text descriptor.  The payload is padded with 0x0a and then
 * spaces, neither of which belongs in the decoded value.
 */
static void edid_copy_text(const uint8_t *payload, char *out, size_t len)
{
	size_t i;
	size_t end = 13;

	for (i = 0; i < 13; i++) {
		if (payload[i] == 0x0a) {
			end = i;
			break;
		}
	}

	while (end && payload[end - 1] == ' ')
		end--;

	if (end >= len)
		end = len - 1;

	memcpy(out, payload, end);
	out[end] = '\0';
}

static int parse_edid(const uint8_t *edid, size_t len, struct display *disp)
{
	static const uint8_t magic[8] = { 0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00 };
	uint16_t vendor;
	size_t i;

	if (len < EDID_BLOCK_SIZE || memcmp(edid, magic, sizeof(magic)))
		return -EINVAL;

	vendor = (uint16_t)((edid[8] << 8) | edid[9]);
	disp->mfg[0] = (char)('A' + ((vendor >> 10) & 0x1f) - 1);
	disp->mfg[1] = (char)('A' + ((vendor >> 5) & 0x1f) - 1);
	disp->mfg[2] = (char)('A' + (vendor & 0x1f) - 1);
	disp->mfg[3] = '\0';

	disp->product = (uint16_t)(edid[10] | (edid[11] << 8));

	for (i = 54; i + 18 <= EDID_BLOCK_SIZE; i += 18) {
		const uint8_t *desc = edid + i;

		if (desc[0] || desc[1])
			continue;

		switch (desc[3]) {
		case EDID_TAG_MODEL:
			edid_copy_text(desc + 5, disp->model, sizeof(disp->model));
			break;
		case EDID_TAG_SERIAL:
			edid_copy_text(desc + 5, disp->serial, sizeof(disp->serial));
			break;
		default:
			break;
		}
	}

	return 0;
}

static int probe_connector(const char *connector, struct display *disp)
{
	uint8_t edid[EDID_BLOCK_SIZE * 2];
	char path[256];
	size_t got = 0;
	int ret;

	memset(disp, 0, sizeof(*disp));

	if (!connector_is_connected(connector))
		return -ENODEV;

	ret = connector_i2c_path(connector, disp->i2c_path, sizeof(disp->i2c_path));
	if (ret)
		return ret;

	snprintf(path, sizeof(path), DRM_SYSFS_ROOT "/%s/edid", connector);
	ret = read_sysfs_file(path, edid, sizeof(edid), &got);
	if (ret)
		return ret;

	ret = parse_edid(edid, got, disp);
	if (ret)
		return ret;

	snprintf(disp->connector, sizeof(disp->connector), "%s", connector);

	return 0;
}

/**
 * display_enumerate() - list connected displays that expose a DDC bus
 * @out:	array receiving the results
 * @max:	capacity of @out
 *
 * Return: number of displays written, or a negative errno.
 */
int display_enumerate(struct display *out, size_t max)
{
	struct dirent *entry;
	DIR *dir;
	size_t count = 0;

	dir = opendir(DRM_SYSFS_ROOT);
	if (!dir)
		return -errno;

	while ((entry = readdir(dir)) && count < max) {
		/* Connector directories are named card<N>-<OUTPUT>. */
		if (strncmp(entry->d_name, "card", 4) || !strchr(entry->d_name, '-'))
			continue;

		if (probe_connector(entry->d_name, &out[count]))
			continue;

		count++;
	}

	closedir(dir);

	return (int)count;
}

/**
 * display_find() - locate a single display by connector name
 * @out:	receives the result
 * @connector:	DRM connector name, or NULL to take the first display found
 *
 * Return: 0 on success, or a negative errno.
 */
int display_find(struct display *out, const char *connector)
{
	struct display displays[DISPLAY_MAX];
	int count;
	int i;

	if (connector)
		return probe_connector(connector, out);

	count = display_enumerate(displays, DISPLAY_MAX);
	if (count < 0)
		return count;
	if (!count)
		return -ENODEV;

	for (i = 1; i < count; i++)
		log_debug("ignoring additional display %s", displays[i].connector);

	*out = displays[0];

	return 0;
}
