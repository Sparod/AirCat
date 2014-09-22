/*
 * http.c - A Tiny HTTP Client
 *
 * Support only HTTP 1.0 with:
 * 	- Basic HTTP Proxy (no auth!)
 * 	- Basic Auth
 *	- Follow redirection
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
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <pthread.h>

#define BUFFER_SIZE 8192
#define MAX_SIZE_HEADER BUFFER_SIZE
#define MAX_SIZE_LINE 512
#define DEFAULT_USER_AGENT "tiny_http 0.1"
#define MAX_FOLLOW 10

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef HAVE_OPENSSL
#include <openssl/ssl.h>
#endif

#include "utils.h"
#include "http.h"

struct http_header {
	char *name;
	char *value;
	struct http_header *next;
};

struct http_handle {
	/* Socket */
	int sock;
	char *hostname;
	unsigned int port;
	/* SSL part */
	int is_ssl;
#ifdef HAVE_OPENSSL
	SSL *ssl;
	SSL_CTX *ssl_ctx;
#endif
	/* Proxy config */
	int proxy_use;
	char *proxy_hostname;
	unsigned int proxy_port;
	/* Follow redirect */
	int follow;
	int i_follow;
	int max_follow;
	/* Request and associated headers */
	char *user_agent;
	char *extra;
	int keep_alive;
	struct http_header *headers;
	/* Thread */
	pthread_t thread;
	pthread_mutex_t mutex;
	int stop;
	int running;
	/* Request for thread */
	http_head_cb head_cb;
	http_read_cb read_cb;
	http_comp_cb comp_cb;
	void *user_data;
	int code;
	char *url;
	char *method;
	unsigned char *buffer;
	unsigned long length;
};

#define FREE_STR(s) if(s != NULL) free(s); s = NULL;

/* Default configuration */
static char *user_agent = NULL;
static int proxy_use = 0;
static char *proxy_hostname = NULL;
static unsigned int proxy_port = 0;
static char *extra = NULL;
static int follow = 0;
static int max_follow = MAX_FOLLOW;
static pthread_mutex_t def_mutex = PTHREAD_MUTEX_INITIALIZER;

int http_set_default_option(int option, const char *c_value,
			    unsigned int i_value)
{
	/* Lock default configuration */
	pthread_mutex_lock(&def_mutex);

	switch(option)
	{
		case HTTP_USER_AGENT:
			FREE_STR(user_agent);
			user_agent = c_value != NULL ? strdup(c_value) : NULL;
			break;
		case HTTP_PROXY:
			proxy_use = i_value;
			break;
		case HTTP_PROXY_HOST:
			FREE_STR(proxy_hostname);
			proxy_hostname = c_value != NULL ? strdup(c_value) :
							   NULL;
			break;
		case HTTP_PROXY_PORT:
			proxy_port = i_value;
			break;
		case HTTP_EXTRA_HEADER:
			FREE_STR(extra);
			extra = c_value != NULL ? strdup(c_value) : NULL;
			break;
		case HTTP_FOLLOW_REDIRECT:
			follow = i_value;
			break;
		case HTTP_MAX_REDIRECT:
			max_follow = i_value;
			if(max_follow <= 0)
				max_follow = 1;
			break;
	}

	/* Unlock default configuration */
	pthread_mutex_unlock(&def_mutex);

	return 0;
}

int http_get_default_option(int option, char **c_value, unsigned int *i_value)
{
	/* Lock default configuration */
	pthread_mutex_lock(&def_mutex);

	switch(option)
	{
		case HTTP_USER_AGENT:
			*c_value = user_agent != NULL ?
						      strdup(user_agent) : NULL;
			break;
		case HTTP_PROXY:
			*i_value = proxy_use;
			break;
		case HTTP_PROXY_HOST:
			*c_value = proxy_hostname != NULL ?
						  strdup(proxy_hostname) : NULL;
			break;
		case HTTP_PROXY_PORT:
			*i_value = proxy_port;
			break;
		case HTTP_EXTRA_HEADER:
			*c_value = extra != NULL ? strdup(extra) : NULL;
			break;
		case HTTP_FOLLOW_REDIRECT:
			*i_value = follow;
			break;
		case HTTP_MAX_REDIRECT:
			*i_value = max_follow;
			break;
	}

	/* Unlock default configuration */
	pthread_mutex_unlock(&def_mutex);

	return 0;
}

