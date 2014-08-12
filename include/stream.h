/*
 * stream.h - An input stream
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

#ifndef _STREAM_H
#define _STREAM_H

struct stream_handle;

int stream_open(struct stream_handle **handle, const char *uri,
		unsigned char *buffer, size_t size);
const unsigned char *stream_get_buffer(struct stream_handle *h);
size_t stream_get_buffer_size(struct stream_handle *h);
const char *stream_get_content_type(struct stream_handle *h);
int stream_is_seekable(struct stream_handle *h);
void stream_close(struct stream_handle *h);

/*
 * Read len bytes in stream and fill input buffer with it.
 * If len equal to 0, all allocated buffer is filled.
 */
#define stream_read(h, l) stream_read_timeout(h, l, -1)
ssize_t stream_read_timeout(struct stream_handle *h, size_t len, long timeout);

/*
 * Read len bytes more in stream and append to input buffer.
 * If len equal to 0, all allocated buffer is filled.
 */
#define stream_complete(h, l) stream_complete_timeout(h, l, -1)
ssize_t stream_complete_timeout(struct stream_handle *h, size_t len,
				long timeout);

/*
 * Move input buffer to fit with stream position passed. Input buffer position
 * is then moved and stream position is updated.
 * /!\ Input buffer is not filled with any data. A call to stream_read() or
 *     to stream_complete() must be done if more data is needed.
 */
long stream_seek(struct stream_handle *h, long pos, int whence);

/*
 * Get current position in stream.
 */
long stream_get_pos(struct stream_handle *h);

/*
 * Get stream length.
 */
long stream_get_size(struct stream_handle *h);

/*
 * Get current available data length in input buffer (in bytes).
 */
long stream_get_len(struct stream_handle *h);

#endif
