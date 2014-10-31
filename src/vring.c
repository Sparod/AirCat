/*
 * vring.c - A virtual ring buffer with direct access
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
#include <pthread.h>

#include "vring.h"

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

struct vring_handle {
	/* Ring buffer */
	unsigned char *buffer;
	size_t buffer_size;
	size_t max_rw_size;
	/* Buffer status */
	size_t buffer_len;
	size_t read_pos;
	size_t write_pos;
	/* Mutex thread */
	pthread_mutex_t mutex;
};

int vring_open(struct vring_handle **handle, size_t buffer_size,
	       size_t max_rw_size)
{
	struct vring_handle *h;

	/* Check values */
	if(buffer_size == 0 || max_rw_size == 0)
		return -1;

	/* Allocate handle */
	*handle = malloc(sizeof(struct vring_handle));
	if(*handle == NULL)
		return -1;
	h = *handle;

	/* Init handle */
	h->buffer_size = buffer_size;
	h->max_rw_size = max_rw_size;
	h->buffer_len = 0;
	h->read_pos = 0;
	h->write_pos = 0;

	/* Allocate buffer */
	h->buffer = malloc(h->buffer_size+h->max_rw_size);
	if(h->buffer == NULL)
		return -1;

	/* Init mutex */
	pthread_mutex_init(&h->mutex, NULL);

	return 0;
}

size_t vring_get_length(struct vring_handle *h)
{
	size_t len;

	/* Lock access to ring buffer */
	pthread_mutex_lock(&h->mutex);

	/* Copy length */
	len = h->buffer_len;

	/* Unlock access to ring buffer */
	pthread_mutex_unlock(&h->mutex);

	return len;
}

ssize_t vring_read(struct vring_handle *h, unsigned char **buffer,
		   size_t len, size_t pos)
{
	/* Limit length access */
	if(len > h->max_rw_size || len == 0)
		len = h->max_rw_size;

	/* Lock access to ring buffer */
	pthread_mutex_lock(&h->mutex);

	/* Get available length */
	if(len > h->buffer_len - pos)
		len = h->buffer_len - pos;

	/* Set buffer pointer */
	*buffer = h->buffer + h->read_pos + pos;
	if(*buffer >= h->buffer + h->buffer_size)
		*buffer -= h->buffer_size;

	/* Unlock access to ring buffer */
	pthread_mutex_unlock(&h->mutex);

	return len;
}

ssize_t vring_read_forward(struct vring_handle *h, size_t len)
{
	/* Lock access to ring buffer */
	pthread_mutex_lock(&h->mutex);

	/* Check buffer length */
	if(len > h->buffer_len)
		len = h->buffer_len;

	/* No update */
	if(len == 0)
	{
		/* Unlock access to ring buffer */
		pthread_mutex_unlock(&h->mutex);
		return 0;
	}

	/* Update read position and available length */
	h->read_pos += len;
	if(h->read_pos >= h->buffer_size)
		h->read_pos -= h->buffer_size;
	h->buffer_len -= len;

	/* Unlock access to ring buffer */
	pthread_mutex_unlock(&h->mutex);

	return len;
}

ssize_t vring_write(struct vring_handle *h, unsigned char **buffer)
{
	ssize_t len = h->max_rw_size;

	/* Lock access to ring buffer */
	pthread_mutex_lock(&h->mutex);

	/* Check available space in ring buffer */
	if(len > h->buffer_size - h->buffer_len)
		len = h->buffer_size - h->buffer_len;

	/* Unlock access to ring buffer */
	pthread_mutex_unlock(&h->mutex);

	/* Set buffer pointer */
	*buffer = h->buffer + h->write_pos;

	return len;
}

ssize_t vring_write_forward(struct vring_handle *h, size_t len)
{
	size_t size = 0;
	size_t rem = 0;

	/* Lock access to ring buffer */
	pthread_mutex_lock(&h->mutex);

	/* Check available space in ring buffer */
	if(len > h->buffer_size - h->buffer_len)
		len = h->buffer_size - h->buffer_len;

	/* Unlock access to ring buffer */
	pthread_mutex_unlock(&h->mutex);

	/* No available space */
	if(len == 0)
		return 0;

	/* Overlap in ring buffer */
	if(h->write_pos + len > h->buffer_size)
	{
		/* Copy data to ring buffer start */
		rem = h->buffer_size - h->write_pos - len;
		size = len - rem;
		memcpy(h->buffer, h->buffer + rem, size);
	}
	else if(h->write_pos < h->max_rw_size)
	{
		/* Copy data to ring buffer reserve */
		rem = h->buffer_size + h->write_pos;
		size = h->max_rw_size - h->write_pos;
		if(len < size)
			size = len;
		memcpy(h->buffer + rem , h->buffer, size);
	}

	/* Lock access to ring buffer */
	pthread_mutex_lock(&h->mutex);

	/* Update write position and available length */
	h->write_pos += len;
	if(h->write_pos >= h->buffer_size)
		h->write_pos -= h->buffer_size;
	h->buffer_len += len;

	/* Unlock access to ring buffer */
	pthread_mutex_unlock(&h->mutex);

	return len;
}

void vring_close(struct vring_handle *h)
{
	if(h == NULL)
		return;

	/* Free ring buffer */
	if(h->buffer != NULL)
		free(h->buffer);

	/* Free handle */
	free(h);
}
