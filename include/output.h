/*
 * output.h - Audio output module
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

#ifndef _OUTPUT_H
#define _OUTPUT_H

#define OUTPUT_VOLUME_MAX 65536

struct output_handle;
struct output_stream_handle;

/* Add/Remove output stream */
struct output_stream_handle *output_add_stream(struct output_handle *h,
					       const char *name,
					       unsigned long samplerate,
					       unsigned char channels,
					       unsigned long cache,
					       int use_cache_thread,
					       void *input_callback,
					       void *user_data);
void output_remove_stream(struct output_handle *h,
			  struct output_stream_handle *s);

/* Play/Pause output stream */
int output_play_stream(struct output_handle *h, struct output_stream_handle *s);
int output_pause_stream(struct output_handle *h,
			struct output_stream_handle *s);

/* Volume output stream control */
int output_set_volume_stream(struct output_handle *h,
			     struct output_stream_handle *s,
			     unsigned int volume);
unsigned int output_get_volume_stream(struct output_handle *h,
				      struct output_stream_handle *s);

#endif
