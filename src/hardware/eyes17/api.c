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

static const uint32_t scanopts[] = {
	SR_CONF_CONN,
	SR_CONF_SERIALCOMM,
};

static const uint32_t drvopts[] = {
	SR_CONF_OSCILLOSCOPE,
};

static const uint32_t devopts[] = {
	SR_CONF_SAMPLERATE | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_LIMIT_SAMPLES | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_RANGE | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_DIGITS | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_TRIGGER_SOURCE | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_TRIGGER_SLOPE | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_TRIGGER_LEVEL | SR_CONF_GET | SR_CONF_SET,
};

/* Only A1 has a golden trigger path; "none" is immediate capture. */
static const char *eyes17_trigger_sources[] = { "none", "A1" };
static const char *eyes17_trigger_slopes[] = { "rising" };

/* ADC resolution option: 10-bit now, 12-bit arrives with M6. */
 static const char *eyes17_digits[] = { "10", "12" };

/*
 * All of scan/config_get/config_set/config_list/dev_open/dev_close/
 * dev_acquisition_start/dev_acquisition_stop must be non-NULL: this tree's
 * src/backend.c sanity_check_all_drivers() aborts sr_init() for the whole
 * library otherwise. scan probes the given conn for an ExpEYES17 firmware
 * string; dev_open/dev_close are the standard serial helpers.
 * config_channel_set/config_commit are genuinely optional and stay NULL.
 */
static GSList *scan(struct sr_dev_driver *driver, GSList *options)
{
	struct sr_dev_inst *sdi;
	struct dev_context *devc;
	struct sr_config *src;
	struct sr_serial_dev_inst *serial;
	GSList *l;
	const char *conn, *serialcomm;

	conn = NULL;
	serialcomm = "500000/8n1";
	for (l = options; l; l = l->next) {
		src = l->data;
		switch (src->key) {
		case SR_CONF_CONN:
			conn = g_variant_get_string(src->data, NULL);
			break;
		case SR_CONF_SERIALCOMM:
			serialcomm = g_variant_get_string(src->data, NULL);
			break;
		}
	}
	if (!conn)
		return NULL;

	serial = sr_serial_dev_inst_new(conn, serialcomm);
	if (serial_open(serial, SERIAL_RDWR) != SR_OK) {
		sr_serial_dev_inst_free(serial);
		return NULL;
	}

	devc = g_malloc0(sizeof(*devc));
	devc->samplerate = EYES17_DEFAULT_SAMPLERATE;
	devc->limit_samples = EYES17_DEFAULT_LIMIT_SAMPLES;
	devc->gain = 0;
	devc->resolution = EYES17_RESOLUTION_10BIT;
	g_strlcpy(devc->trigger_source, "none", sizeof(devc->trigger_source));
	g_strlcpy(devc->trigger_slope, "rising", sizeof(devc->trigger_slope));
	devc->trigger_level = 0.0;
	if (eyes17_get_version(serial, &devc->fw) != SR_OK) {
		g_free(devc);
		serial_close(serial);
		sr_serial_dev_inst_free(serial);
		return NULL;
	}
	serial_close(serial);

	sdi = g_malloc0(sizeof(*sdi));
	sdi->status = SR_ST_INACTIVE;
	sdi->vendor = g_strdup("ExpEYES");
	sdi->model = g_strdup("17");
	sdi->version = g_strdup(devc->fw.raw);
	sdi->inst_type = SR_INST_SERIAL;
	sdi->conn = serial;
	sdi->priv = devc;
	devc->serial = serial;
	sr_channel_new(sdi, 0, SR_CHANNEL_ANALOG, TRUE, "A1");
	/* A2 joins disabled: default acquisitions stay single-channel (M2/M3 behavior byte-identical); enabling A2 selects dual. */
	sr_channel_new(sdi, 1, SR_CHANNEL_ANALOG, FALSE, "A2");

	return std_scan_complete(driver, g_slist_append(NULL, sdi));
}

static int config_get(uint32_t key, GVariant **data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	struct dev_context *devc;
	const char *text;

	(void)cg;

	if (!sdi || !data)
		return SR_ERR_ARG;
	devc = sdi->priv;
	if (!devc)
		return SR_ERR_ARG;

	switch (key) {
	case SR_CONF_SAMPLERATE:
		*data = g_variant_new_uint64(devc->samplerate);
		break;
	case SR_CONF_LIMIT_SAMPLES:
		*data = g_variant_new_uint64(devc->limit_samples);
		break;
	case SR_CONF_RANGE:
		text = eyes17_range_text(devc->gain);
		if (!text)
			return SR_ERR_BUG;
		*data = g_variant_new_string(text);
		break;
	case SR_CONF_DIGITS:
		if (devc->resolution == EYES17_RESOLUTION_10BIT)
			text = "10";
		else if (devc->resolution == EYES17_RESOLUTION_12BIT)
			text = "12";
		else
			return SR_ERR_BUG;
		*data = g_variant_new_string(text);
		break;
	case SR_CONF_TRIGGER_SOURCE:
		*data = g_variant_new_string(devc->trigger_source);
		break;
	case SR_CONF_TRIGGER_SLOPE:
		*data = g_variant_new_string(devc->trigger_slope);
		break;
	case SR_CONF_TRIGGER_LEVEL:
		*data = g_variant_new_double(devc->trigger_level);
		break;
	default:
		return SR_ERR_NA;
	}

	return SR_OK;
}

