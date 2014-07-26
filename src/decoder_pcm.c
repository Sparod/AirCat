/*
 * decoder_pcm.c - A PCM Decoder
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
 *
 * Inspired of:
 * ALAC (Apple Lossless Audio Codec) decoder
 * Copyright (c) 2005 David Hammerton
 * http://crazney.net/programs/itunes/alac.html
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "decoder_pcm.h"

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#define BUFFER_SIZE 8192

#define FROM_8(b)  (int32_t) ((b)[0] << 24 & 0xFF000000)
#define FROM_16(b) (int32_t) FROM_8(b) | ((b)[1] << 16 & 0x00FF0000)
#define FROM_24(b) (int32_t) FROM_16(b) | ((b)[2] << 8 & 0x0000FF00)
#define FROM_32(b) (int32_t) FROM_24(b) | ((b)[3] & 0x000000FF)

struct decoder {
	/* Input format */
	unsigned long samplerate;
	unsigned char channels;
	unsigned char bytes;
	unsigned char bits;
	/* PCM output */
	unsigned char buffer[BUFFER_SIZE];
	unsigned long pcm_length;
	unsigned long pcm_remain;
};

static int decoder_pcm_parse_riff(struct decoder *dec, const char *conf,
				  size_t size);

int decoder_pcm_open(struct decoder **decoder, const unsigned char *config,
		     size_t config_size, unsigned long *samplerate,
		     unsigned char *channels)
{
	struct decoder *dec;

	/* Alloc structure */
	*decoder = malloc(sizeof(struct decoder));
	if(*decoder == NULL)
		return -1;
	dec = *decoder;

	/* Init decoder structure */
	dec->pcm_length = 0;
	dec->pcm_remain = 0;

	/* Decode configuration */
	if(decoder_pcm_parse_riff(dec, (char*) config, config_size) != 0)
	{
		/* Set default configuration */
		dec->samplerate = 44100;
		dec->channels = 2;
		dec->bytes = 2;
		dec->bits = 16;
	}

	/* Get samplerate and channels */
	if(samplerate != NULL)
		*samplerate = dec->samplerate;
	if(channels != NULL)
		*channels = dec->channels;

	return 0;
}

static int decoder_pcm_parse_riff(struct decoder *dec, const char *config,
				  size_t size)
{
	/* Check header */
	if(config == NULL || size < 44 || strncmp(config, "RIFF", 4) != 0 ||
	   strcmp(config+12, "fmt") != 0 || config[21] != 1)
		return -1;

	/* Get expected values */
	dec->channels = config[23];
	dec->samplerate = FROM_32(&config[24]);
	dec->bits = config[35];
	dec->bytes = dec->bits / 8;

	return 0;
}

#ifdef USE_FLOAT
	#define CONV8(b)  (float)(FROM_8(b)) / 0x7fffffff
	#define CONV16(b) (float)(FROM_16(b)) / 0x7fffffff
	#define CONV24(b) (float)(FROM_24(b)) / 0x7fffffff
	#define CONV32(b) (float)(FROM_32(b)) / 0x7fffffff
#else
	#define CONV8(b)  FROM_8(b)
	#define CONV16(b) FROM_16(b)
	#define CONV24(b) FROM_24(b)
	#define CONV32(b) FROM_32(b)
#endif

static long decoder_pcm_fill_output(struct decoder *dec,
				    unsigned char *output_buffer,
				    size_t output_size)
{
#ifdef USE_FLOAT
	float *p = (float*) output_buffer;
#else
	int32_t *p = (int32_t*) output_buffer;
#endif 
	unsigned long pos;
	unsigned long size;
	int i;

	/* Calculate position and size to exract from buffer */
	pos = (dec->pcm_length - dec->pcm_remain) * dec->bytes;

	/* Calculate size to return */
	if(output_size > dec->pcm_remain)
		output_size = dec->pcm_remain;
	size = (output_size * dec->bytes) + pos;

	/* Convert samples to 32bit */
	switch(dec->bits)
	{
		case 32:
			for(i = pos; i < size; i += 4)
				*p++ = CONV32(&dec->buffer[i]);
			break;
		case 24:
			for(i = pos; i < size; i += 3)
				*p++ = CONV24(&dec->buffer[i]);
			break;
		case 16:
			for(i = pos; i < size; i += 2)
				*p++ = CONV16(&dec->buffer[i]);
			break;
		case 8:
			for(i = pos; i < size; i++)
				*p++ = CONV8(&dec->buffer[i]);
			break;
		default:
			;
	}

	dec->pcm_remain -= output_size;

	return output_size;
}

int decoder_pcm_decode(struct decoder *dec, unsigned char *in_buffer,
		       size_t in_size, unsigned char *out_buffer,
		       size_t out_size, struct decoder_info *info)
{
	int size = 0;

	/* Reset position of PCM output buffer */
	if(in_buffer == NULL && out_buffer == NULL)
	{
		if(out_size > dec->pcm_length)
			out_size = dec->pcm_length;
		dec->pcm_remain = dec->pcm_length - out_size;
		return out_size;
	}

	/* Empty remaining PCM before decoding another frame */
	if(dec->pcm_remain > 0 || in_buffer == NULL)
	{
		size = decoder_pcm_fill_output(dec, out_buffer, out_size);

		/* Fill decoder info */
		info->used = 0;
		info->remaining = dec->pcm_remain;
		info->samplerate = dec->samplerate;
		info->channels = dec->channels;

		return size;
	}

	/* Check input size */
	if(in_size == 0)
		return 0;

	/* Copy frame */
	if(in_size > BUFFER_SIZE)
		in_size = BUFFER_SIZE;
	memcpy(dec->buffer, in_buffer, in_size);

	/* Fill output buffer with PCM */
	dec->pcm_remain = in_size / dec->bytes;
	dec->pcm_length = dec->pcm_remain;

	size = decoder_pcm_fill_output(dec, out_buffer, out_size);

	/* Fill decoder info */
	info->used = in_size;
	info->remaining = dec->pcm_remain;
	info->samplerate = dec->samplerate;
	info->channels = dec->channels;

	return size;
}

int decoder_pcm_close(struct decoder *dec)
{
	if(dec == NULL)
		return 0;

	/* Free decoder */
	free(dec);

	return 0;
}

struct decoder_handle decoder_pcm = {
	.dec = NULL,
	.open = &decoder_pcm_open,
	.decode = &decoder_pcm_decode,
	.close = &decoder_pcm_close,
};
