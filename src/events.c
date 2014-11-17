/*
 * events.c - Event system for multiplexed notification
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

#include "events.h"

#define EVENT_SESSION_KEY "event_last"

struct event {
	char *name;		/*!< Event name */
	enum event_type type;	/*!< Event type */
	struct json *data;	/*!< Event data */
	time_t timestamp;	/*!< Event time stamp */
	struct event *next;	/*!< Next event in list */
};

struct event_handle {
	char *name;			/*!< Name of this child */
	struct event *evs;		/*!< Event list */
	pthread_mutex_t mutex;		/*!< Mutex for local event access */
	struct events_handle *events;	/*!< Parent events handle */
	struct event_handle *next;	/*!< Next handle in list */
};

struct events_handle {
	struct event_handle *events;	/*!< Event handle list (children) */
	pthread_mutex_t mutex;		/*!< Mutex for events access */
};

int events_open(struct events_handle **handle)
{
	struct events_handle *h;

	/* Allocate handle */
	*handle = malloc(sizeof(struct events_handle));
	if(*handle == NULL)
		return -1;
	h = *handle;

	/* Init handle */
	h->events = NULL;

	/* Init mutex */
	pthread_mutex_init(&h->mutex, NULL);

	return 0;
}

static char *events_get_events(struct events_handle *h, time_t *last)
{
	struct json *root, *hev, *aev, *jev;
	struct event_handle *eh;
	struct event *ev;
	char *str = NULL;
	time_t max = *last;

	/* Create a new JSON array */
	root = json_new_array();
	if(root == NULL)
		return NULL;

	/* Lock events access */
	pthread_mutex_lock(&h->mutex);

	/* Fill list */
	for(eh = h->events; eh != NULL; eh = eh->next)
	{
		/* Create a new entry */
		hev = json_new();
		if(hev == NULL)
			continue;

		/* Create a new array */
		aev = json_new_array();
		if(aev == NULL)
			continue;

		/* Lock event list access */
		pthread_mutex_lock(&eh->mutex);

		for(ev = eh->evs; ev != NULL; ev = ev->next)
		{
			/* If event is too old, go to next handler */
			if(ev->timestamp <= *last)
				break;

			/* Update highest timestamp */
			if(ev->timestamp > max)
				max = ev->timestamp;

			/* Create a new event */
			jev = json_new();
			if(jev == NULL)
				continue;

			/* Fill event */
			json_set_string(jev, "name", ev->name);
			json_set_int(jev, "type", ev->type);
			json_set_int64(jev, "ts", ev->timestamp);
			json_add(jev, "data", json_copy(ev->data));

			/* Add event to list */
			if(json_array_add(aev, jev) != 0)
				json_free(jev);
		}

		/* Unlock event list access */
		pthread_mutex_unlock(&eh->mutex);

		/* Fill object */
		json_set_string(hev, "name", eh->name);
		json_add(hev, "events", aev);

		/* Add to list */
		if(json_array_add(root, hev) != 0)
			json_free(hev);
	}

	/* Unlock events access */
	pthread_mutex_unlock(&h->mutex);

	/* Update highest timestamp */
	*last = max;

	/* Export JSON object to string */
	str = (char *) json_export(root);
	if(str != NULL)
		str = strdup(str);

	/* Free JSON object */
	json_free(root);

	return str;
}

void events_close(struct events_handle *h)
{
	if(h == NULL)
		return;

	/* Free event handle */
	while(h->events != NULL)
	{
		/* Close event */
		event_close(h->events);
	}

	/* Free handle */
	free(h);
}

int event_open(struct event_handle **handle, struct events_handle *events,
	       const char *name)
{
	struct event_handle *h;

	/* Allocate handle */
	*handle = malloc(sizeof(struct event_handle));
	if(*handle == NULL)
		return -1;
	h = *handle;

	/* Init handle */
	h->name = name != NULL ? strdup(name) : NULL;
	h->events = events;
	h->evs = NULL;

	/* Init mutex */
	pthread_mutex_init(&h->mutex, NULL);

	/* Lock events access */
	pthread_mutex_lock(&events->mutex);

	/* Add handle to main list */
	h->next = events->events;
	events->events = h;

	/* Unlock events access */
	pthread_mutex_unlock(&events->mutex);

	return 0;
}

