/*
 * resample.h - Samplerate and channel converter based on libsoxr
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

#ifndef _RESAMPLE_H
#define _RESAMPLE_H

struct resample_handle;

int resample_open(struct resample_handle **h, unsigned long in_samplerate,
		  unsigned char in_nb_channel, unsigned long out_samplerate,
		  unsigned char out_channel, void *input_callback,
		  void *user_data);
int resample_read(struct resample_handle *h, unsigned char *buffer,
		  size_t size);
int resample_close(struct resample_handle *h);

#endif

