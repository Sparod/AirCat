/*
 * alsa.c - Alsa output
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
#include <pthread.h>

#include <asoundlib.h>

#include "alsa.h"

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

struct alsa_handle {
	snd_pcm_t *alsa;
	/* Read Callback */
	int (*input_callback)(void *, float *, size_t);
	void *user_data;
	/* Input buffer */
	float *buffer;
	size_t size;
	/* Input format */
	struct alsa_format input;
	/* Output format */
	struct alsa_format output;
	/* Thread objects */
	pthread_t th;
	int is_running;
	int stop;
};

int alsa_open(struct alsa_handle **handle, struct alsa_format input, struct alsa_format output, unsigned long latency, void *input_callback, void *user_data)
{
	struct alsa_handle *h;

	*handle = malloc(sizeof(struct alsa_handle));
	if(*handle == NULL)
		return -1;
	h = *handle;

	/* Copy callback pointers */
	h->input_callback = input_callback;
	h->user_data = user_data;

	/* Init thread variables */
	h->is_running = 0;
	h->stop = 0;

	/* Copy input and output format */
	h->input = input;
	h->output = output;

	/* Open alsa devide */
	if(snd_pcm_open(&h->alsa, "default", SND_PCM_STREAM_PLAYBACK, 0) < 0) /* FIXME */
		return -1;

	/* Set parameters for output */
	if(snd_pcm_set_params(h->alsa, SND_PCM_FORMAT_FLOAT_LE, SND_PCM_ACCESS_RW_INTERLEAVED, h->output.nb_channel, h->output.samplerate, 1, latency) < 0)
		return -1;

	/* Alloc buffer for samples */
	h->size = 8192;
	h->buffer = malloc(h->size*sizeof(float)); /* FIXME */
	if(h->buffer == NULL)
		return -1;

	return 0;
}

static void *alsa_thread(void *user_data)
{
	struct alsa_handle *h = (struct alsa_handle*) user_data;
	snd_pcm_sframes_t frames;
	size_t input_size;

	while(!h->stop)
	{
		input_size = h->input_callback(h->user_data, h->buffer, h->size) / h->output.nb_channel;

		/* Play pcm sample */ /* FIXME */
		frames = snd_pcm_writei(h->alsa, (unsigned char*)h->buffer, input_size);
		if (frames < 0)
			frames = snd_pcm_recover(h->alsa, frames, 0);
		if (frames < 0)
		{
			printf("snd_pcm_writei failed!\n");
			break;
		}
		if (frames > 0 && frames < (long)input_size)
			printf("Short write (expected %li, wrote %li)\n", (long)input_size, frames);
	}

	return NULL;
}

int alsa_play(struct alsa_handle *h)
{
	if(h == NULL)
		return -1;

	if(h->is_running != 0)
		return -1;

	h->stop = 0;

	/* Create thread */
	if(pthread_create(&h->th, NULL, alsa_thread, h) != 0)
		return -1;

	h->is_running = 1;
	return 0;
}

int alsa_stop(struct alsa_handle *h)
{
	if(h == NULL)
		return -1;

	if(h->is_running != 1)
		return -1;

	h->stop = 1;

	/* Create thread */
	if(pthread_join(h->th, NULL) < 0)
		return -1;

	h->is_running = 0;
	return 0;
}

int alsa_close(struct alsa_handle *h)
{
	if(h == NULL)
		return 0;

	/* Stop thread */
	alsa_stop(h);

	/* Close alsa */
	if(h->alsa != NULL)
		snd_pcm_close(h->alsa);

	/* Free buffer */
	if(h->buffer != NULL)
		free(h->buffer);

	/* Free structure */
	free(h);

	return 0;
}

