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
#include <unistd.h>

#include "file_format.h"
#include "files_list.h"
#include "utils.h"
#include "json.h"

#define FILES_LIST_DEFAULT_COUNT 25

static char *files_ext[] = {
	".mp3", ".m4a", ".mp4",	".aac",	".ogg",	".wav",
	NULL
};

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
			 " cover TEXT,"
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
	char *gpath = NULL;
	char last = '/';
	char *sql;
	int len;
	int i, j;

	/* Remove multiple slash in path string */
	if(path != NULL)
	{
		/* Prepare new string */
		len = strlen(path);
		gpath = malloc(len + 1);
		if(gpath == NULL)
			return -1;

		/* Copy string */
		for(i = 0, j = 0; i < len; i++)
		{
			if(last != '/' || path[i] != '/')
				gpath[j++] = path[i];
			last = path[i];
		}
		if(last == '/' && j > 0)
			j--;
		gpath[j] = '\0';
	}

	/* Generate SQL */
	sql = db_mprintf("SELECT id,mtime FROM path WHERE path='%q'",
			 gpath != NULL ? gpath : "");
	if(sql == NULL)
	{
		if(gpath != NULL)
			free(gpath);
		return -1;
	}

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
				 "VALUES ('%q',0)", gpath != NULL ? gpath : "");
		if(sql == NULL)
		{
			if(gpath != NULL)
				free(gpath);
			return -1;
		}

		/* Add entry in database */
		db_exec(db, sql, NULL, NULL);
		db_free(sql);

		/* Get last rowid */
		*id = db_get_last_id(db);
		if(mtime != NULL)
			*mtime = 0;

		/* Free good path */
		if(gpath != NULL)
			free(gpath);

		return 0;
	}

	/* Get values */
	*id = db_column_int64(q, 0);
	if(mtime != NULL)
		*mtime = db_column_int64(q, 1);

	/* Finalize request */
	db_finalize(q);
	db_free(sql);

	/* Free good path */
	if(gpath != NULL)
		free(gpath);

	return 0;
}

static char *files_list_save_cover(struct file_format *format, const char *path,
				   const char *file)
{
	char *file_path;
	char *cover;
	char *md5;
	FILE *fp;
	int len;

	/* Calculate hash of image */
	md5 = md5_encode_str(format->picture.data, format->picture.size);
	if(md5 == NULL || *md5 == '\0')
	{
		if(format->artist == NULL && format->album == NULL)
			cover = strdup(file);
		else
			asprintf(&cover , "%s_%s.xxx",
				 format->artist != NULL ? format->artist : "",
				 format->album != NULL ? format->album : "");
		if(cover == NULL)
			goto end;
		len = strlen(cover) - 4;
	}
	else
	{
		len = strlen(md5);
		cover = realloc(md5, len + 5);
		if(cover == NULL)
			goto end;
		md5 = NULL;
	}

	/* Generate file name with extension */
	cover[len] = '\0';
	if(format->picture.mime != NULL)
	{
		if(strcmp(format->picture.mime, "image/jpeg") == 0 ||
		   strcmp(format->picture.mime, "image/jpg") == 0)
			strcpy(&cover[len], ".jpg");
		else if(strcmp(format->picture.mime, "image/png") == 0)
			strcpy(&cover[len], ".png");
	}

	/* No mime type found */
	if(cover[len] =='\0')
	{
		/* Guess mime type with picture data */
		if(format->picture.size >= 2 &&
		   format->picture.data[0] == 0xFF &&
		   format->picture.data[1] == 0xD8)
			strcpy(&cover[len], ".jpg");
		else if(format->picture.size >= 4 &&
			memcmp(&format->picture.data[1], "PNG", 3) == 0)
			strcpy(&cover[len], ".png");
	}

	/* Generate complete file path */
	asprintf(&file_path, "%s/%s", path, cover);
	if(file_path == NULL)
		goto end;

	/* Create file */
	if(access(file_path, F_OK ) == 0)
		goto end;

	/* Open new file */
	fp = fopen(file_path, "wb");
	if(fp == NULL)
		goto end;

	/* Write data to it */
	fwrite(format->picture.data, format->picture.size, 1, fp);

	/* Close file */
	fclose(fp);

end:
	if(md5 != NULL)
		free(md5);
	if(file_path != NULL)
		free(file_path);
	return cover;
}

