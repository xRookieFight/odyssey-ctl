// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Samsung Odyssey G5 (G53F) DDC/CI monitor control driver.
 *
 * The monitor exposes its OSD settings over DDC/CI (VESA MCCS 2.1) at I2C
 * address 0x37 on the DDC bus of the DRM connector it is attached to.  This
 * driver wraps the luminance feature in a standard backlight class device so
 * that desktop environments can drive an external panel the same way they
 * drive an internal one, and exports the remaining controls through sysfs.
 *
 * The device is not detected automatically: class based I2C instantiation is
 * not available for DDC buses, so the client has to be created by writing to
 * the connector's new_device attribute.  The shipped udev rule does this.
 *
 * Copyright (C) 2026 xRookieFight
 */

#include <linux/backlight.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/i2c.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/sysfs.h>

#define ODYSSEY_DRV_NAME		"odyssey-ddc"

/*
 * DDC/CI framing.  A host request is
 *
 *	0x51 | 0x80 | len | payload... | checksum
 *
 * where the checksum is the XOR of every preceding byte seeded with the
 * destination address as it appears on the wire (0x37 << 1).  Replies are
 * seeded with the virtual host address 0x50 instead.
 */
#define ODYSSEY_HOST_ADDR		0x51
#define ODYSSEY_DEST_ADDR		0x6e
#define ODYSSEY_REPLY_SEED		0x50
#define ODYSSEY_LEN_FLAG		0x80
#define ODYSSEY_MAX_PAYLOAD		32

/* MCCS command opcodes. */
#define ODYSSEY_CMD_GET			0x01
#define ODYSSEY_CMD_REPLY		0x02
#define ODYSSEY_CMD_SET			0x03
#define ODYSSEY_CMD_SAVE		0x0c

/* VCP feature codes supported by the G53F, as reported by its capability string. */
#define ODYSSEY_VCP_BRIGHTNESS		0x10
#define ODYSSEY_VCP_CONTRAST		0x12
#define ODYSSEY_VCP_COLOR_PRESET	0x14
#define ODYSSEY_VCP_INPUT_SOURCE	0x60
#define ODYSSEY_VCP_AUDIO_VOLUME	0x62
#define ODYSSEY_VCP_POWER_MODE		0xd6

/*
 * VESA DDC/CI 1.1 mandates a 40 ms gap before reading a reply and a 50 ms gap
 * after a set request.  Shorter delays make the G53F NAK sporadically.
 */
#define ODYSSEY_DELAY_GET_MS		40
#define ODYSSEY_DELAY_SET_MS		50
#define ODYSSEY_RETRIES			3

/**
 * struct odyssey_ddc - per display driver state
 * @client:	I2C client sitting at the DDC/CI address
 * @bl:		backlight device exposing the luminance feature
 * @lock:	serialises DDC/CI transactions, which are not reentrant
 */
struct odyssey_ddc {
	struct i2c_client *client;
	struct backlight_device *bl;
	struct mutex lock;
};

static int odyssey_frame_send(struct odyssey_ddc *od, const u8 *payload, size_t len)
{
	u8 frame[ODYSSEY_MAX_PAYLOAD + 3];
	u8 checksum;
	size_t i;
	int ret;

	if (len > ODYSSEY_MAX_PAYLOAD)
		return -EINVAL;

	frame[0] = ODYSSEY_HOST_ADDR;
	frame[1] = ODYSSEY_LEN_FLAG | (u8)len;
	memcpy(frame + 2, payload, len);

	checksum = ODYSSEY_DEST_ADDR;
	for (i = 0; i < len + 2; i++)
		checksum ^= frame[i];
	frame[len + 2] = checksum;

	ret = i2c_master_send(od->client, frame, len + 3);
	if (ret < 0)
		return ret;
	if (ret != (int)(len + 3))
		return -EIO;

	return 0;
}

static int odyssey_frame_recv(struct odyssey_ddc *od, u8 *payload, size_t len)
{
	u8 frame[ODYSSEY_MAX_PAYLOAD + 3];
	size_t total = len + 3;
	u8 checksum;
	size_t i;
	int ret;

	if (len > ODYSSEY_MAX_PAYLOAD)
		return -EINVAL;

	ret = i2c_master_recv(od->client, frame, total);
	if (ret < 0)
		return ret;
	if (ret != (int)total)
		return -EIO;

	if (frame[0] != ODYSSEY_DEST_ADDR)
		return -EPROTO;
	if ((frame[1] & ~ODYSSEY_LEN_FLAG) != len)
		return -EPROTO;

	checksum = ODYSSEY_REPLY_SEED;
	for (i = 0; i < total - 1; i++)
		checksum ^= frame[i];
	if (checksum != frame[total - 1])
		return -EBADMSG;

	memcpy(payload, frame + 2, len);

	return 0;
}

