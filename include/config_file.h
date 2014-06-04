/*
 * config_file.h - Configuration file reader/writer
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

#ifndef _CONFIG_FILE_H
#define _CONFIG_FILE_H

#include "json.h"

struct config_handle;

int config_open(struct config_handle **h, const char *file);
int config_load(struct config_handle *h);
int config_save(struct config_handle *h);
void config_close(struct config_handle *h);

struct json *config_get_json(struct config_handle *h, const char *name);
int config_set_json(struct config_handle *h, const char *name, struct json *j);


#endif
