/*
 * raop.c - A RAOP Server
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
	#define RTP_CACHE_SIZE 100
#endif

#ifndef RTP_CACHE_RESENT
	#define RTP_CACHE_RESENT 4
#endif

#ifndef RTP_CACHE_LOST
	#define RTP_CACHE_LOST 80
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
	/* Input buffer */
	unsigned char packet[MAX_PACKET_SIZE];
	unsigned long packet_len;
	unsigned long pcm_remaining;
	/* Stream properties */
	unsigned long samplerate;
	unsigned char channels;
};

/* Callbacks for RTP */
static size_t raop_cust_cb(void *user_data, unsigned char *buffer, size_t len);
static void raop_rtcp_cb(void *user_data, unsigned char *buffer, size_t len);
static void raop_resent_cb(void *user_data, unsigned int seq, unsigned count);

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

int raop_open(struct raop_handle **handle, struct raop_attr *attr)
{
	unsigned int config_size = 0;
	unsigned char config[55];
	struct rtp_attr r_attr;
	struct raop_handle *h;
	int codec = CODEC_NO;

	/* Alloc structure */
	*handle = malloc(sizeof(struct raop_handle));
	if(*handle == NULL)
		return -1;
	h = *handle;

	/* Init structure */
	h->transport = attr->transport;
	h->packet_len = 0;
	h->pcm_remaining = 0;

	/* Prepare openssl for AES */
	AES_set_decrypt_key(attr->aes_key, 128, &h->aes);
	memcpy(h->aes_iv, attr->aes_iv, sizeof(h->aes_iv));

	/* Socket */
	if(attr->transport == RAOP_TCP)
	{
		/* Open TCP server */
		while(raop_tcp_open(&h->tcp, attr->port, 1) != 0)
		{
			attr->port++;
			if(attr->port >= 7000)
				return -1;
		}
	}
	else
	{
		/* Open RTP server */
		memset(&r_attr, 0, sizeof(struct rtp_attr));
		r_attr.ip = attr->ip;
		r_attr.port = attr->port;
		r_attr.rtcp_port = attr->control_port;
		r_attr.payload = 0x60;
		r_attr.pool_packet_count = 44100/352;
		r_attr.delay_packet_count = 11025/352;
		r_attr.resent_ratio = 10;
		r_attr.cust_cb = &raop_cust_cb;
		r_attr.cust_data = NULL;
		r_attr.rtcp_cb = &raop_rtcp_cb;
		r_attr.rtcp_data = h;
		r_attr.resent_cb = &raop_resent_cb;
		r_attr.resent_data = h;

		while(rtp_open(&h->rtp, &r_attr) != 0)
		{
			r_attr.port += 2;
			if(r_attr.port >= 7000)
				return -1;
		}
		attr->port = r_attr.port;
	}

	if(attr->codec == RAOP_ALAC)
	{
		codec = CODEC_ALAC;
		raop_prepare_alac(config, attr->format);
		config_size = 55;
	}
	else if(attr->codec == RAOP_AAC)
	{
		codec = CODEC_AAC;
		config_size = 0;
	}

	/* Open decoder */
	if(decoder_open(&h->dec, codec, config, config_size, &h->samplerate,
		     &h->channels) != 0)
		return -1;

	return 0;
}

static int raop_get_next_packet(struct raop_handle *h)
{
	unsigned char packet[MAX_PACKET_SIZE];
	unsigned char iv[16];
	size_t in_size;
	ssize_t read_len, aes_len;

	in_size = MAX_PACKET_SIZE - h->packet_len;
	if(in_size == 0)
		return 0;

	/* Read next packet */
	if(h->transport == RAOP_TCP)
	{
		/* Read packet from TCP */
		read_len = raop_tcp_read(h->tcp, packet, in_size);
	}
	else
	{
		/* Read RTP packet */
		do{
			read_len = rtp_read(h->rtp, packet, in_size);
		} while (read_len == RTP_DISCARDED_PACKET);
		h->packet_len = 0;
	}

	/* If a packet has been received: decrypt it */
	if(read_len > 0)
	{
		/* Decrypt AES packet */
		aes_len = read_len & ~0xf;
		memcpy(iv, h->aes_iv, sizeof(h->aes_iv));
		AES_cbc_encrypt(packet, &h->packet[h->packet_len], aes_len,
				&h->aes, iv, AES_DECRYPT);
		memcpy(&h->packet[h->packet_len+aes_len], packet+aes_len,
		       read_len - aes_len);

		h->packet_len += read_len;
	}

	return 0;
}

