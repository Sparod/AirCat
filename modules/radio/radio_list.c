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

/* Get a radio by id */
struct radio_item *radio_get_radio_item(struct db_handle *db, const char *id)
{
	struct radio_item *radio = NULL;
	struct db_query *q = NULL;
	char *sql;
	int len;

	/* Prepare SQL */
	len = asprintf(&sql, "SELECT id,name,url,description "
			     "FROM radio_list "
			     "WHERE id = %ld", atol(id));
	if(len < 0)
		return NULL;

	/* Send SQL query */
	q = db_prepare(db, sql, len);
	free(sql);
	if(q == NULL)
		return NULL;

	/* Get first row */
	if(db_step(q) < 0)
		goto end;

	/* Allocate item */
	radio = malloc(sizeof(struct radio_item));
	if(radio == NULL)
		goto end;

	/* Copy values */
	radio->id = db_column_copy_text(q, 0);
	radio->name = db_column_copy_text(q, 1);
	radio->url = db_column_copy_text(q, 2);
	radio->description = db_column_copy_text(q, 3);

end:
	/* Finalize */
	db_finalize(q);

	return radio;
}

/* Get a category by id */
struct category_item *radio_get_category_item(struct db_handle *db,
					      const char *id)
{
	struct category_item *category = NULL;
	struct db_query *q = NULL;
	char *sql;
	int len;

	/* Prepare SQL */
	len = asprintf(&sql, "SELECT id,name "
			     "FROM category_list "
			     "WHERE id = %ld", atol(id));
	if(len < 0)
		return NULL;

	/* Send SQL query */
	q = db_prepare(db, sql, len);
	free(sql);
	if(q == NULL)
		return NULL;

	/* Get first row */
	if(db_step(q) < 0)
		goto end;

	/* Allocate item */
	category = malloc(sizeof(struct category_item));
	if(category == NULL)
		goto end;

	/* Copy values */
	category->id = db_column_copy_text(q, 0);
	category->name = db_column_copy_text(q, 1);
end:
	/* Finalize */
	db_finalize(q);

	return category;
}

/* Free a radio item */
void radio_free_radio_item(struct radio_item *radio)
{
	if(radio == NULL)
		return;

	if(radio->id != NULL)
		free(radio->id);
	if(radio->name != NULL)
		free(radio->name);
	if(radio->url != NULL)
		free(radio->url);
	if(radio->description != NULL)
		free(radio->description);

	free(radio);
}

/* Free a category item */
void radio_free_category_item(struct category_item *category)
{
	if(category == NULL)
		return;

	if(category->id != NULL)
		free(category->id);
	if(category->name != NULL)
		free(category->name);

	free(category);
}

/* Get category info */
char *radio_get_json_category_info(struct db_handle *db, const char *id)
{
	struct db_query *q = NULL;
	struct json *info;
	char *str = NULL;
	char *sql;
	int len;

	/* Prepare SQL */
	len = asprintf(&sql, "SELECT id,name "
			     "FROM category_list "
			     "WHERE id = %ld", atol(id));
	if(len < 0)
		return NULL;

	/* Send SQL query */
	q = db_prepare(db, sql, len);
	free(sql);
	if(q == NULL)
		return NULL;

	/* Get first row */
	if(db_step(q) < 0)
		goto end;

	/* Create JSON object */
	info = json_new();
	if(info == NULL)
		goto end;

	/* Add values to it */
	json_set_string(info, "id", db_column_text(q, 0));
	json_set_string(info, "name", db_column_text(q, 1));

	/* Get string from JSON object */
	str = strdup(json_export(info));

	/* Free JSON object */
	json_free(info);

end:
	/* Finalize */
	db_finalize(q);

	return str;
}

