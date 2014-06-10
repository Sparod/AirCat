/*
 * file_mp4.c - A MP4 file demuxer for File module
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

#include "file_private.h"

#define ATOM_CHECK(b, a) memcmp(&b[4], a, 4)
#define ATOM_READ16(b) ((b)[0] << 8) | (b)[1];
#define ATOM_READ32(b) ((b)[0] << 24) | ((b)[1] << 16) | ((b)[2] << 8) | \
		       (b)[3];
#define ATOM_READ64(b) ((unsigned long long)(b)[0] << 56) | \
		       ((unsigned long long)(b)[1] << 48) | \
		       ((unsigned long long)(b)[2] << 40) | \
		       ((unsigned long long)(b)[3] << 32) | \
		       ((unsigned long long)(b)[4] << 24) | \
		       ((unsigned long long)(b)[5] << 16) | \
		       ((unsigned long long)(b)[6] << 8) | \
		       (unsigned long long)(b)[7];
#define ATOM_LEN(b) ATOM_READ32(b)

struct mp4_demux {
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
};

static int file_mp4_find_chunk(struct mp4_demux *d, unsigned long sample,
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

static long file_mp4_find_sample(struct mp4_demux *d, unsigned long pos,
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

static void file_mp4_parse_mdhd(struct file_handle *h)
{
	struct mp4_demux *d = h->demux_data;
	unsigned long version;
	unsigned long size;

	/* Get size */
	size = ATOM_LEN(h->in_buffer);

	/* Get Version */
	file_read_input(h, 4);
	version = ATOM_READ32(h->in_buffer);

	/* Go to first entry */
	size -= 12;

	if(version == 1)
	{
		file_read_input(h, 28);
		d->mdhd_time_scale = ATOM_READ32(&h->in_buffer[16]);
		d->mdhd_duration = ATOM_READ64(&h->in_buffer[20]);
	}
	else
	{
		file_read_input(h, 16);
		d->mdhd_time_scale = ATOM_READ32(&h->in_buffer[8]);
		d->mdhd_duration = ATOM_READ32(&h->in_buffer[12]);
	}

	/* Go to next atom */
	file_seek_input(h, size, SEEK_CUR);
}

static uint32_t file_mp4_read_len(struct file_handle *h, unsigned long *count)
{
	uint32_t len = 0;
	uint8_t i, b = 0x80;

	for(i = 0; b & 0x80 && i < 4; i++)
	{
		file_read_input(h, 1);
		(*count) -= 1;
		b = h->in_buffer[0];
		len = (len << 7) | (b & 0x7F);
	}

	return len;
}

static int file_mp4_parse_esds(struct file_handle *h)
{
	struct mp4_demux *d = h->demux_data;
	unsigned long atom_size;
	unsigned char tag;

	/* Get size */
	atom_size = ATOM_LEN(h->in_buffer);

	/* Skip version and flags */
	file_seek_input(h, 12, SEEK_CUR);
	atom_size -= 12;

	/* Check ES_DescrTag */
	file_read_input(h, 1);
	tag = h->in_buffer[0];
	atom_size--;
	if(tag == 0x03)
	{
		/* Read length */
		if(file_mp4_read_len(h, &atom_size) < 20)
			goto end;

		/* Skip 3 bytes */
		file_read_input(h, 3);
		atom_size -= 3;
	}
	else
	{
		/* Skip 2 bytes */
		file_read_input(h, 2);
		atom_size -= 2;
	}

	/* Check DecoderConfigDescrTab */
	file_read_input(h, 1);
	atom_size--;
	if(h->in_buffer[0] != 0x04)
		goto end;

	/* Read length */
	if(file_mp4_read_len(h, &atom_size) < 13)
		goto end;

	/* Get esds properties */
	file_read_input(h, 14);
	atom_size -= 14;
	d->esds_audio_type = h->in_buffer[0];
	d->esds_max_bitrate = ATOM_READ32(&h->in_buffer[5]);
	d->esds_avg_bitrate = ATOM_READ32(&h->in_buffer[9]);

	/* Check DecSpecificInfoTag */
	if(h->in_buffer[13] != 0x05)
		goto end;

	/* Read length */
	d->esds_size = file_mp4_read_len(h, &atom_size);

	/* Copy decoder config */
	if(d->esds_buffer)
		free(d->esds_buffer);
	d->esds_buffer = malloc(d->esds_size);
	if(d->esds_buffer != NULL)
	{
		/* Read coder config */
		file_read_input(h, d->esds_size);
		memcpy(d->esds_buffer, h->in_buffer, d->esds_size);
	}
	else
	{
		d->esds_size = 0;
	}

end:
	/* Go to next atom */
	file_seek_input(h, atom_size, SEEK_CUR);
	return 0;
}

