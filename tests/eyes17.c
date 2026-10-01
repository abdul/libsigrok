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

Suite *suite_eyes17(void)
{
	Suite *s = suite_create("eyes17");
	TCase *tc = tcase_create("protocol");
	tcase_add_test(tc, test_u16_roundtrip);
	tcase_add_test(tc, test_u32_roundtrip);
	tcase_add_test(tc, test_version_ok);
	tcase_add_test(tc, test_version_reject);
	suite_add_tcase(s, tc);
	return s;
}
