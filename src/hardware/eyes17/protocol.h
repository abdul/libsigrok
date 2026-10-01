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

/* Transport timeouts. */
#define EYES17_ACK_TIMEOUT_MS 1000
#define EYES17_WRITE_TIMEOUT_MS 100
#define EYES17_READLINE_TIMEOUT_MS 1000

/* Timebase and single-channel capture (acquisition.c). ADC tick is 8 MHz. */
#define EYES17_ADC_CLOCK_HZ 8000000ULL
#define EYES17_TIMEBASE_MIN_US 1.5
#define EYES17_TB8_MIN 12
#define EYES17_CHOSA_A1 3
#define EYES17_CAPTURE_FRAME_LEN 7
#define EYES17_CAPTURE_TIMEOUT_MS 2000

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
SR_PRIV uint16_t eyes17_timebase_to_tb8(double timebase_us);
SR_PRIV void eyes17_tb8_to_rate(uint16_t tb8, uint64_t *num, uint64_t *den);
SR_PRIV uint16_t eyes17_clamp_count(size_t count);
SR_PRIV size_t eyes17_build_capture_one(uint8_t *buf, uint16_t tb8,
		uint16_t count);
SR_PRIV float eyes17_adc_to_volts(uint16_t raw, int gain);
SR_PRIV int eyes17_capture_one(struct sr_serial_dev_inst *serial,
		uint16_t tb8, uint16_t count, int gain, float *volts_out);

#endif
