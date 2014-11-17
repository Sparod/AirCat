/*
 * events.h - Event system for multiplexed notification
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

#ifndef _EVENTS_H
#define _EVENTS_H

#include "event.h"
#include "httpd.h"

struct events_handle;

int events_open(struct events_handle **handle);
void events_close(struct events_handle *h);

int event_open(struct event_handle **handle, struct events_handle *events,
	       const char *name);
void event_close(struct event_handle *h);

extern struct url_table events_urls[];

#endif