/**
 * odyssey_vcp_get() - read a VCP feature
 * @od:		driver state
 * @opcode:	VCP feature code
 * @current_val:	where to store the current value, may be NULL
 * @max_val:	where to store the maximum value, may be NULL
 *
 * Return: 0 on success, a negative errno otherwise.  Callers must hold @od->lock.
 */
static int odyssey_vcp_get(struct odyssey_ddc *od, u8 opcode, u16 *current_val,
			   u16 *max_val)
{
	const u8 request[] = { ODYSSEY_CMD_GET, opcode };
	u8 reply[8];
	int attempt;
	int ret = -EIO;

	for (attempt = 0; attempt < ODYSSEY_RETRIES; attempt++) {
		ret = odyssey_frame_send(od, request, sizeof(request));
		if (ret) {
			msleep(ODYSSEY_DELAY_GET_MS);
			continue;
		}

		msleep(ODYSSEY_DELAY_GET_MS);

		ret = odyssey_frame_recv(od, reply, sizeof(reply));
		if (ret) {
			msleep(ODYSSEY_DELAY_GET_MS);
			continue;
		}

		if (reply[0] != ODYSSEY_CMD_REPLY || reply[2] != opcode) {
			ret = -EPROTO;
			msleep(ODYSSEY_DELAY_GET_MS);
			continue;
		}

		/* reply[1] is the MCCS result code; non-zero means unsupported. */
		if (reply[1])
			return -EOPNOTSUPP;

		if (max_val)
			*max_val = ((u16)reply[4] << 8) | reply[5];
		if (current_val)
			*current_val = ((u16)reply[6] << 8) | reply[7];

		return 0;
	}

	return ret;
}

/**
 * odyssey_vcp_set() - write a VCP feature
 * @od:		driver state
 * @opcode:	VCP feature code
 * @value:	value to write
 *
 * The monitor does not acknowledge set requests, so failures are only visible
 * as I2C level errors.  Callers must hold @od->lock.
 *
 * Return: 0 on success, a negative errno otherwise.
 */
static int odyssey_vcp_set(struct odyssey_ddc *od, u8 opcode, u16 value)
{
	const u8 request[] = {
		ODYSSEY_CMD_SET, opcode, (u8)(value >> 8), (u8)value,
	};
	int attempt;
	int ret = -EIO;

	for (attempt = 0; attempt < ODYSSEY_RETRIES; attempt++) {
		ret = odyssey_frame_send(od, request, sizeof(request));
		msleep(ODYSSEY_DELAY_SET_MS);
		if (!ret)
			break;
	}

	return ret;
}

static int odyssey_bl_update_status(struct backlight_device *bl)
{
	struct odyssey_ddc *od = bl_get_data(bl);
	int brightness = backlight_get_brightness(bl);
	int ret;

	mutex_lock(&od->lock);
	ret = odyssey_vcp_set(od, ODYSSEY_VCP_BRIGHTNESS, (u16)brightness);
	mutex_unlock(&od->lock);

	return ret;
}

static int odyssey_bl_get_brightness(struct backlight_device *bl)
{
	struct odyssey_ddc *od = bl_get_data(bl);
	u16 current_val;
	int ret;

	mutex_lock(&od->lock);
	ret = odyssey_vcp_get(od, ODYSSEY_VCP_BRIGHTNESS, &current_val, NULL);
	mutex_unlock(&od->lock);
	if (ret)
		return ret;

	return current_val;
}

static const struct backlight_ops odyssey_bl_ops = {
	.options	= BL_CORE_SUSPENDRESUME,
	.update_status	= odyssey_bl_update_status,
	.get_brightness	= odyssey_bl_get_brightness,
};

static ssize_t odyssey_feature_show(struct device *dev, u8 opcode, char *buf)
{
	struct odyssey_ddc *od = dev_get_drvdata(dev);
	u16 current_val;
	int ret;

	mutex_lock(&od->lock);
	ret = odyssey_vcp_get(od, opcode, &current_val, NULL);
	mutex_unlock(&od->lock);
	if (ret)
		return ret;

	return sysfs_emit(buf, "%u\n", current_val);
}

