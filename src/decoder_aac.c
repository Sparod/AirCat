/*
 * decoder_aac.c - A AAC Decoder based on faad2
 *
 * /!\ EXPERIMENTAL /!\
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
#include <neaacdec.h>

#include "decoder_aac.h"

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#define BUFFER_SIZE 8192

#if FAAD_MIN_STREAMSIZE*8 > BUFFER_SIZE
#error "Buffer size for AAC decoder is too small!"
#endif

struct decoder {
	NeAACDecHandle hDec;
	/* Read Callback */
	int (*input_callback)(void *, unsigned char *, size_t);
	void *user_data;
	/* Input buffer */
	unsigned char buffer[BUFFER_SIZE];
	unsigned long buffer_pos;
	unsigned long buffer_size;
	/* Output buffer */
	unsigned char *pcm_buffer;
	unsigned long pcm_length;
	unsigned long pcm_remain;
	/* Infos */
	unsigned long samplerate;
	unsigned char nb_channel;
};

static long decoder_aac_fill(struct decoder *dec, unsigned long bytes);
static long decoder_aac_fill_output(struct decoder *dec, unsigned char *output_buffer, size_t output_size);

/* FIXME */
static int decoder_aac_sync(struct decoder *dec)
{
	while(1)
	{
		/* Look for a ADTS header */
		if(dec->buffer[dec->buffer_pos] == 0xFF && (dec->buffer[dec->buffer_pos+1] & 0xF6) == 0xF0)
		{
			/* Check if syncword on next frame with calculated frame length */
			size_t frame_length = ((((unsigned int)dec->buffer[dec->buffer_pos+3] & 0x3)) << 11) | (((unsigned int)dec->buffer[dec->buffer_pos+4]) << 3) | (dec->buffer[dec->buffer_pos+5] >> 5);
			decoder_aac_fill(dec, 0);
			if(dec->buffer[dec->buffer_pos+frame_length] == 0xFF) 
			{
				decoder_aac_fill(dec, 0);
				return 0;
			}
		}

		decoder_aac_fill(dec, 1);
	}
}

int decoder_aac_open(struct decoder **decoder, void *input_callback, void *user_data)
{
	NeAACDecConfigurationPtr config;
	NeAACDecFrameInfo frameInfo;
	struct decoder *dec;

	*decoder = malloc(sizeof(struct decoder));
	if(*decoder == NULL)
		return -1;
	dec = *decoder;

	dec->input_callback = input_callback;
	dec->user_data = user_data;

	dec->buffer_size = 0;
	dec->buffer_pos = 0;

	/* Initialize faad */
	dec->hDec = NeAACDecOpen();

	/* Set the default object type and samplerate */
	config = NeAACDecGetCurrentConfiguration(dec->hDec);
#ifdef USE_FLOAT
	config->outputFormat = FAAD_FMT_FLOAT;
#else
	config->outputFormat = FAAD_FMT_32BIT;
#endif
	NeAACDecSetConfiguration(dec->hDec, config);

	/* PCM data remaining in output buffer */
	dec->pcm_remain = 0;

	/* FIXME: Sync with AAC header */
	decoder_aac_fill(dec, 0);
	decoder_aac_sync(dec);

	/* Parse first frame to init decoder */
	if(NeAACDecInit(dec->hDec, &dec->buffer[dec->buffer_pos], dec->buffer_size, &dec->samplerate, &dec->nb_channel) < 0)
	{
		NeAACDecClose(dec->hDec);
		return 1;
	}

	/* Decode first frame and get informations */
	NeAACDecDecode(dec->hDec, &frameInfo, &dec->buffer[dec->buffer_pos], dec->buffer_size);

	return 0;
}

unsigned long decoder_aac_get_samplerate(struct decoder* dec)
{
	return dec->samplerate;
}

unsigned char decoder_aac_get_channels(struct decoder *dec)
{
	return dec->nb_channel;
}

