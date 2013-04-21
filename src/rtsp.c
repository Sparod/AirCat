/*
 * rtsp.c - A Tiny RTSP Server
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
#include <fcntl.h>
#include <poll.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "rtsp.h"

#define BUFFER_SIZE 8192

#define min(x,y) x <= y ? x : y

enum {RTSPSTATE_SEND_REPLY, RTSPSTATE_SEND_PACKET, RTSPSTATE_WAIT_REQUEST, RTSPSTATE_WAIT_PACKET};

struct rtsp_header {
	char *name;
	char *value;
	struct rtsp_header *next;
};

struct rtsp_client {
	/* Socket fd */
	int sock;
	struct pollfd *poll_entry;
	struct sockaddr_in addr;
	/* IP Address */
	unsigned char server_ip[4];
	unsigned char ip[4];
	unsigned int server_port;
	unsigned int port;
	/* Header buffer */
	char req_buffer[BUFFER_SIZE];
	size_t req_len;
	/* Packet buffer */
	unsigned char in_buffer[BUFFER_SIZE];
	size_t in_len;
	size_t in_content_len;
	/* Response header buffer */
	char *resp_buffer;
	size_t resp_len;
	/* Response packet buffer */
	unsigned char *packet_buffer;
	size_t packet_len;
	/* Buffer pointers */
	char *buffer_ptr;
	char *buffer_end;
	/* RTSP status */
	int state;
	/* RTSP variables */
	unsigned int seq;
	int request;
	char *url;
	struct rtsp_header *headers;
	/* User data */
	void *user_data;
	/* Next element */
	struct rtsp_client *next;
};

struct rtsp_handle {
	int sock;
	int users;
	int max_user;
	int (*request_callback)(struct rtsp_client *, int, const char *, void *);
	int (*read_callback)(struct rtsp_client *, unsigned char *, size_t, int, void *);
	int (*close_callback)(struct rtsp_client *, void *);
	void *user_data;
	struct pollfd *poll_table;
	struct rtsp_client *clients;
};

static char base64_table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static void rtsp_accept(struct rtsp_handle *h);
static int rtsp_handle_client(struct rtsp_handle *h, struct rtsp_client *c);
static void rtsp_close_client(struct rtsp_handle *h, struct rtsp_client *c);

static void rtsp_accept(struct rtsp_handle *h)
{
	struct rtsp_client *c = NULL;
	struct sockaddr_in addr;
	socklen_t len;
	int sock = -1;

	/* Accept client */
	len = sizeof(addr);
	sock = accept(h->sock, (struct sockaddr *)&addr, &len);
	if (sock < 0)
	{
		return;
	}

	/* Set as non blocking socket */
	fcntl(sock, F_SETFL, O_NONBLOCK);

	/* Too many users */
	if(h->users >= h->max_user)
	{
		send(sock, "RTSP/1.0 503 Server too busy\r\n\r\n", 32, 0);
		close(sock);
		return;
	}

	/* add a new connection */
	c = malloc(sizeof(struct rtsp_client));
	if(c == NULL)
	{
		close(sock);
		return;
	}

	/* Fill client structure */
	c->sock = sock;
	c->poll_entry = NULL;
        c->state = RTSPSTATE_WAIT_REQUEST;
	c->addr = addr;
	c->headers = NULL;
	/* Get Client IP Address */
	memcpy(c->ip, &addr.sin_addr, 4);
	c->port = ntohs(addr.sin_port);
	/* Get Server IP Address */
	len = sizeof(addr);
	getsockname(sock, (struct sockaddr*)&addr, &len);
	memcpy(c->server_ip, &addr.sin_addr, 4);
	c->server_port = ntohs(addr.sin_port);
	/* Init buffer lens */
	c->req_len = BUFFER_SIZE;
	c->in_len = BUFFER_SIZE;
	c->in_content_len = 0;
	c->resp_len = 0;
	c->packet_len = 0;
	/* Init buffers */
	c->resp_buffer = NULL;
	c->packet_buffer = NULL;
	c->buffer_ptr = c->req_buffer;
	c->buffer_end = c->req_buffer + c->req_len;
	/* User data */
	c->user_data = NULL;

	/* Update user number */
	c->next = h->clients;
	h->clients = c;
	h->users++;

	return;
}

