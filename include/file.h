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

#include "format.h"

/* File status */
enum {
	FILE_OPENED = 0,
	FILE_EOF = 1,
	FILE_CLOSED = 2,
	FILE_NULL = -1
};

struct file_handle;

int file_open(struct file_handle **h, const char *uri);

unsigned long file_get_samplerate(struct file_handle *h);
unsigned char file_get_channels(struct file_handle *h);

unsigned long file_set_pos(struct file_handle *h, unsigned long pos);
unsigned long file_get_pos(struct file_handle *h);
long file_get_length(struct file_handle *h);
int file_get_status(struct file_handle *h);

int file_read(void *h, unsigned char *buffer, size_t size,
	      struct a_format *fmt);
void file_close(struct file_handle *h);

/* File event */
enum file_event {
	FILE_EVENT_READY,	/*!< File is ready (cache is full) */
	FILE_EVENT_BUFFERING,	/*!< File is buffering (cache not full) */
	FILE_EVENT_SEEK, 	/*!< Seek has been done: new position is in 
				     data as an unsigned long */
	FILE_EVENT_END		/*!< End of file has been reached */
};
typedef void (*file_event_cb)(void *user_data, enum file_event event,
			      void *data);
int file_set_event_cb(struct file_handle *h, file_event_cb cb, void *user_data);

#endif

