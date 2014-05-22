/*
 * avahi.h - Avahi client and service publisher
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

#ifndef _AVAHI_CLIENT_H
#define _AVAHI_CLIENT_H

struct avahi_handle;

int avahi_open(struct avahi_handle **h);
int avahi_add_service(struct avahi_handle *h, const char *name, const char *type, unsigned int port, ...);
int avahi_remove_service(struct avahi_handle *h, const char *name, unsigned int port);
int avahi_loop(struct avahi_handle *h, int timeout);
int avahi_close(struct avahi_handle *h);

#endif