/* Get radio info */
char *radio_get_json_radio_info(struct db_handle *db, const char *id)
{
	struct db_query *q = NULL;
	struct json *info;
	char *str = NULL;
	char *sql;
	int len;

	/* Prepare SQL */
	len = asprintf(&sql, "SELECT id,name,url,description "
			     "FROM radio_list "
			     "WHERE id = %ld", atol(id));
	if(len < 0)
		return NULL;

	/* Send SQL query */
	q = db_prepare(db, sql, len);
	free(sql);
	if(q == NULL)
		return NULL;

	/* Get first row */
	if(db_step(q) < 0)
		goto end;

	/* Create JSON object */
	info = json_new();
	if(info == NULL)
		goto end;

	/* Add values to it */
	json_set_string(info, "id", db_column_text(q, 0));
	json_set_string(info, "name", db_column_text(q, 1));
	json_set_string(info, "url", db_column_text(q, 2));
	json_set_string(info, "description", db_column_text(q, 3));

	/* Get string from JSON object */
	str = strdup(json_export(info));

	/* Free JSON object */
	json_free(info);

end:
	/* Finalize */
	db_finalize(q);

	return str;
}

static int radio_to_json(void *user_data, int col_count, char **values,
			 char **names)
{
	struct json *list = user_data;
	struct json *tmp;

	/* Create JSON object */
	tmp = json_new();
	if(tmp == NULL)
		return -1;

	/* Add values to it */
	json_set_string(tmp, "id", values[0]);
	json_set_string(tmp, "name", values[1]);
	json_set_string(tmp, "url", values[2]);
	json_set_string(tmp, "description", values[3]);

	/* Add object to array */
	if(json_array_add(list, tmp) != 0)
		json_free(tmp);

	return 0;
}

static int category_to_json(void *user_data, int col_count, char **values,
			    char **names)
{
	struct json *list = user_data;
	struct json *tmp;

	/* Create JSON object */
	tmp = json_new();
	if(tmp == NULL)
		return -1;

	/* Add values to it */
	json_set_string(tmp, "id", values[0]);
	json_set_string(tmp, "name", values[1]);

	/* Add object to array */
	if(json_array_add(list, tmp) != 0)
		json_free(tmp);

	return 0;
}

char *radio_get_json_list(struct db_handle *db, const char *id)
{
	struct json *root, *list;
	char *str = NULL;
	long p_id;
	char *sql;
	int len;

	if(id != NULL && strcmp(id, "all") == 0)
	{
		/* Create radio array */
		list = json_new_array();
		if(list == NULL)
			return NULL;

		/* List all radios */
		db_exec(db, "SELECT id,name,url,description FROM radio_list",
			&radio_to_json, list);

		/* Get string from JSON object */
		str = strdup(json_export(list));

		/* Free JSON object */
		json_free(list);
	}
	else
	{
		/* Create a new JSON object */
		root = json_new();
		if(root == NULL)
			return NULL;

		/* Get id */
		p_id = id == NULL || *id == '\0' ? 0 : atol(id);

		/* Create category array */
		list = json_new_array();
		if(list == NULL)
			goto end;

		/* Prepare SQL */
		len = asprintf(&sql, "SELECT id,name FROM category_list "
				     "WHERE p_id = '%ld'", p_id);
		if(len < 0)
		{
			json_free(list);
			goto end;
		}

		/* List all categories with p_id as parent */
		db_exec(db, sql, &category_to_json, list);
		free(sql);

		/* Add array to JSON object */
		json_add(root, "category", list);

		/* Create radio array */
		list = json_new_array();
		if(list == NULL)
			goto end;

		/* Prepare SQL */
		len = asprintf(&sql, "SELECT id,name,url,description "
				     "FROM radio_list AS r "
				     "INNER JOIN radio_category AS rc "
				     "ON r.id = rc.rad_id "
				     "WHERE rc.cat_id = '%ld'", p_id);

		if(len < 0)
		{
			json_free(list);
			goto end;
		}

		/* List all categories with p_id as parent */
		db_exec(db, sql, &radio_to_json, list);
		free(sql);

		/* Add array to JSON object */
		json_add(root, "radio", list);

end:
		/* Get string from JSON object */
		str = strdup(json_export(root));

		/* Free JSON object */
		json_free(root);
	}

	return str;
}

