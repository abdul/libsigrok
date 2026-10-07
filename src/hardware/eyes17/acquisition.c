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
 * Resolution gate: 10-bit dual/quad plus 12-bit single (M6).
 * 12-bit multi (incl. CAPTURE_12BIT_SCAN) has no driver wire
 * path and fails in check_acquisition as a combination.
 */
int eyes17_check_resolution(int bits)
{
	if (bits == EYES17_RESOLUTION_10BIT)
		return SR_OK;
	if (bits == EYES17_RESOLUTION_12BIT)
		return SR_OK;
	return SR_ERR_ARG;
}

/*
 * Validate a requested acquisition combo before touching hardware.
 * Oversize sample counts clamp per the Task-4 convention (dual
 * captures clamp per channel to EYES17_MAX_SAMPLES_DUAL); a zero
 * count, an off-ladder rate, an invalid gain, a channel count
 * other than 1, 2 or 4, a dual/quad rate below the TB8_MIN floor,
 * or a 12-bit combo other than single-channel at or above the
 * 3 us floor (tb8 24, golden capture_highres_traces) fail. 12-bit
 * multi (incl. CAPTURE_12BIT_SCAN) has no driver wire path. The
 * 12-bit cap stays 10000 (single channel).
 */
int eyes17_check_acquisition(uint64_t samplerate, uint64_t limit,
		int gain, int resolution, int channels, uint16_t *tb8_out,
		uint16_t *count_out)
{
	uint16_t tb8, count;
	uint16_t tb8_min, count_max;
	int ret;

	if (channels != 1 && channels != 2 && channels != 4)
		return SR_ERR_ARG;
	ret = eyes17_check_resolution(resolution);
	if (ret != SR_OK)
		return ret;
	if (resolution == EYES17_RESOLUTION_12BIT && channels != 1)
		return SR_ERR_ARG;
	if (!eyes17_gain_is_valid(gain))
		return SR_ERR_ARG;
	if (eyes17_samplerate_to_tb8(samplerate, &tb8) != SR_OK)
		return SR_ERR_ARG;
	if (limit == 0)
		return SR_ERR_ARG;
	tb8_min = (channels == 2) ? EYES17_TB8_MIN_DUAL :
		((channels == 4) ? EYES17_TB8_MIN_QUAD :
		((resolution == EYES17_RESOLUTION_12BIT) ?
			EYES17_TB8_MIN_12BIT : EYES17_TB8_MIN));
	count_max = (channels == 2) ? EYES17_MAX_SAMPLES_DUAL :
		((channels == 4) ? EYES17_MAX_SAMPLES_QUAD : EYES17_MAX_SAMPLES);
	if (tb8 < tb8_min)
		return SR_ERR_ARG;
	count = (limit > count_max) ? count_max : (uint16_t)limit;
	if (tb8_out)
		*tb8_out = tb8;
	if (count_out)
		*count_out = count;
	return SR_OK;
}

/*
 * Golden eyes.py:capture_traces: CAPTURE_ONE takes the sample count
 * first and the timebase second: [ADC, CAPTURE_ONE, CHOSA, n, tb8].
 */
size_t eyes17_build_capture_one(uint8_t *buf, uint16_t tb8, uint16_t count)
{
	if (!buf)
		return 0;
	buf[0] = EYES17_HDR_ADC;
	buf[1] = EYES17_SUB_CAPTURE_ONE;
	buf[2] = EYES17_CHOSA_A1;
	eyes17_put_u16_le(buf + 3, count);
	eyes17_put_u16_le(buf + 5, tb8);
	return EYES17_CAPTURE_FRAME_LEN;
}

/*
 * Golden eyes.py:capture_traces CAPTURE_TWO frame
 * [ADC=2,SUB=2,CHOSA=3]+count u16le+tb8 u16le. Plain CHOSA here;
 * OR-ing EYES17_CHOSA_TRIGGERED is the triggered caller's job.
 */
size_t eyes17_build_capture_two(uint8_t *buf, uint16_t tb8, uint16_t count)
{
	if (!buf)
		return 0;
	buf[0] = EYES17_HDR_ADC;
	buf[1] = EYES17_SUB_CAPTURE_TWO;
	buf[2] = EYES17_CHOSA_A1;
	eyes17_put_u16_le(buf + 3, count);
	eyes17_put_u16_le(buf + 5, tb8);
	return EYES17_CAPTURE_FRAME_LEN;
}
/*
 * Golden eyes.py:capture_traces num==4 frame
 * [ADC=2,SUB=4,CHOSA=3]+count u16le+tb8 u16le. Plain CHOSA here;
 * OR-ing EYES17_CHOSA_TRIGGERED is the triggered caller's job.
 */
