/*
 * demux_mp4.c - An MP4 demuxer
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
#include <string.h>
#include <stdint.h>
#include <strings.h>

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "demux.h"
#include "id3.h"

#define ATOM_CHECK(b, a) memcmp(&b[4], a, 4)
#define ATOM_READ16(b) ((b)[0] << 8) | (b)[1]
#define ATOM_READ32(b) ((b)[0] << 24) | ((b)[1] << 16) | ((b)[2] << 8) | \
		       (b)[3]
#define ATOM_READ64(b) ((unsigned long long)(b)[0] << 56) | \
		       ((unsigned long long)(b)[1] << 48) | \
		       ((unsigned long long)(b)[2] << 40) | \
		       ((unsigned long long)(b)[3] << 32) | \
		       ((unsigned long long)(b)[4] << 24) | \
		       ((unsigned long long)(b)[5] << 16) | \
		       ((unsigned long long)(b)[6] << 8) | \
		       (unsigned long long)(b)[7]
#define ATOM_LEN(b) (ATOM_READ32(b))

struct demux {
	/* Stream */
	struct stream_handle *stream;
	const unsigned char *buffer;
	unsigned long buffer_size;
	unsigned long size;
	/* Stream meta */
	struct meta meta;
	/* MP4 Atoms */
	/* mdhd atom */
	int32_t mdhd_time_scale;
	int64_t mdhd_duration;
	/* stsz atom */
	int32_t stsz_sample_size;
	int32_t stsz_sample_count;
	int32_t *stsz_table;
	/* stco atom */
	int32_t stco_entry_count;
	int32_t *stco_chunk_offset;
	/* stsc atom */
	int32_t stsc_entry_count;
	int32_t *stsc_first_chunk;
	int32_t *stsc_samples_per_chunk;
	int32_t *stsc_sample_desc_index;
	/* stts atom */
	int32_t stts_entry_count;
	int32_t *stts_sample_count;
	int32_t *stts_sample_delta;
	/* mp4a atom */
	int32_t mp4a_channel_count;
	int32_t mp4a_sample_size;
	unsigned long mp4a_samplerate;
	/* esds atom */
	unsigned char *esds_buffer;
	unsigned long esds_size;
	int32_t esds_audio_type;
	uint32_t esds_max_bitrate;
	uint32_t esds_avg_bitrate;
	/* Demux variables */
	/* Total samples */
	unsigned long num_samples;
	/* An Mp4a track found flag */
	int track_found;
	/* Current sample/chunk */
	unsigned int cur_sample_size;
	unsigned long cur_sample;
	unsigned long cur_chunk_sample;
	unsigned long cur_chunk_idx;
	unsigned long cur_chunk;
	unsigned long cur_offset;
	/* Meta data */
	char *title;
	char *artist;
	char *album;
	char *comment;
	char *genre;
	char *year;
	int track;
	int total_track;
	unsigned char *pic;
	size_t pic_len;
	char *pic_mime;
};

static int demux_mp4_find_chunk(struct demux *d, unsigned long sample,
				unsigned long *chunk, unsigned long *chunk_idx,
				unsigned long *chunk_sample)
{
	unsigned long nb_chunk_sample = 0;
	unsigned long cur_sample = 0;
	unsigned long cur_chunk = 1;
	unsigned long nb_sample = 0;
	unsigned long nb_chunk = 0;
	unsigned long i = 0;

	/* Find chunk and sample in chunk */
	for(i = 0; i < d->stsc_entry_count; i++)
	{
		if(i < d->stsc_entry_count-1)
			nb_chunk = d->stsc_first_chunk[i+1] - 
				   d->stsc_first_chunk[i];
		else
			nb_chunk = 1;

		nb_chunk_sample = d->stsc_samples_per_chunk[i];
		nb_sample = nb_chunk * nb_chunk_sample;
		if(cur_sample + nb_sample > sample)
			break;

		cur_sample += nb_sample;
		cur_chunk += nb_chunk;
	}

	*chunk = cur_chunk + ((sample - cur_sample) / nb_chunk_sample) - 1;
	*chunk_sample = sample - (*chunk + 1 - cur_chunk) * nb_chunk_sample - 
			cur_sample;
	*chunk_idx = i;

	if(*chunk >= d->stsc_first_chunk[d->stsc_entry_count-1])
		return -1;

	return 0;
}

