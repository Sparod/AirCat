/*
 * shoutcast.c - A ShoutCast Client
 * 
 * Copyright (c) 2013   A. Dilly
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

#include "http.h"
#include "shoutcast.h"

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

struct shout_handle {
	struct http_handle *http;	// HTTP Client handle
	unsigned int metaint;		// Metaint from shoutcast server
	unsigned int remaining;		// Remaining bytes before next metadata block
	int meta_len;			// Metadata string length
	char meta_buffer[16*255];	// Metadata string
	struct radio_info info;		// Radio infos
};

struct shout_handle *shoutcast_init()
{
	struct shout_handle *h;

	/* Alloc structure */
	h = malloc(sizeof(struct shout_handle));
	if(h == NULL)
		return NULL;

	/* Set to zero radio_info structure */
	memset((unsigned char*)&h->info, 0, sizeof(struct radio_info));

	/* Init http client */
	h->http = http_init();
	if(h->http == NULL)
	{
		shoutcast_close(h);
		return NULL;
	}

	return h;
}

int shoutcast_open(struct shout_handle *h, const char *url)
{
	int code = 0;
	char *p;

	/* Set options */
	http_set_option(h->http, HTTP_USER_AGENT, "Aircat 1.0");
	http_set_option(h->http, HTTP_EXTRA_HEADER, "Icy-MetaData: 1\r\n");

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

	return 0;
}

int shoutcast_read(struct shout_handle *h, unsigned char *buffer, size_t size)
{
	int read_len = 0;
	int meta_len = 0;
	unsigned char c;

	/* If metadata enabled, read until next metadata */
	if(h->metaint > 0 && size > h->remaining)
		read_len = h->remaining;
	else
		read_len = size;

	/* Read */
	read_len = http_read(h->http, buffer, read_len);

	/* If metadata enabled */
	if(h->metaint > 0)
	{
		/* Update remaining bytes before next metadata */
		h->remaining -= read_len;

		/* Read metadata block */
		if(h->remaining == 0)
		{
			/* Read size */
			http_read(h->http, &c, 1);
			meta_len = c*16;

			/* Read string */
			if(meta_len > 0)
			{
				h->meta_len = http_read(h->http, (unsigned char*)h->meta_buffer, meta_len);
			}

			/* Reset remaining bytes */
			h->remaining = h->metaint;
		}
	}

	return read_len;
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
	http_close(h->http);

	free(h);

	return 0;
}

