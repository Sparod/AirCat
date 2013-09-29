/*
 * raop.c - A RAOP Server
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
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>

#include <openssl/aes.h>

#include "rtp.h"
#include "raop_tcp.h"
#include "decoder.h"
#include "raop.h"

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifndef MAX_PACKET_SIZE
	#define MAX_PACKET_SIZE 16384
#endif

#ifndef RTP_CACHE_SIZE
	#define RTP_CACHE_SIZE 32
#endif

struct raop_handle {
	/* Protocol handler */
	int transport;			// Type of socket (TCP or UDP (=RTP))
	struct raop_tcp_handle *tcp;	// RAOP TCP server handle
	struct rtp_handle *rtp;		// RTP Server handle
	/* Crypto */
	AES_KEY aes;			// AES Key
	unsigned char aes_iv[16];	// AES IV
	/* Decoder */
	struct decoder_handle *dec;	// Decoder structure
	unsigned char alac_header[55];	// ALAC file header
	int first;
};

/* Callback for decoder */
static int raop_read_stream(void * user_data, unsigned char *buffer, size_t size);

static void raop_prepare_alac(unsigned char *header, char *format)
{
	char *fmt[12];
	char *f, *f2;
	int i;

	/* Prepare list of parameters */
	f = format;
	for(i = 0; i < 12; i++)
	{
		f2 = strchr(f, ' ');
		if(f2 == NULL)
		{
			fmt[i] = f;
			break;
		}
		*f2 = '\0';
		fmt[i] = f;
		f = f2+1;
	}

	/* Prepare alac header to set correct format */
	/* Size, frma, alac, size, alac, ? */
	header += 24;
	/* Max samples per frame (4 bytes) */
	*((uint32_t*)header) = htonl(atol(fmt[1]));
	header += 4;
	/* 7a: ? (1 byte) */
	*header++ = atoi(fmt[2]);
	/* Sample size (4 bytes) */
	*header++ = atoi(fmt[3]);
	/* Rice historymult (1 byte) */
	*header++ = atoi(fmt[4]);
	/* Rice initialhistory (1 byte) */
	*header++ = atoi(fmt[5]);
	/* Rice kmodifier (1 byte) */
	*header++ = atoi(fmt[6]);
	/* 7f: ? (1 byte) */
	*header++ = atoi(fmt[7]);
	/* 80: ? (2 bytes) */
	*((uint16_t*)header) = htons(atoi(fmt[8]));
	header += 2;
	/* 82: ? (4 bytes) */
	*((uint32_t*)header) = htonl(atol(fmt[9]));
	header += 4;
	/* 86: ? (4 bytes) */
	*((uint32_t*)header) = htonl(atol(fmt[10]));
	header += 4;
	/* Sample rate (4 bytes) */
	*((uint32_t*)header) = htonl(atol(fmt[11]));
}

int raop_open(struct raop_handle **handle, int transport, unsigned int *port, unsigned char *aes_key, unsigned char *aes_iv, int codec, char *format)
{
	struct raop_handle *h;

	/* Alloc structure */
	*handle = malloc(sizeof(struct raop_handle));
	if(*handle == NULL)
		return -1;
	h = *handle;

	/* Init structure */
	h->transport = transport;

	/* Prepare openssl for AES */
	AES_set_decrypt_key(aes_key, 128, &h->aes);
	memcpy(h->aes_iv, aes_iv, sizeof(h->aes_iv));

	/* Socket */
	if(transport == RAOP_TCP)
	{
		/* Open TCP server */
		while(raop_tcp_open(&h->tcp, *port, 100) != 0)
		{
			(*port)++;
			if(*port >= 7000)
				return -1;
		}
	}
	else
	{
		/* Open RTP server */
		while(rtp_open(&h->rtp, *port, RTP_CACHE_SIZE, 0, 0x60, 100) != 0)
		{
			(*port) += 2;
			if(*port >= 7000)
				return -1;
		}
	}

	if(codec == RAOP_ALAC)
	{
		codec = CODEC_ALAC;
		raop_prepare_alac(h->alac_header, format);
		h->first = 1;
	}
	else if(codec == RAOP_AAC)
		codec = CODEC_AAC;

	/* Open decoder */
	decoder_open(&h->dec, codec, &raop_read_stream, h);

	return 0;
}

int raop_read(struct raop_handle *h, unsigned char *buffer, size_t size)
{
	if(h == NULL)
		return -1;

	return decoder_read(h->dec, buffer, size);
}

unsigned long raop_get_samplerate(struct raop_handle *h)
{
	if(h == NULL)
		return 0;

	return decoder_get_samplerate(h->dec);
}

unsigned char raop_get_channels(struct raop_handle *h)
{
	if(h == NULL)
		return 0;

	return decoder_get_channels(h->dec);
}

int raop_close(struct raop_handle *h)
{
	if(h == NULL)
		return 0;

	/* Close decoder */
	decoder_close(h->dec);

	/* Close socket */
	if(h->transport == RAOP_TCP)
	{
		/* Close TCP */
		raop_tcp_close(h->tcp);
	}
	else
	{
		/* Close RTP */
		rtp_close(h->rtp);
	}

	free(h);

	return 0;
}

static int raop_read_stream(void * user_data, unsigned char *buffer, size_t size)
{
	struct raop_handle *h = (struct raop_handle*) user_data;
	unsigned char packet[MAX_PACKET_SIZE];
	unsigned char iv[16];
	size_t read_len, aes_len;

	/* First of all: return alac file header */
	if(h->first)
	{
		memcpy(buffer, h->alac_header, 55);
		h->first = 0;
		return 55;
	}

	/* Read a packet */
	if(h->transport == RAOP_TCP)
	{
		/* Read packet from TCP */
		read_len = raop_tcp_read(h->tcp, packet, MAX_PACKET_SIZE);
	}
	else
	{
		/* Read RTP packet */
		read_len = rtp_read(h->rtp, packet, MAX_PACKET_SIZE);
	}


	if(read_len > 0)
	{
		/* Decrypt AES packet */
		aes_len = read_len & ~0xf;
		memcpy(iv, h->aes_iv, sizeof(h->aes_iv));
		AES_cbc_encrypt(packet, buffer, aes_len, &h->aes, iv, AES_DECRYPT);
		memcpy(buffer+aes_len, packet+aes_len, read_len-aes_len);
	}

	return read_len;
}

