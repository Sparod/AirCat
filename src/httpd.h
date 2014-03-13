/*
 * httpd.h - An HTTP server
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

#ifndef _HTTP_SERVER_H
#define _HTTP_SERVER_H

#include "airtunes.h"

struct httpd_attr{
	/* Audio output module */
	//struct output_handle *output;
	/* Radio module */
	struct radio_handle *radio;
	/* Airtunes module */
	struct airtunes_handle *airtunes;
	/* Config file */
	char *config_filename;
};

struct httpd_handle;

int httpd_open(struct httpd_handle **handle, struct httpd_attr *attr);
int httpd_start(struct httpd_handle *h);
int httpd_stop(struct httpd_handle *h);
int httpd_close(struct httpd_handle *h);

#endif
