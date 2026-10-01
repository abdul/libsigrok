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

Suite *suite_eyes17(void)
{
	Suite *s = suite_create("eyes17");
	TCase *tc = tcase_create("protocol");
	TCase *tt = tcase_create("timebase");
	tcase_add_test(tc, test_u16_roundtrip);
	tcase_add_test(tc, test_u32_roundtrip);
	tcase_add_test(tc, test_version_ok);
	tcase_add_test(tc, test_version_reject);
	tcase_add_test(tt, test_timebase_floor);
	tcase_add_test(tt, test_timebase_1_5us_rate);
	tcase_add_test(tt, test_timebase_10us);
	tcase_add_test(tt, test_count_cap);
	tcase_add_test(tt, test_capture_one_frame);
	suite_add_tcase(s, tc);
	suite_add_tcase(s, tt);
	return s;
}