void http_free_default_options(void)
{
	/* Lock default configuration */
	pthread_mutex_lock(&def_mutex);

	/* Free strings */
	FREE_STR(user_agent);
	FREE_STR(proxy_hostname);
	FREE_STR(extra);

	/* Unlock default configuration */
	pthread_mutex_unlock(&def_mutex);
}

int http_open(struct http_handle **handle, int use_default)
{
	struct http_handle *h;

	/* Alloc structure */
	*handle = malloc(sizeof(struct http_handle));
	if(*handle == NULL)
		return -1;
	h = *handle;

	/* Init structure */
	memset(h, 0, sizeof(struct http_handle));
	h->sock = -1;
	h->max_follow = MAX_FOLLOW;

	/* Use default configuration */
	if(use_default)
	{
		/* Lock default configuration */
		pthread_mutex_lock(&def_mutex);

		/* Copy configuration */
		h->user_agent = user_agent != NULL ? strdup(user_agent) : NULL;
		h->proxy_use = proxy_use;
		h->proxy_hostname = proxy_hostname != NULL ?
						  strdup(proxy_hostname) : NULL;
		h->proxy_port = proxy_port;
		h->extra = extra != NULL ? strdup(extra) : NULL;
		h->follow = follow;
		h->max_follow = max_follow;

		/* Unlock default configuration */
		pthread_mutex_unlock(&def_mutex);
	}

	/* Init mutex */
	pthread_mutex_init(&h->mutex, NULL);

	return 0;
}

int http_set_option(struct http_handle *h, int option, const char *c_value,
		    unsigned int i_value)
{
	switch (option)
	{
		case HTTP_USER_AGENT:
			FREE_STR(h->user_agent);
			h->user_agent = c_value != NULL ? strdup(c_value) :
							  NULL;
			break;
		case HTTP_PROXY:
			h->proxy_use = i_value;
			break;
		case HTTP_PROXY_HOST:
			FREE_STR(h->proxy_hostname);
			h->proxy_hostname = c_value != NULL ? strdup(c_value) :
							      NULL;
			break;
		case HTTP_PROXY_PORT:
			h->proxy_port = i_value;
			break;
		case HTTP_EXTRA_HEADER:
			FREE_STR(h->extra);
			h->extra = c_value != NULL ? strdup(c_value) : NULL;
			break;
		case HTTP_FOLLOW_REDIRECT:
			h->follow = i_value;
			break;
		case HTTP_MAX_REDIRECT:
			h->max_follow = i_value;
			if(h->max_follow <= 0)
				h->max_follow = 1;
			break;
		default:
			return -1;
	}

	return 0;
}

static int http_connect(struct http_handle *h, char *hostname,
			unsigned int port)
{
	struct sockaddr_in server_addr;
	struct hostent *server_ip;

	/* Check if a socket is already opened */
	if(h->sock >= 0)
	{
		/* Check keep_alive and if hostname/port have changed */
		if(h->keep_alive && strcmp(h->hostname, hostname) == 0 &&
		   h->port == port)
			return 0;

		/* Close previous socket */
		close(h->sock);
	}

	/* Reset hostname and port */
	FREE_STR(h->hostname);
	h->port = 0;

	/* Open socket to HTTP server */
	h->sock = socket(AF_INET, SOCK_STREAM, 0);
	if(h->sock < 0)
		return -1;

	/* Get ip address from hostname */
	server_ip = gethostbyname(h->proxy_use ? h->proxy_hostname : hostname);
	if (server_ip == NULL)
		return -1;

	/* Set shoutcast server address */
	memset(&server_addr, 0, sizeof(server_addr));
	server_addr.sin_family = AF_INET;
	server_addr.sin_port = htons(h->proxy_use ? h->proxy_port : port);
	memcpy(&server_addr.sin_addr.s_addr, server_ip->h_addr,
	       server_ip->h_length);

	/* Connect to HTTP server */
	if(connect(h->sock, (struct sockaddr *)&server_addr,
		   sizeof(server_addr)) < 0)
		return -1;

#ifdef HAVE_OPENSSL
	/* Create connection if HTTPS */
	if(h->is_ssl)
	{
		/* Init library */
		SSL_library_init();

		/* Create openssl context */
		h->ssl_ctx = SSL_CTX_new(SSLv23_client_method());
		if(h->ssl_ctx == NULL)
			return -1;

		/* Create a new SSL object */
		h->ssl = SSL_new(h->ssl_ctx);
		if(h->ssl == NULL)
			return -1;

		/* Attach socket to SSL */
		if(SSL_set_fd(h->ssl, h->sock) != 1)
			return -1;

		/* Initiate the TLS/SSL handshake with server */
		if(SSL_connect(h->ssl) != 1)
			return -1;
	}
#endif

	/* Set hostname and port */
	h->hostname = strdup(hostname);
	h->port = port;

	return 0;
}

