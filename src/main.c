// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * odyssey-ctl - control a Samsung Odyssey G5 (G53F) over DDC/CI.
 *
 * Copyright (C) 2026 xRookieFight
 */

#include <errno.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "config.h"
#include "ddc.h"
#include "display.h"
#include "log.h"
#include "vcp.h"

#ifndef ODYSSEY_VERSION
#define ODYSSEY_VERSION	"0.0.0"
#endif

#define EXIT_USAGE	2

struct options {
	const char *connector;
	const char *i2c_path;
	const char *profile;
	int save;
};

static void usage(FILE *stream)
{
	fprintf(stream,
		"Usage: odyssey-ctl [OPTION]... COMMAND [ARGUMENT]...\n"
		"\n"
		"Control a DDC/CI capable display from the command line.\n"
		"\n"
		"Commands:\n"
		"  list                  list connected displays that expose a DDC bus\n"
		"  info                  show the selected display's identity\n"
		"  features              list the features this tool knows about\n"
		"  caps                  dump the display's raw capability string\n"
		"  get FEATURE           read a feature\n"
		"  set FEATURE VALUE     write a feature\n"
		"  apply [FILE]          write every feature listed in a profile\n"
		"\n"
		"Options:\n"
		"  -d, --display NAME    select a DRM connector, for example card1-HDMI-A-1\n"
		"  -b, --bus PATH        use an i2c-dev node directly, bypassing discovery\n"
		"  -s, --save            ask the display to persist the change\n"
		"  -v, --verbose         raise the log level, repeatable\n"
		"  -h, --help            show this help and exit\n"
		"  -V, --version         show version information and exit\n");
}

static int open_selected(const struct options *opts, struct ddc_conn *conn,
			 struct display *disp)
{
	int ret;

	if (opts->i2c_path) {
		memset(disp, 0, sizeof(*disp));
		snprintf(disp->i2c_path, sizeof(disp->i2c_path), "%s", opts->i2c_path);
	} else {
		ret = display_find(disp, opts->connector);
		if (ret) {
			log_error("no usable display found: %s", strerror(-ret));
			return ret;
		}
	}

	ret = ddc_open(conn, disp->i2c_path);
	if (ret) {
		log_error("cannot open %s: %s", disp->i2c_path, strerror(-ret));
		if (ret == -EACCES)
			log_error("install the shipped udev rule or run as root");
		return ret;
	}

	return 0;
}

static int cmd_list(void)
{
	struct display displays[DISPLAY_MAX];
	int count;
	int i;

	count = display_enumerate(displays, DISPLAY_MAX);
	if (count < 0) {
		log_error("cannot enumerate displays: %s", strerror(-count));
		return EXIT_FAILURE;
	}

	if (!count) {
		printf("No connected display exposes a DDC bus.\n");
		return EXIT_SUCCESS;
	}

	for (i = 0; i < count; i++) {
		const struct display *disp = &displays[i];

		printf("%-20s %-12s %s %s\n", disp->connector, disp->i2c_path,
		       disp->mfg, disp->model[0] ? disp->model : "(unnamed)");
	}

	return EXIT_SUCCESS;
}

static int cmd_info(const struct options *opts)
{
	struct ddc_conn conn = { .fd = -1 };
	struct display disp;

	if (open_selected(opts, &conn, &disp))
		return EXIT_FAILURE;

	printf("Connector:    %s\n", disp.connector[0] ? disp.connector : "(unknown)");
	printf("I2C bus:      %s\n", disp.i2c_path);
	printf("Manufacturer: %s\n", disp.mfg[0] ? disp.mfg : "(unknown)");
	printf("Model:        %s\n", disp.model[0] ? disp.model : "(unknown)");
	printf("Product code: 0x%04x\n", disp.product);
	printf("Serial:       %s\n", disp.serial[0] ? disp.serial : "(unknown)");

	ddc_close(&conn);

	return EXIT_SUCCESS;
}

static int cmd_features(void)
{
	const struct vcp_feature *table = vcp_table();
	size_t i;

	for (i = 0; i < vcp_table_size(); i++) {
		const struct vcp_feature *feature = &table[i];

		printf("%-14s 0x%02x  %-40s%s\n", feature->name, feature->opcode,
		       feature->summary, feature->read_only ? " (read-only)" : "");

		if (feature->values) {
			const struct vcp_enum *value;

			printf("%-14s       values:", "");
			for (value = feature->values; value->name; value++)
				printf(" %s", value->name);
			printf("\n");
		}
	}

	return EXIT_SUCCESS;
}

static int cmd_caps(const struct options *opts)
{
	struct ddc_conn conn = { .fd = -1 };
	struct display disp;
	char caps[DDC_CAPS_MAX];
	ssize_t len;
	int status = EXIT_FAILURE;

	if (open_selected(opts, &conn, &disp))
		return EXIT_FAILURE;

	len = ddc_read_capabilities(&conn, caps, sizeof(caps));
	if (len < 0)
		log_error("capability request failed: %s", strerror((int)-len));
	else {
		printf("%s\n", caps);
		status = EXIT_SUCCESS;
	}

	ddc_close(&conn);

	return status;
}

