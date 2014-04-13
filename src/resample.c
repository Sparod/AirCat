/*
 * resample.c - Samplerate and channel converter based on libsoxr
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
#include <stdint.h>

#include <soxr.h>

#include "resample.h"

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#define BUFFER_SIZE 8192
#define max(x,y) (x >= y ? x : y)
#define min(x,y) (x > y ? y : x)

struct resample_handle {
	/* Libsoxr variables */
	soxr_t soxr;
	/* Samplerate converter configuration */
	unsigned long in_samplerate;
	unsigned char in_nb_channel;
	unsigned long out_samplerate;
	unsigned char out_nb_channel;
	/* Input callback */
	int (*input_callback)( void *, unsigned char *, size_t);
	void *user_data;
	/* Input buffer */
	unsigned char *buffer;
	size_t size;
	/* Output buffer (mono -> stereo) */
	unsigned char *buffer_out;
	size_t size_out;
	/* Mixing table: inspired from remix effect from sox */
	struct {
		unsigned num_in_channels;
		struct in_spec {
			unsigned channel_num;
			float   multiplier;
		} * in_specs;
	} * out_specs;
};

static size_t resample_input_callback(void *user_data, soxr_cbuf_t *buffer, size_t len)
{
	struct resample_handle *h = (struct resample_handle*) user_data;

	/* Read one block into the buffer */
	len = h->input_callback(h->user_data, h->buffer, len * h->in_nb_channel) / h->in_nb_channel;

	/* Down-mixing channels: inspired from remix effect from sox  */
	if(h->in_nb_channel > h->out_nb_channel)
	{
#ifdef USE_FLOAT
		float sample = 0;
		float *p_in = (float*) h->buffer;
		float *p_out = (float*) h->buffer;
#else
		int32_t sample = 0;
		int32_t *p_in = (int32_t*) h->buffer;
		int32_t *p_out = (int32_t*) h->buffer;

#endif
		size_t i;
		int j, k;

		for(i = len; i--; p_in += h->in_nb_channel)
		{
			for(j = 0; j < h->out_nb_channel; j++)
			{
				sample = 0;
				for(k = 0; k < h->out_specs[j].num_in_channels; k++)
				{
					sample += p_in[h->out_specs[j].in_specs[k].channel_num] * h->out_specs[j].in_specs[k].multiplier;
				}
				*(p_out++) = sample;
			}
		}
	}

	if(len <= 0)
		*buffer = NULL;
	else
		*buffer = h->buffer;

	return len;
}

