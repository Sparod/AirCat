/*
 * timers.h - A timer for event management
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

#ifndef _TIMERS_H
#define _TIMERS_H

#include "httpd.h"
#include "timer.h"

struct timers_handle;
extern struct url_table timers_urls[];

int timers_open(struct timers_handle **h);
int timers_start(struct timers_handle *h);
int timers_stop(struct timers_handle *h);
void timers_close(struct timers_handle *h);

int timer_open(struct timer_handle **h, struct timers_handle *timers,
	       const char *name);
void timer_close(struct timer_handle *h);

#endif

