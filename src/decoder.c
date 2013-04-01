/*
 * decoder.c - Decoder base
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

#include "decoder.h"
#include "decoder_aac.h"
#include "decoder_mp3.h"

struct decoder_handle {
	struct decoder *dec;
	int (*open)(struct decoder**, void*, void*);
	int (*read)(struct decoder*, float*, size_t);
	int (*close)(struct decoder*);
};

int decoder_open(struct decoder_handle **handle, int codec, void *input_callback, void *user_data)
{
	struct decoder_handle *h;

	if(input_callback == NULL)
		return -1;

	*handle = malloc(sizeof(struct decoder_handle));
	if(*handle == NULL)
		return -1;
	h = *handle;

	if(codec == CODEC_MP3)
	{
		h->open = &decoder_mp3_open;
		h->read = &decoder_mp3_read;
		h->close = &decoder_mp3_close;
	}
	else if(codec == CODEC_AAC)
	{
		h->open = &decoder_aac_open;
		h->read = &decoder_aac_read;
		h->close = &decoder_aac_close;
	}
	else
	{
		h->dec = NULL;
		return -1;
	}

	return h->open(&h->dec, input_callback, user_data);
}

int decoder_read(struct decoder_handle *h, float *buffer, size_t size)
{
	return h->read(h->dec, buffer, size);
}

int decoder_close(struct decoder_handle *h)
{
	if(h == NULL)
		return -1;

	if(h->dec != NULL)
		h->close(h->dec);

	free(h);

	return 0;
}

