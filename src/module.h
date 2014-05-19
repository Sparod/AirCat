/*
 * module.h - Module def
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

#ifndef _MODULE_H
#define _MODULE_H

#include <json.h>

#include "config_file.h"
#include "output.h"
#include "avahi.h"

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

#define HTTPD_REQ_INIT {NULL, NULL, 0, NULL, NULL, 0}
struct httpd_req {
	/* URL specific */
	const char *url;
	const char *resource;
	int method;
	/* Uploaded data */
	json_object *json;
	unsigned char *data;
	size_t len;
};

struct url_table {
	const char *url;
	int extended;
	int method;
	int upload;
	int (*process)(void *, struct httpd_req *, unsigned char **, size_t *);
};

struct module_attr {
	struct output_handle *output;
	struct avahi_handle *avahi;
	const struct config *config;
};

struct module {
	/* Module name */
	char *name;
	/* Module handle */
	void *handle;
	/* Module functions */
	int (*open)(void **, struct module_attr *);
	int (*close)(void *);
	int (*set_config)(void *, const struct config *);
	struct config *(*get_config)(void *);
	/* HTTP URL table */
	struct url_table *urls;
};

#endif
