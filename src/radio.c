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
#include <pthread.h>

#include <json.h>
#include <json_tokener.h>

#include "config_file.h"
#include "shoutcast.h"
#include "radio.h"

struct radio_list {
	char *name;
	char *url;
	char *description;
};

struct radio_category {
	char *name;
	enum {CAT_NODE, CAT_ENTRY} type;
	struct radio_category *sub_cat;
	struct radio_list **radio_list;
};

struct radio_handle {
	/* Radio player */
	struct shout_handle *shout;
	int listening;
	/* Radio list */
	struct radio_list *radio_list;
	unsigned int radio_list_count;
	pthread_mutex_t radio_list_mutex;
};

int radio_open(struct radio_handle **handle)
{
	struct radio_handle *h;

	/* Allocate structure */
	*handle = malloc(sizeof(struct radio_handle));
	if(*handle == NULL)
		return -1;
	h = *handle;

	/* Init structure */
	h->shout = NULL;
	h->listening = -1;
	h->radio_list = NULL;
	h->radio_list_count = 0;

	/* Init radio list mutex */
	pthread_mutex_init(&h->radio_list_mutex, NULL);

	/* Load radio list */
	radio_load_list_from_file(h, config.radio_list_file);

	return 0;
}

static void radio_free_list(struct radio_list *list, unsigned int count)
{
	int i;

	/* Free each string of list */
	for(i = 0; i < count; i++)
	{
		if(list[i].name != NULL)
			free(list[i].name);
		if(list[i].url != NULL)
			free(list[i].url);
		if(list[i].description != NULL)
			free(list[i].description);
	}

	/* Free list */
	free(list);
}

int radio_load_list(struct radio_handle *h, const char *json_str)
{
	struct radio_list *tmp = NULL;
	struct json_object *root, *json_list, *cur;
	struct list_array;
	int count = 0;
	const char *name, *url;
	int i, j;

	/* Parse JSON string */
	root = json_tokener_parse(json_str);
	if(root == NULL)
		return -1;

	/* Get radio array from JSON */
	json_list = json_object_object_get(root, "list");
	if(json_list == NULL)
		goto error;

	/* Create a temporary list */
	count = json_object_array_length(json_list);
	if(count > 0)
	{
		/* Allocate new list */
		tmp = calloc(count, sizeof(struct radio_list));
		if(tmp == NULL)
			return -1;

		/* Fill the list from JSON */
		for(i = 0, j = 0; i < count; i++)
		{
			/* Get JSON object */
			cur = json_object_array_get_idx(json_list, i);
			if(cur == NULL)
				continue;

			/* Get name and url */
			name = json_object_get_string(
					   json_object_object_get(cur, "name"));
			url = json_object_get_string(
					    json_object_object_get(cur, "url"));

			/* Name and url cannot be empty */
			if(name == NULL || *name == 0 ||
			   url == NULL || *url == 0)
				continue;

			/* Fill the radio entry */
			tmp[j].name = strdup(name);
			tmp[j].url = strdup(url);

			/* Update radio list counter */
			j++;
		}
		if(j != count)
			count = j;
	}

	/* Update global radio list */
	if(tmp != NULL)
	{
		/* Lock radio list access */
		pthread_mutex_lock(&h->radio_list_mutex);

		/* Free global list */
		if(h->radio_list != NULL)
			radio_free_list(h->radio_list, h->radio_list_count);

		/* Update list */
		h->radio_list = tmp;
		h->radio_list_count = count;

		/* Unlock radio list */
		pthread_mutex_unlock(&h->radio_list_mutex);

		/* Free JSON object */
		json_object_put(root);

		return 0;
	}

error:
	json_object_put(root);
	return -1;
}

int radio_load_list_from_file(struct radio_handle *h, const char *filename)
{
	FILE *fp;
	char *str;
	int len, ret;

	if(filename == NULL)
		return -1;

	/* Open file */
	fp = fopen(filename, "rb");
	if(fp == NULL)
		return -1;

	/* Get file len */
	fseek(fp, 0, SEEK_END);
	len = ftell(fp);
	fseek(fp, 0, SEEK_SET);

	/* Alloc string to contain JSON data */
	str = malloc(len + 1);
	if(str == NULL)
	{
		fclose(fp);
		return -1;
	}

	/* Read file */
	fread(str, 1, len, fp);
	str[len] = 0;

	/* Load the list */
	ret = radio_load_list(h, str);

	/* Free string */
	free(str);

	/* Close file */
	fclose(fp);

	return ret;
}

int radio_close(struct radio_handle *h)
{
	if(h == NULL)
		return 0;

	/* Free radio list */
	pthread_mutex_lock(&h->radio_list_mutex);
	if(h->radio_list != NULL)
		radio_free_list(h->radio_list, h->radio_list_count);
	pthread_mutex_unlock(&h->radio_list_mutex);

	/* Stop and close shoutcast player */
	if(h->shout != NULL)
		shoutcast_close(h->shout);

	free(h);

	return 0;
}
