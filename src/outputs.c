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
#include <pthread.h>

#include "outputs.h"
#include "output_alsa.h"
#include "utils.h"

#define FREE_STRING(s) if(s != NULL) free(s);

#define NO_ID NULL
#define NO_NAME "No output"
#define NO_DESCRIPTION ""

struct output_stream_handle {
	/* Stream properties */
	char *id;
	char *name;
	unsigned long samplerate;
	unsigned char channels;
	unsigned int volume;
	int cache;
	int use_cache_thread;
	void *input_callback;
	void *user_data;
	/* Stream status */
	int is_playing;
	/* Next stream in list */
	struct output_stream_handle *next;
	/* Output stream module handle */
	void *stream;
};

struct output_handle {
	/* Output name */
	char *id;
	char *name;
	/* Global volume for the handle */
	unsigned int volume;
	/* Outputs associated handle */
	struct outputs_handle *outputs;
	pthread_mutex_t *mutex;
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
	unsigned int volume;
	/* Mutex for thread-safe */
	pthread_mutex_t mutex;
};

static int output_reset_volume_stream(struct outputs_handle *h,
				      struct output_handle *handle,
				      struct output_stream_handle *stream);

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

	/* Init thread mutex */
	pthread_mutex_init(&h->mutex, NULL);

	/* Set configuration */
	outputs_set_config(h, config);

	return 0;
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

static void outputs_reload(struct outputs_handle *h, struct output_list *new,
			   unsigned long samplerate, unsigned char channels)
{
	struct output_stream_handle *stream;
	struct output_handle *handle;

	/* Update value */
	h->samplerate = samplerate;
	h->channels = channels;

	/* Close previous output module */
	if(h->current != NULL && h->mod != NULL && h->handle != NULL)
	{
		/* Close output */
		h->mod->close(h->handle);
		h->handle = NULL;
		h->current = NULL;
		h->mod = NULL;
	}

	/* Open new output module and reload all streams */
	if(new != NULL)
	{
		h->current = new;
		h->mod = h->current->mod;

		/* Open output module */
		if(h->mod->open(&h->handle, h->samplerate, h->channels)
		   != 0)
		{
			h->mod->close(h->handle);
			h->handle = NULL;
			return;
		}

		/* Reload streams */
		for(handle = h->handles; handle != NULL;
		    handle = handle->next)
		{
			for(stream = handle->streams; stream != NULL;
			    stream = stream->next)
			{
				/* Add stream */
				stream->stream = h->mod->add_stream(h->handle,
						       stream->samplerate,
						       stream->channels,
						       stream->cache,
						       stream->use_cache_thread,
						       stream->input_callback,
						       stream->user_data);

				/* Reset volume */
				output_reset_volume_stream(h, handle, stream);

				/* Play stream */
				if(stream->stream != NULL && stream->is_playing)
					h->mod->play_stream(h->handle,
							    stream->stream);
			}
		}
	}
}

int outputs_set_config(struct outputs_handle *h, struct json *cfg)
{
	struct output_list *current = NULL;
	unsigned long samplerate = 0;
	unsigned char channels = 0;
	const char *id;

	if(h == NULL)
		return -1;

	/* Lock output access */
	pthread_mutex_lock(&h->mutex);

	/* Free all configuration */
	current = NULL;
	samplerate = 0;
	channels = 0;
	h->volume = OUTPUT_VOLUME_MAX;

	/* Get configuration */
	if(cfg != NULL)
	{
		id = json_get_string(cfg, "name");
		current = outputs_find_module(h, id);
		samplerate = json_get_int(cfg, "samplerate");
		channels = json_get_int(cfg, "channels");
		h->volume = json_has_key(cfg, "volume") ?
						   json_get_int(cfg, "volume") :
						   OUTPUT_VOLUME_MAX;
	}

	/* Set default values */
	if(current == NULL)
	{
		/* Choose ALSA as defaut module */
		current = outputs_find_module(h, "alsa");
	}
	if(samplerate == 0)
		samplerate = 44100;
	if(channels == 0)
		channels = 2;
	if(h->volume > OUTPUT_VOLUME_MAX)
		h->volume = OUTPUT_VOLUME_MAX;

	/* Reload output */
	if(current != h->current || samplerate != h->samplerate ||
	   channels != h->channels)
		outputs_reload(h, current, samplerate, channels);

	/* Unlock output access */
	pthread_mutex_unlock(&h->mutex);

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

	/* Lock output access */
	pthread_mutex_lock(&h->mutex);

	/* Fill configuration */
	if(h->current != NULL)
		name = h->current->id;
	json_set_string(cfg, "id", name);
	json_set_int(cfg, "samplerate", h->samplerate);
	json_set_int(cfg, "channels", h->channels);
	json_set_int(cfg, "volume", h->volume);

	/* Unlock output access */
	pthread_mutex_unlock(&h->mutex);

	return cfg;
}

