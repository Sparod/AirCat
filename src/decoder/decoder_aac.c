/*
 * decoder_aac.c - A AAC Decoder based on faad2
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
#include <stdint.h>

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
	/* Output buffer */
	unsigned char *pcm_buffer;
	unsigned long pcm_length;
	unsigned long pcm_remain;
	/* Infos */
	unsigned long samplerate;
	unsigned char channels;
};

int decoder_aac_open(struct decoder **decoder, const unsigned char *dec_config,
		     size_t dec_config_size, unsigned long *samplerate,
		     unsigned char *channels)
{
	NeAACDecConfigurationPtr config;
	struct decoder *dec;
	int ret;

	/* Alloc structure */
	*decoder = malloc(sizeof(struct decoder));
	if(*decoder == NULL)
		return -1;
	dec = *decoder;

	/* Init structure */
	dec->pcm_buffer = NULL;
	dec->pcm_length = 0;
	dec->pcm_remain = 0;

	/* Initialize faad */
	dec->hDec = NeAACDecOpen();

	/* Set the default object type and samplerate */
	config = NeAACDecGetCurrentConfiguration(dec->hDec);
#ifdef USE_FLOAT
	config->outputFormat = FAAD_FMT_FLOAT;
#else
	config->outputFormat = FAAD_FMT_24BIT;
#endif
	NeAACDecSetConfiguration(dec->hDec, config);

	/* PCM data remaining in output buffer */
	dec->pcm_remain = 0;

	/* Check if ADTS or ADIF header */
	if((dec_config[0] == 0xFF && (dec_config[1] & 0xF6) == 0xF0) ||
	   memcmp(dec_config, "ADIF", 4) == 0)
	{
		/* Init decoder from frame */
		ret = NeAACDecInit(dec->hDec, (unsigned char *) dec_config,
				   dec_config_size, &dec->samplerate,
				   &dec->channels);
	}
	else
	{
		/* Init decoder */
		ret = NeAACDecInit2(dec->hDec, (unsigned char *) dec_config,
				    dec_config_size, &dec->samplerate,
				    &dec->channels);
	}

	/* Check init return */
	if(ret < 0)
	{
		NeAACDecClose(dec->hDec);
		dec->hDec = NULL;
		return -1;
	}

	/* Retunr samplerate and channels */
	if(samplerate != NULL)
		*samplerate = dec->samplerate;
	if(channels != NULL)
		*channels = dec->channels;

	return 0;
}

static long decoder_aac_fill_output(struct decoder *dec,
				    unsigned char *output_buffer,
				    size_t output_size)
{
	unsigned long pos;
	unsigned long size;

	pos = dec->pcm_length-dec->pcm_remain;
	if(output_size < dec->pcm_remain)
		size = output_size;
	else
		size = dec->pcm_remain;

	/* Copy samples to output buffer */
	if(dec->channels > 2) /* Specific case of surround file */
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
		/* 32-bit wide sample (float or 32-bit fixed) */
		memcpy(output_buffer, &dec->pcm_buffer[pos * 4], size * 4);
#else
	{
		int32_t *p_in = (int32_t*) &dec->pcm_buffer[pos*4];
		int32_t *p_out = (int32_t*) output_buffer;
		size_t i;
		for(i = 0; i < size; i++)
		{
			*(p_out++) = (*p_in << 8);
			p_in++;
		}
	}
#endif

	dec->pcm_remain -= size;

	return size;
}

int decoder_aac_decode(struct decoder *dec, unsigned char *in_buffer,
		       size_t in_size, unsigned char *out_buffer,
		       size_t out_size, struct decoder_info *info)
{
	NeAACDecFrameInfo frameInfo;
	unsigned short size = 0;

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
		size = decoder_aac_fill_output(dec, out_buffer, out_size);

		/* Update buffer */
		info->used = 0;
		info->remaining = dec->pcm_remain;
		info->samplerate = dec->samplerate;
		info->channels = dec->channels;

		return size;
	}

	/* Decode a new frame */
	dec->pcm_buffer = (unsigned char*) NeAACDecDecode(dec->hDec, &frameInfo,
							  in_buffer, in_size);

	if(frameInfo.error > 0)
	{
		/* Update buffer */
		info->used = frameInfo.bytesconsumed;
		info->remaining = 0;

		return 0;
	}

	/* Fill output buffer with PCM */
	dec->pcm_remain = frameInfo.samples;
	dec->pcm_length = frameInfo.samples;
	dec->samplerate = frameInfo.samplerate;
	dec->channels = frameInfo.channels;
	size = decoder_aac_fill_output(dec, out_buffer, out_size);

	/* Update buffer */
	info->used = frameInfo.bytesconsumed;
	info->remaining = dec->pcm_remain;
	info->samplerate = dec->samplerate;
	info->channels = dec->channels;

	return size;
}

int decoder_aac_close(struct decoder *dec)
{
	if(dec == NULL)
		return  0;

	/* Close faad */
	if(dec->hDec != NULL)
		NeAACDecClose(dec->hDec);

	/* Free structure */
	free(dec);

	return 0;
}

struct decoder_handle decoder_aac = {
	.dec = NULL,
	.open = &decoder_aac_open,
	.decode = &decoder_aac_decode,
	.close = &decoder_aac_close,
};
