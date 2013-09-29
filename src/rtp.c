/*
 * rtp.c - A Tiny RTP Receiver
 * Imported from mplayer project
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
#include <errno.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <netinet/in.h>

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#ifdef HAVE_SYS_FILIO_H
#include <sys/filio.h>
#endif

#ifdef HAVE_SYS_UIO_H
#include <sys/uio.h>
#endif

#include "rtp.h"

#ifndef MAX_RTP_PACKET_SIZE
	#define MAX_RTP_PACKET_SIZE 1500
#endif

#define DEFAULT_CACHE_SIZE 32
#define MAX_IOVECS 16

/**
 * RTP Header structure
 */
struct rtp_header {
    uint16_t version:2;
    uint16_t padding:1;
    uint16_t extension:1;
    uint16_t cc:4;
    uint16_t marker:1;
    uint16_t payload:7;
    uint16_t sequence;
    uint32_t timestamp;
    uint32_t ssrc;
    uint32_t csrc[16];
};

struct rtp_cache{
    struct rtp_header header;
    char buffer[MAX_RTP_PACKET_SIZE];
    unsigned int len;
    uint16_t seq;
};

struct rtp_handle {
	/* UDP socket */
	int sock;
	int rtp_port;
	unsigned int timeout;
	/* RTP params */
	int cache_size;
	uint32_t ssrc;
	unsigned char payload;
//	uint32_t timestamp;
	/* RTP cache */
	struct rtp_cache *cache;
	unsigned int cache_pos;
};

static void rtp_cache_reset(struct rtp_handle *h, uint16_t seq)
{
	int i = 0;

	//h->cache[0].seq = ++seq;

	for(i = 0; i < h->cache_size; i++)
	{
		h->cache[i].len = 0;
		h->cache[i].seq = ++seq;
	}
}

