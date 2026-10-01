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
#include <check.h>
#include <libsigrok/libsigrok.h>
#include "../src/hardware/eyes17/protocol.h"
#include "lib.h"

START_TEST(test_u16_roundtrip)
{
	uint8_t p[2];
	eyes17_put_u16_le(p, 0x1234);
	ck_assert_uint_eq(p[0], 0x34);
	ck_assert_uint_eq(p[1], 0x12);
	ck_assert_uint_eq(eyes17_get_u16_le(p), 0x1234);
}
END_TEST

START_TEST(test_u32_roundtrip)
{
	uint8_t p[4];
	eyes17_put_u32_le(p, 0x12345678);
	ck_assert_uint_eq(p[0], 0x78);
	ck_assert_uint_eq(p[3], 0x12);
	ck_assert_uint_eq(eyes17_get_u32_le(p), 0x12345678);
}
END_TEST

START_TEST(test_version_ok)
{
	struct eyes17_version v;
	ck_assert_int_eq(eyes17_parse_version("SJ-2.4", &v), SR_OK);
	ck_assert_int_eq(v.major, 2);
	ck_assert_int_eq(v.minor, 4);
}
END_TEST

START_TEST(test_version_reject)
{
	struct eyes17_version v;
	ck_assert_int_ne(eyes17_parse_version("XX-1.0", &v), SR_OK);
	ck_assert_int_ne(eyes17_parse_version("", &v), SR_OK);
	ck_assert_int_ne(eyes17_parse_version(NULL, &v), SR_OK);
}
END_TEST

START_TEST(test_timebase_floor)
{
	ck_assert_uint_eq(eyes17_timebase_to_tb8(1.5), 12);
	ck_assert_uint_eq(eyes17_timebase_to_tb8(1.0), 12);
	ck_assert_uint_eq(eyes17_timebase_to_tb8(0.0), 12);
}
END_TEST

START_TEST(test_timebase_1_5us_rate)
{
	uint64_t num = 0, den = 0;
	ck_assert_uint_eq(eyes17_timebase_to_tb8(1.5), 12);
	eyes17_tb8_to_rate(12, &num, &den);
	ck_assert_uint_eq(num, 8000000);
	ck_assert_uint_eq(den, 12);
}
END_TEST

START_TEST(test_timebase_10us)
{
	uint64_t num = 0, den = 0;
	ck_assert_uint_eq(eyes17_timebase_to_tb8(10.0), 80);
	eyes17_tb8_to_rate(80, &num, &den);
	ck_assert_uint_eq(num, 8000000);
	ck_assert_uint_eq(den, 80);
	ck_assert_uint_eq(num / den, 100000);
}
END_TEST

START_TEST(test_count_cap)
{
	ck_assert_uint_eq(eyes17_clamp_count(100), 100);
	ck_assert_uint_eq(eyes17_clamp_count(10000), 10000);
	ck_assert_uint_eq(eyes17_clamp_count(20000), 10000);
}
END_TEST

START_TEST(test_capture_one_frame)
{
	uint8_t buf[EYES17_CAPTURE_FRAME_LEN];
	uint8_t expect[] = { 2, 1, 3, 0x50, 0x00, 0x64, 0x00 };
	ck_assert_uint_eq(eyes17_build_capture_one(buf, 80, 100),
		sizeof(expect));
	ck_assert_int_eq(memcmp(buf, expect, sizeof(expect)), 0);
}
END_TEST

/*
 * Link-only transport stubs. The eyes17 unit tests exercise pure functions
 * only; the serial helpers are hidden in the shared lib, so these
 * never-called definitions solely satisfy the tests/main link. They
 * return SR_ERR so any accidental call fails loudly.
 */
int serial_write_blocking(struct sr_serial_dev_inst *serial,
	const void *buf, size_t count, unsigned int timeout_ms);
int serial_read_blocking(struct sr_serial_dev_inst *serial,
	void *buf, size_t count, unsigned int timeout_ms);