size_t eyes17_build_capture_four(uint8_t *buf, uint16_t tb8,
		uint16_t count)
{
	if (!buf)
		return 0;
	buf[0] = EYES17_HDR_ADC;
	buf[1] = EYES17_SUB_CAPTURE_FOUR;
	buf[2] = EYES17_CHOSA_A1;
	eyes17_put_u16_le(buf + 3, count);
	eyes17_put_u16_le(buf + 5, tb8);
	return EYES17_CAPTURE_FRAME_LEN;
}

/*
 * SJ-2.0 firmware-bug preamble (golden capture_traces:756): raw bytes
 * [02,04,CHOSA,02,00,16,00] written raw before the normal frame, then
 * 1 byte read back. No ACK framing — the caller handles the raw
 * write and the 1-byte read exactly like the golden fd.write/read.
 */
size_t eyes17_build_quad_bug_preamble(uint8_t *buf, uint8_t chosa)
{
	if (!buf)
		return 0;
	buf[0] = 0x02;
	buf[1] = 0x04;
	buf[2] = chosa;
	buf[3] = 0x02;
	buf[4] = 0x00;
	buf[5] = 0x16;
	buf[6] = 0x00;
	return 7;
}

/*
 * Golden eyes.py:__fetch_channel__: the firmware holds captured data
 * for the host to pull: [ADC, GET_CAPTURE_CHANNEL, channel0, n, off]
 * (channel 0-based, n samples from offset off), then 2 x n bulk bytes
 * plus an ACK. At most EYES17_FETCH_CHUNK samples per fetch.
 */
size_t eyes17_build_fetch_channel(uint8_t *buf, uint8_t ch, uint16_t n,
		uint16_t offset)
{
	if (!buf)
		return 0;
	buf[0] = EYES17_HDR_ADC;
	buf[1] = EYES17_SUB_GET_CAPTURE_CHANNEL;
	buf[2] = ch;
	eyes17_put_u16_le(buf + 3, n);
	eyes17_put_u16_le(buf + 5, offset);
	return EYES17_FETCH_FRAME_LEN;
}

/*
 * Poll GET_CAPTURE_STATUS until the conversion-done bit sets or the
 * deadline passes. The reply is data-then-ACK like GET_VOLTAGE
 * (done byte, samples u16, ACK), so the frame is written raw.
 */
static int eyes17_wait_conversion(struct sr_serial_dev_inst *serial,
		uint16_t tb8, uint16_t count)
{
	uint64_t conv_ms;
	gint64 deadline;
	uint8_t reply[3];

	/* Conversion takes count x tb8 / 8 MHz, plus headroom. */
	conv_ms = (uint64_t)count * tb8 / 8000;
	deadline = g_get_monotonic_time() +
		(2000 + conv_ms + 1000) * 1000;
	for (;;) {
		if (eyes17_write_cmd(serial, EYES17_HDR_ADC,
				EYES17_SUB_GET_CAPTURE_STATUS,
				NULL, 0) != SR_OK)
			return SR_ERR_IO;
		if (serial_read_blocking(serial, reply, sizeof(reply),
				EYES17_ACK_TIMEOUT_MS) != sizeof(reply))
			return SR_ERR_TIMEOUT;
		if (eyes17_read_ack(serial) != SR_OK)
			return SR_ERR_DATA;
		if (reply[0] & 0x01)
			return SR_OK;
		if (g_get_monotonic_time() >= deadline)
			return SR_ERR_TIMEOUT;
		g_usleep(10000);
	}
}

/*
 * Single-channel immediate capture (trigger-disabled path): send
 * CAPTURE_ONE for A1, wait for conversion, then pull the samples with
 * GET_CAPTURE_CHANNEL fetches and decode them to volts. Triggered
 * captures land in Plan 2 (M3); no trigger-setup command exists on
 * this path, so there is nothing to configure here beyond using the
 * immediate single-shot command.
 */
