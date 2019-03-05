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

#ifndef LIBSIGROK_HARDWARE_TEKTRONIX_TDS220_PROTOCOL_H
#define LIBSIGROK_HARDWARE_TEKTRONIX_TDS220_PROTOCOL_H

#include <stdint.h>
#include <glib.h>
#include <libsigrok/libsigrok.h>
#include "libsigrok-internal.h"

#define LOG_PREFIX "tektronix-tds220"

SR_PRIV int tektronix_tds220_receive_data(int fd, int revents, void *cb_data);
SR_PRIV int tektronix_tds220_configure_scope(const struct sr_dev_inst *sdi);
SR_PRIV void tektronix_tds220_prepare_next_channel(const struct sr_dev_inst *sdi);
SR_PRIV int tektronix_tds220_start_acquisition(const struct sr_dev_inst *sdi);
SR_PRIV int tektronix_tds220_start_collection(const struct sr_dev_inst *sdi);
SR_PRIV int tektronix_tds220_send(const struct sr_dev_inst *sdi, const char *cmd, ...);
SR_PRIV void tektronix_tds220_recv_curve(const struct sr_dev_inst *sdi);
SR_PRIV uint64_t tektronix_tds220_parse_curve(char data[], float processed[], uint64_t max_length);


#define TEK_BUFSIZE 16384	// Three digits and 1 Comma for 2500 Samples
#define DEFAULT_DATA_SOURCE	DATA_SOURCE_LIVE
#define MAX_CHANNELS 4
#define SAMPLE_DEPTH 2500
#define DIVS_PER_SCREEN 10
#define DEFAULT_VOLTAGE_SCALE_FACTOR 12.8	// 256 bit depth / 10 DIVS / 2 V default scale

#define SERIAL_WRITE_TIMEOUT_MS 1
#define SERIAL_READ_TIMEOUT_MS 500
#define SERIALCOMM "9600/8n1"	// We can go to 19200 but it is too easy to overflow
				// scope's the input buffer.

#define ACQ_COMMAND			"ACQ:STATE RUN\n"
#define CHANNEL_COLLECT_TEMPLATE	"DAT:SOU CH%d\n"\
					"CURV?\n"

static inline double timebase_for_samplerate(uint64_t sample_rate)
{
	return SAMPLE_DEPTH/((double) sample_rate*DIVS_PER_SCREEN);
}

/* Data sources */
enum {
	DATA_SOURCE_LIVE,
};

/* Supported models */
enum {
	TEK_TDS210 = 10000, 
	TEK_TDS220, 
	TEK_TDS224, 
	TEK_TDS1002,
	TEK_TDS1012,
	TEK_TDS2001,
	TEK_TDS2002,
	TEK_TDS2012,
	TEK_TDS2022,
	TEK_TDS2004,
	TEK_TDS2014,
	TEK_TDS2024
};

/* Supported device profiles */
struct tektronix_tds220_profile {
	int model;
	const char *modelname;
	int nb_channels;
};

struct dev_context {
	const struct tektronix_tds220_profile *profile;
	struct sr_sw_limits limits;
	int data_source;

	unsigned char buf[TEK_BUFSIZE];
	int buflen;
	uint64_t cur_samplerate;
	struct sr_channel *cur_channel;
	int cur_sample;
	enum sr_mq cur_mq[MAX_CHANNELS];
	enum sr_unit cur_unit[MAX_CHANNELS];
	enum sr_mqflag cur_mqflags[MAX_CHANNELS];
	int cur_digits[MAX_CHANNELS];
	int cur_encoding[MAX_CHANNELS];
	int cur_exponent[MAX_CHANNELS];
};

#endif
