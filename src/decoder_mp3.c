/*
 * decoder_mp3.c - A MP3 Decoder based on mad
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
#include <mad.h>

#include "decoder_mp3.h"

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

struct decoder {
	struct mad_stream Stream;
	struct mad_frame Frame;
	struct mad_synth Synth;
	/* Output cursor */
	unsigned long pcm_remain;
};

int decoder_mp3_open(struct decoder **decoder, const unsigned char *config,
		     size_t config_size, unsigned long *samplerate,
		     unsigned char *channels)
{
	struct mad_header header;
	struct decoder *dec;

	/* Alloc structure */
	*decoder = malloc(sizeof(struct decoder));
	if(*decoder == NULL)
		return -1;
	dec = *decoder;
	
	/* Init structure */
	dec->pcm_remain = 0;

	/* Initialize mad */
	mad_stream_init(&dec->Stream);
	mad_frame_init(&dec->Frame);
	mad_synth_init(&dec->Synth);

	/* Decoder first frame */
	if(config != NULL && config_size > 0)
	{
		/* Parse first frame in config buffer */
		mad_header_init(&header);

		/* Fill mad stream with first frame */
		mad_stream_buffer(&dec->Stream, config, config_size);

		/* Parse config */
		if(mad_header_decode(&header, &dec->Stream) != 0)
			return -1;

		/* Get infos */
		if(samplerate != NULL)
			*samplerate = header.samplerate;
		if(channels != NULL)
			*channels = MAD_NCHANNELS(&header);

		/* Free header parser */
		mad_header_finish(&header);
	}

	return 0;
}

#ifdef USE_FLOAT
inline float mad_scale(mad_fixed_t sample)
{
	return (float) (sample / (float) (1L << MAD_F_FRACBITS));
}
#else
inline int32_t mad_scale(mad_fixed_t sample)
{

	/* Round sample */
	sample += (1L << 4);

	/* Clip */
	if (sample >= MAD_F_ONE)
		sample = MAD_F_ONE - 1;
	else if (sample < -MAD_F_ONE)
		sample = -MAD_F_ONE;

	return (sample << 3) & 0xFFFFFF00;
}
#endif

static long decoder_mp3_fill_output(struct decoder *dec,
				    unsigned char *output_buffer,
				    size_t output_size)
{
#ifdef USE_FLOAT
	float *p = (float*) output_buffer;
#else
	int32_t *p = (int32_t*) output_buffer;
#endif
	unsigned short pos;
	int i;

	pos = dec->Synth.pcm.length - dec->pcm_remain;

	for(i = 0; pos < dec->Synth.pcm.length && i < output_size;
	    pos++, i += dec->Synth.pcm.channels)
	{
		/* Left channel */
		*(p++) = mad_scale(dec->Synth.pcm.samples[0][pos]);

		/* Right channel */
		if(dec->Synth.pcm.channels == 2)
			*(p++) = mad_scale(dec->Synth.pcm.samples[1][pos]);
	}

	dec->pcm_remain = dec->Synth.pcm.length - pos;

	return i;
}

int decoder_mp3_decode(struct decoder *dec, unsigned char *in_buffer,
		       size_t in_size, unsigned char *out_buffer,
		       size_t out_size, struct decoder_info *info)
{
	unsigned short size = 0;

	/* Reset position of PCM output buffer */
	if(in_buffer == NULL && out_buffer == 0 && out_size == 0)
	{
		dec->pcm_remain = dec->Synth.pcm.length;
		return 0;
	}

	/* Empty remaining PCM before decoding another frame */
	if(dec->pcm_remain > 0 || in_buffer == NULL)
	{
		size = decoder_mp3_fill_output(dec, out_buffer, out_size);

		/* Update buffer */
		info->used = 0;
		info->remaining = dec->pcm_remain;
		info->samplerate = dec->Frame.header.samplerate;
		info->channels = MAD_NCHANNELS(&dec->Frame.header);

		return size;
	}

	/* Check input size */
	if(in_size == 0)
		return 0;

	/* Add frame to stream */
	mad_stream_buffer(&dec->Stream, in_buffer, in_size);

	/* Decode a new frame */
	while(mad_frame_decode(&dec->Frame, &dec->Stream))
	{
		if(MAD_RECOVERABLE(dec->Stream.error))
		{
			/* Needs more frame */
			continue;
		}
		else
		{
			/* Update buffer */
			info->used = dec->Stream.next_frame - dec->Stream.buffer;
			info->remaining = 0;

			if(dec->Stream.error == MAD_ERROR_BUFLEN)
				return DECODER_ERROR_BUFLEN;
			else
				return DECODER_ERROR_SYNC;
		}
	}

	/* Synthethise PCM */
	mad_synth_frame(&dec->Synth, &dec->Frame);

	/* Fill output buffer with PCM */
	dec->pcm_remain = dec->Synth.pcm.length;
	size = decoder_mp3_fill_output(dec, out_buffer, out_size);

	/* Update buffer */
	info->used = dec->Stream.next_frame - dec->Stream.buffer;
	info->remaining = dec->pcm_remain;
	info->samplerate = dec->Frame.header.samplerate;
	info->channels = MAD_NCHANNELS(&dec->Frame.header);

	return size;
}

int decoder_mp3_close(struct decoder *dec)
{
	if(dec == NULL)
		return 0;

	/* Close mad */
	mad_synth_finish(&dec->Synth);
	mad_frame_finish(&dec->Frame);
	mad_stream_finish(&dec->Stream);

	/* Free decoder */
	free(dec);

	return 0;
}

struct decoder_handle decoder_mp3 = {
	.dec = NULL,
	.open = &decoder_mp3_open,
	.decode = &decoder_mp3_decode,
	.close = &decoder_mp3_close,
};
