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
#include "meta.h"
#include "fs.h"

/**
 * \struct struct demux_frame
 * \brief Structure to handle an encoded audio frame.
 *
 * demux_frame is a container to handle an audio frame in the ring buffer used
 * in Demuxer. This structure contains frame length, original frame position in
 * stream and its data. It must be allocated with sizeof(struct demux_frame) + 
 * frame length.
 */
struct demux_frame {
	off_t pos;		/*!< Original frame position in stream */
	size_t len;		/*!< Frame length */
	unsigned char data[0];	/*!< Frame data */
};

struct demux;

/* Demux module */
struct demux_module {
	int (*open)(struct demux **, struct fs_file *, size_t, unsigned long *,
		    unsigned char *);
	struct meta *(*get_meta)(struct demux *);
	int (*get_dec_config)(struct demux *, int *, const unsigned char **,
			      size_t *);
	ssize_t (*next_frame)(struct demux *, struct demux_frame *, size_t);
	unsigned long (*set_pos)(struct demux *, unsigned long);
	unsigned long (*calc_pos)(struct demux *, unsigned long, off_t *);
	void (*close)(struct demux *);
	const size_t min_buffer_size;
};

struct demux_handle;

/**
 * Open a new demuxer.
 */
int demux_open(struct demux_handle **handle, const char *uri,
	       unsigned long *samplerate, unsigned char *channels,
	       size_t cache_size, int use_thread);

/**
 * Get metadata extracted from stream.
 */
struct meta *demux_get_meta(struct demux_handle *h);

/**
 * Get decoder configuration extracted from stream.
 */
int demux_get_dec_config(struct demux_handle *h, int *codec,
			 const unsigned char **dec_config,
			 size_t *dec_config_size);

/**
 * Get current frame in demuxer.
 * Used bytes in current frame must be specified with demux_set_used_frame().
 * When all frame has been consummed, the next is returned.
*/
ssize_t demux_get_frame(struct demux_handle *h, unsigned char **buffer);

/**
 * Set used bytes in current frame.
 * This function must be used in combination with demux_get_frame().
 */
void demux_set_used_frame(struct demux_handle *h, ssize_t len);

/**
 * Get next frame in demuxer.
 * This function has the same behavior as demux_get_frame() but it force next
 * frame fetching at each call.
 */
ssize_t demux_get_next_frame(struct demux_handle *h, unsigned char **buffer);

/**
 * Set position in demuxer. The position is expressed in seconds.
 */
unsigned long demux_set_pos(struct demux_handle *h, unsigned long pos);

/**
 * Close demuxer.
 */
void demux_close(struct demux_handle *h);

#endif
