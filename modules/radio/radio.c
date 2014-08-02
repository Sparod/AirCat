/*
 * radio.c - A Radio module
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

#include "module.h"
#include "shoutcast.h"
#include "radio_list.h"

struct radio_handle {
	/* Output module */
	struct output_handle *output;
	struct output_stream_handle *stream;
	/* Radio player */
	struct shout_handle *shout;
	struct radio_item *radio;
	/* Databse: radio list */
	struct db_handle *db;
	/* Config part */
	unsigned long cache;
};

static int radio_stop(struct radio_handle *h);
static int radio_set_config(struct radio_handle *h, const struct json *c);

static int radio_open(struct radio_handle **handle, struct module_attr *attr)
{
	struct radio_handle *h;

	/* Allocate structure */
	*handle = malloc(sizeof(struct radio_handle));
	if(*handle == NULL)
		return -1;
	h = *handle;

	/* Init structure */
	h->shout = NULL;
	h->output = attr->output;
	h->db = attr->db;
	h->stream = NULL;
	h->radio = NULL;
	h->cache = 0;

	/* Load configuration */
	radio_set_config(h, attr->config);

	return 0;
}

static int radio_play(struct radio_handle *h, const char *id)
{
	unsigned long samplerate;
	unsigned char channels;

	if(id == NULL)
		return -1;

	/* Stop previous radio */
	radio_stop(h);

	/* Get radio item */
	h->radio = radio_get_radio_item(h->db, id);
	if(h->radio == NULL)
		return -1;

	/* Open radio */
	if(shoutcast_open(&h->shout, h->radio->url) != 0)
		return -1;

	/* Get samplerate and channels */
	samplerate = shoutcast_get_samplerate(h->shout);
	channels = shoutcast_get_channels(h->shout);

	/* Open new Audio stream output and play */
	h->stream = output_add_stream(h->output, NULL, samplerate, channels,
				      h->cache, 1, &shoutcast_read, h->shout);
	output_play_stream(h->output, h->stream);

	return 0;
}

static int radio_stop(struct radio_handle *h)
{
	if(h == NULL || h->shout == NULL)
		return 0;

	if(h->stream != NULL)
		output_remove_stream(h->output, h->stream);

	if(h->shout != NULL)
		shoutcast_close(h->shout);

	if(h->radio != NULL)
		radio_free_radio_item(h->radio);

	h->stream = NULL;
	h->shout = NULL;
	h->radio = NULL;

	return 0;
}

static char *radio_get_json_status(struct radio_handle *h, int add_pic)
{
	struct json *root;
	char *artist = NULL;
	char *title = NULL;
	char *str = NULL;
	unsigned long v;

	/* No radio playing */
	if(h->radio == NULL)
	{
		return strdup("{ \"id\": null }");
	}

	/* Create JSON object */
	root = json_new();
	if(root == NULL)
		return NULL;

	/* Add radio infos */
	json_set_string(root, "id", h->radio->id);
	json_set_string(root, "name", h->radio->name);

	/* Add current title */
	if(h->shout != NULL)
	{
		/* Get string */
		str = shoutcast_get_metadata(h->shout);
		if(str != NULL)
		{
			/* Parse metadata */
			artist = strstr(str, "StreamTitle='");
			if(artist != NULL)
			{
				artist += 13;
				title = strstr(artist, "';");
				if(title != NULL)
				{
					*title = '\0';
					title = strchr(artist, '-');
					if(title != NULL)
					{
						*title = '\0';
						title += 1;
					}
					else
					{
						artist = NULL;
						title = artist;
					}
				}
				else
					artist = NULL;
			}
		}

		/* Add title and artist */
		json_set_string(root, "title", title);
		json_set_string(root, "artist", artist);

		/* Add playing status */
		v = output_get_status_stream(h->output, h->stream,
					     OUTPUT_STREAM_CACHE_STATUS);
		if(v == CACHE_BUFFERING)
		{
			/* Get cache filling rate */
			v = output_get_status_stream(h->output, h->stream,
						   OUTPUT_STREAM_CACHE_FILLING);
			json_set_int(root, "buffering", v);
		}
		v = output_get_status_stream(h->output, h->stream,
					     OUTPUT_STREAM_PLAYED);
		json_set_int(root, "elapsed", v / 1000);

		/* Free string */
		if(str != NULL)
			free(str);
	}

	/* Get JSON string */
	str = strdup(json_export(root));

	/* Free JSON object */
	json_free(root);

	return str;
}