#define FILES_SQL_INSERT "INSERT INTO song (file,title,artist,album,cover," \
			 "path_id,mtime) " \
			 "VALUES ('%q', '%q', '%q', '%q', '%q', %ld, %ld)"
#define FILES_SQL_UPDATE "UPDATE song " \
			 "SET file='%q',title='%q',artist='%q',album='%q'," \
			 "cover='%q',path_id='%ld',mtime='%ld'" \
			 "WHERE id='%ld'"

static int files_list_update_file(struct db_handle *db, const char *cover_path,
				  const char *path, const char *file,
				  int64_t mtime, int64_t path_id, int64_t id)
{
	struct file_format *format;
	char *cover = NULL;
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

	/* Save format */
	if(format != NULL && format->picture.data != NULL &&
	   format->picture.size > 0)
		cover = files_list_save_cover(format, cover_path, file);

#define FMT_STR(n) format != NULL && format->n != NULL ? format->n : ""

	/* Prepare SQL request */
	str = db_mprintf(id > 0 ? FILES_SQL_UPDATE : FILES_SQL_INSERT,
			 file, FMT_STR(title), FMT_STR(artist), FMT_STR(album),
			 cover != NULL ? cover : "", path_id, mtime, id);

#undef FMT_STR

	/* Add file to database */
	ret = db_exec(db, str, NULL, NULL);

	/* Free format and SQL request */
	db_free(str);
	file_format_free(format);

	/* Free cover */
	if(cover != NULL)
		free(cover);

	return ret;
}

static int files_list_add_meta(struct db_handle *db, struct json *root,
			       const char *path, const char *file,
			       int64_t path_id, int64_t mtime, int parse,
			       const char *cover_path)
{
	struct db_query *query;
	char *sql = NULL;
	int up = 0;
	int ret;

	/* Prepare SQL request */
	sql = db_mprintf("SELECT id,mtime,title,artist,album,cover "
			 "FROM song WHERE file='%q' AND path_id='%ld'",
			 file, path_id);
	if(sql == NULL)
		goto end;

retry:
	/* Prepare request */
	query = db_prepare(db, sql, -1);
	if(query == NULL)
		goto end;

	/* Do request */
	ret = db_step(query);

	/* File not present or out of date in database */
	if(!up && (ret != 0 || db_column_int64(query, 1) != mtime))
	{
		/* Skip parsing */
		if(!parse)
			goto end;

		/* Add or update file in database */
		files_list_update_file(db, cover_path, path, file, mtime,
				       path_id, db_column_int64(query, 0));

		/* No JSON to fill */
		if(root == NULL)
			goto end;

		/* Finalize request */
		db_finalize(query);

		/* Retry SQL request */
		up = 1;
		goto retry;
	}

	/* Fill JSON object */
	if(root != NULL)
	{
		json_set_string(root, "title", db_column_text(query, 2));
		json_set_string(root, "artist", db_column_text(query, 3));
		json_set_string(root, "album", db_column_text(query, 4));
		json_set_string(root, "cover", db_column_text(query, 5));
	}

end:
	/* Finalize request */
	db_finalize(query);
	if(sql != NULL)
		db_free(sql);

	return 0;
}

