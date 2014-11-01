/*
 * demux.c - An input demuxer
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

#include "demux_mp3.h"
#include "demux_mp4.h"
#include "demux.h"
#include "vring.h"
#include "fs.h"

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

struct demux_handle {
	/* Demuxer handler */
	struct demux_module module;
	struct demux *demux;
	/* File handler */
	struct fs_file *file;
	size_t file_size;
	/* Frame ring buffer */
	struct vring_handle *ring;
	/* Current frame */
	unsigned char *frame_data;
	size_t frame_pos;
	size_t frame_len;
	/* Stream position in ring buffer */
	off_t start_pos;
	off_t end_pos;
};

int demux_open(struct demux_handle **handle, const char *uri,
	       unsigned long *samplerate, unsigned char *channels,
	       size_t cache_size)
{
	struct demux_handle *h;
	struct demux_module *d;
	struct fs_file *file;
	struct stat st;
	const char *ext;
	int len;

	/* Check file URI */
	if(uri == NULL || (len = strlen(uri)) < 4)
		return -1;

	/* Get content type from file extension */
	ext = &uri[len-4];
	if(strcasecmp(ext, ".mp3") == 0)
		d = &demux_mp3;
	else if(strcasecmp(ext, ".m4a") == 0 || strcasecmp(ext, ".mp4") == 0)
		d = &demux_mp4;
	else
		return -1;

	/* Open file */
	file = fs_open(uri, O_RDONLY, 0);
	if(file == NULL)
		return -1;

	/* Allocate handle */
	*handle = malloc(sizeof(struct demux_handle));
	if(*handle == NULL)
		return -1;
	h = *handle;

	/* Init handle */
	memset(h, 0, sizeof(struct demux_handle));
	memcpy(&h->module, d, sizeof(struct demux_module));
	h->file = file;

	/* Get file size */
	if(fs_fstat(file, &st) == 0)
		h->file_size = st.st_size;

	/* Open demuxer */
	if(h->module.open(&h->demux, file, h->file_size, samplerate, channels)
	   != 0)
		return -1;

	/* Allocate vring buffer */
	if(vring_open(&h->ring, cache_size, 8192) != 0)
		return -1;

	return 0;
}

struct meta *demux_get_meta(struct demux_handle *h)
{
	if(h == NULL || h->demux == NULL || h->module.get_meta == NULL)
		return NULL;

	return h->module.get_meta(h->demux);
}

int demux_get_dec_config(struct demux_handle *h, int *codec,
			 const unsigned char **dec_config,
			 size_t *dec_config_size)
{
	if(h == NULL || h->demux == NULL || h->module.get_dec_config == NULL)
		return -1;

	return h->module.get_dec_config(h->demux, codec, dec_config,
					dec_config_size);
}

static ssize_t demux_fill_buffer(struct demux_handle *h)
{
	struct demux_frame *frame;
	ssize_t size, len;

	/* Get writting buffer from ring buffer */
	size = vring_write(h->ring, (unsigned char**) &frame);
	if(size <= 0)
		return 0;

	/* Try to get next frame from stream */
	len = h->module.next_frame(h->demux, frame, size);
	if(len <= 0)
		return len;

	/* Update ring buffer */
	len = vring_write_forward(h->ring,
				  frame->len + sizeof(struct demux_frame));
	if(len > 0)
		h->end_pos = frame->pos + frame->len;

	return len;
}

ssize_t demux_get_frame(struct demux_handle *h, unsigned char **buffer)
{
	if(h == NULL || buffer == NULL)
		return -1;

	/* All frame has been used: get next frame */
	if(h->frame_pos >= h->frame_len)
		return demux_get_next_frame(h, buffer);

	/* Fill ring buffer */
	demux_fill_buffer(h);

	/* Return frame */
	*buffer = h->frame_data + h->frame_pos;

	return h->frame_len - h->frame_pos;
}

void demux_set_used_frame(struct demux_handle *h, ssize_t len)
{
	if(h == NULL)
		return;

	/* Calculate new frame position */
	h->frame_pos += len;
	if(len < 0 && h->frame_pos > h->frame_len)
		h->frame_pos = 0;
}

ssize_t demux_get_next_frame(struct demux_handle *h, unsigned char **buffer)
{
	struct demux_frame *f;
	ssize_t fill_len;
	ssize_t len;

	/* Fill ring buffer */
	fill_len = demux_fill_buffer(h);

	/* Go to next frame in ring buffer */
	if(h->frame_len > 0)
	{
		/* Update start position in ring buffer: it is an estimation
		 * since a gap between two consecutive frames in stream is
		 * possible.
		 */
		h->start_pos += h->frame_len;

		/* Forward to next frame */
		vring_read_forward(h->ring,
				   h->frame_len + sizeof(struct demux_frame));
		h->frame_data = NULL;
		h->frame_len = 0;
	}

	/* Get new frame */
	len = vring_read(h->ring, (unsigned char **) &f, 0, 0);
	if(len == 0 && fill_len < 0)
		return -1;
	else if(len < sizeof(struct demux_frame))
		return 0;

	/* Check that enough data are available */
	if(f->len + sizeof(struct demux_frame) > len)
		return 0;

	/* Update current frame */
	h->frame_data = f->data;
	h->frame_len = f->len;
	h->frame_pos = 0;
	h->start_pos += f->pos;

	/* Return next frame */
	*buffer = f->data;
	return f->len;
}

unsigned long demux_set_pos(struct demux_handle *h, unsigned long pos)
{
	struct demux_frame *frame;
	unsigned long new_pos;
	off_t stream_pos;
	size_t buffer_len;
	size_t len = 0;

	/* Get stream position for time position */
	new_pos = h->module.calc_pos(h->demux, pos, &stream_pos);

	/* New position is not in ring buffer */
	if(stream_pos < h->start_pos || stream_pos >= h->end_pos)
	{
		/* Flush ring buffer */
		vring_read_forward(h->ring, vring_get_length(h->ring));
		h->frame_data = NULL;
		h->frame_len = 0;
		h->start_pos = 0;
		h->end_pos = 0;

		/* Go to new position */
		return h->module.set_pos(h->demux, pos);
	}

	/* Get buffer length */
	buffer_len = vring_get_length(h->ring);

	/* Find frame */
	while(len < buffer_len)
	{
		/* Get frame */
		vring_read(h->ring, (unsigned char **)&frame,
			   sizeof(struct demux_frame), len);
		if(stream_pos >= frame->pos &&
		   stream_pos < frame->pos + frame->len)
			break;

			/* Go to next frame */
		len += frame->len + sizeof(struct demux_frame);
	}

	/* Frame not found */
	if(len >= buffer_len)
		len = buffer_len;

	/* Move to this frame */
	vring_read_forward(h->ring, len);
	h->start_pos = frame->pos;
	h->frame_data = NULL;
	h->frame_len = 0;

	return new_pos;
}

void demux_close(struct demux_handle *h)
{
	if(h == NULL)
		return;

	/* Close demuxer */
	if(h->demux != NULL)
		h->module.close(h->demux);

	/* Close file */
	if(h->file != NULL)
		fs_close(h->file);

	/* Close ring buffer */
	if(h->ring != NULL)
		vring_close(h->ring);

	/* Free handle */
	free(h);
}
