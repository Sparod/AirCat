/*
 * decoder_aac.h - A AAC Decoder based on faad2
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

#ifndef _DECODER_AAC_H
#define _DECODER_AAC_H

#include "decoder.h"

struct decoder;

int decoder_aac_open(struct decoder **dec, void *input_callback, void *user_data);
int decoder_aac_read(struct decoder *dec, float *buffer, size_t size);
int decoder_aac_close(struct decoder *dec);

#endif