static long demux_mp4_find_sample(struct demux *d, unsigned long pos,
				  unsigned long *skip)
{
	unsigned long s_count, s_delta;
	unsigned long sample = 0;
	unsigned long count = 0;
	unsigned long delta;
	unsigned int i;

	/* Find sample with stts table */
	for(i = 0; i < d->stts_entry_count; i++)
	{
		s_count = d->stts_sample_count[i];
		s_delta = d->stts_sample_delta[i];
		delta = s_count * s_delta;

		if(delta + count > pos)
		{
			if(skip != NULL)
				*skip = ((pos - count) % s_delta);
			return sample + ((pos - count) / s_delta);
		}
		else
			count += delta;
		sample += s_count;
	}

	return -1;
}

static void demux_mp4_parse_mdhd(struct demux *d)
{
	unsigned long version;
	unsigned long size;

	/* Get size */
	size = ATOM_LEN(d->buffer);

	/* Get Version */
	stream_read(d->stream, 4);
	version = ATOM_READ32(d->buffer);

	/* Go to first entry */
	size -= 12;

	if(version == 1)
	{
		stream_read(d->stream, 28);
		d->mdhd_time_scale = ATOM_READ32(&d->buffer[16]);
		d->mdhd_duration = ATOM_READ64(&d->buffer[20]);
	}
	else
	{
		stream_read(d->stream, 16);
		d->mdhd_time_scale = ATOM_READ32(&d->buffer[8]);
		d->mdhd_duration = ATOM_READ32(&d->buffer[12]);
	}

	/* Go to next atom */
	stream_seek(d->stream, size, SEEK_CUR);
}

static uint32_t demux_mp4_read_len(struct demux *d, unsigned long *count)
{
	uint32_t len = 0;
	uint8_t i, b = 0x80;

	for(i = 0; b & 0x80 && i < 4; i++)
	{
		stream_read(d->stream, 1);
		(*count) -= 1;
		b = d->buffer[0];
		len = (len << 7) | (b & 0x7F);
	}

	return len;
}

static int demux_mp4_parse_esds(struct demux *d)
{
	unsigned long atom_size;
	unsigned char tag;

	/* Get size */
	atom_size = ATOM_LEN(d->buffer);

	/* Skip version and flags */
	stream_seek(d->stream, 12, SEEK_CUR);
	atom_size -= 12;

	/* Check ES_DescrTag */
	stream_read(d->stream, 1);
	tag = d->buffer[0];
	atom_size--;
	if(tag == 0x03)
	{
		/* Read length */
		if(demux_mp4_read_len(d, &atom_size) < 20)
			goto end;

		/* Skip 3 bytes */
		stream_read(d->stream, 3);
		atom_size -= 3;
	}
	else
	{
		/* Skip 2 bytes */
		stream_read(d->stream, 2);
		atom_size -= 2;
	}

	/* Check DecoderConfigDescrTab */
	stream_read(d->stream, 1);
	atom_size--;
	if(d->buffer[0] != 0x04)
		goto end;

	/* Read length */
	if(demux_mp4_read_len(d, &atom_size) < 13)
		goto end;

	/* Get esds properties */
	stream_read(d->stream, 14);
	atom_size -= 14;
	d->esds_audio_type = d->buffer[0];
	d->esds_max_bitrate = ATOM_READ32(&d->buffer[5]);
	d->esds_avg_bitrate = ATOM_READ32(&d->buffer[9]);

	/* Check DecSpecificInfoTag */
	if(d->buffer[13] != 0x05)
		goto end;

	/* Read length */
	d->esds_size = demux_mp4_read_len(d, &atom_size);

	/* Copy decoder config */
	if(d->esds_buffer)
		free(d->esds_buffer);
	d->esds_buffer = malloc(d->esds_size);
	if(d->esds_buffer != NULL)
	{
		/* Read coder config */
		stream_read(d->stream, d->esds_size);
		memcpy(d->esds_buffer, d->buffer, d->esds_size);
	}
	else
	{
		d->esds_size = 0;
	}

end:
	/* Go to next atom */
	stream_seek(d->stream, atom_size, SEEK_CUR);
	return 0;
}

