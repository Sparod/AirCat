/*
 * outputs.h - Audio output module
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

#ifndef _OUTPUTS_H
#define _OUTPUTS_H

#include "httpd.h"
#include "output.h"
#include "json.h"

struct output_module {
	int (*open)(void **, unsigned int, int);
	int (*set_volume)(void *, unsigned int);
	unsigned int (*get_volume)(void *);
	void *(*add_stream)(void *, unsigned long, unsigned char, unsigned long,
			    int, a_read_cb, void *);
	int (*play_stream)(void *, void *);
	int (*pause_stream)(void *, void *);
	void (*flush_stream)(void *, void *);
	int (*set_volume_stream)(void *, void *, unsigned int);
	unsigned int (*get_volume_stream)(void *, void *);
	unsigned long (*get_status_stream)(void *, void *,
					   enum output_stream_key);
	int (*remove_stream)(void *, void *);
	int (*close)(void *);
};

struct outputs_handle;
extern struct url_table outputs_urls[];

int outputs_open(struct outputs_handle **handle, struct json *config);
int outputs_set_config(struct outputs_handle *h, struct json *cfg);
struct json *outputs_get_config(struct outputs_handle *h);
int outputs_set_volume(struct outputs_handle *h, unsigned int volume);
unsigned int outputs_get_volue(struct outputs_handle *h);
void outputs_close(struct outputs_handle *h);

int output_open(struct output_handle **handle, struct outputs_handle *outputs,
		const char *name);
void output_close(struct output_handle *h);

#endif
