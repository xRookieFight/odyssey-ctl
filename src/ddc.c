// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * DDC/CI transport over a Linux i2c-dev character device.
 *
 * A host request is framed as
 *
 *	0x51 | 0x80 | len | payload... | checksum
 *
 * where the checksum is the XOR of every preceding byte seeded with the
 * destination address as it appears on the wire (0x37 << 1).  Replies use the
 * virtual host address 0x50 as the seed instead.
 *
 * Copyright (C) 2026 xRookieFight
 */

#include <errno.h>
#include <fcntl.h>
#include <linux/i2c-dev.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

#include "ddc.h"
#include "log.h"

#define DDC_HOST_ADDR		0x51
#define DDC_DEST_ADDR		0x6e
#define DDC_REPLY_SEED		0x50
#define DDC_LEN_FLAG		0x80

/* MCCS command opcodes. */
#define DDC_CMD_GET		0x01
#define DDC_CMD_REPLY		0x02
#define DDC_CMD_SET		0x03
#define DDC_CMD_SAVE		0x0c
#define DDC_CMD_CAPS		0xf3
#define DDC_CMD_CAPS_REPLY	0xe3

/*
 * VESA DDC/CI 1.1 mandates a 40 ms gap before reading a reply and a 50 ms gap
 * after a set request.  Shorter delays make the G53F NAK sporadically.
 */
#define DDC_DELAY_GET_MS	40
#define DDC_DELAY_SET_MS	50
#define DDC_RETRIES		3

static void ddc_sleep_ms(long ms)
{
	struct timespec ts = {
		.tv_sec = ms / 1000,
		.tv_nsec = (ms % 1000) * 1000000L,
	};

	while (nanosleep(&ts, &ts) == -1 && errno == EINTR)
		;
}

static int ddc_frame_send(struct ddc_conn *conn, const uint8_t *payload, size_t len)
{
	uint8_t frame[DDC_MAX_PAYLOAD + 3];
	uint8_t checksum;
	size_t i;
	ssize_t written;

	if (len > DDC_MAX_PAYLOAD)
		return -EINVAL;

	frame[0] = DDC_HOST_ADDR;
	frame[1] = DDC_LEN_FLAG | (uint8_t)len;
	memcpy(frame + 2, payload, len);

	checksum = DDC_DEST_ADDR;
	for (i = 0; i < len + 2; i++)
		checksum ^= frame[i];
	frame[len + 2] = checksum;

	written = write(conn->fd, frame, len + 3);
	if (written < 0)
		return -errno;
	if ((size_t)written != len + 3)
		return -EIO;

	return 0;
}

static int ddc_frame_recv(struct ddc_conn *conn, uint8_t *payload, size_t *len)
{
	uint8_t frame[DDC_MAX_PAYLOAD + 3];
	size_t total = *len + 3;
	uint8_t checksum;
	size_t reported;
	size_t i;
	ssize_t got;

	if (*len > DDC_MAX_PAYLOAD)
		return -EINVAL;

	got = read(conn->fd, frame, total);
	if (got < 0)
		return -errno;
	if ((size_t)got != total)
		return -EIO;

	if (frame[0] != DDC_DEST_ADDR)
		return -EPROTO;

	reported = frame[1] & (uint8_t)~DDC_LEN_FLAG;
	if (reported > *len)
		return -EPROTO;

	/*
	 * Replies are variable length, so the trailing checksum sits right after
	 * the payload the header announced rather than at the end of whatever
	 * the bus handed back.
	 */
	checksum = DDC_REPLY_SEED;
	for (i = 0; i < reported + 2; i++)
		checksum ^= frame[i];
	if (checksum != frame[reported + 2])
		return -EBADMSG;

	memcpy(payload, frame + 2, reported);
	*len = reported;

	return 0;
}

int ddc_open(struct ddc_conn *conn, const char *i2c_path)
{
	int fd;

	fd = open(i2c_path, O_RDWR | O_CLOEXEC);
	if (fd < 0)
		return -errno;

	if (ioctl(fd, I2C_SLAVE, DDC_SLAVE_ADDR) < 0) {
		int saved = -errno;

		close(fd);
		return saved;
	}

	conn->fd = fd;
	snprintf(conn->path, sizeof(conn->path), "%s", i2c_path);

	return 0;
}

void ddc_close(struct ddc_conn *conn)
{
	if (conn->fd >= 0) {
		close(conn->fd);
		conn->fd = -1;
	}
}

