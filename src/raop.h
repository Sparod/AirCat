/*
 * raop.h - A RAOP Server
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

#ifndef _RAOP_SERVER_H
#define _RAOP_SERVER_H

enum {RAOP_PCM, RAOP_ALAC, RAOP_AAC};
enum {RAOP_TCP, RAOP_UDP};

struct raop_attr{
	/* Transport method: TCP or UDP */
	int transport;
	/* Server port: incremented by RAOP if port already used */
	unsigned int port;
	/* Client ip address */
	unsigned char *ip;
	/* RTCP ports: not used if 0 */
	unsigned int control_port;
	unsigned int timing_port;
	/* AES key and IV to decrypt audio */
	unsigned char *aes_key;
	unsigned char *aes_iv;
	/* Audio codec: PCM, ALAC or AAC */
	int codec;
	/* Format string for codec parameters: provided by RTSP */
	char *format;
};

struct raop_handle;

int raop_open(struct raop_handle **h, struct raop_attr *attr);

int raop_read(struct raop_handle *h, unsigned char *buffer, size_t size);

unsigned long raop_get_samplerate(struct raop_handle *h);

unsigned char raop_get_channels(struct raop_handle *h);

int raop_close(struct raop_handle *h);

#endif

