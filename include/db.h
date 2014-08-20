/*
 * db.h - A database interface based on SQLite
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

#ifndef _DB_H
#define _DB_H

enum db_type {
	DB_INTEGER,
	DB_FLOAT,
	DB_BLOB,
	DB_TEXT,
	DB_NULL
};

struct db_handle;
struct db_query;

typedef int (*db_cb)(void *user_data, int col_count, char **values,
		     char **names);

int db_open(struct db_handle **handle, const char *path, const char *name);
const char *db_get_name(struct db_handle *h);
void *db_get_db(struct db_handle *h);
int db_attach(struct db_handle *h, const char *file, const char *name);
int db_exec(struct db_handle *h, const char *sql, db_cb callback,
	    void *user_data);
int64_t db_get_last_id(struct db_handle *h);
void db_close(struct db_handle *h);

char *db_mprintf(const char *str, ...);
void db_free(void *ptr);

struct db_query *db_prepare(struct db_handle *h, const char *sql, size_t len);
int db_step(struct db_query *query);
int db_finalize(struct db_query *query);

int db_column_count(struct db_query *query);
const char *db_column_text(struct db_query *query, int i);
char *db_column_copy_text(struct db_query *query, int i);
int db_column_blob(struct db_query *query, int i, const void **blob);
int db_column_int(struct db_query *query, int i);
int64_t db_column_int64(struct db_query *query, int i);
double db_column_double(struct db_query *query, int i);
int db_column_type(struct db_query *query, int i);

#endif

