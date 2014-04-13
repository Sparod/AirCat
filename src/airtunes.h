/*
 * airtunes.h - A RAOP/Airplay server
 *
 * Copyright (c) 2013   A. Dilly
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

#ifndef _AIRTUNES_SERVER_H
#define _AIRTUNES_SERVER_H

#include "avahi.h"
#include "output.h"

enum{
	AIRTUNES_STARTING,
	AIRTUNES_RUNNING,
	AIRTUNES_STOPPING,
	AIRTUNES_STOPPED
};

struct airtunes_handle;

int airtunes_open(struct airtunes_handle **h, struct avahi_handle *a,
		  struct output_handle *o);

void airtunes_set_name(struct airtunes_handle *h, const char *name);
void airtunes_set_port(struct airtunes_handle *h, unsigned int port);
void airtunes_set_password(struct airtunes_handle *h, const char *password);

int airtunes_start(struct airtunes_handle *h);
int airtunes_stop(struct airtunes_handle *h);
int airtunes_status(struct airtunes_handle *h);

int airtunes_close(struct airtunes_handle *h);

#endif
