/*
 * rtp.h - A Tiny RTP Receiver
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
#ifndef TINY_RTP_H
#define TINY_RTP_H

struct rtp_attr{
	/* RTP config */
	unsigned int port;
	unsigned long ssrc;
	unsigned char payload;
	/* Cache settings */
	unsigned int cache_size;
	unsigned int cache_resent;
	unsigned int cache_lost;
	/* Resent callback */
	void (*resent_cb)(void *, unsigned int, unsigned int);
	void *resent_data;
	/* Timeout for recv() */
	unsigned int timeout;
};

struct rtp_handle;

int rtp_open(struct rtp_handle **h, struct rtp_attr *attr);
int rtp_read(struct rtp_handle *h, unsigned char *buffer, size_t len);
void rtp_flush(struct rtp_handle *h, unsigned int seq);
int rtp_close(struct rtp_handle *h);

#endif
