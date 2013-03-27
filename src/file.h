/*
 * file.h - A file input
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

#ifndef _FILE_CLIENT_H
#define _FILE_CLIENT_H

struct file_handle;

struct file_handle *file_init();

int file_open(struct file_handle *h, const char *name);

int file_read(struct file_handle *h, float *buffer, size_t size);

int file_close(struct file_handle *h);

#endif

