/*
 * meta_taglib.cpp - An Audio File format parser and tag extractor (based on
 *                   taglib)
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

#include <taglib/asffile.h>
#include <taglib/mpegfile.h>
#include <taglib/vorbisfile.h>
#include <taglib/flacfile.h>
#include <taglib/oggflacfile.h>
#include <taglib/mpcfile.h>
#include <taglib/mp4file.h>
#include <taglib/wavpackfile.h>
#include <taglib/speexfile.h>
#include <taglib/trueaudiofile.h>
#include <taglib/aifffile.h>
#include <taglib/wavfile.h>
#include <taglib/apefile.h>

#if (TAGLIB_MAJOR_VERSION >= 1) &&  (TAGLIB_MINOR_VERSION >= 9)
#include <taglib/opusfile.h>
#include <taglib/modfile.h>
#include <taglib/s3mfile.h>
#include <taglib/itfile.h>
#include <taglib/xmfile.h>
#endif

#include <taglib/id3v2tag.h>
#include <taglib/attachedpictureframe.h>

#include "meta_taglib_file.h"
#include "meta.h"

#define COPY_STRING(d, s) str = s; if(str != NULL && *str != 0) d = strdup(str);

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

static void tag_get_total_track(struct meta *m, const char *str)
{
	unsigned int track, total;

	if(sscanf(str, "%u/%u", &track, &total) == 2)
	{
		m->track = track;
		m->total_track = total;
	}
}

static void tag_read_from_id3v2(ID3v2::Tag *tag, struct meta *m, int options)
{

	ID3v2::FrameList list;
	/* Picture part */
	ID3v2::AttachedPictureFrame *cur, *pic = NULL; 
	ID3v2::FrameList::Iterator i;
	ByteVector picture;
	int type, i_pref = -1;
	const char *str;

#define SET(opt, key, dest) if(options & opt) \
    { \
        list = tag->frameListMap()[key]; \
        if(!list.isEmpty()) \
            COPY_STRING(dest, (*list.begin())->toString().to8Bit().c_str()); \
    }

	SET(TAG_COPYRIGHT, "TCOP", m->copyright);
	SET(TAG_ENCODED, "TENC", m->encoded);
	SET(TAG_LANGUAGE, "TLAN", m->language);
	SET(TAG_PUBLISHER, "TPUB", m->publisher);

