/*
 * decoder.h - Decoder base
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

#ifndef _DECODER_H
#define _DECODER_H

/* Output status for decoder */
struct decoder_info {
	unsigned long used;		// Bytes consumed from input buffer
	unsigned long remaining;	// Remaining samples in decoder
};

enum {
	DECODER_ERROR_BUFLEN = -1,
	DECODER_ERROR_SYNC = -2
};

/* Generic handle */
struct decoder_handle {
	struct decoder *dec;
	int (*open)(struct decoder**, unsigned char *, size_t, unsigned long*,
		    unsigned char*);
	int (*decode)(struct decoder*, unsigned char*, size_t, unsigned char*,
		      size_t, struct decoder_info*);
	int (*close)(struct decoder*);
};

enum {
	CODEC_NO,
	CODEC_ALAC,
	CODEC_MP3,
	CODEC_AAC
};

int decoder_open(struct decoder_handle **handle, int codec,
		 unsigned char *buffer, size_t len, unsigned long *samplerate,
		 unsigned char *channels);
int decoder_decode(struct decoder_handle *h, unsigned char *in_buffer,
		   size_t in_size, unsigned char *out_buffer,
		   size_t out_size, struct decoder_info *info);
int decoder_close(struct decoder_handle *h);

#endif

