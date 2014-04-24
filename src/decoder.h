/*
 * decoder.h - Decoder base
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

#ifndef _DECODER_H
#define _DECODER_H

/* Generic handle */
struct decoder_handle {
	struct decoder *dec;
	int (*open)(struct decoder**, void*, void*);
	unsigned long (*get_samplerate)(struct decoder*);
	unsigned char (*get_channels)(struct decoder*);
	unsigned long (*get_bitrate)(struct decoder*);
	int (*read)(struct decoder*, unsigned char*, size_t);
	int (*flush)(struct decoder*);
	int (*close)(struct decoder*);
};

enum {
	CODEC_NO,
	CODEC_ALAC,
	CODEC_MP3,
	CODEC_AAC
};


int decoder_open(struct decoder_handle **handle, int codec,
		 void *input_callback, void *user_data);
unsigned long decoder_get_samplerate(struct decoder_handle *h);
unsigned char decoder_get_channels(struct decoder_handle *h);
unsigned long decoder_get_bitrate(struct decoder_handle *h);
int decoder_read(struct decoder_handle *h, unsigned char *buffer, size_t size);
int decoder_flush(struct decoder_handle *h);
int decoder_close(struct decoder_handle *h);

#endif

