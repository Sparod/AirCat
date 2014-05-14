/*
 * shoutcast.c - A ShoutCast Client
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
#include <unistd.h>

#include "http.h"
#include "decoder.h"
#include "shoutcast.h"

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#define BUFFER_SIZE 8192
#define SHOUT_TIMEOUT 10
#define SHOUT_SYNC_TIMEOUT 1000

struct shout_handle {
	/* HTTP Client */
	struct http_handle *http;
	/* Metadata handling */
	unsigned int metaint;
	unsigned int remaining;
	int meta_len;
	char meta_buffer[16*255];
	/* Radio info */
	struct radio_info info;
	/* Decoder and stream properties */
	struct decoder_handle *dec;
	unsigned long samplerate;
	unsigned char channels;
	/* Input buffer and PCM output cache */
	unsigned char in_buffer[BUFFER_SIZE];
	unsigned long in_len;
	unsigned long pcm_remaining;
};

static int shoutcast_read_stream(struct shout_handle *h);
static int shoutcast_sync_mp3_stream(struct shout_handle *h);
static int shoutcast_sync_aac_stream(struct shout_handle *h);

int shoutcast_open(struct shout_handle **handle, const char *url)
{
	struct shout_handle *h;
	int code = 0;
	int type;
	char *p;
	int i;

	/* Alloc structure */
	*handle = malloc(sizeof(struct shout_handle));
	if(*handle == NULL)
		return -1;
	h = *handle;

	/* Init structure */
	h->http = NULL;
	h->metaint = 0;
	h->remaining = 0;
	h->meta_len = 0;
	h->dec = NULL;
	h->in_len = 0;
	h->pcm_remaining = 0;

	/* Set to zero radio_info structure */
	memset((unsigned char*)&h->info, 0, sizeof(struct radio_info));

	/* Init http client */
	if(http_open(&h->http) != 0)
	{
		shoutcast_close(h);
		return -1;
	}

	/* Set options */
	http_set_option(h->http, HTTP_USER_AGENT, "Aircat 1.0");
	http_set_option(h->http, HTTP_EXTRA_HEADER, "Icy-MetaData: 1\r\n");
	http_set_option(h->http, HTTP_FOLLOW_REDIRECT, "yes");

	/* Connect and get header from server */
	code = http_get(h->http, url);
	if(code != 200)
		return -1;

	/* Fill info radio structure */
	h->info.description = http_get_header(h->http, "icy-description", 0);
	h->info.genre = http_get_header(h->http, "icy-genre", 0);
	h->info.name = http_get_header(h->http, "icy-name", 0);
	h->info.url = http_get_header(h->http, "icy-url", 0);
	p = http_get_header(h->http, "icy-br", 0);
	if(p != NULL)
		h->info.bitrate = atoi(p);
	p = http_get_header(h->http, "icy-pub", 0);
	if(p != NULL)
		h->info.pub = atoi(p);
	p = http_get_header(h->http, "icy-private", 0);
	if(p != NULL)
		h->info.private = atoi(p);
	p = http_get_header(h->http, "icy-metaint", 0);
	if(p != NULL)
		h->info.metaint = atoi(p);
	/* TODO: ice-audio-info: */
	p = http_get_header(h->http, "content-type", 0);
	if(p != NULL)
	{
		if(strncmp(p, "audio/mpeg", 10) == 0)
			h->info.type = MPEG_STREAM;
		else if(strncmp(p, "audio/aacp", 10) == 0)
			h->info.type = AAC_STREAM;
		else
		{
			h->info.type = NONE_STREAM;
			return -1;
		}
	}

	/* Update metaint with extracted info */
	h->metaint = h->info.metaint;
	h->remaining = h->metaint;

	/* Fill input buffer */
	for(i = 0; i < SHOUT_SYNC_TIMEOUT && 
		   h->in_len < BUFFER_SIZE; i++)
	{
		shoutcast_read_stream(h);
		usleep(1000);
	}
	if(h->in_len < 1024)
		return -1;

	/* Open decoder */
	if(h->info.type == MPEG_STREAM)
	{
		type = CODEC_MP3;

		/* Sync to first frame */
		if(shoutcast_sync_mp3_stream(h) != 0)
			return -1;
	}
	else
	{
		type = CODEC_AAC;

		/* Sync to first frame */
		if(shoutcast_sync_aac_stream(h) != 0)
			return -1;
	}

	/* Open decoder */
	if(decoder_open(&h->dec, type, h->in_buffer, h->in_len, &h->samplerate,
			&h->channels) != 0)
		return -1;

	return 0;
}

