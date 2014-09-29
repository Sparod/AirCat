/*
 * meta.h - An Audio File format parser and tag extractor
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

#ifndef _META_H
#define _META_H

#ifdef __cplusplus
extern "C" {
#endif

enum {
	FILE_FORMAT_UNKNOWN = 0,
	FILE_FORMAT_MPEG,
	FILE_FORMAT_AAC,
	FILE_FORMAT_ALAC
};

enum {
	TAG_PICTURE = 1,
	TAG_TOTAL_TRACK = 2,
	TAG_COPYRIGHT = 4,
	TAG_ENCODED = 8,
	TAG_LANGUAGE = 16,
	TAG_PUBLISHER = 32,
	TAG_ALL = 0xFFFFF
};

struct tag_picture {
	unsigned char *data;
	char *description;
	char *mime;
	int size;
};

struct meta {
	/* Audio file type */
	int type;
	/* String values */
	char *title;
	char *artist;
	char *album;
	char *comment;
	char *genre;
	/* Integer value */
	int track;
	int total_track;
	int year;
	/* File properties */
	unsigned long length;
	unsigned int bitrate;
	unsigned long samplerate;
	unsigned int channels;
	size_t stream_offset;
	/* Picture tag */
	struct tag_picture picture;
	/* Extended tags */
	char *copyright;
	char *encoded;
	char *language;
	char *publisher;
};

extern struct meta *meta_parse(const char *filename, int options);

void meta_free(struct meta *m);

#ifdef __cplusplus
}
#endif

#endif

