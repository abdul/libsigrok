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

/*
 * A1 gain index -> factor. Golden: achan.py:4 gains list; HARDWARE.md
 * documents index 8 as the external-attenuator mode (10 MΩ series,
 * eyes.py:set_gain:1260), so the normal path carries a 0.0 sentinel
 * there and rejects it. Only A1/A2 have PGA gain (HARDWARE.md pin map).
 */
static const double eyes17_gain_factors[] = { 1, 2, 4, 5, 8, 10, 16, 32, 0.0 };

/* A1 uncalibrated input range, golden achan.py:18 inputRanges['A1'].
 * Inverted op-amp: raw ADC 0 maps to +16.5 V, full scale to -16.5 V. */
#define EYES17_A1_HI 16.5
#define EYES17_A1_LO -16.5
#define EYES17_ADC10_FULL 1023.0
#define EYES17_ADC12_FULL 4095.0

/* Per-gain deviation rejection, golden achan.py:loadPolynomials:103. */
#define EYES17_CAL_MAX_DEVIATION_PCT 30.0

/* Channel spans (golden achan.py:inputRanges): A1/A2 inverted
 * [16.5,-16.5], A3/MIC plain [-3.3,+3.3]. ideal(raw) =
 * R0 + (R1 - R0) * x / 4095 with x = raw * 4095/1023. */
static const float eyes17_ch_span[EYES17_NUM_CHANNELS][2] = {
	{ 16.5f, -16.5f },
	{ 16.5f, -16.5f },
	{ -3.3f, 3.3f },
	{ -3.3f, 3.3f },
};
/* Flash bulk payload markers, golden eyes.py:__runInitSequence__:238-249
 * ('ExpEYES' magic, per-channel '>|name|<' sections, 'STOP' terminator). */
#define EYES17_FLASH_MAGIC "ExpEYES"
#define EYES17_FLASH_MAGIC_LEN 7
#define EYES17_FLASH_A1_MARKER ">|A1|<"
#define EYES17_FLASH_A1_MARKER_LEN 6
#define EYES17_FLASH_A2_MARKER ">|A2|<"
#define EYES17_FLASH_A2_MARKER_LEN 6
#define EYES17_FLASH_A3_MARKER ">|A3|<"
#define EYES17_FLASH_A3_MARKER_LEN 6
#define EYES17_FLASH_MIC_MARKER ">|MIC|<"
#define EYES17_FLASH_MIC_MARKER_LEN 7
#define EYES17_FLASH_STOP "STOP"
#define EYES17_FLASH_STOP_LEN 4
#define EYES17_FLASH_POLY_LEN 12

/* Stored flash polynomials: quadratic c2*x^2 + c1*x + c0 per normal gain
 * index (golden struct.unpack('3f') triples, eyes.py:__runInitSequence__:265).
 * The 10-bit path scales raw into the 12-bit domain first, matching the
 * golden __cal10__ (achan.py:158). */
static double eyes17_cal_polys[EYES17_NUM_CHANNELS][8][3];
static gboolean eyes17_cal_use_poly[EYES17_NUM_CHANNELS][8];
static gboolean eyes17_calibration_ready = FALSE;

double eyes17_gain_factor(int gain)
{
	if (gain < 0 || gain > EYES17_GAIN_EXTERNAL)
		return 0.0;
	return eyes17_gain_factors[gain];
}

gboolean eyes17_gain_is_valid(int gain)
{
	return gain >= 0 && gain <= EYES17_GAIN_NORMAL_MAX;
}

/*
 * Ideal 10-bit curve: golden regenerateCalibration else-branch
 * (achan.py:147): calPoly10 = [0, slope/1023, intercept] with
 * slope/intercept scaled by the PGA factor. A3/MIC have no PGA
 * (gainPGAs has A1/A2 only), so their factor is pinned to 1.
 */
