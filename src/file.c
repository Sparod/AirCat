/*
 * file.c - A file input
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <pthread.h>

#include "decoder.h"
#include "demux.h"
#include "file.h"

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

struct file_handle {
	/* Demuxer */
	struct demux_handle *demux;
	/* Audio decoder */
	struct decoder_handle *dec;
	/* Stream status */
	uint64_t pcm_pos;
	unsigned long pcm_pos_off;
	unsigned long pcm_remaining;
	/* File properties */
	unsigned long samplerate;
	unsigned long channels;
	unsigned int bitrate;
	unsigned long length;
	/* Event callback */
	file_event_cb event_cb;
	void *event_udata;
	int buffering;
	int end;
	/* Mutex for thread safe status */
	pthread_mutex_t mutex;
};

int file_open(struct file_handle **handle, const char *uri)
{
	struct file_handle *h;
	struct meta *meta;
	const unsigned char *dec_config;
	unsigned long dec_config_size;
	unsigned long dec_samplerate;
	unsigned char dec_channels;
	unsigned long samplerate;
	unsigned char channels;
	int use_thread = 0;
	int codec = -1;

	/* Alloc structure */
	*handle = malloc(sizeof(struct file_handle));
	if(*handle == NULL)
		return -1;
	h = *handle;

	/* Init structure */
	h->demux = NULL;
	h->dec = NULL;
	h->pcm_pos = 0;
	h->pcm_pos_off = 0;
	h->pcm_remaining = 0;
	h->event_cb = NULL;
	h->event_udata = NULL;
	h->buffering = 0;
	h->end = 0;

	/* Init mutex */
	pthread_mutex_init(&h->mutex, NULL);

	/* Check URI: use a thread cache if not a local file system */
	if(strstr(uri, "://") != NULL)
		use_thread = 1;

	/* Open demuxer */
	if(demux_open(&h->demux, uri, &samplerate, &channels, 8192*2,
		      use_thread) != 0)
		return -1;

	/* Get decoder configuration from demuxer (useful for MP4) */
	demux_get_dec_config(h->demux, &codec, &dec_config, &dec_config_size);

	/* Open decoder */
	if(decoder_open(&h->dec, codec, dec_config, dec_config_size,
			&dec_samplerate, &dec_channels) != 0)
		return -1;

	/* Get file properties */
	meta = demux_get_meta(h->demux);
	h->samplerate = meta->samplerate;
	h->channels = meta->channels;
	h->length = meta->length;
	h->bitrate = meta->bitrate * 1000;

	/* Samplerate fix: bad samplerate and/or channels in mp4 header */
	if(meta->type == FILE_FORMAT_AAC &&
	   ((dec_samplerate != 0 && dec_samplerate != h->samplerate) ||
	   (dec_channels != 0 && dec_channels != h->channels)))
	{
		h->samplerate = dec_samplerate;
		h->channels = dec_channels;
	}

	return 0;
}

unsigned long file_get_samplerate(struct file_handle *h)
{
	if(h == NULL)
		return 0;

	return h->samplerate;
}

unsigned char file_get_channels(struct file_handle *h)
{
	if(h == NULL)
		return 0;

	return h->channels;
}

unsigned long file_set_pos(struct file_handle *h, unsigned long pos)
{
	if(h == NULL)
		return -1;

	/* Lock stream access */
	pthread_mutex_lock(&h->mutex);

	/* Set output position */
	pos = demux_set_pos(h->demux, pos);
	h->pcm_pos = 0;
	h->pcm_pos_off = pos * 1000;
	h->pcm_remaining = 0;

	/* Notify new position */
	if(h->event_cb != NULL)
		h->event_cb(h->event_udata, FILE_EVENT_SEEK, &pos);

	/* Unlock stream access */
	pthread_mutex_unlock(&h->mutex);

	return pos;
}

unsigned long file_get_pos(struct file_handle *h)
{
	uint64_t pos;

	if(h == NULL)
		return -1;

	/* Lock stream access */
	pthread_mutex_lock(&h->mutex);

	/* Get output position */
	pos = h->pcm_pos / h->samplerate / h->channels;
	pos += h->pcm_pos_off / 1000;

	/* Unlock stream access */
	pthread_mutex_unlock(&h->mutex);

	return (unsigned long) pos;
}

long file_get_length(struct file_handle *h)
{
	if(h == NULL)
		return -1;

	return h->length;
}

