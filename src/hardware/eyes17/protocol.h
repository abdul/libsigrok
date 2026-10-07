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

#define EYES17_HDR_ADC 2
#define EYES17_SUB_CAPTURE_ONE 1
#define EYES17_SUB_CAPTURE_TWO 2
/* Hardware triggering (acquisition.c). Golden eyes.py:configure_trigger:1182: */
/* [ADC, CONFIGURE_TRIGGER, (prescaler<<4)|(1<<chan), level u16le] + ACK. */
/* Single-channel A1 uses chan 0, prescaler 0 (8 ms hardware timeout). */
#define EYES17_SUB_CONFIGURE_TRIGGER 5
#define EYES17_TRIGGER_FRAME_LEN 5
 #define EYES17_TRIGGER_CHAN_A1 0
 #define EYES17_TRIGGER_LEVEL_MAX 1023
#define EYES17_TRIGGER_LEVEL_MAX_12 4095
/* 12-bit single-channel (acquisition.c, M6). Golden
 * eyes.py:capture_highres_traces: [ADC, CAPTURE_12BIT, CHOSA|trigger]
 * + count + tb8 + ACK. Floor 3 us (tb8 24), cap 10000. Fetch is the
 * same GET_CAPTURE_CHANNEL wire; codes are 12-bit (x = raw, no
 * upscale). 12-bit multi (incl. CAPTURE_12BIT_SCAN) is rejected. */
#define EYES17_SUB_CAPTURE_12BIT 13
#define EYES17_TIMEBASE_MIN_12BIT_US 3.0
#define EYES17_TB8_MIN_12BIT 24
#define EYES17_SUB_GET_CAPTURE_STATUS 6
#define EYES17_SUB_GET_CAPTURE_CHANNEL 7
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
/* Capture channels: 0 = A1, 1 = A2, 2 = A3, 3 = MIC.
 * A3/MIC share the gains list, 30% rule and ideal form
 * (golden achan.py:3,103-113); spans differ ([-3.3,+3.3]
 * non-inverted, achan.py:19-20) and the effective gain row is
 * always 0 (no PGA — gainPGAs has A1/A2 only, achan.py:29). */
#define EYES17_NUM_CHANNELS 4
#define EYES17_CH_A1 0
#define EYES17_CH_A2 1
#define EYES17_CH_A3 2
#define EYES17_CH_MIC 3
/* Trigger flag OR'd into the CHOSA byte (golden eyes.py:capture_traces:837). */
#define EYES17_CHOSA_TRIGGERED 0x80
#define EYES17_CAPTURE_FRAME_LEN 7
#define EYES17_FETCH_FRAME_LEN 7
/* Golden DATA_SPLITTING (eyes.py:116 via commands_proto.py:10): the
 * firmware serves at most this many samples per GET_CAPTURE_CHANNEL. */
#define EYES17_FETCH_CHUNK 200
/* Dual-channel capture (M4 Task 2). Golden eyes.py:capture_traces CAPTURE_TWO
 * frame [ADC=2,SUB=2,CHOSA=3]+count u16le+tb8 u16le+ACK; fetch
 * [ADC,7,ch-1,n,offset]. Dual floor 1.75 us (tb8 >= 14), cap 5000/ch. */
#define EYES17_TIMEBASE_MIN_DUAL_US 1.75
#define EYES17_TB8_MIN_DUAL 14
#define EYES17_MAX_SAMPLES_DUAL 5000
#define EYES17_FETCH_CH_A1 0
#define EYES17_FETCH_CH_A2 1
/* Quad capture (acquisition.c, M5). Golden eyes.py:capture_traces
 * num==4: [ADC, CAPTURE_FOUR, CHOSA|trigger] + count + tb8 + ACK.
 * Channel byte is A1's CHOSA (sel fixed A1); A2/A3/MIC implicit.
 * Floor 1.75 us (tb8 14), cap 2500/channel. Fetch bytes 0..3
 * (golden __fetch_channel__: channel_number - 1). FW <= 2.0 needs
 * the raw preamble [02,04,CHOSA,02,00,16,00] + 1-byte read first. */
#define EYES17_SUB_CAPTURE_FOUR 4
#define EYES17_TIMEBASE_MIN_QUAD_US 1.75
#define EYES17_TB8_MIN_QUAD 14
#define EYES17_MAX_SAMPLES_QUAD 2500
#define EYES17_FETCH_CH_A3 2
#define EYES17_FETCH_CH_MIC 3
#define EYES17_CAPTURE_TIMEOUT_MS 2000
/* Digital inputs (acquisition.c, M7). Golden eyes.py:get_states:2038:
 * [DIN=9, GET_STATES=1] -> 1 status byte, bit N = digital_inputs[N]
 * (IN2, SQR1_READ, OD1_READ, SEN, SQR1, OD1, SQ2, SQ3). Polled block:
 * driver samples the byte at the rate interval (no firmware LA
 * stream exists in the high-level API). */