float eyes17_adc_to_volts_ideal(uint16_t raw, int gain, int ch)
{
	double f, r0, r1, span;

	if (!eyes17_gain_is_valid(gain) || ch < 0 || ch >= EYES17_NUM_CHANNELS)
		return NAN;
	f = (ch >= EYES17_CH_A3) ? 1.0 : eyes17_gain_factors[gain];
	r0 = eyes17_ch_span[ch][0];
	r1 = eyes17_ch_span[ch][1];
	span = (r1 - r0) / f;
	return (float)(r0 / f + span * raw / EYES17_ADC10_FULL);
}

gboolean eyes17_calibration_is_ready(void)
{
	return eyes17_calibration_ready;
}

static double eyes17_ideal_at_12bit(int ch, int gain, double x)
{
	double f = (ch >= EYES17_CH_A3) ? 1.0 : eyes17_gain_factors[gain];
	double r0 = eyes17_ch_span[ch][0];
	double r1 = eyes17_ch_span[ch][1];
	double span = (r1 - r0) / f;

	return r0 / f + span * x / EYES17_ADC12_FULL;
}

static const uint8_t *eyes17_find_marker(const uint8_t *p, size_t len,
		const char *marker, size_t mlen)
{
	size_t i;

	if (len < mlen)
		return NULL;
	for (i = 0; i + mlen <= len; i++) {
		if (memcmp(p + i, marker, mlen) == 0)
			return p + i;
	}
	return NULL;
}

static void eyes17_calibration_clear(void)
{
	int ch, g;

	for (ch = 0; ch < EYES17_NUM_CHANNELS; ch++)
		for (g = 0; g <= EYES17_GAIN_NORMAL_MAX; g++)
			eyes17_cal_use_poly[ch][g] = FALSE;
	eyes17_calibration_ready = FALSE;
}

/*
 * Validate a flash bulk payload and install its A1 polynomials. Returns
 * the calibrationReady flag: FALSE on any unreadable input (and the
 * driver keeps serving the ideal curve, never calibrated output).
 * Per-gain entries deviating >30% from ideal at full scale fall back
 * to the ideal curve (golden loadPolynomials rule).
 */
static void eyes17_load_channel_polys(const uint8_t *p, size_t avail, int ch)
{
	size_t n, g;
	float triple[3];
	double c2, c1, c0, fit, ideal, err;

	n = avail / EYES17_FLASH_POLY_LEN;
	if (n > 8)
		n = 8;
	for (g = 0; g < n; g++) {
		memcpy(triple, p + g * EYES17_FLASH_POLY_LEN,
			EYES17_FLASH_POLY_LEN);
		c2 = triple[0];
		c1 = triple[1];
		c0 = triple[2];
		fit = c2 * EYES17_ADC12_FULL * EYES17_ADC12_FULL +
			c1 * EYES17_ADC12_FULL + c0;
		ideal = eyes17_ideal_at_12bit(ch, (int)g, EYES17_ADC12_FULL);
		if (ideal == 0.0)
			continue;
		err = 100.0 * fabs((fit - ideal) / ideal);
		if (err > EYES17_CAL_MAX_DEVIATION_PCT)
			continue;
		eyes17_cal_polys[ch][g][0] = c2;
		eyes17_cal_polys[ch][g][1] = c1;
		eyes17_cal_polys[ch][g][2] = c0;
		eyes17_cal_use_poly[ch][g] = TRUE;
	}
}