static void file_mp4_parse_mp4a(struct file_handle *h)
{
	struct mp4_demux *d = h->demux_data;
	unsigned long atom_size;
	unsigned long size;

	/* Get size */
	atom_size = ATOM_LEN(h->in_buffer);
	file_read_input(h, 28);

	/* Get track properties */
	d->mp4a_channel_count = ATOM_READ16(&h->in_buffer[16]);
	d->mp4a_sample_size = ATOM_READ16(&h->in_buffer[18]);
	d->mp4a_samplerate = ATOM_READ16(&h->in_buffer[24]);

	/* Set track found flag */
	d->track_found = 1;

	/* Skip reserved bytes */
	atom_size -= 36;

	/* Get size of sub-atom */
	file_read_input(h, 8);
	size = ATOM_LEN(h->in_buffer);

	/* Parse "esds" atom */
	if(ATOM_CHECK(h->in_buffer, "esds") == 0)
	{
		file_mp4_parse_esds(h);
		atom_size -= size;
	}

	/* Go to next atom */
	file_seek_input(h, atom_size, SEEK_CUR);
}

static int file_mp4_parse_stsd(struct file_handle *h)
{
	unsigned long atom_size;
	unsigned long size;
	int32_t count;
	int32_t i;
	int is_mp4a = 0;

	/* Get size */
	atom_size = ATOM_LEN(h->in_buffer);
	file_read_input(h, 8);

	/* Count entries */
	count = ATOM_READ32(&h->in_buffer[4]);

	/* Go to first entry */
	atom_size -= 16;

	/* Parse each entries */
	for(i = 0; i < count; i++)
	{
		/* Get size of sub-atom */
		file_read_input(h, 8);
		size = ATOM_LEN(h->in_buffer);

		/* Process sub-atom */
		if(ATOM_CHECK(h->in_buffer, "mp4a") == 0)
		{
			/* Parse "mp4a" atom */
			file_mp4_parse_mp4a(h);
			is_mp4a = 1;
		}
		else
		{
			file_seek_input(h, size, SEEK_CUR);
		}
		atom_size -= size;
	}

	/* Go to next atom */
	file_seek_input(h, atom_size, SEEK_CUR);

	return is_mp4a;
}

static void file_mp4_parse_stts(struct file_handle *h)
{
	struct mp4_demux *d = h->demux_data;
	unsigned long size;
	unsigned int i, j, count;

	/* Get size */
	size = ATOM_LEN(h->in_buffer);
	file_read_input(h, 8);

	/* Get count */
	d->stts_entry_count = ATOM_READ32(&h->in_buffer[4]);

	/* Go to first entry */
	size -= 16;

	/* Create stts table */
	d->stts_sample_count = malloc(d->stts_entry_count * sizeof(int32_t));
	d->stts_sample_delta = malloc(d->stts_entry_count * sizeof(int32_t));
	d->num_samples = 0;

	/* Fill table */
	for(i = 0; i < d->stts_entry_count; i += count)
	{
		count = BUFFER_SIZE / 8;
		if(count > d->stts_entry_count - i)
			count = d->stts_entry_count - i;

		file_read_input(h, count*8);
		for(j = 0; j < count; j++)
		{
			d->stts_sample_count[i+j] = ATOM_READ32(
							   &h->in_buffer[j*8]);
			d->stts_sample_delta[i+j] = ATOM_READ32(
						       &h->in_buffer[4+(j*8)]);
			d->num_samples += d->stts_sample_count[i+j];
		}
	}
	size -= 8 * d->stts_entry_count;

	/* Go to next atom */
	file_seek_input(h, size, SEEK_CUR);
}

static void file_mp4_parse_stsc(struct file_handle *h)
{
	struct mp4_demux *d = h->demux_data;
	unsigned long size;
	unsigned int i, j, count;

	/* Get size */
	size = ATOM_LEN(h->in_buffer);
	file_read_input(h, 8);

	/* Get chunk count */
	d->stsc_entry_count = ATOM_READ32(&h->in_buffer[4]);

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
		count = BUFFER_SIZE / 12;
		if(count > d->stsc_entry_count - i)
			count = d->stsc_entry_count - i;

		file_read_input(h, count*12);
		for(j = 0; j < count; j++)
		{
			d->stsc_first_chunk[i+j] = ATOM_READ32(
							   &h->in_buffer[j*12]);
			d->stsc_samples_per_chunk[i+j] = ATOM_READ32(
						       &h->in_buffer[4+(j*12)]);
			d->stsc_sample_desc_index[i+j] = ATOM_READ32(
						       &h->in_buffer[8+(j*12)]);
		}
	}
	size -= 12 * d->stsc_entry_count;

	/* Go to next atom */
	file_seek_input(h, size, SEEK_CUR);
}