#define EYES17_HDR_DIN 9
#define EYES17_SUB_GET_STATES 1
#define EYES17_NUM_DIGITAL 8
#define EYES17_DIGITAL_NAMES { "IN2", "SQR1_READ", "OD1_READ", "SEN", \
        "SQR1", "OD1", "SQ2", "SQ3" }
/* Poll-based caps (driver-measured, NOT golden): single serial */
/* round-trip ≈ 1-2 ms. */
#define EYES17_DIGITAL_MAX_SAMPLERATE 1000
#define EYES17_DIGITAL_MAX_SAMPLES 4096
/* On-board stimulus (M12). Golden eyes.py:set_wave:2771: WG frame is
 * [WAVEGEN, SET_SINE1, HIGHRES|(prescaler<<1), wavelength-1 u16le] +
 * ACK; 512-pt table below 1100 Hz, 32-pt at/above; prescalers
 * 1/8/64/256 off 64 MHz, first wavelength < 65525 wins; below 0.1 Hz
 * (incl. 0) the output switches off via [.., 0x80, 0]. Amplitude is a
 * separate [WAVEGEN, SET_SINE_AMP, 0/1/2] + ACK (0.1/1/3.3 V,
 * golden eyes.py:set_sine_amp:2839). Tria needs table upload and is
 * out of scope (own task). */
#define EYES17_HDR_WAVEGEN 7
#define EYES17_SUB_SET_SINE1 13
#define EYES17_SUB_SET_SINE_AMP 16
#define EYES17_WG_TABLE_HIRES 512
#define EYES17_WG_TABLE_LORES 32
#define EYES17_WG_TABLE_SWITCH_HZ 1100
#define EYES17_WG_MIN_HZ 0.1
#define EYES17_WG_MAX_HZ 2000000
#define EYES17_WG_FRAME_LEN 5
#define EYES17_WG_OFF_BYTE 0x80
#define EYES17_WG_AMP_STEPS { 0.1, 1.0, 3.3 }
/* SQ stimulus (M12 Task 2). Golden set_sqr1:2990: fast frame is
 * [WAVEGEN, SET_SQR1, wavelength u16le, high_time u16le, prescaler]
 * + ACK (prescalers 1/8/64/256 off 64 MHz, SQ2 = prescaler|0x4,
 * golden set_sqr2:3090); freq < 4 Hz falls through to the slow path
 * [WAVEGEN, SET_SQR_LONG, W lo/hi, H lo/hi] + ACK (SQ1 only, golden
 * set_sqr1_slow:3043, 32-bit counters); 0/-1 parks HIGH/LOW via
 * [DOUT=8, SET_STATE=1, data] (golden set_state:2078); duty 0 means
 * no-act. SQ2 has no golden slow path (prescaler-exhausted no-send).
 */
#define EYES17_SUB_SET_SQR1 3
#define EYES17_SUB_SET_SQR_LONG 4
#define EYES17_SQ_FAST_MIN_HZ 4
#define EYES17_SQ_FAST_MAX_HZ 10000000
#define EYES17_HDR_DOUT 8
#define EYES17_SUB_SET_STATE 1
#define EYES17_SQ_PARK_HIGH 0
#define EYES17_SQ_PARK_LOW -1
#define EYES17_SQ_SLOW_W_MAX 4294967295.0
/* PV stimulus (M12 Task 3). Golden Peripherals.py:__setRawVoltage__:716:
 * [DAC, SET_DAC, code u16le] + ACK, PV1 channel 3 span [-5,5] plain,
 * PV2 channel 2 span [-3.3,3.3] with the 0x8000 flag. Code is the
 * ideal 12-bit linear map (DAC calibration skipped, documented);
 * out-of-span is rejected, never clamped silently. */
#define EYES17_HDR_DAC 6
#define EYES17_SUB_SET_DAC 1

struct eyes17_version {
	char raw[32];
	int major;
	int minor;
};

/* Config surface and acquisition validation (api.c, Task 6). A1
 * symmetric input ranges (+/-V), golden eyes.py:select_range:1303, map
 * 1:1 onto PGA gain indices 0..7; index 8 stays excluded. */
