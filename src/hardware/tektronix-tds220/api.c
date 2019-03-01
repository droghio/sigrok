/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2019 droghio <admin@jldapps.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <config.h>
#include <string.h>
#include "protocol.h"

static const uint32_t scanopts[] = {
	SR_CONF_CONN,
	SR_CONF_SERIALCOMM,
};

static const uint32_t drvopts[] = {
	SR_CONF_OSCILLOSCOPE,
};

static const uint32_t devopts[] = {
	SR_CONF_CONTINUOUS,
	SR_CONF_LIMIT_SAMPLES | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_LIMIT_MSEC | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_SAMPLERATE | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_DATA_SOURCE | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
};

static const uint64_t samplerates[] = {
	SR_HZ(50),
	SR_HZ(100),
	SR_HZ(250),
	SR_HZ(500),
	SR_HZ(1000),
	SR_HZ(2500),
	SR_KHZ(5),
	SR_KHZ(10),
	SR_KHZ(25),
	SR_KHZ(50),
	SR_KHZ(100),
	SR_KHZ(250),
	SR_KHZ(500),
	SR_KHZ(1000),
	SR_KHZ(2500),
	SR_MHZ(5),
	SR_MHZ(10),
	SR_MHZ(25),
	SR_MHZ(50),
	SR_MHZ(100),
	SR_MHZ(250),
};

static const char *data_sources[] = {
	"LIVE"
};

static const struct agdmm_profile supported_agdmm[] = {
	{ TEK_TDS220, "TDS 220", 2 },
	{ TEK_TDS2000, "TDS 2000", 4 },
	ALL_ZERO
};

