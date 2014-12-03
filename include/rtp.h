/*
 * rtp.h - A Tiny RTP Receiver
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
#ifndef TINY_RTP_H
#define TINY_RTP_H

/* Return code for rtp_read():
 *  - RTP_NO_PACKET: no packet is available, RTP module is filling its buffer,
 *  - RTP_LOST_PACKET: requested packet is never arrived: lost packet,
 *  - RTP_DISCARDED_PACKET: requested packet has been discarded because delay
 *                          has been changed during operation, or buffer has not
 *                          been empty on time.
 */
#define RTP_NO_PACKET 0
#define RTP_LOST_PACKET -1
#define RTP_DISCARDED_PACKET -2

struct rtp_attr {
	/* RTP configuration */
	unsigned char *ip;
	unsigned int port;
	unsigned int rtcp_port;
	/* Expected packet configuration */
	unsigned long ssrc;		// SSRC to handle: 0 if unknown
	unsigned int seq;		// First sequence number: 0 if unknown
	unsigned long timestamp;	// First timestamp: 0 if unknown
	unsigned char payload;		// Payload to handle
	/* Jitter buffer configuration */
	size_t max_packet_size;		// Maximum packet size to save
	uint16_t pool_packet_count;	// Pool of allocated packets
	uint16_t delay_packet_count;	// Delay of jitter buffer (fixed in
					//  packet count)
	unsigned char resent_ratio;	// Packet resent threshold event (%)
					//  max is 80% of delay_packet_count.
	unsigned char fill_ratio;	/*!< Packet count threshold (in %) 
					     under which the jitter buffer must 
					     be refilled. Maximum ratio is 80% 
					     of delay_packet_count. */
	/* RTP module params */
	uint16_t max_misorder;		// Reset threshold for late packets:
					//  0 for default value (= 100)
	uint16_t max_dropout;		// Reset threshold for big packet jump:
					//  0 for default value (= 3000)
	/* RTCP packet callback:
	 *   This function is called when received RTCP packet type is unknown.
	 *   It means all RTCP packets not defined in RFC 3550 are transmitted
	 *   to this function. So, RTCP packets SR, RR, SDES, BYE and APP are
	 *   handled by the module.
	 */
	void (*rtcp_cb)(void *, unsigned char *, size_t);
	void *rtcp_data;
	/* RTP packet callback:
	 *   This function is called when payload of received RTP packet differs
	 *   from the expected payload.
	 */
	size_t (*cust_cb)(void *, unsigned char *, size_t);
	void *cust_data;
	/* Resent callback:
	 *   This function is called when a packet has not been received after
	 *   the `resent` delay.
	 */
	void (*resent_cb)(void *, unsigned int, unsigned int);
	void *resent_data;
};

struct rtp_handle;

int rtp_open(struct rtp_handle **h, struct rtp_attr *attr);
uint16_t rtp_set_delay_packet(struct rtp_handle *h, uint16_t delay);
ssize_t rtp_read(struct rtp_handle *h, unsigned char *buffer, size_t len);
int rtp_put(struct rtp_handle *h, unsigned char *buffer, size_t len);
ssize_t rtp_send_rtcp(struct rtp_handle *h, unsigned char *buffer, size_t len);
void rtp_flush(struct rtp_handle *h, uint16_t seq, uint32_t ts);
int rtp_close(struct rtp_handle *h);

#endif
