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

/*
 * Samplerate ladder (Task 6): every entry is exactly 8e6/tb8 for an
 * integer tb8 (Task-4 timebase math above), so each advertised rate
 * is a tested quantizer step -- never an untested rate. tb8 12 is the
 * 1.5 us floor (EYES17_TB8_MIN); the slowest entry keeps a full
 * single-shot capture inside the firmware window.
 */
static const uint16_t eyes17_ladder_tb8[] = {
	12, 16, 20, 32, 40, 80, 160, 200, 400, 800, 1600, 8000,
};

static const uint64_t eyes17_ladder_rates[] = {
	666666, 500000, 400000, 250000, 200000, 100000,
	50000, 40000, 20000, 10000, 5000, 1000,
};

const uint64_t *eyes17_samplerate_list(unsigned *n)
{
	if (n)
		*n = ARRAY_SIZE(eyes17_ladder_rates);
	return eyes17_ladder_rates;
}

int eyes17_samplerate_to_tb8(uint64_t rate, uint16_t *tb8_out)
{
	unsigned i;

	for (i = 0; i < ARRAY_SIZE(eyes17_ladder_rates); i++) {
		if (eyes17_ladder_rates[i] == rate) {
			if (tb8_out)
				*tb8_out = eyes17_ladder_tb8[i];
			return SR_OK;
		}
	}
	return SR_ERR_ARG;
}

/*
 * 12-bit capture belongs to M6: reject it explicitly, never fall
 * back to 10-bit silently.
 */
int eyes17_check_resolution(int bits)
{
	if (bits == EYES17_RESOLUTION_10BIT)
		return SR_OK;
	if (bits == EYES17_RESOLUTION_12BIT)
		return SR_ERR;
	return SR_ERR_ARG;
}

/*
 * Validate a requested acquisition combo before touching hardware.
 * Oversize sample counts clamp per the Task-4 convention; a zero
 * count, an off-ladder rate, an invalid gain, or 12-bit mode fail.
 */
int eyes17_check_acquisition(uint64_t samplerate, uint64_t limit,
		int gain, int resolution, uint16_t *tb8_out,
		uint16_t *count_out)
{
	uint16_t tb8, count;
	int ret;

	ret = eyes17_check_resolution(resolution);
	if (ret != SR_OK)
		return ret;
	if (!eyes17_gain_is_valid(gain))
		return SR_ERR_ARG;
	if (eyes17_samplerate_to_tb8(samplerate, &tb8) != SR_OK)
		return SR_ERR_ARG;
	if (limit == 0)
		return SR_ERR_ARG;
	count = eyes17_clamp_count((size_t)limit);
	if (tb8_out)
		*tb8_out = tb8;
	if (count_out)
		*count_out = count;
	return SR_OK;
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