static void demux_mp4_parse_mp4a(struct demux *d)
{
	unsigned long atom_size;
	unsigned long size;

	/* Get size */
	atom_size = ATOM_LEN(d->buffer);
	stream_read(d->stream, 28);

	/* Get track properties */
	d->mp4a_channel_count = ATOM_READ16(&d->buffer[16]);
	d->mp4a_sample_size = ATOM_READ16(&d->buffer[18]);
	d->mp4a_samplerate = ATOM_READ16(&d->buffer[24]);

	/* Set track found flag */
	d->track_found = 1;

	/* Skip reserved bytes */
	atom_size -= 36;

	/* Get size of sub-atom */
	stream_read(d->stream, 8);
	size = ATOM_LEN(d->buffer);

	/* Parse "esds" atom */
	if(ATOM_CHECK(d->buffer, "esds") == 0)
	{
		demux_mp4_parse_esds(d);
		atom_size -= size;
	}

	/* Go to next atom */
	stream_seek(d->stream, atom_size, SEEK_CUR);
}

static int demux_mp4_parse_stsd(struct demux *d)
{
	unsigned long atom_size;
	unsigned long size;
	int32_t count;
	int32_t i;
	int is_mp4a = 0;

	/* Get size */
	atom_size = ATOM_LEN(d->buffer);
	stream_read(d->stream, 8);

	/* Count entries */
	count = ATOM_READ32(&d->buffer[4]);

	/* Go to first entry */
	atom_size -= 16;

	/* Parse each entries */
	for(i = 0; i < count; i++)
	{
		/* Get size of sub-atom */
		stream_read(d->stream, 8);
		size = ATOM_LEN(d->buffer);

		/* Process sub-atom */
		if(ATOM_CHECK(d->buffer, "mp4a") == 0)
		{
			/* Parse "mp4a" atom */
			demux_mp4_parse_mp4a(d);
			is_mp4a = 1;
		}
		else
		{
			stream_seek(d->stream, size, SEEK_CUR);
		}
		atom_size -= size;
	}

	/* Go to next atom */
	stream_seek(d->stream, atom_size, SEEK_CUR);

	return is_mp4a;
}

static void demux_mp4_parse_stts(struct demux *d)
{
	unsigned long size;
	unsigned int i, j, count;

	/* Get size */
	size = ATOM_LEN(d->buffer);
	stream_read(d->stream, 8);

	/* Get count */
	d->stts_entry_count = ATOM_READ32(&d->buffer[4]);

	/* Go to first entry */
	size -= 16;

	/* Create stts table */
	d->stts_sample_count = malloc(d->stts_entry_count * sizeof(int32_t));
	d->stts_sample_delta = malloc(d->stts_entry_count * sizeof(int32_t));
	d->num_samples = 0;

	/* Fill table */
	for(i = 0; i < d->stts_entry_count; i += count)
	{
		count = d->buffer_size / 8;
		if(count > d->stts_entry_count - i)
			count = d->stts_entry_count - i;

		stream_read(d->stream, count*8);
		for(j = 0; j < count; j++)
		{
			d->stts_sample_count[i+j] = ATOM_READ32(
							   &d->buffer[j*8]);
			d->stts_sample_delta[i+j] = ATOM_READ32(
						       &d->buffer[4+(j*8)]);
			d->num_samples += d->stts_sample_count[i+j];
		}
	}
	size -= 8 * d->stts_entry_count;

	/* Go to next atom */
	stream_seek(d->stream, size, SEEK_CUR);
}

static void demux_mp4_parse_stsc(struct demux *d)
{
	unsigned long size;
	unsigned int i, j, count;

	/* Get size */
	size = ATOM_LEN(d->buffer);
	stream_read(d->stream, 8);

	/* Get chunk count */
	d->stsc_entry_count = ATOM_READ32(&d->buffer[4]);

	/* Go to first entry */
	size -= 16;

	/* Create chunk table */
	d->stsc_first_chunk = malloc(d->stsc_entry_count * sizeof(int32_t));
	d->stsc_samples_per_chunk = malloc(d->stsc_entry_count *
							       sizeof(int32_t));
	d->stsc_sample_desc_index = malloc(d->stsc_entry_count *
							       sizeof(int32_t));

	/* Fill table */
	for(i = 0; i < d->stsc_entry_count; i += count)
	{
		count = d->buffer_size / 12;
		if(count > d->stsc_entry_count - i)
			count = d->stsc_entry_count - i;

		stream_read(d->stream, count*12);
		for(j = 0; j < count; j++)
		{
			d->stsc_first_chunk[i+j] = ATOM_READ32(
							   &d->buffer[j*12]);
			d->stsc_samples_per_chunk[i+j] = ATOM_READ32(
						       &d->buffer[4+(j*12)]);
			d->stsc_sample_desc_index[i+j] = ATOM_READ32(
						       &d->buffer[8+(j*12)]);
		}
	}
	size -= 12 * d->stsc_entry_count;

	/* Go to next atom */
	stream_seek(d->stream, size, SEEK_CUR);
}

