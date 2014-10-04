/*
 * fs_http.c - An HTTP(S) implementation for FS
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
//#include <unistd.h>

#include "fs_http.h"
#include "http.h"

struct fs_http_handle {
	struct http_handle *http;
	int is_seekable;
	size_t size;
	long skip_len;
	long pos;
	char *url;
};

void fs_http_init(void)
{
	return;
}

void fs_http_free(void)
{
	return;
}

static int fs_http_open(struct fs_file *f, const char *url, int flags,
			mode_t mode)
{
	struct fs_http_handle *h;
	struct http_handle *http;
	const char *ext;
	int code;

	/* Create a new HTTP client */
	if(http_open(&http, 1) != 0)
		goto error;

	/* Prepare request with seek option */
	http_set_option(http, HTTP_EXTRA_HEADER, "Range: bytes=0-\r\n", 0);

	/* Do request */
	code = http_get(http, url);
	if(code != 200 && code != 206)
		goto error;

	/* Allocate a new handle */
	h = malloc(sizeof(struct fs_http_handle));
	if(h == NULL)
		goto error;

	/* Init structure */
	f->data = (void*) h;
	h->url = strdup(url);
	h->http = http;
	h->skip_len = 0;
	h->is_seekable = 0;
	h->size = 0;
	h->pos = 0;

	/* Get accepted range */
	ext = http_get_header(h->http, "Accept-Ranges", 0);
	if(ext != NULL && strncmp(ext, "bytes", 5) == 0)
		h->is_seekable = 1;

	/* Get file length */
	ext = http_get_header(http, "Content-Length", 0);
	if(ext != NULL)
		h->size = strtoul(ext, NULL, 10);

	return 0;
error:
	/* Close HTTP client */
	http_close(http);
	return -1;
}

static int fs_http_creat(struct fs_file *f, const char *url, mode_t mode)
{
	/* Write is not yet implemented */
	return -1;
}

static ssize_t fs_http_read_to(struct fs_file *f, void *buf, size_t count,
			       long timeout)
{
	struct fs_http_handle *h;
	ssize_t len;

	if(f == NULL || f->data == NULL)
		return -1;
	h = f->data;

	/* Skip len */
	while(h->skip_len > 0)
	{
		/* Read data */
		len = h->skip_len > count ? count : h->skip_len;
		len = http_read_timeout(h->http, buf, len, timeout);
		if(len <= 0)
			return len;
		h->skip_len -= len;
	}

	/* Read content */
	len = http_read_timeout(h->http, buf, count, timeout);

	/* Update current position */
	if(len > 0)
		h->pos += len;

	return len;
}

static ssize_t fs_http_read(struct fs_file *f, void *buf, size_t count)
{
	return fs_http_read_to(f, buf, count, -1);
}

static ssize_t fs_http_write_to(struct fs_file *f, const void *buf,
				size_t count, long timeout)
{
	/* Write is not yet implemented */
	return -1;
}

static ssize_t fs_http_write(struct fs_file *f, const void *buf, size_t count)
{
	/* Write is not yet implemented */
	return -1;
}

static off_t fs_http_lseek(struct fs_file *f, off_t offset, int whence)
{
	struct fs_http_handle *h;
	char req[256];
	int code;

	if(f == NULL || f->data == NULL)
		return -1;
	h = f->data;

	/* Calculate offset from beginning of file */
	if(whence == SEEK_CUR)
	{
		offset += h->pos;
	}
	else if(whence == SEEK_END)
	{
		if(h->size == 0)
			return -1;
		offset -= h->size;
	}

	/* Seek to desired position */
	if(h->is_seekable)
	{
		/* Prepare a request with new location */
		snprintf(req, 255, "Range: bytes=%ld-\r\n", offset);
		http_set_option(h->http, HTTP_EXTRA_HEADER, req, 0);

		/* Do a new request */
		code = http_get(h->http, h->url);
		if(code != 200 && code != 206)
			return -1;
	}
	else
	{
		/* Skip diff between current position and asked position */
		h->skip_len = offset - h->pos;
		if(h->skip_len < 0)
		{
			/* Free extra header */
			http_set_option(h->http, HTTP_EXTRA_HEADER, NULL, 0);

			/* Do a new request */
			code = http_get(h->http, h->url);

			/* Update length to skip */
			h->skip_len = offset;
		}
	}

	/* Update position */
	h->pos = offset;

	return h->pos;
}

static int fs_http_ftruncate(struct fs_file *f, off_t length)
{
	/* Write is not yet implemented */
	return -1;
}

static void fs_http_close(struct fs_file *f)
{
	struct fs_http_handle *h;

	if(f == NULL || f->data == NULL)
		return;
	h = f->data;

	/* Close HTTP client */
	http_close(h->http);

	/* Free URL */
	if(h->url != NULL)
		free(h->url);

	/* Free local handle */
	free(h);
}

static int fs_http_opendir(struct fs_dir *d, const char *url)
{
	/* Directory not yet implemented */
	return 0;
}

static int fs_http_mount(struct fs_dir *d)
{
	/* No mount or network scan */
	return 0;
}

static struct fs_dirent *fs_http_readdir(struct fs_dir *d)
{
	/* Directory not yet implemented */
	return NULL;
}

static off_t fs_http_telldir(struct fs_dir *d)
{
	/* Directory not yet implemented */
	return 0;
}

static void fs_http_closedir(struct fs_dir *d)
{
	/* Directory not yet implemented */
	return;
}

static int fs_http_stat(const char *url, struct stat *buf)
{
	/* Not yet implemented */
	return 0;
}

static int fs_http_fstat(struct fs_file *f, struct stat *buf)
{
	struct fs_http_handle *h;

	if(f == NULL || f->data == NULL)
		return -1;
	h = f->data;

	/* Fill stat */
	buf->st_mode = S_IFREG & S_IRUSR & S_IRGRP & S_IROTH;
	buf->st_size = h->size;
	buf->st_blksize = 512;
	buf->st_blocks = h->size / 512;

	return 0;
}

static int fs_http_statvfs(const char *url, struct statvfs *buf)
{
	/* Not yet implemented */
	return 0;
}

static int fs_http_fstatvfs(struct fs_file *f, struct statvfs *buf)
{
	/* No information are available */
	memset(buf, 0, sizeof(struct statvfs));

	return 0;
}

static int fs_http_fake(void)
{
	/* Function is not yet implemented */
	return 0;
}

struct fs_handle fs_http = {
	.open = fs_http_open,
	.creat = fs_http_creat,
	.read = fs_http_read,
	.read_to = fs_http_read_to,
	.write = fs_http_write,
	.write_to = fs_http_write_to,
	.lseek = fs_http_lseek,
	.ftruncate = fs_http_ftruncate,
	.close = fs_http_close,
	.mkdir = (void*)fs_http_fake,
	.unlink = (void*)fs_http_fake,
	.rmdir = (void*)fs_http_fake,
	.rename = (void*)fs_http_fake,
	.chmod = (void*)fs_http_fake,
	.opendir = fs_http_opendir,
	.mount = fs_http_mount,
	.readdir = fs_http_readdir,
	.telldir = fs_http_telldir,
	.closedir = fs_http_closedir,
	.stat = fs_http_stat,
	.fstat = fs_http_fstat,
	.statvfs = fs_http_statvfs,
	.fstatvfs = fs_http_fstatvfs,
};