static struct event *event_remove_from_list(struct event_handle *h,
					    const char *name)
{
	struct event **evp, *ev = NULL;

	/* Lock event list access */
	pthread_mutex_lock(&h->mutex);

	/* Find event and remove it */
	evp = &h->evs;
	while((*evp) != NULL)
	{
		ev = *evp;
		if(strcmp(ev->name, name) == 0)
		{
			*evp = ev->next;
			break;
		}
		else
			evp = &ev->next;
	}

	/* Unlock event list access */
	pthread_mutex_unlock(&h->mutex);

	return ev;
}

int event_add(struct event_handle *h, const char *name, enum event_type type,
	      struct json *data)
{
	struct event *ev;

	/* Check name */
	if(name == NULL)
		return -1;

	/* Find event if already exist */
	ev = event_remove_from_list(h, name);

	/* Event not found */
	if(ev == NULL)
	{
		/* Allocate new event */
		ev = malloc(sizeof(struct event));
		if(ev == NULL)
			return -1;

		/* Add name */
		ev->name = strdup(name);
	}
	else
	{
		/* Free previous data */
		if(ev->data != NULL)
			json_free(ev->data);
	}

	/* Fill event */
	ev->type = type;
	ev->timestamp = time(NULL);
	ev->data = data;

	/* Lock event list access */
	pthread_mutex_lock(&h->mutex);

	/* Add to list */
	ev->next = h->evs;
	h->evs = ev;

	/* Unlock event list access */
	pthread_mutex_unlock(&h->mutex);

	return 0;
}

static void event_free(struct event *ev)
{
	/* Free name event */
	free(ev->name);

	/* Free event data */
	if(ev->data != NULL)
		json_free(ev->data);

	/* Free event */
	free(ev);
}

int event_remove(struct event_handle *h, const char *name)
{
	struct event *ev;

	/* Find event */
	ev = event_remove_from_list(h, name);
	if(ev == NULL)
		return -1;

	/* Free event */
	event_free(ev);

	return 0;
}

void event_flush(struct event_handle *h)
{
	struct event *ev;

	/* Lock event list access */
	pthread_mutex_lock(&h->mutex);

	/* Remove all events */
	while(h->evs != NULL)
	{
		ev = h->evs;
		h->evs = ev->next;
		event_free(ev);
	}

	/* Unlock event list access */
	pthread_mutex_unlock(&h->mutex);
}

void event_close(struct event_handle *h)
{
	struct event_handle **ep, *e;

	if(h == NULL)
		return;

	/* Lock events access*/
	pthread_mutex_lock(&h->events->mutex);

	/* Remove from event handle list */
	ep = &h->events->events;
	while((*ep) != NULL)
	{
		e = *ep;
		if(e == h)
		{
			*ep = e->next;
			break;
		}
		else
			ep = &e->next;
	}

	/* Unlock events access*/
	pthread_mutex_unlock(&h->events->mutex);

	/* Flush event list */
	event_flush(h);

	/* Free strings */
	if(h->name != NULL)
		free(h->name);

	/* Free handle */
	free(h);
}

static int events_httpd_get_events(void *user_data, struct httpd_req *req,
				   struct httpd_res **res)
{
	struct events_handle *h = user_data;
	time_t last = 0;
	char value[11];
	char *str;

	/* Get last event from session */
	str = httpd_get_session_value(req, EVENT_SESSION_KEY);
	if(str != NULL)
	{
		last = strtoul(str, NULL, 10);
		free(str);
	}

	/* Get event list from last event */
	str = events_get_events(h, &last);
	if(str == NULL)
		return 500;

	/* Generate string for new session value */
	snprintf(value, sizeof(value), "%lu", last);

	/* Update last event in session */
	httpd_set_session_value(req, EVENT_SESSION_KEY, value);

	/* Create HTTP response */
	*res = httpd_new_response(str, 1, 0);
	return 200;
}

struct url_table events_urls[] = {
	{"", 0, HTTPD_GET, 0, &events_httpd_get_events},
	{0, 0, 0, 0, 0}
};