int raop_read(void *user_data, unsigned char *buffer, size_t size,
	      struct a_format *fmt)
{
	struct raop_handle *h = (struct raop_handle *) user_data;
	struct decoder_info info;
	int total_samples = 0;
	int samples;

	if(h == NULL)
		return -1;

	/* Process remaining pcm data */
	if(h->pcm_remaining > 0)
	{
		/* Get remaining pcm data from decoder */
		samples = decoder_decode(h->dec, NULL, 0, buffer, size, &info);
		if(samples < 0)
			return -1;

		h->pcm_remaining -= samples;
		total_samples += samples;
	}

	/* Fill output buffer */
	while(total_samples < size)
	{
		/* Get next packet */
		raop_get_next_packet(h);

		/* Decode next frame */
		samples = decoder_decode(h->dec, h->packet, h->packet_len,
					 &buffer[total_samples * 4],
					 size - total_samples, &info);
		if(samples <= 0)
			return total_samples;

		/* Move input buffer to next frame (ignore RTP errors) */
		if(h->transport == RAOP_TCP && info.used < h->packet_len)
		{
			memmove(h->packet, &h->packet[info.used],
				h->packet_len - info.used);
			h->packet_len -=  info.used;
		}
		else
		{
			h->packet_len = 0;
		}

		/* Update remaining counter */
		h->pcm_remaining = info.remaining;
		total_samples += samples;
	}

	return total_samples;
}

unsigned long raop_get_samplerate(struct raop_handle *h)
{
	if(h == NULL)
		return 0;

	return h->samplerate;
}

unsigned char raop_get_channels(struct raop_handle *h)
{
	if(h == NULL)
		return 0;

	return h->channels;
}

int raop_flush(struct raop_handle *h, unsigned int seq)
{
	if(h->transport == RAOP_UDP)
		rtp_flush(h->rtp, seq, 0);

	return 0;
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

static size_t raop_cust_cb(void *user_data, unsigned char *buffer, size_t len)
{
	(void) user_data;

	/* The minimal size of packet RTP header + 4 */
	if(len < 16)
		return -1;

	/* Remove first RTP header */
	len -= 4;
	memmove(buffer, buffer+4, len);

	return len;
}

static void raop_rtcp_cb(void *user_data, unsigned char *buffer, size_t len)
{
	struct raop_handle *h = (struct raop_handle *) user_data;

	/* Verify payload */
	if(buffer[1] != 0xD6)
		return;

	/* The minimal size of packet RTP header + 4 */
	if(len < 16)
		return;

	/* Remove first RTP header */
	len -= 4;
	memmove(buffer, buffer+4, len);

	/* Send packet to RTP module */
	rtp_put(h->rtp, buffer, len);
}

static void raop_resent_cb(void *user_data, unsigned int seq, unsigned count)
{
	struct raop_handle *h = (struct raop_handle *) user_data;
	unsigned char request[8];

	if(h == NULL || h->rtp == NULL)
		return;

	/* Prepare Resent packet */
	request[0] = 0x80; // RTP header 
	request[1] = 0xD5; // Payload 0x85 with marker bit
	*(uint16_t*)(request+2) = htons(1); // Sequence number of RTCP packet
	*(uint16_t*)(request+4) = htons(seq); // First needed packet
	*(uint16_t*)(request+6) = htons(count); // Count of packet needed

	/* Send the resent packet request */
	rtp_send_rtcp(h->rtp, request, 8);
}