static int rtsp_parse_request(struct rtsp_client *c)
{
	struct rtsp_header *head;
	char *p = NULL;
	char *p_end = NULL;
	char *name = NULL;
	char *value = NULL;

	/* Free headers from last request */
	while(c->headers != NULL)
	{
		head = c->headers;
		c->headers = head->next;
		free(head);
	}

	if(c->req_buffer == NULL)
		return -1;

	/* Read request command */
	p = (char*) c->req_buffer;
	p_end = strchr(p, ' ');
	if(p_end == NULL)
		return -1;
	*p_end = 0;
	if(strcmp(p, "ANNOUNCE") == 0)
		c->request = RTSP_ANNOUNCE;
	else if(strcmp(p, "DESCRIBE") == 0)
		c->request = RTSP_DESCRIBE;
	else if(strcmp(p, "OPTIONS") == 0)
		c->request = RTSP_OPTIONS;
	else if(strcmp(p, "SETUP") == 0)
		c->request = RTSP_SETUP;
	else if(strcmp(p, "RECORD") == 0)
		c->request = RTSP_RECORD;
	else if(strcmp(p, "SET_PARAMETER") == 0)
		c->request = RTSP_SET_PARAMETER;
	else if(strcmp(p, "GET_PARAMETER") == 0)
		c->request = RTSP_GET_PARAMETER;
	else if(strcmp(p, "FLUSH") == 0)
		c->request = RTSP_FLUSH;
	else if(strcmp(p, "PLAY") == 0)
		c->request = RTSP_PLAY;
	else if(strcmp(p, "PAUSE") == 0)
		c->request = RTSP_PAUSE;
	else if(strcmp(p, "TEARDOWN") == 0)
		c->request = RTSP_TEARDOWN;
	else
		c->request = RTSP_UNKNOWN;

	/* Read url */
	p = p_end+1;
	p_end = strchr(p, ' ');
	if(p_end == NULL)
		return -1;
	*p_end = 0;
	c->url = p;

	/* Read all lines until end of request */
	name = p_end;
	while(name < c->buffer_end)
	{
		/* Search next line */
		while(name < c->buffer_end && *name != '\n')
			name++;
		name++;

		/*  End of header */
		if(name[0] == '\r' && name[1] == '\n')
			break;

		/* Get name */
		p_end = strchr(name, ':');
		if(p_end == NULL)
			continue;
		*p_end = 0;
		value = p_end+1;
		/* Skip all whitespace */
		while(*value != 0 && *value == ' ')
			value++;
		/* Find end of string */
		p_end = value;
		while(*p_end != 0 && *p_end != '\r')
			p_end++;
		*p_end = 0;

		/* Add name and value to list */
		head = malloc(sizeof(struct rtsp_header));
		if(head == NULL)
			return -1;
		head->name = name;
		head->value = value;
		head->next = c->headers;
		c->headers = head;

		name = p_end+1;
	}

	return 0;	
}

