/*
 * alsa.h - Alsa output
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

#ifndef _ALSA_OUTPUT_H
#define _ALSA_OUTPUT_H

struct alsa_handle;

int alsa_open(struct alsa_handle **h, unsigned long samplerate, unsigned char nb_channel, unsigned long latency, void *input_callback, void *user_data);
int alsa_play(struct alsa_handle *h);
int alsa_stop(struct alsa_handle *h);
int alsa_close(struct alsa_handle *h);


#endif

