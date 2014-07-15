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
#include <stdint.h>
#include <unistd.h>
#include <pthread.h>

#include "cache.h"

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#define BUFFER_SIZE 8192

struct cache_format {
	struct a_format fmt;
	unsigned long len;
	struct cache_format *next;
};

struct cache_handle {
	/* Cache properties */
	unsigned long samplerate;
	unsigned char channels;
	unsigned long time;
	int use_thread;
	/* Input callback */
	a_read_cb input_callback;
	a_write_cb output_callback;
	void *input_user;
	void *output_user;
	/* Buffer handling */
	unsigned char *buffer;
	unsigned long size;
	unsigned long len;
	unsigned long pos;
	int is_ready;
	int end_of_stream;
	int new_size;
	/* Associated format to buffer */
	struct cache_format *fmt_first;
	struct cache_format *fmt_last;
	unsigned long fmt_len;
	/* Thread objects */
	pthread_t thread;
	pthread_mutex_t mutex;
	pthread_mutex_t input_lock;
	int flush;
	int stop;
};

static void *cache_read_thread(void *user_data);

int cache_open(struct cache_handle **handle, unsigned long time,
	       unsigned long samplerate, unsigned char channels, int use_thread,
	       a_read_cb input_callback, void *input_user,
	       a_write_cb output_callback, void *output_user)
{
	struct cache_handle *h;

	if(samplerate == 0 || channels == 0 ||
	   (input_callback == NULL && use_thread == 1) ||
	   (input_callback != NULL && output_callback != NULL &&
	    use_thread == 0))
		return -1;

	/* Alloc structure */
	*handle = malloc(sizeof(struct cache_handle));
	if(*handle == NULL)
		return -1;
	h = *handle;

	/* Init structure */
	h->time = time;
	h->samplerate = samplerate;
	h->channels = channels;
	h->buffer = NULL;
	h->size = time * samplerate * channels / 1000;
	h->len = 0;
	h->pos = 0;
	h->is_ready = 0;
	h->end_of_stream = 0;
	h->new_size = 0;
	h->input_callback = input_callback;
	h->output_callback = output_callback;
	h->input_user = input_user;
	h->output_user = output_user;
	h->use_thread = use_thread;
	h->stop = 0;
	h->fmt_first = NULL;
	h->fmt_last = NULL;
	h->fmt_len = 0;

	/* Buffer must be allocated with a thread using input callback and no
	 * output callback */
	if(time == 0 && ((input_callback == NULL && output_callback == NULL) ||
	   ((input_callback == NULL || output_callback == NULL) && use_thread)))
		h->size = BUFFER_SIZE;

	/* Allocate buffer */
	if(h->size != 0)
	{
		h->buffer = malloc(h->size * 4);
		if(h->buffer == NULL)
			return -1;
	}

	/* Init thread mutex */
	pthread_mutex_init(&h->mutex, NULL);
	pthread_mutex_init(&h->input_lock, NULL);

	if(use_thread)
	{
		/* Create thread */
		if(pthread_create(&h->thread, NULL, cache_read_thread, h) != 0)
			return -1;
	}

	return 0;
}

unsigned long cache_get_time(struct cache_handle *h)
{
	unsigned long time;

	/* Lock cache access */
	pthread_mutex_lock(&h->mutex);

	/* Get time */
	time = h->time;

	/* Unlock cache access */
	pthread_mutex_unlock(&h->mutex);

	return time;
}

static void cache_resize(struct cache_handle *h, int unset_is_ready)
{
	unsigned long size;
	unsigned char *p;

	/* Calculate new cache size */
	size = h->time * h->samplerate * h->channels / 1000;
	if(size == h->size)
		return;

	/* Check new size */
	if(size > h->size)
	{
		/* Reallocate bigger buffer */
		p = realloc(h->buffer, size*4);
		if(p == NULL)
			return;
		h->buffer = p;

		/* Unset is_ready */
		if(unset_is_ready || h->size == 0)
			h->is_ready = 0;

		/* Unset new lower size signal */
		h->new_size = 0;
	}
	else
	{
		/* Zero sized buffer not allowed with both callback = NULL or 
		 * Thread with input callback and no output callback */
		if(size == 0 &&
		   ((h->input_callback == NULL && h->output_callback == NULL) ||
		   ((h->input_callback == NULL || h->output_callback == NULL) &&
		     h->use_thread)))
			size = BUFFER_SIZE;

		/* Signal new lower size */
		h->new_size = 1;
	}

	/* Update size */
	h->size = size;
}