static int config_set(uint32_t key, GVariant *data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	struct dev_context *devc;
	const uint64_t *rates;
	const char *s;
	unsigned n;
	int gain;

	(void)cg;

	if (!sdi || !data)
		return SR_ERR_ARG;
	devc = sdi->priv;
	if (!devc)
		return SR_ERR_ARG;

	switch (key) {
	case SR_CONF_SAMPLERATE:
		rates = eyes17_samplerate_list(&n);
		if (std_u64_idx(data, rates, n) < 0)
			return SR_ERR_ARG;
		devc->samplerate = g_variant_get_uint64(data);
		break;
	case SR_CONF_LIMIT_SAMPLES:
		/* Clamp per the Task-4 convention; zero fails at start. */
		devc->limit_samples =
			eyes17_clamp_count((size_t)g_variant_get_uint64(data));
		break;
	case SR_CONF_RANGE:
		s = g_variant_get_string(data, NULL);
		if (eyes17_range_to_gain(s, &gain) != SR_OK)
			return SR_ERR_ARG;
		devc->gain = gain;
		break;
	case SR_CONF_DIGITS:
		s = g_variant_get_string(data, NULL);
		if (g_strcmp0(s, "10") == 0) {
			devc->resolution = EYES17_RESOLUTION_10BIT;
		} else if (g_strcmp0(s, "12") == 0) {
			sr_err("12-bit capture not yet supported (M6).");
			return SR_ERR;
		} else {
			return SR_ERR_ARG;
		}
		break;
	case SR_CONF_TRIGGER_SOURCE:
		if (std_str_idx(data, ARRAY_AND_SIZE(eyes17_trigger_sources)) < 0)
			return SR_ERR_ARG;
		g_strlcpy(devc->trigger_source, g_variant_get_string(data, NULL), sizeof(devc->trigger_source));
		break;
	case SR_CONF_TRIGGER_SLOPE:
		if (std_str_idx(data, ARRAY_AND_SIZE(eyes17_trigger_slopes)) < 0)
			return SR_ERR_ARG;
		g_strlcpy(devc->trigger_slope, g_variant_get_string(data, NULL), sizeof(devc->trigger_slope));
		break;
	case SR_CONF_TRIGGER_LEVEL:
		devc->trigger_level = g_variant_get_double(data);
		break;
	default:
		return SR_ERR_NA;
	}

	return SR_OK;
}

static int config_list(uint32_t key, GVariant **data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	unsigned n;

	switch (key) {
	case SR_CONF_SCAN_OPTIONS:
	case SR_CONF_DEVICE_OPTIONS:
		return std_opts_config_list(key, data, sdi, cg,
			ARRAY_AND_SIZE(scanopts), ARRAY_AND_SIZE(drvopts),
			ARRAY_AND_SIZE(devopts));
	case SR_CONF_SAMPLERATE:
		*data = std_gvar_samplerates(eyes17_samplerate_list(&n), n);
		return SR_OK;
	case SR_CONF_RANGE:
		*data = std_gvar_array_str(eyes17_range_list(&n), n);
		return SR_OK;
	case SR_CONF_DIGITS:
		*data = std_gvar_array_str(ARRAY_AND_SIZE(eyes17_digits));
		return SR_OK;
	case SR_CONF_TRIGGER_SOURCE:
		*data = std_gvar_array_str(ARRAY_AND_SIZE(eyes17_trigger_sources));
		return SR_OK;
	case SR_CONF_TRIGGER_SLOPE:
		*data = std_gvar_array_str(ARRAY_AND_SIZE(eyes17_trigger_slopes));
		return SR_OK;
	default:
		return SR_ERR_NA;
	}
}

/*
 * Single-shot A1 acquisition: validate the requested combo (including
 * the trigger level against the current gain), run the immediate or
 * triggered capture, then emit HEADER, one ANALOG packet
 * (SR_MQ_VOLTAGE, volts) and END. Gain selects the calibration curve.
 */
