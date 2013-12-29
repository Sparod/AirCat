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

#include <inttypes.h>
#include <sys/socket.h>
#include <sys/types.h>

struct rtp_handle;

int rtp_open(struct rtp_handle **h, unsigned int port, unsigned int cache_size, unsigned int cache_lost, unsigned long ssrc, unsigned char payload, unsigned int timeout);
int rtp_read(struct rtp_handle *h, unsigned char *buffer, size_t len);
void rtp_flush(struct rtp_handle *h, unsigned int seq);
int rtp_close(struct rtp_handle *h);

//TODO: Callback for RTCP

#endif
