/*
 * files_list.c - Directory and library part of Files module
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>

#include "files_list.h"
#include "utils.h"
#include "json.h"

#define FILES_LIST_DEFAULT_COUNT 25

void files_list_init(struct db_handle *db)
{
	char *sql;

	/* Prepare SQL */
	sql = db_mprintf("CREATE TABLE IF NOT EXISTS path ("
			 " id INTEGER PRIMARY KEY,"
			 " path TEXT,"
			 " mtime INTEGER"
			 ");"
			 "CREATE TABLE IF NOT EXISTS song ("
			 " id INTEGER PRIMARY KEY,"
			 " file TEXT,"
			 " path_id INTEGER,"
			 " title TEXT,"
			 " artist TEXT,"
			 " album TEXT,"
			 " mtime INTEGER"
			 ")");
	if(sql == NULL)
		return;

	/* Create tables */
	db_exec(db, sql, NULL, NULL);
	db_free(sql);
}

static int files_list_get_path(struct db_handle *db, const char *path,
			       int64_t *id, time_t *mtime)
{
	struct db_query *q;
	char *sql;

	/* Generate SQL */
	sql = db_mprintf("SELECT id,mtime FROM path WHERE path='%q'", path);
	if(sql == NULL)
		return -1;

	/* Prepare request */
	q = db_prepare(db, sql, -1);

	/* Do request */
	if(db_step(q) != 0)
	{
		/* Not found: insert a new line */
		db_finalize(q);
		db_free(sql);

		/* Generate SQL */
		sql = db_mprintf("INSERT INTO path (path,mtime) "
				 "VALUES ('%q',0)", path);
		if(sql == NULL)
			return -1;

		/* Add entry in database */
		db_exec(db, sql, NULL, NULL);
		db_free(sql);

		/* Get last rowid */
		*id = db_get_last_id(db);
		*mtime = 0;
		return 0;
	}

	/* Get values */
	*id = db_column_int64(q, 0);
	*mtime = db_column_int64(q, 1);

	/* Finalize request */
	db_finalize(q);
	db_free(sql);

	return 0;
}

static int files_list_check_file(struct db_handle *db, const char *path,
				 const char *file,
				 int64_t path_id, time_t mtime, int64_t *id)
{
	struct db_query *q;
	time_t _mtime;
	char *sql;

	/* Generate SQL */
	sql = db_mprintf("SELECT id,mtime FROM song "
			 "WHERE file='%q' AND path_id='%ld'", file, path_id);
	if(sql == NULL)
		return -1;

	/* Prepare request */
	q = db_prepare(db, sql, -1);

	/* Do request */
	if(db_step(q) != 0)
	{
		/* Not found: free */
		db_finalize(q);
		db_free(sql);
		*id = 0;
		return -1;
	}

	/* Get values */
	*id = db_column_int64(q, 0);
	_mtime = db_column_int64(q, 1);

	/* Finalize request */
	db_finalize(q);
	db_free(sql);

	/* Out of date */
	if(mtime != _mtime)
		return -1;

	return 0;
}

#define FILES_SQL_INSERT "INSERT INTO song (file,title,artist,album,path_id," \
			 "mtime) " \
			 "VALUES ('%q', '%q', '%q', '%q', %ld, %ld)"
#define FILES_SQL_UPDATE "UPDATE song " \
			 "SET file='%q',title='%q',artist='%q',album='%q'," \
			 "path_id='%ld',mtime='%ld'" \
			 "WHERE id='%ld'"

static int files_list_update_file(struct db_handle *db, const char *path,
				  const char *file, int64_t mtime,
				  int64_t path_id, int64_t *id)
{
	struct file_format *format;
	char *file_path;
	char *str;
	int ret;

	/* Generate complete path */
	asprintf(&file_path, "%s/%s", path, file);
	if(file_path == NULL)
		return -1;

	/* Get format and tag from file */
	format = file_format_parse(file_path, TAG_PICTURE);
	free(file_path);

#define FMT_STR(n) format != NULL && format->n != NULL ? format->n : ""

	/* Prepare SQL request */
	str = db_mprintf(*id > 0 ? FILES_SQL_UPDATE : FILES_SQL_INSERT,
			file, FMT_STR(title), FMT_STR(artist), FMT_STR(album),
			path_id, mtime, *id);

#undef FMT_STR

	/* Add file to database */
	ret = db_exec(db, str, NULL, NULL);

	/* Get id */
	if(*id <= 0)
		*id = db_get_last_id(db);

	/* Free format and SQL request */
	db_free(str);
	file_format_free(format);

	return ret;
}

