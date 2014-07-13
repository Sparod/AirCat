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

#include "demux_mp3.h"
#include "demux_mp4.h"
#include "demux.h"

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

int demux_open(struct demux_handle **handle, struct stream_handle *stream,
	       unsigned long *samplerate, unsigned char *channels)
{
	struct demux_handle *h;
	const char *type;

	/* Check input stream */
	if(stream == NULL)
		return -1;

	/* Allocate handle */
	*handle = malloc(sizeof(struct demux_handle));
	if(*handle == NULL)
		return -1;
	h = *handle;

	/* Init handle */
	memset(h, 0, sizeof(struct demux_handle));

	/* Find demuxer */
	type = stream_get_content_type(stream);
	if(strcmp(type, "audio/mpeg") == 0)
		memcpy(h, &demux_mp3, sizeof(struct demux_handle));
	else if(strcmp(type, "audio/mp4") == 0)
		memcpy(h, &demux_mp4, sizeof(struct demux_handle));
	else
		return -1;

	/* Check if open() is available */
	if(h->open == NULL)
		return 0;

	/* Open demuxer */
	return h->open(&h->demux, stream, samplerate, channels);
}

struct file_format *demux_get_format(struct demux_handle *h)
{
	if(h == NULL || h->demux == NULL || h->get_format == NULL)
		return NULL;
	return h->get_format(h->demux);
}

int demux_get_dec_config(struct demux_handle *h, int *codec,
			 const unsigned char **dec_config,
			 size_t *dec_config_size)
{
	if(h == NULL || h->demux == NULL || h->get_dec_config == NULL)
		return -1;
	return h->get_dec_config(h->demux, codec, dec_config, dec_config_size);
}

ssize_t demux_next_frame(struct demux_handle *h)
{
	if(h == NULL || h->demux == NULL || h->next_frame == NULL)
		return -1;
	return h->next_frame(h->demux);
}

void demux_set_used(struct demux_handle *h, size_t len)
{
	if(h == NULL || h->demux == NULL || h->set_used == NULL)
		return;
	h->set_used(h->demux, len);
}

unsigned long demux_set_pos(struct demux_handle *h, unsigned long pos)
{
	if(h == NULL || h->demux == NULL || h->set_pos == NULL)
		return -1;
	return h->set_pos(h->demux, pos);
}

void demux_close(struct demux_handle *h)
{
	if(h == NULL)
		return;

	/* Close demuxer */
	if(h->demux != NULL && h->close != NULL)
		h->close(h->demux);

	/* Free handle */
	free(h);
}