int serial_readline(struct sr_serial_dev_inst *serial,
	char **buf, int *buflen, gint64 timeout_ms);

int serial_write_blocking(struct sr_serial_dev_inst *serial,
	const void *buf, size_t count, unsigned int timeout_ms)
{
	(void)serial;
	(void)buf;
	(void)count;
	(void)timeout_ms;
	return SR_ERR;
}

int serial_read_blocking(struct sr_serial_dev_inst *serial,
	void *buf, size_t count, unsigned int timeout_ms)
{
	(void)serial;
	(void)buf;
	(void)count;
	(void)timeout_ms;
	return SR_ERR;
}

int serial_readline(struct sr_serial_dev_inst *serial,
	char **buf, int *buflen, gint64 timeout_ms)
{
	(void)serial;
	(void)buf;
	(void)buflen;
	(void)timeout_ms;
	return SR_ERR;
}

/*
 * Task 5 calibration tests. Golden refs (via docs/HARDWARE.md):
 * - gains list / inputRanges: achan.py:4 / achan.py:18
 * - >30% deviation rejection: achan.py:loadPolynomials:103
 * - READY-tagged flash polys: eyes.py:__runInitSequence__:210-279
 * - A1 inverted op-amp range +16.5..-16.5 V: HARDWARE.md pin map
 */
START_TEST(test_gain_table)
{
	static const double expect[] = { 1, 2, 4, 5, 8, 10, 16, 32 };
	int i;

	for (i = 0; i < 8; i++) {
		ck_assert(eyes17_gain_is_valid(i));
		ck_assert(fabs(eyes17_gain_factor(i) - expect[i]) < 1e-12);
	}
	/* Index 8 is external-attenuator-only: rejected from normal path. */
	ck_assert(!eyes17_gain_is_valid(8));
	ck_assert(fabs(eyes17_gain_factor(8)) < 1e-12);
	ck_assert(!eyes17_gain_is_valid(-1));
	ck_assert(!eyes17_gain_is_valid(9));
}
END_TEST

START_TEST(test_ideal_midscale)
{
	/* 1 LSB at gain x1 over the 33 V span. */
	const float lsb = (float)(33.0 / 1023.0);

	eyes17_calibration_load(NULL, 0);
	ck_assert(fabsf(eyes17_adc_to_volts_ideal(511, 0)) <= lsb);
	ck_assert(fabsf(eyes17_adc_to_volts_ideal(512, 0)) <= lsb);
	ck_assert(fabsf(eyes17_adc_to_volts(511, 0)) <= lsb);
}
END_TEST

START_TEST(test_ideal_endpoints)
{
	float hi, lo, hi2;

	eyes17_calibration_load(NULL, 0);
	hi = eyes17_adc_to_volts_ideal(0, 0);
	lo = eyes17_adc_to_volts_ideal(1023, 0);
	ck_assert(fabsf(hi - 16.5f) / 16.5f < 0.01f);
	ck_assert(fabsf(lo + 16.5f) / 16.5f < 0.01f);
	/* PGA scaling: gain index 1 (x2) halves the span. */
	hi2 = eyes17_adc_to_volts_ideal(0, 1);
	ck_assert(fabsf(hi2 - 8.25f) / 8.25f < 0.01f);
}
END_TEST

START_TEST(test_gain8_rejected)
{
	eyes17_calibration_load(NULL, 0);
	ck_assert(isnan(eyes17_adc_to_volts_ideal(512, 8)));
	ck_assert(isnan(eyes17_adc_to_volts(512, 8)));
	ck_assert(isnan(eyes17_adc_to_volts(512, -1)));
	ck_assert(isnan(eyes17_adc_to_volts(512, 9)));
}
END_TEST