static void demux_mp4_parse_stsz(struct demux *d)
{
	unsigned long size;
	unsigned int i, j, count;

	/* Get size */
	size = ATOM_LEN(d->buffer);
	stream_read(d->stream, 12);

	/* Get sample size and sample count */
	d->stsz_sample_size = ATOM_READ32(&d->buffer[4]);
	d->stsz_sample_count = ATOM_READ32(&d->buffer[8]);

	/* Go to first entry */
	size -= 20;

	/* Create sample size table */
	if(d->stsz_sample_size == 0)
	{
		d->stsz_table = malloc(d->stsz_sample_count * sizeof(int32_t));

		/* Fill table */
		for(i = 0; i < d->stsz_sample_count; i += count)
		{
			count = d->buffer_size / 4;
			if(count > d->stsz_sample_count - i)
				count = d->stsz_sample_count - i;

			stream_read(d->stream, count*4);
			for(j = 0; j < count; j++)
			{
				d->stsz_table[i+j] = ATOM_READ32(
							    &d->buffer[j*4]);
			}
		}
		size -= 4 * d->stsz_sample_count;
	}

	/* Go to next atom */
	stream_seek(d->stream, size, SEEK_CUR);
}

static void demux_mp4_parse_stco(struct demux *d)
{
	unsigned long size;
	unsigned int i, j, count;

	/* Get size */
	size = ATOM_LEN(d->buffer);
	stream_read(d->stream, 8);

	/* Get chunk offset count */
	d->stco_entry_count = ATOM_READ32(&d->buffer[4]);

	/* Go to first entry */
	size -= 16;

	/* Create chunk offset table */
	d->stco_chunk_offset = malloc(d->stco_entry_count * sizeof(int32_t));

	/* Fill table */
	for(i = 0; i < d->stco_entry_count; i += count)
	{
		count = d->buffer_size / 4;
		if(count > d->stco_entry_count - i)
			count = d->stco_entry_count - i;

		stream_read(d->stream, count*4);
		for(j = 0; j < count; j++)
		{
			d->stco_chunk_offset[i+j] = ATOM_READ32(
							    &d->buffer[j*4]);
		}
	}
	size -= 4 * d->stco_entry_count;

	/* Go to next atom */
	stream_seek(d->stream, size, SEEK_CUR);
}

static void demux_mp4_parse_track(struct demux *d)
{
	unsigned long atom_size;
	unsigned long count = 8;
	unsigned long size;
	int is_mp4a = 0;

	/* Get size of current atom */
	atom_size = ATOM_LEN(d->buffer);

	/* Get all children atoms */
	while(count < atom_size)
	{
		/* Size of sub-atom */
		stream_read(d->stream, 8);
		size = ATOM_LEN(d->buffer);

		/* Process sub-atom */
		if(ATOM_CHECK(d->buffer, "mdia") == 0 ||
		   ATOM_CHECK(d->buffer, "minf") == 0 ||
		   ATOM_CHECK(d->buffer, "stbl") == 0)
		{
			/* Parse sub-atom: get mdia -> minf -> stbl */
			demux_mp4_parse_track(d);
		}
		else if(ATOM_CHECK(d->buffer, "mdhd") == 0)
		{
			/* Parse "mdhd" atom */
			demux_mp4_parse_mdhd(d);
		}
		else if(ATOM_CHECK(d->buffer, "stsd") == 0)
		{
			/* Parse "stsd" atom */
			is_mp4a = demux_mp4_parse_stsd(d);
		}
		else if(ATOM_CHECK(d->buffer, "stts") == 0 && is_mp4a)
		{
			/* Parse "stts" atom */
			demux_mp4_parse_stts(d);
		}
		else if(ATOM_CHECK(d->buffer, "stsc") == 0 && is_mp4a)
		{
			/* Parse "stsc" atom */
			demux_mp4_parse_stsc(d);
		}
		else if(ATOM_CHECK(d->buffer, "stsz") == 0 && is_mp4a)
		{
			/* Parse "stsz" atom */
			demux_mp4_parse_stsz(d);
		}
		else if(ATOM_CHECK(d->buffer, "stco") == 0 && is_mp4a)
		{
			/* Parse "stco" atom */
			demux_mp4_parse_stco(d);
		}
		else
		{
			/* Ignore other sub-atoms */
			stream_seek(d->stream, size, SEEK_CUR);
		}
		count += size;
	}

	/* Finish atom reading */
	stream_seek(d->stream, atom_size-count, SEEK_CUR);
}

