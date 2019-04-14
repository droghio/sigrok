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

//
// Function signatures
//
SR_PRIV int tektronix_tds220_receive_data(int fd, int revents, void *cb_data);
SR_PRIV int tektronix_tds220_configure_scope(const struct sr_dev_inst *sdi);
SR_PRIV void tektronix_tds220_prepare_next_channel(const struct sr_dev_inst *sdi);
SR_PRIV int tektronix_tds220_start_acquisition(const struct sr_dev_inst *sdi);
SR_PRIV int tektronix_tds220_start_collection(const struct sr_dev_inst *sdi);
SR_PRIV int tektronix_tds220_send(const struct sr_dev_inst *sdi, const char *cmd, ...);
SR_PRIV void tektronix_tds220_recv_curve(const struct sr_dev_inst *sdi);
SR_PRIV uint64_t tektronix_tds220_parse_curve(char data[], float processed[], uint64_t max_length, double voltage_scale);


//
// Driver defines
//
#define LOG_PREFIX "tektronix-tds220"
#define BASE_10	10

#define DEFAULT_DATA_SOURCE	DATA_SOURCE_LIVE
#define DEFAULT_VDIV_INDEX	7
#define DEFAULT_SAMPLERATE	13
#define DEFAULT_FRAMES	1

//
// Scope parameters
//
#define MAX_CHANNELS	4
#define MAX_SAMPLE_VALUE	256
#define SAMPLE_DEPTH	2500
#define DIVS_PER_SCREEN	10
#define VOLTAGE_SCALE_FACTOR	(MAX_SAMPLE_VALUE/DIVS_PER_SCREEN)

//
// Communication defines
//
#define TEK_BUFSIZE	16384	// Three digits and 1 Comma for 2500 Samples
#define TIMEOUT_MS	500

//
// Command templates
//

#define ACQ_SETUP			"ACQ:STOPA SEQ"
#define ACQ_COMMAND			"ACQ:STATE RUN"

#define CHANNEL_COLLECT_TEMPLATE	"DAT:SOU CH%d"
#define CHANNEL_COLLECT_COMMAND		"CURV?"

#define CHANNEL_CONFIGURE_TEMPLATE0	"CH%d:POS 0"
#define CHANNEL_CONFIGURE_TEMPLATE1	"CH%d:SCA %3.1e"
#define CHANNEL_CONFIGURE_TEMPLATE2	"SEL:CH%d ON"
#define CHANNEL_CONFIGURE_TEMPLATE3	"HOR:SCA %5.2e"

//
// Utility functions
//
static inline double timebase_for_samplerate(uint64_t sample_rate)
{
	return SAMPLE_DEPTH/((double) sample_rate*DIVS_PER_SCREEN);
}


//
// Data types and enumerations
//
static const uint64_t volts_per_div[][2] = {
	/* millivolts */
	{ 10, 1000 },
	{ 20, 1000 },
	{ 50, 1000 },
	{ 100, 1000 },
	{ 200, 1000 },
	{ 500, 1000 },
	/* volts */
	{ 1, 1 },
	{ 2, 1 },
	{ 5, 1 },
	{ 10, 1 },
	{ 20, 1 },
	{ 50, 1 },
};

// Data sources
enum {
	DATA_SOURCE_LIVE,
};

// Supported models
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

// Supported device profiles
struct tektronix_tds220_profile {
	int model;
	const char *modelname;
	int nb_channels;
};

// Driver runtime profile
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
	int cur_volts_per_div_index[MAX_CHANNELS];
};

#endif