static int radio_set_config(struct radio_handle *h, const struct json *c)
{
	unsigned long cache;
	const char *file;

	if(h == NULL)
		return -1;

	/* Free previous values */
	cache = 0;

	/* Parse config */
	if(c != NULL)
	{
		/* Get cache size (in ms) */
		cache = json_get_int(c, "cache");
	}

	/* Set default values */
	if(cache == 0)
		cache = 5000;

	/* Reload cache */
	if(h->cache != cache)
	{
		h->cache = cache;

		/* Update cache size for output stream */
		if(h->stream != NULL)
			output_set_cache_stream(h->output, h->stream, cache);
	}

	return 0;
}

static struct json *radio_get_config(struct radio_handle *h)
{
	struct json *c;

	c = json_new();
	if(c == NULL)
		return NULL;

	/* Set current cache */
	json_set_int(c, "cache", h->cache);

	return c;
}

static int radio_close(struct radio_handle *h)
{
	if(h == NULL)
		return 0;

	/* Stop radio */
	radio_stop(h);

	/* Stop and close shoutcast player */
	if(h->shout != NULL)
		shoutcast_close(h->shout);

	free(h);

	return 0;
}

static int radio_httpd_play(void *user_data, struct httpd_req *req,
			    struct httpd_res **res)
{
	struct radio_handle *h = user_data;

	/* Play radio */
	radio_play(h, req->resource);

	return 200;
}

static int radio_httpd_stop(void *user_data, struct httpd_req *req,
			    struct httpd_res **res)
{
	struct radio_handle *h = user_data;

	/* Stop current radio */
	radio_stop(h);

	return 200;
}

static int radio_httpd_status(void *user_data, struct httpd_req *req,
			      struct httpd_res **res)
{
	struct radio_handle *h = user_data;
	char *stat;

	/* Get radio status */
	stat = radio_get_json_status(h, 0);
	if(stat == NULL)
	{
		*res = httpd_new_response("No status", 0, 0);
		return 500;
	}

	*res = httpd_new_response(stat, 1, 0);
	return 200;
}

static int radio_httpd_cat_info(void *user_data, struct httpd_req *req,
				struct httpd_res **res)
{
	struct radio_handle *h = user_data;
	char *info;

	/* Get info about category */
	info = radio_get_json_category_info(h->db, req->resource);
	if(info == NULL)
	{
		*res = httpd_new_response("Radio not found", 0, 0);
		return 404;
	}

	*res = httpd_new_response(info, 1, 0);
	return 200;
}

static int radio_httpd_info(void *user_data, struct httpd_req *req,
			    struct httpd_res **res)
{
	struct radio_handle *h = user_data;
	char *info;

	/* Get info about radio */
	info = radio_get_json_radio_info(h->db, req->resource);
	if(info == NULL)
	{
		*res = httpd_new_response("Radio not found", 0, 0);
		return 404;
	}

	*res = httpd_new_response(info, 1, 0);
	return 200;
}

static int radio_httpd_list(void *user_data, struct httpd_req *req,
			    struct httpd_res **res)
{
	struct radio_handle *h = user_data;
	unsigned long page = 0, count = 0;
	const char *value;
	char *list = NULL;

	/* Get page */
	value = httpd_get_query(req, "page");
	if(value != NULL)
		page = strtoul(value, NULL, 10);

	/* Get entries per page */
	value = httpd_get_query(req, "count");
	if(value != NULL)
		count = strtoul(value, NULL, 10);

	/* Get Radio list */
	list = radio_get_json_list(h->db, req->resource, page, count);
	if(list == NULL)
	{
		*res = httpd_new_response("No radio list", 0, 0);
		return 500;
	}

	*res = httpd_new_response(list, 1, 0);
	return 200;
}

static struct url_table radio_url[] = {
	{"/category/info/", HTTPD_EXT_URL, HTTPD_GET, 0, &radio_httpd_cat_info},
	{"/info/",          HTTPD_EXT_URL, HTTPD_GET, 0, &radio_httpd_info},
	{"/list",           HTTPD_EXT_URL, HTTPD_GET, 0, &radio_httpd_list},
	{"/play",           HTTPD_EXT_URL, HTTPD_PUT, 0, &radio_httpd_play},
	{"/stop",           0,             HTTPD_PUT, 0, &radio_httpd_stop},
	{"/status",         HTTPD_EXT_URL, HTTPD_GET, 0, &radio_httpd_status},
	{0, 0, 0}
};

struct module module_entry = {
	.id = "radio",
	.name = "Radio",
	.description = "Listen any radio over the world.",
	.open = (void*) &radio_open,
	.close = (void*) &radio_close,
	.set_config = (void*) &radio_set_config,
	.get_config = (void*) &radio_get_config,
	.urls = (void*) &radio_url,
};
