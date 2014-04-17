/*
 * tag.c - An Audio Tag parser
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

#include <stdio.h>
#include <stdlib.h>

#include "tag.h"

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifndef HAVE_TAGLIB
struct tag *tag_read(const char *filename, int options)
{
	return NULL;
}
#endif

#define FREE_STRING(str) if(str != NULL) free(str);

void tag_free(struct tag *tag)
{
	if(tag == NULL)
		return;

	/* Free string values */
	FREE_STRING(tag->title);
	FREE_STRING(tag->artist);
	FREE_STRING(tag->album);
	FREE_STRING(tag->comment);
	FREE_STRING(tag->genre);
	FREE_STRING(tag->copyright);
	FREE_STRING(tag->encoded);
	FREE_STRING(tag->language);
	FREE_STRING(tag->publisher);

	/* Free picture */
	FREE_STRING(tag->picture.description);
	FREE_STRING(tag->picture.mime);
	if(tag->picture.data != NULL)
		free(tag->picture.data);

	/* Free structure */
	free(tag);
}

