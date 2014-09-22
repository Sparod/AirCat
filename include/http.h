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
	HTTP_MAX_REDIRECT,
	HTTP_EXTRA_HEADER
};

struct http_handle;

/* Set default values for HTTP client. If this function is called at least once,
 * http_free_default_options() must be called at end of program execution.
 */
int http_set_default_option(int option, const char *c_value,
			    unsigned int i_value);
/* Get default values for HTTP client */
int http_get_default_option(int option, char **c_value, unsigned int *i_value);
/* Free default values for HTTP client */
void http_free_default_options(void);

/* Create a new HTTP client */
int http_open(struct http_handle **h, int use_default);

/* Set options for next HTTP connection */
int http_set_option(struct http_handle *h, int option, const char *c_value,
		    unsigned int i_value);

/* Send a GET/HEAD/POST request to an URL.
 * To get respond headers use http_get_header() and to get data use http_read()
 * or http_read_timeout().
 * Return: HTTP respond code.
 */
#define http_get(h, u) http_request(h, u, "GET", NULL, 0)
#define http_head(h, u) http_request(h, u, "HEAD", NULL, 0)
#define http_post(h, u, b, l) http_request(h, u, "POST", b, l)
int http_request(struct http_handle *h, const char *url, const char *method,
		 unsigned char *buffer, unsigned long len);

/* Get an header from HTTP respond */
char *http_get_header(struct http_handle *h, const char *name,
		      int case_sensitive);

/* Read data from HTTP connection */
#define http_read(h, b, s) http_read_timeout(h, b, s, -1)
ssize_t http_read_timeout(struct http_handle *h, unsigned char *buffer,
			  size_t size, long timeout);

/* Close current HTTP connection */
void http_close_connection(struct http_handle *h);

/* Close connection and free handle */
void http_close(struct http_handle *h);

#endif


