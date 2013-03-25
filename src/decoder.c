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
#include "decoder_mp3.h"

struct decoder_handle {
	struct decoder *dec;
	int (*open)(struct decoder*);
	int (*read)(struct decoder*, float*, size_t);
	int (*close)(struct decoder*);
};

struct decoder_handle *decoder_init(int codec, void *input_callback, void *user_data)
{
	struct decoder_handle *h;

	if(input_callback == NULL)
		return NULL;

	h = malloc(sizeof(struct decoder_handle));
	if(h == NULL)
		return NULL;

	if(codec == CODEC_MP3)
	{
		h->dec = decoder_mp3_init(input_callback, user_data);
		h->open = &decoder_mp3_open;
		h->read = &decoder_mp3_read;
		h->close = &decoder_mp3_close;
	}
	else
		h->dec = NULL;

	return h;
}

int decoder_open(struct decoder_handle *h)
{
	return h->open(h->dec);
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