static void demux_mp4_parse_txt(struct demux *d, char **str)
{
	unsigned long pos = 0;
	unsigned long count;
	unsigned long size;
	unsigned long len;

	/* Get size */
	size = ATOM_LEN(d->buffer);
	stream_read(d->stream, 8);
	size -= 8;

	/* Check sub-atom */
	if(ATOM_CHECK(d->buffer, "data") == 0)
	{
		/* Get string length */
		len = ATOM_LEN(d->buffer) - 16;

		/* Skip version and flags */
		stream_read(d->stream, 8);
		size -= 16;

		/* Free previous string */
		if(*str != NULL)
			free(*str);

		/* Allocate new string */
		*str = calloc(1, len + 1);
		if(*str != NULL)
		{
			/* Copy string */
			while(len > 0)
			{
				count = d->buffer_size;
				if(count > len)
					count = len;
				count = stream_read(d->stream, count);
				memcpy(*str + pos, d->buffer, count);
				pos += count;
				len -= count;
				size -= count;
			}
		}
	}

	/* Go to next atom */
	stream_seek(d->stream, size, SEEK_CUR);
}

static void demux_mp4_parse_trkn(struct demux *d)
{
	unsigned long size;

	/* Get size */
	size = ATOM_LEN(d->buffer);
	stream_read(d->stream, 8);
	size -= 8;

	/* Check sub-atom */
	if(ATOM_CHECK(d->buffer, "data") == 0 && ATOM_LEN(d->buffer) == 24)
	{
		/* Skip version and flags */
		stream_read(d->stream, 10);
		size -= 20;

		/* Read track */
		stream_read(d->stream, 2);
		d->track = ATOM_READ16(d->buffer);

		/* Read total tack */
		stream_read(d->stream, 2);
		d->total_track = ATOM_READ16(d->buffer);
	}

	/* Go to next atom */
	stream_seek(d->stream, size, SEEK_CUR);
}

static void demux_mp4_parse_gnre(struct demux *d)
{
	unsigned long size;
	uint16_t genre;

	/* Get size */
	size = ATOM_LEN(d->buffer);
	stream_read(d->stream, 8);
	size -= 8;

	/* Check sub-atom */
	if(ATOM_CHECK(d->buffer, "data") == 0 && ATOM_LEN(d->buffer) == 18)
	{
		/* Skip version and flags */
		stream_read(d->stream, 8);
		size -= 16;

		/* Read genre index */
		stream_read(d->stream, 2);
		genre = ATOM_READ16(d->buffer);

		/* Check genre */
		if(genre > 0 || genre <= ID3v1_genres_count)
		{
			/* Free previous genre */
			if(d->genre != NULL)
				free(d->genre);

			/* Copy genre */
			d->genre = strdup(ID3v1_genres[genre-1]);
		}
	}

	/* Go to next atom */
	stream_seek(d->stream, size, SEEK_CUR);
}

static void demux_mp4_parse_covr(struct demux *d)
{
	unsigned long pos = 0;
	unsigned long count;
	unsigned long size;
	unsigned long len;
	uint32_t flags;

	/* Get size */
	size = ATOM_LEN(d->buffer);
	stream_read(d->stream, 8);
	size -= 8;

	/* Check sub-atom */
	if(ATOM_CHECK(d->buffer, "data") == 0)
	{
		/* Get length */
		len =  ATOM_LEN(d->buffer) - 16;

		/* Get flags for type */
		stream_read(d->stream, 8);
		flags = ATOM_READ32(d->buffer);
		size -= 8;

		/* Free previous buffer */
		if(d->pic != NULL)
			free(d->pic);
		if(d->pic_mime != NULL)
			free(d->pic_mime);
		d->pic_mime = NULL;
		d->pic_len = 0;

		/* Allocate new buffer */
		d->pic = malloc(len);
		if(d->pic != NULL)
		{
			/* Copy length */
			d->pic_len = len;

			/* Copy data */
			while(len > 0)
			{
				count = d->buffer_size;
				if(count > len)
					count = len;
				count = stream_read(d->stream, count);
				memcpy(d->pic + pos, d->buffer, count);
				pos += count;
				len -= count;
				size -= count;
			}

			/* Generate mime */
			if(flags == 13)
				d->pic_mime = strdup("image/jpeg");
			else if(flags == 14)
				d->pic_mime = strdup("image/png");
		}
	}

	/* Go to next atom */
	stream_seek(d->stream, size, SEEK_CUR);
}

