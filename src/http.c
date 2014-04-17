/*
 * http.c - A Tiny HTTP Client
 *
 * Support only HTTP 1.0 with:
 * 	- Basic HTTP Proxy (no auth!)
 * 	- Basic Auth
 *	- Follow redirection
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>

#define MAX_SIZE_HEADER 8192
#define MAX_SIZE_LINE 512
#define DEFAULT_USER_AGENT "tiny_http 0.1"

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef HAVE_OPENSSL
#include <openssl/ssl.h>
#endif

#include "utils.h"
#include "http.h"

enum {HTTP_GET, HTTP_HEAD, HTTP_POST};

struct http_header {
	char *name;
	char *value;
	struct http_header *next;
};

struct http_request {
	char *user_agent;
	char *hostname;
	unsigned int port;
	char *auth;
	char *resource;
	char *extra;
};

struct http_proxy {
	int use;
	char *hostname;
	unsigned int port;
};

struct http_handle {
	int sock;
	int is_ssl;
#ifdef HAVE_OPENSSL
	SSL *ssl;
	SSL_CTX *ssl_ctx;
#endif
	int follow;
	struct http_proxy proxy;
	struct http_request req;
	struct http_header *headers;
};

static int http_parse_url(struct http_handle *h, const char *url);
static int http_connect(struct http_handle *h);
static int http_send_request(struct http_handle *h, const char *url, int type, unsigned char *post_buffer, int post_length);
static int http_parse_header(struct http_handle *h);
static int http_copy_options(struct http_handle *h2, struct http_handle *h);
static int http_copy(struct http_handle *h, struct http_handle *h2);
static int http_close_free(struct http_handle *h, int free);

struct http_handle *http_init()
{
	struct http_handle *h;

	h = malloc(sizeof(struct http_handle));

	h->sock = -1;
	h->follow = 1;
	h->is_ssl = 0;
	memset((unsigned char*)&(h->proxy), 0, sizeof(struct http_proxy));
	memset((unsigned char*)&(h->req), 0, sizeof(struct http_request));
	h->headers = NULL;

	return h;
}

int http_set_option(struct http_handle *h, int option, char *value)
{
	switch (option)
	{
		case HTTP_USER_AGENT:
			h->req.user_agent = strdup(value);
			break;
		case HTTP_PROXY:
			h->proxy.use = strcmp(value, "yes") == 0 ? 1 : 0;
			break;
		case HTTP_PROXY_HOST:
			h->proxy.hostname = strdup(value);
			break;
		case HTTP_PROXY_PORT:
			h->proxy.port = atoi(value);
			break;
		case HTTP_EXTRA_HEADER:
			h->req.extra = strdup(value);
			break;
		default:
			return -1;
	}

	return 0;
}

int http_get(struct http_handle *h, const char *url)
{
	return http_send_request(h, url, HTTP_GET, NULL, 0);
}

int http_head(struct http_handle *h, const char *url)
{
	return http_send_request(h, url, HTTP_HEAD, NULL, 0);
}

int http_post(struct http_handle *h, const char *url, unsigned char *buffer, int length)
{
	return http_send_request(h, url, HTTP_POST, buffer, length);
}

char *http_get_header(struct http_handle *h, const char *name, int case_sensitive)
{
	struct http_header *header;
	int (*_strcmp)(const char*, const char*);
	if(case_sensitive)
		_strcmp = &strcmp;
	else
		_strcmp = &strcasecmp;

	header = h->headers;
	while(header != NULL)
	{
		if(_strcmp(name, header->name) == 0)
			return header->value;
		header = header->next;
	}

	return NULL;
}

int http_read(struct http_handle *h, unsigned char *buffer, int size)
{
	if(h->sock < 0)
		return -1;

#ifdef HAVE_OPENSSL
	if(h->is_ssl)
		return SSL_read(h->ssl, buffer, size);
	else
#endif
		return read(h->sock, buffer, size);
}

int http_close(struct http_handle *h)
{
	return http_close_free(h, 1);
}

/* Private functions */