int cache_set_time(struct cache_handle *h, unsigned long time)
{
	/* Lock cache access */
	pthread_mutex_lock(&h->mutex);

	/* Check time */
	if(time != h->time)
	{
		/* Change size */
		h->time = time;
		cache_resize(h, 1);
	}

	/* Unlock cache access */
	pthread_mutex_unlock(&h->mutex);

	return 0;
}

int cache_is_ready(struct cache_handle *h)
{
	int ret;

	/* Lock cache access */
	pthread_mutex_lock(&h->mutex);

	/* Check data availability in cache */
	ret = h->is_ready;

	/* Unlock cache access */
	pthread_mutex_unlock(&h->mutex);

	return ret;
}

unsigned char cache_get_filling(struct cache_handle *h)
{
	unsigned long percent;

	/* Lock cache access */
	pthread_mutex_lock(&h->mutex);

	/* Check data availability in cache */
	if(h->is_ready)
		percent = 100;
	else
		percent = h->len * 100 / h->size;

	/* Unlock cache access */
	pthread_mutex_unlock(&h->mutex);

	return (unsigned char) percent;
}

static int cache_put_format(struct cache_handle *h, struct a_format *fmt)
{
	struct cache_format *cf;

	/* Allocate format entry */
	cf = malloc(sizeof(struct cache_format));
	if(cf == NULL)
		return -1;

	/* Copy format */
	format_cpy(&cf->fmt, fmt);

	/* Set len before format change */
	cf->len = h->fmt_len;
	cf->next = NULL;

	/* Add to format list */
	if(h->fmt_last != NULL)
		h->fmt_last->next = cf;
	h->fmt_last = cf;
	if(h->fmt_first == NULL)
		h->fmt_first = cf;
	h->fmt_len = 0;

	return 0;
}

static void cache_get_format(struct cache_handle *h)
{
	struct cache_format *cf;

	if(h->fmt_first == NULL)
		return;

	/* Update list */
	cf = h->fmt_first;
	h->fmt_first = cf->next;

	/* Free current entry */
	free(cf);
}

static void cache_update_format(struct cache_handle *h, size_t size,
				struct a_format *fmt)
{
	if(h->fmt_last == NULL ||
	   ((fmt->samplerate != 0 || fmt->channels != 0) &&
	    format_cmp(fmt, &h->fmt_last->fmt) != 0))
		cache_put_format(h, fmt);
	h->fmt_len += size;
}

static void cache_next_format(struct cache_handle *h, size_t *size,
			      struct a_format *fmt)
{
	struct cache_format *next_fmt;

	/* Check format list */
	if(h->fmt_first != NULL)
	{
		/* Copy current format */
		format_cpy(fmt, &h->fmt_first->fmt);

		/* Check next format */
		next_fmt = h->fmt_first->next;
		if(next_fmt != NULL)
		{
			if(next_fmt->len < *size)
			{
				/* Limit size to format switching byte
				 * and free current format
				 */
				*size = next_fmt->len;
				cache_get_format(h);

				/* Update format */
				if(next_fmt->fmt.samplerate != 0)
					h->samplerate =
						       next_fmt->fmt.samplerate;
				if(next_fmt->fmt.channels != 0)
					h->channels = next_fmt->fmt.channels;

				/* Update cache size */
				cache_resize(h, 0);
			}
			next_fmt->len -= *size;
		}
		else
			h->fmt_len -= *size;
	}
}

static void cache_reduce(struct cache_handle *h)
{
	unsigned char *p;

	/* Reduce buffer */
	if(h->len <= h->size)
	{
		/* Zero size case */
		if(h->size == 0)
		{
			/* Free buffer */
			if(h->buffer != NULL)
				free(h->buffer);
			h->buffer = NULL;
		}
		else
		{
			/* Rallocate buffer */
			p = realloc(h->buffer, h->size*4);
			if(p == NULL)
				return;
			h->buffer = p;
		}
		h->new_size = 0;
	}
}

static void cache_output(struct cache_handle *h)
{
	struct a_format out_fmt = A_FORMAT_INIT;
	size_t size;

	if(h->buffer != NULL && h->output_callback != NULL && h->is_ready)
	{
		/* Set size to buffer size */
		size = h->len;

		/* Check format list */
		cache_next_format(h, &size, &out_fmt);

		/* Send data */
		size = h->output_callback(h->output_user, h->buffer, size,
					  &out_fmt);
		if(size > 0)
		{
			h->len -= size;
			memmove(h->buffer, &h->buffer[size*4], h->size*4);

			/* Reduce buffer to new size */
			if(h->new_size)
				cache_reduce(h);
		}

		/* No more data is available */
		if(h->len == 0)
			h->is_ready = 0;
	}
}

