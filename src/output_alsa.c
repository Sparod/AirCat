/*
 * output_alsa.c - Alsa Audio output module
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

#include "output_alsa.h"
#include "output.h"

#include "resample.h"

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#define BUFFER_SIZE 8192

#ifdef USE_FLOAT
 	#define ALSA_FORMAT SND_PCM_FORMAT_FLOAT
#else
	#define ALSA_FORMAT SND_PCM_FORMAT_S32
#endif

struct output_stream {
	/* Resample object */
	struct resample_handle *res;
	/* Input callback */
	int (*input_callback)(void *, char *, size_t);
	void *user_data;
	/* Format */
	unsigned long samplerate;
	unsigned char nb_channel;
	/* Stream status */
	int is_playing;

	struct output_stream *next;
};

struct output {
	/* ALSA output */
	snd_pcm_t *alsa;
	/* Format */
	unsigned long samplerate;
	unsigned char nb_channel;
	/* Thread objects */
	pthread_t thread;
	// MUTEX
	int stop;
	/* Stream list */
	struct output_stream *streams;
};

static void *output_alsa_thread(void *user_data);

int output_alsa_open(struct output **handle, unsigned int samplerate,
		     int nb_channel)
{
	struct output *h;
	int latency = 100;
	int ret;

	*handle = malloc(sizeof(struct output));
	if(*handle == NULL)
		return -1;
	h = *handle;

	/* Init structure */
	h->streams = NULL;
	h->stop = 0;

	/* Copy input and output format */
	h->samplerate = samplerate;
	h->nb_channel = nb_channel;

	/* Open alsa device */
	if(snd_pcm_open(&h->alsa, "default", SND_PCM_STREAM_PLAYBACK, 0) < 0)
		return -1;

	/* Set parameters for output */
	ret = snd_pcm_set_params(h->alsa, ALSA_FORMAT,
				 SND_PCM_ACCESS_RW_INTERLEAVED, h->nb_channel,
				 h->samplerate, 1, latency*1000);
	if(ret < 0)
		return -1;

	/* Create thread */
	if(pthread_create(&h->thread, NULL, output_alsa_thread, h) != 0)
		return -1;

	return 0;
}

struct output_stream *output_alsa_add_stream(struct output *h,
					     unsigned long samplerate,
					     unsigned char nb_channel,
					     void *input_callback,
					     void *user_data)
{
	struct output_stream *s;

	/* Alloc the stream handler */
	s = malloc(sizeof(struct output_stream));
	if(s == NULL)
		return NULL;

	/* Fill the handler */
	s->samplerate = samplerate;
	s->nb_channel = nb_channel;
	s->res = NULL;
	s->is_playing = 0;

	/* Use resampler module if samplerate or/and channels are different */
	if(samplerate != h->samplerate || nb_channel != h->nb_channel)
	{
		if(resample_open(&s->res, samplerate, nb_channel, h->samplerate,
			      h->nb_channel, input_callback, user_data) != 0)
			goto error;
		s->input_callback = &resample_read;
		s->user_data = s->res;
	}
	else
	{
		s->input_callback = input_callback;
		s->user_data = user_data;
	}

	/* Add stream to stream list */
	s->next = h->streams;
	h->streams = s;

	return s;

error:
	free(s);
	return NULL;
}

int output_alsa_play_stream(struct output *h, struct output_stream *s)
{
	// LOCK
	s->is_playing = 1;
	// UNLOCK

	return 0;
}

int output_alsa_pause_stream(struct output *h, struct output_stream *s)
{
	// LOCK
	s->is_playing = 0;
	// UNLOCK

	return 0;
}

static void output_alsa_free_stream(struct output_stream *s)
{
	/* Close resample module */
	if(s->res != NULL)
		resample_close(s->res);

	/* Free stream */
	free(s);
}

