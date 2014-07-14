/*
 * output_alsa.c - Alsa Audio output module
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
#include <pthread.h>

#include <asoundlib.h>

#include "output_alsa.h"
#include "output.h"

#include "resample.h"
#include "cache.h"

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#define BUFFER_SIZE 8192/2

#ifdef USE_FLOAT
 	#define ALSA_FORMAT SND_PCM_FORMAT_FLOAT
#else
	#define ALSA_FORMAT SND_PCM_FORMAT_S32
#endif

struct output_stream {
	/* Resample object */
	struct resample_handle *res;
	/* Input callback */
	a_read_cb input_callback;
	void *user_data;
	/* Format */
	unsigned long samplerate;
	unsigned char nb_channel;
	/* Stream status */
	int is_playing;
	int end_of_stream;
	unsigned long played;
	/* Stream volume */
	unsigned int volume;
	/* Stream cache */
	struct cache_handle *cache;
	/* Next output stream in list */
	struct output_stream *next;
};

struct output {
	/* ALSA output */
	snd_pcm_t *alsa;
	/* Format */
	unsigned long samplerate;
	unsigned char nb_channel;
	/* General volume */
	unsigned int volume;
	/* Thread objects */
	pthread_t thread;
	pthread_mutex_t mutex;
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
	h->volume = OUTPUT_VOLUME_MAX;

	/* Open alsa device */
	if(snd_pcm_open(&h->alsa, "default", SND_PCM_STREAM_PLAYBACK, 0) < 0)
		return -1;

	/* Set parameters for output */
	ret = snd_pcm_set_params(h->alsa, ALSA_FORMAT,
				 SND_PCM_ACCESS_RW_INTERLEAVED, h->nb_channel,
				 h->samplerate, 1, latency*1000);
	if(ret < 0)
		return -1;

	/* Initialize mutex */
	pthread_mutex_init(&h->mutex, NULL);

	/* Create thread */
	if(pthread_create(&h->thread, NULL, output_alsa_thread, h) != 0)
		return -1;

	return 0;
}

int output_alsa_set_volume(struct output *h, unsigned int volume)
{
	pthread_mutex_lock(&h->mutex);
	h->volume = volume;
	pthread_mutex_unlock(&h->mutex);

	return 0;
}

unsigned int output_alsa_get_volume(struct output *h)
{
	unsigned int volume;

	pthread_mutex_lock(&h->mutex);
	volume = h->volume;
	pthread_mutex_unlock(&h->mutex);

	return volume;
}

