/*
 * radio_list.h - A Radio List submodule
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

#ifndef _RADIO_LIST_H
#define _RADIO_LIST_H

#include "db.h"

struct radio_item {
	char *id;
	char *name;
	char *url;
	char *description;
};

struct category_item {
	char *id;
	char *name;
};

/* Getters */
struct radio_item *radio_get_radio_item(struct db_handle *db, const char *id);
struct category_item *radio_get_category_item(struct db_handle *db,
					      const char *id);

/* Free functions */
void radio_free_radio_item(struct radio_item *radio);
void radio_free_category_item(struct category_item *category);

/* JSON list */
char *radio_get_json_category_info(struct db_handle *db, const char *id);
char *radio_get_json_radio_info(struct db_handle *db, const char *id);
char *radio_get_json_list(struct db_handle *db, const char *id);

#endif

