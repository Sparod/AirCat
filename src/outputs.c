/*
 * outputs.c - Audio output module
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

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "outputs.h"
#include "output_alsa.h"

#define FREE_STRING(s) if(s != NULL) free(s);

struct output_stream_handle {
	/* Stream properties */
	char *name;
	unsigned long samplerate;
	unsigned char channels;
	int cache;
	int use_cache_thread;
	void *input_callback;
	void *user_data;
	/* Next stream in list */
	struct output_stream_handle *next;
	/* Output stream module handle */
	void *stream;
};

struct output_handle {
	/* Output name */
	char *name;
	/* Outputs associated handle */
	struct outputs_handle *outputs;
	/* Streams output list */
	struct output_stream_handle *streams;
	/* Next output in list */
	struct output_handle *next;
};

struct output_list {
	/* Output properties */
	char *id;
	char *name;
	char *description;
	/* Output pointers */
	struct output_module *mod;
	/* Next output in list */
	struct output_list *next;
};

struct outputs_handle {
	/* Output module list */
	int output_count;
	struct output_list *list;
	/* Current output module */
	void *handle;
	struct output_module *mod;
	struct output_list *current;
	struct output_handle *handles;
	/* Configuration */
	unsigned long samplerate;
	unsigned char channels;
};

int outputs_open(struct outputs_handle **handle, struct json *config)
{
	struct outputs_handle *h;
	struct output_list list[] = {
		{"alsa", "ALSA", "ALSA audio output.", &output_alsa, NULL},
	};
	struct output_list *l;
	int i;

	/* Allocate structure */
	*handle = malloc(sizeof(struct outputs_handle));
	if(*handle == NULL)
		return -1;
	h = *handle;

	/* Init structure */
	h->list = NULL;
	h->output_count = 0;
	h->mod = NULL;
	h->current = NULL;
	h->handle = NULL;
	h->handles = NULL;

	/* Create output list */
	for(i = 0; i < sizeof(list)/sizeof(struct output_list); i++)
	{
		/* Add output module */
		l = malloc(sizeof(struct output_list));
		if(l != NULL)
		{
			l->id = list[i].id ? strdup(list[i].id) : NULL;
			l->name =  list[i].name ? strdup(list[i].name) : NULL;
			l->description =  list[i].description != NULL ?
					     strdup(list[i].description) : NULL;
			l->mod = list[i].mod;
			l->next = h->list;
			h->list = l;
			h->output_count++;
		}
	}

	/* Set configuration */
	outputs_set_config(h, config);

	/* Dummy output */
	if(h->current == NULL || h->mod == NULL)
		return 0;

	/* Open output */
	return h->mod->open(&h->handle, h->samplerate, h->channels);
}

static struct output_list *outputs_find_module(struct outputs_handle *h,
					       const char *id)
{
	struct output_list *l;

	if(h == NULL || id == NULL)
		return NULL;

	/* Look for output id */
	for(l = h->list; l != NULL; l = l->next)
		if(strcmp(l->id, id) == 0)
			return l;

	return NULL;
}

int outputs_set_config(struct outputs_handle *h, struct json *cfg)
{
	struct output_list *l;
	const char *id;

	if(h == NULL)
		return -1;

	/* Free all configuration */
	h->current = NULL;
	h->mod = NULL;
	h->samplerate = 0;
	h->channels = 0;

	/* Get configuration */
	if(cfg != NULL)
	{
		id = json_get_string(cfg, "name");
		l = outputs_find_module(h, id);
		if(l != NULL)
		{
			h->current = l;
			h->mod = l->mod;
		}
		h->samplerate = json_get_int(cfg, "samplerate");
		h->channels = json_get_int(cfg, "channels");
	}

	/* Set default values */
	if(h->current == NULL)
	{
		/* Choose ALSA as defaut module */
		l = outputs_find_module(h, "alsa");
		if(l != NULL)
			h->current = l;
		if(h->current != NULL)
			h->mod = h->current->mod;
	}
	if(h->samplerate == 0)
		h->samplerate = 44100;
	if(h->channels == 0)
		h->channels = 2;

	return 0;
}

struct json *outputs_get_config(struct outputs_handle *h)
{
	struct json *cfg;
	char *name = NULL;

	if(h == NULL)
		return NULL;

	/* Create a JSON object */
	cfg = json_new();
	if(cfg == NULL)
		return NULL;

	/* Fill configuration */
	if(h->current != NULL)
		name = h->current->id;
	json_set_string(cfg, "name", name);
	json_set_int(cfg, "samplerate", h->samplerate);
	json_set_int(cfg, "channels", h->channels);

	return cfg;
}

void outputs_close(struct outputs_handle *h)
{
	struct output_handle *handle;
	struct output_list *l;

	if(h == NULL)
		return;

	/* Close all output_handles */
	while(h->handles != NULL)
	{
		handle = h->handles;
		h->handles = handle->next;

		output_close(handle);
	}

	/* Close output */
	if(h->current != NULL && h->mod != NULL && h->handle != NULL)
		h->mod->close(h->handle);

	/* Free output list */
	while(h->list != NULL)
	{
		l = h->list;
		h->list = l->next;

		/* Free strings */
		FREE_STRING(l->id);
		FREE_STRING(l->name);
		FREE_STRING(l->description);

		/* Free entry */
		free(l);
	}

	free(h);
}

