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

#include "config_file.h"
#include "radio_list.h"
#include "shoutcast.h"
#include "radio.h"

struct radio_handle {
	/* Radio player */
	struct shout_handle *shout;
	/* Radio list */
	struct radio_list_handle *list;
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
	h->list = NULL;

	/* Load radio list */
	if(radio_list_open(&h->list, config.radio_list_file) != 0)
		return -1;

	return 0;
}

int radio_play(struct radio_handle *h, const char *id)
{
	if(id == NULL)
		return -1;

	/* Radio module must be enabled */
	if(config.radio_enabled != 1)
		return 0;

	return 0;
}

int radio_stop(struct radio_handle *h)
{
	if(h == NULL || h->shout == NULL)
		return 0;

	return 0;
}

char *radio_get_json_category_info(struct radio_handle *h, const char *id)
{
	return radio_list_get_category_json(h->list, id);
}

char *radio_get_json_radio_info(struct radio_handle *h, const char *id)
{
	return radio_list_get_radio_json(h->list, id);
}

char *radio_get_json_list(struct radio_handle *h, const char *id)
{
	return radio_list_get_list_json(h->list, id);
}

int radio_close(struct radio_handle *h)
{
	if(h == NULL)
		return 0;

	/* Free radio list */
	if(h->list != NULL)
		radio_list_close(h->list);

	/* Stop and close shoutcast player */
	if(h->shout != NULL)
		shoutcast_close(h->shout);

	free(h);

	return 0;
}