#undef SET

	/* Get track total */
	if(options & TAG_TOTAL_TRACK)
	{
		list = tag->frameListMap()["TRCK"];
		if(!list.isEmpty())
		{
			tag_get_total_track(m,
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
		COPY_STRING(m->picture.mime,
			    pic->mimeType().to8Bit().c_str());

		/* Get description */
		COPY_STRING(m->picture.description,
			    pic->description().to8Bit().c_str());

		/* Get picture */
		picture = pic->picture();
		m->picture.size = picture.size();
		m->picture.data = (unsigned char *) malloc(picture.size());
		if(m->picture.data == NULL)
		{
			m->picture.size = 0;
			return;
		}

		/* Copy data */
		memcpy(m->picture.data, picture.data(), m->picture.size);
	}
}

static void tag_read_from_mp4(MP4::Tag *tag, struct meta *m, int options)
{
	MP4::CoverArtList covr;
	const char *str;

	if(tag->itemListMap().contains("covr"))
	{
		covr = tag->itemListMap()["covr"].toCoverArtList();

		/* Get mime type */
		if(covr[0].format() == MP4::CoverArt::PNG)
		{
			COPY_STRING(m->picture.mime, "image/png");
		}
		else
		{
			COPY_STRING(m->picture.mime, "image/jpeg");
		}

		/* Get picture */
		m->picture.size = covr[0].data().size();
		m->picture.data = (unsigned char *) malloc(m->picture.size);
		if(m->picture.data == NULL)
		{
			m->picture.size = 0;
			return;
		}

		/* Copy data */
		memcpy(m->picture.data, covr[0].data().data(), m->picture.size);
	}
}

struct meta *meta_parse(const char *filename, int options)
{
	AudioProperties *prop;
	struct meta *m = NULL;
	const char *str;
	File *file;
	Tag *tag;

#if (TAGLIB_MAJOR_VERSION >= 1) &&  (TAGLIB_MINOR_VERSION >= 9)
	/* Open file */
	std::string strFileName = filename;
	MetaTaglibFile *stream = new MetaTaglibFile(strFileName, true);

	/* File doesn't exist */
	if(!stream->isOpen())
		return NULL;

	/* Get file extension */
	std::string ext;
	const int pos = strFileName.rfind(".");
	if(pos != -1)
		ext = strFileName.substr(pos + 1);
	std::transform(ext.begin(), ext.end(), ext.begin(), ::toupper);

	/* Parse file */
	if(ext == "MP3")
		file = new MPEG::File(stream, ID3v2::FrameFactory::instance());
	else if(ext == "OGG")
		file = new Ogg::Vorbis::File(stream);
	else if(ext == "OGA")
	{
		file = new Ogg::FLAC::File(stream);
		if(!file->isValid())
		{
			delete file;
			file = new Ogg::Vorbis::File(stream);
		}
	}
	else if(ext == "FLAC")
		file = new FLAC::File(stream, ID3v2::FrameFactory::instance());
	else if(ext == "MPC")
		file = new MPC::File(stream);
	else if(ext == "WV")
		file = new WavPack::File(stream);
	else if(ext == "SPX")
		file = new Ogg::Speex::File(stream);
	else if(ext == "OPUS")
		file = new Ogg::Opus::File(stream);
	else if(ext == "TTA")
		file = new TrueAudio::File(stream);
	else if(ext == "M4A" || ext == "M4R" || ext == "M4B" || ext == "M4P" ||
	   ext == "MP4" || ext == "3G2")
		file = new MP4::File(stream);
	else if(ext == "WMA" || ext == "ASF")
		file = new ASF::File(stream);
	else if(ext == "AIF" || ext == "AIFF")
		file = new RIFF::AIFF::File(stream);
	else if(ext == "WAV")
		file = new RIFF::WAV::File(stream);
	else if(ext == "APE")
		file = new APE::File(stream);
	else if(ext == "MOD" || ext == "MODULE" || ext == "NST" || ext == "WOW")
		file = new Mod::File(stream);
	else if(ext == "S3M")
		file = new S3M::File(stream);
	else if(ext == "IT")
		file = new IT::File(stream);
	else if(ext == "XM")
		file = new XM::File(stream);

	/* File cannot be openned */
	if(!file)
		goto end;

	/* Get tags and propeties */
	tag = file->tag();
	prop = file->audioProperties();
#else
	/* Open file */
	FileRef f = FileRef(filename);

	/* File doesn't exist */
	if(f.isNull())
		return NULL;

	/* Get tags and propeties */
	tag = f.tag();
	prop = f.audioProperties();
	file = f.file();
#endif

	/* Allocate tag structure */
	m = (struct meta *) calloc(1, sizeof(struct meta));
	if(m == NULL)
		goto end;

	/* Get tags */
	if(tag != NULL && !tag->isEmpty())
	{
		/* Fill structure with values */
		COPY_STRING(m->title, tag->title().to8Bit().c_str());
		COPY_STRING(m->artist, tag->artist().to8Bit().c_str());
		COPY_STRING(m->album, tag->album().to8Bit().c_str());
		COPY_STRING(m->comment, tag->comment().to8Bit().c_str());
		COPY_STRING(m->genre, tag->genre().to8Bit().c_str());
		m->track = tag->track();
		m->year = tag->year();
	}

	/* Get file properties */
	if(prop != NULL)
	{
		m->length = prop->length();
		m->bitrate = prop->bitrate();
		m->samplerate = prop->sampleRate();
		m->channels = prop->channels();
	}

	/* Get file type */
	if(MPEG::File* mpeg = dynamic_cast<MPEG::File*>(file))
	{
		m->type = FILE_FORMAT_MPEG;
		m->stream_offset = mpeg->firstFrameOffset();

		/* Get extended tags */
		if(options != 0 && mpeg->ID3v2Tag())
			tag_read_from_id3v2(mpeg->ID3v2Tag(), m, options);
	}
	else if(MP4::File *mp4 = dynamic_cast<MP4::File*>(file))
	{
		m->type = FILE_FORMAT_AAC;

		/* Get extended tags */
		if(options != 0 && mp4->tag())
			tag_read_from_mp4(mp4->tag(), m, options);
	}
	else
		m->type = FILE_FORMAT_UNKNOWN;

end:
#if (TAGLIB_MAJOR_VERSION >= 1) &&  (TAGLIB_MINOR_VERSION >= 9)
	if(file)
		delete file;
	if(stream)
		delete stream;
#endif
	return m;
}

