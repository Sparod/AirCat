/*
 * decoder_mp3.c - A MP3 Decoder based on mad
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
#include <mad.h>

#include "decoder_mp3.h"

#define BUFFER_SIZE 8192

struct decoder {
	struct mad_stream Stream;
	struct mad_header Header;
	struct mad_frame Frame;
	struct mad_synth Synth;
	int (*read_stream)(unsigned char *, size_t, void *);
	void *user_data;
	unsigned char buffer[BUFFER_SIZE+MAD_BUFFER_GUARD];
	unsigned short pcm_remain;
	unsigned long samplerate;
	int nb_channel;
};

static long decoder_mp3_fill(struct decoder *dec);
static long decoder_mp3_fill_output(struct decoder *dec, float *output_buffer, size_t output_size);

int decoder_mp3_open(struct decoder **decoder, void *input_callback, void *user_data)
{
	struct decoder *dec;

	*decoder = malloc(sizeof(struct decoder));
	if(*decoder == NULL)
		return -1;
	dec = *decoder;

	dec->read_stream = input_callback;
	dec->user_data = user_data;

	/* Initialize mad */
	mad_stream_init(&dec->Stream);
	mad_header_init(&dec->Header);
	mad_frame_init(&dec->Frame);
	mad_synth_init(&dec->Synth);

	/* PCM data remaining in output buffer */
	dec->pcm_remain = 0;

	/* Parse first header to get samplerate and number of channel */
	decoder_mp3_fill(dec);
	while(mad_header_decode(&dec->Header, &dec->Stream))
	{
		if(MAD_RECOVERABLE(dec->Stream.error))
		{
			continue;
		}
		else
		{
			if(dec->Stream.error == MAD_ERROR_BUFLEN)
			{
				/* Refill buffer */
				if(decoder_mp3_fill(dec) < 0)
					return -1;
			}
			else
			{
				break;
			}
		}
	}

	/* Get infos */
	dec->samplerate = dec->Header.samplerate;
	dec->nb_channel = MAD_NCHANNELS(&dec->Header);

	return 0;
}

unsigned long decoder_mp3_get_samplerate(struct decoder* dec)
{
	return dec->samplerate;
}

int decoder_mp3_get_nb_channel(struct decoder *dec)
{
	return dec->nb_channel;
}

static long decoder_mp3_fill(struct decoder *dec)
{
	long size = 0;
	size_t remaining = 0;

	if(dec->Stream.buffer == NULL)
	{
		size = BUFFER_SIZE;
		remaining = 0;
	}
	else
	{
		remaining = dec->Stream.bufend - dec->Stream.next_frame;
		size = BUFFER_SIZE - remaining;
		memmove(dec->buffer, dec->Stream.next_frame, remaining);
	}

	/* Read data from callback */
	size = dec->read_stream(&dec->buffer[remaining], size, dec->user_data);
	if(size < 0)
		return -1;

	mad_stream_buffer(&dec->Stream, dec->buffer, size+remaining);
	dec->Stream.error = 0;

	return size+remaining;
}

static long decoder_mp3_fill_output(struct decoder *dec, float *output_buffer, size_t output_size)
{
	unsigned short pos;
	int i;

	pos = dec->Synth.pcm.length-dec->pcm_remain;

	for(i = 0; pos < dec->Synth.pcm.length && i < output_size; pos++, i += 2)
	{
		/* Left channel */
		*(output_buffer++) = (float) (dec->Synth.pcm.samples[0][pos] / (float) (1L << MAD_F_FRACBITS));

		/* Right channel */
		if(MAD_NCHANNELS(&dec->Frame.header) == 2)
			*(output_buffer++) = (float) (dec->Synth.pcm.samples[1][pos] / (float) (1L << MAD_F_FRACBITS));
	}

	dec->pcm_remain = dec->Synth.pcm.length-pos;

	return i;
}

int decoder_mp3_read(struct decoder *dec, float *output_buffer, size_t output_size)
{
	unsigned short size = 0;

	/* Empty remaining PCM before decoding another frame */
	if(dec->pcm_remain > 0)
	{
		size = decoder_mp3_fill_output(dec, output_buffer, output_size);
		if(dec->pcm_remain > 0 || size == output_size)
			return size;
	}

	/* Decode a new frame */
	while(mad_frame_decode(&dec->Frame, &dec->Stream))
	{
		if(MAD_RECOVERABLE(dec->Stream.error))
		{
			continue;
		}
		else
		{
			if(dec->Stream.error == MAD_ERROR_BUFLEN)
			{
				/* Refill buffer */
				if(decoder_mp3_fill(dec) < 0)
					return -1;
			}
			else
			{
				break;
			}
		}
	}

	/* Synthethise PCM */
	mad_synth_frame(&dec->Synth, &dec->Frame);

	/* Fill output buffer with PCM */
	dec->pcm_remain = dec->Synth.pcm.length;
	size += decoder_mp3_fill_output(dec, &output_buffer[size], output_size-size);

	return size;
}

int decoder_mp3_close(struct decoder *dec)
{
	/* Close mad */
	mad_synth_finish(&dec->Synth);
	mad_frame_finish(&dec->Frame);
	mad_header_finish(&dec->Header);
	mad_stream_finish(&dec->Stream);

	if(dec != NULL)
		free(dec);

	return 0;
}