static void demux_mp4_parse_ilst(struct demux *d)
{
	unsigned long atom_size;
	unsigned long count = 8;
	unsigned long size;

	/* Get size of current atom */
	atom_size = ATOM_LEN(d->buffer);

	/* Get all children atoms */
	while(count < atom_size)
	{
		/* Size of sub-atom */
		stream_read(d->stream, 8);
		size = ATOM_LEN(d->buffer);

		/* Process sub-atom */
		if(ATOM_CHECK(d->buffer, "\251alb") == 0)
			demux_mp4_parse_txt(d, &d->album);
		else if(ATOM_CHECK(d->buffer, "\251ART") == 0)
			demux_mp4_parse_txt(d, &d->artist);
		else if(ATOM_CHECK(d->buffer, "\251cmt") == 0)
			demux_mp4_parse_txt(d, &d->comment);
		else if(ATOM_CHECK(d->buffer, "\251day") == 0)
			demux_mp4_parse_txt(d, &d->year);
		else if(ATOM_CHECK(d->buffer, "\251nam") == 0)
			demux_mp4_parse_txt(d, &d->title);
		else if(ATOM_CHECK(d->buffer, "\251gen") == 0)
			demux_mp4_parse_txt(d, &d->genre);
		else if(ATOM_CHECK(d->buffer, "trkn") == 0)
			demux_mp4_parse_trkn(d);
		else if(ATOM_CHECK(d->buffer, "gnre") == 0)
			demux_mp4_parse_gnre(d);
		else if(ATOM_CHECK(d->buffer, "covr") == 0)
			demux_mp4_parse_covr(d);
		else
		{
			/* Ignore other sub-atoms */
			stream_seek(d->stream, size, SEEK_CUR);
		}
		count += size;
	}

	/* Finish atom reading */
	stream_seek(d->stream, atom_size-count, SEEK_CUR);
}

static void demux_mp4_parse_meta(struct demux *d)
{
	unsigned long atom_size;
	unsigned long count = 12;
	unsigned long size;

	/* Get size of current atom */
	atom_size = ATOM_LEN(d->buffer);

	/* Skip version and flags */
	stream_read(d->stream, 4);

	/* Get all children atoms */
	while(count < atom_size)
	{
		/* Size of sub-atom */
		stream_read(d->stream, 8);
		size = ATOM_LEN(d->buffer);

		/* Process sub-atom */
		if(ATOM_CHECK(d->buffer, "ilst") == 0)
		{
			/* Parse "ilst" atom */
			demux_mp4_parse_ilst(d);
		}
		else
		{
			/* Ignore other sub-atoms */
			stream_seek(d->stream, size, SEEK_CUR);
		}
		count += size;
	}

	/* Finish atom reading */
	stream_seek(d->stream, atom_size-count, SEEK_CUR);
}

static void demux_mp4_parse_udta(struct demux *d)
{
	unsigned long atom_size;
	unsigned long count = 8;
	unsigned long size;

	/* Get size of current atom */
	atom_size = ATOM_LEN(d->buffer);

	/* Get all children atoms */
	while(count < atom_size)
	{
		/* Size of sub-atom */
		stream_read(d->stream, 8);
		size = ATOM_LEN(d->buffer);

		/* Process sub-atom */
		if(ATOM_CHECK(d->buffer, "meta") == 0)
		{
			/* Parse "meta" atom */
			demux_mp4_parse_meta(d);
		}
		else
		{
			/* Ignore other sub-atoms */
			stream_seek(d->stream, size, SEEK_CUR);
		}
		count += size;
	}

	/* Finish atom reading */
	stream_seek(d->stream, atom_size-count, SEEK_CUR);
}