/******************************************************************************
 *                                Output part                                 *
 ******************************************************************************/

int output_open(struct output_handle **handle, struct outputs_handle *outputs,
		const char *name)
{
	struct output_handle *h;

	/* Check name */
	if(outputs == NULL || name == NULL || *name == '\0')
		return -1;

	/* Allocate structure */
	*handle = malloc(sizeof(struct output_handle));
	if(*handle == NULL)
		return -1;
	h = *handle;

	/* Init structure */
	h->name = strdup(name);
	h->outputs = outputs;
	h->streams = NULL;

	/* Add output handle to outputs */
	h->next = outputs->handles;
	outputs->handles = h;

	return 0;
}

struct output_stream_handle *output_add_stream(struct output_handle *h,
					       const char *name,
					       unsigned long samplerate,
					       unsigned char channels,
					       unsigned long cache,
					       int use_cache_thread,
					       void *input_callback,
					       void *user_data)
{
	struct output_stream_handle *s;
	struct output_stream *stream;

	/* Check output module */
	if(h == NULL || h->outputs->mod == NULL)
		return NULL;

	/* Add stream to output module */
	stream = h->outputs->mod->add_stream(h->outputs->handle, samplerate,
					     channels, cache, use_cache_thread,
					     input_callback, user_data);
	if(stream == NULL)
		return NULL;

	/* Alloc stream structure */
	s = malloc(sizeof(struct output_stream_handle));
	if(s == NULL)
		return NULL;

	/* Fill handle */
	s->name = name ? strdup(name) : NULL;
	s->samplerate = samplerate;
	s->channels = channels;
	s->cache = cache;
	s->use_cache_thread = use_cache_thread;
	s->input_callback = input_callback;
	s->user_data = user_data;
	s->stream = stream;

	/* Add stream to stream list */
	s->next = h->streams;
	h->streams = s;

	return s;
}

void output_remove_stream(struct output_handle *h,
			  struct output_stream_handle *s)
{
	struct output_stream_handle **lp, *l;

	if(h == NULL || s == NULL)
		return;

	/* Remove stream from streams */
	lp = &h->streams;
	while((*lp) != NULL)
	{
		l = *lp;
		if(l == s)
		{
			*lp = l->next;
			break;
		}
		else
			lp = &l->next;
	}

	/* Remove stream from output module */
	h->outputs->mod->remove_stream(h->outputs->handle, s->stream);

	/* Free stream name */
	free(s->name);

	/* Free structure */
	free(s);
}

void output_close(struct output_handle *h)
{
	struct output_handle **lp, *l;

	if(h == NULL)
		return;

	/* Close and remove all streams */
	while(h->streams != NULL)
	{
		/* Remove stream */
		output_remove_stream(h, h->streams);
	}

	/* Remove output from outputs */
	lp = &h->outputs->handles;
	while((*lp) != NULL)
	{
		l = *lp;
		if(l == h)
		{
			*lp = l->next;
			break;
		}
		else
			lp = &l->next;
	}

	/* Free output name */
	free(h->name);

	/* Free structure */
	free(h);
}

/******************************************************************************
 *                                Stream part                                 *
 ******************************************************************************/

int output_play_stream(struct output_handle *h, struct output_stream_handle *s)
{
	/* Check output module */
	if(h == NULL || h->outputs == NULL || h->outputs->mod == NULL ||
	   h->outputs->handle == NULL || s == NULL || s->stream == NULL)
		return -1;

	return h->outputs->mod->play_stream(h->outputs->handle, s->stream);
}

int output_pause_stream(struct output_handle *h, struct output_stream_handle *s)
{
	/* Check output module */
	if(h == NULL || h->outputs == NULL || h->outputs->mod == NULL ||
	   h->outputs->handle == NULL || s == NULL || s->stream == NULL)
		return -1;

	return h->outputs->mod->pause_stream(h->outputs->handle, s->stream);
}

int output_set_volume_stream(struct output_handle *h,
			     struct output_stream_handle *s,
			     unsigned int volume)
{
	/* Check output module */
	if(h == NULL || h->outputs == NULL || h->outputs->mod == NULL ||
	   h->outputs->handle == NULL || s == NULL || s->stream == NULL)
		return -1;

	return h->outputs->mod->set_volume_stream(h->outputs->handle, s->stream,
						  volume);
}

unsigned int output_get_volume_stream(struct output_handle *h,
				      struct output_stream_handle *s)
{
	/* Check output module */
	if(h == NULL || h->outputs == NULL || h->outputs->mod == NULL ||
	   h->outputs->handle == NULL || s == NULL || s->stream == NULL)
		return 0;

	return h->outputs->mod->get_volume_stream(h->outputs->handle,
						  s->stream);
}
