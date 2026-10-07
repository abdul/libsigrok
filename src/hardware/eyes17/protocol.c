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
#include <math.h>
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
/*
 * Python round-half-even, matching the golden int(round(...)) prescaler
 * math (C round() rounds half away from zero and would pick different
 * prescalers/wavelengths on .5 fractions).
 */
double eyes17_py_round(double v)
{
	double a, f, d, r;

	a = fabs(v);
	f = floor(a);
	d = a - f;
	if (d < 0.5)
		r = f;
	else if (d > 0.5)
		r = f + 1.0;
	else
		r = (fmod(f, 2.0) == 0.0) ? f : f + 1.0;
	return (v < 0.0) ? -r : r;
}

int eyes17_wg_check(const char *wave, double freq, double amp)
{
	int step;

	if (!wave)
		return SR_ERR_ARG;
	if (g_strcmp0(wave, "off") != 0 && g_strcmp0(wave, "sine") != 0)
		return SR_ERR_ARG;
	if (freq < 0.0 || freq > EYES17_WG_MAX_HZ)
		return SR_ERR_ARG;
	if (amp <= 0.0)
		return SR_ERR_ARG;
	if (eyes17_wg_amp_step(amp, &step) != SR_OK)
		return SR_ERR_ARG;
	return SR_OK;
}

int eyes17_wg_amp_step(double volts, int *step_out)
{
	static const double steps[] = EYES17_WG_AMP_STEPS;

	if (!step_out || volts <= 0.0)
		return SR_ERR_ARG;
	if (volts < (steps[0] + steps[1]) / 2.0)
		*step_out = 0;
	else if (volts < (steps[1] + steps[2]) / 2.0)
		*step_out = 1;
	else
		*step_out = 2;
	return SR_OK;
}

size_t eyes17_build_wg(uint8_t *buf, double freq)
{
	static const double pres[] = { 1.0, 8.0, 64.0, 256.0 };
	double table, wavelength;
	int prescaler;

	if (!buf)
		return 0;
	buf[0] = EYES17_HDR_WAVEGEN;
	buf[1] = EYES17_SUB_SET_SINE1;
	if (freq < EYES17_WG_MIN_HZ) {
		buf[2] = EYES17_WG_OFF_BYTE;
		buf[3] = 0;
		buf[4] = 0;
		return EYES17_WG_FRAME_LEN;
	}
	table = (freq < EYES17_WG_TABLE_SWITCH_HZ) ?
		(double)EYES17_WG_TABLE_HIRES : (double)EYES17_WG_TABLE_LORES;
	prescaler = 0;
	wavelength = 0.0;
	while (prescaler <= 3) {
		wavelength = eyes17_py_round(
			64000000.0 / freq / pres[prescaler] / table);
		if (wavelength < 65525.0)
			break;
		prescaler++;
	}
	if (prescaler == 4 || wavelength < 1.0)
		return 0;
	buf[2] = (uint8_t)(((table == (double)EYES17_WG_TABLE_HIRES) ? 1 : 0) |
		(prescaler << 1));
	eyes17_put_u16_le(buf + 3, (uint16_t)wavelength - 1);
	return EYES17_WG_FRAME_LEN;
}

int eyes17_wg_apply(struct sr_serial_dev_inst *serial,
	const char *wave, double freq, double amp)
{
	uint8_t buf[EYES17_WG_FRAME_LEN];
	uint8_t ab;
	size_t n;
	int step, ret;

	if (eyes17_wg_check(wave, freq, amp) != SR_OK)
		return SR_ERR_ARG;
	if (g_strcmp0(wave, "off") == 0)
		freq = 0.0;
	n = eyes17_build_wg(buf, freq);
	if (n != EYES17_WG_FRAME_LEN)
		return SR_ERR_ARG;
	ret = eyes17_send_cmd(serial, buf[0], buf[1], buf + 2, n - 2);
	if (ret != SR_OK)
		return ret;
	if (g_strcmp0(wave, "off") == 0)
		return SR_OK;
	if (eyes17_wg_amp_step(amp, &step) != SR_OK)
		return SR_ERR_ARG;
	ab = (uint8_t)step;
	return eyes17_send_cmd(serial, EYES17_HDR_WAVEGEN,
		EYES17_SUB_SET_SINE_AMP, &ab, sizeof(ab));
}

int eyes17_sq_check(int which, double freq, double duty)
{
	if (which != 1 && which != 2)
		return SR_ERR_ARG;
	if (freq == (double)EYES17_SQ_PARK_HIGH ||
		freq == (double)EYES17_SQ_PARK_LOW)
		return SR_OK;
	if (duty <= 0.0 || duty > 100.0)
		return SR_ERR_ARG;
	if (which == 2) {
		if (freq < (double)EYES17_SQ_FAST_MIN_HZ ||
			freq > (double)EYES17_SQ_FAST_MAX_HZ)
			return SR_ERR_ARG;
		return SR_OK;
	}
	if (freq > (double)EYES17_SQ_FAST_MAX_HZ)
		return SR_ERR_ARG;
	if (64000000.0 / freq > EYES17_SQ_SLOW_W_MAX)
		return SR_ERR_ARG;
	return SR_OK;
}