static void file_mp4_parse_stsz(struct file_handle *h)
{
	struct mp4_demux *d = h->demux_data;
	unsigned long size;
	unsigned int i, j, count;

	/* Get size */
	size = ATOM_LEN(h->in_buffer);
	file_read_input(h, 12);

	/* Get sample size and sample count */
	d->stsz_sample_size = ATOM_READ32(&h->in_buffer[4]);
	d->stsz_sample_count = ATOM_READ32(&h->in_buffer[8]);

	/* Go to first entry */
	size -= 20;

	/* Create sample size table */
	if(d->stsz_sample_size == 0)
	{
		d->stsz_table = malloc(d->stsz_sample_count * sizeof(int32_t));

		/* Fill table */
		for(i = 0; i < d->stsz_sample_count; i += count)
		{
			count = BUFFER_SIZE / 4;
			if(count > d->stsz_sample_count - i)
				count = d->stsz_sample_count - i;

			file_read_input(h, count*4);
			for(j = 0; j < count; j++)
			{
				d->stsz_table[i+j] = ATOM_READ32(
							    &h->in_buffer[j*4]);
			}
		}
		size -= 4 * d->stsz_sample_count;
	}

	/* Go to next atom */
	file_seek_input(h, size, SEEK_CUR);
}

static void file_mp4_parse_stco(struct file_handle *h)
{
	struct mp4_demux *d = h->demux_data;
	unsigned long size;
	unsigned int i, j, count;

	/* Get size */
	size = ATOM_LEN(h->in_buffer);
	file_read_input(h, 8);

	/* Get chunk offset count */
	d->stco_entry_count = ATOM_READ32(&h->in_buffer[4]);

	/* Go to first entry */
	size -= 16;

	/* Create chunk offset table */
	d->stco_chunk_offset = malloc(d->stco_entry_count * sizeof(int32_t));

	/* Fill table */
	for(i = 0; i < d->stco_entry_count; i += count)
	{
		count = BUFFER_SIZE / 4;
		if(count > d->stco_entry_count - i)
			count = d->stco_entry_count - i;

		file_read_input(h, count*4);
		for(j = 0; j < count; j++)
		{
			d->stco_chunk_offset[i+j] = ATOM_READ32(
							    &h->in_buffer[j*4]);
		}
	}
	size -= 4 * d->stco_entry_count;

	/* Go to next atom */
	file_seek_input(h, size, SEEK_CUR);
}

static void file_mp4_parse_track(struct file_handle *h)
{
	unsigned long atom_size;
	unsigned long count = 8;
	unsigned long size;
	int is_mp4a = 0;

	/* Get size of current atom */
	atom_size = ATOM_LEN(h->in_buffer);

	/* Get all children atoms */
	while(count < atom_size)
	{
		/* Size of sub-atom */
		file_read_input(h, 8);
		size = ATOM_LEN(h->in_buffer);

		/* Process sub-atom */
		if(ATOM_CHECK(h->in_buffer, "mdia") == 0 ||
		   ATOM_CHECK(h->in_buffer, "minf") == 0 ||
		   ATOM_CHECK(h->in_buffer, "stbl") == 0)
		{
			/* Parse sub-atom: get mdia -> minf -> stbl */
			file_mp4_parse_track(h);
		}
		else if(ATOM_CHECK(h->in_buffer, "mdhd") == 0)
		{
			/* Parse "mdhd" atom */
			file_mp4_parse_mdhd(h);
		}
		else if(ATOM_CHECK(h->in_buffer, "stsd") == 0)
		{
			/* Parse "stsd" atom */
			is_mp4a = file_mp4_parse_stsd(h);
		}
		else if(ATOM_CHECK(h->in_buffer, "stts") == 0 && is_mp4a)
		{
			/* Parse "stts" atom */
			file_mp4_parse_stts(h);
		}
		else if(ATOM_CHECK(h->in_buffer, "stsc") == 0 && is_mp4a)
		{
			/* Parse "stsc" atom */
			file_mp4_parse_stsc(h);
		}
		else if(ATOM_CHECK(h->in_buffer, "stsz") == 0 && is_mp4a)
		{
			/* Parse "stsz" atom */
			file_mp4_parse_stsz(h);
		}
		else if(ATOM_CHECK(h->in_buffer, "stco") == 0 && is_mp4a)
		{
			/* Parse "stco" atom */
			file_mp4_parse_stco(h);
		}
		else
		{
			/* Ignore other sub-atoms */
			file_seek_input(h, size, SEEK_CUR);
		}
		count += size;
	}

	/* Finish atom reading */
	file_seek_input(h, atom_size-count, SEEK_CUR);
}

