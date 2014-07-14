/*
 * cache.h - A generic cache module
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

#ifndef _CACHE_H
#define _CACHE_H

#include "format.h"

struct cache_handle;

int cache_open(struct cache_handle **handle, unsigned long size, int use_thread,
	       a_read_cb input_callback, void *input_user,
	       a_write_cb output_callback, void *output_user);
int cache_is_ready(struct cache_handle *h);
unsigned char cache_get_filling(struct cache_handle *h);
int cache_read(void *h, unsigned char *buffer, size_t size,
	       struct a_format *fmt);
ssize_t cache_write(void *h, const unsigned char *buffer, size_t size,
		    struct a_format *fmt);
void cache_flush(struct cache_handle *h);
void cache_lock(struct cache_handle *h);
void cache_unlock(struct cache_handle *h);
int cache_close(struct cache_handle *h);

#endif