int eyes17_capture_one(struct sr_serial_dev_inst *serial,
		uint16_t tb8, uint16_t count, int gain, float *volts_out)
{
	uint8_t args[5];
	uint8_t fetch[5];
	uint8_t *raw;
	uint16_t got, n;
	size_t i;
	int ret;

	if (!serial || !volts_out || count == 0)
		return SR_ERR_ARG;
	if (!tb8)
		tb8 = EYES17_TB8_MIN;
	count = eyes17_clamp_count(count);
	args[0] = EYES17_CHOSA_A1;
	eyes17_put_u16_le(args + 1, count);
	eyes17_put_u16_le(args + 3, tb8);
	ret = eyes17_send_cmd(serial, EYES17_HDR_ADC,
		EYES17_SUB_CAPTURE_ONE, args, sizeof(args));
	if (ret != SR_OK)
		return ret;
	ret = eyes17_wait_conversion(serial, tb8, count);
	if (ret != SR_OK)
		return ret;
	raw = g_malloc(EYES17_FETCH_CHUNK * 2);
	got = 0;
	while (got < count) {
		n = count - got;
		if (n > EYES17_FETCH_CHUNK)
			n = EYES17_FETCH_CHUNK;
		fetch[0] = 0;
		eyes17_put_u16_le(fetch + 1, n);
		eyes17_put_u16_le(fetch + 3, got);
		ret = eyes17_write_cmd(serial, EYES17_HDR_ADC,
			EYES17_SUB_GET_CAPTURE_CHANNEL, fetch, sizeof(fetch));
		if (ret != SR_OK)
			break;
		if (serial_read_blocking(serial, raw, (size_t)n * 2,
				EYES17_CAPTURE_TIMEOUT_MS) != (int)((size_t)n * 2)) {
			ret = SR_ERR_TIMEOUT;
			break;
		}
		ret = eyes17_read_ack(serial);
		if (ret != SR_OK)
			break;
		for (i = 0; i < n; i++)
			volts_out[got + i] = eyes17_adc_to_volts(
				eyes17_get_u16_le(raw + 2 * i), gain, EYES17_CH_A1);
		got += n;
	}
	g_free(raw);
	return ret;
}

int eyes17_check_trigger(const char *source, const char *slope,
		double level_volts, int gain, uint16_t *level_out)
{
	if (!source || !slope || !level_out)
		return SR_ERR_ARG;
	if (g_strcmp0(source, "none") == 0) {
		*level_out = 0;
		return SR_OK;
	}
	if (g_strcmp0(source, "A1") != 0)
		return SR_ERR_ARG;
	if (g_strcmp0(slope, "rising") != 0)
		return SR_ERR_ARG;
	return eyes17_trigger_level_code(level_volts, gain, level_out);
}

/*
 * Triggered single-channel capture: configure the trigger first
 * (golden order), then run the immediate path with the triggered
 * CHOSA byte. The 8 ms hardware trigger timeout is absorbed by the
 * conversion deadline in eyes17_wait_conversion.
 */
int eyes17_capture_triggered(struct sr_serial_dev_inst *serial,
		uint16_t tb8, uint16_t count, int gain, uint16_t level,
		float *volts_out)
{
	uint8_t args[5];
	uint8_t fetch[5];
	uint8_t *raw;
	uint16_t got, n;
	size_t i;
	int ret;

	if (!serial || !volts_out || count == 0)
		return SR_ERR_ARG;
	if (!tb8)
		tb8 = EYES17_TB8_MIN;
	count = eyes17_clamp_count(count);
	ret = eyes17_configure_trigger(serial, level);
	if (ret != SR_OK)
		return ret;
	args[0] = EYES17_CHOSA_A1 | EYES17_CHOSA_TRIGGERED;
	eyes17_put_u16_le(args + 1, count);
	eyes17_put_u16_le(args + 3, tb8);
	ret = eyes17_send_cmd(serial, EYES17_HDR_ADC,
		EYES17_SUB_CAPTURE_ONE, args, sizeof(args));
	if (ret != SR_OK)
		return ret;
	ret = eyes17_wait_conversion(serial, tb8, count);
	if (ret != SR_OK)
		return ret;
	raw = g_malloc(EYES17_FETCH_CHUNK * 2);
	got = 0;
	while (got < count) {
		n = count - got;
		if (n > EYES17_FETCH_CHUNK)
			n = EYES17_FETCH_CHUNK;
		fetch[0] = 0;
		eyes17_put_u16_le(fetch + 1, n);
		eyes17_put_u16_le(fetch + 3, got);
		ret = eyes17_write_cmd(serial, EYES17_HDR_ADC,
			EYES17_SUB_GET_CAPTURE_CHANNEL, fetch, sizeof(fetch));
		if (ret != SR_OK)
			break;
		if (serial_read_blocking(serial, raw, (size_t)n * 2,
				EYES17_CAPTURE_TIMEOUT_MS) != (int)((size_t)n * 2)) {
			ret = SR_ERR_TIMEOUT;
			break;
		}
		ret = eyes17_read_ack(serial);
		if (ret != SR_OK)
			break;
		for (i = 0; i < n; i++)
			volts_out[got + i] = eyes17_adc_to_volts(
				eyes17_get_u16_le(raw + 2 * i), gain, EYES17_CH_A1);
		got += n;
	}
	g_free(raw);
	return ret;
}

