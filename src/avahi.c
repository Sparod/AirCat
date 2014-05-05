/*
 * avahi.c - Avahi client and service publisher
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

#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include <avahi-client/client.h>
#include <avahi-client/publish.h>

#include <avahi-common/alternative.h>
#include <avahi-common/simple-watch.h>
#include <avahi-common/malloc.h>
#include <avahi-common/error.h>
#include <avahi-common/timeval.h>

#include "avahi.h"

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

struct avahi_service {
	char *name;
	char *type;
	unsigned int port;
	AvahiStringList *txt;
	AvahiClient *client;
	AvahiEntryGroup *group;
	int failed;
	struct avahi_service *next;
};

struct avahi_handle {
	AvahiSimplePoll *simple_poll;
	struct avahi_service *service_list;
};

static void avahi_entry_group_callback(AvahiEntryGroup *g, AvahiEntryGroupState state, void *userdata)
{
	struct avahi_service *s = (struct avahi_service *) userdata;

	/* Verify group */
	if(g == NULL)
		return;

	/* Called whenever the entry group state changes */
	switch (state)
	{
		case AVAHI_ENTRY_GROUP_ESTABLISHED:
			/* The entry group has been established successfully */
			break;
		case AVAHI_ENTRY_GROUP_COLLISION:
		{
			/* Service collision => failed */
			s->failed = 1;
			break;
		}
		case AVAHI_ENTRY_GROUP_FAILURE:
			/* Some kind of failure happened while we were registering our services */
			s->failed = 1;
			break;
		case AVAHI_ENTRY_GROUP_UNCOMMITED:
		case AVAHI_ENTRY_GROUP_REGISTERING:
			;
	}
}

static void avahi_client_callback(AvahiClient *c, AvahiClientState state, void *userdata)
{
	struct avahi_service *s = (struct avahi_service *) userdata;
	int ret;

	/* Verify client */
	if(c == NULL)
		return;

	/* Called whenever the client or server state changes */
	switch (state)
	{
		case AVAHI_CLIENT_S_RUNNING:
			/* Register service if new detected */
			if(s->group == NULL)
			{
				s->group = avahi_entry_group_new(c, avahi_entry_group_callback, s);
				if(s->group == NULL)
				{
					s->failed = 1;
					return;
				}
			}

			if(avahi_entry_group_is_empty(s->group))
			{
				ret = avahi_entry_group_add_service_strlst(s->group, AVAHI_IF_UNSPEC, AVAHI_PROTO_UNSPEC, 0, s->name, s->type, NULL, NULL, s->port, s->txt);
				if(ret != 0)
				{
					s->failed = 1;
					return;
				}

				/* Tell the server to register the service */
				ret = avahi_entry_group_commit(s->group);
				if(ret < 0)
				{
					s->failed = 1;
					return;
				}
			}
			break;
		case AVAHI_CLIENT_FAILURE:
			/* The client failed => close all */
			s->failed = 1;
			break;
		case AVAHI_CLIENT_S_COLLISION:
			/* Let's drop our registered services. When the server is back
			* in AVAHI_SERVER_RUNNING state we will register them
			* again with the new host name. */
		case AVAHI_CLIENT_S_REGISTERING:
			/* The server records are now being established. This
			* might be caused by a host name change. We need to wait
			* for our own records to register until the host name is
			* properly esatblished. */
			avahi_entry_group_reset(s->group);
			break;
		case AVAHI_CLIENT_CONNECTING:
			;
	}
}

int avahi_open(struct avahi_handle **handle)
{
	struct avahi_handle *h;

	/* Allocate structure */
	*handle = malloc(sizeof(struct avahi_handle));
	if(*handle == NULL)
		return -1;
	h = *handle;

	/* Init structure */
	h->service_list = NULL;

	/* Allocate main loop object */
	h->simple_poll = avahi_simple_poll_new();
	if(h == NULL)
		return -1;

	return 0;
}

int avahi_add_service(struct avahi_handle *h, const char *name, const char *type, unsigned int port, ...)
{
	struct avahi_service *s;
	va_list va;

	if(h == NULL)
		return -1;

	/* Search if name is already registered with same port */
	s = h->service_list;
	while(s != NULL)
	{
		if(strcmp(s->name, name) == 0 && s->port == port)
			return -1;
		s = s->next;
	}

	/* Add service to list */
	s = malloc(sizeof(struct avahi_service));
	s->name = strdup(name);
	s->type = strdup(type);
	s->port = port;
//	s->txt = strdup(txt);
	s->client = NULL;
	s->group = NULL;
	s->failed = 0;
	s->next = h->service_list;
	h->service_list = s;

	/* Create txt list */
	va_start(va, port);
	s->txt = avahi_string_list_new_va(va);
	va_end(va);

	/* Allocate a new client */
	s->client = avahi_client_new(avahi_simple_poll_get(h->simple_poll), 0, avahi_client_callback, s, NULL);
	if(s->client == NULL)
		return -1;

	return 0;
}

int avahi_remove_service(struct avahi_handle *h, const char *name, unsigned int port)
{
	struct avahi_service *s, *s_prev = NULL;

	if(h == NULL)
		return -1;

	/* Search if service exist */
	s = h->service_list;
	while(s != NULL)
	{
		if(strcmp(s->name, name) == 0 && s->port == port)
		{
			if(s == h->service_list)
				h->service_list = s->next;
			else
				s_prev->next = s->next;

			/* Remove from list */
			if(s->name != NULL)
				free(s->name);
			if(s->type != NULL)
				free(s->type);
			if(s->txt != NULL)
				avahi_string_list_free(s->txt);
			/* Close entry group */
			if(s->group != NULL)
				avahi_entry_group_free(s->group);
			/* Close client */
			if(s->client != NULL)
				avahi_client_free(s->client);

			free(s);

			return 0;
		}
		s_prev = s;
		s = s->next;
	}

	return -1;
}

int avahi_loop(struct avahi_handle *h, int timeout)
{
	if(h == NULL)
		return -1;

	return avahi_simple_poll_iterate(h->simple_poll, timeout);
}

int avahi_close(struct avahi_handle *h)
{
	struct avahi_service *s;

	if(h == NULL)
		return 0;

	/* Stop services */
	while(h->service_list != NULL)
	{
		s = h->service_list;
		h->service_list = s->next;
		if(s->name != NULL)
			free(s->name);
		if(s->type != NULL)
			free(s->type);
		if(s->txt != NULL)
			free(s->txt);
		/* Close entry group */
		if(s->group != NULL)
			avahi_entry_group_free(s->group);
		/* Close client */
		if(s->client != NULL)
			avahi_client_free(s->client);
		
		free(s);
	}

	/* Close poll */
	if(h->simple_poll != NULL)
		avahi_simple_poll_free(h->simple_poll);

	free(h);

	return 0;
}

