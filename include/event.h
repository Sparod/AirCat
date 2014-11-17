/*
 * event.h - Event system for notification
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

#ifndef _EVENT_H
#define _EVENT_H

#include "json.h"

enum event_type {
	EVENT_UPDATE
};

struct event_handle;

int event_add(struct event_handle *h, const char *name, enum event_type type,
	      struct json *data);
int event_remove(struct event_handle *h, const char *name);
void event_flush(struct event_handle *h);

#endif