/*
 * Pull both buffered channels after a CAPTURE_TWO conversion: per-channel
 * loop of GET_CAPTURE_CHANNEL fetches (fetch byte ch selects the buffered
 * channel), each write_cmd + bulk read + ACK, decoded per channel with
 * eyes17_adc_to_volts(raw, gain, (int)ch).
 */
static int eyes17_fetch_dual_stream(struct sr_serial_dev_inst *serial,
		uint16_t count, int gain, float *a1_out, float *a2_out)
{
	uint8_t fetch[5];
	uint8_t *raw;
	float *outs[2];
	uint8_t ch;
	uint16_t got, n;
	size_t i;
	int ret = SR_OK;

	outs[EYES17_FETCH_CH_A1] = a1_out;
	outs[EYES17_FETCH_CH_A2] = a2_out;
	raw = g_malloc(EYES17_FETCH_CHUNK * 2);
	for (ch = 0; ch <= EYES17_FETCH_CH_A2; ch++) {
		got = 0;
		while (got < count) {
			n = count - got;
			if (n > EYES17_FETCH_CHUNK)
				n = EYES17_FETCH_CHUNK;
			fetch[0] = ch;
			eyes17_put_u16_le(fetch + 1, n);
			eyes17_put_u16_le(fetch + 3, got);
			ret = eyes17_write_cmd(serial, EYES17_HDR_ADC,
				EYES17_SUB_GET_CAPTURE_CHANNEL, fetch, sizeof(fetch));
			if (ret != SR_OK)
				break;
			if (serial_read_blocking(serial, raw, (size_t)n * 2,
					EYES17_CAPTURE_TIMEOUT_MS) != (int)((size_t)n * 2)) {
				ret = SR_ERR_TIMEOUT;
				break;
			}
			ret = eyes17_read_ack(serial);
			if (ret != SR_OK)
				break;
			for (i = 0; i < n; i++)
				outs[ch][got + i] = eyes17_adc_to_volts(
					eyes17_get_u16_le(raw + 2 * i), gain, (int)ch);
			got += n;
		}
		if (ret != SR_OK)
			break;
	}
	g_free(raw);
	return ret;
}

/*
 * Dual-channel immediate capture: send CAPTURE_TWO with the plain CHOSA
 * byte, wait for the per-channel conversion, then pull both channels.
 */
int eyes17_capture_two(struct sr_serial_dev_inst *serial,
		uint16_t tb8, uint16_t count, int gain, float *a1_out,
		float *a2_out)
{
	uint8_t args[5];
	int ret;

	if (!serial || !a1_out || !a2_out || count == 0)
		return SR_ERR_ARG;
	if (tb8 < EYES17_TB8_MIN_DUAL)
		tb8 = EYES17_TB8_MIN_DUAL;
	if (count > EYES17_MAX_SAMPLES_DUAL)
		count = EYES17_MAX_SAMPLES_DUAL;
	args[0] = EYES17_CHOSA_A1;
	eyes17_put_u16_le(args + 1, count);
	eyes17_put_u16_le(args + 3, tb8);
	ret = eyes17_send_cmd(serial, EYES17_HDR_ADC,
		EYES17_SUB_CAPTURE_TWO, args, sizeof(args));
	if (ret != SR_OK)
		return ret;
	ret = eyes17_wait_conversion(serial, tb8, count);
	if (ret != SR_OK)
		return ret;
	return eyes17_fetch_dual_stream(serial, count, gain, a1_out, a2_out);
}

