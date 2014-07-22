/*
 * rtp.c - A Tiny RTP Receiver
 * Inspired by VLC RTP access module
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
#include <errno.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <pthread.h>

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

/* Default RTP configuration */
#define DEFAULT_MAX_MISORDER 100
#define DEFAULT_MAX_DROPOUT  3000

/* Maximum RTP packets to receive in one rtp_read() call */
#define MAX_RTP_RCV 50

/**
 * RTP Header structure
 */
struct rtp_packet {
	/* Packet buffer (header + data) */
	unsigned char *buffer;
	size_t len;
};

struct rtp_handle {
	/* RTP socket */
	int sock;
	int port;
	/* RTCP socket */
	int rtcp_sock;
	struct sockaddr_in rtcp_addr;
	/* Session values */
	uint32_t ssrc;
	uint8_t payload;
	size_t max_packet_size;
	/* Jitter buffer */
	struct rtp_packet *packets;
	uint16_t max_packet_count;
	uint16_t delay_packet_count;
	uint16_t resent_packet_count;
	/* Current jitter buffer status */
	int filling;
	uint16_t packet_count;
	uint16_t first_packet;
	uint16_t first_seq;
	uint16_t first_ts;
	uint16_t resent_count;
	uint32_t discarded_count;
	uint32_t drop_count;
	/* RTP module params */
	uint16_t max_misorder;
	uint16_t max_dropout;
	/* RTCP packet callback */
	void (*rtcp_cb)(void *, unsigned char *, size_t);
	void *rtcp_data;
	/* Custom received callback */
	size_t (*cust_cb)(void *, unsigned char *, size_t);
	void *cust_data;
	/* Resent Callback */
	void (*resent_cb)(void *, unsigned int, unsigned int);
	void *resent_data;
	/* Mutex */
	pthread_mutex_t mutex;
};

