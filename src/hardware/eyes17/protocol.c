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

static int eyes17_read_ack(struct sr_serial_dev_inst *serial)
{
	uint8_t ack;
	if (serial_read_blocking(serial, &ack, 1,
			EYES17_ACK_TIMEOUT_MS) != 1)
		return SR_ERR_TIMEOUT;
	return ((ack & EYES17_ACK_MASK) == EYES17_ACK_OK) ? SR_OK : SR_ERR_DATA;
}

int eyes17_send_cmd(struct sr_serial_dev_inst *serial, uint8_t hdr,
		uint8_t sub, const uint8_t *args, size_t arglen)
{
	uint8_t buf[64];
	if (arglen + 2 > sizeof(buf))
		return SR_ERR_ARG;
	buf[0] = hdr;
	buf[1] = sub;
	memcpy(buf + 2, args, arglen);
	if (serial_write_blocking(serial, buf, arglen + 2,
			EYES17_WRITE_TIMEOUT_MS) != (int)(arglen + 2))
		return SR_ERR_IO;
	return eyes17_read_ack(serial);
}

int eyes17_get_version(struct sr_serial_dev_inst *serial,
		struct eyes17_version *out)
{
	char *buf = NULL;
	int buflen = 64, ret;
	uint8_t dummy = 0;

	if (eyes17_send_cmd(serial, EYES17_HDR_COMMON,
			EYES17_SUB_GET_VERSION, &dummy, 0) != SR_OK)
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