int ddc_get_vcp(struct ddc_conn *conn, uint8_t opcode, struct ddc_vcp *out)
{
	const uint8_t request[] = { DDC_CMD_GET, opcode };
	int attempt;
	int ret = -EIO;

	for (attempt = 0; attempt < DDC_RETRIES; attempt++) {
		uint8_t reply[8];
		size_t len = sizeof(reply);

		ret = ddc_frame_send(conn, request, sizeof(request));
		if (ret) {
			ddc_sleep_ms(DDC_DELAY_GET_MS);
			continue;
		}

		ddc_sleep_ms(DDC_DELAY_GET_MS);

		ret = ddc_frame_recv(conn, reply, &len);
		if (ret) {
			log_debug("get 0x%02x: read failed: %s", opcode, strerror(-ret));
			ddc_sleep_ms(DDC_DELAY_GET_MS);
			continue;
		}

		if (len != sizeof(reply) || reply[0] != DDC_CMD_REPLY ||
		    reply[2] != opcode) {
			ret = -EPROTO;
			ddc_sleep_ms(DDC_DELAY_GET_MS);
			continue;
		}

		/* reply[1] is the MCCS result code; non-zero means unsupported. */
		if (reply[1])
			return -EOPNOTSUPP;

		out->opcode = opcode;
		out->type = reply[3];
		out->maximum = ((uint16_t)reply[4] << 8) | reply[5];
		out->current = ((uint16_t)reply[6] << 8) | reply[7];

		return 0;
	}

	return ret;
}

int ddc_set_vcp(struct ddc_conn *conn, uint8_t opcode, uint16_t value)
{
	const uint8_t request[] = {
		DDC_CMD_SET, opcode, (uint8_t)(value >> 8), (uint8_t)value,
	};
	int attempt;
	int ret = -EIO;

	/*
	 * The display does not acknowledge set requests, so a failure is only
	 * observable as an I2C level error on the write itself.
	 */
	for (attempt = 0; attempt < DDC_RETRIES; attempt++) {
		ret = ddc_frame_send(conn, request, sizeof(request));
		ddc_sleep_ms(DDC_DELAY_SET_MS);
		if (!ret)
			break;
	}

	return ret;
}

int ddc_save_settings(struct ddc_conn *conn)
{
	const uint8_t request[] = { DDC_CMD_SAVE };
	int ret;

	ret = ddc_frame_send(conn, request, sizeof(request));
	ddc_sleep_ms(DDC_DELAY_SET_MS);

	return ret;
}

/**
 * ddc_read_capabilities() - reassemble the display's capability string
 * @conn:	open connection
 * @buf:	destination buffer, NUL terminated on success
 * @len:	size of @buf
 *
 * The string is transferred in fragments addressed by a byte offset.  A reply
 * carrying no data marks the end of the string.
 *
 * Return: number of bytes written excluding the terminator, or a negative errno.
 */
ssize_t ddc_read_capabilities(struct ddc_conn *conn, char *buf, size_t len)
{
	size_t offset = 0;
	int attempt = 0;

	if (!len)
		return -EINVAL;

	while (offset + 1 < len) {
		const uint8_t request[] = {
			DDC_CMD_CAPS, (uint8_t)(offset >> 8), (uint8_t)offset,
		};
		uint8_t reply[35];
		size_t reply_len = sizeof(reply);
		size_t chunk;
		int ret;

		ret = ddc_frame_send(conn, request, sizeof(request));
		if (!ret) {
			ddc_sleep_ms(DDC_DELAY_GET_MS);
			reply_len = sizeof(reply);
			ret = ddc_frame_recv(conn, reply, &reply_len);
		}

		if (ret || reply_len < 3 || reply[0] != DDC_CMD_CAPS_REPLY) {
			if (++attempt >= DDC_RETRIES)
				return ret ? ret : -EPROTO;
			ddc_sleep_ms(DDC_DELAY_GET_MS);
			continue;
		}

		attempt = 0;

		/* An empty fragment terminates the string. */
		chunk = reply_len - 3;
		if (!chunk)
			break;

		if (chunk > len - offset - 1)
			chunk = len - offset - 1;

		memcpy(buf + offset, reply + 3, chunk);
		offset += chunk;

		ddc_sleep_ms(DDC_DELAY_GET_MS);
	}

	buf[offset] = '\0';

	return (ssize_t)offset;
}
