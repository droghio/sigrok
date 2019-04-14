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
#include "protocol.h"
#include "scpi.h"

static gboolean receive_curve_packet(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	gboolean stop = FALSE;

	devc = (struct dev_context *) sdi->priv;

	/* Strip CRLF */
	while (devc->buflen) {
		if (*(devc->buf + devc->buflen - 1) == '\r'
				|| *(devc->buf + devc->buflen - 1) == '\n')
			*(devc->buf + --devc->buflen) = '\0';
		else
			break;
	}
	sr_spew("Received '%s'.", devc->buf);
	return stop;
}

SR_PRIV int tektronix_tds220_send(const struct sr_dev_inst *sdi, const char *cmd, ...)
{
	struct sr_scpi_dev_inst *scpi;
	va_list args;
	char buf[256];

	scpi = (struct sr_scpi_dev_inst *) sdi->conn;

	sr_spew("Checking if scope is busy...");
	if (sr_scpi_get_opc(scpi) != SR_OK){
		sr_err("Unable to get opc status from scope.");
		return SR_ERR;
	}

	va_start(args, cmd);
	vsnprintf(buf, sizeof(buf) - 3, cmd, args);
	va_end(args);
	sr_spew("Sending '%s'.", buf);
	strcat(buf, "\n");
	if (sr_scpi_send(scpi, buf) != SR_OK) {
		sr_err("Failed to send.");
		return SR_ERR;
	}

	return SR_OK;
}

SR_PRIV int tektronix_tds220_configure_scope(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	gboolean return_value, inner_success;
	devc = (struct dev_context *) sdi->priv;
	const uint64_t *vscale_raw;
	float vscale;

	vscale_raw = volts_per_div[devc->cur_volts_per_div_index[0]];
	vscale = vscale_raw[0]/((float) vscale_raw[1]);
	return_value = SR_OK;
	// First channel on the scope is 1 not 0.
	for (int i = 1; i < devc->profile->nb_channels+1; i++){
		inner_success = tektronix_tds220_send(sdi, CHANNEL_CONFIGURE_TEMPLATE0, i);
		if (inner_success != SR_OK)
			sr_err("Received error %d when configuring 0 scope channel CH%d", inner_success, i+1);

		inner_success = tektronix_tds220_send(sdi, CHANNEL_CONFIGURE_TEMPLATE1, i, vscale);
		if (inner_success != SR_OK)
			sr_err("Received error %d when configuring 1 scope channel CH%d", inner_success, i+1);

		inner_success = tektronix_tds220_send(sdi, CHANNEL_CONFIGURE_TEMPLATE2, i);
		if (inner_success != SR_OK)
			sr_err("Received error %d when configuring 2 scope channel CH%d", inner_success, i+1);

		inner_success |= tektronix_tds220_send(sdi, CHANNEL_CONFIGURE_TEMPLATE3, timebase_for_samplerate(devc->cur_samplerate));
		if (inner_success != SR_OK)
			sr_err("Received error %d when configuring 3 scope channel CH%d", inner_success, i+1);

		return_value |= inner_success;
	}
	sr_info("config_inner: %d", return_value);
	return return_value == SR_OK ? SR_OK : SR_ERR;
}

SR_PRIV int tektronix_tds220_start_acquisition(const struct sr_dev_inst *sdi)
{
	int ret;
	ret = tektronix_tds220_send(sdi, ACQ_SETUP);
	if (ret != SR_OK)
		sr_err("Received error when setting up acquisition.");

	ret |= tektronix_tds220_send(sdi, ACQ_COMMAND);
	if (ret != SR_OK)
		sr_err("Received error when sending acquisition command.");

	return ret;
}

SR_PRIV int tektronix_tds220_start_collection(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	int ret;

	devc = (struct dev_context *) sdi->priv;
	devc->limits.samples_read = 0;
	// First channel on the scope is 1 not 0.
	sr_spew("Beginning collection on channel %d of %d", devc->cur_channel->index+1, devc->profile->nb_channels);
	ret = tektronix_tds220_send(sdi, CHANNEL_COLLECT_TEMPLATE, devc->cur_channel->index+1);
	if (ret != SR_OK)
		sr_err("Received error when selecting channel for data collection.");

	ret |= tektronix_tds220_send(sdi, CHANNEL_COLLECT_COMMAND, devc->cur_channel->index+1);
	if (ret != SR_OK)
		sr_err("Received error when collecting data.");

	return ret;
}

