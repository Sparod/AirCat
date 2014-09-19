/*
 * timers.c - A timer for event management
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
#include <pthread.h>

#include "timers.h"
#include "utils.h"

#define TIMER_ID_SIZE 10

struct timer_event {
	/* Name */
	char id[TIMER_ID_SIZE+1];
	char *name;
	char *description;
	/* Configuration */
	enum timer_type type;
	enum timer_day day;
	time_t next_wakeup;
	uint64_t time;
	int enable;
	/* Callback */
	timer_event_cb cb;
	void *user_data;
	/* Next event */
	struct timer_event *next;
};

struct timer_handle {
	/* Timer name */
	char *name;
	/* Event list*/
	struct timer_event *events;
	/* Timers handle */
	struct timers_handle *timers;
	/* Next timer */
	struct timer_handle *next;
};

struct timers_handle {
	/* Timer list */
	struct timer_handle *timers;
	/* Event thread */
	pthread_t thread;
	pthread_cond_t cond;
	int running;
	int stop;
	/* Mutex used for timers access */
	pthread_mutex_t mutex;
};

static void *timers_thread(void *user_data);

int timers_open(struct timers_handle **handle)
{
	struct timers_handle *h;

	/* Allocate handle */
	*handle = malloc(sizeof(struct timers_handle));
	if(*handle == NULL)
		return -1;
	h = *handle;

	/* Init handle */
	h->timers = NULL;
	h->running = 0;
	h->stop = 0;

	/* Init thread mutex and condition */
	pthread_cond_init(&h->cond, NULL);
	pthread_mutex_init(&h->mutex, NULL);

	return 0;
}

int timers_start(struct timers_handle *h)
{
	if(h == NULL)
		return -1;

	/* Thread already running */
	if(h->running)
		return -1;

	/* Create thread */
	if(pthread_create(&h->thread, NULL, timers_thread, h) != 0)
		return -1;
	h->running = 1;

	return 0;
}

int timers_stop(struct timers_handle *h)
{
	if(h == NULL || h->running == 0)
		return -1;

	/* Send signal to stop thread */
	h->stop = 1;
	pthread_cond_signal(&h->cond);

	/* Wait end of thread */
	pthread_join(h->thread, NULL);
	h->running = 0;
	h->stop = 0;

	return 0;
}

static inline time_t timers_calc_next_time(struct timer_event *e)
{
	enum timer_day day;
	struct tm tm;
	time_t next;
	time_t now;

	/* Get current date */
	now = time(NULL);
	localtime_r(&now, &tm);

	/* Calculate hour and minute */
	tm.tm_hour = e->time / 3600;
	tm.tm_min = e->time / 60 - (tm.tm_hour * 60);
	tm.tm_sec = 0;

	/* Make new time */
	next = mktime(&tm);

	/* Add time before next day */
	day = 1 << tm.tm_wday;
	while(next < now || (e->day & day) == 0)
	{
		next += 86400;
		day = day == 64 ? 1 : day << 1;
	}

	return next;
}

static void timers_update_time(struct timer_event *e)
{
	/* Calculate next event */
	switch(e->type)
	{
		case TIMER_ONE_SHUT:
		case TIMER_PERIODIC:
			e->next_wakeup += e->time;
			break;
		case TIMER_DATE:
			e->next_wakeup = e->time;
			break;
		case TIMER_TIME:
			e->next_wakeup = timers_calc_next_time(e);
			break;
		default:
			e->enable = 0;
	}
}

static void *timers_thread(void *user_data)
{
	pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
	struct timers_handle *h = user_data;
	struct timer_handle *t;
	struct timer_event *e;
	struct timespec ts;
	time_t now;

	/* Lock condition */
	pthread_mutex_lock(&mutex);

	/* Loop until stop signal */
	while(!h->stop)
	{
		/* Get current time */
		now = time(NULL);

		/* Lock timers access */
		pthread_mutex_lock(&h->mutex);

		/* Process timers */
		for(t = h->timers; t != NULL; t = t->next)
		{
			/* Process events */
			for(e = t->events; e != NULL; e = e->next)
			{
				/* Check event */
				if(e->enable && e->next_wakeup < now)
				{
					/* Do task */
					e->cb(e->user_data);

					/* Update event */
					if(e->type == TIMER_ONE_SHUT ||
					   e->type == TIMER_DATE)
						e->enable = 0;
					else
						timers_update_time(e);
				}
			}
		}

		/* Unlock timers access */
		pthread_mutex_unlock(&h->mutex);

		/* Prepare sleep: 1 minute */
		ts.tv_sec = now + 60;
		ts.tv_nsec = 0;

		/* Sleep or wait stop signal */
		pthread_cond_timedwait(&h->cond, &mutex, &ts);
	}

	/* Unlock condition */
	pthread_mutex_unlock(&mutex);

	return NULL;
}

void timers_close(struct timers_handle *h)
{
	struct timer_handle *t;

	if(h == NULL)
		return;

	/* Stop all timers and events */
	timers_stop(h);

	/* Free timers */
	while(h->timers != NULL)
	{
		t = h->timers;
		h->timers = t->next;

		/* Free event */
		timer_close(t);
	}

	/* Destroy mutex and cond */
	pthread_cond_destroy(&h->cond);
	pthread_mutex_destroy(&h->mutex);

	/* Free handle */
	free(h);
}