static int rtsp_handle_client(struct rtsp_handle *h, struct rtsp_client *c)
{
	char *ptr;
	int len;

	/* Return error */
	if (c->poll_entry->revents & (POLLERR | POLLHUP))
		return -1;

	switch(c->state)
	{
		case RTSPSTATE_WAIT_REQUEST:
		{
			/* Return if no event */
			if (!(c->poll_entry->revents & POLLIN))
				return 0;

			/* Read data until end of request or end of reception */
			while(1)
			{
				/* Read one byte */
				len = recv(c->sock, c->buffer_ptr, 1, 0);
				if(len <= 0)
				{
					if(errno != EAGAIN && errno != EINTR)
						return -1;
					return 0;
				}
				c->buffer_ptr += len;

				/* search for end of request. */
				ptr = c->buffer_ptr;
				if ((ptr >= c->req_buffer + 2 && !memcmp(ptr-2, "\n\n", 2)) || (ptr >= c->req_buffer + 4 && !memcmp(ptr-4, "\r\n\r\n", 4)))
				{
					/* Terminate string by '\0' */
					*c->buffer_ptr = 0;

					/* Reached end of request */
					if(rtsp_parse_request(c) < 0)
						return -1;

					/* Look for CSeq header */
					if(rtsp_get_header(c, "CSeq", 1) == NULL)
						return -1;

					/* Call callback function */
					if(h->request_callback(c, c->request, c->url, h->user_data) < 0)
						return -1;

					/* Prepare response */
					ptr = rtsp_get_header(c, "Content-Length", 1);
					if(ptr == NULL)
					{
						if(c->resp_buffer == NULL)
						{
							/* No response from callback: send a bad request */
							c->resp_buffer = strdup("RTSP/1.0 400 Bad Request\r\n\r\n");
							c->resp_len = strlen(c->resp_buffer);
							if(c->packet_buffer != NULL)
							{
								free(c->packet_buffer);
								c->packet_buffer = NULL;
								c->packet_len = 0;
							}
						}
						c->buffer_ptr = c->resp_buffer;
						c->buffer_end = c->resp_buffer + c->resp_len;
						c->state = RTSPSTATE_SEND_REPLY;
					}
					else /* Read packet */
					{
						c->in_content_len = atol(ptr);
						c->buffer_ptr = c->in_buffer;
						c->buffer_end = c->in_buffer + c->in_len;
						c->state = RTSPSTATE_WAIT_PACKET;
					}
					break;
				}
				else if (ptr >= c->buffer_end)
				{
					/* Too long request: close connection */
					return -1;
				}
			}
			break;
		}
		case RTSPSTATE_WAIT_PACKET:
		{
			/* Return if no event */
			if (!(c->poll_entry->revents & POLLIN))
				return 0;

			/* Read until end of buffer_size or content length */
			len = min(c->buffer_end-c->buffer_ptr, c->in_content_len);
			len = recv(c->sock, c->buffer_ptr, len, 0);
			if(len <= 0)
			{
				if(errno != EAGAIN && errno != EINTR)
					return -1;
				return 0;
			}
			c->in_content_len -= len;
			c->buffer_ptr += len;

			/* End of stream or buffer is full */
			if (c->in_content_len == 0 || c->buffer_ptr == c->buffer_end)
			{
				/* Call read callback function */
				if(h->read_callback != NULL)
				{
					if(h->read_callback(c, c->in_buffer, (unsigned char*)c->buffer_ptr-c->in_buffer, c->in_content_len == 0 ? 1:0, h->user_data) < 0)
						return -1;
				}

				/* Prepare response if end of stream */
				if(c->in_content_len == 0)
				{
					if(c->resp_buffer == NULL)
					{
						/* No response from callback: send a bad request */
						c->resp_buffer = strdup("RTSP/1.0 400 Bad Request\r\n\r\n");
						c->resp_len = strlen(c->resp_buffer);
						if(c->packet_buffer != NULL)
						{
							free(c->packet_buffer);
							c->packet_buffer = NULL;
							c->packet_len = 0;
						}
					}
					c->buffer_ptr = c->resp_buffer;
					c->buffer_end = c->resp_buffer + c->resp_len;
					c->state = RTSPSTATE_SEND_REPLY;
				}
				else
				{
					c->buffer_ptr = c->in_buffer;
				}
			}
			break;
		}
		case RTSPSTATE_SEND_REPLY:
		{
			/* Return if no event */
			if (!(c->poll_entry->revents & POLLOUT))
				return 0;

			/* Send data */
			len = send(c->sock, c->buffer_ptr, c->buffer_end - c->buffer_ptr, 0);
			if (len < 0)
			{
				if(errno == EAGAIN || errno == EINTR)
					return 0;
				free(c->resp_buffer);
				c->resp_buffer = NULL;
				c->resp_len = 0;

				if(c->packet_buffer != NULL)
				{
					free(c->packet_buffer);
					c->packet_buffer = NULL;
					c->packet_len = 0;
				}
				return -1;
			}

			/* Update buffer position */
			c->buffer_ptr += len;
			if (c->buffer_ptr >= c->buffer_end)
			{
				/* Free response buffer */
				free(c->resp_buffer);
				c->resp_buffer = NULL;
				c->resp_len = 0;

				if(c->packet_buffer != NULL)
				{
					/* Send packet */
					c->buffer_ptr = (char*)c->packet_buffer;
					c->buffer_end = (char*)(c->packet_buffer + c->packet_len);
					c->state = RTSPSTATE_SEND_PACKET;
				}
				else
				{
					/* Wait for next request */
					c->buffer_ptr = c->req_buffer;
					c->buffer_end = c->req_buffer + c->req_len;
					c->state = RTSPSTATE_WAIT_REQUEST;
				}
			}
			break;
		}
		case RTSPSTATE_SEND_PACKET:
		{
			/* Return if no event */
			if (!(c->poll_entry->revents & POLLOUT))
				return 0;

			/* Send data */
			len = send(c->sock, c->buffer_ptr, c->buffer_end - c->buffer_ptr, 0);
			if (len < 0)
			{
				if(errno == EAGAIN || errno == EINTR)
					return 0;

				free(c->packet_buffer);
				c->packet_buffer = NULL;
				c->packet_len = 0;
				return -1;
			}

			/* Update buffer position */
			c->buffer_ptr += len;
			if (c->buffer_ptr >= c->buffer_end)
			{
				/* Free packet buffer */
				free(c->packet_buffer);
				c->packet_buffer = NULL;
				c->packet_len = 0;

				/* Wait for next request */
				c->buffer_ptr = c->req_buffer;
				c->buffer_end = c->req_buffer + c->req_len;
				c->state = RTSPSTATE_WAIT_REQUEST;
			}
			break;
		}
		default:
			return -1;
	}

	return 0;
}


