/*
 * module.h - Module def
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

#ifndef _MODULE_H
#define _MODULE_H

#include "output.h"
#include "avahi.h"
#include "httpd.h"
#include "event.h"
#include "timer.h"
#include "json.h"
#include "db.h"

struct module_attr {
	struct output_handle *output;
	struct avahi_handle *avahi;
	struct event_handle *event;
	struct timer_handle *timer;
	struct db_handle *db;
	const char *path;
	const struct json *config;
};

struct module {
	/* Module name */
	const char *id;
	const char *name;
	const char *description;
	/* Module functions */
	int (*open)(void **, struct module_attr *);
	int (*close)(void *);
	/* Configuration functions */
	int (*set_config)(void *, const struct json *);
	struct json *(*get_config)(void *);
	/* URL table */
	struct url_table *urls;
};

#endif