int outputs_set_volume(struct outputs_handle *h, unsigned int volume)
{
	int ret = -1;

	if(h == NULL)
		return -1;

	/* Lock output access */
	pthread_mutex_lock(&h->mutex);

	if(h->current != NULL && h->mod != NULL && h->handle != NULL)
		ret = h->mod->set_volume(h->handle, volume);
	h->volume = volume;

	/* Unlock output access */
	pthread_mutex_unlock(&h->mutex);

	return ret;
}
unsigned int outputs_get_volue(struct outputs_handle *h)
{
	unsigned int vol = 0;

	if(h == NULL)
		return 0;

	/* Lock output access */
	pthread_mutex_lock(&h->mutex);

	if(h->current != NULL && h->mod != NULL && h->handle != NULL)
		vol = h->mod->get_volume(h->handle);

	/* Unlock output access */
	pthread_mutex_unlock(&h->mutex);

	return vol;
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
	h->id = random_string(10);
	h->name = strdup(name);
	h->outputs = outputs;
	h->mutex = &outputs->mutex;
	h->streams = NULL;
	h->volume = OUTPUT_VOLUME_MAX;

	/* Lock output access */
	pthread_mutex_lock(h->mutex);

	/* Add output handle to outputs */
	h->next = outputs->handles;
	outputs->handles = h;

	/* Unlock output access */
	pthread_mutex_unlock(h->mutex);

	return 0;
}

struct output_stream_handle *output_add_stream(struct output_handle *h,
					       const char *name,
					       unsigned long samplerate,
					       unsigned char channels,
					       unsigned long cache,
					       int use_cache_thread,
					       a_read_cb input_callback,
					       void *user_data)
{
	struct output_stream_handle *s = NULL;
	struct output_stream *stream;

	/* Lock output access */
	pthread_mutex_lock(h->mutex);

	/* Check output module */
	if(h == NULL || h->outputs->mod == NULL)
		goto end;

	/* Add stream to output module */
	stream = h->outputs->mod->add_stream(h->outputs->handle, samplerate,
					     channels, cache, use_cache_thread,
					     input_callback, user_data);
	if(stream == NULL)
		goto end;

	/* Alloc stream structure */
	s = malloc(sizeof(struct output_stream_handle));
	if(s == NULL)
		goto end;

	/* Fill handle */
	s->id = random_string(10);
	s->name = name ? strdup(name) : NULL;
	s->samplerate = samplerate;
	s->channels = channels;
	s->cache = cache;
	s->use_cache_thread = use_cache_thread;
	s->input_callback = input_callback;
	s->user_data = user_data;
	s->stream = stream;
	s->is_playing = 0;
	s->volume = OUTPUT_VOLUME_MAX;

	/* Reset volume */
	output_reset_volume_stream(h->outputs, h, s);

	/* Add stream to stream list */
	s->next = h->streams;
	h->streams = s;

end:
	/* Unlock output access */
	pthread_mutex_unlock(h->mutex);

	return s;
}

void output_remove_stream(struct output_handle *h,
			  struct output_stream_handle *s)
{
	struct output_stream_handle **lp, *l;

	if(h == NULL || s == NULL)
		return;

	/* Lock output access */
	pthread_mutex_lock(h->mutex);

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

	/* Unlock output access */
	pthread_mutex_unlock(h->mutex);

	/* Free stream name */
	free(s->name);
	free(s->id);

	/* Free structure */
	free(s);
}

int output_set_volume(struct output_handle *h, unsigned int volume)
{
	int ret = -1;

	if(h == NULL)
		return -1;

	/* Lock output access */
	pthread_mutex_lock(h->mutex);

	/* Set volume */
	h->volume = volume;

	/* Reload volume for all streams */
	output_reset_volume_stream(h->outputs, h, NULL);

	/* Unlock output access */
	pthread_mutex_unlock(h->mutex);

	return ret;
}

