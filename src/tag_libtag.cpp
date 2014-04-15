/*
 * tag_libtag.cpp - An Audio Tag parser (based on libtag)
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

#include <string.h>

#include <taglib/tag.h>
#include <taglib/fileref.h>

#include "tag.h"

#define COPY_STRING(d, s) if(s != NULL && *s != 0) d = strdup(s);

struct tag *tag_read(const char *filename, int options)
{
	struct tag *meta;
	TagLib::FileRef file;
	TagLib::Tag *tag;

	file = TagLib::FileRef(filename);

	if(file.isNull())
		return NULL;
	if(!file.tag() || file.tag()->isEmpty())
		return NULL;

	tag = file.tag();

	/* Allocate tag structure */
	meta = (struct tag*) calloc(1, sizeof(struct tag));
	if(meta == NULL)
		return NULL;

	/* Fill structure with values */
	COPY_STRING(meta->title, tag->title().toCString());
	COPY_STRING(meta->artist, tag->artist().toCString());
	COPY_STRING(meta->album, tag->album().toCString());
	COPY_STRING(meta->comment, tag->comment().toCString());
	COPY_STRING(meta->genre, tag->genre().toCString());
	meta->track = tag->track();
	meta->year = tag->year();

	return meta;
}

