/*
 * file_format_taglib.cpp - An Audio File format parser and tag extractor (based
 *                          on taglib)
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
#include <string.h>

#include <taglib/tag.h>
#include <taglib/fileref.h>
#include <taglib/tbytevector.h>
#include <taglib/mpegfile.h>
#include <taglib/mp4file.h>
#include <taglib/id3v2tag.h>
#include <taglib/attachedpictureframe.h>

#include "file_format.h"

#define COPY_STRING(d, s) if(s != NULL && *s != 0) d = strdup(s);

#define ID3V2_PIC_TYPES 21

/* ID3v2 picture type preference: extracted from meta_engine module of VLC */
static const int id3v2_pic_type_pref[] = {
	0,  /* Other */
	5,  /* 32x32 PNG image that should be used as the file icon */
	4,  /* File icon of a different size or format. */
	20, /* Front cover image of the album. */
	19, /* Back cover image of the album. */
	13, /* Inside leaflet page of the album. */
	18, /* Image from the album itself. */
	17, /* Picture of the lead artist or soloist. */
	16, /* Picture of the artist or performer. */
	14, /* Picture of the conductor. */
	15, /* Picture of the band or orchestra. */
	9,  /* Picture of the composer. */
	8,  /* Picture of the lyricist or text writer. */
	7,  /* Picture of the recording location or studio. */
	10, /* Picture of the artists during recording. */
	11, /* Picture of the artists during performance. */
	6,  /* Picture from a movie or video related to the track. */
	1,  /* Picture of a large, coloured fish. */
	12, /* Illustration related to the track. */
	3,  /* Logo of the band or performer. */
	2   /* Logo of the publisher (record company). */
};

using namespace TagLib;

static void tag_get_total_track(struct file_format *f, const char *str)
{
	unsigned int track, total;

	if(sscanf(str, "%u/%u", &track, &total) == 2)
	{
		f->track = track;
		f->total_track = total;
	}
}

static void tag_read_from_id3v2(ID3v2::Tag *tag, struct file_format *f,
				int options)
{

	ID3v2::FrameList list;
	/* Picture part */
	ID3v2::AttachedPictureFrame *cur, *pic = NULL; 
	ID3v2::FrameList::Iterator i;
	ByteVector picture;
	int type, i_pref = -1;

#define SET(opt, key, dest) if(options & opt) \
    { \
        list = tag->frameListMap()[key]; \
        if(!list.isEmpty()) \
            COPY_STRING(dest, (*list.begin())->toString().toCString(true)); \
    }

	SET(TAG_COPYRIGHT, "TCOP", f->copyright);
	SET(TAG_ENCODED, "TENC", f->encoded);
	SET(TAG_LANGUAGE, "TLAN", f->language);
	SET(TAG_PUBLISHER, "TPUB", f->publisher);

#undef SET

	/* Get track total */
	if(options & TAG_TOTAL_TRACK)
	{
		list = tag->frameListMap()["TRCK"];
		if(!list.isEmpty())
		{
			tag_get_total_track(f,
				   (*list.begin())->toString().toCString(true));
		}
	}

	if(options & TAG_PICTURE)
	{
		/* Get embedded picture */
		list = tag->frameListMap()["APIC"];
		if(list.isEmpty())
			return;

		/* Get prefered picture */
		for(i = list.begin(); i != list.end(); i++)
		{
			/* Get picture object */
			cur = dynamic_cast<ID3v2::AttachedPictureFrame*>(*i);
			if(!cur)
				continue;

			/* Choose preference picture type */
			type = cur->type();
			if(type >= ID3V2_PIC_TYPES)
				type = 0;

			if(cur->picture().size() > 0 && type > i_pref)
			{
				pic = cur;
				i_pref = type;
			}
		}

		if(!pic)
			return;

		/* Get mime type */
		COPY_STRING(f->picture.mime,
			    pic->mimeType().toCString(true));

		/* Get description */
		COPY_STRING(f->picture.description,
			    pic->description().toCString(true));

		/* Get picture */
		picture = pic->picture();
		f->picture.size = picture.size();
		f->picture.data = (unsigned char *) malloc(picture.size());
		if(f->picture.data == NULL)
		{
			f->picture.size = 0;
			return;
		}

		/* Copy data */
		memcpy(f->picture.data, picture.data(), f->picture.size);
	}
}

static void tag_read_from_mp4(MP4::Tag *tag, struct file_format *f,
				int options)
{
	MP4::CoverArtList covr;

	if(tag->itemListMap().contains("covr"))
	{
		covr = tag->itemListMap()["covr"].toCoverArtList();

		/* Get mime type */
		if(covr[0].format() == MP4::CoverArt::PNG)
		{
			COPY_STRING(f->picture.mime, "image/png");
		}
		else
		{
			COPY_STRING(f->picture.mime, "image/jpeg");
		}

		/* Get picture */
		f->picture.size = covr[0].data().size();
		f->picture.data = (unsigned char *) malloc(f->picture.size);
		if(f->picture.data == NULL)
		{
			f->picture.size = 0;
			return;
		}

		/* Copy data */
		memcpy(f->picture.data, covr[0].data().data(), f->picture.size);
	}
}

struct file_format *file_format_parse(const char *filename, int options)
{
	AudioProperties *prop;
	struct file_format *f;
	FileRef file;
	Tag *tag;

	file = FileRef(filename);

	/* Allocate tag structure */
	f = (struct file_format*) calloc(1, sizeof(struct file_format));
	if(f == NULL)
		return NULL;

	if(file.isNull())
		return NULL;

	tag = file.tag();
	if(tag != NULL && !tag->isEmpty())
	{
		/* Fill structure with values */
		COPY_STRING(f->title, tag->title().toCString());
		COPY_STRING(f->artist, tag->artist().toCString());
		COPY_STRING(f->album, tag->album().toCString());
		COPY_STRING(f->comment, tag->comment().toCString());
		COPY_STRING(f->genre, tag->genre().toCString());
		f->track = tag->track();
		f->year = tag->year();
	}

	/* Get file properties */
	prop = file.audioProperties();
	if(prop != NULL)
	{
		f->length = prop->length();
		f->bitrate = prop->bitrate();
		f->samplerate = prop->sampleRate();
		f->channels = prop->channels();
	}

	/* Get file type */
	if(MPEG::File* mpeg = dynamic_cast<MPEG::File*>(file.file()))
	{
		f->type = FILE_FORMAT_MPEG;
		f->stream_offset = mpeg->firstFrameOffset();

		/* Get extended tags */
		if(options != 0 && mpeg->ID3v2Tag())
			tag_read_from_id3v2(mpeg->ID3v2Tag(), f, options);
	}
	else if(MP4::File *mp4 = dynamic_cast<MP4::File*>(file.file()))
	{
		f->type = FILE_FORMAT_AAC;

		/* Get extended tags */
		if(options != 0 && mp4->tag())
			tag_read_from_mp4(mp4->tag(), f, options);
	}
	else
		f->type = FILE_FORMAT_UNKNOWN;

	return f;
}

