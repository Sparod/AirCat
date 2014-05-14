/*
 * http.h - A Tiny HTTP Client
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
#ifndef TINY_HTTP_H
#define TINY_HTTP_H

enum {
	HTTP_USER_AGENT,
	HTTP_PROXY,
	HTTP_PROXY_HOST,
	HTTP_PROXY_PORT,
	HTTP_FOLLOW_REDIRECT,
	HTTP_EXTRA_HEADER
};

struct http_handle;

int http_open(struct http_handle **h);

int http_set_option(struct http_handle *h, int option, char *value);

#define http_get(h, u) http_request(h, u, "GET", NULL, 0)
#define http_head(h, u) http_request(h, u, "HEAD", NULL, 0)
#define http_post(h, u, b, l) http_request(h, u, "POST", b, l)
int http_request(struct http_handle *h, const char *url, const char *method,
		 unsigned char *buffer, unsigned long len);

char *http_get_header(struct http_handle *h, const char *name,
		      int case_sensitive);

#define http_read(h, b, s) http_read_timeout(h, b, s, -1)
int http_read_timeout(struct http_handle *h, unsigned char *buffer, int size,
		      long timeout);

int http_close(struct http_handle *h);

#endif