static void demux_mp4_parse_moov(struct demux *d)
{
	unsigned long atom_size;
	unsigned long count = 8;
	unsigned long size;

	/* Get atom size */
	atom_size = ATOM_LEN(d->buffer);


	/* Get all children atoms */
	while(count < atom_size)
	{
		/* Size of sub-atom */
		stream_read(d->stream, 8);
		size = ATOM_LEN(d->buffer);

		/* Process sub-atom */
		if(ATOM_CHECK(d->buffer, "trak") == 0 && !d->track_found)
		{
			/* Parse "track" atom */
			demux_mp4_parse_track(d);
		}
		else if(ATOM_CHECK(d->buffer, "udta") == 0)
		{
			/* Parse "udta" atom */
			demux_mp4_parse_udta(d);
		}
		else
		{
			/* Ignore other sub-atoms */
			stream_seek(d->stream, size, SEEK_CUR);
		}
		count += size;
	}

	/* Finish atom reading */
	stream_seek(d->stream, atom_size-count, SEEK_CUR);
}

int demux_mp4_open(struct demux **demux, struct stream_handle *stream,
		   unsigned long *samplerate, unsigned char *channels)
{
	struct demux *d;
	const unsigned char *buffer;
	unsigned long mdat_pos = 0;
	unsigned long moov_pos = 0;
	unsigned long count = 0;
	unsigned long size;

	if(stream == NULL)
		return -1;

	/* Get stream buffer */
	buffer = stream_get_buffer(stream);

	/* Read 8 first bytes for first atom header */
	if(stream_read(stream, 8) != 8)
		return -1;

	/* Check "ftyp" atom */
	if(ATOM_CHECK(buffer, "ftyp") != 0)
		return -1;
	size = ATOM_LEN(buffer);
	count = size;

	/* Allocate demux data structure */
	*demux = malloc(sizeof(struct demux));
	if(*demux == NULL)
		return -1;
	d = *demux;

	/* Init demux structure */
	memset(d, 0, sizeof(struct demux));
	d->stream = stream;
	d->buffer = buffer;
	d->buffer_size = stream_get_buffer_size(stream);
	d->size = stream_get_size(stream);

	/* Seek to next atom and get next atom header */
	stream_seek(d->stream, size, SEEK_CUR);

	/* Read all atom until "mdat" */
	while(count < d->size)
	{
		/* Get size of sub-atom */
		stream_read(d->stream, 8);
		size = ATOM_LEN(d->buffer);

		/* Process sub-atom */
		if(ATOM_CHECK(d->buffer, "moov") == 0)
		{
			/* Process "moov" */
			demux_mp4_parse_moov(d);
			moov_pos = count;
		}
		else
		{
			if(ATOM_CHECK(d->buffer, "mdat") == 0)
			{
				mdat_pos = count;
				if(moov_pos > 0)
					break;
			}

			/* Go to next atom */
			stream_seek(d->stream, size, SEEK_CUR);
		}
		/* Update read bytes count */
		count += size;
	}

	/* Check if a valid mp4 file and have found a mp4a track */
	if(mdat_pos == 0 || d->track_found == 0)
		return -1;

	/* Go to first frame */
	if(d->stsz_sample_size != 0)
		d->cur_sample_size = d->stsz_sample_size;
	else
		d->cur_sample_size = d->stsz_table[0];
	d->cur_sample = 0;
	d->cur_chunk_sample = 0;
	d->cur_chunk_idx = 0;
	d->cur_chunk = 0;
	d->cur_offset = d->stco_chunk_offset[0];

	/* Fill meta */
	d->meta.samplerate = d->mp4a_samplerate;
	d->meta.channels = d->mp4a_channel_count;
	d->meta.bitrate = d->esds_avg_bitrate / 1000;
	d->meta.title = d->title;
	d->meta.artist = d->artist;
	d->meta.album = d->album;
	d->meta.comment = d->comment;
	d->meta.genre = d->genre;
	d->meta.track = d->track;
	d->meta.total_track = d->total_track;
	if(d->year != NULL)
		d->meta.year = strtol(d->year, NULL, 10);
	d->meta.picture.data = d->pic;
	d->meta.picture.mime = d->pic_mime;
	d->meta.picture.size = d->pic_len;

	/* Calculate stream duration */
	if(d->mdhd_time_scale != 0)
		d->meta.length = d->mdhd_duration / d->mdhd_time_scale;

	/* Update samplerate and channels */
	*samplerate = d->mp4a_samplerate;
	*channels = d->mp4a_channel_count;

	return 0;
}

struct meta *demux_mp4_get_meta(struct demux *d)
{
	return &d->meta;
}

int demux_mp4_get_dec_config(struct demux *d, int *codec,
			     const unsigned char **config, size_t *size)
{
	*codec = CODEC_AAC;
	*config = d->esds_buffer;
	*size = d->esds_size;
	return 0;
}

