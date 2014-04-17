/*
 * tag.h - An Audio Tag parser
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

#ifndef _TAG_H
#define _TAG_H

#ifdef __cplusplus
extern "C" {
#endif

enum {
	TAG_PICTURE = 1,
	TAG_TOTAL_TRACK = 2,
	TAG_COPYRIGHT = 4,
	TAG_ENCODED = 8,
	TAG_LANGUAGE = 16,
	TAG_PUBLISHER = 32,
};

struct tag_picture {
	unsigned char *data;
	char *description;
	char *mime;
	int size;
};

struct tag {
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
	/* Picture tag */
	struct tag_picture picture;
	/* Extended tags */
	char *copyright;
	char *encoded;
	char *language;
	char *publisher;
};

extern struct tag *tag_read(const char *filename, int options);
void tag_free(struct tag *tag);

#ifdef __cplusplus
}
#endif

#endif

