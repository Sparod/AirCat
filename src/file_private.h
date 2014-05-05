/*
 * file_private.h - Private structure and functions for File module
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

#ifndef _FILE_PRIVATE_H
#define _FILE_PRIVATE_H

#include "file.h"

#define BUFFER_SIZE 8192

struct file_demux {
	int (*init)(struct file_handle *, unsigned long *, unsigned char *);
	int (*get_next_frame)(struct file_handle *);
	int (*set_pos)(struct file_handle *, unsigned long);
	void (*free)(struct file_handle *);
};

struct file_handle {
	/* File */
	int fd;
	size_t file_pos;
	size_t file_size;
	/* Input buffer */
	unsigned char in_buffer[BUFFER_SIZE];
	unsigned long in_size;
	/* Tags and file format */
	struct file_format *format;
	/* Demuxer */
	struct file_demux *demux;
	void *demux_data;
	/* Audio decoder */
	struct decoder_handle *dec;
	unsigned char *decoder_config;
	unsigned long decoder_config_size;
	unsigned long pcm_remaining;
	/* File and stream status */
	unsigned long pos;
	struct file_frame *frames;
	/* File properties */
	unsigned long samplerate;
	unsigned long channels;
	unsigned int bitrate;
	unsigned long length;
	/* Mutex for thread safe status */
	pthread_mutex_t mutex;
	pthread_mutex_t flush_mutex;
};

/*
 * Read len bytes in file and fill input buffer with it.
 * If len equal to 0, all allocated buffer is filled.
 */
ssize_t file_read_input(struct file_handle *h, size_t len);

/*
 * Read len bytes more in file and append to end of input buffer.
 * If len equal to 0, all allocated buffer is filled.
 */
ssize_t file_complete_input(struct file_handle *h, size_t len);

/*
 * Move input buffer to fit with file position passed. Input buffer position is
 * the moved and file position is updated.
 * /!\ Input buffer is not filled with any data. A call to file_read_input() or
 *     to file_complete_input() must be done is more data is needed.
 */
int file_seek_input(struct file_handle *h, unsigned long pos, int whence);

/* MP3 file demuxer */
struct file_demux file_mp3_demux;

/* MP4 file demuxer */
struct file_demux file_mp4_demux;

#endif

