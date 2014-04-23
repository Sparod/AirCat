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
#include <sys/stat.h>
#include <pthread.h>

#include "decoder.h"
#include "file.h"

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

struct file_handle {
	/* Handle */
	FILE *fp;
	struct file_format *format;
	struct decoder_handle *dec;
	/* File and stream status */
	unsigned long pos;
	size_t file_pos;
	/* File properties */
	unsigned long samplerate;
	unsigned long channels;
	unsigned long length;
	size_t file_size;
	/* Mutex for thread safe status */
	pthread_mutex_t mutex;
};

/* Callback for decoder */
static int file_read_stream(void * user_data, unsigned char *buffer,
			    size_t size);

int file_open(struct file_handle **handle, const char *name)
{
	struct file_handle *h;
	struct stat st;
	int codec_tab[][2] = {
		{FILE_FORMAT_MPEG, CODEC_MP3},
		{0, 0}
	};
	int codec = -1;
	int i;

	/* Alloc structure */
	*handle = malloc(sizeof(struct file_handle));
	if(*handle == NULL)
		return -1;
	h = *handle;

	/* Init structure */
	h->fp = NULL;
	h->dec = NULL;
	h->pos = 0;
	h->file_pos = 0;

	/* Init mutex */
	pthread_mutex_init(&h->mutex, NULL);

	/* Check file */
	if(stat(name, &st) != 0 || !S_ISREG(st.st_mode))
		return -1;

	/* Get its size */
	h->file_size = st.st_size;

	/* Get file format infos */
	h->format = file_format_parse(name, TAG_ALL);
	if(h->format == NULL || h->format->type == FILE_FORMAT_UNKNOWN)
		/* Unsupported file */
		return -1;

	/* Find good decoder from file type */
	for(i = 0; codec_tab[i][0] != 0; i++)
	{
		if(codec_tab[i][0] == h->format->type)
		{
			codec = codec_tab[i][1];
			break;
		}
	}
	if(codec == -1)
		return -1;

	/* Connect and get header from server */
	h->fp = fopen(name, "rb");
	if(h->fp == NULL)
		return -1;

	/* Open decoder */
	if(decoder_open(&h->dec, codec, &file_read_stream, h) != 0)
		return -1;

	/* Get file properties */
	if(h->format->samplerate == 0 || h->format->channels == 0)
	{
		/* Get information from decoder */
		h->samplerate = decoder_get_samplerate(h->dec);
		h->channels = decoder_get_channels(h->dec);

		/* Calculate stream length */
		h->length = h->file_size * 8 / decoder_get_bitrate(h->dec);
	}
	else
	{
		h->samplerate = h->format->samplerate;
		h->channels = h->format->channels;
		h->length = h->format->length;
	}

	return 0;
}

unsigned long file_get_samplerate(struct file_handle *h)
{
	if(h == NULL)
		return 0;

	return h->samplerate;
}

unsigned char file_get_channels(struct file_handle *h)
{
	if(h == NULL)
		return 0;

	return h->channels;
}

int file_set_pos(struct file_handle *h, int pos)
{
	size_t f_pos;

	if(h == NULL || h->fp == NULL)
		return -1;

	/* Calculate new position */
	f_pos = 0;
	if(f_pos > h->file_size)
		return -1;

	/* Lock file access */
	pthread_mutex_lock(&h->mutex);

	/* Seek in file */
	if(fseek(h->fp, f_pos, SEEK_SET) != 0)
		return -1;

	/* Update position */
	h->file_pos = f_pos;
	h->pos = pos * h->samplerate * h->channels;

	/* Unlock file access */
	pthread_mutex_unlock(&h->mutex);

	return 0;
}

int file_get_pos(struct file_handle *h)
{
	unsigned long pos;

	if(h == NULL)
		return -1;

	/* Lock position access */
	pthread_mutex_lock(&h->mutex);

	pos = h->pos;

	/* Unlock position access */
	pthread_mutex_unlock(&h->mutex);

	return pos / (h->samplerate * h->channels);
}

int file_get_length(struct file_handle *h)
{
	if(h == NULL)
		return -1;

	return h->length;
}

int file_get_status(struct file_handle *h)
{
	if(h == NULL)
		return FILE_NULL;

	if(h->fp == NULL)
		return FILE_CLOSED;

	if(feof(h->fp))
		return FILE_EOF;

	return FILE_OPENED;
}

int file_read(struct file_handle *h, unsigned char *buffer, size_t size)
{
	int ret;

	if(h == NULL)
		return -1;

	/* Read next PCM buffer */
	ret = decoder_read(h->dec, buffer, size);

	/* Update output stream position */
	if(ret > 0)
	{
		/* Lock file access */
		pthread_mutex_lock(&h->mutex);

		h->pos += ret;

		/* Unlock file access */
		pthread_mutex_unlock(&h->mutex);
	}

	return ret;
}

int file_close(struct file_handle *h)
{
	if(h == NULL)
		return 0;

	/* Close decoder */
	if(h->dec != NULL)
		decoder_close(h->dec);

	/* Close file */
	if(h->fp != NULL)
		fclose(h->fp);

	/* Free format */
	if(h->format != NULL)
		file_format_free(h->format);

	free(h);

	return 0;
}

static int file_read_stream(void * user_data, unsigned char *buffer, size_t size)
{
	struct file_handle *h = (struct file_handle*) user_data;
	size_t ret = 0;

	/* Lock file access */
	pthread_mutex_lock(&h->mutex);

	ret = fread(buffer, 1, size, h->fp);
	if(ret <= 0)
	{
		/* End of file: unlock file access */
		pthread_mutex_unlock(&h->mutex);
		return -1;
	}

	/* Update file position */
	h->file_pos += ret;

	/* Unlock file access */
	pthread_mutex_unlock(&h->mutex);

	return ret;
}

