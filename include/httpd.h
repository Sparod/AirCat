/*
 * httpd.h - An HTTP server
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

#ifndef _HTTP_SERVER_H
#define _HTTP_SERVER_H

#include "json.h"

/* HTTP return code */
#define HTTPD_OK 200
#define HTTPD_BAD_REQUEST 400
#define HTTPD_FORBIDDEN 403
#define HTTPD_NOT_FOUND 404
#define HTTPD_METHOD_NOT_ALLOWED 405
#define HTTPD_METHOD_NOT_ACCEPTABLE 406
#define HTTPD_NO_RESPONSE 444
#define HTTPD_RETRY_WITH 449
#define HTTPD_INTERNAL_SERVER_ERROR 500
#define HTTPD_NOT_IMPLEMENTED 501
#define HTTPD_SERVICE_UNAVAILABLE 503

/* HTTP method accepted */
#define HTTPD_GET 1
#define HTTPD_PUT 2
#define HTTPD_POST 4
#define HTTPD_DELETE 8

/* HTTP extended URL support */
#define HTTPD_STRICT_URL 0
#define HTTPD_EXT_URL 1

/* JSON/RAW uploaded data */
#define HTTPD_RAW 0
#define HTTPD_JSON 1

#define HTTPD_REQ_INIT {NULL, NULL, 0, NULL, NULL, 0, NULL}
struct httpd_req {
	/* URL specific */
	const char *url;
	const char *resource;
	int method;
	/* Uploaded data */
	struct json *json;
	unsigned char *data;
	size_t len;
	/* Private data: do not edit! */
	void *priv_data;
};

struct url_table {
	const char *url;
	int extended;
	int method;
	int upload;
	int (*process)(void *, struct httpd_req *, unsigned char **, size_t *);
};

struct httpd_handle;

/* HTTP basic functions */
int httpd_open(struct httpd_handle **handle, struct json *config);
int httpd_start(struct httpd_handle *h);
int httpd_stop(struct httpd_handle *h);
int httpd_set_config(struct httpd_handle *h, struct json *c);
struct json *httpd_get_config(struct httpd_handle *h);
int httpd_close(struct httpd_handle *h);

/* Add/Remove a URL group */
int httpd_add_urls(struct httpd_handle *h, const char *name,
		   struct url_table *urls, void *user_data);
int httpd_remove_urls(struct httpd_handle *h, const char *name);

/* Get stringquery values in URL */
const char *httpd_get_query(struct httpd_req *req, const char *key);

/* Set/Get values in session */
int httpd_set_session_value(struct httpd_req *req, const char *key,
			     const char *value);
char *httpd_get_session_value(struct httpd_req *req, const char *key);

#endif