static void http_free_header(struct http_handle *h)
{
	struct http_header *temp;

	while(h->headers != NULL)
	{
		temp = h->headers;
		h->headers = h->headers->next;

		FREE_STR(temp->name);
		FREE_STR(temp->value);

		free(temp);
	}
}

static int http_read_line(struct http_handle *h, char *buffer, int length)
{
	int ret;
	int i;

	for(i = 0; i < length-1; i++)
	{
#ifdef HAVE_OPENSSL
		if(h->is_ssl)
			ret = SSL_read(h->ssl, &buffer[i], 1);
		else
#endif
			ret = read(h->sock, &buffer[i], 1);

		if(ret != 1)
			return i;

		if(buffer[i] == '\n')
			break;
	}

	buffer[i+1] = 0;
	return i+1;
}

static int http_parse_header(struct http_handle *h)
{
	struct http_header *header;
	char buffer[MAX_SIZE_LINE];
	int status_code = 0;
	char *temp, *end;
	int size = 0;

	/* Free previous header */
	http_free_header(h);

	/* Read first line */
	size = http_read_line(h, buffer, MAX_SIZE_LINE);
	if(size > 100) //Too long for a first line in HTTP protocol
		return -1;

	/* Verify protocol */
	if(strncmp(buffer, "HTTP/1.", 7) != 0 &&
	   strncmp(buffer, "ICY", 3) != 0) // HACK for Shoutcast !
		return -1;

	/* Get status code */
	if(sscanf(buffer, "%*s %d %*s", &status_code) != 1)
		return -1;

	while(http_read_line(h, buffer, MAX_SIZE_HEADER) != 0)
	{
		/* End of header */
		if(buffer[0] == '\r' && buffer[1] == '\n')
			break;

		/* Get name */
		temp = strchr(buffer, ':');
		if(temp == NULL)
			continue;
		*temp = 0;
		temp++;
		/* Skip all whitespace */
		while(*temp != 0 && *temp == ' ')
			temp++;
		/* Find end of string */
		end = temp;
		while(*end != 0 && *end != '\r')
			end++;

		/* Add name and value to list */
		header = malloc(sizeof(struct http_header));
		header->name = strdup(buffer);
		header->value = strndup(temp, end-temp);
		header->next = h->headers;
		h->headers = header;
	}

	return status_code;
}

