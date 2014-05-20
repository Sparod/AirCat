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

#include <json.h>

struct config;
struct config_handle;

#define config_json_to_config(j) (struct config *) json_object_get(j)
#define config_to_json(c) json_object_get((json_object *)c)

int config_open(struct config_handle **h, const char *file);
int config_load(struct config_handle *h);
int config_save(struct config_handle *h);
void config_close(struct config_handle *h);

struct config *config_get_config(struct config_handle *h, const char *name);
int config_set_config(struct config_handle *h, const char *name,
		      struct config *c);

struct config *config_new_config();
void config_free_config(struct config *c);

const char *config_get_string(const struct config *c, const char *name);
int config_set_string(const struct config *c, const char *name,
		      const char *value);

int config_get_bool(const struct config *c, const char *name);
int config_set_bool(const struct config *c, const char *name, int value);

long config_get_int(const struct config *c, const char *name);
int config_set_int(const struct config *c, const char *name, long value);

#endif