size_t eyes17_build_sq_fast(uint8_t *buf, double freq,
	double duty, int sq2)
{
	static const double pres[] = { 1.0, 8.0, 64.0, 256.0 };
	double wavelength, high_time;
	int prescaler;

	if (!buf)
		return 0;
	prescaler = 0;
	wavelength = 0.0;
	while (prescaler <= 3) {
		wavelength = eyes17_py_round(
			64000000.0 / freq / pres[prescaler]);
		if (wavelength < 65525.0)
			break;
		prescaler++;
	}
	if (prescaler == 4 || wavelength < 1.0)
		return 0;
	high_time = eyes17_py_round(wavelength * duty / 100.0);
	buf[0] = EYES17_HDR_WAVEGEN;
	buf[1] = EYES17_SUB_SET_SQR1;
	eyes17_put_u16_le(buf + 2, (uint16_t)wavelength);
	eyes17_put_u16_le(buf + 4, (uint16_t)high_time);
	buf[6] = (uint8_t)(prescaler | (sq2 ? 0x4 : 0x0));
	return 7;
}

size_t eyes17_build_sq_slow(uint8_t *buf, double freq, double duty)
{
	double w, h;
	uint32_t wi, hi;

	if (!buf)
		return 0;
	w = eyes17_py_round(64000000.0 / freq);
	if (w < 1.0 || w > EYES17_SQ_SLOW_W_MAX)
		return 0;
	h = eyes17_py_round(w * duty / 100.0);
	wi = (uint32_t)w;
	hi = (uint32_t)h;
	buf[0] = EYES17_HDR_WAVEGEN;
	buf[1] = EYES17_SUB_SET_SQR_LONG;
	eyes17_put_u16_le(buf + 2, (uint16_t)(wi & 0xffff));
	eyes17_put_u16_le(buf + 4, (uint16_t)((wi >> 16) & 0xffff));
	eyes17_put_u16_le(buf + 6, (uint16_t)(hi & 0xffff));
	eyes17_put_u16_le(buf + 8, (uint16_t)((hi >> 16) & 0xffff));
	return 10;
}

size_t eyes17_build_sq_park(uint8_t *buf, int which, int high)
{
	uint8_t data;

	if (!buf || (which != 1 && which != 2))
		return 0;
	if (which == 1)
		data = (uint8_t)(0x40 | (high ? 0x04 : 0x00));
	else
		data = (uint8_t)(0x80 | (high ? 0x08 : 0x00));
	buf[0] = EYES17_HDR_DOUT;
	buf[1] = EYES17_SUB_SET_STATE;
	buf[2] = data;
	return 3;
}

int eyes17_sq_apply(struct sr_serial_dev_inst *serial, int which,
	double freq, double duty)
{
	uint8_t buf[10];
	size_t n;
	int ret;

	if (eyes17_sq_check(which, freq, duty) != SR_OK)
		return SR_ERR_ARG;
	if (freq == (double)EYES17_SQ_PARK_HIGH ||
		freq == (double)EYES17_SQ_PARK_LOW) {
		n = eyes17_build_sq_park(buf, which,
			freq == (double)EYES17_SQ_PARK_HIGH);
		if (n != 3)
			return SR_ERR_ARG;
		return eyes17_send_cmd(serial, buf[0], buf[1],
			buf + 2, n - 2);
	}
	if (freq < (double)EYES17_SQ_FAST_MIN_HZ) {
		if (which != 1)
			return SR_ERR_ARG;
		n = eyes17_build_sq_slow(buf, freq, duty);
		if (n != 10)
			return SR_ERR_ARG;
		ret = eyes17_send_cmd(serial, buf[0], buf[1],
			buf + 2, n - 2);
		return ret;
	}
	n = eyes17_build_sq_fast(buf, freq, duty, which == 2);
	if (n != 7)
		return SR_ERR_ARG;
	return eyes17_send_cmd(serial, buf[0], buf[1], buf + 2, n - 2);
}

int eyes17_pv_check(int which, double volts)
{
	if (which == 1)
		return (volts >= -5.0 && volts <= 5.0) ? SR_OK : SR_ERR_ARG;
	if (which == 2)
		return (volts >= 0.0 && volts <= 3.3) ? SR_OK : SR_ERR_ARG;
	return SR_ERR_ARG;
}

int eyes17_pv_code(int which, double volts, uint16_t *code_out)
{
	double span0, span1, code;

	if (!code_out || eyes17_pv_check(which, volts) != SR_OK)
		return SR_ERR_ARG;
	if (which == 1) {
		span0 = -5.0;
		span1 = 5.0;
	} else {
		span0 = -3.3;
		span1 = 3.3;
	}
	code = eyes17_py_round(4095.0 * (volts - span0) / (span1 - span0));
	*code_out = (uint16_t)code;
	return SR_OK;
}

size_t eyes17_build_pv(uint8_t *buf, int which, uint16_t code)
{
	uint16_t v;

	if (!buf || (which != 1 && which != 2) || code > 4095)
		return 0;
	v = (which == 1) ? code : (uint16_t)(0x8000 | code);
	buf[0] = EYES17_HDR_DAC;
	buf[1] = EYES17_SUB_SET_DAC;
	eyes17_put_u16_le(buf + 2, v);
	return 4;
}

int eyes17_pv_apply(struct sr_serial_dev_inst *serial, int which,
	double volts)
{
	uint8_t buf[4];
	uint16_t code;
	size_t n;

	if (eyes17_pv_code(which, volts, &code) != SR_OK)
		return SR_ERR_ARG;
	n = eyes17_build_pv(buf, which, code);
	if (n != 4)
		return SR_ERR_ARG;
	return eyes17_send_cmd(serial, buf[0], buf[1], buf + 2, n - 2);
}
