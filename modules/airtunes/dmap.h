/*
 * dmap.h - A Tiny DMAP parser
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

#ifndef _DMAP_H
#define _DMAP_H

#define DMAP_MAX_DEPTH 2

enum dmap_type {
	DMAP_UNKOWN,
	DMAP_UINT,
	DMAP_STR,
	DMAP_DATE,
	DMAP_VER,
	DMAP_CONT
};

struct dmap;

typedef void (*dmap_cb)(void *user_data, enum dmap_type type, const char *tag,
			const char *full_tag, const char *str, uint64_t value,
			const unsigned char *data, size_t len);
typedef void (*dmap_in_cb)(void *user_data, const char *tag,
			   const char *full_tag);
typedef void (*dmap_out_cb)(void *user_data, const char *tag,
			    const char *full_tag);

struct dmap *dmap_init(dmap_cb cb, dmap_in_cb in_cb, dmap_out_cb out_cb,
		       void *user_data);
int dmap_parse(struct dmap *d, unsigned char *buffer, size_t size);
void dmap_free(struct dmap *d);

#endif

