/*
 * stream.c - An input stream
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
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>

#include "stream.h"

/* Default internal buffer size */
#define BUFFER_SIZE 8192

struct stream_handle {
	/* URI to file */
	char *uri;
	char *content_type;
	struct file_format *format;
	/* File descriptor */
	int fd;
	long pos;
	size_t size;
	/* Read buffer */
	unsigned char *buffer;
	size_t buffer_size;
	size_t buffer_len;
	/* Flag to free buffer at close */
	int free_buffer;
};

int stream_open(struct stream_handle **handle, const char *uri,
		unsigned char *buffer, size_t size)
{
	struct stream_handle *h;
	struct stat st;
	const char *ext;

	/* Check URI */
	if(uri == NULL)
		return -1;

	/* Check file */
	if(stat(uri, &st) != 0 || !S_ISREG(st.st_mode))
		return -1;

	/* Allocate handle */
	*handle = malloc(sizeof(struct stream_handle));
	if(*handle == NULL)
		return -1;
	h = *handle;

	/* Init structure */
	h->uri = strdup(uri);
	h->content_type = NULL;
	h->fd = -1;
	h->pos = 0;
	h->size = st.st_size;
	h->buffer = buffer;
	h->buffer_size = size != 0 ? size : BUFFER_SIZE;
	h->buffer_len = 0;
	h->free_buffer = 0;

	/* Get file format */
	h->format = file_format_parse(uri, 0);
	if(h->format != NULL)
	{
		if(h->format->type == FILE_FORMAT_MPEG)
			h->content_type = strdup("audio/mpeg");
		else if(h->format->type == FILE_FORMAT_AAC)
			h->content_type = strdup("audio/mp4");
	}

	/* Allocate buffer if not specified */
	if(buffer == NULL)
	{
		h->buffer = malloc(h->buffer_size);
		if(h->buffer == NULL)
			return -1;
		h->free_buffer = 1;
	}

	/* Open file stream */
	h->fd = open(uri, O_RDONLY);
	if(h->fd < 0)
		return -1;

	/* Guess content type with extension */
	if(h->content_type == NULL)
	{
		ext = &uri[strlen(uri)-4];
		if(strcasecmp(ext, ".mp3") == 0)
			h->content_type = strdup("audio/mpeg");
		else if(strcasecmp(ext, ".mp4") == 0)
			h->content_type = strdup("audio/mp4");
	}

	return 0;
}

const unsigned char *stream_get_buffer(struct stream_handle *h)
{
	return h->buffer;
}

size_t stream_get_buffer_size(struct stream_handle *h)
{
	return h->buffer_size;
}

struct file_format *stream_get_format(struct stream_handle *h)
{
	return h->format;
}

const char *stream_get_content_type(struct stream_handle *h)
{
	return h->content_type;
}

ssize_t stream_read(struct stream_handle *h, size_t len)
{
	ssize_t size;

	if(h == NULL || h->fd < 0)
		return -1;

	/* Fill all buffer */
	if(len == 0 || len > h->buffer_size)
		len = h->buffer_size;

	/* Read and fill input buffer */
	size = read(h->fd, h->buffer, len);
	if(size < 0)
		return -1;

	/* Update input buffer size and stream position */
	h->pos += h->buffer_len;
	h->buffer_len = size;

	return size;
}

ssize_t stream_complete(struct stream_handle *h, size_t len)
{
	ssize_t size;

	if(h == NULL || h->fd < 0)
		return -1;

	/* Fill all buffer */
	if(len == 0 || len + h->buffer_len > h->buffer_size)
		len = h->buffer_size - h->buffer_len;

	if(len == 0)
		return h->buffer_len;

	/* Read and append to input buffer */
	size = read(h->fd, &h->buffer[h->buffer_len], len);
	if(size <= 0)
		return -1;

	/* Update input buffer size */
	h->buffer_len += size;

	return h->buffer_len;
}

long stream_seek(struct stream_handle *h, long pos, int whence)
{
	unsigned long p;
	size_t size = 0;

	if(h == NULL || h->fd < 0)
		return -1;

		/* Move pos bytes from current stream position */
	if(whence == SEEK_CUR)
		pos += h->pos;

	if(pos >= h->pos && pos < h->pos + h->buffer_len)
	{
		/* Just move input buffer data */
		p = pos - h->pos;
		size = h->buffer_len - p;
		memmove(h->buffer, &h->buffer[p], size);
	}
	else if(pos != h->pos + h->buffer_len)
	{
		/* Seek in stream */
		if(lseek(h->fd, pos, SEEK_SET) != pos)
			return -1;
	}

	/* Update input buffer size and stream position */
	h->pos = pos;
	h->buffer_len = size;

	return 0;
}

long stream_get_pos(struct stream_handle *h)
{
	return h->pos;
}

long stream_get_size(struct stream_handle *h)
{
	return h->size;
}

long stream_get_len(struct stream_handle *h)
{
	return h->buffer_len;
}

void stream_close(struct stream_handle *h)
{
	if(h == NULL)
		return;

	/* Close file stream */
	if(h->fd > 0)
		close(h->fd);

	/* Free internal buffer */
	if(h->free_buffer)
		free(h->buffer);

	/* Free strings */
	if(h->content_type != NULL)
		free(h->content_type);
	free(h->uri);

	/* Free format */
	if(h->format != NULL)
		file_format_free(h->format);

	/* Free handle */
	free(h);
}
