/*
 * radio_list.c - A Radio List submodule
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

#include "radio_list.h"
#include "json.h"

struct radio_chain_list {
	struct radio_item *radio;
	struct radio_chain_list *next;
};

struct category_item_priv {
	char *id;
	char *name;
	/* Sub category */
	struct category_item_priv *sub_category;
	unsigned int sub_category_count;
	/* Radio list in this category */
	struct radio_chain_list *radio_list;
};

struct radio_list_handle {
	/* Radio list */
	struct radio_item *radio_list;
	unsigned int radio_list_count;
	/* Category list */
	struct category_item_priv *category_list;
	unsigned int category_list_count;
	/* Mutex for thread safe */
	pthread_mutex_t mutex;
};

static void radio_list_free_radio_list(struct radio_item *list,
				  unsigned int count);
static void radio_list_free_category_list(struct category_item_priv *list,
				     unsigned int count);

int radio_list_open(struct radio_list_handle **handle, const char *filename)
{
	struct radio_list_handle *h;

	/* Allocate structure */
	*handle = malloc(sizeof(struct radio_list_handle));
	if(*handle == NULL)
		return -1;
	h = *handle;

	/* Init structure */
	h->radio_list = NULL;
	h->radio_list_count = 0;
	h->category_list = NULL;
	h->category_list_count = 0;

	/* Init mutex */
	pthread_mutex_init(&h->mutex, NULL);

	/* Load radio list */
	if(filename != NULL)
		radio_list_load(h, filename);

	return 0;
}

int radio_list_close(struct radio_list_handle *h)
{
	if(h == NULL)
		return 0;

	/* Lock list access */
	pthread_mutex_lock(&h->mutex);

	/* Free radio list */
	if(h->radio_list != NULL)
		radio_list_free_radio_list(h->radio_list, h->radio_list_count);

	/* Free category list */
	if(h->category_list != NULL)
		radio_list_free_category_list(h->category_list,
					 h->category_list_count);

	/* Unlock list access */
	pthread_mutex_unlock(&h->mutex);

	free(h);

	return 0;
}

/******************************************************************************
 *                Basic functions for category and radio list                 *
 ******************************************************************************/

/* Copy a radio item */
static struct radio_item *radio_list_copy_radio(struct radio_item *in)
{
	struct radio_item *radio;

	radio = calloc(1, sizeof(struct radio_item));
	if(radio == NULL)
		return NULL;

	if(in->id != NULL)
		radio->id = strdup(in->id);
	if(in->name != NULL)
		radio->name = strdup(in->name);
	if(in->url != NULL)
		radio->url = strdup(in->url);
	if(in->description != NULL)
		radio->description = strdup(in->description);

	return radio;
}

/* Free content of a radio item */
static inline void radio_list_free_radio(struct radio_item *radio)
{
	if(radio->id != NULL)
		free(radio->id);
	if(radio->name != NULL)
		free(radio->name);
	if(radio->url != NULL)
		free(radio->url);
	if(radio->description != NULL)
		free(radio->description);
}

/* Free a radio item */
void radio_list_free_radio_item(struct radio_item *radio)
{
	if(radio == NULL)
		return;

	radio_list_free_radio(radio);
	free(radio);
}

/* Copy a category item */
static struct category_item *radio_list_copy_category(
						  struct category_item_priv *in)
{
	struct category_item *category;

	category = calloc(1, sizeof(struct category_item));
	if(category == NULL)
		return NULL;

	if(in->id != NULL)
		category->id = strdup(in->id);
	if(in->name != NULL)
		category->name = strdup(in->name);

	return category;
}

/* Free a category item */
void radio_list_free_category_item(struct category_item *category)
{
	if(category == NULL)
		return;

	if(category->id != NULL)
		free(category->id);
	if(category->name != NULL)
		free(category->name);

	free(category);
}

/* Free a radio list */
static void radio_list_free_radio_list(struct radio_item *list,
				  unsigned int count)
{
	int i;

	/* Free each string of list */
	for(i = 0; i < count; i++)
		radio_list_free_radio(&list[i]);

	/* Free list */
	free(list);
}

