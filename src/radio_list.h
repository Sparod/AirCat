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

struct radio_list_handle;

int radio_list_open(struct radio_list_handle **handle, const char *filename);
int radio_list_close(struct radio_list_handle *h);

/* Loadind functions */
int radio_list_load(struct radio_list_handle *h, const char *filename);
int radio_list_load_mem(struct radio_list_handle *h, const char *str);

/* Getters */
struct radio_item *radio_list_get_radio(struct radio_list_handle *h,
					const char *id);
struct category_item *radio_list_get_category(struct radio_list_handle *h,
					      const char *id);

/* Free functions */
void radio_list_free_radio_item(struct radio_item *radio);
void radio_list_free_category_item(struct category_item *category);

/* JSON list */
char *radio_list_get_category_json(struct radio_list_handle *h, const char *id);
char *radio_list_get_radio_json(struct radio_list_handle *h, const char *id);
char *radio_list_get_list_json(struct radio_list_handle *h, const char *id);

#endif

