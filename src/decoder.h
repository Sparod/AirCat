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
struct decoder_handle;

enum {
	CODEC_NO,
	CODEC_MP3,
	CODEC_AAC
};

int decoder_open(struct decoder_handle **h, int codec, void *input_callback, void *user_data);
int decoder_read(struct decoder_handle *h, float *buffer, size_t size);
int decoder_close(struct decoder_handle *h);

#endif