int http_request(struct http_handle *h, const char *url, const char *method,
		 unsigned char *buffer, unsigned long length)
{
	char req[MAX_SIZE_HEADER];
	int protocol = URL_HTTP;
	char *hostname = NULL;
	char *username = NULL;
	char *password = NULL;
	char *resource = NULL;
	char *location = NULL;
	char *auth = NULL;
	unsigned int port = 0;
	int first_req = 0;
	int code = -1;
	int ret = 0;
	int len;

	/* Parse URL */
	ret = parse_url(url, &protocol, &hostname, &port, &username, &password,
			&resource);
	if(ret < 0 || port == 0)
		goto end;

	/* Check Protocol */
	if(protocol == URL_HTTPS)
	{
		h->is_ssl = 1;
#ifndef HAVE_OPENSSL
		fprintf(stderr, "SSL is not supported!\n");
		goto end;
#endif
	}

	/* Connect to HTTP server */
	if(http_connect(h, hostname, port) != 0)
		goto end;

	/* Generate Auth string */
	if(username != NULL || password != NULL)
	{
		auth = NULL;//"Authorization: Basic %s"
	}

	/* Make HTTP request */
	len = snprintf(req, MAX_SIZE_HEADER,
		       "%s %s%s HTTP/1.0\r\n"
		       "Host: %s\r\n"
		       "User-Agent: %s\r\n"
		       "Connection: %s\r\n"
		       "Content-type: %s\r\n"
		       "Content-Length: %lu\r\n"
		       "%s"
		       "%s"
		       "\r\n",
		       method, h->proxy_use ? "" : "/", h->proxy_use ? url :
					       resource != NULL ? resource : "",
		       hostname,
		       h->user_agent == NULL ? DEFAULT_USER_AGENT :
					       h->user_agent,
		       h->keep_alive ? "keep-alive" : "close",
		       length > 0 ? "application/x-www-form-urlencoded" : "",
		       length,
		       auth != NULL ? auth : "",
		       h->extra != NULL ? h->extra : "");

	/* Send HTTP request */
#ifdef HAVE_OPENSSL
	if(h->is_ssl)
		len = SSL_write(h->ssl, req, len);
	else
#endif
		len = write(h->sock, req, len);
	if(len <= 0)
		goto end;

	/* Send data buffer */
	while(length > 0)
	{
#ifdef HAVE_OPENSSL
		if(h->is_ssl)
			len = SSL_write(h->ssl, buffer, length);
		else
#endif
			len = write(h->sock, buffer, length);
		if(len < 0)
			goto end;
		length -= len;
	}

	/* Parse HTTP header response */
	code = http_parse_header(h);

	/* Follow redirection */
	if(h->follow && (code == 301 || code == 302) &&
	   h->i_follow < h->max_follow)
	{
		/* Increment nb follow */
		if(h->i_follow == 0)
			first_req = 1;
		h->i_follow++;

		/* Get redirection from header */
		location = http_get_header(h, "Location", 0);
		if(location == NULL)
			return code;
		location = strdup(location);

		/* Regenerate URL with username and/or password */
		if(username != NULL || password != NULL)
		{
			/* Allocate a new URL */
			len = username != NULL ? strlen(username) : 0;
			len += password != NULL ? strlen(password) : 0;
			len += 1;
			auth = malloc(len + strlen(location) + 1);
			if(auth == NULL)
			{
				free(location);
				return code;
			}

			/* Copy string */
			strcpy(auth, location);
			url = strstr(location, "//");
			if(url != NULL)
				url += 2;
			else
				url = location;
			memmove((char*)url+len, url, len);
			snprintf((char*)url, len, "%s:%s", username, password);

			/* Copy to location and free previous one */
			free(location);
			location = auth;
		}

		/* Send new request */
		code = http_request(h, location, method, buffer, length);

		/* Free new URL */
		free(location);

		/* Reset follow counter for next request */
		if(first_req)
			h->i_follow = 0;
	}

end:
	/* Free strings from URL parser */
	FREE_STR(hostname);
	FREE_STR(username);
	FREE_STR(password);
	FREE_STR(resource);

	return code;
}

char *http_get_header(struct http_handle *h, const char *name,
		      int case_sensitive)
{
	int (*_strcmp)(const char*, const char*);
	struct http_header *header;

	if(h == NULL)
		return NULL;

	/* Select cmp function */
	if(case_sensitive)
		_strcmp = &strcmp;
	else
		_strcmp = &strcasecmp;

	/* Parse all headers */
	header = h->headers;
	while(header != NULL)
	{
		if(_strcmp(name, header->name) == 0)
			return header->value;

		header = header->next;
	}

	return NULL;
}

ssize_t http_read_timeout(struct http_handle *h, unsigned char *buffer,
			  size_t size, long timeout)
{
	struct timeval tv;
	fd_set readfs;
	ssize_t len = 0;
	ssize_t ret;

	if(h == NULL || h->sock < 0)
		return -1;

	/* Get all buffer */
	while(size > 0)
	{
		/* Uses a timeout */
		if(timeout >= 0)
		{
			/* Prepare a select */
			FD_ZERO(&readfs);
			FD_SET(h->sock, &readfs);

			/* Set timeout */
			tv.tv_sec = 0;
			tv.tv_usec = timeout*1000;

			/* Do select */
			ret = select(h->sock + 1, &readfs, NULL, NULL, &tv);
			if(ret < 0)
				return -1;

			/* Timeout */
			if(ret == 0)
				break;
		}

		/* Read data from TCP socket */
		if(timeout == -1 || FD_ISSET(h->sock, &readfs))
		{
	#ifdef HAVE_OPENSSL
			if(h->is_ssl)
				ret = SSL_read(h->ssl, buffer, size);
			else
	#endif
				ret = read(h->sock, buffer, size);

			/* End of stream */
			if(ret <= 0)
				return -1;

			len += ret;
			buffer += ret;
			size -= ret;
		}
	}

	return len;
}