/* FIXME */
static long decoder_aac_fill(struct decoder *dec, unsigned long bytes)
{
	size_t size = 0;
	size_t remaining = 0;
	long bread = 0;

	if(dec->buffer_size == 0)
	{
		remaining = 0;
		size = BUFFER_SIZE;
	}
	else
	{
		dec->buffer_pos += bytes;
		dec->buffer_size -= bytes;
		if(dec->buffer_size >= FAAD_MIN_STREAMSIZE*2)
			return 0;

		remaining = dec->buffer_size;
		size = dec->buffer_pos;
		memmove(dec->buffer, &dec->buffer[size], remaining);
	}

	dec->buffer_pos = 0;

	/* Read data from callback */
	while(dec->buffer_size < FAAD_MIN_STREAMSIZE*2)
	{
		bread = dec->input_callback(dec->user_data, &dec->buffer[remaining], size);
		if(bread < 0)
			return -1;
		dec->buffer_size += bread;
		remaining += bread;
		size -= bread;		
	}

	return 0;
}

static long decoder_aac_fill_output(struct decoder *dec, unsigned char *output_buffer, size_t output_size)
{
	unsigned long pos;
	unsigned long size;

	pos = dec->pcm_length-dec->pcm_remain;
	if(output_size < dec->pcm_remain)
		size = output_size;
	else
		size = dec->pcm_remain;

	/* Copy samples to output buffer */
	if(dec->nb_channel > 2) /* Specific case of surround file */
	{
		/* Transform:
		 * From (3 channels) FC , FL , FR
		 *      (4 channels) FC , FL , FR , BC
		 *      (5 channels) FC , FL , FR , BL , BR
		 *      (6 channels) FC , FL , FR , BL , BR , LFE
		 *      (8 channels) FC , FL , FR , SL , SR , BL , BR , LFE
		 *      To    ->     FL , FR , SL , SR , BL , BR , FC , LFE
		 */
		/* TODO */
	}
	else
#ifdef USE_FLOAT
	memcpy(output_buffer, &dec->pcm_buffer[pos*4], size*4); // 32-bit wide sample (float or 32-bit fixed)
#else
	{
		int32_t *p_in = (int32_t*) &dec->pcm_buffer[pos*4];
		int32_t *p_out = (int32_t*) output_buffer;
		size_t i;
		for(i = 0; i < size; i++)
		{
			*(p_out++) = *p_in == 0x80000000 ? 0x7FFFFFFF : *p_in;
			p_in++;
		}
	}
#endif

	dec->pcm_remain -= size;

	return size;
}

int decoder_aac_read(struct decoder *dec, unsigned char *output_buffer, size_t output_size)
{
	NeAACDecFrameInfo frameInfo;
	unsigned short size = 0;

	/* Empty remaining PCM before decoding another frame */
	if(dec->pcm_remain > 0)
	{
		size = decoder_aac_fill_output(dec, output_buffer, output_size);
		if(dec->pcm_remain > 0 || size == output_size)
			return size;
	}

	/* FIXME: */
	/* Decode a new frame */
	dec->pcm_buffer = (unsigned char*) NeAACDecDecode(dec->hDec, &frameInfo, &dec->buffer[dec->buffer_pos], dec->buffer_size);

        if (frameInfo.error > 0)
        {
		return 0;
        }

	if(frameInfo.samples == 0)
	{
		dec->pcm_buffer = NeAACDecDecode(dec->hDec, &frameInfo, &dec->buffer[dec->buffer_pos], dec->buffer_size);
		return 0;
	}

	if(dec->pcm_buffer == NULL)
		return 0;

	/* Update buffer */
	decoder_aac_fill(dec, frameInfo.bytesconsumed);

	/* Fill output buffer with PCM */
	dec->pcm_remain = frameInfo.samples;
	dec->pcm_length = frameInfo.samples;
	size += decoder_aac_fill_output(dec, &output_buffer[size], output_size-size);

	return size;
}

int decoder_aac_close(struct decoder *dec)
{
	/* Close mad */
	NeAACDecClose(dec->hDec);

	if(dec != NULL)
		free(dec);

	return 0;
}

struct decoder_handle decoder_aac = {
	.dec = NULL,
	.open = &decoder_aac_open,
	.get_samplerate = &decoder_aac_get_samplerate,
	.get_channels = &decoder_aac_get_channels,
	.read = &decoder_aac_read,
	.close = &decoder_aac_close,
};