/*
 * Triggered dual-channel capture: configure the trigger first (golden
 * order), then run the immediate dual path with the triggered CHOSA byte.
 */
int eyes17_capture_two_triggered(struct sr_serial_dev_inst *serial,
		uint16_t tb8, uint16_t count, int gain, uint16_t level,
		float *a1_out, float *a2_out)
{
	uint8_t args[5];
	int ret;

	if (!serial || !a1_out || !a2_out || count == 0)
		return SR_ERR_ARG;
	if (tb8 < EYES17_TB8_MIN_DUAL)
		tb8 = EYES17_TB8_MIN_DUAL;
	if (count > EYES17_MAX_SAMPLES_DUAL)
		count = EYES17_MAX_SAMPLES_DUAL;
	ret = eyes17_configure_trigger(serial, level);
	if (ret != SR_OK)
		return ret;
	args[0] = EYES17_CHOSA_A1 | EYES17_CHOSA_TRIGGERED;
	eyes17_put_u16_le(args + 1, count);
	eyes17_put_u16_le(args + 3, tb8);
	ret = eyes17_send_cmd(serial, EYES17_HDR_ADC,
		EYES17_SUB_CAPTURE_TWO, args, sizeof(args));
	if (ret != SR_OK)
		return ret;
	ret = eyes17_wait_conversion(serial, tb8, count);
	if (ret != SR_OK)
		return ret;
	return eyes17_fetch_dual_stream(serial, count, gain, a1_out, a2_out);
}

/*
 * SJ-2.0 firmware-bug preamble send (golden capture_traces:756): write
 * the 7 raw bytes and read 1 byte back, before the normal CAPTURE_FOUR
 * frame. Only called when legacy_fw_bug is TRUE (probed FW <= 2.0).
 */
static int eyes17_send_quad_bug_preamble(struct sr_serial_dev_inst *serial,
		uint8_t chosa)
{
	uint8_t buf[7];
	uint8_t dummy;

	eyes17_build_quad_bug_preamble(buf, chosa);
	if (serial_write_blocking(serial, buf, sizeof(buf),
			EYES17_WRITE_TIMEOUT_MS) != (int)sizeof(buf))
		return SR_ERR_IO;
	if (serial_read_blocking(serial, &dummy, 1,
			EYES17_ACK_TIMEOUT_MS) != 1)
		return SR_ERR_TIMEOUT;
	return SR_OK;
}

/*
 * Pull all four buffered channels after a CAPTURE_FOUR conversion:
 * per-channel loop of GET_CAPTURE_CHANNEL fetches (fetch bytes 0-3),
 * each write_cmd + bulk read + ACK, decoded per channel with
 * eyes17_adc_to_volts(raw, gain, (int)ch) — the gain-row-0 rule for
 * A3/MIC lives inside the converter.
 */
static int eyes17_fetch_quad_stream(struct sr_serial_dev_inst *serial,
		uint16_t count, int gain, float *v1_out, float *v2_out,
		float *v3_out, float *v4_out)
{
	uint8_t fetch[5];
	uint8_t *raw;
	float *outs[EYES17_NUM_CHANNELS];
	uint8_t ch;
	uint16_t got, n;
	size_t i;
	int ret = SR_OK;

	outs[EYES17_FETCH_CH_A1] = v1_out;
	outs[EYES17_FETCH_CH_A2] = v2_out;
	outs[EYES17_FETCH_CH_A3] = v3_out;
	outs[EYES17_FETCH_CH_MIC] = v4_out;
	raw = g_malloc(EYES17_FETCH_CHUNK * 2);
	for (ch = 0; ch < EYES17_NUM_CHANNELS; ch++) {
		got = 0;
		while (got < count) {
			n = count - got;
			if (n > EYES17_FETCH_CHUNK)
				n = EYES17_FETCH_CHUNK;
			fetch[0] = ch;
			eyes17_put_u16_le(fetch + 1, n);
			eyes17_put_u16_le(fetch + 3, got);
			ret = eyes17_write_cmd(serial, EYES17_HDR_ADC,
				EYES17_SUB_GET_CAPTURE_CHANNEL, fetch, sizeof(fetch));
			if (ret != SR_OK)
				break;
			if (serial_read_blocking(serial, raw, (size_t)n * 2,
					EYES17_CAPTURE_TIMEOUT_MS) != (int)((size_t)n * 2)) {
				ret = SR_ERR_TIMEOUT;
				break;
			}
			ret = eyes17_read_ack(serial);
			if (ret != SR_OK)
				break;
			for (i = 0; i < n; i++)
				outs[ch][got + i] = eyes17_adc_to_volts(
					eyes17_get_u16_le(raw + 2 * i), gain, (int)ch);
			got += n;
		}
		if (ret != SR_OK)
			break;
	}
	g_free(raw);
	return ret;
}