/* Free a private category list */
static void radio_list_free_category_list(struct category_item_priv *list,
				     unsigned int count)
{
	struct radio_chain_list *tmp;
	int i;

	/* Free each string of list */
	for(i = 0; i < count; i++)
	{
		if(list[i].id != NULL)
			free(list[i].id);
		if(list[i].name != NULL)
			free(list[i].name);
		if(list[i].sub_category != NULL)
			radio_list_free_category_list(list[i].sub_category,
						    list[i].sub_category_count);
		while(list[i].radio_list != NULL)
		{
			tmp = list[i].radio_list;
			list[i].radio_list = tmp->next;
			free(tmp);
		}
	}

	/* Free list */
	free(list);
}

/* Find a radio by id in a list */
static int radio_list_find_radio(struct radio_item *list, unsigned int count,
				 const char *id)
{
	int i;

	for(i = 0; i < count; i++)
	{
		if(strcmp(id, list[i].id) == 0)
			return i;
	}

	return -1;
}

/* Find a category by id in the tree */
static struct category_item_priv *radio_list_find_category(
						struct category_item_priv *list,
						unsigned int count,
						const char *id)
{
	char *next;
	int len;
	int i;

	if(id == NULL)
		return NULL;

	next = strchr(id, '/');
	if(next != NULL)
	{
		len = next-id;
		next++;
	}
	else
		len = strlen(id);

	for(i = 0; i < count; i++)
	{
		if(strncmp(id, list[i].id, len) == 0 && list[i].id[len] == 0)
		{
			if(next != NULL)
				return radio_list_find_category(
						     list[i].sub_category,
						     list[i].sub_category_count,
						     next);
			else
				return &list[i];
		}
	}

	return NULL;
}

/* Get a radio by id */
struct radio_item *radio_list_get_radio(struct radio_list_handle *h,
					const char *id)
{
	int idx;

	/* Search radio */
	idx = radio_list_find_radio(h->radio_list, h->radio_list_count, id);

	/* Copy item */
	if(idx >= 0)
		return radio_list_copy_radio(&h->radio_list[idx]);

	return NULL;
}

/* Get a category by id */
struct category_item *radio_list_get_category(struct radio_list_handle *h,
					      const char *id)
{
	struct category_item_priv *category;

	/* Search category */
	category = radio_list_find_category(h->category_list,
					    h->category_list_count, id);

	/* Copy item */
	if(category >= 0)
		return radio_list_copy_category(category);

	return NULL;
}

/******************************************************************************
 *             Imported JSON parsing (load from file and memory)              *
 ******************************************************************************/

/* Category list parsing */
static struct category_item_priv *radio_list_parse_category(struct json *root,
							    int *count)
{
	struct category_item_priv *tmp = NULL;
	struct json *json_list, *cur;
	const char *name, *id;
	int i, j = 0;

	/* Get category array from JSON */
	json_list = json_get(root, "category");
	if(json_list == NULL)
	{
		*count = 0;
		return NULL;
	}

	/* Get number of categories */
	*count = json_array_length(json_list);

	/* Parse array */
	if(*count > 0)
	{
		/* Allocate new category list */
		tmp = calloc(*count+1, sizeof(struct category_item_priv));
		if(tmp == NULL)
			return NULL;

		/* Fill the list from JSON */
		for(i = 0, j = 0; i < *count; i++)
		{
			/* Get JSON object */
			cur = json_array_get(json_list, i);
			if(cur == NULL)
				continue;

			/* Get name and url */
			id = json_get_string(cur, "id");
			name = json_get_string(cur, "name");

			/* Name and url cannot be empty */
			if(name == NULL || *name == 0 ||
			   id == NULL || *id == 0)
				continue;

			/* Fill the category entry */
			tmp[j].id = strdup(id);
			tmp[j].name = strdup(name);
			tmp[j].sub_category = radio_list_parse_category(cur,
					     (int*) &tmp[j].sub_category_count);

			/* Update category list counter */
			j++;
		}
	}

	/* Update counter */
	if(j != *count)
		*count = j;

	return tmp;
}