unsigned int output_get_volue(struct output_handle *h)
{
	unsigned int vol = 0;

	if(h == NULL)
		return 0;

	/* Lock output access */
	pthread_mutex_lock(h->mutex);

	/* Get current volume */
	vol = h->volume;

	/* Unlock output access */
	pthread_mutex_unlock(h->mutex);

	return vol;
}

void output_close(struct output_handle *h)
{
	struct output_handle **lp, *l;

	if(h == NULL)
		return;

	/* Lock output access */
	pthread_mutex_lock(h->mutex);

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

	/* Unlock output access */
	pthread_mutex_unlock(h->mutex);

	/* Free output name */
	free(h->name);
	free(h->id);

	/* Free structure */
	free(h);
}

/******************************************************************************
 *                                Stream part                                 *
 ******************************************************************************/

int output_play_stream(struct output_handle *h, struct output_stream_handle *s)
{
	int ret = -1;

	if(h == NULL || s == NULL)
		return -1;

	/* Lock output access */
	pthread_mutex_lock(h->mutex);

	/* Check output module */
	if(h->outputs != NULL && h->outputs->mod != NULL &&
	   h->outputs->handle != NULL && s->stream != NULL)
	{
		/* Play stream */
		ret = h->outputs->mod->play_stream(h->outputs->handle,
						   s->stream);
	}
	s->is_playing = 1;

	/* Unlock output access */
	pthread_mutex_unlock(h->mutex);

	return ret;
}

int output_pause_stream(struct output_handle *h, struct output_stream_handle *s)
{
	int ret = -1;

	if(h == NULL || s == NULL)
		return -1;

	/* Lock output access */
	pthread_mutex_lock(h->mutex);

	/* Check output module */
	if(h->outputs != NULL && h->outputs->mod != NULL &&
	   h->outputs->handle != NULL && s->stream != NULL)
	{
		/* Pause stream */
		ret = h->outputs->mod->pause_stream(h->outputs->handle,
						    s->stream);
	}
	s->is_playing = 0;

	/* Unlock output access */
	pthread_mutex_unlock(h->mutex);

	return ret;
}

static int output_reset_volume_stream(struct outputs_handle *h,
				      struct output_handle *handle,
				      struct output_stream_handle *stream)
{
	struct output_stream_handle *s;
	struct output_handle *l;
	unsigned long vol;

	if(h == NULL || h->mod == NULL || h->handle == NULL)
		return -1;

	for(l = (handle == NULL ? h->handles : handle); l != NULL;
	    l = handle == NULL ? l->next : NULL)
	{
		for(s = (stream == NULL ? l->streams : stream); s != NULL;
		    s = stream == NULL ? s->next : NULL)
		{
			if(s->stream == NULL)
				continue;

			/* Calculate volume */
			vol = s->volume * l->volume / OUTPUT_VOLUME_MAX;
			vol = vol * h->volume / OUTPUT_VOLUME_MAX;

			/* Set stream volume */
			h->mod->set_volume_stream(h->handle, s->stream, vol);
		}
	}

	return 0;
}

int output_set_volume_stream(struct output_handle *h,
			     struct output_stream_handle *s,
			     unsigned int volume)
{
	int ret = -1;

	if(h == NULL || s == NULL)
		return -1;

	/* Lock output access */
	pthread_mutex_lock(h->mutex);

	/* Set volume */
	s->volume = volume;

	/* Set stream volume */
	ret = output_reset_volume_stream(h->outputs, h, s);

	/* Unlock output access */
	pthread_mutex_unlock(h->mutex);

	return ret;
}

unsigned int output_get_volume_stream(struct output_handle *h,
				      struct output_stream_handle *s)
{
	unsigned int ret = 0;

	if(h == NULL || s == NULL)
		return 0;

	/* Lock output access */
	pthread_mutex_lock(h->mutex);

	/* Check output module */
	if(h->outputs != NULL && h->outputs->mod != NULL &&
	   h->outputs->handle != NULL && s->stream != NULL)
	{
		/* Get stream volume */
		ret = h->outputs->mod->get_volume_stream(h->outputs->handle,
							 s->stream);
	}
	s->volume = ret;

	/* Unlock output access */
	pthread_mutex_unlock(h->mutex);

	return ret;
}

unsigned long output_get_status_stream(struct output_handle *h,
				       struct output_stream_handle *s,
				       enum output_stream_key key)
{
	unsigned long ret = 0;

	if(h == NULL || s == NULL)
		return 0;

