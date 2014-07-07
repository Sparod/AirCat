/*
 * format.h - Audio output module
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

#ifndef _FORMAT_H
#define _FORMAT_H

#define A_FORMAT_INIT {0, 0}

struct a_format {
	unsigned long samplerate;
	unsigned char channels;
};

typedef int (*a_read_cb) (void *user_data, unsigned char *buffer,
			  size_t size, struct a_format *fmt);

#endif