static int http_parse_url(struct http_handle *h, const char *url)
{
	char *temp;

	h->req.port = 80;

	/* Remove http:// and https:// */
	if(strncmp(url, "http://", 7) == 0)
	{
		url += 7;
	}
	else if(strncmp(url, "https://", 8) == 0)
	{
		url += 8;
		h->req.port = 443;
		h->is_ssl = 1;
#ifndef HAVE_OPENSSL
		fprintf(stderr, "HTTPS not yet supported!\n");
		return -1;
#endif
	}

	/* Scheme: http://username:password@hostname:port/resource?data */

	/* Separate in two url (search first '/') */
	temp = strchr(url, '/');
	if(temp != NULL)
	{
		*temp = '\0';
		h->req.resource = temp+1;
	}

	/* Separate auth and hostname part (search first '@') */
	temp = strchr(url, '@');
	if(temp != NULL)
	{
		*temp = '\0';
		h->req.hostname = temp+1;
		h->req.auth = (char*)url;
	}
	else
		h->req.hostname = (char*)url;

	/* Separate hostname and port (search first ':') */
	temp = strchr(h->req.hostname, ':');
	if(temp != NULL)
	{
		*temp = '\0';
		h->req.port = atoi(temp+1);
	}

	return 0;
}

static int http_connect(struct http_handle *h)
{
	struct sockaddr_in server_addr;
	struct hostent *server_ip;

	/* Open socket to shoutcast */
	h->sock = socket(AF_INET, SOCK_STREAM, 0);
	if (h->sock < 0)
	{
		return -1;
	}

	/* Get ip address from hostname */
	if(h->proxy.use)
		server_ip = gethostbyname(h->proxy.hostname);
	else
		server_ip = gethostbyname(h->req.hostname);
	if (server_ip == NULL)
	{
		return -1;
	}

	/* Set shoutcast server address */
	memset((unsigned char *)&server_addr, 0, sizeof(server_addr));
	server_addr.sin_family = AF_INET;
	memcpy((char *)&server_addr.sin_addr.s_addr, (char *)server_ip->h_addr, server_ip->h_length);
	if(h->proxy.use)
		server_addr.sin_port = htons(h->proxy.port);
	else
		server_addr.sin_port = htons(h->req.port);

	/* Connect to HTTP server */
	if (connect(h->sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
	{
		return -1;
	}

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

	return 0;
}

static int http_send_request(struct http_handle *h, const char *url, int type, unsigned char *post_buffer, int post_length)
{
	char *method[] = {"GET", "HEAD", "POST"};
	char buffer[MAX_SIZE_HEADER];
	char *location;
	char *auth = NULL;
	int code = 0;
	int size = 0;
	int ret = 0;

	/* Parse URL */
	if(http_parse_url(h, url) < 0)
		return -1;

	/* Connect */
	if(http_connect(h) < 0)
		return -1;

	/* Make HTTP request */
	if(h->proxy.use)
		size = sprintf(buffer, "%s http://%s/%s HTTP/1.0\r\n", method[type], h->req.hostname, h->req.resource == NULL ? "" : h->req.resource);
	else
		size = sprintf(buffer, "%s /%s HTTP/1.0\r\n", method[type], h->req.resource == NULL ? "" : h->req.resource);
	size += sprintf(&buffer[size], "Host: %s\r\n", h->req.hostname);
	size += sprintf(&buffer[size], "User-Agent: %s\r\n", h->req.user_agent == NULL ? DEFAULT_USER_AGENT : h->req.user_agent);
	size += sprintf(&buffer[size], "Connection: keep-alive\r\n");
	if(type == HTTP_POST)
	{
		size += sprintf(&buffer[size], "Content-type: application/x-www-form-urlencoded\r\n");
		size += sprintf(&buffer[size], "Content-Length: %d\r\n", post_length);
	}
	if(h->req.auth != NULL)
	{
		auth = base64_encode(h->req.auth, strlen(h->req.auth));
		size += sprintf(&buffer[size], "Authorization: Basic %s\r\n", auth);
		free(auth);
	}
	if(h->req.extra != NULL)
		size += sprintf(&buffer[size], "%s", h->req.extra);
	size += sprintf(&buffer[size], "\r\n");

	/* Send HTTP request */
#ifdef HAVE_OPENSSL
	if(h->is_ssl)
		ret = SSL_write(h->ssl, buffer, size);
	else
#endif
		ret = write(h->sock, buffer, size);
	if (ret != size)
	{
		http_close(h);
		return -1;
	}

	/* Send data */
	if (type == HTTP_POST && write(h->sock, post_buffer, post_length) != post_length)
	{
		http_close(h);
		return -1;
	}

	/* Parse header */
	code = http_parse_header(h);
	/* Follow redirection */
	if(h->follow && (code == 301 || code == 302))
	{
		struct http_handle *h2;

		/* Get redirection from header */
		location = (char*)http_get_header(h, "Location", 0);

		/* Regenerate url if auth */
		if(h->req.auth != NULL)
		{
			auth = malloc((strlen(h->req.auth)+strlen(location)+2)*sizeof(char));
			ret = (strstr(location, "//")-location);
			location[ret] = 0;
			sprintf(auth, "%s//%s@%s", location, h->req.auth, &location[ret+2]);
			location = auth;
		}

		/* Init a new connection */
		h2 = http_init();

		/* Copy options from current connection */
		http_copy_options(h2, h);

		/* Send new request */
		code = http_send_request(h2, location, type, post_buffer, post_length);

		/* Close current connection and copy new connection in current */
		http_copy(h, h2);

		/* If url has been regenerated */
		if(h->req.auth != NULL)
		{
			free(auth);
		}
	}

	return code;
}

static int http_read_line(struct http_handle *h, char *buffer, int length)
{
	int i;
	int ret;

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

	/* Read first line */
	size = 	http_read_line(h, buffer, MAX_SIZE_LINE);
	if(size > 100) //Too long for a first line in HTTP protocol
		return -1;

	/* Verify protocol */
	if(strncmp(buffer, "HTTP/1.", 7) != 0 && strncmp(buffer, "ICY", 3) != 0) // HACK for Shoutcast !
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

/* Copy options from h -> h2 */
static int http_copy_options(struct http_handle *h2, struct http_handle *h)
{
	if(h == NULL || h2 == NULL)
		return -1;

	if(h->req.user_agent != NULL)
		h2->req.user_agent = strdup(h->req.user_agent);
	if(h->proxy.hostname != NULL)
		h2->proxy.hostname = strdup(h->proxy.hostname);
	if(h->req.extra != NULL)
		h2->req.extra = strdup(h->req.extra);
	h2->proxy.use = h->proxy.use;
	h2->proxy.port = h->proxy.port;

	return 0;
}

/* Copy h2 -> h */
static int http_copy(struct http_handle *h, struct http_handle *h2)
{
	if(h == NULL || h2 == NULL)
		return -1;

	/* Close socket and free only members of structure */
	http_close_free(h, 0);

	/* Copy memory of h2 -> h */
	memcpy((unsigned char*)h, (unsigned char*)h2, sizeof(struct http_handle));

	/* Free only structure but not its members */
	free(h2);

	return 0;
}

static int http_close_free(struct http_handle *h, int do_free)
{
	if(h == NULL)
		return 0;

	/* Close socket */
	if(h->sock != -1)
	{
#ifdef HAVE_OPENSSL
		if(h->is_ssl)
		{
			SSL_free(h->ssl);
			SSL_CTX_free(h->ssl_ctx);
		}
#endif
		close(h->sock);
	}
	h->sock = -1;

	/* Free memory */
	while(h->headers != NULL)
	{
		struct http_header *temp = h->headers;
		h->headers = h->headers->next;
		if(temp->name != NULL)
			free(temp->name);
		if(temp->value != NULL)
			free(temp->value);
		free(temp);
	}
	if(h->req.user_agent != NULL)
		free(h->req.user_agent);
	if(h->req.extra != NULL)
		free(h->req.extra);
	if(h->proxy.hostname != NULL)
		free(h->proxy.hostname);

	/* Free structure */
	if(do_free)
		free(h);

	return 0;
}

