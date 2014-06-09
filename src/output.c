/*
 * output.c - Audio output module
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

#include "output.h"
#include "output_alsa.h"

int output_open(struct output_handle **handle, int module,
		unsigned int samplerate, int nb_channel)
{
	struct output_handle *h;

	*handle = malloc(sizeof(struct output_handle));
	if(*handle == NULL)
		return -1;
	h = *handle;

	switch(module)
	{
		case OUTPUT_ALSA:
		default:
			memcpy(h, &output_alsa, sizeof(struct output_handle));
	}

	return h->open(&h->out, samplerate, nb_channel);
}

int output_set_volume(struct output_handle *h, unsigned int volume)
{
	if(h == NULL || h->out == NULL)
		return -1;

	return h->set_volume(h->out, volume);
}

unsigned int output_get_volume(struct output_handle *h)
{
	if(h == NULL || h->out == NULL)
		return -1;

	return h->get_volume(h->out);
}

struct output_stream *output_add_stream(struct output_handle *h,
					const char *stream_name,
					unsigned long samplerate,
					unsigned char nb_channel,
					unsigned long cache,
					int use_cache_thread,
					void *input_callback, void *user_data)
{
	if(h == NULL || h->out == NULL)
		return NULL;

	return h->add_stream(h->out, stream_name, samplerate, nb_channel, cache,
			     use_cache_thread, input_callback, user_data);
}

int output_play_stream(struct output_handle *h, struct output_stream *s)
{
	if(h == NULL || h->out == NULL || s == NULL)
		return -1;

	return h->play_stream(h->out, s);
}

int output_pause_stream(struct output_handle *h, struct output_stream *s)
{
	if(h == NULL || h->out == NULL || s == NULL)
		return -1;

	return h->pause_stream(h->out, s);
}

int output_set_volume_stream(struct output_handle *h, struct output_stream *s,
			     unsigned int volume)
{
	if(h == NULL || h->out == NULL || s == NULL)
		return -1;

	return h->set_volume_stream(h->out, s, volume);
}

unsigned int output_get_volume_stream(struct output_handle *h,
				      struct output_stream *s)
{
	if(h == NULL || h->out == NULL || s == NULL)
		return -1;

	return h->get_volume_stream(h->out, s);
}

int output_remove_stream(struct output_handle* h, struct output_stream *s)
{
	if(h == NULL || h->out == NULL || s == NULL)
		return -1;

	return h->remove_stream(h->out, s);
}

int output_close(struct output_handle* h)
{
	if(h == NULL)
		return 0;

	if(h->out != NULL)
		h->close(h->out);

	free(h);

	return 0;
}
