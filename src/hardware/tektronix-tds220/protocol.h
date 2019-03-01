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
SR_PRIV int tek_configure_scope(const struct sr_dev_inst *sdi);
SR_PRIV int tek_start_collection(const struct sr_dev_inst *sdi);
SR_PRIV int tek_send(const struct sr_dev_inst *sdi, const char *cmd, ...);
SR_PRIV void tek_recv_curve(const struct sr_dev_inst *sdi);

#define MAX_CHANNELS 4
#define SAMPLE_DEPTH 2500
// Three digits and 1 Comma for 2500 Samples
#define TEK_BUFSIZE 16384 
#define DIVS_PER_SCREEN 10

// We can go to 19200 but it is too easy to overflow the input buffer.
#define SERIALCOMM "9600/8n1"

/* Always USB-serial, 1ms is plenty. */
#define SERIAL_WRITE_TIMEOUT_MS 1
#define SERIAL_READ_TIMEOUT_MS 500

#define DEFAULT_DATA_SOURCE	DATA_SOURCE_LIVE

enum {
	DATA_SOURCE_LIVE,
};


static double timebase_for_samplerate(uint64_t sample_rate)
{
        return (SAMPLE_DEPTH*DIVS_PER_SCREEN)/((double) sample_rate);
}

/* Supported models */
enum {
	TEK_TDS220 = 1,
	TEK_TDS1000,
	TEK_TDS2000,
// In theory a lot more too.
};

/* Supported device profiles */
struct agdmm_profile {
	int model;
	const char *modelname;
	int nb_channels;
};

struct dev_context {
	const struct agdmm_profile *profile;
	struct sr_sw_limits limits;
	int data_source;

	unsigned char buf[TEK_BUFSIZE];
	int buflen;
	uint64_t cur_samplerate;
	struct sr_channel *cur_channel;
	struct sr_channel *cur_conf;
	int cur_sample;
	int cur_mq[MAX_CHANNELS];
	int cur_unit[MAX_CHANNELS];
	int cur_mqflags[MAX_CHANNELS];
	int cur_digits[MAX_CHANNELS];
	int cur_encoding[MAX_CHANNELS];
	int cur_exponent[MAX_CHANNELS];
	int mode_continuity;
};

#endif
