/*
 * file.c - A file input
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
#include <pthread.h>

#include "decoder.h"
#include "file.h"

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "file_private.h"

int file_open(struct file_handle **handle, const char *name)
{
	struct file_handle *h;
	struct stat st;
	struct file_type {
		int type;
		struct file_demux *demux;
		int codec;
	} file_tab[3] = {
		{FILE_FORMAT_MPEG, &file_mp3_demux, CODEC_MP3},
		{FILE_FORMAT_AAC, &file_mp4_demux, CODEC_AAC},
		{0, NULL, 0}
	};
	unsigned long dec_samplerate;
	unsigned char dec_channels;
	unsigned long samplerate;
	unsigned char channels;
	int codec = -1;
	int i;

	/* Alloc structure */
	*handle = malloc(sizeof(struct file_handle));
	if(*handle == NULL)
		return -1;
	h = *handle;

	/* Init structure */
	h->fd = -1;
	h->file_pos = 0;
	h->in_size = 0;
	h->dec = NULL;
	h->demux = NULL;
	h->demux_data = NULL;
	h->pos = 0;
	h->pcm_remaining = 0;

	/* Init mutex */
	pthread_mutex_init(&h->mutex, NULL);
	pthread_mutex_init(&h->flush_mutex, NULL);

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
	for(i = 0; file_tab[i].demux != NULL; i++)
	{
		if(file_tab[i].type == h->format->type)
		{
			h->demux = file_tab[i].demux;
			codec = file_tab[i].codec;
			break;
		}
	}
	if(codec == -1 && h->demux == NULL)
		return -1;

	/* Open audio file */
	h->fd = open(name, O_RDONLY);
	if(h->fd < 0)
		return -1;

	/* Init demuxer */
	if(h->demux->init(h, &samplerate, &channels) != 0)
		return -1;

	/* Open decoder */
	if(decoder_open(&h->dec, codec, h->decoder_config,
			h->decoder_config_size, &dec_samplerate, &dec_channels)
	    != 0)
		return -1;

	/* Get file properties */
	if(h->format->samplerate == 0 || h->format->channels == 0)
	{
		/* Get information from decoder */
		h->samplerate = samplerate;
		h->channels = channels;

		/* Calculate stream length */
		//h->length = h->file_size * 8 / decoder_get_bitrate(h->dec);
	}
	else
	{
		h->samplerate = h->format->samplerate;
		h->channels = h->format->channels;
		h->length = h->format->length;
		h->bitrate = h->format->bitrate * 1000;
	}

	/* Samplerate fix: bad samplerate and/or channels in mp4 header */
	if(h->format->type == FILE_FORMAT_AAC &&
	   (dec_samplerate != 0 && dec_samplerate != h->samplerate) ||
	   (dec_channels != 0 && dec_channels != h->channels))
	{
		h->samplerate = dec_samplerate;
		h->channels = dec_channels;
	}

	return 0;
}

/*
 * Read len bytes in file and fill input buffer with it.
 * If len equal to 0, all allocated buffer is filled.
 */
ssize_t file_read_input(struct file_handle *h, size_t len)
{
	ssize_t size;

	if(h == NULL || h->fd < 0)
		return -1;

	/* Fill all buffer */
	if(len == 0)
		len = BUFFER_SIZE;

	/* Read and fill beginning of input buffer */
	size = read(h->fd, h->in_buffer, len);
	if(size < 0)
		return -1;

	/* Update input buffer size and file position */
	h->file_pos += h->in_size;
	h->in_size = size;

	return size;
}

/*
 * Read len bytes more in file and append to end of input buffer.
 * If len equal to 0, all allocated buffer is filled.
 */
