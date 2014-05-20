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
	json_object *json;
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
	json_object *json;

	/* Load config file */
	json = json_object_from_file(h->file);
	if(json == NULL)
	{
		json = (json_object *) config_new_config();
		if(json == NULL)
			return -1;
	}

	/* Copy new JSON */
	if(h->json != NULL)
		json_object_put(h->json);
	h->json = json;

	return 0;
}

int config_save(struct config_handle *h)
{
	/* Save file */
	return json_object_to_file_ext(h->file, h->json,
				       JSON_C_TO_STRING_PRETTY);
}

struct config *config_get_config(struct config_handle *h, const char *name)
{
	json_object *tmp;

	if(h == NULL || h->json == NULL)
		return NULL;

	/* Get global object */
	if(name == NULL || *name == '\0')
		return (struct config *) json_object_get(h->json);

	/* Get JSON object */
	if(json_object_object_get_ex(h->json, name, &tmp) == 0)
		return NULL;

	return (struct config *) json_object_get(tmp);
}

int config_set_config(struct config_handle *h, const char *name,
		      struct config *c)
{
	if(h->json == NULL)
		return -1;

	/* Change jSON object with new one */
	json_object_object_add(h->json, name, json_object_get((json_object*)c));

	return 0;
}

struct config *config_new_config()
{
	return (struct config *) json_object_new_object();
}

const char *config_get_string(const struct config *c, const char *name)
{
	json_object *tmp;

	if(c == NULL)
		return NULL;

	/* Get JSON object */
	if(json_object_object_get_ex((json_object *)c, name, &tmp) == 0)
		return NULL;

	return json_object_get_string(tmp);
}

int config_set_string(const struct config *c, const char *name,
		      const char *value)
{
	json_object *tmp = NULL;

	/* Create JSON string object */
	if(value != NULL)
		tmp = json_object_new_string(value);

	/* Add object */
	json_object_object_add((json_object*)c, name, tmp);

	return 0;
}

int config_get_bool(const struct config *c, const char *name)
{
	json_object *tmp;

	if(c == NULL)
		return -1;

	/* Get JSON object */
	if(json_object_object_get_ex((json_object *)c, name, &tmp) == 0)
		return -1;

	return json_object_get_boolean(tmp);
}

int config_set_bool(const struct config *c, const char *name, int value)
{
	json_object *tmp;

	/* Create JSON string object */
	tmp = json_object_new_boolean(value);
	if(tmp == NULL)
		return -1;

	/* Add object */
	json_object_object_add((json_object*)c, name, tmp);

	return 0;
}

long config_get_int(const struct config *c, const char *name)
{
	json_object *tmp;

	if(c == NULL)
		return -1;

	/* Get JSON object */
	if(json_object_object_get_ex((json_object *)c, name, &tmp) == 0)
		return -1;

	return json_object_get_int(tmp);
}

int config_set_int(const struct config *c, const char *name, long value)
{
	json_object *tmp;

	/* Create JSON string object */
	tmp = json_object_new_int(value);
	if(tmp == NULL)
		return -1;

	/* Add object */
	json_object_object_add((json_object*)c, name, tmp);

	return 0;
}

void config_free_config(struct config *c)
{
	if(c != NULL)
		json_object_put((json_object*)c);
}

void config_close(struct config_handle *h)
{
	if(h == NULL)
		return;

	/* Free JSON object */
	if(h->json != NULL)
		json_object_put(h->json);

	/* Free filename */
	if(h->file != NULL)
		free(h->file);

	free(h);
}