static GSList *scan(struct sr_dev_driver *di, GSList *options)
{
	struct sr_dev_inst *sdi;
	struct dev_context *devc;
	struct sr_config *src;
	struct sr_serial_dev_inst *serial;
	GSList *l, *devices;
	int len, i;
	const char *conn, *serialcomm;
	char *buf, **tokens;

	devices = NULL;
	conn = serialcomm = NULL;
	for (l = options; l; l = l->next) {
		src = ((struct sr_config *) l->data);
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
	if (!serialcomm)
		serialcomm = SERIALCOMM;

	serial = sr_serial_dev_inst_new(conn, serialcomm);

	if (serial_open(serial, SERIAL_RDWR) != SR_OK)
		return NULL;

	serial_flush(serial);
        serial_drain(serial);

	len = 128;
	buf = (char *) g_malloc(len);

        // Clear out old messages if any are present.
        while (serial_read_blocking(serial, buf, len, SERIAL_READ_TIMEOUT_MS));
	if (serial_write_blocking(serial, "*IDN?\r\n", 7, SERIAL_WRITE_TIMEOUT_MS) < 7) {
		sr_err("Unable to send identification string.");
		return NULL;
	}

	serial_readline(serial, &buf, &len, 250);
	if (!len)
		return NULL;

        sr_spew("Scanning for scopes.");
	tokens = g_strsplit(buf, ",", 4);
	if (!strcmp("TEKTRONIX", tokens[0])
			&& tokens[1] && tokens[2] && tokens[3]) {
		for (i = 0; supported_agdmm[i].model; i++) {
			if (strcmp(supported_agdmm[i].modelname, tokens[1]))
				continue;
                        sr_spew("Scope found '%s'.", buf);
			sdi = (struct sr_dev_inst *) g_malloc0(sizeof(struct sr_dev_inst));
			sdi->status = SR_ST_INACTIVE;
			sdi->vendor = g_strdup(tokens[0]);
			sdi->model = g_strdup(tokens[1]);
			sdi->version = g_strdup(tokens[3]);
			devc = (struct dev_context *) g_malloc0(sizeof(struct dev_context));
			sr_sw_limits_init(&devc->limits);
			devc->profile = &supported_agdmm[i];
			devc->data_source = DEFAULT_DATA_SOURCE;
			devc->cur_samplerate = 5;
			if (supported_agdmm[i].nb_channels > 1) {
				int temp_chan = supported_agdmm[i].nb_channels - 1;
				devc->cur_mq[temp_chan] = SR_MQ_TEMPERATURE;
				devc->cur_unit[temp_chan] = SR_UNIT_CELSIUS;
				devc->cur_digits[temp_chan] = 1;
				devc->cur_encoding[temp_chan] = 2;
			}
			sdi->inst_type = SR_INST_SERIAL;
			sdi->conn = serial;
			sdi->priv = devc;
			sr_channel_new(sdi, 0, SR_CHANNEL_ANALOG, TRUE, "CH1");
			if (supported_agdmm[i].nb_channels > 1)
				sr_channel_new(sdi, 1, SR_CHANNEL_ANALOG, TRUE, "CH2");
			if (supported_agdmm[i].nb_channels > 2)
				sr_channel_new(sdi, 2, SR_CHANNEL_ANALOG, TRUE, "CH3");
			if (supported_agdmm[i].nb_channels > 3)
				sr_channel_new(sdi, 3, SR_CHANNEL_ANALOG, TRUE, "CH4");
			devices = g_slist_append(devices, sdi);
			break;
		}
	}
	g_strfreev(tokens);
	g_free(buf);

	serial_close(serial);
	if (!devices)
		sr_serial_dev_inst_free(serial);

	return std_scan_complete(di, devices);
}

static int config_get(uint32_t key, GVariant **data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	struct dev_context *devc;

	(void)cg;

	devc = (struct dev_context *) sdi->priv;

	switch (key) {
	case SR_CONF_SAMPLERATE:
		*data = g_variant_new_uint64(devc->cur_samplerate);
		break;
	case SR_CONF_LIMIT_SAMPLES:
	case SR_CONF_LIMIT_MSEC:
		return sr_sw_limits_config_get(&devc->limits, key, data);
	case SR_CONF_DATA_SOURCE:
		*data = g_variant_new_string(data_sources[devc->data_source]);
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
	uint64_t samplerate;
	int idx;

	(void)cg;

	devc = (struct dev_context *) sdi->priv;

	switch (key) {
	case SR_CONF_SAMPLERATE:
		samplerate = g_variant_get_uint64(data);
		if (samplerate < samplerates[0] || samplerate > samplerates[sizeof(samplerates)/sizeof(samplerates[0])-1])
			return SR_ERR_ARG;
		devc->cur_samplerate = g_variant_get_uint64(data);
		break;
	case SR_CONF_LIMIT_SAMPLES:
	case SR_CONF_LIMIT_MSEC:
		return sr_sw_limits_config_set(&devc->limits, key, data);
	case SR_CONF_DATA_SOURCE:
		if ((idx = std_str_idx(data, ARRAY_AND_SIZE(data_sources))) < 0)
			return SR_ERR_ARG;
		devc->data_source = idx;
		break;
	default:
		return SR_ERR_NA;
	}

	return SR_OK;
}

static int config_list(uint32_t key, GVariant **data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	int ret;

	(void)sdi;
	(void)data;
	(void)cg;

	ret = SR_OK;
	switch (key) {
        case SR_CONF_SCAN_OPTIONS:
        case SR_CONF_DEVICE_OPTIONS:
                return STD_CONFIG_LIST(key, data, sdi, cg, scanopts, drvopts, devopts);
	default:
		return SR_ERR_NA;
	}

	return ret;
}

static int dev_acquisition_start(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc = (struct dev_context *) sdi->priv;
	struct sr_serial_dev_inst *serial;

	devc->cur_channel = sr_next_enabled_channel(sdi, NULL);
	devc->cur_conf = sr_next_enabled_channel(sdi, NULL);
	devc->cur_sample = 1;
	devc->cur_mq[0] = -1;
	if (devc->profile->nb_channels > 2)
		devc->cur_mq[1] = -1;

	if (devc->data_source == DATA_SOURCE_LIVE) {
	} else {
		sr_err("Data source is not implemented for this model.");
		return SR_ERR_NA;
	}

	sr_sw_limits_acquisition_start(&devc->limits);
	std_session_send_df_header(sdi);

	serial = (struct sr_serial_dev_inst *) sdi->conn;
	serial_source_add(sdi->session, serial, G_IO_IN, 10,
			tektronix_tds220_receive_data, (void *) sdi);

        tek_configure_scope(sdi);
        tek_start_collection(sdi);
	return SR_OK;
}

static struct sr_dev_driver tektronix_tds220_driver_info = {
	.name = "tektronix-tds220",
	.longname = "Tektronix TDS220",
	.api_version = 1,
	.init = std_init,
	.cleanup = std_cleanup,
	.scan = scan,
	.dev_list = std_dev_list,
	.dev_clear = std_dev_clear,
	.config_get = config_get,
	.config_set = config_set,
	.config_list = config_list,
	.dev_open = std_serial_dev_open,
	.dev_close = std_serial_dev_close,
	.dev_acquisition_start = dev_acquisition_start,
	.dev_acquisition_stop = std_serial_dev_acquisition_stop,
	.context = NULL,
};
SR_REGISTER_DEV_DRIVER(tektronix_tds220_driver_info);