int rtp_open(struct rtp_handle **handle, unsigned int port, unsigned int cache_size, unsigned long ssrc, unsigned char payload, unsigned int timeout)
{
	struct rtp_handle *h;
	struct sockaddr_in addr;
	int opt;

	/* Set a default cache size */
	if(cache_size == 0)
		cache_size = DEFAULT_CACHE_SIZE;

	/* Allocate structure */
	*handle = malloc(sizeof(struct rtp_handle));
	if(*handle == NULL)
		return -1;
	h = *handle;

	/* Init variables */
	h->rtp_port = port;
	h->cache_size = cache_size;
	h->cache = NULL;
	h->cache_pos = 0;
	h->ssrc = ssrc;
	h->payload = payload;
	h->timeout = timeout*1000;

	/* Open socket */
	if((h->sock = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
		return -1;

	/* Set low delay on UDP socket */
#ifdef SO_PRIORITY
	opt = 6;
	if (setsockopt(h->sock, SOL_SOCKET, SO_PRIORITY, (const void *) &opt, sizeof(opt)) < 0)
		fprintf(stderr, "Can't change socket priority!\n");
#endif

#if defined(IPTOS_LOWDELAY) && defined(IP_TOS) && (defined(SOL_IP) || defined(IPPROTO_IP))
	{
		opt = IPTOS_LOWDELAY;
#ifdef SOL_IP
		if (setsockopt(h->sock, SOL_IP, IP_TOS, (const void *) &opt, sizeof(opt)) < 0)
#else
		if (setsockopt(h->sock, IPPROTO_IP, IP_TOS, (const void *) &opt, sizeof(opt)) < 0)
#endif
			fprintf(stderr, "Can't change socket TOS!\n");
	}
#endif

	/* Force socket to bind */
	opt = 1;
	if(setsockopt(h->sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0)
		return -1;

	/* Bind */
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(port);
	addr.sin_addr.s_addr = htonl(INADDR_ANY);
	if(bind(h->sock, (struct sockaddr *) &addr, sizeof(addr)) != 0)
		return -1;

	/* Initialize cache */
	h->cache = malloc(sizeof(struct rtp_cache)*h->cache_size);
	if(h->cache == NULL)
		return -1;

	/* Reset cache */
	rtp_cache_reset(h, 0);

	return 0;
}

int rtp_recv(struct rtp_handle *h, struct rtp_header *head, unsigned char *buffer, int len, int *pos)
{
	socklen_t sockaddr_size;
	struct sockaddr src_addr;
	struct timeval tv;
	uint32_t header;
	fd_set readfs;
	int size;

	sockaddr_size = sizeof(src_addr);

	FD_ZERO(&readfs);
	FD_SET(h->sock, &readfs);

	tv.tv_sec = 0;
	tv.tv_usec = h->timeout;

	if(select(h->sock + 1, &readfs, NULL, NULL, &tv) < 0)
		return -1;

	if(FD_ISSET(h->sock, &readfs))
	{
		/* Get message from socket */
		if((size = recvfrom(h->sock, buffer, len, 0, &src_addr, &sockaddr_size)) <= 0)
		{
			fprintf(stderr, "recvfrom() error\n");
			return size;
		}

		/* Verify real packet size */
		if(size < 12)
		{
			fprintf(stderr, "RTP packet is too short\n");
			return -1;
		}

		/* Parse RTP header */
		memcpy(&header, buffer, sizeof(uint32_t));
		header = ntohl(header);
		head->version = (header >> 30) & 0x03;
		head->padding = (header >> 29) & 0x01;
		head->extension = (header >> 28) & 0x01;
		head->cc = (header >> 24) & 0x0F;
		head->payload = ((header >> 16) & 0x7F);
		head->sequence = (uint16_t) (header & 0x0FFFF);

		memcpy(&head->timestamp, (uint8_t*) buffer + 4, sizeof(uint32_t));
		head->timestamp = ntohl(head->timestamp);

		memcpy(&head->ssrc, (uint8_t*) buffer + 8, sizeof(uint32_t));
		head->ssrc = ntohl(head->ssrc);

		/* Calculate position of data */
		*pos = 12 + head->cc*4;
		if (*pos > (unsigned) size)
		{
			fprintf(stderr, "RTP packet too short. (CSRC)\n");
			return -1;
		}

		return size - *pos;
	}

	return 0;

}

int rtp_read(struct rtp_handle *h, unsigned char *buffer, int len)
{
	unsigned short nextseq;
	struct rtp_header header;
	unsigned char recv_buf[MAX_RTP_PACKET_SIZE];
	int recv_len = MAX_RTP_PACKET_SIZE;
	int pos = 0;
	int32_t seqdiff = 0;
	uint16_t seq;

	if(h == NULL)
		return -1;

	/* Is first packet available? */
	if (h->cache[h->cache_pos].len != 0)
	{
		// Copy next non empty packet from cache
		memcpy(buffer, h->cache[h->cache_pos].buffer, h->cache[h->cache_pos].len);
		len = h->cache[h->cache_pos].len; // can be zero?

		// Reset fisrt slot and go next in cache
		h->cache[h->cache_pos].len = 0;
		nextseq = h->cache[h->cache_pos].seq;
		h->cache_pos = ( 1 + h->cache_pos ) % h->cache_size;
		h->cache[h->cache_pos].seq = nextseq + 1;
		return len;
	}
	else
	{
		/* Get packet from socket */
		while((recv_len = rtp_recv(h, &header, recv_buf, MAX_RTP_PACKET_SIZE, &pos)) > 0)
		{
			/* Verify SSRC */
			if(header.ssrc != h->ssrc)
			{
				/* Not yet received ssrc: first packet */
				if(h->ssrc == 0)
				{
					h->ssrc = header.ssrc;
					h->cache[h->cache_pos].seq = header.sequence;
				}
				else
					continue;
			}

			/* Verify payload */
			if(h->payload != 0 && header.payload != h->payload)
				continue;
			
			/* Calculate sequence number difference */
			seq = header.sequence;
			seqdiff = seq - h->cache[h->cache_pos].seq;

			/* If expected packet: feed */
			if(seqdiff == 0)
			{
				h->cache_pos = ( 1 + h->cache_pos ) % h->cache_size;
				h->cache[h->cache_pos].seq = ++seq;
				goto feed;
			}
			else if (seqdiff > h->cache_size)
			{
				rtp_cache_reset(h, seq);
				fprintf(stderr, "RTP cache: Overrun! %u\n", seq);
				goto feed;
			}
			else if (seqdiff < 0)
			{
				int i;

				// Is it a stray packet re-sent to network?
				for (i = 0; i < h->cache_size; i++)
					if (h->cache[i].seq == seq)
					{
						fprintf(stderr, "RTP cache: Re-sent packet!\n");
						continue;
					}

				// Some heuristic to decide when to drop packet or to restart everything
				if (seqdiff > -(3 * h->cache_size))
				{
					fprintf(stderr, "RTP cache: Underrun!\n");
					continue;
				}

				rtp_cache_reset(h, seq);
				goto feed;
			}

			seqdiff = ( seqdiff + h->cache_pos ) % h->cache_size;
			memcpy(h->cache[seqdiff].buffer, recv_buf+pos, recv_len);
			h->cache[seqdiff].len = recv_len;
			h->cache[seqdiff].seq = seq;
		}
		return 0;
	}

feed:
	memcpy(buffer, recv_buf+pos, recv_len);
	return recv_len;
}

void rtp_flush(struct rtp_handle *h)
{
	if(h == NULL)
		return;
	rtp_cache_reset(h, 0);
}

int rtp_close(struct rtp_handle *h)
{
    if(h == NULL)
        return 0;

    if(h->cache != NULL)
        free(h->cache);

    free(h);

    return 0;
}