	/* Lock output access */
	pthread_mutex_lock(h->mutex);

	/* Check output module */
	if(h->outputs != NULL && h->outputs->mod != NULL &&
	   h->outputs->handle != NULL && s->stream != NULL)
	{
		/* Get stream status */
		ret = h->outputs->mod->get_status_stream(h->outputs->handle,
							 s->stream, key);
	}

	/* Unlock output access */
	pthread_mutex_unlock(h->mutex);

	return ret;
}

/******************************************************************************
 *                              Output URLs part                              *
 ******************************************************************************/

static int outputs_find_stream_from_url(struct outputs_handle *h,
					const char *url,
					struct output_handle **handle,
					struct output_stream_handle **stream,
					int is_get)
{
	struct output_stream_handle *s;
	struct output_handle *l;
	char *str, *idx;
	int len;

	if(h == NULL || url == NULL)
		return -1;

	/* Get handle name */
	str = strchr(url, '/');
	len = str == NULL ? strlen(url) : str - url;

	/* Parse all handle by name */
	for(l = h->handles; l != NULL; l = l->next)
	{
		/* Check handle name */
		if(strncmp(l->id, url, len) != 0 || strlen(l->id) != len)
			continue;
		*handle = l;
		if((is_get && url[len] != '/') ||
		   (!is_get && strrchr(url+len, '/') == url+len))
			return 0;

		/* Get index */
		idx = (char*)url + len + 1;
		len = is_get ? strlen(idx) : strrchr(url+len, '/') - idx;

		/* Parse streams */
		for(s = l->streams; s != NULL; s = s->next)
		{
			if(strncmp(s->id, idx, len) == 0 &&
			   strlen(s->id) == len)
			{
				*stream = s;
				return 0;
			}
		}
		return -1;
	}

	return -1;
}

static int outputs_httpd_volume(struct outputs_handle *h, struct httpd_req *req,
			      unsigned char **buffer, size_t *size)
{
	struct output_stream_handle *s = NULL;
	struct output_handle *handle = NULL;
	unsigned int vol = 0;
	struct json *json;
	int is_master;
	char *str;

	/* Check HTTP method */
	if(req->method == HTTPD_GET)
	{
		/* Create a JSON object */
		json = json_new();

		/* Lock output access */
		pthread_mutex_lock(&h->mutex);

		/* Check URL */
		if(req->resource == NULL || *req->resource == '\0')
		{
			/* Add volume */
			json_set_int(json, "volume", h->volume);
		}
		else
		{
			/* Get stream from url */
			if(outputs_find_stream_from_url(h, req->resource,
							&handle, &s, 1) == 0)
			{
				vol = s == NULL ? handle->volume : s->volume;
				json_set_int(json, "volume", vol);
			}
		}

		/* Unlock output access */
		pthread_mutex_unlock(&h->mutex);

		/* Get JSON string */
		str = strdup(json_export(json));
		*buffer = (unsigned char*) str;
		if(str != NULL)
			*size = strlen(str);

		/* Free JSON object */
		json_free(json);
	}
	else
	{
		/* Get volume at end of URL */
		str = strrchr(req->resource, '/');
		if(str == NULL)
		{
			is_master = 1;
			str = (char*) req->resource;
		}
		else
		{
			is_master = 0;
			str++;
		}
		if(*str < 48 || *str > 57)
			return 400;

		/* Convert volume */
		vol = strtoul(str, NULL, 10);
		if(vol > OUTPUT_VOLUME_MAX)
			vol = OUTPUT_VOLUME_MAX;

		/* Lock output access */
		pthread_mutex_lock(&h->mutex);

		/* Check URL */
		if(is_master)
		{
			if(h->mod != NULL && h->handle != NULL)
				h->mod->set_volume(h->handle, vol);
			h->volume = vol;
		}
		else
		{
			/* Get stream from url */
			if(outputs_find_stream_from_url(h, req->resource,
							&handle, &s, 0) == 0)
			{
				if(s == NULL)
					handle->volume = vol;
				else
					s->volume = vol;
			}
		}
		output_reset_volume_stream(h, handle, s);

		/* Unlock output access */
		pthread_mutex_unlock(&h->mutex);
	}

	return 200;
}

