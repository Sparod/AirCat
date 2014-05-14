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
#include <unistd.h>
#include <pthread.h>

#include "cache.h"

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#define BUFFER_SIZE 8192

struct cache_handle {
	/* Cache properties */
	int use_thread;
	/* Input callback */
	int (*input_callback)(void *, unsigned char *, size_t);
	void *user_data;
	/* Buffer handling */
	unsigned char *buffer;
	unsigned long size;
	unsigned long len;
	unsigned long pos;
	int is_ready;
	/* Thread objects */
	pthread_t thread;
	pthread_mutex_t mutex;
	int stop;
};

static void *cache_read_thread(void *user_data);

int cache_open(struct cache_handle **handle, unsigned long size, int use_thread,
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
	h->use_thread = use_thread;
	h->stop = 0;

	/* Allocate buffer */
	h->buffer = malloc(size * 4);
	if(h->buffer == NULL)
		return -1;

	/* Init thread mutex */
	pthread_mutex_init(&h->mutex, NULL);

	if(use_thread)
	{
		/* Create thread */
		if(pthread_create(&h->thread, NULL, cache_read_thread, h) != 0)
			return -1;
	}

	return 0;
}

static void *cache_read_thread(void *user_data)
{
	struct cache_handle *h = (struct cache_handle *) user_data;
	unsigned char buffer[BUFFER_SIZE];
	unsigned long in_size = 0;
	unsigned long len = 0;

	/* Read indefinitively the input callback */
	while(!h->stop)
	{
		/* Sleep for 1ms */
		usleep(1000);

		/* Read next packet from input callback */
		len += h->input_callback(h->user_data, &buffer[len*4],
					 (BUFFER_SIZE / 4) - len);
		if(len < 0)
			break;
		else if(len == 0)
			continue;

		/* Lock cache access */
		pthread_mutex_lock(&h->mutex);

		/* Copy data to cache */
		in_size = h->size - h->len;
		if(in_size > len)
			in_size = len;
		memcpy(&h->buffer[h->len*4], buffer, in_size * 4);
		h->len += in_size;
		len -= in_size;

		/* Cache is full */
		if(h->len == h->size)
			h->is_ready = 1;

		/* Unlock cache access */
		pthread_mutex_unlock(&h->mutex);

		/* Move remaining data*/
		if(len > 0)
			memmove(buffer, &buffer[in_size*4], len * 4);
	}

	return NULL;
}

int cache_read(struct cache_handle *h, unsigned char *buffer, size_t size)
{
	unsigned long in_size;
	long len;

	if(h == NULL || h->buffer == NULL)
		return -1;

	/* Lock cache access */
	pthread_mutex_lock(&h->mutex);

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

	/* Unlock cache access */
	pthread_mutex_unlock(&h->mutex);

	/* Check cache status */
	if(!h->use_thread && h->len < h->size)
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

	/* Stop thread */
	if(h->use_thread)
	{
		/* Send stop signal */
		h->stop = 1;

		/* Wait end of the thread */
		pthread_join(h->thread, NULL);
	}

	/* Free buffer */
	if(h->buffer != NULL)
		free(h->buffer);

	/* Free structure */
	free(h);

	return 0;
}