static void *cache_read_thread(void *user_data)
{
	struct cache_handle *h = (struct cache_handle *) user_data;
	struct a_format in_fmt = A_FORMAT_INIT;
	unsigned char *buffer = NULL;
	unsigned long in_size = 0;
	unsigned long len = 0;
	ssize_t size;
	int ret = 0;

	/* Allocate buffer */
	if(h->input_callback != NULL)
	{
		buffer = malloc(BUFFER_SIZE);
		if(buffer == NULL)
			return NULL;
	}

	/* Read indefinitively the input callback */
	while(!h->stop)
	{
		/* Lock input callback */
		cache_lock(h);

		/* Check stop */
		if(h->stop)
		{
			/* Unlock cache */
			cache_unlock(h);
			break;
		}

		/* No cache */
		if(h->buffer == NULL)
		{
			if(h->input_callback == NULL ||
			   h->output_callback == NULL)
				return NULL;

			/* Get data */
			if(len == 0)
			{
				len = h->input_callback(h->input_user, buffer,
							BUFFER_SIZE / 4,
							&in_fmt);
			}

			/* Copy data */
			if(len > 0)
			{
				size = h->output_callback(h->output_user,
							  buffer, len, &in_fmt);

				/* Move unused data */
				len -= size;
				memmove(buffer, &buffer[size*4], len * 4);
			}

			/* Unlock cache */
			cache_unlock(h);

			continue;
		}

		if(h->input_callback == NULL)
			goto flush;

		/* Flush this buffer */
		if(h->flush)
		{
			h->flush = 0;
			len = 0;
		}

		/* Check buffer len */
		if(len < BUFFER_SIZE / 4)
		{
			/* Read next packet from input callback */
			ret = h->input_callback(h->input_user, &buffer[len*4],
						(BUFFER_SIZE / 4) - len,
						&in_fmt);
			if(ret < 0)
			{
				h->is_ready = 1;
				h->end_of_stream = 1;
				usleep(10000);
				goto copy;
			}
			h->end_of_stream = 0;
			len += ret;
		}

copy:
		/* Lock cache access */
		pthread_mutex_lock(&h->mutex);

		/* No data to copy: jump to flush */
		if(len == 0 || h->len > h->size)
			goto flush;

		/* Copy data to cache */
		in_size = h->size - h->len;
		if(in_size > len)
			in_size = len;
		memcpy(&h->buffer[h->len*4], buffer, in_size * 4);
		h->len += in_size;
		len -= in_size;

		/* Update format list */
		cache_update_format(h, in_size, &in_fmt);

		/* Cache is full */
		if(h->len == h->size)
			h->is_ready = 1;

flush:
		/* Send data if output callback is available */
		cache_output(h);

		/* Unlock cache access */
		pthread_mutex_unlock(&h->mutex);

		/* Move remaining data*/
		if(len > 0)
			memmove(buffer, &buffer[in_size*4], len * 4);

		/* Unlock cache */
		cache_unlock(h);

		/* Buffer is already fill: sleep 1ms */
		if(len >= BUFFER_SIZE / 4)
			usleep(1000);
	}

	/* Free buffer */
	if(buffer != NULL)
		free(buffer);

	return NULL;
}

int cache_read(void *user_data, unsigned char *buffer, size_t size,
	       struct a_format *fmt)
{
	struct cache_handle *h = (struct cache_handle *) user_data;
	struct a_format in_fmt = A_FORMAT_INIT;
	unsigned long in_size;
	long len;

	if(h == NULL || h->output_callback != NULL)
		return -1;

	/* No cache */
	if(h->buffer == NULL)
	{
		if(h->input_callback == NULL)
			return -1;

		/* Copy data */
		return h->input_callback(h->input_user, buffer, size, fmt);
	}

	/* Lock cache access */
	pthread_mutex_lock(&h->mutex);

	/* Check data availability in cache */
	if(h->is_ready)
	{
		/* Some data is available */
		if(size > h->len)
			size = h->len;

		/* Check format list */
		cache_next_format(h, &size, fmt);

		/* Read in cache */
		memcpy(buffer, h->buffer, size*4);
		h->len -= size;
		memmove(h->buffer, &h->buffer[size*4], h->len*4);

		/* Reduce buffer to new size */
		if(h->new_size)
			cache_reduce(h);

		/* No more data is available */
		if(h->len == 0)
			h->is_ready = 0;
	}
	else if(h->end_of_stream && h->len == 0)
	{
		/* End of stream */
		size = -1;
	}
	else
	{
		/* No data */
		size = 0;
	}

	/* Unlock cache access */
	pthread_mutex_unlock(&h->mutex);