struct output_stream *output_alsa_add_stream(struct output *h,
					     unsigned long samplerate,
					     unsigned char nb_channel,
					     unsigned long cache,
					     int use_cache_thread,
					     a_read_cb input_callback,
					     void *user_data)
{
	struct output_stream *s;
	unsigned long size;
	a_write_cb out = NULL;

	/* Alloc the stream handler */
	s = malloc(sizeof(struct output_stream));
	if(s == NULL)
		return NULL;

	/* Fill the handler */
	s->samplerate = samplerate;
	s->nb_channel = nb_channel;
	s->res = NULL;
	s->is_playing = 0;
	s->end_of_stream = 0;
	s->played = 0;
	s->volume = OUTPUT_VOLUME_MAX;
	s->cache = NULL;

	/* Add cache */
	if(input_callback == NULL && cache > 0)
	{
		/* Open a new cache */
		size = h->samplerate * s->nb_channel * cache / 1000;
		if(cache_open(&s->cache, size, 0, NULL, NULL, NULL, NULL) != 0)
			goto error;
		out = &cache_write;
		user_data = s->cache;
	}

	/* Open resample/mixer filter */
	if(resample_open(&s->res, samplerate, nb_channel, h->samplerate,
			 h->nb_channel, input_callback, out, user_data) != 0)
		goto error;
	s->input_callback = &resample_read;
	s->user_data = s->res;

	/* Add cache */
	if(input_callback != NULL && cache > 0)
	{
		/* Open a new cache */
		size = h->samplerate * s->nb_channel * cache / 1000;
		if(cache_open(&s->cache, size, use_cache_thread,
			      s->input_callback, s->user_data, NULL, NULL) != 0)
			goto error;
	}

	/* Replace callback with cache */
	if(s->cache != NULL)
	{
		s->input_callback = &cache_read;
		s->user_data = s->cache;
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
	pthread_mutex_lock(&h->mutex);

	/* Play */
	s->is_playing = 1;

	/* Unlock cache after a flush */
	if(s->cache != NULL)
		cache_unlock(s->cache);

	pthread_mutex_unlock(&h->mutex);

	return 0;
}

int output_alsa_pause_stream(struct output *h, struct output_stream *s)
{
	pthread_mutex_lock(&h->mutex);

	/* Pause */
	s->is_playing = 0;

	pthread_mutex_unlock(&h->mutex);

	return 0;
}

void output_alsa_flush_stream(struct output *h, struct output_stream *s)
{
	pthread_mutex_lock(&h->mutex);

	if(s->cache != NULL)
	{
		/* Flush the cache */
		cache_flush(s->cache);

		/* Must unlock input callback in cache after a flush */
		if(s->is_playing)
			cache_unlock(s->cache);
	}
	s->played = 0;

	pthread_mutex_unlock(&h->mutex);
}

ssize_t output_alsa_write_stream(struct output *h, struct output_stream *s,
				 const unsigned char *buffer, size_t size,
				 struct a_format *fmt)
{
	ssize_t ret;

	pthread_mutex_lock(&h->mutex);

	/* Write data to SR/Mixer filter */
	ret = resample_write(s->res, buffer, size, fmt);

	pthread_mutex_unlock(&h->mutex);

	return ret;
}

int output_alsa_set_volume_stream(struct output *h, struct output_stream *s,
				  unsigned int volume)
{
	pthread_mutex_lock(&h->mutex);
	s->volume = volume;
	pthread_mutex_unlock(&h->mutex);

	return 0;
}

unsigned int output_alsa_get_volume_stream(struct output *h,
					   struct output_stream *s)
{
	unsigned int volume;

	pthread_mutex_lock(&h->mutex);
	volume = s->volume;
	pthread_mutex_unlock(&h->mutex);

	return volume;
}

unsigned long output_alsa_get_status_stream(struct output *h,
					    struct output_stream *s,
					    enum output_stream_key key)
{
	unsigned long ret;

	/* Lock stream access */
	pthread_mutex_lock(&h->mutex);

	switch(key)
	{
		case OUTPUT_STREAM_STATUS:
			if(s->end_of_stream)
				ret = STREAM_ENDED;
			else if(s->is_playing)
				ret = STREAM_PLAYING;
			else
				ret = STREAM_PAUSED;
			break;
		case OUTPUT_STREAM_PLAYED:
			ret = s->played;
			break;
		case OUTPUT_STREAM_CACHE_STATUS:
			if(s->cache != NULL && cache_is_ready(s->cache) == 0)
				ret = CACHE_BUFFERING;
			else
				ret = CACHE_READY;
			break;
		case OUTPUT_STREAM_CACHE_FILLING:
			if(s->cache != NULL)
				ret = cache_get_filling(s->cache);
			else
				ret = 100;
			break;
		default:
			ret = 0;
	}

	/* Unlock stream access */
	pthread_mutex_unlock(&h->mutex);

	return ret;
}

static void output_alsa_free_stream(struct output_stream *s)
{
	/* Free cache buffer */
	if(s->cache != NULL)
		cache_close(s->cache);

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
	pthread_mutex_lock(&h->mutex);
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
	pthread_mutex_unlock(&h->mutex);

	/* Free stream */
	output_alsa_free_stream(s);

	return 0;
}

#ifdef USE_FLOAT
static inline float output_alsa_vol(float x, unsigned int v)
{
	return x * (v * 1.0 / OUTPUT_VOLUME_MAX);
}

static inline float output_alsa_add(float a, float b)
{
	float sum;

	sum = a + b;

	if(sum > 1.0)
		sum = 1.0;
	else if(sum < -1.0)
		sum = -1.0;

	return sum;
}
#else
static inline int32_t output_alsa_vol(int32_t x, unsigned int v)
{
	int64_t value;

	value = ((int64_t)x * v) / OUTPUT_VOLUME_MAX;

	return (int32_t) value;
}

static inline int32_t output_alsa_add(int32_t a, int32_t b)
{
	int64_t sum;

	sum = (int64_t)a + (int64_t)b;

	/* Introduce some distorsion */
	/*if(a > 0 && b > 0)
		sum -= (int64_t)a * (int64_t)b / 0x7FFFFFFFLL;
	else if(a < 0 && b < 0)
		sum -= (int64_t)a * (int64_t)b / -0x80000000LL;*/

	if(sum > 0x7FFFFFFFLL)
		sum = 0x7FFFFFFFLL;
	else if(sum < -0x80000000LL)
		sum = -0x80000000LL;

	return (int32_t) sum;
}
#endif

static int output_alsa_mix_streams(struct output *h, unsigned char *in_buffer,
				   unsigned char *out_buffer, size_t len)
{
	struct output_stream *s;
	struct a_format fmt = A_FORMAT_INIT;
#ifdef USE_FLOAT
	float *p_in = (float*) in_buffer;
	float *p_out = (float*) out_buffer;
	float sample;
#else
	int32_t *p_in = (int32_t*) in_buffer;
	int32_t *p_out = (int32_t*) out_buffer;
	int32_t sample;
#endif
	int out_size = 0;
	int first = 1;
	int in_size;
	int i;

	pthread_mutex_lock(&h->mutex);
	for(s = h->streams; s != NULL; s = s->next)
	{
		if(!s->is_playing || s->end_of_stream)
			continue;

		/* Get input data */
		in_size = s->input_callback(s->user_data, in_buffer, len, &fmt);
		if(in_size <= 0)
		{
			if(in_size < 0)
				s->end_of_stream = 1;
			continue;
		}

		/* Update played value (in ms) */
		s->played += in_size * 1000 / h->samplerate / h->nb_channel;

		/* Add it to output buffer */
		if(first)
		{
			first = 0;
			for(i = 0; i < in_size; i++)
			{
				sample = output_alsa_vol(p_in[i], s->volume);
				p_out[i] = sample;
			}
		}
		else
		{
			/* Add it to output buffer */
			for(i = 0; i < in_size; i++)
			{
				sample = output_alsa_vol(p_in[i], s->volume);
				p_out[i] = output_alsa_add(p_out[i], sample);
			}
		}

		/* Update out_size */
		if(out_size < in_size);
			out_size = in_size;
	}
	pthread_mutex_unlock(&h->mutex);

	return out_size;
}

static void *output_alsa_thread(void *user_data)
{
	struct output *h = (struct output *) user_data;
	snd_pcm_sframes_t frames;
	unsigned char *in_buffer, *out_buffer;
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
		frames = snd_pcm_writei(h->alsa, out_buffer, out_size);

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

	/* Free ALSA config */
	snd_config_update_free_global();

	/* Free structure */
	free(h);

	return 0;
}

struct output_module output_alsa = {
	.open = (void*) &output_alsa_open,
	.set_volume = (void*) &output_alsa_set_volume,
	.get_volume = (void*) &output_alsa_get_volume,
	.add_stream = (void*) &output_alsa_add_stream,
	.play_stream = (void*) &output_alsa_play_stream,
	.pause_stream = (void*) &output_alsa_pause_stream,
	.flush_stream = (void*) &output_alsa_flush_stream,
	.write_stream = (void*) &output_alsa_write_stream,
	.set_volume_stream = (void*) &output_alsa_set_volume_stream,
	.get_volume_stream = (void*) &output_alsa_get_volume_stream,
	.get_status_stream = (void*) &output_alsa_get_status_stream,
	.remove_stream = (void*) &output_alsa_remove_stream,
	.close = (void*) &output_alsa_close,
};
