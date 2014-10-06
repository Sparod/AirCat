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

#include "json.h"
#include "db.h"

enum files_list_sort {
	FILES_LIST_SORT_DEFAULT,
	FILES_LIST_SORT_REVERSE,
	FILES_LIST_SORT_ALPHA,
	FILES_LIST_SORT_ALPHA_REVERSE,
	FILES_LIST_SORT_TITLE = 10,
	FILES_LIST_SORT_ALBUM,
	FILES_LIST_SORT_ARTIST,
	FILES_LIST_SORT_TRACK,
	FILES_LIST_SORT_YEAR,
	FILES_LIST_SORT_DURATION,
	FILES_LIST_SORT_TITLE_REVERSE = 100,
	FILES_LIST_SORT_ALBUM_REVERSE,
	FILES_LIST_SORT_ARTIST_REVERSE,
	FILES_LIST_SORT_TRACK_REVERSE,
	FILES_LIST_SORT_YEAR_REVERSE,
	FILES_LIST_SORT_DURATION_REVERSE,
};

enum files_list_display {
	FILES_LIST_DISPLAY_DEFAULT = 0,
	FILES_LIST_DISPLAY_ALBUM,
	FILES_LIST_DISPLAY_ARTIST,
	FILES_LIST_DISPLAY_GENRE,
};

/* Init database */
void files_list_init(struct db_handle *db, const char *path);

/* Get file info */
struct json *files_list_file(struct db_handle *db, const char *cover_path,
			     uint64_t media_id, const char *uri);

/* List files */
char *files_list_files(struct db_handle *db, const char *cover_path,
		       uint64_t media_id, const char *uri, unsigned long page,
		       unsigned long count, enum files_list_sort sort,
		       enum files_list_display display, uint64_t artist_id,
		       uint64_t album_id, uint64_t genre_id,
		       const char *filter);

/* List/scan Media storage (local or network) */
char *files_list_media(struct db_handle *db, const char *path,
		       const char *mount_path);
int files_list_add_media(struct db_handle *db, const char *name,
			 const char *path, uint64_t media_id);
int files_list_delete_media(struct db_handle *db, uint64_t media_id);
char *files_list_get_media(struct db_handle *db, uint64_t media_id);

/* Scan files */
int files_list_scan(struct db_handle *db, const char *cover_path,
		    uint64_t media_id, int recursive);
char *files_list_get_scan(void);
int files_list_is_scanning(void);

/* List files with callback */
typedef int (*files_list_fn)(void *, uint64_t, const char *, int, int);
int files_list_list(struct db_handle *db, const char *uri, files_list_fn fn,
		    void *user_data);

/* File extension */
extern char *files_ext[];
static inline int files_ext_check(const char *name)
{
	int len = strlen(name);
	int i;

	for(i = 0; files_ext[i] != NULL; i++)
	{
		if(strcmp(&name[len-4], files_ext[i]) == 0)
			return 1;
	}

	return 0;
}

#endif

