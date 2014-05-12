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

struct output_stream;

struct output_handle {
	struct output *out;
	int (*open)(struct output **, unsigned int samplerate, int nb_channel);
	int (*set_volume)(struct output *, unsigned int);
	unsigned int (*get_volume)(struct output *);
	struct output_stream *(*add_stream)(struct output *,
						   unsigned long, unsigned char,
						   unsigned long, void *,
						   void *);
	int (*play_stream)(struct output *, struct output_stream *);
	int (*pause_stream)(struct output *, struct output_stream *);
	int (*set_volume_stream)(struct output *, struct output_stream *,
				 unsigned int);
	unsigned int (*get_volume_stream)(struct output *,
					  struct output_stream *);
	int (*remove_stream)(struct output *, struct output_stream *);
	int (*close)(struct output *);
};

enum {
	OUTPUT_ALSA
};

int output_open(struct output_handle **handle, int module,
		unsigned int samplerate, int nb_channel);
int output_set_volume(struct output_handle *h, unsigned int volume);
unsigned int output_get_volume(struct output_handle *h);
struct output_stream *output_add_stream(struct output_handle *h, 
					unsigned long samplerate,
					unsigned char nb_channel,
					unsigned long cache,
					void *input_callback, void *user_data);
int output_play_stream(struct output_handle *h, struct output_stream *s);
int output_pause_stream(struct output_handle *h, struct output_stream *s);
int output_set_volume_stream(struct output_handle *h, struct output_stream *s,
			     unsigned int volume);
unsigned int output_get_volume_stream(struct output_handle *h,
				      struct output_stream *s);
int output_remove_stream(struct output_handle *h, struct output_stream *s);

int output_close(struct output_handle*);

#endif

