/*
 * file.h - A file input
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

#ifndef _FILE_CLIENT_H
#define _FILE_CLIENT_H

#include "file_format.h"

/* File status */
enum {
	FILE_OPENED = 0,
	FILE_EOF = 1,
	FILE_CLOSED = 2,
	FILE_NULL = -1
};

struct file_handle;

int file_open(struct file_handle **h, const char *name);

unsigned long file_get_samplerate(struct file_handle *h);
unsigned char file_get_channels(struct file_handle *h);

int file_set_pos(struct file_handle *h, int pos);
int file_get_pos(struct file_handle *h);
int file_get_length(struct file_handle *h);
int file_get_status(struct file_handle *h);

int file_read(struct file_handle *h, unsigned char *buffer, size_t size);

int file_close(struct file_handle *h);

#endif

