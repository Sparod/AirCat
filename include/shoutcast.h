/*
 * shoutcast.h - A ShoutCast Client
 *
 * Copyright (c) 2014   A. Dilly
 *
 * AirCat is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation.
 *
 * AirCat is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with AirCat.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef _SHOUTCAST_CLIENT_H
#define _SHOUTCAST_CLIENT_H

#include "format.h"

enum {NONE_STREAM, MPEG_STREAM, AAC_STREAM};

struct radio_info {
	int bitrate;		//icy-br: || ice-audio-info: ice-bitrate=
	char *description;	//icy-description:
	char *genre;		//icy-genre:
	char *name;		//icy-name:
	int pub;		//icy-pub:
	int private;		//icy-private;
	char *url;		//icy-url:
	int samplerate;		//ice-audio-info: ice-samplerate=
	int channels;		//ice-audio-info: ice-channels=
	int metaint;		//icy-metaint:
	int type;		// Audio Codec (MPEG_STREAM or AAC_STREAM)
};

struct shout_handle;

int shoutcast_open(struct shout_handle **h, const char *url);

unsigned long shoutcast_get_samplerate(struct shout_handle *h);

unsigned char shoutcast_get_channels(struct shout_handle *h);

int shoutcast_read(void *h, unsigned char *buffer, size_t size,
		   struct a_format *fmt);

const struct radio_info *shoutcast_get_info(struct shout_handle *h);

char *shoutcast_get_metadata(struct shout_handle *h);

int shoutcast_close(struct shout_handle *h);

#endif

