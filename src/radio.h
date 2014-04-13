/*
 * radio.h - A Radio module
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

#ifndef _RADIO_H
#define _RADIO_H

#include "output.h"

struct radio_handle;

int radio_open(struct radio_handle **handle, struct output_handle *o);
char *radio_get_json_category_info(struct radio_handle *h, const char *id);
char *radio_get_json_radio_info(struct radio_handle *h, const char *id);
char *radio_get_json_list(struct radio_handle *h, const char *id);
int radio_close(struct radio_handle *h);

#endif

