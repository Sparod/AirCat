/*
 * decoder.c - Decoder base
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

#include "decoder.h"
#include "decoder_alac.h"
#include "decoder_aac.h"
#include "decoder_mp3.h"

int decoder_open(struct decoder_handle **handle, int codec,
		 unsigned char *buffer, size_t len, unsigned long *samplerate,
		 unsigned char *channels)
{
	struct decoder_handle *h;

	/* Alloc structure */
	*handle = malloc(sizeof(struct decoder_handle));
	if(*handle == NULL)
		return -1;
	h = *handle;

	if(codec == CODEC_ALAC)
	{
		memcpy(h, &decoder_alac, sizeof(struct decoder_handle));
	}
	else if(codec == CODEC_MP3)
	{
		memcpy(h, &decoder_mp3, sizeof(struct decoder_handle));
	}
	else if(codec == CODEC_AAC)
	{
		memcpy(h, &decoder_aac, sizeof(struct decoder_handle));
	}
	else
	{
		h->dec = NULL;
		return -1;
	}

	return h->open(&h->dec, buffer, len, samplerate, channels);
}

int decoder_decode(struct decoder_handle *h, unsigned char *in_buffer,
		   size_t in_size, unsigned char *out_buffer,
		   size_t out_size, struct decoder_info *info)
{
	if(h == NULL || h->dec == NULL)
		return -1;

	return h->decode(h->dec, in_buffer, in_size, out_buffer, out_size,
			 info);
}

int decoder_close(struct decoder_handle *h)
{
	if(h == NULL)
		return 0;

	if(h->dec != NULL)
		h->close(h->dec);

	free(h);

	return 0;
}

