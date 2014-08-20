/*
 * db.c - A database interface based on SQLite
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

#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <pthread.h>

#include <sqlite3.h>

#include "db.h"

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

struct db_handle {
	/* Database name and file path */
	char *file;
	char *name;
	/* Sqlite database handle */
	sqlite3 *db;
};

int db_open(struct db_handle **handle, const char *path, const char *name)
{
	struct db_handle *h;

	/* Needs a name */
	if(name == NULL)
		return -1;

	/* Allocate handle */
	*handle = malloc(sizeof(struct db_handle));
	if(*handle == NULL)
		return -1;
	h = *handle;

	/* Init handle */
	h->name = strdup(name);
	h->file = NULL;
	h->db = NULL;

	/* Generate complete file path */
	if(asprintf(&h->file, "%s/%s.db", path == NULL ? "." : path, h->name)
	    < 0)
		return -1;

	/* Check if file exists */
	if(access(h->file, R_OK | W_OK) != 0)
		return 0;

	/* Open database */
	return sqlite3_open(h->file, &h->db);
}

const char *db_get_name(struct db_handle *h)
{
	if(h == NULL || h->name == NULL)
		return NULL;

	return h->name;
}

void *db_get_db(struct db_handle *h)
{
	if(h == NULL || h->file == NULL)
		return NULL;

	/* Open database */
	if(h->db == NULL && sqlite3_open(h->file, &h->db) != 0)
		return NULL;

	return h->db;
}

int db_attach(struct db_handle *h, const char *file, const char *name)
{
	char *sql;
	int ret;

	/* Create SQL */
	if(asprintf(&sql, "ATTACH DATABASE %s AS %s", file, name) < 0)
		return -1;

	/* Process */
	ret = db_exec(h, sql, NULL, NULL);

	/* Free SQL */
	free(sql);

	return ret;
}

int db_exec(struct db_handle *h, const char *sql, db_cb callback,
	    void *user_data)
{
	char *error = NULL;
	int ret;

	if(h == NULL || h->file == NULL || sql == NULL)
		return -1;

	/* Open database */
	if(h->db == NULL && sqlite3_open(h->file, &h->db) != 0)
		return -1;

	/* Process */
	ret = sqlite3_exec(h->db, sql, callback, user_data, &error);

	/* Display error */
	if(error != NULL)
	{
		fprintf(stderr, "[db] error with %s: %s\n", h->name, error);
		sqlite3_free(error);
	}

	return ret;
}

int64_t db_get_last_id(struct db_handle *h)
{
	return sqlite3_last_insert_rowid(h->db);
}

void db_close(struct db_handle *h)
{
	if(h == NULL)
		return;

	/* Close database */
	if(h->db != NULL)
		sqlite3_close(h->db);

	/* Free name and file path */
	if(h->file != NULL)
		free(h->file);
	if(h->name != NULL)
		free(h->name);

	/* Free structure */
	free(h);
}

char *db_mprintf(const char *str, ...)
{
	char *result;
	va_list vl;

	va_start(vl, str);
	result = sqlite3_vmprintf(str, vl);
	va_end(vl);

	return result;
}

void db_free(void *ptr)
{
	sqlite3_free(ptr);
}

struct db_query *db_prepare(struct db_handle *h, const char *sql, size_t len)
{
	sqlite3_stmt *stmt;

	if(h == NULL || h->file == NULL || sql == NULL)
		return NULL;

	/* Open database */
	if(h->db == NULL && sqlite3_open(h->file, &h->db) != 0)
		return NULL;

	if(sqlite3_prepare_v2(h->db, sql, len, &stmt, NULL) < 0)
		return NULL;

	return (struct db_query *) stmt;
}

int db_step(struct db_query *query)
{
	if(query == NULL)
		return -1;

	if(sqlite3_step((sqlite3_stmt *) query) != SQLITE_ROW)
		return -1;

	return 0;
}

int db_finalize(struct db_query *query)
{
	if(query == NULL)
		return -1;

	return sqlite3_finalize((sqlite3_stmt *) query);
}

int db_column_count(struct db_query *query)
{
	if(query == NULL)
		return -1;

	return sqlite3_column_count((sqlite3_stmt *) query);
}

const char *db_column_text(struct db_query *query, int i)
{
	if(query == NULL)
		return NULL;

	return (const char *) sqlite3_column_text((sqlite3_stmt *) query, i);
}

char *db_column_copy_text(struct db_query *query, int i)
{
	const char *str;

	if(query == NULL)
		return NULL;

	/* Get string and copy */
	str = (const char *) sqlite3_column_text((sqlite3_stmt *) query, i);
	if(str != NULL)
		return strdup(str);

	return NULL;
}

int db_column_blob(struct db_query *query, int i, const void **blob)
{
	if(query == NULL || blob == NULL)
		return -1;

	/* Get blob */
	*blob = sqlite3_column_blob((sqlite3_stmt *) query, i);

	/* Return byte count for blob */
	return sqlite3_column_bytes((sqlite3_stmt *) query, i);;
}

int db_column_int(struct db_query *query, int i)
{
	if(query == NULL)
		return -1;

	return sqlite3_column_int((sqlite3_stmt *) query, i);
}

int64_t db_column_int64(struct db_query *query, int i)
{
	if(query == NULL)
		return -1;

	return (int64_t) sqlite3_column_int64((sqlite3_stmt *) query, i);
}

double db_column_double(struct db_query *query, int i)
{
	if(query == NULL)
		return -1;

	return sqlite3_column_double((sqlite3_stmt *) query, i);
}

int db_column_type(struct db_query *query, int i)
{
	int type;

	if(query == NULL)
		return -1;

	/* Find type */
	switch(sqlite3_column_type((sqlite3_stmt *) query, i))
	{
		case SQLITE_INTEGER:
			type = DB_INTEGER;
			break;
		case SQLITE_FLOAT:
			type = DB_FLOAT;
			break;
		case SQLITE_BLOB:
			type = DB_BLOB;
			break;
		case SQLITE_TEXT:
			type = DB_TEXT;
			break;
		case SQLITE_NULL:
		default:
			type = DB_NULL;
	}

	return type;
}

