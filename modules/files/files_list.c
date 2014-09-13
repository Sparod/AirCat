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
#include <pthread.h>

#include "file_format.h"
#include "files_list.h"
#include "utils.h"
#include "json.h"

#define FILES_LIST_DEFAULT_COUNT 25

static pthread_mutex_t scan_mutex =  PTHREAD_MUTEX_INITIALIZER;
static char *scan_status = NULL;
static int scan_len = 0;
static int scanning = 0;

static char *files_ext[] = {
	".mp3", ".m4a", ".mp4",	".aac",	".ogg",	".wav",
	NULL
};

static int files_list_recursive_scan(struct db_handle *db,
				     const char *cover_path, const char *path,
				     int len, int recursive, int update_status);

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
			 " path_id INTEGER PRIMARY KEY,"
			 " path TEXT,"
			 " mtime INTEGER,"
			 " UNIQUE (path)"
			 ");"
			 "CREATE TABLE IF NOT EXISTS artist ("
			 " artist_id INTEGER PRIMARY KEY,"
			 " artist TEXT,"
			 " UNIQUE (artist)"
			 ");"
			 "CREATE TABLE IF NOT EXISTS cover ("
			 " cover_id INTEGER PRIMARY KEY,"
			 " cover TEXT,"
			 " UNIQUE (cover)"
			 ");"
			 "CREATE TABLE IF NOT EXISTS album ("
			 " album_id INTEGER PRIMARY KEY,"
			 " album TEXT,"
			 " tracks INTEGER,"
			 " cover_id INTEGER,"
			 " FOREIGN KEY (cover_id) REFERENCES cover,"
			 " UNIQUE (album)"
			 ");"
			 "CREATE TABLE IF NOT EXISTS artist_album ("
			 " artist_id INTEGER,"
			 " album_id INTEGER,"
			 " FOREIGN KEY (artist_id) REFERENCES artist,"
			 " FOREIGN KEY (album_id) REFERENCES album,"
			 " UNIQUE (artist_id,album_id)"
			 ");"
			 "CREATE TABLE IF NOT EXISTS genre ("
			 " genre_id INTEGER PRIMARY KEY,"
			 " genre TEXT,"
			 " UNIQUE (genre)"
			 ");"
			 "CREATE TABLE IF NOT EXISTS song ("
			 " id INTEGER PRIMARY KEY,"
			 " file TEXT,"
			 " path_id INTEGER,"
			 " title TEXT,"
			 " artist_id INTEGER,"
			 " album_id INTEGER,"
			 " comment TEXT,"
			 " genre_id INTEGER,"
			 " track INTEGER,"
			 " year INTEGER,"
			 " duration INTEGER,"
			 " bitrate INTEGER,"
			 " samplerate INTEGER,"
			 " channels INTEGER,"
			 " copyright TEXT,"
			 " encoded TEXT,"
			 " language TEXT,"
			 " publisher TEXT,"
			 " cover_id INTEGER,"
			 " mtime INTEGER,"
			 " FOREIGN KEY (path_id) REFERENCES path,"
			 " FOREIGN KEY (artist_id) REFERENCES artist,"
			 " FOREIGN KEY (album_id) REFERENCES album,"
			 " FOREIGN KEY (genre_id) REFERENCES genre,"
			 " FOREIGN KEY (cover_id) REFERENCES cover"
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
	sql = db_mprintf("SELECT path_id,mtime FROM path WHERE path='%q'",
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

#define FILES_SQL_INSERT "INSERT INTO song (file,title,artist_id,album_id," \
			 "comment,genre_id,track,year,duration,bitrate," \
			 "samplerate,channels,copyright,encoded,language," \
			 "publisher,cover_id,path_id,mtime) " \
			 "VALUES ('%q','%q','%ld','%ld','%q','%ld','%ld'," \
			 "'%d','%ld','%d','%ld','%d','%q','%q','%q','%q'," \
			 "'%ld','%ld', '%ld')"
#define FILES_SQL_UPDATE "UPDATE song " \
			 "SET file='%q',title='%q',artist_id='%ld'," \
			 "album_id='%ld',comment='%q',genre_id='%ld'," \
			 "track='%ld',year='%d',duration='%ld',bitrate='%d'," \
			 "samplerate='%ld',channels='%d',copyright='%q'," \
			 "encoded='%q',language='%q',publisher='%q'," \
			 "cover='%ld',path_id='%ld',mtime='%ld'" \
			 "WHERE id='%ld'"

static int64_t files_list_update_sub_table(struct db_handle *db, const char *insert,
				       const char *select)
{
	struct db_query *query;
	int64_t id;

	/* Insert SQL */
	if(db_exec(db, insert, NULL, NULL) != 0)
		return -1;

	/* Prepare select request */
	query = db_prepare(db, select, -1);
	if(query == NULL)
		return -1;

	/* Do select */
	if(db_step(query) != 0)
	{
		db_finalize(query);
		return -1;
	}

	/* Get id */
	id = db_column_int64(query, 0);

	/* Free SQL */
	db_finalize(query);

	return id;
}

static int files_list_update_file(struct db_handle *db, const char *cover_path,
				  const char *path, const char *file,
				  int64_t mtime, int64_t path_id, int64_t id)
{
	struct file_format *format = NULL;
	int64_t artist_id = 0;
	int64_t album_id = 0;
	int64_t cover_id = 0;
	int64_t genre_id = 0;
	char *cover = NULL;
	char *file_path;
	char *in_sql = NULL;
	char *se_sql = NULL;
	char *str = NULL;
	int ret = -1;

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

	/* Add artist and album to database */
	if(format != NULL)
	{
		/* Process artist */
		if(format->artist != NULL)
		{
			/* Create insert SQL */
			in_sql = db_mprintf("INSERT OR IGNORE INTO artist "
					    "(artist) VALUES ('%q')",
					    format->artist);
			if(in_sql == NULL)
				goto end;

			/* Create select SQL */
			se_sql = db_mprintf("SELECT artist_id FROM artist "
					    "WHERE artist='%q'",
					    format->artist);
			if(se_sql == NULL)
			{
				db_free(in_sql);
				goto end;
			}

			artist_id = files_list_update_sub_table(db, in_sql,
								se_sql);
			db_free(in_sql);
			db_free(se_sql);
			if(artist_id < 0)
				goto end;
		}

		/* Process cover */
		if(cover != NULL)
		{
			/* Create insert SQL */
			in_sql = db_mprintf("INSERT OR IGNORE INTO cover "
					    "(cover) VALUES ('%q')", cover);
			if(in_sql == NULL)
				goto end;

			/* Create select SQL */
			se_sql = db_mprintf("SELECT cover_id FROM cover "
					    "WHERE cover='%q'", cover);
			if(se_sql == NULL)
			{
				db_free(in_sql);
				goto end;
			}

			cover_id = files_list_update_sub_table(db, in_sql,
								se_sql);
			db_free(in_sql);
			db_free(se_sql);
			if(cover_id < 0)
				goto end;
		}

		/* Process album */
		if(format->album != NULL)
		{
			/* Create insert SQL */
			in_sql = db_mprintf("INSERT OR IGNORE INTO album "
					    "(album,tracks,cover_id) "
					    "VALUES ('%q','%ld','%ld')",
					    format->album, format->total_track,
					    cover_id);
			if(in_sql == NULL)
				goto end;

			/* Create select SQL */
			se_sql = db_mprintf("SELECT album_id FROM album "
					    "WHERE album='%q'", format->album);
			if(se_sql == NULL)
			{
				db_free(in_sql);
				goto end;
			}

			album_id = files_list_update_sub_table(db, in_sql,
								se_sql);
			db_free(in_sql);
			db_free(se_sql);
			if(album_id < 0)
				goto end;
		}

		/* Process genre */
		if(format->genre != NULL)
		{
			/* Create insert SQL */
			in_sql = db_mprintf("INSERT OR IGNORE INTO genre "
					    "(genre) VALUES ('%q')",
					    format->genre);
			if(in_sql == NULL)
				goto end;

			/* Create select SQL */
			se_sql = db_mprintf("SELECT genre_id FROM genre "
					    "WHERE genre='%q'", format->genre);
			if(se_sql == NULL)
			{
				db_free(in_sql);
				goto end;
			}

			genre_id = files_list_update_sub_table(db, in_sql,
								se_sql);
			db_free(in_sql);
			db_free(se_sql);
			if(genre_id < 0)
				goto end;
		}

		/* Update artist <-> album */
		if(artist_id != 0 && album_id != 0)
		{
			/* Create new SQL */
			str = db_mprintf("INSERT OR IGNORE INTO artist_album "
					 "(artist_id,album_id) "
					 "VALUES ('%ld','%ld')",
					 artist_id, album_id);
			if(str == NULL)
				goto end;

			/* Get album id from database */
			if(db_exec(db, str, NULL, NULL) != 0)
				goto end;

			/* Free SQL */
			db_free(str);
		}
	}

#define FMT_STR(n) format != NULL && format->n != NULL ? format->n : ""
#define FMT_INT(n) format != NULL ? format->n : (long) 0

	/* Prepare SQL request */
	str = db_mprintf(id > 0 ? FILES_SQL_UPDATE : FILES_SQL_INSERT,
			 file, FMT_STR(title), artist_id, album_id,
			 FMT_STR(comment), genre_id, FMT_INT(track), FMT_INT(year),
			 FMT_INT(length), FMT_INT(bitrate),
			 FMT_INT(samplerate), FMT_INT(channels),
			 FMT_STR(copyright), FMT_STR(encoded),
			 FMT_STR(language), FMT_STR(publisher),
			 cover_id, path_id, mtime, id);

#undef FMT_INT
#undef FMT_STR

	/* Add file to database */
	ret = db_exec(db, str, NULL, NULL);

end:
	/* Free SQL request */
	if(str != NULL)
		db_free(str);

	/* Free format */
	if(format != NULL)
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
	sql = db_mprintf("SELECT id,mtime,title,artist,album,cover,genre,"
			 "artist_id,album_id,genre_id "
			 "FROM song "
			 "LEFT JOIN artist USING (artist_id) "
			 "LEFT JOIN album USING (album_id) "
			 "LEFT JOIN cover USING (cover_id) "
			 "LEFT JOIN genre USING (genre_id) "
			 "WHERE file='%q' AND path_id='%ld'",
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
		json_set_string(root, "genre", db_column_text(query, 6));

		/* Add IDs */
		json_set_int64(root, "artist_id", db_column_int64(query, 7));
		json_set_int64(root, "album_id", db_column_int64(query, 8));
		json_set_int64(root, "genre_id", db_column_int64(query, 9));
	}

end:
	/* Finalize request */
	db_finalize(query);
	if(sql != NULL)
		db_free(sql);

	return 0;
}

static int files_list_add_file(void *user_data, int col_count, char **values,
			       char **names)
{
	struct json *list = user_data;
	struct json *tmp;

	/* Create JSON object */
	tmp = json_new();
	if(tmp == NULL)
		return -1;

	/* Add values to it */
	json_set_string(tmp, "file", values[0]);
	json_set_string(tmp, "title", values[1]);
	json_set_string(tmp, "artist", values[2]);
	json_set_string(tmp, "album", values[3]);
	json_set_string(tmp, "cover", values[4]);
	json_set_string(tmp, "genre", values[5]);

	/* Add IDs */
	json_set_int64(tmp, "artist_id", strtoul(values[6], NULL, 10));
	json_set_int64(tmp, "album_id", strtoul(values[7], NULL, 10));
	json_set_int64(tmp, "genre_id", strtoul(values[8], NULL, 10));

	/* Add object to array */
	if(json_array_add(list, tmp) != 0)
		json_free(tmp);

	return 0;
}

static int files_list_add_album(void *user_data, int col_count, char **values,
			        char **names)
{
	struct json *list = user_data;
	struct json *tmp;

	/* Create JSON object */
	tmp = json_new();
	if(tmp == NULL)
		return -1;

	/* Add values to it */
	json_set_string(tmp, "album", values[0]);
	json_set_int64(tmp, "album_id", strtoul(values[1], NULL, 10));
	json_set_string(tmp, "cover", values[2]);

	/* Add object to array */
	if(json_array_add(list, tmp) != 0)
		json_free(tmp);

	return 0;
}

static int files_list_add_artist(void *user_data, int col_count, char **values,
			         char **names)
{
	struct json *list = user_data;
	struct json *tmp;

	/* Create JSON object */
	tmp = json_new();
	if(tmp == NULL)
		return -1;

	/* Add values to it */
	json_set_string(tmp, "artist", values[0]);
	json_set_int64(tmp, "artist_id", strtoul(values[1], NULL, 10));

	/* Add object to array */
	if(json_array_add(list, tmp) != 0)
		json_free(tmp);

	return 0;
}

static int files_list_add_genre(void *user_data, int col_count, char **values,
			        char **names)
{
	struct json *list = user_data;
	struct json *tmp;

	/* Create JSON object */
	tmp = json_new();
	if(tmp == NULL)
		return -1;

	/* Add values to it */
	json_set_string(tmp, "genre", values[0]);
	json_set_int64(tmp, "genre_id", strtoul(values[1], NULL, 10));

	/* Add object to array */
	if(json_array_add(list, tmp) != 0)
		json_free(tmp);

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
	/* Check file ext */
	if(s->st_mode & S_IFREG)
		return files_ext_check(d->d_name);

	return 1;
}

static int files_list_filter_dir(const struct dirent *d, const struct stat *s)
{
	/* Check file ext */
	if(s->st_mode & S_IFDIR)
		return 1;

	return 0;
}

char *files_list_files(struct db_handle *db, const char *cover_path,
		       const char *path, const char *uri, unsigned long page,
		       unsigned long count, enum files_list_sort sort,
		       enum files_list_display display, uint64_t artist_id,
		       uint64_t album_id, uint64_t genre_id)
{
	int (*_sort)(const struct _dirent **, const struct _dirent **);
	int (*_filter)(const struct dirent *, const struct stat *) =
							      files_list_filter;
	struct _dirent **list_dir = NULL;
	struct json *root, *tmp;
	char *real_path = NULL;
	char *str = NULL;
	char *tag_sort;
	int64_t path_id = 0;
	int list_count = 0;
	int only_dir = 0;
	unsigned long offset = 0;
	time_t mtime;
	int i;

	/* Create new JSON array */
	root = json_new_array();
	if(root == NULL)
		return NULL;

	/* Set default values */
	if(page == 0)
		page = 1;
	if(count == 0)
		count = FILES_LIST_DEFAULT_COUNT;
	offset = (page-1) * count;

	/* Skip directory scan */
	if(path == NULL)
	{
		if(sort < FILES_LIST_SORT_TITLE)
			sort = FILES_LIST_SORT_TITLE;
		only_dir = 1;
		goto do_sql;
	}

	/* Generate path from URI */
	asprintf(&real_path, "%s/%s", path, uri != NULL ? uri : "");
	if(real_path == NULL)
		goto end;

	/* Get path id and time in database */
	if(files_list_get_path(db, uri, &path_id, &mtime) != 0)
		goto end;

	/* Select sort algorithm */
	if(sort >= FILES_LIST_SORT_TITLE)
	{
		_sort = _alphasort;
		only_dir = 1;
	}
	else if(sort == FILES_LIST_SORT_REVERSE)
		_sort = _alphasort_last;
	else if(sort == FILES_LIST_SORT_ALPHA)
		_sort = _alphasort;
	else if(sort == FILES_LIST_SORT_ALPHA_REVERSE)
		_sort = _alphasort_reverse;
	else
		_sort = _alphasort_first;

	/* Scan entire folder for tag sort */
	if(only_dir || display != FILES_LIST_DISPLAY_DEFAULT ||
	   artist_id > 0 || album_id > 0 || genre_id > 0)
	{
		/* Set only dir */
		only_dir = 1;

		/* Reverse sort */
		if(sort >= FILES_LIST_SORT_TITLE_REVERSE)
			_sort = _alphasort_reverse;

		/* Set filter to only folder */
		_filter = files_list_filter_dir;

		/* Scan all directory not recursively */
		files_list_recursive_scan(db, cover_path, real_path,
					  strlen(path), 0, 0);
	}

	/* Scan folder in alphabetic order */
	list_count = _scandir(real_path, &list_dir, _filter, _sort);
	if(list_count < 0)
		goto end;

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
		else if(!only_dir && list_dir[i]->mode & S_IFREG)
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

do_sql:
	/* No more files to retrieve */
	if(only_dir == 0 || count == 0)
		goto end;

	/* Sort by tag */
	if(display == FILES_LIST_DISPLAY_DEFAULT)
	{
		/* Select correct tag sort */
		switch(sort)
		{
			case FILES_LIST_SORT_TITLE:
			case FILES_LIST_SORT_TITLE_REVERSE:
				tag_sort = "title";
				break;
			case FILES_LIST_SORT_ALBUM:
			case FILES_LIST_SORT_ALBUM_REVERSE:
				tag_sort = "album";
				break;
			case FILES_LIST_SORT_ARTIST:
			case FILES_LIST_SORT_ARTIST_REVERSE:
				tag_sort = "artist";
				break;
			case FILES_LIST_SORT_TRACK:
			case FILES_LIST_SORT_TRACK_REVERSE:
				tag_sort = "track";
				break;
			case FILES_LIST_SORT_YEAR:
			case FILES_LIST_SORT_YEAR_REVERSE:
				tag_sort = "year";
				break;
			case FILES_LIST_SORT_DURATION:
			case FILES_LIST_SORT_DURATION_REVERSE:
				tag_sort = "duration";
				break;
			default:
				tag_sort = "file";
		}

		/* Complete request with data from database */
		str = db_mprintf("SELECT file,title,artist,album,cover,genre,"
				 "artist_id,album_id,genre_id "
				 "FROM song "
				 "LEFT JOIN artist USING (artist_id) "
				 "LEFT JOIN album USING (album_id) "
				 "LEFT JOIN cover USING (cover_id) "
				 "LEFT JOIN genre USING (genre_id) "
				 "WHERE 1 \n"
				 "%s path_id='%ld' \n"
				 "%s album_id='%ld' \n"
				 "%s artist_id='%ld' \n"
				 "%s genre_id='%ld' \n"
				 "ORDER BY %s %s LIMIT %ld, %ld",
				 path == NULL ? "--" : "AND", path_id,
				 album_id == 0 ? "--" : "AND", album_id,
				 artist_id == 0 ? "--" : "AND", artist_id,
				 genre_id == 0 ? "--" : "AND", genre_id,
				 tag_sort,
				 sort >= FILES_LIST_SORT_TITLE_REVERSE ?
								 "DESC" : "ASC",
				 offset > list_count ? offset - list_count : 0,
				 count);

		/* Do request */
		if(str != NULL)
		{
			db_exec(db, str, files_list_add_file, root);
			db_free(str);
		}
	}
	else if(display == FILES_LIST_DISPLAY_ALBUM)
	{
		/* Prepare request */
		str = db_mprintf("SELECT album,album_id,cover FROM album "
				 "LEFT JOIN cover USING (cover_id) "
				 "ORDER BY album %s LIMIT %ld, %ld",
				 sort >= FILES_LIST_SORT_TITLE_REVERSE ?
								 "DESC" : "ASC",
				 offset > list_count ? offset - list_count : 0,
				 count);

		/* Do request */
		if(str != NULL)
		{
			db_exec(db, str, files_list_add_album, root);
			db_free(str);
		}
	}
	else if(display == FILES_LIST_DISPLAY_ARTIST)
	{
		/* Prepare request */
		str = db_mprintf("SELECT artist,artist_id FROM artist "
				 "ORDER BY artist %s LIMIT %ld, %ld",
				 sort >= FILES_LIST_SORT_TITLE_REVERSE ?
								 "DESC" : "ASC",
				 offset > list_count ? offset - list_count : 0,
				 count);

		/* Do request */
		if(str != NULL)
		{
			db_exec(db, str, files_list_add_artist, root);
			db_free(str);
		}
	}
	else if(display == FILES_LIST_DISPLAY_GENRE)
	{
		/* Prepare request */
		str = db_mprintf("SELECT genre,genre_id FROM genre "
				 "ORDER BY genre %s LIMIT %ld, %ld",
				 sort >= FILES_LIST_SORT_TITLE_REVERSE ?
								 "DESC" : "ASC",
				 offset > list_count ? offset - list_count : 0,
				 count);

		/* Do request */
		if(str != NULL)
		{
			db_exec(db, str, files_list_add_genre, root);
			db_free(str);
		}
	}

end:
	/* Get string from JSON object */
	str = strdup(json_export(root));

	/* Free JSON object */
	json_free(root);

	/* Free path */
	if(real_path != NULL)
		free(real_path);

	return str;
}

static int files_list_recursive_scan(struct db_handle *db,
				     const char *cover_path, const char *path,
				     int len, int recursive, int update_status)
{
	struct dirent *dir = NULL;
	int64_t path_id;
	int64_t mtime;
	struct stat s;
	char *r_path;
	char *status;
	int s_len;
	DIR *dp;

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

		/* Update status */
		if(update_status)
		{
			/* Lock scan status access */
			pthread_mutex_lock(&scan_mutex);

			/* Update string status */
			s_len = strlen(r_path) - len + 1;
			if(s_len > scan_len)
			{
				status = realloc(scan_status, s_len);
				if(status != NULL)
				{
					scan_len = s_len;
					scan_status = status;
				}
			}
			strncpy(scan_status, r_path + len, scan_len);

			/* Unlock scan status access */
			pthread_mutex_unlock(&scan_mutex);
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
			files_list_recursive_scan(db, cover_path, r_path, len,
						  recursive, update_status);
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

int files_list_scan(struct db_handle *db, const char *cover_path,
		    const char *path, int recursive)
{
	int ret;

	if(path == NULL)
		return -1;

	/* Lock scan access */
	pthread_mutex_lock(&scan_mutex);

	/* Check scanning */
	if(scanning != 0)
	{
		/* Unlock scan access */
		pthread_mutex_unlock(&scan_mutex);
		return 1;
	}
	scanning = 1;

	/* Unlock scan access */
	pthread_mutex_unlock(&scan_mutex);

	/* Scan directory with status */
	ret = files_list_recursive_scan(db, cover_path, path, strlen(path),
					recursive, 1);

	/* Lock scan status access */
	pthread_mutex_lock(&scan_mutex);

	/* Free scan status string */
	if(scan_status != NULL)
		free(scan_status);
	scan_status = NULL;
	scan_len = 0;

	/* Unset scanning flag */
	scanning = 0;

	/* Unlock scan status access */
	pthread_mutex_unlock(&scan_mutex);

	return 0;
}

char *files_list_get_scan(void)
{
	char *status = NULL;

	/* Lock scan status access */
	pthread_mutex_lock(&scan_mutex);

	/* Copy string */
	if(scan_status != NULL)
		status = strdup(scan_status);

	/* Unlock scan status access */
	pthread_mutex_unlock(&scan_mutex);

	return status;
}

int files_list_is_scanning(void)
{
	int status;

	/* Lock scan access */
	pthread_mutex_lock(&scan_mutex);

	/* Copy status */
	status = scanning;

	/* Unlock scan access */
	pthread_mutex_unlock(&scan_mutex);

	return status;
}