static void *http_thread(void *user_data)
{
	struct http_handle *h = user_data;
	unsigned char buffer[BUFFER_SIZE];
	size_t size = BUFFER_SIZE;
	ssize_t len;

	/* Do request */
	h->code = http_request(h, h->url, h->method, h->buffer, h->length);

	/* Free thread values */
	FREE_STR(h->url);
	FREE_STR(h->method);
	FREE_STR(h->buffer);
	h->url = NULL;
	h->method = NULL;
	h->buffer = NULL;
	h->length = 0;

	/* Bad request */
	if(h->code < 0)
		goto end;

	/* Call header callback */
	if(h->head_cb)
		h->head_cb(h->user_data, h->code, h);

	/* Read until end of stream */
	while(!h->stop)
	{
		/* Read data from connection */
		len = http_read_timeout(h, buffer, size, 1000);
		if(len < 0)
			break;

		/* Send data to callback */
		if(h->read_cb &&
		   h->read_cb(h->user_data, h->code, buffer, size) < 0)
			break;
	}

end:
	/* End of request: call complete callback */
	if(h->comp_cb)
		h->comp_cb(h->user_data, h->code);

	/* Lock connection */
	pthread_mutex_lock(&h->mutex);

	/* Thread is stopped */
	h->stop = 0;
	h->running = 0;

	/* Unlock connection */
	pthread_mutex_unlock(&h->mutex);

	return NULL;
}

int http_request_thread(struct http_handle *h, const char *url,
			const char *method, unsigned char *buffer,
			unsigned long len, http_head_cb head_cb,
			http_read_cb read_cb, http_comp_cb comp_cb,
			void *user_data)
{
	int ret = -1;

	/* Check URL */
	if(url == NULL || method == NULL)
		return -1;

	/* Lock connection */
	pthread_mutex_lock(&h->mutex);

	/* Thread is stopped */
	if(h->running)
	{
		ret = 1;
		goto end;
	}

	/* Copy data */
	FREE_STR(h->url);
	FREE_STR(h->method);
	FREE_STR(h->buffer);
	h->url = strdup(url);
	h->method = strdup(method);
	if(len > 0 && buffer != NULL)
	{
		/* Allocate memory */
		h->buffer = malloc(len);
		if(h->buffer == NULL)
			goto end;

		/* Copy data */
		memcpy(h->buffer, buffer, len);
		h->length = len;
	}
	else
	{
		h->buffer = NULL;
		h->length = 0;
	}

	/* Create thread */
	if(pthread_create(&h->thread, NULL, http_thread, h) != 0)
		goto end;

	/* Thread is now running */
	h->running = 1;

end:
	/* Unlock connection */
	pthread_mutex_unlock(&h->mutex);

	return ret;
}

int http_get_code(struct http_handle *h)
{
	int code;

	if(h == NULL)
		return -1;

	/* Lock connection */
	pthread_mutex_lock(&h->mutex);

	/* Get code */
	code = h->code;

	/* Unlock connection */
	pthread_mutex_unlock(&h->mutex);

	return code;
}

int http_status(struct http_handle *h)
{
	int status;

	if(h == NULL)
		return -1;

	/* Lock connection */
	pthread_mutex_lock(&h->mutex);

	/* Get thread status */
	status = h->running;

	/* Unlock connection */
	pthread_mutex_unlock(&h->mutex);

	return status;
}

void http_close_connection(struct http_handle *h)
{
	/* Close socket */
	if(h->sock >= 0)
	{
#ifdef HAVE_OPENSSL
		if(h->is_ssl)
		{
			if(h->ssl != NULL)
				SSL_free(h->ssl);
			if(h->ssl_ctx != NULL)
				SSL_CTX_free(h->ssl_ctx);
		}
#endif
		close(h->sock);
		h->sock = -1;
	}

	/* Lock connection */
	pthread_mutex_lock(&h->mutex);

	/* HTTP connection is a thread */
	if(h->running)
	{
		/* Stop thread */
		h->stop = 1;

		/* Unlock connection */
		pthread_mutex_unlock(&h->mutex);

		/* Wait end of thread */
		pthread_join(h->thread, NULL);

		/* Lock connection */
		pthread_mutex_lock(&h->mutex);
	}

	/* Unlock connection */
	pthread_mutex_unlock(&h->mutex);

	/* Free headers */
	http_free_header(h);

	/* Free hostname */
	FREE_STR(h->hostname);
}

void http_close(struct http_handle *h)
{
	if(h == NULL)
		return;

	/* Close connection */
	http_close_connection(h);

	/* Free config strings */
	FREE_STR(h->user_agent);
	FREE_STR(h->extra);
	FREE_STR(h->proxy_hostname);
	FREE_STR(h->url);
	FREE_STR(h->method);
	FREE_STR(h->buffer);

	/* Free handle */
	free(h);
}