static ssize_t odyssey_feature_store(struct device *dev, u8 opcode, const char *buf,
				     size_t count)
{
	struct odyssey_ddc *od = dev_get_drvdata(dev);
	u16 value;
	int ret;

	ret = kstrtou16(buf, 0, &value);
	if (ret)
		return ret;

	mutex_lock(&od->lock);
	ret = odyssey_vcp_set(od, opcode, value);
	mutex_unlock(&od->lock);
	if (ret)
		return ret;

	return count;
}

#define ODYSSEY_FEATURE_ATTR_RW(_name, _opcode)					\
static ssize_t _name##_show(struct device *dev,					\
			    struct device_attribute *attr, char *buf)		\
{										\
	return odyssey_feature_show(dev, _opcode, buf);				\
}										\
static ssize_t _name##_store(struct device *dev,				\
			     struct device_attribute *attr, const char *buf,	\
			     size_t count)					\
{										\
	return odyssey_feature_store(dev, _opcode, buf, count);			\
}										\
static DEVICE_ATTR_RW(_name)

ODYSSEY_FEATURE_ATTR_RW(contrast, ODYSSEY_VCP_CONTRAST);
ODYSSEY_FEATURE_ATTR_RW(color_preset, ODYSSEY_VCP_COLOR_PRESET);
ODYSSEY_FEATURE_ATTR_RW(input_source, ODYSSEY_VCP_INPUT_SOURCE);
ODYSSEY_FEATURE_ATTR_RW(audio_volume, ODYSSEY_VCP_AUDIO_VOLUME);
ODYSSEY_FEATURE_ATTR_RW(power_mode, ODYSSEY_VCP_POWER_MODE);

static struct attribute *odyssey_attrs[] = {
	&dev_attr_contrast.attr,
	&dev_attr_color_preset.attr,
	&dev_attr_input_source.attr,
	&dev_attr_audio_volume.attr,
	&dev_attr_power_mode.attr,
	NULL,
};
ATTRIBUTE_GROUPS(odyssey);

static int odyssey_ddc_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct backlight_properties props = { };
	struct odyssey_ddc *od;
	u16 current_val, max_val;
	int ret;

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C))
		return -ENODEV;

	od = devm_kzalloc(dev, sizeof(*od), GFP_KERNEL);
	if (!od)
		return -ENOMEM;

	od->client = client;
	mutex_init(&od->lock);
	i2c_set_clientdata(client, od);
	dev_set_drvdata(dev, od);

	/*
	 * Probing the luminance feature doubles as a presence check: a bus with
	 * nothing listening at 0x37 fails here instead of registering a
	 * backlight device that can never be driven.
	 */
	mutex_lock(&od->lock);
	ret = odyssey_vcp_get(od, ODYSSEY_VCP_BRIGHTNESS, &current_val, &max_val);
	mutex_unlock(&od->lock);
	if (ret)
		return dev_err_probe(dev, ret, "no DDC/CI responder on this bus\n");

	if (!max_val)
		return dev_err_probe(dev, -EINVAL,
				     "display reports a zero luminance range\n");

	props.type = BACKLIGHT_RAW;
	props.scale = BACKLIGHT_SCALE_LINEAR;
	props.max_brightness = max_val;
	props.brightness = current_val;

	od->bl = devm_backlight_device_register(dev, ODYSSEY_DRV_NAME, dev, od,
						&odyssey_bl_ops, &props);
	if (IS_ERR(od->bl))
		return dev_err_probe(dev, PTR_ERR(od->bl),
				     "failed to register backlight device\n");

	dev_info(dev, "DDC/CI display attached, luminance range 0-%u\n", max_val);

	return 0;
}

static const struct i2c_device_id odyssey_ddc_id[] = {
	{ ODYSSEY_DRV_NAME },
	{ }
};
MODULE_DEVICE_TABLE(i2c, odyssey_ddc_id);

static struct i2c_driver odyssey_ddc_driver = {
	.driver = {
		.name		= ODYSSEY_DRV_NAME,
		.dev_groups	= odyssey_groups,
	},
	.probe		= odyssey_ddc_probe,
	.id_table	= odyssey_ddc_id,
};
module_i2c_driver(odyssey_ddc_driver);

MODULE_AUTHOR("xRookieFight");
MODULE_DESCRIPTION("Samsung Odyssey G5 (G53F) DDC/CI monitor control driver");
MODULE_LICENSE("GPL");