/* Global file parsing and radio list parsing */
int radio_list_load_mem(struct radio_list_handle *h, const char *json_str)
{
	struct category_item_priv *cat_tmp = NULL, *cat_cur = NULL;
	struct json *root, *json_list, *cur, *cat_list;
	struct radio_chain_list *cat_radio_list;
	struct radio_item *tmp = NULL;
	int count = 0, cat_count = 0, cat_list_count = 0;
	const char *id, *name, *url, *cat_str;
	int i, j, k;

	/* Parse JSON string */
	root = (struct json *) json_tokener_parse(json_str);
	if(root == NULL)
		return -1;

	/* Get category list */
	cat_tmp = radio_list_parse_category(root, &cat_count);

	/* Get radio array from JSON */
	json_list = json_get(root, "list");
	if(json_list == NULL)
		goto error;

	/* Create a temporary list */
	count = json_array_length(json_list);
	if(count > 0)
	{
		/* Allocate new list */
		tmp = calloc(count+1, sizeof(struct radio_item));
		if(tmp == NULL)
			return -1;

		/* Fill the list from JSON */
		for(i = 0, j = 0; i < count; i++)
		{
			/* Get JSON object */
			cur = json_array_get(json_list, i);
			if(cur == NULL)
				continue;

			/* Get name and url */
			id = json_get_string(cur, "id");
			name = json_get_string(cur, "name");
			url = json_get_string(cur, "url");

			/* Name and url cannot be empty */
			if(name == NULL || *name == 0 ||
			   id == NULL || *id == 0 ||
			   url == NULL || *url == 0)
				continue;

			/* Fill the radio entry */
			tmp[j].id = strdup(id);
			tmp[j].name = strdup(name);
			tmp[j].url = strdup(url);

			/* Get category array */
			cat_list = json_get(cur, "category");
			if(cat_list != NULL)
				cat_list_count = json_array_length(cat_list);
			else
				cat_list_count = 0;
			for(k = 0; k < cat_list_count; k++)
			{
				cat_str = json_to_string(
						   json_array_get(cat_list, k));
				if(cat_str == NULL)
					continue;

				/* Find category */
				cat_cur = radio_list_find_category(cat_tmp,
								   cat_count,
								   cat_str);
				if(cat_cur == NULL)
					continue;

				/* Add radio to category */
				cat_radio_list = malloc(
					    sizeof(struct radio_chain_list));
				if(cat_radio_list == NULL)
					continue;

				cat_radio_list->radio = &tmp[j];
				cat_radio_list->next = cat_cur->radio_list;
				cat_cur->radio_list = cat_radio_list;
			}

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
		pthread_mutex_lock(&h->mutex);

		/* Free radio and categiry list */
		if(h->category_list != NULL)
			radio_list_free_category_list(h->category_list,
						      h->category_list_count);

		if(h->radio_list != NULL)
			radio_list_free_radio_list(h->radio_list,
						   h->radio_list_count);

		/* Update list */
		h->radio_list = tmp;
		h->radio_list_count = count;
		h->category_list = cat_tmp;
		h->category_list_count = cat_count;

		/* Unlock radio list */
		pthread_mutex_unlock(&h->mutex);

		/* Free JSON object */
		json_free(root);

		return 0;
	}

error:
	json_free(root);
	return -1;
}

/* Load from file */
int radio_list_load(struct radio_list_handle *h, const char *filename)
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
	ret = radio_list_load_mem(h, str);

	/* Free string */
	free(str);

	/* Close file */
	fclose(fp);

	return ret;
}

/******************************************************************************
 *                   JSON export of radio and category list                   *
 ******************************************************************************/

static struct json *radio_list_make_json_category(
					    struct category_item_priv *category)
{
	struct json *info;

	/* Create JSON object */
	info = json_new();
	if(info == NULL)
		return NULL;

	/* Add values to it */
	json_set_string(info, "id", category->id);
	json_set_string(info, "name", category->name);

	return info;
}

/* Get category info */
char *radio_list_get_category_json(struct radio_list_handle *h, const char *id)
{
	struct category_item_priv *category;
	struct json *info;
	char *str = NULL;

	/* Lock radio list access */
	pthread_mutex_lock(&h->mutex);

	/* Search for radio */
	category = radio_list_find_category(h->category_list,
					    h->category_list_count, id);
	if(category == NULL)
		goto end;

	/* Create JSON object */
	info = radio_list_make_json_category(category);
	if(info == NULL)
		goto end;

	/* Get string from JSON object */
	str = strdup(json_export(info));

	/* Free JSON object */
	json_free(info);

end:
	/* Unlock radio list access */
	pthread_mutex_unlock(&h->mutex);

	return str;
}

