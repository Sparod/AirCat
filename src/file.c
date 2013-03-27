/*
 * file.c - A file input
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

#include "decoder.h"
#include "file.h"

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

struct file_handle {
	FILE *fp;			// File handle
	struct decoder_handle *dec;	// Decoder structure
};

/* Callback for decoder */
static int file_read_stream(unsigned char *buffer, size_t size, void * user_data);

struct file_handle *file_init()
{
	struct file_handle *h;

	/* Alloc structure */
	h = malloc(sizeof(struct file_handle));
	if(h == NULL)
		return NULL;

	return h;
}

int file_open(struct file_handle *h, const char *name)
{
	/* Connect and get header from server */
	h->fp = fopen(name, "rb");
	if(h->fp == NULL)
		return -1;

	/* Detect file format and codec */

	/* Init decoder */
	h->dec = decoder_init(2, &file_read_stream, h);

	/* Open decoder */
	decoder_open(h->dec);

	return 0;
}

int file_read(struct file_handle *h, float *buffer, size_t size)
{
	return decoder_read(h->dec, buffer, size);
}

int file_close(struct file_handle *h)
{
	/* Close decoder */
	decoder_close(h->dec);

	/* Close file */
	fclose(h->fp);

	free(h);

	return 0;
}

static int file_read_stream(unsigned char *buffer, size_t size, void * user_data)
{
	struct file_handle *h = (struct file_handle*) user_data;
	size_t ret = 0;

	ret = fread(buffer, 1, size, h->fp);
	if(ret <= 0) //End of file
		return -1;

	return ret;
}

