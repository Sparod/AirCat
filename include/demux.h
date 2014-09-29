/*
 * demux.h - An input demuxer
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

#ifndef _DEMUX_H
#define _DEMUX_H

#include "format.h"
#include "stream.h"
#include "meta.h"

/* Generic handle */
struct demux_handle {
	struct demux *demux;
	int (*open)(struct demux **, struct stream_handle *, unsigned long *,
		    unsigned char *);
	struct meta *(*get_meta)(struct demux *);
	int (*get_dec_config)(struct demux *, int *, const unsigned char **,
			      size_t *);
	ssize_t (*next_frame)(struct demux *);
	void (*set_used)(struct demux *, size_t);
	unsigned long (*set_pos)(struct demux *, unsigned long);
	void (*close)(struct demux *);
};

int demux_open(struct demux_handle **handle, struct stream_handle *stream,
	       unsigned long *samplerate, unsigned char *channels);
struct meta *demux_get_meta(struct demux_handle *h);
int demux_get_dec_config(struct demux_handle *h, int *codec,
			 const unsigned char **dec_config,
			 size_t *dec_config_size);
ssize_t demux_next_frame(struct demux_handle *h);
void demux_set_used(struct demux_handle *h, size_t len);
unsigned long demux_set_pos(struct demux_handle *h, unsigned long pos);
void demux_close(struct demux_handle *h);

#endif