int timer_open(struct timer_handle **handle, struct timers_handle *timers,
	       const char *name)
{
	struct timer_handle *h;

	/* Allocate handle */
	*handle = malloc(sizeof(struct timer_handle));
	if(*handle == NULL)
		return -1;
	h = *handle;

	/* Init handle */
	h->events = NULL;
	h->timers = timers;
	h->name = name != NULL ? strdup(name) : NULL;

	/* Lock timers access*/
	pthread_mutex_lock(&h->timers->mutex);

	/* Add to timer list */
	h->next = timers->timers;
	timers->timers = h;

	/* Unlock timers access*/
	pthread_mutex_unlock(&h->timers->mutex);

	return 0;
}

int timer_event_add(struct timer_handle *h, const char *name,
		    const char *description, timer_event_cb cb, void *user_data,
		    int enable, enum timer_type type, uint64_t value,
		    enum timer_day day)
{
	struct timer_event *e;

	/* Check event */
	if(type == TIMER_TIME && day == 0)
		return -1;

	/* Allocate a new event */
	e = malloc(sizeof(struct timer_event));
	if(e == NULL)
		return -1;

	/* Generate a random id */
	random_string(e->id, TIMER_ID_SIZE);

	/* Fill event */
	e->enable = enable;
	e->cb = cb;
	e->user_data = user_data;
	e->name = name != NULL ? strdup(name) : NULL;
	e->description = description != NULL ? strdup(description) : NULL;
	e->type = type;
	e->time = value;
	e->day = day;
	e->next_wakeup = time(NULL);

	/* Disable event if date is passed */
	if(type == TIMER_DATE && (e->time + 60) < e->next_wakeup)
		e->enable = 0;

	/* Prepare event */
	if(e->enable)
		timers_update_time(e);

	/* Lock timers access */
	pthread_mutex_lock(&h->timers->mutex);

	/* Add to list */
	e->next = h->events;
	h->events = e;

	/* Unlock timers access */
	pthread_mutex_unlock(&h->timers->mutex);

	return 0;
}

int timer_event_enable(struct timer_handle *h, const char *id, int enable)
{
	struct timer_event *e;

	/* Lock timers access */
	pthread_mutex_lock(&h->timers->mutex);

	/* Find event and remove from list */
	for(e = h->events; e != NULL; e = e->next)
	{
		if(strcmp(e->id, id) == 0)
		{
			/* Enable/Disable event */
			e->enable = enable;

			/* Update event */
			timers_update_time(e);

			/* Unlock timers access */
			pthread_mutex_unlock(&h->timers->mutex);
			return 0;
		}
	}

	/* Unlock timers access */
	pthread_mutex_unlock(&h->timers->mutex);

	return -1;
}

static void timer_event_free(struct timer_event *e)
{
	/* Free strings */
	if(e->name != NULL)
		free(e->name);
	if(e->description != NULL)
		free(e->description);

	/* Free event */
	free(e);
}

int timer_event_remove(struct timer_handle *h, const char *id)
{
	struct timer_event **ep, *e = NULL;

	/* Lock timers access */
	pthread_mutex_lock(&h->timers->mutex);

	/* Find event and remove from list */
	ep = &h->events;
	while((*ep) != NULL)
	{
		e = *ep;
		if(strcmp(e->id, id) == 0)
		{
			*ep = e->next;
			break;
		}
		else
			ep = &e->next;
	}

	/* Unlock timers access */
	pthread_mutex_unlock(&h->timers->mutex);

	/* Event not found */
	if(e == NULL)
		return -1;

	/* Free event */
	timer_event_free(e);

	return 0;
}

void timer_close(struct timer_handle *h)
{
	struct timer_handle **tp, *t;
	struct timer_event *e;

	if(h == NULL)
		return;

	/* Free events */
	while(h->events != NULL)
	{
		e = h->events;
		h->events = e->next;

		/* Free event */
		timer_event_free(e);
	}

	/* Free strings */
	if(h->name != NULL)
		free(h->name);

	/* Lock timers access */
	pthread_mutex_lock(&h->timers->mutex);

	/* Remove from timer list */
	tp = &h->timers->timers;
	while((*tp) != NULL)
	{
		t = *tp;
		if(t == h)
		{
			*tp = t->next;
			break;
		}
		else
			tp = &t->next;
	}

	/* Unlock timers access*/
	pthread_mutex_unlock(&h->timers->mutex);

	/* Free handle */
	free(h);
}

uint64_t timer_mkdate(int year, int month, int day, int hour, int minute)
{
	struct tm tm;
	time_t t;

	/* Fill tm struct */
	tm.tm_sec = 0;
	tm.tm_min = minute;
	tm.tm_hour = hour;
	tm.tm_mday = day;
	tm.tm_mon = month - 1;
	tm.tm_year = year - 1900;
	tm.tm_isdst = -1;

	/* Make time */
	t = mktime(&tm);
	if(t < 0)
		return -1;

	return (uint64_t) t;
}

struct url_table timers_urls[] = {
	{0, 0, 0, 0}
};

