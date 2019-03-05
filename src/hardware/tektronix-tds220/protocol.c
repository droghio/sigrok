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
	struct sr_serial_dev_inst *serial;
	va_list args;
	char buf[256];

	serial = (struct sr_serial_dev_inst *) sdi->conn;

	va_start(args, cmd);
	vsnprintf(buf, sizeof(buf) - 3, cmd, args);
	va_end(args);
	sr_spew("Sending '%s'.", buf);
	strcat(buf, "\n");
	if (serial_write_blocking(serial, buf, strlen(buf), SERIAL_WRITE_TIMEOUT_MS) < (int)strlen(buf)) {
		sr_err("Failed to send.");
		return SR_ERR;
	}

	return SR_OK;
}

SR_PRIV int tektronix_tds220_configure_scope(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	devc = (struct dev_context *) sdi->priv;
	// TODO: Adapt per number of channels used by the device.
	char setup_commands[] = "CH1:POS 0\n"
				"CH2:POS 0\n"
				"CH1:SCA 2\n"
				"CH2:SCA 2\n"
				"SEL:CH1 ON\n"
				"SEL:CH2 ON\n"
				"HOR:SCA %5.2e\n"
				"ACQ:STOPA SEQ\n";
	return tektronix_tds220_send(sdi, setup_commands, timebase_for_samplerate(devc->cur_samplerate));
}

SR_PRIV int tektronix_tds220_start_collection(const struct sr_dev_inst *sdi)
{
	// TODO: Adapt per number of channels used by the device.
	char collection_commands[] = "ACQ:STATE RUN\n"
				     "DAT:SOU CH1\n"
				     "CURV?\n";
	return tektronix_tds220_send(sdi, collection_commands);
}

SR_PRIV int tektronix_tds220_receive_data(int fd, int revents, void *cb_data)
{
	struct sr_dev_inst *sdi;
	struct dev_context *devc;
	struct sr_serial_dev_inst *serial;
	gboolean stop = FALSE;
	gboolean have_response = FALSE;
	int len;

	(void)fd;

	if (!(sdi = (struct sr_dev_inst *) cb_data))
		return TRUE;

	if (!(devc = (struct dev_context *) sdi->priv))
		return TRUE;

	serial = (struct sr_serial_dev_inst *) sdi->conn;
	if (revents == G_IO_IN) {
		sr_spew("Receiving data.");
		/* Serial data arrived. */
		while (TEK_BUFSIZE - devc->buflen - 1 > 0) {
			len = serial_read_nonblocking(serial, devc->buf + devc->buflen, 8);
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

	if (sr_sw_limits_check(&devc->limits) || stop)
		sr_dev_acquisition_stop(sdi);
	else if (have_response)
		tektronix_tds220_recv_curve(sdi);

	return TRUE;
}

SR_PRIV uint64_t tektronix_tds220_parse_curve(char data[], float processed[], uint64_t max_length)
{
	const uint8_t BASE_10 = 10;
	const char delim[] = ",";
	uint64_t i = 0;
	for (char* tmp = strtok(data, delim); tmp != NULL && i < max_length; tmp = strtok(NULL, delim),i++)
	      processed[i] = ((float) strtol(tmp, NULL, BASE_10))/DEFAULT_VOLTAGE_SCALE_FACTOR;
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

	devc = (struct dev_context *) sdi->priv;
	i = devc->cur_channel->index;
	sr_spew("Received samples for channel %d: '%s'.", devc->cur_channel->index, devc->buf);
	samples = tektronix_tds220_parse_curve((char *) devc->buf, fvalues, SAMPLE_DEPTH);
	sr_spew("Received %d samples.", samples);
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