static struct json *radio_list_make_json_radio(struct radio_item *radio)
{
	struct json *info;

	/* Create JSON object */
	info = json_new();
	if(info == NULL)
		return NULL;

	/* Add values to it */
	json_set_string(info, "id", radio->id);
	json_set_string(info, "name", radio->name);
	json_set_string(info, "url", radio->url);

	return info;
}

/* Get radio info */
char *radio_list_get_radio_json(struct radio_list_handle *h, const char *id)
{
	struct json *info;
	char *str = NULL;
	int idx;

	/* Lock radio list access */
	pthread_mutex_lock(&h->mutex);

	/* Search for radio */
	idx = radio_list_find_radio(h->radio_list, h->radio_list_count, id);
	if(idx < 0)
		goto end;

	/* Create JSON object */
	info = radio_list_make_json_radio(&h->radio_list[idx]);
	if(info == NULL)
		goto end;

	/* Get string from JSON object */
	str = strdup(json_export(info));

	/* Free JSON object */
	json_free(info);

end:
	/* Unlock radio list access */
	pthread_mutex_unlock(&h->mutex);

	return str;
}

static char *radio_list_list_by_category(
				       struct category_item_priv *category_list,
				       unsigned int category_list_count,
				       struct radio_chain_list *radio_list)
{
	struct json *root, *list, *tmp;
	char *str = NULL;
	int i;

	if(category_list == NULL)
		category_list_count = 0;

	/* Create a new JSON object */
	root = json_new();
	if(root == NULL)
		return NULL;

	/* Create category array */
	list = json_new_array();
	if(list != NULL)
	{
		for(i = 0; i < category_list_count; i++)
		{
			/* Create JSON object */
			tmp = radio_list_make_json_category(&category_list[i]);
			if(tmp == NULL)
				continue;

			/* Add object to array */
			if(json_array_add(list, tmp) != 0)
				json_free(tmp);
		}

		/* Add array to JSON object */
		json_add(root, "category", list);
	}

	/* Create radio array */
	list = json_new_array();
	if(list != NULL)
	{
		for( ; radio_list != NULL; radio_list = radio_list->next)
		{
			/* Create JSON object */
			tmp = radio_list_make_json_radio(radio_list->radio);
			if(tmp == NULL)
				continue;

			/* Add object to array */
			if(json_array_add(list, tmp) != 0)
				json_free(tmp);
		}

		/* Add array to JSON object */
		json_add(root, "radio", list);
	}

	/* Get string from JSON object */
	str = strdup(json_export(root));

	/* Free JSON object */
	json_free(root);

	return str;
}

static char *radio_list_list_all(struct radio_list_handle *h)
{
	struct json *list, *tmp;
	char *str = NULL;
	int i;

	/* Create radio array */
	list = json_new_array();
	if(list == NULL)
		return NULL;

	/* Fill the radio list */
	for(i = 0; i < h->radio_list_count; i++)
	{
		/* Create JSON object */
		tmp = radio_list_make_json_radio(&h->radio_list[i]);

		/* Add object to array */
		if(json_array_add(list, tmp) != 0)
			json_free(tmp);
	}

	/* Get string from JSON object */
	str = strdup(json_export(list));

	/* Free JSON object */
	json_free(list);

	return str;
}

char *radio_list_get_list_json(struct radio_list_handle *h, const char *id)
{
	struct category_item_priv *category;
	char *str = NULL;

	/* Lock radio list access */
	pthread_mutex_lock(&h->mutex);

	if(id == NULL || *id == 0)
	{
		str = radio_list_list_by_category(h->category_list,
						  h->category_list_count,
						  NULL);
	}
	else if(strcmp(id, "all") == 0)
	{
		str = radio_list_list_all(h);
	}
	else
	{
		category = radio_list_find_category(h->category_list,
						    h->category_list_count, id);
		if(category != NULL)
			str = radio_list_list_by_category(
						   category->sub_category,
						   category->sub_category_count,
						   category->radio_list);
	}

	/* Unlock radio list access */
	pthread_mutex_unlock(&h->mutex);

	return str;
}

