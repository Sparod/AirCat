/*
 * decoder_alac.h - A AppleLossless Decoder
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

#ifndef _DECODER_ALAC_H
#define _DECODER_ALAC_H

#include "decoder.h"

struct decoder;

int decoder_alac_open(struct decoder **dec, void *input_callback, void *user_data);
unsigned long decoder_alac_get_samplerate(struct decoder *dec);
unsigned char decoder_alac_get_channels(struct decoder *dec);
int decoder_alac_read(struct decoder *dec, unsigned char *buffer, size_t size);
int decoder_alac_close(struct decoder *dec);

#endif