gboolean eyes17_calibration_load(const uint8_t *flash, size_t len)
{
	const uint8_t *mark, *stop, *p;
	size_t avail;
	gboolean have_a1 = FALSE;

	eyes17_calibration_clear();
	if (!flash || len < EYES17_FLASH_MAGIC_LEN)
		return FALSE;
	if (memcmp(flash, EYES17_FLASH_MAGIC, EYES17_FLASH_MAGIC_LEN) != 0)
		return FALSE;
	mark = eyes17_find_marker(flash, len,
		EYES17_FLASH_A1_MARKER, EYES17_FLASH_A1_MARKER_LEN);
	if (mark) {
		p = mark + EYES17_FLASH_A1_MARKER_LEN;
		avail = (size_t)(flash + len - p);
		stop = eyes17_find_marker(p, avail,
			EYES17_FLASH_STOP, EYES17_FLASH_STOP_LEN);
		if (stop)
			avail = (size_t)(stop - p);
		if (avail / EYES17_FLASH_POLY_LEN == 0)
			return FALSE;
		eyes17_load_channel_polys(p, avail, EYES17_CH_A1);
		have_a1 = TRUE;
	}
	mark = eyes17_find_marker(flash, len,
		EYES17_FLASH_A2_MARKER, EYES17_FLASH_A2_MARKER_LEN);
	if (mark) {
		p = mark + EYES17_FLASH_A2_MARKER_LEN;
		avail = (size_t)(flash + len - p);
		stop = eyes17_find_marker(p, avail,
			EYES17_FLASH_STOP, EYES17_FLASH_STOP_LEN);
		if (stop)
			avail = (size_t)(stop - p);
		eyes17_load_channel_polys(p, avail, EYES17_CH_A2);
	}
	mark = eyes17_find_marker(flash, len,
		EYES17_FLASH_A3_MARKER, EYES17_FLASH_A3_MARKER_LEN);
	if (mark) {
		p = mark + EYES17_FLASH_A3_MARKER_LEN;
		avail = (size_t)(flash + len - p);
		stop = eyes17_find_marker(p, avail,
			EYES17_FLASH_STOP, EYES17_FLASH_STOP_LEN);
		if (stop)
			avail = (size_t)(stop - p);
		eyes17_load_channel_polys(p, avail, EYES17_CH_A3);
	}
	mark = eyes17_find_marker(flash, len,
		EYES17_FLASH_MIC_MARKER, EYES17_FLASH_MIC_MARKER_LEN);
	if (mark) {
		p = mark + EYES17_FLASH_MIC_MARKER_LEN;
		avail = (size_t)(flash + len - p);
		stop = eyes17_find_marker(p, avail,
			EYES17_FLASH_STOP, EYES17_FLASH_STOP_LEN);
		if (stop)
			avail = (size_t)(stop - p);
		eyes17_load_channel_polys(p, avail, EYES17_CH_MIC);
	}
	if (!have_a1)
		return FALSE;
	eyes17_calibration_ready = TRUE;
	return TRUE;
}

/*
 * A1 symmetric range option (Task 6): golden select_range volts
 * (eyes.py:select_range:1303), 1:1 onto PGA gain indices 0..7.
 * Gain index 8 (external attenuator) has no range entry.
 */
static const char *eyes17_ranges[EYES17_NUM_RANGES] = {
	"16", "8", "4", "2.5", "1.5", "1", "0.5", "0.25",
};

const char **eyes17_range_list(unsigned *n)
{
	if (n)
		*n = ARRAY_SIZE(eyes17_ranges);
	return eyes17_ranges;
}

const char *eyes17_range_text(int gain)
{
	if (gain < 0 || gain >= EYES17_NUM_RANGES)
		return NULL;
	return eyes17_ranges[gain];
}

int eyes17_range_to_gain(const char *s, int *gain_out)
{
	int g;

	if (!s || !gain_out)
		return SR_ERR_ARG;
	for (g = 0; g < EYES17_NUM_RANGES; g++) {
		if (strcmp(s, eyes17_ranges[g]) == 0) {
			*gain_out = g;
			return SR_OK;
		}
	}
	return SR_ERR_ARG;
}

float eyes17_adc_to_volts(uint16_t raw, int gain, int ch)
{
	double x;
	int row;

	if (!eyes17_gain_is_valid(gain) || ch < 0 || ch >= EYES17_NUM_CHANNELS)
		return NAN;
	/* A3/MIC have no PGA (golden achan.py:gainPGAs): the gain
	 * index is validated but ignored, row 0 always. */
	row = (ch >= EYES17_CH_A3) ? 0 : gain;
	if (eyes17_calibration_ready && eyes17_cal_use_poly[ch][row]) {
		x = raw * EYES17_ADC12_FULL / EYES17_ADC10_FULL;
		return (float)(eyes17_cal_polys[ch][row][0] * x * x +
			eyes17_cal_polys[ch][row][1] * x +
			eyes17_cal_polys[ch][row][2]);
	}
	return eyes17_adc_to_volts_ideal(raw, gain, ch);
}
