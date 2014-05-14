/*
 * cache.c - A generic cache module
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

#include "cache.h"

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

struct cache_handle {
	/* Input callback */
	int (*input_callback)(void *, unsigned char *, size_t);
	void *user_data;
	/* Buffer handling */
	unsigned char *buffer;
	unsigned long size;
	unsigned long len;
	unsigned long pos;
	int is_ready;
};

int cache_open(struct cache_handle **handle, unsigned long size,
	       void *input_callback, void *user_data)
{
	struct cache_handle *h;

	if(size == 0 || input_callback == NULL)
		return -1;

	/* Alloc structure */
	*handle = malloc(sizeof(struct cache_handle));
	if(*handle == NULL)
		return -1;
	h = *handle;

	/* Init structure */
	h->buffer = NULL;
	h->size = size;
	h->len = 0;
	h->pos = 0;
	h->is_ready = 0;
	h->input_callback = input_callback;
	h->user_data = user_data;

	/* Allocate buffer */
	h->buffer = malloc(size * 4);
	if(h->buffer == NULL)
		return -1;

	return 0;
}

int cache_read(struct cache_handle *h, unsigned char *buffer, size_t size)
{
	unsigned long in_size;
	long len;

	if(h == NULL || h->buffer == NULL)
		return -1;

	/* Check data availability in cache */
	if(h->is_ready)
	{
		/* Some data is available */
		if(size > h->len)
			size = h->len;

		/* Read in cache */
		memcpy(buffer, h->buffer, size*4);
		h->len -= size;
		memmove(h->buffer, &h->buffer[size*4], h->len*4);

		/* No more data is available */
		if(h->len == 0)
			h->is_ready = 0;
	}
	else
		size = 0;

	/* Check cache status */
	if(h->len < h->size)
	{
		/* Fill cache with some samples */
		in_size = h->size - h->len;
		len = h->input_callback(h->user_data, &h->buffer[h->len*4],
					in_size);
		if(len < 0)
		{
			if(h->len == 0)
				return -1;
			return size;
		}
		h->len += len;

		/* Cache is full */
		if(h->len == h->size)
			h->is_ready = 1;
	}

	return size;
}

int cache_close(struct cache_handle *h)
{
	if(h == NULL)
		return 0;

	/* Free buffer */
	if(h->buffer != NULL)
		free(h->buffer);

	/* Free structure */
	free(h);

	return 0;
}

