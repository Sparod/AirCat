/*
 * resample.c - Samplerate and channel converter based on libsoxr
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
	unsigned char in_channels;
	unsigned long out_samplerate;
	unsigned char out_channels;
	/* Input callback */
	a_read_cb input_callback;
	void *user_data;
	/* Input buffer */
	unsigned char *in_buffer;
	size_t in_size;
	size_t in_len;
	/* Output buffer (mono -> stereo) */
	unsigned char *out_buffer;
	size_t out_size;
	/* Mixing table: inspired from remix effect from sox */
	struct {
		unsigned num_in_channels;
		struct in_spec {
			unsigned channel_num;
			float   multiplier;
		} * in_specs;
	} * out_specs;
};

int resample_open(struct resample_handle **handle, unsigned long in_samplerate,
		  unsigned char in_channels, unsigned long out_samplerate,
		  unsigned char out_channels, a_read_cb input_callback,
		  void *user_data)
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
	h->in_channels = in_channels;
	h->out_samplerate = out_samplerate;
	h->out_channels = out_channels;

	/* Alloc buffer */
	h->in_len = 0;
	h->in_size = BUFFER_SIZE * max(in_channels, out_channels);
	h->in_buffer = malloc(h->in_size * 4); //32-bit wide sample
	if(h->in_buffer == NULL)
		return -1;

	/* Alloc a second buffer for in channel < out_channel */
	if(in_channels < out_channels)
	{
		h->out_size = BUFFER_SIZE * in_channels;
		h->out_buffer = malloc(h->out_size * 4); //32-bit wide sample
		if(h->out_buffer == NULL)
			return -1;
	}

	/* Prepapre values for down/up-mixing */
	if(in_channels != out_channels)
	{
		h->out_specs = malloc(h->out_channels * sizeof(*h->out_specs));
		if(h->out_specs == NULL)
			return -1;
	}

	/* Calculate coeff for down/up-mixing: inspired from remix effect from
	 * sox library.
	 */
	if(h->in_channels > h->out_channels)
	{
		/* Down-mixing */
		/* FIXME: for channels > 2 needs to add center on L and R */
		for(i = 0; i < h->out_channels; i++)
		{
			unsigned in_per_out = (h->in_channels +
					       h->out_channels - 1 - i) /
					      h->out_channels;
			h->out_specs[i].in_specs = malloc(in_per_out * 
					     sizeof(*h->out_specs[i].in_specs));
			h->out_specs[i].num_in_channels = in_per_out;

			for (j = 0; j < in_per_out; ++j)
			{
				h->out_specs[i].in_specs[j].channel_num = j *
							     h->out_channels +
							     i;
				h->out_specs[i].in_specs[j].multiplier = 1. /
								     in_per_out;
			}
		}
	}
	else if(h->in_channels < h->out_channels)
	{
		/* Up-mixing */
		for(i = 0; i < h->out_channels; i++)
		{
			h->out_specs[i].in_specs = malloc(
					      sizeof(h->out_specs[i].in_specs));
			h->out_specs[i].in_specs[0].channel_num = i %
							       h->in_channels;
		}
	}

	/* Set inout and output format */
#ifdef USE_FLOAT
	io_spec = soxr_io_spec(SOXR_FLOAT32_I, SOXR_FLOAT32_I);
#else
	io_spec = soxr_io_spec(SOXR_INT32_I, SOXR_INT32_I);
#endif

	/* Create converter */
	h->soxr = soxr_create((double)in_samplerate, (double)out_samplerate,
			      min(in_channels, out_channels), NULL,
			      &io_spec, NULL, NULL);
	if(h->soxr == NULL)
		return -1;

	return 0;
}

