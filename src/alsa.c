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

#define BUFFER_SIZE 8192

struct alsa_handle {
	snd_pcm_t *alsa;
	/* Read Callback */
	int (*input_callback)(void *, unsigned char *, size_t);
	void *user_data;
	/* Input buffer */
	unsigned char *buffer;
	size_t size;
	/* Format */
	unsigned long samplerate;
	unsigned char nb_channel;
	/* Thread objects */
	pthread_t th;
	int is_running;
	int stop;
};

int alsa_open(struct alsa_handle **handle, unsigned long samplerate, unsigned char nb_channel, unsigned long latency, void *input_callback, void *user_data)
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
	h->samplerate = samplerate;
	h->nb_channel = nb_channel;

	/* Open alsa devide */
	if(snd_pcm_open(&h->alsa, "default", SND_PCM_STREAM_PLAYBACK, 0) < 0)
		return -1;

	/* Set parameters for output */
#ifdef USE_FLOAT
	if(snd_pcm_set_params(h->alsa, SND_PCM_FORMAT_FLOAT, SND_PCM_ACCESS_RW_INTERLEAVED, h->nb_channel, h->samplerate, 1, latency*1000) < 0)
#else
	if(snd_pcm_set_params(h->alsa, SND_PCM_FORMAT_S32, SND_PCM_ACCESS_RW_INTERLEAVED, h->nb_channel, h->samplerate, 1, latency*1000) < 0)
#endif
		return -1;

	/* Alloc buffer for samples */
	h->size = BUFFER_SIZE*h->nb_channel;
	h->buffer = malloc(h->size*4); // 32-bit wide sample (float or long)
	if(h->buffer == NULL)
		return -1;

	return 0;
}

static void *alsa_thread(void *user_data)
{
	struct alsa_handle *h = (struct alsa_handle*) user_data;
	snd_pcm_sframes_t frames;
	size_t input_size;
	size_t previous_size = 0;

	while(!h->stop)
	{
		input_size = h->input_callback(h->user_data, h->buffer, h->size) / h->nb_channel;
		if(input_size == 0)
			input_size = previous_size;
		previous_size = input_size;

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