ssize_t file_complete_input(struct file_handle *h, size_t len)
{
	ssize_t size;

	if(h == NULL || h->fd < 0)
		return -1;

	/* Fill all buffer */
	if(len == 0 || len + h->in_size > BUFFER_SIZE)
		len = BUFFER_SIZE - h->in_size;

	/* Read and append to input buffer */
	size = read(h->fd, &h->in_buffer[h->in_size], len);
	if(size <= 0)
		return -1;

	/* Update input buffer size */
	h->in_size += len;

	return size;
}

/*
 * Move input buffer to fit with file position passed. Input buffer position is
 * the moved and file position is updated.
 * /!\ Input buffer is not filled with any data. A call to file_read_input() or
 *     to file_complete_input() must be done is more data is needed.
 */
int file_seek_input(struct file_handle *h, unsigned long pos, int whence)
{
	unsigned long p;
	size_t size = 0;

	if(h == NULL || h->fd < 0)
		return -1;

		/* Move pos bytes from current file position */
	if(whence == SEEK_CUR)
		pos += h->file_pos;

	if(pos >= h->file_pos && pos < h->file_pos + h->in_size)
	{
		/* Just move input buffer data */
		p = pos - h->file_pos;
		size = h->in_size - p;
		memmove(h->in_buffer, &h->in_buffer[p], size);
	}
	else if(pos != h->file_pos + h->in_size)
	{
		/* Seek in file */
		if(lseek(h->fd, pos, SEEK_SET) != pos)
			return -1;
	}

	/* Update input buffer size and file position */
	h->file_pos = pos;
	h->in_size = size;

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
	if(h == NULL)
		return -1;

	/* Lock file access */
	pthread_mutex_lock(&h->mutex);

	h->demux->set_pos(h, pos);

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

	if(h->fd < 0)
		return FILE_CLOSED;

/*	if(feof(h->fp))
		return FILE_EOF;*/

	return FILE_OPENED;
}

int file_read(struct file_handle *h, unsigned char *buffer, size_t size)
{
	struct decoder_info info;
	int total_samples = 0;
	int samples;
	int len;

	if(h == NULL)
		return -1;

	/* Lock decoder access */
	pthread_mutex_lock(&h->flush_mutex);

	/* Process remaining pcm data */
	if(h->pcm_remaining > 0)
	{
		/* Get remaining pcm data from decoder */
		samples = decoder_decode(h->dec, NULL, 0, buffer, size, &info);
		if(samples < 0)
		{
			/* Unlock decoder access */
			pthread_mutex_unlock(&h->flush_mutex);

			return -1;
		}

		h->pcm_remaining -= samples;
		total_samples += samples;
	}

	/* Fill output buffer */
	while(total_samples < size)
	{
		/* Get next frame */
		len = h->demux->get_next_frame(h);

		/* Decode next frame */
		samples = decoder_decode(h->dec, h->in_buffer, h->in_size,
					 &buffer[total_samples * 4],
					 size - total_samples, &info);
		if(samples <= 0)
			break;

		/* Move input buffer to next frame */
		if(info.used < h->in_size)
		{
			memmove(h->in_buffer, &h->in_buffer[info.used],
				h->in_size - info.used);
			h->in_size -=  info.used;
		}
		else
		{
			h->in_size = 0;
		}

		/* Update remaining counter */
		h->pcm_remaining = info.remaining;
		total_samples += samples;
	}

	h->pos += total_samples;

	/* Unlock decoder access */
	pthread_mutex_unlock(&h->flush_mutex);

	/* End of stream */
	if(len < 0 && total_samples == 0)
		return -1;

	return total_samples;
}

int file_close(struct file_handle *h)
{
	if(h == NULL)
		return 0;

	/* Close decoder */
	if(h->dec != NULL)
		decoder_close(h->dec);

	/* Free demuxer */
	if(h->demux != NULL)
		h->demux->free(h);

	/* Close file */
	if(h->fd >= 0)
		close(h->fd);

	/* Free format */
	if(h->format != NULL)
		file_format_free(h->format);

	free(h);

	return 0;
}