struct json *files_list_file(struct db_handle *db, const char *cover_path,
			     const char *path, const char *uri)
{
	struct json *root = NULL;
	struct stat st;
	char *real_path = NULL;
	char *file_path = NULL;
	char *uri_path = NULL;
	char *file;
	int64_t path_id;

	/* Check URI */
	if(uri == NULL || *uri == '\0')
		return NULL;

	/* Get file name from URI */
	file = strrchr(uri, '/');
	if(file != NULL)
	{
		file++;
		uri_path = strndup(uri, file-uri-1);
	}
	else
		file = (char*) uri;

	/* Get complete file path */
	asprintf(&real_path, "%s/%s", path, uri_path != NULL ? uri_path : "");
	if(real_path == NULL)
		goto end;

	/* Get file stat */
	asprintf(&file_path, "%s/%s", path, uri);
	if(file_path == NULL)
		goto end;
	if(stat(file_path, &st) != 0)
		goto end;

	/* Create JSON object */
	root = json_new();
	if(root == NULL)
		goto end;

	/* Get path_id */
	if(files_list_get_path(db, uri_path, &path_id, 0) != 0)
		goto end;

	/* Fill JSON object */
	json_set_string(root, "file", file);

	/* Add meta to JSON object */
	files_list_add_meta(db, root, real_path, file, path_id, st.st_mtime, 1,
			    cover_path);

end:
	if(uri_path != NULL)
		free(uri_path);
	if(real_path != NULL)
		free(real_path);
	if(file_path != NULL)
		free(file_path);

	return root;
}

static int files_list_filter(const struct dirent *d, const struct stat *s)
{
	if(s->st_mode & S_IFREG)
	{
		/* Check file ext */
		return files_ext_check(d->d_name);
	}

	return 1;
}

char *files_list_files(struct db_handle *db, const char *path, const char *uri,
		       unsigned long page, unsigned long count,
		       const char *sort)
{
	struct _dirent **list_dir = NULL;
	struct json *root, *tmp;
	char *str = NULL;
	char *real_path;
	int64_t path_id;
	int list_count;
	unsigned long offset = 0;
	time_t mtime;
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
			/* Create a new JSON object */
			tmp = json_new();
			if(tmp == NULL)
				goto next;

			/* Add file name */
			json_set_string(tmp, "file", list_dir[i]->name);

			/* Add meta to JSON if available */
			files_list_add_meta(db, tmp, real_path,
					    list_dir[i]->name, path_id,
					    list_dir[i]->mtime, 0, NULL);

			/* Add to array */
			if(json_array_add(root, tmp) != 0)
				json_free(tmp);

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

	/* Free JSON object */
	json_free(root);

	/* Free path */
	free(real_path);

	return str;
}

int files_list_scan(struct db_handle *db, const char *cover_path,
		    const char *path, int len, int recursive)
{
	struct dirent *dir = NULL;
	int64_t path_id;
	int64_t mtime;
	struct stat s;
	char *r_path;
	DIR *dp;

	/* Get path length */
	if(len == 0)
		len = strlen(path);

	/* Get path id and time in database */
	if(files_list_get_path(db, path+len, &path_id, &mtime) != 0)
		return -1;

	/* Open directory */
	dp = opendir(path);
	if(dp == NULL)
		return -1;

	/* Parse all entries */
	while((dir = readdir(dp)) != NULL)
	{
		/* Skip . and .. */
		if(dir->d_name[0] == '.' && (dir->d_name[1] == '\0' ||
		   (dir->d_name[1] == '.' && dir->d_name[2] == '\0')))
			continue;

		/* Generate item path */
		asprintf(&r_path, "%s/%s", path, dir->d_name);
		if(r_path == NULL)
			continue;

		/* Get entry stat */
		if(stat(r_path, &s) != 0)
		{
			free(r_path);
			continue;
		}

		/* Process entry */
		if(s.st_mode & S_IFDIR && recursive)
		{
			/* Skip links */
			if(lstat(r_path, &s) == 0 && s.st_mode & S_IFLNK)
			{
				free(r_path);
				continue;
			}

			/* Scan sub_folder */
			files_list_scan(db, cover_path, r_path, len, recursive);
		}
		else if(s.st_mode & S_IFREG && files_ext_check(dir->d_name))
		{
			/* Scan file */
			files_list_add_meta(db, NULL, path, dir->d_name,
					    path_id, s.st_mtime, 1, cover_path);
		}

		/* Free item path */
		free(r_path);
	}

	/* Close directory */
	closedir(dp);

	return 0;
}