int file_get_status(struct file_handle *h)
{
	if(h == NULL)
		return FILE_NULL;

	if(h->demux == NULL)
		return FILE_CLOSED;

	if(h->end)
		return FILE_EOF;

	return FILE_OPENED;
}

int file_read(void *user_data, unsigned char *buffer, size_t size,
	      struct a_format *fmt)
{
	struct file_handle *h = (struct file_handle *) user_data;
	struct decoder_info info;
	unsigned char *frame = NULL;
	int total_samples = 0;
	ssize_t len = 0;
	int samples;

	if(h == NULL)
		return -1;

	/* Lock stream access */
	pthread_mutex_lock(&h->mutex);

	/* Process remaining pcm data */
	if(h->pcm_remaining > 0)
	{
		/* Get remaining pcm data from decoder */
		samples = decoder_decode(h->dec, NULL, 0, buffer, size, &info);
		if(samples < 0)
		{
		
			/* Unlock stream access */
			pthread_mutex_unlock(&h->mutex);

			return -1;
		}

		/* Update audio format */
		if(info.samplerate != h->samplerate ||
		   info.channels != h->channels)
		{
			h->pcm_pos_off = h->pcm_pos * 1000 / h->samplerate /
					 h->channels;
			h->pcm_pos = 0;
			h->samplerate = info.samplerate;
			h->channels = info.channels;
		}

		h->pcm_remaining -= samples;
		total_samples += samples;
	}

	/* Fill output buffer */
	while(total_samples < size)
	{
		/* Get frame */
		len = demux_get_frame(h->demux, &frame);
		if(len <= 0)
		{
			/* File is buffering */
			if(len == 0)
			{
				/* Notify demux is buffering */
				if(h->event_cb != NULL && h->buffering == 0)
					h->event_cb(h->event_udata,
						    FILE_EVENT_BUFFERING, NULL);
				h->buffering = 1;
			}
			break;
		}
		else if(h->buffering == 1)
		{
			/* Notify demux cache is ready */
			if(h->event_cb != NULL)
				h->event_cb(h->event_udata,
					    FILE_EVENT_READY, NULL);
			h->buffering = 0;
		}

		/* Decode next frame */
		samples = decoder_decode(h->dec, frame,
					 len > 0 ? len : 0,
					 &buffer[total_samples * 4],
					 size - total_samples, &info);
		if(samples <= 0)
		{
			/* Set used bytes in frame */
			if(len > 0 && info.used > 0)
				demux_set_used_frame(h->demux, info.used);
			break;
		}

		/* Update used data in frame */
		demux_set_used_frame(h->demux, info.used);

		/* Update remaining counter */
		h->pcm_remaining = info.remaining;

		/* Check audio format */
		if(info.samplerate != h->samplerate ||
		   info.channels != h->channels)
		{
			/* Reset internal position */
			decoder_decode(h->dec, NULL, 0, NULL, 0, NULL);
			h->pcm_remaining += samples;
			break;
		}

		/* Update samples returned */
		total_samples += samples;
	}

	h->pcm_pos += total_samples;

	/* Unlock stream access */
	pthread_mutex_unlock(&h->mutex);

	/* End of stream */
	if(len < 0 && total_samples == 0)
	{
		/* Lock stream access */
		pthread_mutex_lock(&h->mutex);

		/* Notify end of stream */
		if(h->event_cb != NULL && h->end == 0)
			h->event_cb(h->event_udata, FILE_EVENT_END, NULL);
		h->end = 1;

		/* Unlock stream access */
		pthread_mutex_unlock(&h->mutex);

		return -1;
	}

	/* Fill audio format */
	if(fmt != NULL)
	{
		fmt->samplerate = h->samplerate;
		fmt->channels = h->channels;
	}

	return total_samples;
}

int file_set_event_cb(struct file_handle *h, file_event_cb cb, void *user_data)
{
	/* Lock stream access */
	pthread_mutex_lock(&h->mutex);

	/* Set event callback */
	h->event_cb = cb;
	h->event_udata = user_data;

	/* Unlock stream access */
	pthread_mutex_unlock(&h->mutex);

	return 0;
}

void file_close(struct file_handle *h)
{
	if(h == NULL)
		return;

	/* Close decoder */
	if(h->dec != NULL)
		decoder_close(h->dec);

	/* Close demuxer */
	if(h->demux != NULL)
		demux_close(h->demux);

	/* Free handle */
	free(h);
}
