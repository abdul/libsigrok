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

/* ADC resolution option: 10-bit or 12-bit. */
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
	/* A2 joins disabled: default acquisitions stay single-channel; */
	/* enabling A2 selects dual. */
	sr_channel_new(sdi, 1, SR_CHANNEL_ANALOG, FALSE, "A2");
	/* A3/MIC join disabled: quad is opt-in; single/dual defaults unchanged. */
	sr_channel_new(sdi, 2, SR_CHANNEL_ANALOG, FALSE, "A3");
	sr_channel_new(sdi, 3, SR_CHANNEL_ANALOG, FALSE, "MIC");
	/*
	 * Logic channels restart indexes at 0 per type (rigol-ds /
	 * hameg-hmo precedent for mixed analog+logic scopes: their
	 * digital channels restart at index 0 rather than continuing
	 * past the analog ones). Disabled by default so analog
	 * acquisitions stay byte-identical. Names are the PRD §27
	 * digital_inputs order, matching GET_STATES bit N.
	 */
	{
		static const char *names[] = EYES17_DIGITAL_NAMES;
		int i;

		for (i = 0; i < EYES17_NUM_DIGITAL; i++)
			sr_channel_new(sdi, i, SR_CHANNEL_LOGIC, FALSE, names[i]);
	}

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
		if (!g_variant_is_of_type(data, G_VARIANT_TYPE_UINT64))
			return SR_ERR_ARG;
		rates = eyes17_samplerate_list(&n);
		if (std_u64_idx(data, rates, n) < 0) {
			/*
			 * Off-ladder rate: accept any 1..1000 Hz integer
			 * for logic-only polling (M7 Task 2). The ladder
			 * list is unchanged; validation happens at start
			 * (eyes17_check_digital for logic-only,
			 * eyes17_check_acquisition still rejects
			 * off-ladder rates for analog). No dedicated
			 * option unless the list/review asks for one.
			 */
			uint64_t r;

			r = g_variant_get_uint64(data);
			if (r < 1 || r > EYES17_DIGITAL_MAX_SAMPLERATE)
				return SR_ERR_ARG;
		}
		devc->samplerate = g_variant_get_uint64(data);
		break;
	case SR_CONF_LIMIT_SAMPLES:
		if (!g_variant_is_of_type(data, G_VARIANT_TYPE_UINT64))
			return SR_ERR_ARG;
		/* Clamp per the Task-4 convention; zero fails at start. */
		devc->limit_samples =
			eyes17_clamp_count((size_t)g_variant_get_uint64(data));
		break;
	case SR_CONF_RANGE:
		if (!g_variant_is_of_type(data, G_VARIANT_TYPE_STRING))
			return SR_ERR_ARG;
		s = g_variant_get_string(data, NULL);
		if (eyes17_range_to_gain(s, &gain) != SR_OK)
			return SR_ERR_ARG;
		devc->gain = gain;
		break;
	case SR_CONF_DIGITS:
		if (!g_variant_is_of_type(data, G_VARIANT_TYPE_STRING))
			return SR_ERR_ARG;
		s = g_variant_get_string(data, NULL);
		if (g_strcmp0(s, "10") == 0) {
			devc->resolution = EYES17_RESOLUTION_10BIT;
		} else if (g_strcmp0(s, "12") == 0) {
			devc->resolution = EYES17_RESOLUTION_12BIT;
		} else {
			return SR_ERR_ARG;
		}
		break;
	case SR_CONF_TRIGGER_SOURCE:
		if (!g_variant_is_of_type(data, G_VARIANT_TYPE_STRING))
			return SR_ERR_ARG;
		if (std_str_idx(data, ARRAY_AND_SIZE(eyes17_trigger_sources)) < 0)
			return SR_ERR_ARG;
		g_strlcpy(devc->trigger_source,
			g_variant_get_string(data, NULL),
			sizeof(devc->trigger_source));
		break;
	case SR_CONF_TRIGGER_SLOPE:
		if (!g_variant_is_of_type(data, G_VARIANT_TYPE_STRING))
			return SR_ERR_ARG;
		if (std_str_idx(data, ARRAY_AND_SIZE(eyes17_trigger_slopes)) < 0)
			return SR_ERR_ARG;
		g_strlcpy(devc->trigger_slope,
			g_variant_get_string(data, NULL),
			sizeof(devc->trigger_slope));
		break;
	case SR_CONF_TRIGGER_LEVEL:
		if (!g_variant_is_of_type(data, G_VARIANT_TYPE_DOUBLE))
			return SR_ERR_ARG;
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
 * Single/dual/quad A1 acquisition: validate the requested combo (including
 * the trigger level against the current gain), run the immediate or
 * triggered capture, then emit HEADER, per-channel ANALOG packets
 * (SR_MQ_VOLTAGE, volts) and END. Gain selects the calibration curve.
 * Quad is the fixed A1+A2+A3+MIC set (exactly indexes {0,1,2,3});
 * any other enabled count/set without a golden wire path fails SR_ERR_ARG.
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
    struct sr_channel *ch_a1 = NULL, *ch_a2 = NULL, *ch_a3 = NULL, *ch_mic = NULL;
    GSList *ach1 = NULL, *ach2 = NULL;
    GSList *logic_ch = NULL;
    struct sr_datafeed_logic logic;
    int n_enabled = 0, a2_enabled = 0;
    int have_a1 = 0, have_a2 = 0, have_a3 = 0, have_mic = 0;
    int n_logic = 0;
    uint8_t logic_mask = 0;
    uint16_t tb8, count;
    uint16_t level;
    uint16_t dig_interval, dig_count;
    float *volts, *volts2;
    uint8_t *samples;
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
            ch_a1 = ch, have_a1 = 1;
        else if (ch->index == 1)
            a2_enabled = 1, ch_a2 = ch, have_a2 = 1;
        else if (ch->index == 2)
            ch_a3 = ch, have_a3 = 1;
        else if (ch->index == 3)
            ch_mic = ch, have_mic = 1;
    }
    /* Enabled logic channels (indexes restart at 0 per type; bit N
     * of the mask is GET_STATES bit N in PRD order). */
    for (l = sdi->channels; l; l = l->next) {
        ch = l->data;
        if (ch->type != SR_CHANNEL_LOGIC || !ch->enabled)
            continue;
        if (ch->index < 0 || ch->index >= EYES17_NUM_DIGITAL)
            return SR_ERR_ARG;
        logic_mask |= (uint8_t)(1u << ch->index);
        logic_ch = g_slist_append(logic_ch, ch);
        n_logic++;
    }
    if (n_logic > 0) {
        if (n_enabled > 0) {
            /* No mixed wire path: poll timestamps could not align
             * with ADC frames. */
            g_slist_free(logic_ch);
            sr_err("Mixed analog+logic acquisition is not supported; enable only logic channels.");
            return SR_ERR_ARG;
        }
        /* Logic-only: samplerate carries the poll rate (any 1..1000
         * Hz integer; ladder list unchanged). One SR_DF_LOGIC
         * packet (fx2lafw la_send_data_proc idiom: unitsize 1,
         * dense LSB-first bytes), header/data/end. */
        ret = eyes17_check_digital(devc->samplerate, devc->limit_samples,
            &dig_interval, &dig_count);
        if (ret != SR_OK) {
            g_slist_free(logic_ch);
            return ret;
        }
        samples = g_malloc(dig_count * sizeof(*samples));
        ret = eyes17_capture_logic(serial, dig_interval, dig_count,
            logic_mask, samples);
        if (ret != SR_OK) {
            g_slist_free(logic_ch);
            g_free(samples);
            return ret;
        }
        std_session_send_df_header(sdi);
        logic.unitsize = 1;
        logic.length = dig_count;
        logic.data = samples;
        packet.type = SR_DF_LOGIC;
        packet.payload = &logic;
        sr_session_send(sdi, &packet);
        g_slist_free(logic_ch);
        g_free(samples);
        std_session_send_df_end(sdi);
        return SR_OK;
    }
    if (n_enabled != 1 && n_enabled != 2 && n_enabled != 4)
        return SR_ERR_ARG; /* No golden wire path (notably 3-channel). */
    if (a2_enabled && n_enabled != 2 && n_enabled != 4)
        return SR_ERR_ARG; /* A2-only has no golden wire path. */
    if ((have_a3 || have_mic) && n_enabled != 4)
        return SR_ERR_ARG; /* A3/MIC only ride the fixed quad set. */
    if (n_enabled == 4 && !(have_a1 && have_a2 && have_a3 && have_mic))
        return SR_ERR_ARG; /* Quad is exactly A1+A2+A3+MIC. */

    ret = eyes17_check_acquisition(devc->samplerate, devc->limit_samples,
        devc->gain, devc->resolution, n_enabled, &tb8, &count);
    if (ret != SR_OK)
        return ret;

    ret = eyes17_check_trigger(devc->trigger_source, devc->trigger_slope,
        devc->trigger_level, devc->gain, devc->resolution, &level);
    if (ret != SR_OK)
        return ret;

    if (n_enabled == 1) {
        /* 12-bit multi already fails in eyes17_check_acquisition above;
         * resolution 12 here is always the single-channel 12-bit wire. */
        volts = g_malloc(count * sizeof(*volts));
        if (g_strcmp0(devc->trigger_source, "none") == 0) {
            if (devc->resolution == EYES17_RESOLUTION_12BIT)
                ret = eyes17_capture_12bit(serial, tb8, count,
                    devc->gain, volts);
            else
                ret = eyes17_capture_one(serial, tb8, count,
                    devc->gain, volts);
        } else {
            if (devc->resolution == EYES17_RESOLUTION_12BIT)
                ret = eyes17_capture_12bit_triggered(serial, tb8, count,
                    devc->gain, level, volts);
            else
                ret = eyes17_capture_triggered(serial, tb8, count,
                    devc->gain, level, volts);
        }
        if (ret != SR_OK) {
            g_free(volts);
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
	g_slist_free(ach1);
	g_free(volts);

	std_session_send_df_end(sdi);

	return SR_OK;
    }

    if (n_enabled == 2) {
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

    /* Quad path: fixed A1+A2+A3+MIC set, per-channel volts + packets. */
    {
    float *volts3, *volts4;
    GSList *ach3 = NULL, *ach4 = NULL;
    gboolean legacy_fw_bug;

    legacy_fw_bug = (devc->fw.major < 2 ||
        (devc->fw.major == 2 && devc->fw.minor == 0)) ? TRUE : FALSE;
    volts = g_malloc(count * sizeof(*volts));
    volts2 = g_malloc(count * sizeof(*volts2));
    volts3 = g_malloc(count * sizeof(*volts3));
    volts4 = g_malloc(count * sizeof(*volts4));
    if (g_strcmp0(devc->trigger_source, "none") == 0)
        ret = eyes17_capture_four(serial, tb8, count,
            devc->gain, legacy_fw_bug, volts, volts2, volts3, volts4);
    else
        ret = eyes17_capture_four_triggered(serial, tb8, count,
            devc->gain, level, legacy_fw_bug, volts, volts2, volts3, volts4);
    if (ret != SR_OK) {
        g_free(volts);
        g_free(volts2);
        g_free(volts3);
        g_free(volts4);
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

    ach3 = g_slist_append(ach3, ch_a3);
    sr_analog_init(&analog, &encoding, &meaning, &spec,
        EYES17_ANALOG_DIGITS);
    analog.meaning->mq = SR_MQ_VOLTAGE;
    analog.meaning->unit = SR_UNIT_VOLT;
    analog.meaning->channels = ach3;
    analog.num_samples = count;
    analog.data = volts3;
    packet.type = SR_DF_ANALOG;
    packet.payload = &analog;
    sr_session_send(sdi, &packet);

    ach4 = g_slist_append(ach4, ch_mic);
    sr_analog_init(&analog, &encoding, &meaning, &spec,
        EYES17_ANALOG_DIGITS);
    analog.meaning->mq = SR_MQ_VOLTAGE;
    analog.meaning->unit = SR_UNIT_VOLT;
    analog.meaning->channels = ach4;
    analog.num_samples = count;
    analog.data = volts4;
    packet.type = SR_DF_ANALOG;
    packet.payload = &analog;
    sr_session_send(sdi, &packet);

    g_slist_free(ach1);
    g_slist_free(ach2);
    g_slist_free(ach3);
    g_slist_free(ach4);
    g_free(volts);
    g_free(volts2);
    g_free(volts3);
    g_free(volts4);

    std_session_send_df_end(sdi);

    return SR_OK;
    }
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
