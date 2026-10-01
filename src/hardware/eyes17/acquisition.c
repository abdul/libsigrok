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
#include "protocol.h"

uint16_t eyes17_timebase_to_tb8(double timebase_us)
{
	double tb;

	if (!(timebase_us >= EYES17_TIMEBASE_MIN_US))
		timebase_us = EYES17_TIMEBASE_MIN_US;
	tb = timebase_us * 8.0 + 0.5;
	if (tb > 0xffff)
		return 0xffff;
	return (uint16_t)tb;
}

void eyes17_tb8_to_rate(uint16_t tb8, uint64_t *num, uint64_t *den)
{
	if (!tb8)
		tb8 = EYES17_TB8_MIN;
	if (num)
		*num = EYES17_ADC_CLOCK_HZ;
	if (den)
		*den = tb8;
}

uint16_t eyes17_clamp_count(size_t count)
{
	if (count > EYES17_MAX_SAMPLES)
		return EYES17_MAX_SAMPLES;
	return (uint16_t)count;
}

size_t eyes17_build_capture_one(uint8_t *buf, uint16_t tb8, uint16_t count)
{
	if (!buf)
		return 0;
	buf[0] = EYES17_HDR_ADC;
	buf[1] = EYES17_SUB_CAPTURE_ONE;
	buf[2] = EYES17_CHOSA_A1;
	eyes17_put_u16_le(buf + 3, tb8);
	eyes17_put_u16_le(buf + 5, count);
	return EYES17_CAPTURE_FRAME_LEN;
}

/*
 * Single-channel immediate capture (trigger-disabled path): send
 * CAPTURE_ONE for A1, then fetch the 2 x count-byte bulk reply and
 * decode it to volts. Triggered captures land in Plan 2 (M3); no
 * trigger-setup command exists on this path, so there is nothing to
 * configure here beyond using the immediate single-shot command.
 */
int eyes17_capture_one(struct sr_serial_dev_inst *serial,
		uint16_t tb8, uint16_t count, int gain, float *volts_out)
{
	uint8_t args[5];
	uint8_t *raw;
	size_t i, nbytes;
	int ret;

	if (!serial || !volts_out || count == 0)
		return SR_ERR_ARG;
	if (!tb8)
		tb8 = EYES17_TB8_MIN;
	count = eyes17_clamp_count(count);
	args[0] = EYES17_CHOSA_A1;
	eyes17_put_u16_le(args + 1, tb8);
	eyes17_put_u16_le(args + 3, count);
	ret = eyes17_send_cmd(serial, EYES17_HDR_ADC,
		EYES17_SUB_CAPTURE_ONE, args, sizeof(args));
	if (ret != SR_OK)
		return ret;
	nbytes = (size_t)count * 2;
	raw = g_malloc(nbytes);
	if (serial_read_blocking(serial, raw, nbytes,
			EYES17_CAPTURE_TIMEOUT_MS) != (int)nbytes) {
		g_free(raw);
		return SR_ERR_TIMEOUT;
	}
	for (i = 0; i < count; i++)
		volts_out[i] = eyes17_adc_to_volts(
			eyes17_get_u16_le(raw + 2 * i), gain);
	g_free(raw);
	return SR_OK;
}