static void rtsp_close_client(struct rtsp_handle *h, struct rtsp_client *c)
{
	struct rtsp_client **cp, *c1;
	struct rtsp_header *head;

	/* Callback before closing client socket */
	if(h->close_callback != NULL)
		h->close_callback(c, h->user_data);

	/* Remove client from list */
	cp = &h->clients;
	while((*cp) != NULL)
	{
		c1 = *cp;
		if (c1 == c)
			*cp = c->next;
		else
			cp = &c1->next;
	}

	/* Free header */
	while(c->headers != NULL)
	{
		head = c->headers;
		c->headers = head->next;
		free(head);
	}
	
	/* Free buffers */
	if(c->resp_buffer != NULL)
		free(c->resp_buffer);
	if(c->packet_buffer != NULL)
		free(c->packet_buffer);

	/* Close client socket */
	if (c->sock >= 0)
		close(c->sock);

	/* Free client structure */
	free(c);

	/* Decrement user nb */
	h->users--;
}

int rtsp_open(struct rtsp_handle **handle, unsigned int port, unsigned int max_user, void *callback, void *read_callback, void *close_callback, void *user_data)
{
	struct rtsp_handle *h;
	struct sockaddr_in addr;
	int opt = 1;

	/* Test request callback presence */
	if(callback == NULL)
		return -1;

	/* Allocate structure */
	*handle = malloc(sizeof(struct rtsp_handle));
	if(*handle == NULL)
		return -1;
	h = *handle;

	/* Init variables */
	h->sock = -1;
	h->users = 0;
	h->max_user = max_user;
	h->request_callback = callback;
	h->read_callback = read_callback;
	h->close_callback = close_callback;
	h->user_data = user_data;
	h->clients = NULL;

	/* Prepare poll table */
	h->poll_table = malloc((max_user+1)*sizeof(*h->poll_table));
	if(h->poll_table == NULL)
		return -1;

	/* Open socket */
	if((h->sock = socket(AF_INET, SOCK_STREAM, 0)) < 0)
		return -1;

	/* Force socket to bind */
	if(setsockopt(h->sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0)
		return -1;

	/* Bind */
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(port);
	addr.sin_addr.s_addr = htonl(INADDR_ANY);
	if(bind(h->sock, (struct sockaddr *) &addr, sizeof(addr)) != 0)
		return -1;

	/* Listen */
	if(listen(h->sock, 5) != 0)
		return -1;

	return 0;
}

int rtsp_loop(struct rtsp_handle *h, unsigned int timeout)
{
	struct pollfd *poll_entry;
	struct rtsp_client *c, *c_next;
	int ret;

	if(h == NULL || h->sock == -1)
		return -1;

	poll_entry = h->poll_table;

	/* Add server socket to poll table */
	poll_entry->fd = h->sock;
	poll_entry->events = POLLIN;
	poll_entry++;

	/* Fill poll table */
	c = h->clients;
	while (c != NULL)
	{
		switch(c->state)
		{
			case RTSPSTATE_SEND_REPLY:
			case RTSPSTATE_SEND_PACKET:
				c->poll_entry = poll_entry;
				poll_entry->fd = c->sock;
				poll_entry->events = POLLOUT;
				poll_entry++;
				break;
			case RTSPSTATE_WAIT_REQUEST:
			case RTSPSTATE_WAIT_PACKET:
				c->poll_entry = poll_entry;
				poll_entry->fd = c->sock;
				poll_entry->events = POLLIN;
				poll_entry++;
				break;
			default:
				c->poll_entry = NULL;
				break;
		}
		c = c->next;
	}

	/* Wait for an event with a timeout (in ms) */
	ret = poll(h->poll_table, poll_entry - h->poll_table, timeout);
	if(ret <= 0)
		return ret;

	/* Handle events */
	for(c = h->clients; c != NULL; c = c_next)
	{
		c_next = c->next;
		if(rtsp_handle_client(h, c) < 0)
			rtsp_close_client(h, c);
	}

	/* Check for new connection */
	if(h->poll_table->revents & POLLIN)
		rtsp_accept(h);

	return 0;
}

char *rtsp_get_header(struct rtsp_client *c, const char *name, int case_sensitive)
{
	struct rtsp_header *header;
	int (*_strcmp)(const char*, const char*);

	if(c == NULL)
		return NULL;

	if(case_sensitive)
		_strcmp = &strcmp;
	else
		_strcmp = &strcasecmp;

	header = c->headers;
	while(header != NULL)
	{
		if(_strcmp(name, header->name) == 0)
			return header->value;
		header = header->next;
	}

	return NULL;
}

unsigned char *rtsp_get_ip(struct rtsp_client *c)
{
	if(c == NULL)
		return NULL;
	return c->ip;
}

unsigned int rtsp_get_port(struct rtsp_client *c)
{
	if(c == NULL)
		return 0;
	return c->port;
}

unsigned char *rtsp_get_server_ip(struct rtsp_client *c)
{
	if(c == NULL)
		return NULL;
	return c->server_ip;
}

unsigned int rtsp_get_server_port(struct rtsp_client *c)
{
	if(c == NULL)
		return 0;
	return c->server_port;
}

int rtsp_get_request(struct rtsp_client *c)
{
	if(c == NULL)
		return -1;
	return c->request;
}

void *rtsp_get_user_data(struct rtsp_client *c)
{
	if(c == NULL)
		return NULL;
	return c->user_data;
}

int rtsp_create_response(struct rtsp_client *c, unsigned int code, const char *value)
{
	if(c == NULL)
		return -1;

	if(c->resp_buffer != NULL)
		free(c->resp_buffer);

	c->resp_buffer = malloc(BUFFER_SIZE);

	sprintf(c->resp_buffer, "RTSP/1.0 %d %s\r\n\r\n", code, value);

	c->resp_len = strlen(c->resp_buffer);

	return 0;
}

int rtsp_add_response(struct rtsp_client *c, const char *name, const char *value)
{
	int len;
	char *p;

	if(c == NULL)
		return -1;

	if(c->resp_buffer == NULL)
		return -1;

	len = c->resp_len-2;
	c->resp_len += strlen(name)+strlen(value)+4;

	/* Reallocate more space */
	if(c->resp_len >= BUFFER_SIZE)
	{
		p = realloc(c->resp_buffer, c->resp_len);
		if(p == NULL)
		{
			free(c->resp_buffer);
			c->resp_len = 0;
			return -1;
		}
	}

	/* Add entry to response */
	sprintf(&c->resp_buffer[len], "%s: %s\r\n\r\n", name, value);

	return 0;
}

int rtsp_set_response(struct rtsp_client *c, char *str)
{
	if(c == NULL)
		return -1;

	c->resp_buffer = str;
	c->resp_len = strlen(str);
	return 0;
}

int rtsp_set_packet(struct rtsp_client *c, unsigned char *buffer, size_t len)
{
	if(c == NULL)
		return -1;

	c->packet_buffer = buffer;
	c->packet_len = len;
	return 0;
}

int rtsp_close(struct rtsp_handle *h)
{

	if(h == NULL)
		return 0;
	
	/* Close all clients and free it*/
	while(h->clients != NULL)
		rtsp_close_client(h, h->clients);

	/* Close socket */
	if(h->sock != -1)
	{
		close(h->sock);
	}
	h->sock = -1;

	/* Free poll table */
	if(h->poll_table != NULL)
		free(h->poll_table);

	/* Free structure */
	free(h);

	return 0;
}

char *rtsp_encode_base64(const char *buffer, int length)
{
	unsigned char *s = (unsigned char*) buffer;
	char *output, *p;

	output = malloc(((4*((length+2)/3))+1)*sizeof(char));
	if(output == NULL)
		return NULL;

	p = output;

	/* Function from libbb of BusyBox */
	/* Transform the 3x8 bits to 4x6 bits */
	while (length > 0)
	{
		unsigned s1, s2;

		/* Are s[1], s[2] valid or should be assumed 0? */
		s1 = s2 = 0;
		length -= 3; /* can be >=0, -1, -2 */
		if (length >= -1)
		{
			s1 = s[1];
			if (length >= 0)
				s2 = s[2];
		}
		*p++ = base64_table[s[0] >> 2];
		*p++ = base64_table[((s[0] & 3) << 4) + (s1 >> 4)];
		*p++ = base64_table[((s1 & 0xf) << 2) + (s2 >> 6)];
		*p++ = base64_table[s2 & 0x3f];
		s += 3;
	}
	/* Zero-terminate */
	*p = '\0';
	/* If length is -2 or -1, pad last char or two */
	while (length)
	{
		*--p = base64_table[64];
		length++;
	}

	return output;
}

void rtsp_decode_base64(char *buffer)
{
	const unsigned char *in = (const unsigned char *)buffer;
	/* The decoded size will be at most 3/4 the size of the encoded */
	unsigned ch = 0;
	int i = 0;

	while (*in) {
		int t = *in++;

		if (t >= '0' && t <= '9')
			t = t - '0' + 52;
		else if (t >= 'A' && t <= 'Z')
			t = t - 'A';
		else if (t >= 'a' && t <= 'z')
			t = t - 'a' + 26;
		else if (t == '+')
			t = 62;
		else if (t == '/')
			t = 63;
		else if (t == '=')
			t = 0;
		else
			continue;

		ch = (ch << 6) | t;
		i++;
		if (i == 4)
		{
			*buffer++ = (char) (ch >> 16);
			*buffer++ = (char) (ch >> 8);
			*buffer++ = (char) ch;
			i = 0;
		}
	}

	/* Padding */
	if (i != 0)
	{
		while (i--)
			ch = (ch << 6) | 0;
		*buffer++ = (char) (ch >> 16);
		*buffer++ = (char) (ch >> 8);
		*buffer++ = (char) ch;
	}

	*buffer = '\0';
}