static void file_mp4_parse_moov(struct file_handle *h)
{
	struct mp4_demux *d = h->demux_data;
	unsigned long atom_size;
	unsigned long count = 8;
	unsigned long size;

	/* Get atom size */
	atom_size = ATOM_LEN(h->in_buffer);

	/* Get all children atoms */
	while(count < atom_size)
	{
		/* Size of sub-atom */
		file_read_input(h, 8);
		size = ATOM_LEN(h->in_buffer);

		/* Process sub-atom */
		if(ATOM_CHECK(h->in_buffer, "trak") == 0 && !d->track_found)
		{
			/* Parse "track" atom */
			file_mp4_parse_track(h);
		}
		else
		{
			/* Ignore other sub-atoms */
			file_seek_input(h, size, SEEK_CUR);
		}
		count += size;
	}

	/* Finish atom reading */
	file_seek_input(h, atom_size-count, SEEK_CUR);
}

int file_mp4_init(struct file_handle *h, unsigned long *samplerate,
		  unsigned char *channels)
{
	struct mp4_demux *d;
	unsigned long mdat_pos = 0;
	unsigned long moov_pos = 0;
	unsigned long count = 0;
	unsigned long size;

	/* Read 8 first bytes for first atom header */
	if(file_read_input(h, 8) != 8)
		return -1;

	/* Check "ftyp" atom */
	if(ATOM_CHECK(h->in_buffer, "ftyp") != 0)
		return -1;
	size = ATOM_LEN(h->in_buffer);
	count = size;

	/* Allocate demux data structure */
	h->demux_data = malloc(sizeof(struct mp4_demux));
	if(h->demux_data == NULL)
		return -1;
	d = h->demux_data;
	memset(d, 0, sizeof(struct mp4_demux));

	/* Seek to next atom and get next atom header */
	file_seek_input(h, size, SEEK_CUR);

	/* Read all atom until "mdat" */
	while(count < h->file_size)
	{
		/* Get size of sub-atom */
		file_read_input(h, 8);
		size = ATOM_LEN(h->in_buffer);

		/* Process sub-atom */
		if(ATOM_CHECK(h->in_buffer, "moov") == 0)
		{
			/* Process "moov" */
			file_mp4_parse_moov(h);
			moov_pos = count;
		}
		else
		{
			if(ATOM_CHECK(h->in_buffer, "mdat") == 0)
			{
				mdat_pos = count;
				if(moov_pos > 0)
					break;
			}

			/* Go to next atom */
			file_seek_input(h, size, SEEK_CUR);
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

	/* Update samplerate and channels */
	*samplerate = d->mp4a_samplerate;
	*channels = d->mp4a_channel_count;

	/* Update decoder config */
	h->decoder_config = d->esds_buffer;
	h->decoder_config_size = d->esds_size;


	return 0;
}

int file_mp4_get_next_frame(struct file_handle *h)
{
	struct mp4_demux *d = h->demux_data;
	int size;

	/* End of file */
	if(d->cur_sample >= d->num_samples)
		return -1;

	/* Read frame */
	file_seek_input(h, d->cur_offset, SEEK_SET);
	size = file_read_input(h, d->cur_sample_size);

	/* Update current sample */
	d->cur_sample++;
	if(d->cur_sample >= d->num_samples)
		return -1;

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

int file_mp4_set_pos(struct file_handle *h, unsigned long pos)
{
	struct mp4_demux *d = h->demux_data;
	unsigned long to_skip = 0;
	unsigned long chunk, chunk_idx, chunk_sample;
	unsigned long offset, i;
	long sample;

	/* Find sample friom time table */
	sample = file_mp4_find_sample(d, pos*d->mdhd_time_scale, &to_skip);
	if(sample < 0)
		return -1;

	/* Get chunk containing the sample */
	if(file_mp4_find_chunk(d, sample, &chunk, &chunk_idx, &chunk_sample)
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

	/* Update position */
	h->pos = (pos - (to_skip / d->mdhd_time_scale)) * h->samplerate *
		 h->channels;

	return 0;
}

#define FREE_MP4(b) if(b != NULL) free(b);

void file_mp4_free(struct file_handle *h)
{
	struct mp4_demux *d;

	if(h == NULL || h->demux_data == NULL)
		return;

	d = h->demux_data;
	FREE_MP4(d->stsz_table);
	FREE_MP4(d->stco_chunk_offset);
	FREE_MP4(d->stsc_first_chunk);
	FREE_MP4(d->stsc_samples_per_chunk);
	FREE_MP4(d->stsc_sample_desc_index);
	FREE_MP4(d->stts_sample_count);
	FREE_MP4(d->stts_sample_delta);
	FREE_MP4(d->esds_buffer);

	free(h->demux_data);
	h->demux_data = NULL;
}

struct file_demux file_mp4_demux = {
	.init = &file_mp4_init,
	.get_next_frame = &file_mp4_get_next_frame,
	.set_pos = &file_mp4_set_pos,
	.free = &file_mp4_free,
};