static int files_list_filter(const struct dirent *d, const struct stat *s)
{
	char *ext[] = { ".mp3", ".m4a", ".mp4", ".aac", ".ogg", ".wav", NULL };
	int i, len;

	if(s->st_mode & S_IFREG)
	{
		/* Check file ext */
		len = strlen(d->d_name);
		for(i = 0; ext[i] != NULL; i++)
		{
			if(strcmp(&d->d_name[len-4], ext[i]) == 0)
				return 1;
		}

		return 0;
	}

	return 1;
}

char *files_list_files(struct db_handle *db, const char *path, const char *uri,
		       unsigned long page, unsigned long count,
		       const char *sort)
{
	struct _dirent **list_dir = NULL;
	struct file_format *format;
	struct json *root, *tmp;
	struct db_query *q;
	char *str = NULL;
	char *real_path;
	int list_count;
	unsigned long offset = 0;
	time_t mtime;
	int64_t path_id;
	int64_t id;
	int ret;
	int i;

	/* Create new JSON array */
	root = json_new_array();
	if(root == NULL)
		return NULL;

	/* Generate path from URI */
	asprintf(&real_path, "%s/%s", path, uri != NULL ? uri : "");
	if(real_path == NULL)
		goto end;

	/* Get path id and time in database */
	if(files_list_get_path(db, uri, &path_id, &mtime) != 0)
		goto end;

	/* Scan folder in alphabetic order */
	list_count = _scandir(real_path, &list_dir, files_list_filter,
			      _alphasort_first);
	if(list_count < 0)
		goto end;

	/* Set default values */
	if(page == 0)
		page = 1;
	if(count == 0)
		count = FILES_LIST_DEFAULT_COUNT;
	offset = (page-1) * count;

	/* Parse file list */
	for(i = offset; i < list_count && count > 0; i++)
	{
		/* Process entry */
		if(list_dir[i]->mode & S_IFDIR)
		{
			/* Create a new JSON object */
			tmp = json_new();
			if(tmp == NULL)
				goto next;

			/* Add folder name */
			json_set_string(tmp, "folder", list_dir[i]->name);

			/* Add to array */
			if(json_array_add(root, tmp) != 0)
				json_free(tmp);

			count--;
		}
		else if(list_dir[i]->mode & S_IFREG)
		{
			/* Prepare SQL */
			str = db_mprintf("SELECT id,mtime FROM song "
					 "WHERE file='%q' AND path_id='%ld'",
					 list_dir[i]->name, path_id);

			/* Get id and check mtime */
			q = db_prepare(db, str, -1);
			ret = db_step(q);
			id = db_column_int64(q, 0);
			if(ret != 0 ||
			   db_column_int64(q, 1) != list_dir[i]->mtime)
			{
				/* Insert or update file in db */
				files_list_update_file(db, real_path,
						       list_dir[i]->name,
						       list_dir[i]->mtime,
						       path_id, &id);
			}

			/* Finalize request */
			db_finalize(q);
			db_free(str);

			/* Get file from database */
			str = db_mprintf("SELECT title,artist,album "
					 "FROM song WHERE id='%ld'", id);
			if(str == NULL)
				goto next;

			/* Get id and check mtime */
			q = db_prepare(db, str, -1);
			db_step(q);

			/* Create JSON object */
			tmp = json_new();
			if(tmp != NULL)
			{
				/* Add values to entry */
				json_set_string(tmp, "file", list_dir[i]->name);
				json_set_string(tmp, "title", 
							  db_column_text(q, 0));
				json_set_string(tmp, "artist",
							  db_column_text(q, 1));
				json_set_string(tmp, "album",
							  db_column_text(q, 2));

				/* Add object to array */
				if(json_array_add(root, tmp) != 0)
					json_free(tmp);
			}

			/* Finalize request */
			db_finalize(q);
			db_free(str);

			count--;
		}
next:
		/* Free dir entry */
		free(list_dir[i]);
	}

	/* Free list */
	if(list_dir != NULL)
		free(list_dir);

end:
	/* Get string from JSON object */
	str = strdup(json_export(root));

	/* Free path */
	free(real_path);

	return str;
}

