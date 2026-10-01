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

/*
 * All of scan/config_set/config_list/dev_open/dev_close/
 * dev_acquisition_start/dev_acquisition_stop must be non-NULL: this tree's
 * src/backend.c sanity_check_all_drivers() aborts sr_init() for the whole
 * library otherwise. scan probes the given conn for an ExpEYES17 firmware
 * string; dev_open/dev_close are the standard serial helpers.
 * config_get/config_channel_set/config_commit are genuinely optional and
 * stay NULL.
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

	return std_scan_complete(driver, g_slist_append(NULL, sdi));
}

static int config_set(uint32_t key, GVariant *data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	(void)key;
	(void)data;
	(void)sdi;
	(void)cg;
	return SR_ERR_NA;
}

static int dev_acquisition_start(const struct sr_dev_inst *sdi)
{
	(void)sdi;
	return SR_ERR;
}

static int config_list(uint32_t key, GVariant **data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	/* No device options yet (devopts lands with acquisition config in
	 * later tasks); NULL, 0 keeps this warning-free. */
	return std_opts_config_list(key, data, sdi, cg,
		ARRAY_AND_SIZE(scanopts), ARRAY_AND_SIZE(drvopts), NULL, 0);
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
	.config_get = NULL,
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
