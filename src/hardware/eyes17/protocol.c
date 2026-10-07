/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2013 Uwe Hermann <uwe@hermann-uwe.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include <config.h>
#include <string.h>
#include "protocol.h"

#define EYES17_ACK_MASK 0x03
#define EYES17_ACK_OK 0x01

int eyes17_read_ack(struct sr_serial_dev_inst *serial)
{
	uint8_t ack;
	if (serial_read_blocking(serial, &ack, 1,
			EYES17_ACK_TIMEOUT_MS) != 1)
		return SR_ERR_TIMEOUT;
	return ((ack & EYES17_ACK_MASK) == EYES17_ACK_OK) ? SR_OK : SR_ERR_DATA;
}

int eyes17_write_cmd(struct sr_serial_dev_inst *serial, uint8_t hdr,
		uint8_t sub, const uint8_t *args, size_t arglen)
{
	uint8_t buf[64];
	if (arglen + 2 > sizeof(buf))
		return SR_ERR_ARG;
	buf[0] = hdr;
	buf[1] = sub;
	if (arglen)
		memcpy(buf + 2, args, arglen);
	if (serial_write_blocking(serial, buf, arglen + 2,
			EYES17_WRITE_TIMEOUT_MS) != (int)(arglen + 2))
		return SR_ERR_IO;
	return SR_OK;
}

int eyes17_send_cmd(struct sr_serial_dev_inst *serial, uint8_t hdr,
		uint8_t sub, const uint8_t *args, size_t arglen)
{
	int ret;

	ret = eyes17_write_cmd(serial, hdr, sub, args, arglen);
	if (ret != SR_OK)
		return ret;
	return eyes17_read_ack(serial);
}

int eyes17_get_version(struct sr_serial_dev_inst *serial,
		struct eyes17_version *out)
{
	char *buf = NULL;
	int buflen = 64, ret;

	/*
	 * GET_VERSION answers with the version string directly -- there
	 * is no ACK byte (golden packet_handler.get_version: write
	 * COMMON + GET_VERSION, then readline; hardware-observed
	 * "SJ-2.4\\x0c\\n"). A shared send_cmd (write + ACK read)
	 * would consume the 'S' as the ACK and fail the probe.
	 */
	if (eyes17_write_cmd(serial, EYES17_HDR_COMMON,
			EYES17_SUB_GET_VERSION, NULL, 0) != SR_OK)
		return SR_ERR_IO;
	buf = g_malloc0(buflen);
	ret = serial_readline(serial, &buf, &buflen,
		EYES17_READLINE_TIMEOUT_MS);
	if (ret == SR_OK && buflen > 0)
		ret = eyes17_parse_version(buf, out);
	else
		ret = SR_ERR_TIMEOUT;
	g_free(buf);
	return ret;
}

void eyes17_put_u16_le(uint8_t *p, uint16_t v)
{
	p[0] = v & 0xff;
	p[1] = (v >> 8) & 0xff;
}

void eyes17_put_u32_le(uint8_t *p, uint32_t v)
{
	p[0] = v & 0xff;
	p[1] = (v >> 8) & 0xff;
	p[2] = (v >> 16) & 0xff;
	p[3] = (v >> 24) & 0xff;
}

uint16_t eyes17_get_u16_le(const uint8_t *p)
{
	return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

uint32_t eyes17_get_u32_le(const uint8_t *p)
{
	return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
		((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

int eyes17_parse_version(const char *s, struct eyes17_version *out)
{
	int major, minor;
	if (!s || !out)
		return SR_ERR_ARG;
	if (strncmp(s, "SJ", 2) != 0)
		return SR_ERR_DATA;
	if (sscanf(s, "SJ-%d.%d", &major, &minor) != 2)
		return SR_ERR_DATA;
	g_strlcpy(out->raw, s, sizeof(out->raw));
	out->major = major;
	out->minor = minor;
	return SR_OK;
}
size_t eyes17_build_trigger(uint8_t *buf, uint16_t level)
{
	if (!buf)
		return 0;
	buf[0] = EYES17_HDR_ADC;
	buf[1] = EYES17_SUB_CONFIGURE_TRIGGER;
	buf[2] = (0 << 4) | (1 << EYES17_TRIGGER_CHAN_A1);
	eyes17_put_u16_le(buf + 3, level);
	return EYES17_TRIGGER_FRAME_LEN;
}
int eyes17_trigger_level_code(double volts, int gain, uint16_t *code_out)
{
	double f, raw;

	if (!code_out || !eyes17_gain_is_valid(gain))
		return SR_ERR_ARG;
	f = eyes17_gain_factor(gain);
	raw = (16.5 - volts * f) * 1023.0 / 33.0;
	if (raw < 0.0 || raw > (double)EYES17_TRIGGER_LEVEL_MAX)
		return SR_ERR_ARG;
	*code_out = (uint16_t)(raw + 0.5);
	return SR_OK;
}
int eyes17_trigger_level_code_12(double volts, int gain, uint16_t *code_out)
{
	double f, raw;

	if (!code_out || !eyes17_gain_is_valid(gain))
		return SR_ERR_ARG;
	f = eyes17_gain_factor(gain);
	raw = (16.5 - volts * f) * 4095.0 / 33.0;
	if (raw < 0.0 || raw > (double)EYES17_TRIGGER_LEVEL_MAX_12)
		return SR_ERR_ARG;
	*code_out = (uint16_t)(raw + 0.5);
	return SR_OK;
}
int eyes17_configure_trigger(struct sr_serial_dev_inst *serial, uint16_t level)
{
	uint8_t args[3];

	args[0] = (0 << 4) | (1 << EYES17_TRIGGER_CHAN_A1);
	eyes17_put_u16_le(args + 1, level);
	return eyes17_send_cmd(serial, EYES17_HDR_ADC,
		EYES17_SUB_CONFIGURE_TRIGGER, args, sizeof(args));
}
