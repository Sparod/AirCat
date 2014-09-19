/*
 * timer.h - A timer for event management
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

#ifndef _TIMER_H
#define _TIMER_H

enum timer_day {
	TIMER_SUNDAY = 1,
	TIMER_MONDAY = 2,
	TIMER_TUESDAY = 4,
	TIMER_WEDNESDAY = 8,
	TIMER_THURSDAY = 16,
	TIMER_FRIDAY = 32,
	TIMER_SATURDAY = 64,
	TIMER_WEEK = 62,
	TIMER_WEEKEND = 65,
	TIMER_EVERY = 127,
};

enum timer_type {
	TIMER_ONE_SHUT,
	TIMER_PERIODIC,
	TIMER_DATE,
	TIMER_TIME,
};

struct timer_handle;
typedef int (*timer_event_cb)(void *);

/*
 * TIMER_ONE_SHUT: value = delay before event. When event done, timer is 
 *                         disabled
 *                 day = 0
 * TIMER_PERIODIC: value = delay before next event
 *                 day = 0
 * TIMER_DATE: value = date converted in time_t with mktime() or timer_mkdate()
 *             day = 0
 * TIMER_TIME: value = minute of day since midnight (in second)
 *             day = day of week when to do event
 */
int timer_event_add(struct timer_handle *h, const char *name,
		    const char *description, timer_event_cb cb, void *user_data,
		    int enable, enum timer_type type, uint64_t value,
		    enum timer_day day);
int timer_event_enable(struct timer_handle *h, const char *id, int enable);
int timer_event_remove(struct timer_handle *h, const char *id);

uint64_t timer_mkdate(int year, int month, int day, int hour, int minute);

#endif