int rtp_open(struct rtp_handle **handle, struct rtp_attr *attr)
{
	struct rtp_handle *h;
	struct sockaddr_in addr;
	int opt, i;

	/* Check attributes */
	if(attr->max_packet_size > MAX_RTP_PACKET_SIZE || attr->payload == 0 ||
	   attr->max_packet_count == 0 ||
	   attr->delay_packet_count > attr->max_packet_count)
		return -1;

	/* Allocate structure */
	*handle = malloc(sizeof(struct rtp_handle));
	if(*handle == NULL)
		return -1;
	h = *handle;

	/* Init structure */
	h->port = attr->port;
	h->rtcp_sock = -1;
	h->ssrc = attr->ssrc;
	h->payload = attr->payload;
	h->max_packet_size = attr->max_packet_size;
	h->max_packet_count = attr->max_packet_count;
	h->delay_packet_count = attr->delay_packet_count;
	h->resent_packet_count = attr->resent_ratio > 80 ? 
			       h->delay_packet_count * 80 / 100 :
			       h->delay_packet_count * attr->resent_ratio / 100;
	h->max_misorder = attr->max_misorder;
	h->max_dropout = attr->max_dropout;

	/* Set callbacks */
	h->rtcp_cb = attr->rtcp_cb;
	h->rtcp_data = attr->rtcp_data;
	h->cust_cb = attr->cust_cb;
	h->cust_data = attr->cust_data;
	h->resent_cb = attr->resent_cb;
	h->resent_data = attr->resent_data;

	/* Set default values */
	if(h->max_misorder == 0)
		h->max_misorder = DEFAULT_MAX_MISORDER;
	if(h->max_dropout == 0)
		h->max_dropout = DEFAULT_MAX_DROPOUT;
	if(h->max_packet_size == 0)
		h->max_packet_size = MAX_RTP_PACKET_SIZE;

	/* Init jitter buffer */
	h->packets = NULL;
	h->filling = 1;
	h->packet_count = 0;
	h->resent_count = 0;
	h->first_packet = 0;
	h->first_seq = attr->seq;
	h->first_ts = attr->timestamp;
	h->discarded_count = 0;
	h->drop_count = 0;

	/* Allocate packet list */
	h->packets = calloc(h->max_packet_count, sizeof(struct rtp_packet));
	if(h->packets == NULL)
		return -1;
	for(i = 0; i < h->max_packet_count; i++)
	{
		h->packets[i].buffer = malloc(h->max_packet_size);
		if(h->packets[i].buffer == NULL)
			return -1;
	}

	/* Init thread mutex */
	pthread_mutex_init(&h->mutex, NULL);

	/* Open socket */
	if((h->sock = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
		return -1;

	/* Set low delay on UDP socket */
#ifdef SO_PRIORITY
	opt = 6;
	if(setsockopt(h->sock, SOL_SOCKET, SO_PRIORITY, (const void *) &opt,
		      sizeof(opt)) < 0)
		fprintf(stderr, "Can't change socket priority!\n");
#endif

#if defined(IPTOS_LOWDELAY) && defined(IP_TOS) && (defined(SOL_IP) || \
    defined(IPPROTO_IP))
	{
		opt = IPTOS_LOWDELAY;
#ifdef SOL_IP
		if(setsockopt(h->sock, SOL_IP, IP_TOS, (const void *) &opt,
			      sizeof(opt)) < 0)
#else
		if(setsockopt(h->sock, IPPROTO_IP, IP_TOS, (const void *) &opt,
			      sizeof(opt)) < 0)
#endif
			fprintf(stderr, "Can't change socket TOS!\n");
	}
#endif

	/* Bind */
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(h->port);
	addr.sin_addr.s_addr = htonl(INADDR_ANY);
	if(bind(h->sock, (struct sockaddr *) &addr, sizeof(addr)) != 0)
		return -1;

	/* Open RTCP socket */
	if(attr->rtcp_port != 0)
	{
		/* Prepare socket address */
		memset(&h->rtcp_addr, 0, sizeof(h->rtcp_addr));
		h->rtcp_addr.sin_family = AF_INET;
		h->rtcp_addr.sin_port = htons(attr->rtcp_port);
		memcpy(&h->rtcp_addr.sin_addr, attr->ip, 4);

		/* Open a new socket if RTCP port different from RTP port */
		if(attr->rtcp_port != attr->port)
		{
			/* Open socket */
			if((h->rtcp_sock = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
				return 0;

			/* Force socket to bind */
			opt = 1;
			if(setsockopt(h->rtcp_sock, SOL_SOCKET, SO_REUSEADDR,
				      &opt, sizeof(opt)) < 0)
			{
				close(h->rtcp_sock);
				h->rtcp_sock = -1;
				return 0;
			}

			/* Bind */
			memset(&addr, 0, sizeof(addr));
			addr.sin_family = AF_INET;
			addr.sin_port = htons(attr->rtcp_port);
			addr.sin_addr.s_addr = htonl(INADDR_ANY);
			if(bind(h->rtcp_sock, (struct sockaddr *) &addr,
				sizeof(addr)) != 0)
			{
				close(h->rtcp_sock);
				h->rtcp_sock = -1;
				return 0;
			}
		}
	}

	return 0;
}

static uint16_t inline rtp_get_sequence(unsigned char *buffer)
{
	return (uint16_t) (((buffer[2] << 8) & 0xFF00) |
			    (buffer[3] & 0x00FF));
}

static uint8_t inline rtp_get_payload(unsigned char *buffer)
{
	return (uint8_t) (buffer[1] & 0x7F);
}

static uint32_t inline rtp_get_timestamp(unsigned char *buffer)
{
	uint32_t *u = (uint32_t*)buffer;
	return ntohl(u[1]);
}

static uint32_t inline rtp_get_ssrc(unsigned char *buffer)
{
	uint32_t *u = (uint32_t*)buffer;
	return ntohl(u[2]);
}

static void rtp_recv_rtcp(struct rtp_handle *h, unsigned char *buffer,
			  size_t size)
{
	struct sockaddr src_addr;
	socklen_t sockaddr_size;
	ssize_t len;

	if(h == NULL)
		return;

	/* Fill buffer with 0 */
	memset(buffer, 0, size);

	/* Get RTCP packet from socket */
	sockaddr_size = sizeof(src_addr);
	len = recvfrom(h->rtcp_sock, buffer, size, 0, &src_addr,
			&sockaddr_size);
	if(len <= 0)
		return;

	/* Verify packet size */
	if(len < 4)
		return;

	/* Verify protocol version (accept only version 2) */
	if((buffer[0] >> 6) != 2)
		return;

	/* Process packet with external callback */
	/* TODO: Basic RTCP packets are not yet supported */
	if(h->rtcp_cb)
		h->rtcp_cb(h->rtcp_data, buffer, len);
}

static ssize_t rtp_recv(struct rtp_handle *h, unsigned char *buffer,
			size_t size)
{
	struct sockaddr src_addr;
	socklen_t sockaddr_size;
	unsigned char payload;
	int len;

	if(h == NULL || buffer == NULL || size == 0)
		return -1;

	/* Fill packet with 0 */
	memset(buffer, 0, size);

	/* Get message from socket */
	sockaddr_size = sizeof(src_addr);
	len = recvfrom(h->sock, buffer, size, 0, &src_addr, &sockaddr_size);
	if(len <= 0)
		return len;

check:
	/* Verify packet size */
	if(len < 12)
	{
		fprintf(stderr, "RTP packet is too short!\n");
		return -1;
	}

	/* Verify protocol version (accept only version 2) */
	if((buffer[0] >> 6) != 2)
	{
		fprintf(stderr, "Unsupported RTP protocol version!\n");
		return -1;
	}

	/* Verify payload: RTCP */
	payload = rtp_get_payload(buffer);
	if(payload >= 72 && payload <= 76)
	{
		if(h->rtcp_cb !=  NULL)
			h->rtcp_cb(h->rtcp_data, buffer, len);
		return -1;
	}

	/* Custom process on packet if custom payload */
	if(payload != h->payload  && h->cust_cb != NULL)
	{
		len = h->cust_cb(h->cust_data, buffer, len);
		if(len <= 0)
			return -1;
		goto check; // FIXME: possible infinite loop!
	}

	/* Remove packet padding */
	if(buffer[0] & 0x20)
	{
		/* Get padded bytes number */
		uint8_t pads = buffer[len-1];
		if(pads == 0 && (pads + 12) > len)
			return -1;
		len -= pads;
	}

	return len;
}

static void rtp_check_resent(struct rtp_handle *h, uint16_t count)
{
	uint16_t mis_count = 0;
	uint16_t mis_seq = 0;
	uint16_t seq;
	uint16_t i;

	/* Get first values */
	i = (h->first_packet + h->resent_count) % h->max_packet_count;
	seq = h->first_seq + h->resent_count;
	if(h->resent_count >= count)
		return;
	count -= h->resent_count;

	/* Check all packets */
	while(count > 0)
	{
		/* Packet not received */
		if(h->packets[i].len == 0)
		{
			if(mis_count == 0)
				mis_seq = seq;
			mis_count++;
		}
		else if(mis_count > 0)
		{
			/* Call resent function */
			h->resent_cb(h->resent_data, mis_seq, mis_count);
			mis_count = 0;
		}

		/* Go to next packet */
		seq++;
		i++;
		if(i >= h->max_packet_count)
			i = 0;
		count--;
		h->resent_count++;
	}

	/* Call resent function a last time */
	if(mis_count > 0)
		h->resent_cb(h->resent_data, mis_seq, mis_count);

	return;
}

static void _rtp_flush(struct rtp_handle *h, uint16_t seq, uint32_t timestamp)
{
	int i;

	if(h == NULL)
		return;

	/* Reset packet list */
	for(i = 0; i < h->max_packet_count; i++)
	{
		h->packets[i].len = 0;
	}

	/* Reset expected values */
	h->packet_count = 0;
	h->resent_count = 0;
	h->first_packet = 0;
	h->filling = 1;
	h->first_seq = seq;
	h->first_ts = timestamp;

	/* Reset totally buffer if sequence number and timestamp are null */
	if(seq == 0 && timestamp == 0)
		h->ssrc = 0;
}

static int _rtp_put(struct rtp_handle *h, unsigned char *buffer, size_t len)
{
	int16_t delta;
	uint32_t ssrc;
	uint16_t seq;
	uint32_t ts;
	uint16_t i;

	/* Get RTP fields */
	ssrc = rtp_get_ssrc(buffer);
	seq = rtp_get_sequence(buffer);
	ts = rtp_get_timestamp(buffer);

	/* Initialized values : we assume SSRC cannot be equal to zero */
	if(h->ssrc == 0)
	{
		h->ssrc = ssrc;
		if(h->first_seq == 0)
			h->first_seq = seq;
		if(h->first_ts == 0)
			h->first_ts = ts;
	}
	else if(h->ssrc != ssrc)
	{
		/* Packet from another session: drop it */
		return -1;
	}

	/* Calculate difference between sequence number of received packet
	 * and first packet to be dequeued. If this difference is < 0, it means
	 * that it is a late packet and we drop it if distance between both
	 * sequence number is not bigger than misorder. In other case, if
	 * distance is bigger than misorder for a negative distance or distance
	 * is bigger than dropout for a positive value, we assume a sequence
	 * discontinuity happened and we resynchronize by resetting packet list.
	 * Note: overflow is handled by the signed type of delta.
	 */
	delta = seq - h->first_seq;
	if((delta < 0 && -delta > h->max_misorder) ||
	   (delta > 0 && delta > h->max_dropout))
	{
		/* Reset cache */
		_rtp_flush(h, seq, ts);
	}
	if(delta < 0)
	{
		/* Drop packet: arrived too late */
		return -1;
	}

	/* No space available in packet list */
	while(delta >= h->max_packet_count)
	{
		/* Drop older packets in list */
		h->packets[h->first_packet].len = 0;
		h->first_packet++;
		if(h->first_packet >= h->max_packet_count)
			h->first_packet = 0;
		h->first_seq++;
		h->discarded_count++;
		if(h->packet_count > 0)
		{
			h->packet_count--;
			if(h->packet_count == 0)
				h->filling = 1;
		}
		if(h->resent_count > 0)
			h->resent_count--;
		delta--;
	}

	/* Check packets which need to resent */
	if(delta >= h->resent_packet_count)
		rtp_check_resent(h, delta);

	/* Get position to write packet */
	i = (h->first_packet + delta) % h->max_packet_count;

	/* Duplicate packet */
	if(h->packets[i].len != 0)
	{
		/* Drop packet: already in buffer */
		return -1;
	}

	/* Copy packet into list */
	if(len > h->max_packet_size)
		len = h->max_packet_size;
	memcpy(h->packets[i].buffer, buffer, len);
	h->packets[i].len = len;

	/* Update packet count (latest - oldest)
	 * Note: overflow is handled by the signed type of delta.
	 */
	if((uint16_t)(h->first_seq + h->packet_count) <= seq)
	{
		h->packet_count = delta + 1;
		if(h->packet_count > h->delay_packet_count)
			h->filling = 0;
	}

	return 0;
}

static ssize_t rtp_get(struct rtp_handle *h, unsigned char *buffer, size_t size)
{
	unsigned char *p;
	size_t offset;
	ssize_t len;

	/* Jitter buffer is not full */
	if(h->filling)
		return RTP_NO_PACKET;

	/* Some packets has been discarded */
	if(h->discarded_count > 0)
	{
		h->discarded_count--;
		return RTP_DISCARDED_PACKET;
	}

	/* Get packet length */
	len = h->packets[h->first_packet].len;
	if(len > 0)
	{
		/* Get packet */
		p = h->packets[h->first_packet].buffer;

		/* Get data offset in packet */
		offset = 12 + ((p[0] &  0x0F) * 4);
		if(p[0] & 0x10)
		{
			/* Skip extension header */
			offset += 4;
			if(offset < len)
				offset += ((p[offset-2] << 8) & 0xFF00) |
					  (p[offset-1] & 0x00FF);
		}
		len -= offset;
		p += offset;

		/* Copy packet in buffer */
		if(len > size)
			len = size;
		memcpy(buffer, p, len);
	}
	else
	{
		/* Packet not received: lost packet */
		len = RTP_LOST_PACKET;
	}

	/* Update jitter packet count */
	h->packet_count--;
	if(h->packet_count == 0)
		h->filling = 1;
	if(h->resent_count > 0)
		h->resent_count--;

	/* Update jitter buffer position */
	h->packets[h->first_packet].len = 0;
	h->first_seq++;
	h->first_ts += 0;
	h->first_packet++;
	if(h->first_packet >= h->max_packet_count)
		h->first_packet = 0;

	return len;
}

ssize_t rtp_read(struct rtp_handle *h, unsigned char *buffer, size_t size)
{
	unsigned char packet[MAX_RTP_PACKET_SIZE];
	struct timeval tv = { 0, 0 };
	fd_set readfs;
	int max_sock;
	ssize_t len;
	int i;

	/* Empty UDP queue and fill RTP packet queue */
	max_sock = h->sock > h->rtcp_sock ? h->sock + 1 : h->rtcp_sock + 1;
	for(i = 0; i < MAX_RTP_RCV; i++)
	{
		/* Init sockets */
		FD_ZERO(&readfs);
		FD_SET(h->sock, &readfs);
		if(h->rtcp_sock > 0)
			FD_SET(h->rtcp_sock, &readfs);

		/* Check if packets are available on sockets */
		if(select(max_sock, &readfs, NULL, NULL, &tv) <= 0)
			break;

		/* Packets are available on RTCP socket */
		if(h->rtcp_sock > 0 && FD_ISSET(h->rtcp_sock, &readfs))
		{
			/* Get next packet from RTCP socket */
			rtp_recv_rtcp(h, packet, MAX_RTP_PACKET_SIZE);
		}

		/* Lock buffer access */
		pthread_mutex_lock(&h->mutex);

		/* Packets are available on RTP socket */
		if(FD_ISSET(h->sock, &readfs))
		{
			/* Get next packet from RTP socket */
			len = rtp_recv(h, packet, MAX_RTP_PACKET_SIZE);
			if(len <= 0)
				goto next;

			/* Drop packet after a flush */
			if(h->drop_count > 0)
			{
				h->drop_count--;
				goto next;
			}

			/* Add packet to jitter buffer */
			_rtp_put(h, packet, len);
		}
next:
		/* Unlock buffer access */
		pthread_mutex_unlock(&h->mutex);
	}

	/* Lock buffer access */
	pthread_mutex_lock(&h->mutex);

	/* Get next packet in jitter buffer */
	len = rtp_get(h, buffer, size);

	/* Unlock buffer access */
	pthread_mutex_unlock(&h->mutex);

	return len;
}

ssize_t rtp_send_rtcp(struct rtp_handle *h, unsigned char *buffer, size_t len)
{
	if(h == NULL || h->rtcp_sock < 0)
		return -1;

	/* Send packet */
	return sendto(h->rtcp_sock, buffer, len, 0,
		      (struct sockaddr *) &h->rtcp_addr, sizeof(h->rtcp_addr));
}

int rtp_put(struct rtp_handle *h, unsigned char *buffer, size_t len)
{
	int ret;

	/* Lock buffer access */
	pthread_mutex_lock(&h->mutex);

	/* Flush buffer */
	ret = _rtp_put(h, buffer, len);

	/* Unlock buffer access */
	pthread_mutex_unlock(&h->mutex);

	return ret;
}

void rtp_flush(struct rtp_handle *h, uint16_t seq, uint32_t timestamp)
{
	/* Lock buffer access */
	pthread_mutex_lock(&h->mutex);

	/* Flush buffer */
	_rtp_flush(h, seq, timestamp);

	/* Drop delay packet before buffering */
	if(seq == 0)
		h->drop_count = h->packet_count;

	/* Unlock buffer access */
	pthread_mutex_unlock(&h->mutex);
}

int rtp_close(struct rtp_handle *h)
{
	int i;

	if(h == NULL)
		return 0;

	/* Free packet list */
	if(h->packets != NULL)
	{
		for(i = 0; i < h->max_packet_count; i++)
		{
			if(h->packets[i].buffer != NULL)
				free(h->packets[i].buffer);
		}
		free(h->packets);
	}

	/* Close socket */
	if(h->sock > 0)
		close(h->sock);

	free(h);

	return 0;
}