static int dev_acquisition_start(const struct sr_dev_inst *sdi)
{
    struct dev_context *devc;
    struct sr_serial_dev_inst *serial;
    struct sr_datafeed_packet packet;
    struct sr_datafeed_analog analog;
    struct sr_analog_encoding encoding;
    struct sr_analog_meaning meaning;
    struct sr_analog_spec spec;
    GSList *l;
    struct sr_channel *ch;
    struct sr_channel *ch_a1 = NULL, *ch_a2 = NULL;
    GSList *ach1 = NULL, *ach2 = NULL;
    int n_enabled = 0, a2_enabled = 0;
    uint16_t tb8, count;
    uint16_t level;
    float *volts, *volts2;
    int ret;

    if (!sdi || !sdi->priv || !sdi->conn)
        return SR_ERR_ARG;
    devc = sdi->priv;
    serial = sdi->conn;

    for (l = sdi->channels; l; l = l->next) {
        ch = l->data;
        if (ch->type != SR_CHANNEL_ANALOG || !ch->enabled)
            continue;
        n_enabled++;
        if (ch->index == 0)
            ch_a1 = ch;
        else if (ch->index == 1)
            a2_enabled = 1, ch_a2 = ch;
    }
    if (n_enabled == 0)
        return SR_ERR_ARG;
    if (a2_enabled && n_enabled != 2)
        return SR_ERR_ARG; /* A2-only has no golden wire path. */

    ret = eyes17_check_acquisition(devc->samplerate, devc->limit_samples,
        devc->gain, devc->resolution, n_enabled, &tb8, &count);
    if (ret != SR_OK)
        return ret;

    ret = eyes17_check_trigger(devc->trigger_source, devc->trigger_slope,
        devc->trigger_level, devc->gain, &level);
    if (ret != SR_OK)
        return ret;

    if (n_enabled == 1) {
        /* Unchanged M2/M3 single path. */
        volts = g_malloc(count * sizeof(*volts));
        if (g_strcmp0(devc->trigger_source, "none") == 0)
            ret = eyes17_capture_one(serial, tb8, count,
                devc->gain, volts);
        else
            ret = eyes17_capture_triggered(serial, tb8, count,
                devc->gain, level, volts);
        if (ret != SR_OK) {
            g_free(volts);
            return ret;
        }

        std_session_send_df_header(sdi);

        sr_analog_init(&analog, &encoding, &meaning, &spec,
            EYES17_ANALOG_DIGITS);
        analog.meaning->mq = SR_MQ_VOLTAGE;
        analog.meaning->unit = SR_UNIT_VOLT;
        analog.meaning->channels = sdi->channels;
        analog.num_samples = count;
        analog.data = volts;
        packet.type = SR_DF_ANALOG;
        packet.payload = &analog;
        sr_session_send(sdi, &packet);
        g_free(volts);

        std_session_send_df_end(sdi);

        return SR_OK;
    }

    /* Dual path: fixed A1+A2 pair, per-channel volts + packets. */
    volts = g_malloc(count * sizeof(*volts));
    volts2 = g_malloc(count * sizeof(*volts2));
    if (g_strcmp0(devc->trigger_source, "none") == 0)
        ret = eyes17_capture_two(serial, tb8, count,
            devc->gain, volts, volts2);
    else
        ret = eyes17_capture_two_triggered(serial, tb8, count,
            devc->gain, level, volts, volts2);
    if (ret != SR_OK) {
        g_free(volts);
        g_free(volts2);
        return ret;
    }

    std_session_send_df_header(sdi);

    ach1 = g_slist_append(ach1, ch_a1);
    sr_analog_init(&analog, &encoding, &meaning, &spec,
        EYES17_ANALOG_DIGITS);
    analog.meaning->mq = SR_MQ_VOLTAGE;
    analog.meaning->unit = SR_UNIT_VOLT;
    analog.meaning->channels = ach1;
    analog.num_samples = count;
    analog.data = volts;
    packet.type = SR_DF_ANALOG;
    packet.payload = &analog;
    sr_session_send(sdi, &packet);

    ach2 = g_slist_append(ach2, ch_a2);
    sr_analog_init(&analog, &encoding, &meaning, &spec,
        EYES17_ANALOG_DIGITS);
    analog.meaning->mq = SR_MQ_VOLTAGE;
    analog.meaning->unit = SR_UNIT_VOLT;
    analog.meaning->channels = ach2;
    analog.num_samples = count;
    analog.data = volts2;
    packet.type = SR_DF_ANALOG;
    packet.payload = &analog;
    sr_session_send(sdi, &packet);

    g_slist_free(ach1);
    g_slist_free(ach2);
    g_free(volts);
    g_free(volts2);

    std_session_send_df_end(sdi);

    return SR_OK;
}

static struct sr_dev_driver eyes17_driver_info = {
	.name = "eyes17",
	.longname = "ExpEYES-17",
	.api_version = 1,
	.init = std_init,
	.cleanup = std_cleanup,
	.scan = scan,
	.dev_list = std_dev_list,
	.dev_clear = std_dev_clear,
	.config_get = config_get,
	.config_set = config_set,
	.config_channel_set = NULL,
	.config_commit = NULL,
	.config_list = config_list,
	.dev_open = std_serial_dev_open,
	.dev_close = std_serial_dev_close,
	.dev_acquisition_start = dev_acquisition_start,
	.dev_acquisition_stop = std_serial_dev_acquisition_stop,
	.context = NULL,
};
SR_REGISTER_DEV_DRIVER(eyes17_driver_info);