SR_PRIV void tektronix_tds220_prepare_next_channel(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;

	devc = (struct dev_context *) sdi->priv;
	devc->buflen = 0;
	devc->cur_channel = sr_next_enabled_channel(sdi, devc->cur_channel);
	devc->cur_mq[devc->cur_channel->index] = SR_MQ_VOLTAGE;
	devc->cur_sample = 1;
}

SR_PRIV int tektronix_tds220_receive_data(int fd, int revents, void *cb_data)
{
	struct sr_dev_inst *sdi;
	struct dev_context *devc;
	struct sr_scpi_dev_inst *scpi;
	gboolean stop = FALSE;
	gboolean have_response = FALSE;
	int len;

	(void)fd;

	sr_spew("In receive data.");
	if (!(sdi = (struct sr_dev_inst *) cb_data))
		return TRUE;

	if (!(devc = (struct dev_context *) sdi->priv))
		return TRUE;

	scpi = (struct sr_scpi_dev_inst *) sdi->conn;
	if (revents == G_IO_IN || revents == 0) {
		sr_spew("Receiving data.");
		// Serial data arrived.
		while (TEK_BUFSIZE - devc->buflen - 1 > 0) {
			len = sr_scpi_read_data(scpi, ((char *) devc->buf + devc->buflen), 8);
			sr_spew("Received bytes: %d", len);
			if (len < 1)
				break;
			devc->buflen += len;
			*(devc->buf + devc->buflen) = '\0';
			if (*(devc->buf + devc->buflen - 1) == ',' || *(devc->buf + devc->buflen - 1) == '\n') {
				have_response = TRUE;
				stop = receive_curve_packet(sdi);
				break;
			}
		}
	}

	if (sr_sw_limits_check(&devc->limits) || stop){
		if (devc->cur_channel->index < devc->profile->nb_channels-1){
			// Chain download the next channel's data.
			sr_spew("Chaining to next channel.");
			tektronix_tds220_prepare_next_channel(sdi);
			tektronix_tds220_start_collection(sdi);
		} else {
			sr_dev_acquisition_stop(sdi);
		}
	}
	else if (have_response){
		tektronix_tds220_recv_curve(sdi);
	}

	return TRUE;
}

SR_PRIV uint64_t tektronix_tds220_parse_curve(char data[], float processed[], uint64_t max_length, double voltage_scale)
{
	const char delim[] = ",";
	uint64_t i = 0;
	for (char* tmp = strtok(data, delim); tmp != NULL && i < max_length; tmp = strtok(NULL, delim),i++)
		processed[i] = ((float) strtol(tmp, NULL, BASE_10))/voltage_scale;
	// Last hidden increment makes i the number of samples recorded.
	return i;
}

/**
 * Receive curve data from scope.
 */
SR_PRIV void tektronix_tds220_recv_curve(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	struct sr_datafeed_packet packet;
	struct sr_datafeed_analog analog;
	struct sr_analog_encoding encoding;
	struct sr_analog_meaning meaning;
	struct sr_analog_spec spec;
	float fvalues[SAMPLE_DEPTH];
	int i;
	int samples;
	const uint64_t *vscale_raw;
	float vscale;

	devc = (struct dev_context *) sdi->priv;
	i = devc->cur_channel->index;
	vscale_raw = volts_per_div[devc->cur_volts_per_div_index[i]];
	vscale = vscale_raw[0]/((float) vscale_raw[1]);

	sr_spew("Received samples for channel %d of %d: '%s'.", devc->cur_channel->index+1, devc->profile->nb_channels, devc->buf);
	samples = tektronix_tds220_parse_curve((char *) devc->buf, fvalues, SAMPLE_DEPTH, VOLTAGE_SCALE_FACTOR/vscale);
	sr_spew("Received %d samples with a voltage scale of %f.", samples, vscale*VOLTAGE_SCALE_FACTOR);
	devc->buflen = 0;

	sr_analog_init(&analog, &encoding, &meaning, &spec,
			devc->cur_digits[i] - devc->cur_exponent[i]);
	analog.meaning->mq = devc->cur_mq[i];
	analog.meaning->unit = devc->cur_unit[i];
	analog.meaning->mqflags = devc->cur_mqflags[i];
	analog.meaning->channels = g_slist_append(NULL, devc->cur_channel);
	analog.num_samples = samples;
	analog.data = fvalues;
	encoding.digits = devc->cur_encoding[i] - devc->cur_exponent[i];
	packet.type = SR_DF_ANALOG;
	packet.payload = &analog;
	sr_session_send(sdi, &packet);
	g_slist_free(analog.meaning->channels);

	sr_sw_limits_update_samples_read(&devc->limits, samples);
}

