/*
 * raop_tcp.c - A RAOP TCP Server
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
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

struct raop_tcp_handle {
	/* TCP sockets */
	int server_sock;	// TCP server socket
	int client_sock;	// TCP client socket
	/* TODO */
	unsigned int remaining;	// Remaining bytes in a packet
	unsigned int timeout;	// Timeout for incoming packet
};

int raop_tcp_open(struct raop_tcp_handle **handle, unsigned int port, unsigned int timeout)
{
	struct raop_tcp_handle *h;
	struct sockaddr_in addr;
	int opt;

	/* Alloc structure */
	*handle = malloc(sizeof(struct raop_tcp_handle));
	if(*handle == NULL)
		return -1;
	h = *handle;

	/* Init structure */
	h->client_sock = -1;
	h->remaining = 0;
	h->timeout = timeout*1000;

	/* Open TCP server */
	if((h->server_sock = socket(AF_INET, SOCK_STREAM, 0)) < 0)
		return -1;

	/* Force socket to bind */
	opt = 1;
	if(setsockopt(h->server_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0)
		return -1;

	/* Bind */
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(port);
	addr.sin_addr.s_addr = htonl(INADDR_ANY);
	if(bind(h->server_sock, (struct sockaddr *) &addr, sizeof(addr)) != 0)
		return -1;

	/* Listen */
	listen(h->server_sock, 1);

	return 0;
}

static int raop_tcp_read_timeout(struct raop_tcp_handle *h, unsigned char *buffer, size_t size)
{
	struct timeval tv;
	fd_set readfs;

	FD_ZERO(&readfs);
	FD_SET(h->client_sock, &readfs);

	tv.tv_sec = 0;
	tv.tv_usec = h->timeout;

	if(select(h->client_sock + 1, &readfs, NULL, NULL, &tv) < 0)
		return -1;

	if(FD_ISSET(h->client_sock, &readfs))
	{
		/* Read a packet from TCP */
		return read(h->client_sock, buffer, size);
	}

	return 0;
}

static int raop_tcp_accept(struct raop_tcp_handle *h)
{
	struct sockaddr_in addr;
	socklen_t client_len;
	struct timeval tv;
	fd_set readfs;

	FD_ZERO(&readfs);
	FD_SET(h->server_sock, &readfs);

	tv.tv_sec = 0;
	tv.tv_usec = h->timeout;

	if(select(h->server_sock + 1, &readfs, NULL, NULL, &tv) < 0)
		return -1;

	if(FD_ISSET(h->server_sock, &readfs))
	{
		/* Accept TCP connection */
		client_len = sizeof(addr);
		return accept(h->server_sock, (struct sockaddr *)&addr,
			      &client_len);
	}

	return 0;
}

int raop_tcp_read(struct raop_tcp_handle *h, unsigned char *buffer, size_t size)
{
	unsigned char header[16];
	int read_len;
	int sock;

	/* Accept a client */
	if(h->client_sock == -1)
	{
		sock = raop_tcp_accept(h);
		if(sock <= 0)
			return 0;
		h->client_sock = sock;
	}

	/* Get next packet */
	if(h->remaining == 0)
	{
		read_len = raop_tcp_read_timeout(h, header, 16);
		if(read_len <= 0)
			return read_len;

		/* Find the header */
		while(header[0] != 0x24 || header[1] != 0x00 || header[4] != 0xF0 || header[5] != 0xFF)
		{
			memmove(header, header+1, 15);
			if(raop_tcp_read_timeout(h, &header[15], 1) < 0)
				return -1;
		}

		/* Get packet size */
		h->remaining = (header[2] << 8) | header[3];
		h->remaining -= 12;
	}

	/* Read packet */
	read_len = 0;
	if(h->remaining > 0)
	{
		if(size > h->remaining)
			size = h->remaining;
		read_len = raop_tcp_read_timeout(h, buffer, size);
		if(read_len <= 0)
			return read_len;
		h->remaining -= read_len;
	}

	return read_len;
}

int raop_tcp_close(struct raop_tcp_handle *h)
{
	/* Close client socket */
	close(h->client_sock);

	/* Close server socket */
	close(h->server_sock);

	free(h);

	return 0;
}