unsigned long shoutcast_get_samplerate(struct shout_handle *h)
{
	if(h == NULL)
		return 0;

	return h->samplerate;
}

unsigned char shoutcast_get_channels(struct shout_handle *h)
{
	if(h == NULL)
		return 0;

	return h->channels;
}

static int shoutcast_sync_mp3_stream(struct shout_handle *h)
{
	unsigned int bitrates[2][3][15] = {
		{ /* MPEG-1 */
			{0, 32, 64, 96, 128, 160, 192, 224, 256, 288, 320, 352,
			 384, 416, 448},
			{0, 32, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224,
			 256, 320, 384},
			{0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224,
			 256, 320}
		},
		{ /* MPEG-2 LSF, MPEG-2.5 */
			{0, 32, 48, 56, 64, 80, 96, 112, 128, 144, 160, 176,
			 192, 224, 256},
			{0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128,
			 144, 160},
			{0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128,
			 144, 160}
		}
	};
	unsigned int samplerates[3][4] = {
		{44100, 48000, 32000, 0},
		{22050, 24000, 16000, 0},
		{11025, 8000, 8000, 0}
	};
	int mpeg, layer, padding;
	unsigned long samplerate;
	unsigned int bitrate;
	int tmp;
	int len;
	int i;

	/* Sync input buffer to first MP3 header */
	for(i = 0; i < h->in_len - 3; i++)
	{
		if(h->in_buffer[i] == 0xFF &&
		   h->in_buffer[i+1] != 0xFF &&
		   (h->in_buffer[i+1] & 0xE0) == 0xE0)
		{
			/* Get Mpeg version */
			mpeg = 3 - ((h->in_buffer[i+1] >> 3) & 0x03);
			if(mpeg == 2)
				continue;
			if(mpeg == 3)
				mpeg = 2;

			/* Get Layer */
			layer = 3 - ((h->in_buffer[i+1] >> 1) & 0x03);
			if(layer == 3)
				continue;

			/* Get bitrate */
			tmp = (h->in_buffer[i+2] >> 4) & 0x0F;
			if(tmp == 0 || tmp == 15)
				continue;
			else
			{
				if(mpeg != 2)
					bitrate = bitrates[mpeg][layer][tmp];
				else
					bitrate = bitrates[1][layer][tmp];
			}

			/* Get samplerate */
			tmp = (h->in_buffer[i+2] >> 2) & 0x03;
			if(tmp == 3)
				continue;
			else
				samplerate = samplerates[mpeg][tmp];

			/* Get padding */
			padding = (h->in_buffer[i+2] >> 1) & 0x01;

			/* Calculate length */
			if(layer == 0)
			{
				/* Layer I */
				len = ((12 * bitrate * 1000 / samplerate) +
				       padding) * 4;
			}
			else
			{
				/* Layer II or III */
				len = (144 * bitrate * 1000 / samplerate) +
				      padding;
			}

			/* Check presence of next frame */
			if(i + len + 2 > h->in_len ||
			   h->in_buffer[i+len] != 0xFF ||
			   h->in_buffer[i+len+1] == 0xFF ||
			   (h->in_buffer[i+len+1] & 0xE0) != 0xE0)
				continue;

			/* Move to first frame */
			memmove(h->in_buffer, &h->in_buffer[i],
				h->in_len-i);
			h->in_len -= i;

			/* Refill input buffer */
			shoutcast_read_stream(h);

			return 0;
		}
	}

	return -1;
}

