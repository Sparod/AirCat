/*
 * http.h - A Tiny HTTP Client
 * 
 * Copyright (c) 2013   A. Dilly
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

enum {HTTP_USER_AGENT, HTTP_PROXY, HTTP_PROXY_HOST, HTTP_PROXY_PORT, HTTP_FOLLOW_REDIRECT, HTTP_EXTRA_HEADER};
struct http_handle;

struct http_handle *http_init();

int http_set_option(struct http_handle *h, int option, char *value);

int http_get(struct http_handle *h, const char *url);
int http_head(struct http_handle *h, const char *url);
int http_post(struct http_handle *h, const char *url, unsigned char *buffer, int length);

char *http_get_header(struct http_handle *h, const char *name, int case_sensitive);

int http_read(struct http_handle *h, unsigned char *buffer, int size);

int http_close(struct http_handle *h);

#endif