START_TEST(test_flash_unreadable_not_ready)
{
	static const uint8_t junk[] = { 'n', 'o', 'p', 'e' };

	ck_assert(!eyes17_calibration_load(NULL, 0));
	ck_assert(!eyes17_calibration_is_ready());
	ck_assert(!eyes17_calibration_load(junk, sizeof(junk)));
	ck_assert(!eyes17_calibration_is_ready());
	/* Falls back to ideal; never claims calibrated output. */
	ck_assert(fabsf(eyes17_adc_to_volts(512, 0)) <=
		(float)(33.0 / 1023.0));
}
END_TEST

static size_t build_flash_blob(uint8_t *buf, const float polys[8][3])
{
	static const char magic[] = "ExpEYES17-test\n";
	static const char marker[] = ">|A1|<";
	static const char stop[] = "STOP";
	size_t n = 0;
	int g;

	memcpy(buf + n, magic, sizeof(magic) - 1);
	n += sizeof(magic) - 1;
	memcpy(buf + n, marker, sizeof(marker) - 1);
	n += sizeof(marker) - 1;
	for (g = 0; g < 8; g++) {
		memcpy(buf + n, polys[g], 12);
		n += 12;
	}
	memcpy(buf + n, stop, sizeof(stop) - 1);
	n += sizeof(stop) - 1;
	return n;
}

static void ideal_polys(float polys[8][3])
{
	int g;

	for (g = 0; g < 8; g++) {
		double f = eyes17_gain_factor(g);
		polys[g][0] = 0.0f;
		polys[g][1] = (float)(-33.0 / f / 4095.0);
		polys[g][2] = (float)(16.5 / f);
	}
}

START_TEST(test_deviation_rejected)
{
	uint8_t blob[256];
	float polys[8][3];
	float v;

	/* Gain 0 offset +10 V: 60.6% deviation at full scale, must
	 * fall back to the ideal curve. */
	ideal_polys(polys);
	polys[0][2] += 10.0f;
	ck_assert(eyes17_calibration_load(blob,
		build_flash_blob(blob, polys)));
	ck_assert(eyes17_calibration_is_ready());
	v = eyes17_adc_to_volts(0, 0);
	ck_assert(fabsf(v - 16.5f) / 16.5f < 0.01f);
}
END_TEST

START_TEST(test_valid_poly_used)
{
	uint8_t blob[256];
	float polys[8][3];
	float v;

	/* Gain 0 offset +0.1 V: 0.6% deviation, stored and used. */
	ideal_polys(polys);
	polys[0][2] += 0.1f;
	ck_assert(eyes17_calibration_load(blob,
		build_flash_blob(blob, polys)));
	ck_assert(eyes17_calibration_is_ready());
	v = eyes17_adc_to_volts(0, 0);
	ck_assert(fabsf(v - 16.6f) < 0.02f);
}
END_TEST

Suite *suite_eyes17(void)
{
	Suite *s = suite_create("eyes17");
	TCase *tc = tcase_create("protocol");
	TCase *tt = tcase_create("timebase");
	TCase *tk = tcase_create("calibration");
	tcase_add_test(tc, test_u16_roundtrip);
	tcase_add_test(tc, test_u32_roundtrip);
	tcase_add_test(tc, test_version_ok);
	tcase_add_test(tc, test_version_reject);
	tcase_add_test(tt, test_timebase_floor);
	tcase_add_test(tt, test_timebase_1_5us_rate);
	tcase_add_test(tt, test_timebase_10us);
	tcase_add_test(tt, test_count_cap);
	tcase_add_test(tt, test_capture_one_frame);
	tcase_add_test(tk, test_gain_table);
	tcase_add_test(tk, test_ideal_midscale);
	tcase_add_test(tk, test_ideal_endpoints);
	tcase_add_test(tk, test_gain8_rejected);
	tcase_add_test(tk, test_flash_unreadable_not_ready);
	tcase_add_test(tk, test_deviation_rejected);
	tcase_add_test(tk, test_valid_poly_used);
	suite_add_tcase(s, tc);
	suite_add_tcase(s, tt);
	suite_add_tcase(s, tk);
	return s;
}
