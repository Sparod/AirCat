/*
 * file_format.c - An Audio File format parser and tag extractor
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

#include "file_format.h"

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifndef HAVE_TAGLIB
struct file_format *file_format_parse(const char *filename, int options)
{
	return NULL;
}
#endif

#define FREE_STRING(str) if(str != NULL) free(str);

void file_format_free(struct file_format *f)
{
	if(f == NULL)
		return;

	/* Free string values */
	FREE_STRING(f->title);
	FREE_STRING(f->artist);
	FREE_STRING(f->album);
	FREE_STRING(f->comment);
	FREE_STRING(f->genre);
	FREE_STRING(f->copyright);
	FREE_STRING(f->encoded);
	FREE_STRING(f->language);
	FREE_STRING(f->publisher);

	/* Free picture */
	FREE_STRING(f->picture.description);
	FREE_STRING(f->picture.mime);
	if(f->picture.data != NULL)
		free(f->picture.data);

	/* Free structure */
	free(f);
}

