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
#include "http.h"

/* Default internal buffer size */
#define BUFFER_SIZE 8192

/* Default maximum size before using seek instead of skip with HTTP */
#define MAX_SKIP_LEN 8192

struct stream_handle {
	/* URI to file */
	char *uri;
	char *content_type;
	/* File descriptor */
	int fd;
	long pos;
	size_t size;
	/* HTTP client */
	struct http_handle *http;
	int is_seekable;
	/* Read function */
	ssize_t (*read)(void *, unsigned char *, size_t, long);
	void *read_data;
	/* Read buffer */
	unsigned char *buffer;
	size_t buffer_size;
	size_t buffer_len;
	size_t skip_len;
	/* Flag to free buffer at close */
	int free_buffer;
};

static ssize_t stream_read_fd(struct stream_handle *h, unsigned char *buffer,
			      size_t size, long timeout);

int stream_open(struct stream_handle **handle, const char *uri,
		unsigned char *buffer, size_t size)
{
	struct stream_handle *h;
	struct stat st;
	const char *ext;
	int code;

	/* Check URI */
	if(uri == NULL)
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
	h->size = 0;
	h->http = NULL;
	h->is_seekable = 0;
	h->buffer = buffer;
	h->buffer_size = size != 0 ? size : BUFFER_SIZE;
	h->buffer_len = 0;
	h->skip_len = 0;
	h->free_buffer = 0;

	/* Parse URI */
	if(strncmp(uri, "http://", 7) != 0 && strncmp(uri, "https://", 8) != 0)
	{
		/* Check file and get size */
		if(stat(uri, &st) != 0 || !S_ISREG(st.st_mode))
			return -1;
		h->size = st.st_size;

		/* Open file stream */
		h->fd = open(uri, O_RDONLY);
		if(h->fd < 0)
			return -1;

		/* Get read() function */
		h->read = (void *) &stream_read_fd;
		h->read_data = h;

		/* Set seekable */
		h->is_seekable = 1;
	}
	else
	{
		/* Create a new HTTP client */
		if(http_open(&h->http, 1) != 0)
			return -1;

		/* Prepare request with seek option */
		http_set_option(h->http, HTTP_EXTRA_HEADER,
				"Range: bytes=0-\r\n", 0);

		/* Do request */
		code = http_get(h->http, uri);
		if(code == 200 || code == 206)
		{
			/* Get accepted range */
			ext = http_get_header(h->http, "Accept-Ranges", 0);
			if(ext != NULL && strncmp(ext, "bytes", 5) == 0)
				h->is_seekable = 1;
		}
		else
			return -1;

		/* Get file length */
		ext = http_get_header(h->http, "Content-Length", 0);
		if(ext != NULL)
			h->size = strtoul(ext, NULL, 10);

		/* Get content type */
		ext = http_get_header(h->http, "Content-Type", 0);
		if(ext != NULL)
			h->content_type = strdup(ext);

		/* Get read() function */
		h->read = (void *) &http_read_timeout;
		h->read_data = h->http;
	}

	/* Allocate buffer if not specified */
	if(buffer == NULL)
	{
		/* Reduce buffer size if file length is lower */
		if(h->size != 0 && h->size < h->buffer_size)
			h->buffer_size = h->size;

		/* Allocate buffer */
		h->buffer = malloc(h->buffer_size);
		if(h->buffer == NULL)
			return -1;

		/* Memory will be freed in close() call */
		h->free_buffer = 1;
	}

	/* Guess content type with extension */
	ext = &uri[strlen(uri)-4];
	if(h->content_type == NULL)
	{
		if(strcasecmp(ext, ".mp3") == 0)
			h->content_type = strdup("audio/mpeg");
		else if(strcasecmp(ext, ".m4a") == 0 ||
			strcasecmp(ext, ".mp4") == 0)
			h->content_type = strdup("audio/mp4");
	}
	else
	{
		/* Fix coherency between "audio/mpeg" and .m4a */
		if(strcasecmp(ext, ".m4a") == 0 &&
		   strcmp(h->content_type, "audio/mpeg") == 0)
		{
			free(h->content_type);
			h->content_type = strdup("audio/mp4");
		}
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

const char *stream_get_content_type(struct stream_handle *h)
{
	return h->content_type;
}

int stream_is_seekable(struct stream_handle *h)
{
	return h->is_seekable;
}

static ssize_t stream_read_fd(struct stream_handle *h, unsigned char *buffer,
			      size_t size, long timeout)
{
	struct timeval tv;
	fd_set readfs;
	ssize_t len;

	if(h->fd < 0)
		return -1;

	/* Use timeout */
	if(timeout >= 0)
	{
		/* Prepare a select */
		FD_ZERO(&readfs);
		FD_SET(h->fd, &readfs);

		/* Set timeout */
		tv.tv_sec = 0;
		tv.tv_usec = timeout * 1000;

		if(select(h->fd + 1, &readfs, NULL, NULL, &tv) < 0)
			return -1;
	}

	/* Read from file */
	if(timeout == -1 || FD_ISSET(h->fd, &readfs))
	{
		len = read(h->fd, buffer, size);

		/* End of stream */
		if(len <= 0)
			return -1;

		return len;
	}

	return 0;
}

ssize_t stream_read_timeout(struct stream_handle *h, size_t len, long timeout)
{
	ssize_t size;

	if(h == NULL)
		return -1;

	/* Skip bytes */
	while(h->skip_len > 0)
	{
		size = h->skip_len > h->buffer_size ? h->buffer_size :
						      h->skip_len;
		size = h->read(h->read_data, h->buffer, size, timeout);
		if(size <= 0)
			return size;
		h->skip_len -= size;
	}

	/* Fill all buffer */
	if(len == 0 || len > h->buffer_size)
		len = h->buffer_size;

	/* Read and fill input buffer */
	size = h->read(h->read_data, h->buffer, len, timeout);
	if(size < 0)
		return -1;

	/* Update input buffer size and stream position */
	h->pos += h->buffer_len;
	h->buffer_len = size;

	return size;
}

ssize_t stream_complete_timeout(struct stream_handle *h, size_t len,
				long timeout)
{
	ssize_t size;

	if(h == NULL)
		return -1;

	/* Skip bytes */
	while(h->skip_len > 0)
	{
		size = h->skip_len > h->buffer_size ? h->buffer_size :
						      h->skip_len;
		size = h->read(h->read_data, h->buffer, size, timeout);
		if(size <= 0)
			return size;
		h->skip_len -= size;
	}

	/* Fill all buffer */
	if(len == 0 || len + h->buffer_len > h->buffer_size)
		len = h->buffer_size - h->buffer_len;

	if(len == 0)
		return h->buffer_len;

	/* Read and append to input buffer */
	size = h->read(h->read_data, &h->buffer[h->buffer_len], len, timeout);
	if(size < 0)
		return -1;

	/* Update input buffer size */
	h->buffer_len += size;

	return h->buffer_len;
}

long stream_seek(struct stream_handle *h, long pos, int whence)
{
	char range_req[255];
	size_t size = 0;
	int code;

	if(h == NULL || whence == SEEK_END)
		return -1;

	/* Calulate new position */
	if(whence == SEEK_SET)
		pos -= h->pos;

	/* Cannot rewind in non seekable */
	if(pos < 0 && h->is_seekable == 0)
		return -1;

	/* Seek stream */
	if(pos >= 0 && pos < h->buffer_len)
	{
		/* Just move input buffer data */
		size = h->buffer_len - pos;
		memmove(h->buffer, &h->buffer[pos], size);
	}
	else if(h->is_seekable == 0 ||
		(h->http != NULL && pos >= 0 && pos < MAX_SKIP_LEN))
	{
		/* Skip len */
		h->skip_len += pos - h->buffer_len;
	}
	else
	{
		/* Seek in stream */
		if(h->http != NULL)
		{
			/* Prepare request */
			snprintf(range_req, 255, "Range: bytes=%ld-\r\n",
				 h->pos + pos);
			http_set_option(h->http, HTTP_EXTRA_HEADER, range_req,
					0);

			/* Do a new request */
			code = http_get(h->http, h->uri);
			if(code != 200 && code != 206)
				return -1;
		}
		else
		{
			/* Seek in file */
			if(lseek(h->fd, pos - h->buffer_len, SEEK_CUR) !=
			   h->pos + pos)
				return -1;
		}
	}

	/* Update input buffer size and stream position */
	h->pos += pos;
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

	/* Close HTTP client */
	if(h->http != NULL)
		http_close(h->http);

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

	/* Free handle */
	free(h);
}
