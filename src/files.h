/*
 * files.h - A File manager module
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

#ifndef _FILES_H
#define _FILES_H

#include "output.h"

struct files_handle;

int files_open(struct files_handle **handle, struct output_handle *o);
int files_add(struct files_handle *h, const char *filename);
int files_remove(struct files_handle *h, int index);
void files_flush(struct files_handle *h);
int files_play(struct files_handle *h, int index);
int files_pause(struct files_handle *h);
int files_stop(struct files_handle *h);
int files_prev(struct files_handle *h);
int files_next(struct files_handle *h);
char *files_get_json_status(struct files_handle *h, int add_pic);
char *files_get_json_playlist(struct files_handle *h);
char *files_get_json_list(struct files_handle *h, const char *path);
int files_close(struct files_handle *h);

#endif