static int shoutcast_sync_aac_stream(struct shout_handle *h)
{
	int len;
	int i;

	/* Sync input buffer to first ADTS header */
	for(i = 0; i < h->in_len - 5; i++)
	{
		if(h->in_buffer[i] == 0xFF &&
		   (h->in_buffer[i+1] & 0xF6) == 0xF0)
		{
			/* Get frame length */
			len = ((h->in_buffer[i+3] & 0x3) << 11) |
			      (h->in_buffer[i+4] << 3) |
			      (h->in_buffer[i+5] >> 5);

			/* Check next frame */
			if(i + len + 2 > h->in_len ||
			   h->in_buffer[i+len] != 0xFF ||
			   (h->in_buffer[i+len+1] & 0xF6) != 0xF0)
				continue;

			/* Move to first frame */
			memmove(h->in_buffer, &h->in_buffer[i],
				h->in_len-i);
			h->in_len -= i;

			/* Refill input buffer */
			shoutcast_read_stream(h);

			return 0;
		}
	}

	return -1;
}

int shoutcast_read(struct shout_handle *h, unsigned char *buffer, size_t size)
{
	struct decoder_info info;
	int total_samples = 0;
	int samples;

	if(h == NULL)
		return -1;

	/* Process remaining pcm data */
	if(h->pcm_remaining > 0)
	{
		/* Get remaining pcm data from decoder */
		samples = decoder_decode(h->dec, NULL, 0, buffer, size, &info);
		if(samples < 0)
			return -1;

		h->pcm_remaining -= samples;
		total_samples += samples;
	}

	/* Fill output buffer */
	while(total_samples < size)
	{
		/* Fill input buffer as possible */
		shoutcast_read_stream(h);

		/* Decode next frame */
		samples = decoder_decode(h->dec, h->in_buffer, h->in_len,
					 &buffer[total_samples * 4],
					 size - total_samples, &info);
		if(samples <= 0)
			return total_samples;

		/* Move input buffer to next frame */
		if(info.used < h->in_len)
		{
			memmove(h->in_buffer, &h->in_buffer[info.used],
				h->in_len - info.used);
			h->in_len -=  info.used;
		}
		else
		{
			h->in_len = 0;
		}

		/* Update remaining counter */
		h->pcm_remaining = info.remaining;
		total_samples += samples;
	}

	return total_samples;
}

struct radio_info *shoutcast_get_info(struct shout_handle *h)
{
	return &h->info;
}

char *shoutcast_get_metadata(struct shout_handle *h)
{
	return h->meta_buffer;
}

int shoutcast_close(struct shout_handle *h)
{
	if(h == NULL)
		return 0;

	/* Close decoder */
	if(h->dec != NULL)
		decoder_close(h->dec);

	/* Close HTTP */
	if(h->http != NULL)
		http_close(h->http);

	/* Free handler */
	free(h);

	return 0;
}

static int shoutcast_read_stream(struct shout_handle *h)
{
	unsigned char c;
	int meta_len = 0;
	int read_len = 0;
	unsigned long in_len;

	/* Calculate input size to read */
	in_len = BUFFER_SIZE - h->in_len;

	/* Fill input buffer */
	while(in_len > 0)
	{
		/* Get size to read */
		if(h->metaint > 0)
			read_len = in_len > h->remaining ? h->remaining :
							    in_len;

		/* Fill inpput buffer */
		read_len = http_read_timeout(h->http, &h->in_buffer[h->in_len],
					     read_len, SHOUT_TIMEOUT);
		if(read_len == 0)
			break;
		else if(read_len < 0)
			return -1;

		/* Update input buffer size */
		h->remaining -= read_len;
		h->in_len += read_len;
		in_len -= read_len;

		/* Process if metadata */
		if(h->metaint > 0 && h->remaining == 0)
		{
			/* Read size */
			http_read(h->http, &c, 1);
			meta_len = c * 16;

			/* Read string */
			if(meta_len > 0)
			{
				h->meta_len = http_read(h->http,
						 (unsigned char*)h->meta_buffer,
						 meta_len);
				h->meta_buffer[h->meta_len] = '\0';
			}

			/* Reset remaining bytes */
			h->remaining = h->metaint;
		}
	}

	return 0;
}