int resample_open(struct resample_handle **handle, unsigned long in_samplerate, unsigned char in_nb_channel, unsigned long out_samplerate, unsigned char out_nb_channel, void *input_callback, void *user_data)
{
	struct resample_handle *h;
	soxr_io_spec_t io_spec;
	int i, j;
	
	*handle = malloc(sizeof(struct resample_handle));
	if(*handle == NULL)
		return -1;
	h = *handle;

	/* Reset structure */
	memset((unsigned char *)h, 0, sizeof(struct resample_handle));

	/* Set callback function */
	h->input_callback = input_callback;
	h->user_data = user_data;

	/* Set samplerate and channel values */
	h->in_samplerate = in_samplerate ;
	h->in_nb_channel = in_nb_channel;
	h->out_samplerate = out_samplerate;
	h->out_nb_channel = out_nb_channel;

	/* Alloc buffer */
	h->size = BUFFER_SIZE*max(in_nb_channel, out_nb_channel);
	h->buffer = malloc(h->size*4); //32-bit wide sample
	if(h->buffer == NULL)
		return -1;

	/* Alloc a second buffer for in channel < out_channel */
	if(in_nb_channel < out_nb_channel)
	{
		h->size_out = BUFFER_SIZE*in_nb_channel;
		h->buffer_out = malloc(h->size_out*4); //32-bit wide sample
		if(h->buffer_out == NULL)
			return -1;
	}

	/* Prepapre values for down/up-mixing */
	h->out_specs = malloc(h->out_nb_channel*sizeof(*h->out_specs));
	if(h->out_specs == NULL)
		return -1;

	/* Calculate coeff for down/up-mixing: inspired from remix effect from sox */
	if(h->in_nb_channel > h->out_nb_channel)
	{
		/* Down-mixing */ /* FIXME: for channels > 2 needs to add center on left and right */
		for(i = 0; i < h->out_nb_channel; i++)
		{
			unsigned in_per_out = (h->in_nb_channel + h->out_nb_channel - 1 - i) / h->out_nb_channel;
			h->out_specs[i].in_specs = malloc(in_per_out*sizeof(*h->out_specs[i].in_specs));
			h->out_specs[i].num_in_channels = in_per_out;

			for (j = 0; j < in_per_out; ++j)
			{
				h->out_specs[i].in_specs[j].channel_num = j * h->out_nb_channel + i;
				h->out_specs[i].in_specs[j].multiplier = 1. / in_per_out;
			}
		}
	}
	else
	{
		/* Up-mixing */
		for(i = 0; i < h->out_nb_channel; i++)
		{
			h->out_specs[i].in_specs = malloc(sizeof(h->out_specs[i].in_specs));
			h->out_specs[i].in_specs[0].channel_num = i % h->in_nb_channel;
		}
	}

	/* Set inout and output format */
#ifdef USE_FLOAT
	io_spec = soxr_io_spec(SOXR_FLOAT32_I, SOXR_FLOAT32_I);
#else
	io_spec = soxr_io_spec(SOXR_INT32_I, SOXR_INT32_I);
#endif

	/* Create converter */
	h->soxr = soxr_create((double)in_samplerate, (double)out_samplerate, min(in_nb_channel, out_nb_channel), NULL, &io_spec, NULL, NULL);
	if(h->soxr == NULL)
		return -1;

	/* Set input callback for libsoxr */
	if(soxr_set_input_fn(h->soxr, (soxr_input_fn_t)resample_input_callback, h, BUFFER_SIZE) < 0)
		return -1;

	return 0;
}

int resample_read(struct resample_handle *h, unsigned char *buffer, size_t size)
{
	/* Up-mixing channels: inspired from remix effect from sox */
	if(h->in_nb_channel < h->out_nb_channel)
	{
		size_t i, len;
#ifdef USE_FLOAT
		float *p_in = (float*) h->buffer_out;
		float *p_out = (float*) buffer;
#else
		int32_t *p_in = (int32_t*) h->buffer_out;
		int32_t *p_out = (int32_t*) buffer;
#endif
		int j;

		size = size * h->in_nb_channel / h->out_nb_channel;
		if(h->size_out < size)
			size = h->size_out;

		len = soxr_output(h->soxr, h->buffer_out, size / h->in_nb_channel);

		for(i = len; i--; p_in += h->in_nb_channel)
		{
			for(j = 0; j < h->out_nb_channel; j++)
			{
				*(p_out++) = p_in[h->out_specs[j].in_specs[0].channel_num];
			}
		}

		return len * h->out_nb_channel;
	}
	else
		return soxr_output(h->soxr, buffer, size/h->out_nb_channel)*h->out_nb_channel;
}

int resample_close(struct resample_handle *h)
{
	if(h == NULL)
		return 0;

	/* Close libsoxr */
	if(h->soxr != NULL)
		soxr_delete(h->soxr);

	/* Free specs for down/up-mixing */
	if(h->out_specs != NULL)
	{
		int i;
		for(i = 0; i < h->out_nb_channel; i++)
			if(h->out_specs[i].in_specs != NULL)
				free(h->out_specs[i].in_specs);
		free(h->out_specs);
	}

	/* Free buffer */
	if(h->buffer != NULL)
		free(h->buffer);
	if(h->buffer_out != NULL)
		free(h->buffer_out);

	/* Free structure */
	free(h);

	return 0;
}

