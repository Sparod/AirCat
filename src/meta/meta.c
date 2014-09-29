/*
 * meta.c - An Audio File format parser and tag extractor
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

#include "meta.h"

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifndef HAVE_TAGLIB
struct meta *meta_parse(const char *filename, int options)
{
	return NULL;
}
#endif

#define FREE_STRING(str) if(str != NULL) free(str);

void meta_free(struct meta *m)
{
	if(m == NULL)
		return;

	/* Free string values */
	FREE_STRING(m->title);
	FREE_STRING(m->artist);
	FREE_STRING(m->album);
	FREE_STRING(m->comment);
	FREE_STRING(m->genre);
	FREE_STRING(m->copyright);
	FREE_STRING(m->encoded);
	FREE_STRING(m->language);
	FREE_STRING(m->publisher);

	/* Free picture */
	FREE_STRING(m->picture.description);
	FREE_STRING(m->picture.mime);
	if(m->picture.data != NULL)
		free(m->picture.data);

	/* Free structure */
	free(m);
}

