/*
 * config_file.c - Configuration file reader/writer
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

#include "config_file.h"

struct config_handle {
	char *file;
	struct json *json;
};

int config_open(struct config_handle **handle, const char *file)
{
	struct config_handle *h;

	/* Check file */
	if(file == NULL)
		return -1;

	/* Allocate structure */
	*handle = malloc(sizeof(struct config_handle));
	if(*handle == NULL)
		return -1;
	h = *handle;

	/* Init structure */
	h->json = NULL;

	/* Copy file name */
	h->file = strdup(file);

	/* Load config */
	return config_load(h);
}

int config_load(struct config_handle *h)
{
	struct json *json;

	/* Load config file */
	json = json_from_file(h->file);
	if(json == NULL)
	{
		json = json_new();
		if(json == NULL)
			return -1;
	}

	/* Copy new JSON */
	json_free(h->json);
	h->json = json;

	return 0;
}

int config_save(struct config_handle *h)
{
	/* Save file */
	return json_to_file_ex(h->file, h->json, JSON_C_TO_STRING_PRETTY);
}

void config_close(struct config_handle *h)
{
	if(h == NULL)
		return;

	/* Free JSON object */
	json_free(h->json);

	/* Free filename */
	if(h->file != NULL)
		free(h->file);

	free(h);
}

struct json *config_get_json(struct config_handle *h, const char *name)
{
	struct json *tmp;

	if(h == NULL || h->json == NULL)
		return NULL;

	/* Get global object */
	if(name == NULL || *name == '\0')
		return json_copy(h->json);

	/* Get JSON object */
	if(json_get_ex(h->json, name, &tmp) == 0)
		return NULL;

	return json_copy(tmp);
}

int config_set_json(struct config_handle *h, const char *name, struct json *j)
{
	if(h->json == NULL)
		return -1;

	/* Change jSON object with new one */
	json_add(h->json, name, json_copy(j));

	return 0;
}