int output_alsa_remove_stream(struct output *h, struct output_stream *s)
{
	struct output_stream *cur, *prev;

	/* Remove stream from list */
	// LOCK
	prev = NULL;
	cur = h->streams;
	while(cur != NULL)
	{
		if(s == cur)
		{
			if(prev == NULL)
				h->streams = cur->next;
			else
				prev->next = cur->next;
			break;
		}

		prev = cur;
		cur = cur->next;
	}
	// UNLOCK

	/* Free stream */
	output_alsa_free_stream(s);

	return 0;
}

static int output_alsa_mix_streams(struct output *h, char *in_buffer,
				   char *out_buffer, size_t len)
{
	struct output_stream *s;
#ifdef USE_FLOAT
	float *p_in = (float*) in_buffer;
	float *p_out = (float*) out_buffer;
#else
	int32_t *p_in = (int32_t*) in_buffer;
	int32_t *p_out = (int32_t*) out_buffer;
#endif
	int out_size = 0;
	int in_size;
	int i;

	// LOCK
	for(s = h->streams; s != NULL; s = s->next)
	{
		if(!s->is_playing)
			continue;

		/* Get input data */
		in_size = s->input_callback(s->user_data, in_buffer, len);

		/* Add it to output buffer */
		if(s == h->streams)
		{
			for(i = 0; i < in_size; i++)
			{
				p_out[i] = p_in[i];
			}
		}
		else
		{
			/* Add it to output buffer */
			for(i = 0; i < in_size; i++)
			{
				p_out[i] += p_in[i];
			}
		}

		/* Update out_size */
		if(out_size < in_size);
			out_size = in_size;
	}
	// UNLOCK

	return out_size;
}

static void *output_alsa_thread(void *user_data)
{
	struct output *h = (struct output *) user_data;
	snd_pcm_sframes_t frames;
	char *in_buffer, *out_buffer;
	int in_size = BUFFER_SIZE;
	int out_size = 0;

	/* Allocate buffer */
	in_buffer = malloc(in_size * 4);
	if(in_buffer == NULL)
		return NULL;
	out_buffer = malloc(in_size * 4);
	if(out_buffer == NULL)
	{
		free(in_buffer);
		return NULL;
	}

	while(!h->stop)
	{
		out_size = output_alsa_mix_streams(h, in_buffer, out_buffer,
						   in_size) / h->nb_channel;
		if(out_size == 0)
		{
			/* Fill with zero */
			memset(out_buffer, 0, in_size * 4);
			out_size = in_size / h->nb_channel;
		}

		/* Play pcm sample */
		frames = snd_pcm_writei(h->alsa, (unsigned char*) out_buffer,
					out_size);

		/* Try again to send frames */
		if (frames < 0)
			frames = snd_pcm_recover(h->alsa, frames, 0);

		/* Problem with ALSA */
		if (frames < 0)
		{
			printf("snd_pcm_writei failed!\n");
			break;
		}

		/* Underrun */
		if (frames > 0 && frames < (long) out_size)
			printf("Short write (expected %li, wrote %li)\n",
			      (long) out_size, frames);
	}

	/* Free buffers */
	free(in_buffer);
	free(out_buffer);

	return NULL;
}


int output_alsa_close(struct output *h)
{
	struct output_stream *s;

	if(h == NULL)
		return 0;

	/* Stop thread */
	h->stop = 1;

	/* Join thread */
	if(pthread_join(h->thread, NULL) < 0)
		return -1;

	/* Free streams */
	while(h->streams != NULL)
	{
		s = h->streams;
		h->streams = s->next;
		output_alsa_free_stream(s);
	}

	/* Close alsa */
	if(h->alsa != NULL)
		snd_pcm_close(h->alsa);

	/* Free structure */
	free(h);

	return 0;
}

struct output_handle output_alsa = {
	.out = NULL,
	.open = &output_alsa_open,
	.add_stream = &output_alsa_add_stream,
	.play_stream = &output_alsa_play_stream,
	.pause_stream = &output_alsa_pause_stream,
	.remove_stream = &output_alsa_remove_stream,
	.close = &output_alsa_close,
};