static int outputs_httpd_status(struct outputs_handle *h, struct httpd_req *req,
			      unsigned char **buffer, size_t *size)
{
	struct json *root, *list, *list2, *tmp, *tmp2;
	struct output_stream_handle *s;
	struct output_handle *l;
	char *str;

	/* Create a new object */
	root = json_new();

	/* Lock output access */
	pthread_mutex_lock(&h->mutex);

	/* Get output configuration */
	if(h->current != NULL)
	{
		json_set_string(root, "id", h->current->id);
		json_set_string(root, "name", h->current->name);
		json_set_string(root, "description", h->current->description);
	}
	else
	{
		json_set_string(root, "id", NO_ID);
		json_set_string(root, "name", NO_NAME);
		json_set_string(root, "description", NO_DESCRIPTION);
	}
	json_set_int(root, "samplerate", h->samplerate);
	json_set_int(root, "channels", h->channels);
	json_set_int(root, "volume", h->volume);

	/* Create a new JSON array */
	list = json_new_array();
	if(list != NULL)
	{
		for(l = h->handles; l != NULL; l = l->next)
		{
			/* Create a new object */
			tmp = json_new();
			if(tmp == NULL)
				continue;

			/* Get name */
			json_set_string(tmp, "id", l->id);
			json_set_string(tmp, "name", l->name);
			json_set_int(tmp, "volume", l->volume);

			/* Create stream array */
			list2 = json_new_array();
			for(s = l->streams; s != NULL; s = s->next)
			{
				/* Create a new object */
				tmp2 = json_new();
				if(tmp2 == NULL)
					continue;

				/* Get stream configuration */
				json_set_string(tmp2, "id", s->id);
				json_set_string(tmp2, "name", s->name);
				json_set_int(tmp2, "samplerate", s->samplerate);
				json_set_int(tmp2, "channels", s->channels);
				json_set_int(tmp2, "volume", s->volume);

				/* Add object to array */
				if(json_array_add(list2, tmp2) != 0)
					json_free(tmp2);
			}

			/* Add stream list to JSON object */
			json_add(tmp, "streams", list2);

			/* Add object to array */
			if(json_array_add(list, tmp) != 0)
				json_free(tmp);
		}
	}

	/* Unlock output access */
	pthread_mutex_unlock(&h->mutex);

	/* Add list to JSON object */
	json_add(root, "outputs", list);

	/* Get JSON string */
	str = strdup(json_export(root));
	*buffer = (unsigned char*) str;
	if(str != NULL)
		*size = strlen(str);

	/* Free JSON object */
	json_free(root);

	return 200;
}

static int outputs_httpd_list(struct outputs_handle *h, struct httpd_req *req,
			      unsigned char **buffer, size_t *size)
{
	struct json *root, *tmp;
	struct output_list *l;
	char *str;

	/* Create a new JSON array */
	root = json_new_array();

	/* Lock output access */
	pthread_mutex_lock(&h->mutex);

	/* Fill array */
	if(root != NULL)
	{
		/* Create a new object */
		tmp = json_new();
		if(tmp != NULL)
		{
			/* Add No output */
			json_set_string(tmp, "id", NO_ID);
			json_set_string(tmp, "name", NO_NAME);
			json_set_string(tmp, "description", NO_DESCRIPTION);

			/* Add object to array */
			if(json_array_add(root, tmp) != 0)
				json_free(tmp);
		}

		/* Get output module list */
		for(l = h->list; l != NULL; l = l->next)
		{
			/* Create a new object */
			tmp = json_new();
			if(tmp == NULL)
				continue;

			/* Get properties */
			json_set_string(tmp, "id", l->id);
			json_set_string(tmp, "name", l->name);
			json_set_string(tmp, "description", l->description);

			/* Add object to array */
			if(json_array_add(root, tmp) != 0)
				json_free(tmp);
		}
	}

	/* Unlock output access */
	pthread_mutex_unlock(&h->mutex);

	/* Get JSON string */
	str = strdup(json_export(root));
	*buffer = (unsigned char*) str;
	if(str != NULL)
		*size = strlen(str);

	/* Free JSON object */
	json_free(root);

	return 200;
}

struct url_table outputs_urls[] = {
	{"/volume", HTTPD_EXT_URL, HTTPD_GET | HTTPD_PUT, 0,
						 (void*) &outputs_httpd_volume},
	{"/status", 0,             HTTPD_GET,             0,
						 (void*) &outputs_httpd_status},
	{"/list",   0,             HTTPD_GET,             0,
						   (void*) &outputs_httpd_list},
	{0, 0, 0, 0}
};