#define EYES17_NUM_RANGES 8
#define EYES17_RESOLUTION_10BIT 10
#define EYES17_RESOLUTION_12BIT 12
#define EYES17_DEFAULT_SAMPLERATE 100000
#define EYES17_DEFAULT_LIMIT_SAMPLES 1000
#define EYES17_ANALOG_DIGITS 3

struct dev_context {
	struct sr_serial_dev_inst *serial;
	struct eyes17_version fw;
	GByteArray *rxbuf;
	uint64_t samplerate;
	uint64_t limit_samples;
	int gain;
	int resolution;
	char trigger_source[8];
	char trigger_slope[8];
	double trigger_level;
	char wg_wave[8];
	double wg_freq;
	double wg_amp;
	gboolean wg_touched;
	double sq1_freq;
	double sq1_duty;
	gboolean sq1_touched;
	double sq2_freq;
	double sq2_duty;
	gboolean sq2_touched;
	double pv1_volts;
	gboolean pv1_touched;
	double pv2_volts;
	gboolean pv2_touched;
};

SR_PRIV void eyes17_put_u16_le(uint8_t *p, uint16_t v);
SR_PRIV void eyes17_put_u32_le(uint8_t *p, uint32_t v);
SR_PRIV uint16_t eyes17_get_u16_le(const uint8_t *p);
SR_PRIV uint32_t eyes17_get_u32_le(const uint8_t *p);
SR_PRIV int eyes17_parse_version(const char *s, struct eyes17_version *out);
SR_PRIV int eyes17_send_cmd(struct sr_serial_dev_inst *serial, uint8_t hdr,
		uint8_t sub, const uint8_t *args, size_t arglen);
SR_PRIV int eyes17_write_cmd(struct sr_serial_dev_inst *serial, uint8_t hdr,
		uint8_t sub, const uint8_t *args, size_t arglen);
SR_PRIV int eyes17_read_ack(struct sr_serial_dev_inst *serial);
SR_PRIV int eyes17_get_version(struct sr_serial_dev_inst *serial,
	struct eyes17_version *out);
SR_PRIV double eyes17_py_round(double v);
SR_PRIV int eyes17_wg_check(const char *wave, double freq, double amp);
SR_PRIV int eyes17_wg_amp_step(double volts, int *step_out);
SR_PRIV size_t eyes17_build_wg(uint8_t *buf, double freq);
SR_PRIV int eyes17_wg_apply(struct sr_serial_dev_inst *serial,
	const char *wave, double freq, double amp);
SR_PRIV int eyes17_sq_check(int which, double freq, double duty);
SR_PRIV size_t eyes17_build_sq_fast(uint8_t *buf, double freq,
	double duty, int sq2);
SR_PRIV size_t eyes17_build_sq_slow(uint8_t *buf, double freq,
	double duty);
SR_PRIV size_t eyes17_build_sq_park(uint8_t *buf, int which, int high);
SR_PRIV int eyes17_sq_apply(struct sr_serial_dev_inst *serial, int which,
	double freq, double duty);
SR_PRIV int eyes17_pv_check(int which, double volts);
SR_PRIV int eyes17_pv_code(int which, double volts, uint16_t *code_out);
SR_PRIV size_t eyes17_build_pv(uint8_t *buf, int which, uint16_t code);
SR_PRIV int eyes17_pv_apply(struct sr_serial_dev_inst *serial, int which,
	double volts);
SR_PRIV uint16_t eyes17_timebase_to_tb8(double timebase_us);
SR_PRIV void eyes17_tb8_to_rate(uint16_t tb8, uint64_t *num, uint64_t *den);
SR_PRIV uint16_t eyes17_clamp_count(size_t count);
SR_PRIV size_t eyes17_build_capture_one(uint8_t *buf, uint16_t tb8,
		uint16_t count);
SR_PRIV size_t eyes17_build_capture_two(uint8_t *buf, uint16_t tb8,
		uint16_t count);
SR_PRIV size_t eyes17_build_fetch_channel(uint8_t *buf, uint8_t ch,
		uint16_t n, uint16_t offset);
SR_PRIV float eyes17_adc_to_volts(uint16_t raw, int gain, int ch);
SR_PRIV float eyes17_adc_to_volts_12(uint16_t raw, int gain, int ch);
SR_PRIV float eyes17_adc_to_volts_ideal_12(uint16_t raw, int gain, int ch);
SR_PRIV size_t eyes17_build_trigger(uint8_t *buf, uint16_t level);
SR_PRIV int eyes17_trigger_level_code(double volts, int gain, uint16_t *code_out);
SR_PRIV int eyes17_trigger_level_code_12(double volts, int gain, uint16_t *code_out);
SR_PRIV int eyes17_configure_trigger(struct sr_serial_dev_inst *serial, uint16_t level);

