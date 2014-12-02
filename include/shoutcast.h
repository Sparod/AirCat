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

/**
 * Shoutcast stream type (audio codec)
 */
enum shout_type {
	NONE_STREAM,	/*!< Unknown type */
	MPEG_STREAM,	/*!< MP3 stream */
	AAC_STREAM	/*!< AAC or AAC+ stream */
};

/**
 * Radio information extracted from HTTP header
 */
struct radio_info {
	char *description;	/*!< Radio description (icy-description) */
	char *genre;		/*!< Radio/Music genre (icy-genre) */
	char *name;		/*!< Radio name (icy-name) */
	int pub;		/*!< Public radio (icy-pub) */
	int private;		/*!< Private radio (icy-private) */
	char *url;		/*!< Radio URL (icy-url) */
	int samplerate;		/*!< Stream samplerate (ice-audio-info:
							     ice-samplerate) */
	int channels;		/*!< Stream channel number (ice-audio-info:
								ice-channels) */
	int bitrate;		/*!< Stream bitrate (icy-br || ice-audio-info:
								  ice-bitrate */
	int metaint;		/*!< Stream metadata interval (icy-metaint) */
	enum shout_type type;	/*!< Stream audio codec type */
};

struct shout_handle;

int shoutcast_open(struct shout_handle **handle, const char *url,
		   unsigned long cache_size, int use_thread);

unsigned long shoutcast_get_samplerate(struct shout_handle *h);

unsigned char shoutcast_get_channels(struct shout_handle *h);

int shoutcast_read(void *h, unsigned char *buffer, size_t size,
		   struct a_format *fmt);

const struct radio_info *shoutcast_get_info(struct shout_handle *h);

char *shoutcast_get_metadata(struct shout_handle *h);

int shoutcast_play(struct shout_handle *h);

int shoutcast_pause(struct shout_handle *h);

unsigned long shoutcast_get_pause(struct shout_handle *h);

unsigned long shoutcast_skip(struct shout_handle *h, unsigned long skip);

void shoutcast_reset(struct shout_handle *h);

int shoutcast_close(struct shout_handle *h);

/* Shoutcast event */
enum shoutcast_event {
	SHOUT_EVENT_READY,	/*!< Cache is full */
	SHOUT_EVENT_BUFFERING,	/*!< Cache is buffering */
	SHOUT_EVENT_META,	/*!< Metadata has changed: metadata string is 
				     in data as (char*) */
	SHOUT_EVENT_END		/*!< End of stream has been reached */
};
typedef void (*shoutcast_event_cb)(void *user_data, enum shoutcast_event event,
				   void *data);
int shoutcast_set_event_cb(struct shout_handle *h, shoutcast_event_cb cb,
			   void *user_data);

#endif

