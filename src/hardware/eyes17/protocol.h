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

#ifndef LIBSIGROK_HARDWARE_EYES17_PROTOCOL_H
#define LIBSIGROK_HARDWARE_EYES17_PROTOCOL_H

#include <stdint.h>
#include <glib.h>
#include <libsigrok/libsigrok.h>
#include "libsigrok-internal.h"

#define LOG_PREFIX "eyes17"

#define EYES17_BAUD 500000
#define EYES17_MAX_SAMPLES 10000

/* Protocol headers (from golden commands_proto.py mapping). */
#define EYES17_HDR_ADC 2
#define EYES17_SUB_CAPTURE_ONE 1
#define EYES17_HDR_COMMON 11
#define EYES17_SUB_GET_VERSION 5

struct eyes17_version {
	char raw[32];
	int major;
	int minor;
};

struct dev_context {
	struct sr_serial_dev_inst *serial;
	struct eyes17_version fw;
	GByteArray *rxbuf;
};

SR_PRIV void eyes17_put_u16_le(uint8_t *p, uint16_t v);
SR_PRIV void eyes17_put_u32_le(uint8_t *p, uint32_t v);
SR_PRIV uint16_t eyes17_get_u16_le(const uint8_t *p);
SR_PRIV uint32_t eyes17_get_u32_le(const uint8_t *p);
SR_PRIV int eyes17_parse_version(const char *s, struct eyes17_version *out);
SR_PRIV int eyes17_send_cmd(struct sr_serial_dev_inst *serial, uint8_t hdr,
		uint8_t sub, const uint8_t *args, size_t arglen);
SR_PRIV int eyes17_get_version(struct sr_serial_dev_inst *serial,
		struct eyes17_version *out);

#endif