/* Gain table, ideal curve, flash discipline (calibration.c). Gain
 * index 8 is external-attenuator-only and rejected from the normal path. */
#define EYES17_GAIN_NORMAL_MAX 7
#define EYES17_GAIN_EXTERNAL 8
SR_PRIV double eyes17_gain_factor(int gain);
SR_PRIV gboolean eyes17_gain_is_valid(int gain);
SR_PRIV float eyes17_adc_to_volts_ideal(uint16_t raw, int gain, int ch);
SR_PRIV gboolean eyes17_calibration_is_ready(void);
SR_PRIV gboolean eyes17_calibration_load(const uint8_t *flash, size_t len);
SR_PRIV size_t eyes17_build_capture_12bit(uint8_t *buf, uint16_t tb8,
		uint16_t count);
SR_PRIV int eyes17_capture_12bit(struct sr_serial_dev_inst *serial,
		uint16_t tb8, uint16_t count, int gain, float *volts_out);
SR_PRIV int eyes17_capture_12bit_triggered(struct sr_serial_dev_inst *serial,
		uint16_t tb8, uint16_t count, int gain, uint16_t level,
		float *volts_out);
SR_PRIV int eyes17_check_trigger(const char *source, const char *slope,
		double level_volts, int gain, int resolution, uint16_t *level_out);
SR_PRIV int eyes17_capture_triggered(struct sr_serial_dev_inst *serial,
		uint16_t tb8, uint16_t count, int gain, uint16_t level,
		float *volts_out);
SR_PRIV int eyes17_capture_two(struct sr_serial_dev_inst *serial,
		uint16_t tb8, uint16_t count, int gain, float *a1_out,
		float *a2_out);
SR_PRIV int eyes17_capture_two_triggered(struct sr_serial_dev_inst *serial,
		uint16_t tb8, uint16_t count, int gain, uint16_t level,
		float *a1_out, float *a2_out);
SR_PRIV int eyes17_capture_one(struct sr_serial_dev_inst *serial,
		uint16_t tb8, uint16_t count, int gain, float *volts_out);
SR_PRIV size_t eyes17_build_capture_four(uint8_t *buf, uint16_t tb8,
		uint16_t count);
SR_PRIV size_t eyes17_build_quad_bug_preamble(uint8_t *buf, uint8_t chosa);
SR_PRIV int eyes17_capture_four(struct sr_serial_dev_inst *serial,
		uint16_t tb8, uint16_t count, int gain, gboolean legacy_fw_bug,
		float *v1_out, float *v2_out, float *v3_out, float *v4_out);
SR_PRIV int eyes17_capture_four_triggered(struct sr_serial_dev_inst *serial,
		uint16_t tb8, uint16_t count, int gain, uint16_t level,
		gboolean legacy_fw_bug,
		float *v1_out, float *v2_out, float *v3_out, float *v4_out);

/* Samplerate ladder, resolution gate, combo validation (Task 6). */
SR_PRIV const uint64_t *eyes17_samplerate_list(unsigned *n);
SR_PRIV int eyes17_samplerate_to_tb8(uint64_t rate, uint16_t *tb8_out);
SR_PRIV int eyes17_check_resolution(int bits);
SR_PRIV int eyes17_check_acquisition(uint64_t samplerate, uint64_t limit,
		int gain, int resolution, int channels, uint16_t *tb8_out,
		uint16_t *count_out);
SR_PRIV const char **eyes17_range_list(unsigned *n);
SR_PRIV const char *eyes17_range_text(int gain);
SR_PRIV int eyes17_range_to_gain(const char *s, int *gain_out);
SR_PRIV int eyes17_poll_states(struct sr_serial_dev_inst *serial,
        uint8_t *state_out);
SR_PRIV int eyes17_check_digital(uint64_t samplerate, uint64_t limit,
        uint16_t *interval_ms_out, uint16_t *count_out);
SR_PRIV uint8_t eyes17_pack_logic_sample(uint8_t status, uint8_t mask);
/* Polled logic block (M7 Task 2): count polls at interval_ms apart
 * (g_usleep between polls, none after the last), each packed through
 * mask into samples_out. Any poll failure aborts with its code.
 * Wall bounded by count x (interval + timeout), caps keep it <= ~5 s. */
SR_PRIV int eyes17_capture_logic(struct sr_serial_dev_inst *serial,
        uint16_t interval_ms, uint16_t count, uint8_t mask,
        uint8_t *samples_out);

#endif
