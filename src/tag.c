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

void tag_free(struct tag *tag)
{
	if(tag == NULL)
		return;

	/* Free string values */
	if(tag->title != NULL)
		free(tag->title);
	if(tag->artist != NULL)
		free(tag->artist);
	if(tag->album != NULL)
		free(tag->album);
	if(tag->comment != NULL)
		free(tag->comment);
	if(tag->genre != NULL)
		free(tag->genre);

	/* Free binary values */
	if(tag->picture != NULL)
		free(tag->picture);

	/* Free structure */
	free(tag);
}

