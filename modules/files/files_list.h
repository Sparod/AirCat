/*
 * files_list.h - Directory and library part of Files module
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

#ifndef _FILES_LIST_H
#define _FILES_LIST_H

#include "file_format.h"
#include "db.h"

void files_list_init(struct db_handle *db);
char *files_list_files(struct db_handle *db, const char *path, const char *uri,
		       unsigned long page, unsigned long count,
		       const char *sort);

#endif