	/* Check cache status */
	if(h->input_callback != NULL && !h->use_thread && h->len < h->size)
	{
		/* Check input callback access */
		if(pthread_mutex_trylock(&h->input_lock) != 0)
			return size;

		/* Fill cache with some samples */
		in_size = h->size - h->len;
		len = h->input_callback(h->input_user, &h->buffer[h->len*4],
					in_size, &in_fmt);
		if(len < 0)
		{
			/* End of stream */
			h->is_ready = 1;

			/* No more data available */
			if(h->len == 0)
				size = -1;

			/* Unlock input callback access */
			cache_unlock(h);

			return size;
		}
		h->len += len;

		/* Update format list */
		cache_update_format(h, len, &in_fmt);

		/* Cache is full */
		if(h->len >= h->size)
			h->is_ready = 1;

		/* Unlock input callback access */
		cache_unlock(h);
	}

	return size;
}

ssize_t cache_write(void *user_data, const unsigned char *buffer, size_t size,
		    struct a_format *fmt)
{
	struct cache_handle *h = (struct cache_handle *) user_data;
	unsigned long in_size = 0;

	if(h->input_callback != NULL)
		return -1;

	/* No cache */
	if(h->buffer == NULL)
	{
		if(h->output_callback == NULL)
			return -1;

		/* Copy data */
		return h->output_callback(h->output_user, buffer, size, fmt);
	}

	/* Lock cache access */
	pthread_mutex_lock(&h->mutex);

	/* Calculate size to write in cache */
	in_size = h->size - h->len;
	if(size > in_size)
		size = in_size;
	if(size == 0)
		goto end;

	/* Copy data to buffer */
	memcpy(&h->buffer[h->len*4], buffer, size * 4);
	h->len += size;

	/* Update format list */
	cache_update_format(h, size, fmt);

	/* Cache is full */
	if(h->len == h->size)
		h->is_ready = 1;

end:
	/* Send data if output callback is available */
	if(!h->use_thread)
		cache_output(h);

	/* Unlock cache access */
	pthread_mutex_unlock(&h->mutex);

	return size;
}

void cache_flush(struct cache_handle *h)
{
	struct cache_format *cf;

	/* Lock input callback */
	cache_lock(h);

	/* Lock cache access */
	pthread_mutex_lock(&h->mutex);

	/* Flush the cache */
	h->end_of_stream = 0;
	h->is_ready = 0;
	h->len = 0;

	/* Flush format list */
	while(h->fmt_first != NULL)
	{
		cf = h->fmt_first;
		h->fmt_first = cf->next;
		free(cf);
	}
	h->fmt_last = NULL;

	/* Notice flush to thread */
	if(h->use_thread)
		h->flush = 1;

	/* Unlock cache access */
	pthread_mutex_unlock(&h->mutex);
}

void cache_lock(struct cache_handle *h)
{
	/* Lock input callback access */
	pthread_mutex_lock(&h->input_lock);
}

void cache_unlock(struct cache_handle *h)
{
	/* Unlock input callback access */
	pthread_mutex_unlock(&h->input_lock);
}

unsigned long cache_delay(struct cache_handle *h)
{
	struct cache_format *fmt;
	unsigned long samplerate;
	unsigned char channels;
	uint64_t delay = 0;

	if(h == NULL)
		return 0;

	/* Lock cache access */
	pthread_mutex_lock(&h->mutex);

	/* Check format list */
	if(h->fmt_first != NULL)
	{
		samplerate = h->samplerate;
		channels = h->channels;
		/* Parse all format list */
		fmt = h->fmt_first->next;
		while(fmt != NULL)
		{
			delay += ((uint64_t)fmt->len) * 1000 / samplerate /
				 channels;
			if(fmt->fmt.samplerate != 0)
				samplerate = fmt->fmt.samplerate;
			if(fmt->fmt.channels != 0)
				channels = fmt->fmt.channels;
			fmt = fmt->next;
		}
		delay += ((uint64_t)h->fmt_len) * 1000 / samplerate / channels;
	}
	else
		delay = ((uint64_t)h->len) * 1000 / h->samplerate /
			h->channels;

	/* Unlock cache access */
	pthread_mutex_unlock(&h->mutex);

	return delay;
}

int cache_close(struct cache_handle *h)
{
	struct cache_format *cf;

	if(h == NULL)
		return 0;

	/* Stop thread */
	h->stop = 1;

	/* Unlock input callback */
	cache_unlock(h);

	/* Wait end of the thread */
	if(h->use_thread)
		pthread_join(h->thread, NULL);

	/* Free format list */
	while(h->fmt_first != NULL)
	{
		cf = h->fmt_first;
		h->fmt_first = cf->next;
		free(cf);
	}

	/* Free buffer */
	if(h->buffer != NULL)
		free(h->buffer);

	/* Free structure */
	free(h);

	return 0;
}

