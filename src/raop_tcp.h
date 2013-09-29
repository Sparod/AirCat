/*
 * raop_tcp.h - A RAOP TCP Server
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

#ifndef _RAOP_TCP_SERVER_H
#define _RAOP_TCP_SERVER_H

struct raop_tcp_handle;

int raop_tcp_open(struct raop_tcp_handle **h, unsigned int port, unsigned int timeout);

int raop_tcp_read(struct raop_tcp_handle *h, unsigned char *buffer, size_t size);

int raop_tcp_close(struct raop_tcp_handle *h);

#endif