static int cmd_get(const struct options *opts, const char *name)
{
	const struct vcp_feature *feature;
	struct ddc_conn conn = { .fd = -1 };
	struct display disp;
	struct ddc_vcp value;
	const char *label;
	int ret;

	feature = vcp_lookup(name);
	if (!feature) {
		log_error("unknown feature '%s', try 'odyssey-ctl features'", name);
		return EXIT_USAGE;
	}

	if (open_selected(opts, &conn, &disp))
		return EXIT_FAILURE;

	ret = ddc_get_vcp(&conn, feature->opcode, &value);
	ddc_close(&conn);

	if (ret) {
		log_error("cannot read %s: %s", feature->name, strerror(-ret));
		return EXIT_FAILURE;
	}

	label = vcp_format_value(feature, value.current);
	if (label)
		printf("%s = %s (%u, max %u)\n", feature->name, label, value.current,
		       value.maximum);
	else
		printf("%s = %u (max %u)\n", feature->name, value.current,
		       value.maximum);

	return EXIT_SUCCESS;
}

static int cmd_set(const struct options *opts, const char *name, const char *text)
{
	const struct vcp_feature *feature;
	struct ddc_conn conn = { .fd = -1 };
	struct display disp;
	uint16_t value;
	int ret;

	feature = vcp_lookup(name);
	if (!feature) {
		log_error("unknown feature '%s', try 'odyssey-ctl features'", name);
		return EXIT_USAGE;
	}

	if (feature->read_only) {
		log_error("'%s' is read-only", feature->name);
		return EXIT_USAGE;
	}

	if (vcp_parse_value(feature, text, &value)) {
		log_error("invalid value '%s' for %s", text, feature->name);
		return EXIT_USAGE;
	}

	if (open_selected(opts, &conn, &disp))
		return EXIT_FAILURE;

	ret = ddc_set_vcp(&conn, feature->opcode, value);
	if (!ret && opts->save)
		ret = ddc_save_settings(&conn);

	ddc_close(&conn);

	if (ret) {
		log_error("cannot write %s: %s", feature->name, strerror(-ret));
		return EXIT_FAILURE;
	}

	log_info("%s set to %u", feature->name, value);

	return EXIT_SUCCESS;
}

static int cmd_apply(const struct options *opts, const char *path)
{
	struct config_entry entries[CONFIG_MAX_ENTRIES];
	struct ddc_conn conn = { .fd = -1 };
	struct display disp;
	char default_path[256];
	int count;
	int failures = 0;
	int i;

	if (!path) {
		if (config_default_path(default_path, sizeof(default_path))) {
			log_error("cannot determine the default profile path");
			return EXIT_FAILURE;
		}
		path = default_path;
	}

	count = config_load(path, entries, CONFIG_MAX_ENTRIES);
	if (count < 0) {
		log_error("cannot read %s: %s", path, strerror(-count));
		return EXIT_FAILURE;
	}

	if (!count) {
		log_warn("%s contains no applicable settings", path);
		return EXIT_SUCCESS;
	}

	if (open_selected(opts, &conn, &disp))
		return EXIT_FAILURE;

	for (i = 0; i < count; i++) {
		int ret = ddc_set_vcp(&conn, entries[i].feature->opcode, entries[i].value);

		if (ret) {
			log_error("cannot write %s: %s", entries[i].feature->name,
				  strerror(-ret));
			failures++;
			continue;
		}

		log_info("%s set to %u", entries[i].feature->name, entries[i].value);
	}

	if (!failures && opts->save)
		ddc_save_settings(&conn);

	ddc_close(&conn);

	return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}

int main(int argc, char *argv[])
{
	static const struct option long_options[] = {
		{ "display", required_argument, NULL, 'd' },
		{ "bus",     required_argument, NULL, 'b' },
		{ "save",    no_argument,       NULL, 's' },
		{ "verbose", no_argument,       NULL, 'v' },
		{ "help",    no_argument,       NULL, 'h' },
		{ "version", no_argument,       NULL, 'V' },
		{ NULL,      0,                 NULL, 0 },
	};
	struct options opts = { 0 };
	int verbosity = LOG_WARN;
	const char *command;
	int opt;

	while ((opt = getopt_long(argc, argv, "d:b:svhV", long_options, NULL)) != -1) {
		switch (opt) {
		case 'd':
			opts.connector = optarg;
			break;
		case 'b':
			opts.i2c_path = optarg;
			break;
		case 's':
			opts.save = 1;
			break;
		case 'v':
			if (verbosity < LOG_DEBUG)
				verbosity++;
			break;
		case 'h':
			usage(stdout);
			return EXIT_SUCCESS;
		case 'V':
			printf("odyssey-ctl %s\n", ODYSSEY_VERSION);
			return EXIT_SUCCESS;
		default:
			usage(stderr);
			return EXIT_USAGE;
		}
	}

	log_set_level(verbosity);

	if (optind >= argc) {
		usage(stderr);
		return EXIT_USAGE;
	}

	command = argv[optind++];

	if (!strcmp(command, "list"))
		return cmd_list();

	if (!strcmp(command, "info"))
		return cmd_info(&opts);

	if (!strcmp(command, "features"))
		return cmd_features();

	if (!strcmp(command, "caps"))
		return cmd_caps(&opts);

	if (!strcmp(command, "get")) {
		if (optind >= argc) {
			log_error("get requires a feature name");
			return EXIT_USAGE;
		}
		return cmd_get(&opts, argv[optind]);
	}

	if (!strcmp(command, "set")) {
		if (optind + 1 >= argc) {
			log_error("set requires a feature name and a value");
			return EXIT_USAGE;
		}
		return cmd_set(&opts, argv[optind], argv[optind + 1]);
	}

	if (!strcmp(command, "apply"))
		return cmd_apply(&opts, optind < argc ? argv[optind] : NULL);

	log_error("unknown command '%s'", command);
	usage(stderr);

	return EXIT_USAGE;
}