static int resample_down_mix(struct resample_handle *h, unsigned char *buffer,
			     size_t len, unsigned char in_channels,
			     unsigned char out_channels)
{
#ifdef USE_FLOAT
	float sample = 0;
	float *p_in = (float*) buffer;
	float *p_out = (float*) buffer;
#else
	int32_t sample = 0;
	int32_t *p_in = (int32_t*) buffer;
	int32_t *p_out = (int32_t*) buffer;

#endif
	size_t i, j, k;

	if(len <= 0)
		return 0;

	/* Down-mixing channels: inspired from remix effect from sox  */
	for(i = len; i--; p_in += in_channels)
	{
		for(j = 0; j < h->out_channels; j++)
		{
			sample = 0;
			for(k = 0; k < h->out_specs[j].num_in_channels; k++)
			{
				sample +=
				   p_in[h->out_specs[j].in_specs[k].channel_num]
				   * h->out_specs[j].in_specs[k].multiplier;
			}
			*(p_out++) = sample;
		}
	}

	return len * out_channels / in_channels;
}

static int resample_up_mix(struct resample_handle *h, unsigned char *in_buffer,
			   unsigned char *out_buffer, size_t len,
			   unsigned char in_channels,
			   unsigned char out_channels)
{
#ifdef USE_FLOAT
	float *p_in = (float*) in_buffer;
	float *p_out = (float*) out_buffer;
#else
	int32_t *p_in = (int32_t*) in_buffer;
	int32_t *p_out = (int32_t*) out_buffer;
#endif
	int i, j;

	/* Up-mixing channels: inspired from remix effect from sox */
	for(i = len; i--; p_in += in_channels)
	{
		for(j = 0; j < out_channels; j++)
		{
			*(p_out++) = 
			  p_in[h->out_specs[j].in_specs[0].channel_num];
		}
	}

	return len *out_channels / in_channels;
}

int resample_read(void *user_data, unsigned char *buffer, size_t size,
		  struct a_format *fmt)
{
	struct resample_handle *h = (struct resample_handle *) user_data;
	size_t in_consumed, out_samples;
	unsigned char *p_in, *p_out;
	unsigned long total_size = 0;
	size_t in_scale; // samples * _scale => bytes * channels */
	size_t out_len;
	ssize_t len; // => samples * channels

	while(total_size < size)
	{
		/* Fill as possible input buffer */
		len = h->input_callback(h->user_data, &h->in_buffer[h->in_len],
					(h->in_size - h->in_len) / 4, NULL);
		if(len == 0)
			break;

		/* Down-mixing channels */
		if(len > 0)
		{
			if(h->in_channels > h->out_channels)
			{
				len = resample_down_mix(h,
						       &h->in_buffer[h->in_len],
						       len, h->in_channels,
						       h->out_channels);
			}
			h->in_len += len * 4;
		}

		/* End of stream handling */
		p_in = len < 0 && h->in_len == 0 ? NULL : h->in_buffer;
		in_scale = h->in_channels > h->out_channels ?
							h->out_channels * 4 :
							h->in_channels * 4;
		p_out = h->in_channels < h->out_channels ? h->out_buffer :
							&buffer[total_size * 4];
		out_len = (size - total_size) / h->out_channels;
		if(h->in_channels < h->out_channels)
			out_len *= h->in_channels;

		/* Process data */
		soxr_process(h->soxr, p_in, h->in_len / in_scale, &in_consumed,
			     p_out, out_len, &out_samples);

		/* Update input buffer position */
		h->in_len -= in_consumed * in_scale;
		if(h->in_len > 0)
		{
			/* Move remaining data in input buffer */
			memmove(h->in_buffer,
				&h->in_buffer[in_consumed*in_scale], h->in_len);
		}

		/* End of stream and all remaining data has been consumed */
		if(len < 0 && out_samples == 0)
			return -1;

		/* Up-mixing channels: inspired from remix effect from sox */
		if(out_samples > 0 && h->in_channels < h->out_channels)
		{
			resample_up_mix(h, h->out_buffer,
					&buffer[total_size * 4],
					out_samples * h->in_channels,
					h->in_channels, h->out_channels);
		}

		/* Update total size */
		total_size += out_samples * h->out_channels;
	}

	return total_size;
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
		for(i = 0; i < h->out_channels; i++)
			if(h->out_specs[i].in_specs != NULL)
				free(h->out_specs[i].in_specs);
		free(h->out_specs);
	}

	/* Free buffer */
	if(h->in_buffer != NULL)
		free(h->in_buffer);
	if(h->out_buffer != NULL)
		free(h->out_buffer);

	/* Free structure */
	free(h);

	return 0;
}