/*
 * Quad-channel immediate capture: send CAPTURE_FOUR with the plain
 * CHOSA byte (sel fixed A1), wait for the per-channel conversion,
 * then pull all four channels. legacy_fw_bug sends the SJ-2.0 raw
 * preamble first.
 */
int eyes17_capture_four(struct sr_serial_dev_inst *serial,
		uint16_t tb8, uint16_t count, int gain, gboolean legacy_fw_bug,
		float *v1_out, float *v2_out, float *v3_out, float *v4_out)
{
	uint8_t args[5];
	int ret;

	if (!serial || !v1_out || !v2_out || !v3_out || !v4_out || count == 0)
		return SR_ERR_ARG;
	if (tb8 < EYES17_TB8_MIN_QUAD)
		tb8 = EYES17_TB8_MIN_QUAD;
	if (count > EYES17_MAX_SAMPLES_QUAD)
		count = EYES17_MAX_SAMPLES_QUAD;
	if (legacy_fw_bug) {
		ret = eyes17_send_quad_bug_preamble(serial, EYES17_CHOSA_A1);
		if (ret != SR_OK)
			return ret;
	}
	args[0] = EYES17_CHOSA_A1;
	eyes17_put_u16_le(args + 1, count);
	eyes17_put_u16_le(args + 3, tb8);
	ret = eyes17_send_cmd(serial, EYES17_HDR_ADC,
		EYES17_SUB_CAPTURE_FOUR, args, sizeof(args));
	if (ret != SR_OK)
		return ret;
	ret = eyes17_wait_conversion(serial, tb8, count);
	if (ret != SR_OK)
		return ret;
	return eyes17_fetch_quad_stream(serial, count, gain,
		v1_out, v2_out, v3_out, v4_out);
}

/*
 * Triggered quad-channel capture: configure the trigger first (golden
 * order), then run the immediate quad path with the triggered CHOSA
 * byte. Trigger gates A1 only, M3 scope unchanged.
 */
int eyes17_capture_four_triggered(struct sr_serial_dev_inst *serial,
		uint16_t tb8, uint16_t count, int gain, uint16_t level,
		gboolean legacy_fw_bug,
		float *v1_out, float *v2_out, float *v3_out, float *v4_out)
{
	uint8_t args[5];
	int ret;

	if (!serial || !v1_out || !v2_out || !v3_out || !v4_out || count == 0)
		return SR_ERR_ARG;
	if (tb8 < EYES17_TB8_MIN_QUAD)
		tb8 = EYES17_TB8_MIN_QUAD;
	if (count > EYES17_MAX_SAMPLES_QUAD)
		count = EYES17_MAX_SAMPLES_QUAD;
	ret = eyes17_configure_trigger(serial, level);
	if (ret != SR_OK)
		return ret;
	if (legacy_fw_bug) {
		/* Golden preamble carries plain CHOSA; the trigger bit
		 * lives only in the capture frame below. */
		ret = eyes17_send_quad_bug_preamble(serial, EYES17_CHOSA_A1);
		if (ret != SR_OK)
			return ret;
	}
	args[0] = EYES17_CHOSA_A1 | EYES17_CHOSA_TRIGGERED;
	eyes17_put_u16_le(args + 1, count);
	eyes17_put_u16_le(args + 3, tb8);
	ret = eyes17_send_cmd(serial, EYES17_HDR_ADC,
		EYES17_SUB_CAPTURE_FOUR, args, sizeof(args));
	if (ret != SR_OK)
		return ret;
	ret = eyes17_wait_conversion(serial, tb8, count);
	if (ret != SR_OK)
		return ret;
	return eyes17_fetch_quad_stream(serial, count, gain,
		v1_out, v2_out, v3_out, v4_out);
}
