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

#include <json.h>

#include "module.h"
#include "shoutcast.h"
#include "radio_list.h"

struct radio_handle {
	/* Output module */
	struct output_handle *output;
	struct output_stream *stream;
	/* Radio player */
	struct shout_handle *shout;
	struct radio_item *radio;
	/* Radio list */
	struct radio_list_handle *list;
	/* Config part */
	char *list_file;
};

static int radio_stop(struct radio_handle *h);
static int radio_set_config(struct radio_handle *h, const struct config *c);

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
	h->list = NULL;
	h->output = attr->output;
	h->stream = NULL;
	h->radio = NULL;
	h->list_file = NULL;

	/* Load configuration */
	radio_set_config(h, attr->config);

	/* Load radio list */
	if(radio_list_open(&h->list, h->list_file) != 0)
		return -1;

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
	h->radio = radio_list_get_radio(h->list, id);
	if(h->radio == NULL)
		return -1;

	/* Open radio */
	if(shoutcast_open(&h->shout, h->radio->url) != 0)
		return -1;

	/* Get samplerate and channels */
	samplerate = shoutcast_get_samplerate(h->shout);
	channels = shoutcast_get_channels(h->shout);

	/* Open new Audio stream output and play */
	h->stream = output_add_stream(h->output, samplerate, channels, 1000, 1,
				      &shoutcast_read, h->shout);
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
		radio_list_free_radio_item(h->radio);

	h->stream = NULL;
	h->shout = NULL;
	h->radio = NULL;

	return 0;
}

#define ADD_STRING(root, key, value) if(value != NULL) \
	     json_object_object_add(root, key, json_object_new_string(value)); \
	else \
	     json_object_object_add(root, key, NULL);

static char *radio_get_json_status(struct radio_handle *h, int add_pic)
{
	struct json_object *root;
	char *artist = NULL;
	char *title = NULL;
	char *str = NULL;

	/* No radio playing */
	if(h->radio == NULL)
	{
		return strdup("{ \"id\": null }");
	}

	/* Create JSON object */
	root = json_object_new_object();
	if(root == NULL)
		return NULL;

	/* Add radio infos */
	ADD_STRING(root, "id", h->radio->id);
	ADD_STRING(root, "name", h->radio->name);

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
		ADD_STRING(root, "title", title);
		ADD_STRING(root, "artist", artist);

		/* Free string */
		if(str != NULL)
			free(str);
	}

	/* Get JSON string */
	str = strdup(json_object_to_json_string(root));

	/* Free JSON object */
	json_object_put(root);

	return str;
}

static char *radio_get_json_category_info(struct radio_handle *h,
					  const char *id)
{
	return radio_list_get_category_json(h->list, id);
}

static char *radio_get_json_radio_info(struct radio_handle *h, const char *id)
{
	return radio_list_get_radio_json(h->list, id);
}

static char *radio_get_json_list(struct radio_handle *h, const char *id)
{
	return radio_list_get_list_json(h->list, id);
}

static int radio_set_config(struct radio_handle *h, const struct config *c)
{
	const char *file;

	if(h == NULL)
		return -1;

	/* Free previous values */
	if(h->list_file != NULL)
		free(h->list_file);
	h->list_file = NULL;

	/* Parse config */
	if(c != NULL)
	{
		/* Get radio list file */
		file = config_get_string(c, "list_file");
		if(file != NULL)
			h->list_file = strdup(file);
	}

	/* Set default values */
	if(h->list_file == NULL)
		h->list_file = strdup("/var/aircat/radio_list.json");

	return 0;
}

static struct config *radio_get_config(struct radio_handle *h)
{
	struct config *c;

	c = config_new_config();
	if(c == NULL)
		return NULL;

	/* Set current radio list file */
	config_set_string(c, "list_file", h->list_file);

	return c;
}

static int radio_close(struct radio_handle *h)
{
	if(h == NULL)
		return 0;

	/* Stop radio */
	radio_stop(h);

	/* Free radio list */
	if(h->list != NULL)
		radio_list_close(h->list);

	/* Stop and close shoutcast player */
	if(h->shout != NULL)
		shoutcast_close(h->shout);

	/* Free list file */
	if(h->list_file != NULL)
		free(h->list_file);

	free(h);

	return 0;
}

#define HTTPD_RESPONSE(s) *buffer = (unsigned char*)s; \
			  *size = strlen(s);

static int radio_httpd_play(struct radio_handle *h, struct httpd_req *req,
			    unsigned char **buffer, size_t *size)
{
	/* Play radio */
	radio_play(h, req->resource);

	return 200;
}

static int radio_httpd_stop(struct radio_handle *h, struct httpd_req *req,
			    unsigned char **buffer, size_t *size)
{
	/* Stop current radio */
	radio_stop(h);

	return 200;
}

static int radio_httpd_status(struct radio_handle *h, struct httpd_req *req,
			      unsigned char **buffer, size_t *size)
{
	char *stat;

	/* Get radio status */
	stat = radio_get_json_status(h, 0);
	if(stat == NULL)
	{
		HTTPD_RESPONSE(strdup("No status"));
		return 500;
	}

	HTTPD_RESPONSE(stat);
	return 200;
}

static int radio_httpd_cat_info(struct radio_handle *h, struct httpd_req *req,
				unsigned char **buffer, size_t *size)
{
	char *info;

	/* Get info about category */
	info = radio_get_json_category_info(h, req->resource);
	if(info == NULL)
	{
		HTTPD_RESPONSE(strdup("Radio not found"));
		return 404;
	}

	HTTPD_RESPONSE(info);
	return 200;
}

static int radio_httpd_info(struct radio_handle *h, struct httpd_req *req,
			    unsigned char **buffer, size_t *size)
{
	char *info;

	/* Get info about radio */
	info = radio_get_json_radio_info(h, req->resource);
	if(info == NULL)
	{
		HTTPD_RESPONSE(strdup("Radio not found"));
		return 404;
	}

	HTTPD_RESPONSE(info);
	return 200;
}

static int radio_httpd_list(struct radio_handle *h, struct httpd_req *req,
			    unsigned char **buffer, size_t *size)
{
	char *list = NULL;

	/* Get Radio list */
	list = radio_get_json_list(h, req->resource);
	if(list == NULL)
	{
		HTTPD_RESPONSE(strdup("No radio list"));
		return 500;
	}

	HTTPD_RESPONSE(list);
	return 200;
}

static struct url_table radio_url[] = {
	{"/category/info/", HTTPD_EXT_URL, HTTPD_GET, 0,
						 (void*) &radio_httpd_cat_info},
	{"/info/",          HTTPD_EXT_URL, HTTPD_GET, 0,
						     (void*) &radio_httpd_info},
	{"/list",           HTTPD_EXT_URL, HTTPD_GET, 0,
						     (void*) &radio_httpd_list},
	{"/play",           HTTPD_EXT_URL, HTTPD_PUT, 0,
						     (void*) &radio_httpd_play},
	{"/stop",           0,             HTTPD_PUT, 0,
						     (void*) &radio_httpd_stop},
	{"/status",         HTTPD_EXT_URL, HTTPD_GET, 0,
						   (void*) &radio_httpd_status},
	{0, 0, 0}
};

struct module module_entry = {
	.name = "radio",
	.open = (void*) &radio_open,
	.close = (void*) &radio_close,
	.set_config = (void*) &radio_set_config,
	.get_config = (void*) &radio_get_config,
	.urls = (void*) &radio_url,
};