ssize_t demux_mp4_next_frame(struct demux *d)
{
	int size;

	/* End of file */
	if(d->cur_sample >= d->num_samples)
		return -1;

	/* Seek to next frame */
	if(stream_seek(d->stream, d->cur_offset, SEEK_SET) < 0)
		return -1;

	/* Read frame */
	size = stream_read(d->stream, d->cur_sample_size);

	/* Update current sample */
	d->cur_sample++;
	if(d->cur_sample >= d->num_samples)
		return size;

	/* Update current sample counter in current chunk */
	d->cur_chunk_sample++;
	if(d->cur_chunk_sample >= d->stsc_samples_per_chunk[d->cur_chunk_idx])
	{
		/* Go to next chunk */
		d->cur_chunk++;
		if(d->cur_chunk_idx + 1 > d->stsc_entry_count)
			return -1;

		if(d->cur_chunk + 1 >= d->stsc_first_chunk[d->cur_chunk_idx+1])
			d->cur_chunk_idx++;

		/* Get chunk offset */
		d->cur_offset = d->stco_chunk_offset[d->cur_chunk];
		d->cur_sample_size = 0;
		d->cur_chunk_sample = 0;
	}

	/* Update offset */
	d->cur_offset += d->cur_sample_size;
	if(d->stsz_sample_size != 0)
		d->cur_sample_size = d->stsz_sample_size;
	else
		d->cur_sample_size = d->stsz_table[d->cur_sample];

	return size;
}

void demux_mp4_set_used(struct demux *d, size_t len)
{
	if(len <= stream_get_len(d->stream))
		stream_seek(d->stream, len, SEEK_CUR);
}

unsigned long demux_mp4_set_pos(struct demux *d, unsigned long pos)
{
	unsigned long to_skip = 0;
	unsigned long chunk, chunk_idx, chunk_sample;
	unsigned long offset, i;
	long sample;

	/* Find sample friom time table */
	sample = demux_mp4_find_sample(d, pos*d->mdhd_time_scale, &to_skip);
	if(sample < 0)
		return -1;

	/* Get chunk containing the sample */
	if(demux_mp4_find_chunk(d, sample, &chunk, &chunk_idx, &chunk_sample)
	   != 0)
		return -1;

	/* Get chunk offset */
	if(chunk >= d->stco_entry_count)
		return -1;
	offset = d->stco_chunk_offset[chunk];

	/* Get sample offset in chunk */
	if(d->stsz_sample_size == 0)
	{
		for(i = sample - chunk_sample; i < sample; i++)
		{
			offset += d->stsz_table[i];
		}
	}
	else
		offset += chunk_sample * d->stsz_sample_size;

	/* Update current chunk/sample */
	d->cur_sample = sample;
	d->cur_chunk = chunk;
	d->cur_chunk_idx = chunk_idx;
	d->cur_chunk_sample = chunk_sample;
	d->cur_offset = offset;
	if(d->stsz_sample_size != 0)
		d->cur_sample_size = d->stsz_sample_size;
	else
		d->cur_sample_size = d->stsz_table[d->cur_sample];

	return pos - (to_skip / d->mdhd_time_scale);
}

#define FREE_MP4(b) if(b != NULL) free(b);

void demux_mp4_close(struct demux *d)
{
	if(d == NULL)
		return;

	/* Free all buffers */
	FREE_MP4(d->stsz_table);
	FREE_MP4(d->stco_chunk_offset);
	FREE_MP4(d->stsc_first_chunk);
	FREE_MP4(d->stsc_samples_per_chunk);
	FREE_MP4(d->stsc_sample_desc_index);
	FREE_MP4(d->stts_sample_count);
	FREE_MP4(d->stts_sample_delta);
	FREE_MP4(d->esds_buffer);
	FREE_MP4(d->title);
	FREE_MP4(d->artist);
	FREE_MP4(d->album);
	FREE_MP4(d->comment);
	FREE_MP4(d->genre);
	FREE_MP4(d->year);
	FREE_MP4(d->pic);
	FREE_MP4(d->pic_mime);

	/* Free handle */
	free(d);
}

struct demux_handle demux_mp4 = {
	.demux = NULL,
	.open = &demux_mp4_open,
	.get_meta = &demux_mp4_get_meta,
	.get_dec_config = &demux_mp4_get_dec_config,
	.next_frame = &demux_mp4_next_frame,
	.set_used = &demux_mp4_set_used,
	.set_pos = &demux_mp4_set_pos,
	.close = &demux_mp4_close,
};

