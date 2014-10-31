/*
 * vring.h - A virtual ring buffer with direct access
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

#ifndef _VRING_H
#define _VRING_H

struct vring_handle;

/**
 * Open a new Virtual Ring buffer of buffer_size bytes with a direct read/write
 * of maximum max_rw_size bytes.
 * The allocated memory is buffer_size + max_rw_size bytes.
 * All functions are thread-safe but a read/write access must be unique
 * since a direct access is performed.
 */
int vring_open(struct vring_handle **handle, size_t buffer_size,
	       size_t max_rw_size);

/**
 * Get current data length in buffer.
 */
size_t vring_get_length(struct vring_handle *h);

/**
 * Read len bytes from Ring buffer. Every call return the same buffer until
 * vring_read_forward() is called to forward read position in buffer.
 * A position (or offset) can be specified with pos.
 */
ssize_t vring_read(struct vring_handle *h, unsigned char **buffer,
		   size_t len, size_t pos);

/**
 * Forward read position in buffer of len bytes.
 */
ssize_t vring_read_forward(struct vring_handle *h, size_t len);

/**
 * Return a buffer of a maximum of max_rw_size bytes. Every call return the same
 * buffer until vring_write_forward() is called to forward write position in
 * buffer.
 */
ssize_t vring_write(struct vring_handle *h, unsigned char **buffer);

/**
 * Forward write position in buffer of len bytes.
 */
ssize_t vring_write_forward(struct vring_handle *h, size_t len);

/**
 * Close Virtual Ring buffer and deallocate memory.
 */
void vring_close(struct vring_handle *h);

#endif

