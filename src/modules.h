/*
 * modules.h - Plugin handler
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

#ifndef _MODULES_H
#define _MODULES_H

#include "module.h"

struct modules_handle;

/* Basic functions */
int modules_open(struct modules_handle **handle, struct json *config,
		 const char *path);
void modules_close(struct modules_handle *h);

/* Modules config */
int modules_set_config(struct modules_handle *h, struct json *cfg,
		       const char *name);
struct json *modules_get_config(struct modules_handle *h, const char *name);

/* List modules */
char **modules_list_modules(struct modules_handle *h, int *count);
void modules_free_list(char **list, int count);

/* Open or close modules with enabled flag */
void modules_refresh(struct modules_handle *h, struct httpd_handle *httpd, 
		     struct avahi_handle *avahi, struct output_handle *output);

#endif

