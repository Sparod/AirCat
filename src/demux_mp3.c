/*
 * demux_mp3.c - An MP3 demuxer
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
#include <strings.h>

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "demux.h"

unsigned int bitrates[2][3][15] = {
	{ /* MPEG-1 */
		{0, 32, 64, 96, 128, 160, 192, 224, 256, 288, 320, 352, 384,
		 416, 448},
		{0, 32, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320,
		 384},
		{0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256,
		 320}
	},
	{ /* MPEG-2 LSF, MPEG-2.5 */
		{0, 32, 48, 56, 64, 80, 96, 112, 128, 144, 160, 176, 192, 224,
		 256},
		{0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160},
		{0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160}
	}
};
unsigned int samplerates[3][4] = {
	{44100, 48000, 32000, 0},
	{22050, 24000, 16000, 0},
	{11025, 8000, 8000, 0}
};

unsigned int samples[2][3] = {
	{384, 1152, 1152},
	{384, 1152, 576}
};

struct mp3_frame {
	unsigned char mpeg; /* 0: MPEG 1, 1: MPEG 2, 2: MPEG 2.5 */
	unsigned char layer; /* 0: layer 1, 1: layer 2, 2: layer 3 */
	unsigned int bitrate;
	unsigned long samplerate;
	unsigned char padding;
	unsigned char channels; /* 0: Mono, 1: Stereo, 2: Joint Stereo,
				   3: Dual channel */
	unsigned int samples;
	unsigned int length;
};

struct demux {
	/* Stream */
	struct stream_handle *stream;
	const unsigned char *buffer;
	/* Stream length */
	unsigned long duration;
	unsigned long length;
	/* Stream format */
	struct file_format format;
	/* Xing/VBRI specific */
	unsigned long nb_bytes;
	unsigned int nb_frame;
	unsigned int quality;
	unsigned char *toc;
	/* VBRI sepcific */
	unsigned int version;
	unsigned int delay;
	unsigned char *vbri_toc;
	unsigned int toc_scale;
	unsigned int toc_size;
	unsigned int toc_count;
	unsigned int toc_frames;
	/* First frame offset */
	unsigned long offset;
};

static int demux_mp3_parse_header(const unsigned char *buffer,
				  unsigned int size, struct mp3_frame *f)
{
	int mp;

	if(size < 4)
		return -1;

	/* Check syncword */
	if(buffer[0] != 0xFF || (buffer[1] & 0xE0) != 0xE0)
		return -1;

	/* Get Mpeg version */
	f->mpeg = 3 - ((buffer[1] >> 3) & 0x03);
	mp = f->mpeg;
	if(f->mpeg == 2)
		return -1;
	if(f->mpeg == 3)
	{
		f->mpeg = 2;
		mp = 1;
	}

	/* Get Layer */
	f->layer = 3 - ((buffer[1] >> 1) & 0x03);
	if(f->layer == 3)
		return -1;

	/* Get bitrate */
	f->bitrate = (buffer[2] >> 4) & 0x0F;
	if(f->bitrate == 0 || f->bitrate == 15)
		return -1;
	f->bitrate = bitrates[mp][f->layer][f->bitrate];

	/* Get samplerate */
	f->samplerate = (buffer[2] >> 2) & 0x03;
	if(f->samplerate == 3)
		return -1;
	f->samplerate = samplerates[f->mpeg][f->samplerate];

	/* Get padding */
	f->padding = (buffer[2] >> 1) & 0x01;

	/* Get channel count */
	f->channels = (((buffer[3] >> 6) & 0x03) + 1) % 4;

	/* Get samples count */
	f->samples = samples[mp][f->layer];

	/* Calculate frame length */
	if(f->layer == 0)
	{
		/* Layer I */
		f->length = ((12 * f->bitrate * 1000 / f->samplerate) +
			    f->padding) * 4;
	}
	else if(f->mpeg > 0 && f->layer == 2)
	{
		/* MPEG 2 and 2.5 in layer III */
		f->length = (72 * f->bitrate * 1000 / f->samplerate) +
			     f->padding;
	}
	else
	{
		/* Layer II or III */
		f->length = (144 * f->bitrate * 1000 / f->samplerate) +
			     f->padding;
	}

	return 0;
}

#define READ32(b) (b[0] << 24) | (b[1] << 16) | (b[2] << 8) | b[3]; b += 4;
#define READ16(b) (b[0] << 8) | b[1]; b += 2;

static int demux_mp3_parse_xing(struct mp3_frame *f,
				const unsigned char *buffer, size_t len,
				struct demux *d)
{
	unsigned int offset;
	unsigned char flags;

	/* Needs all frame */
	if(f->length > len)
		return -1;

	/* Calculate header position */
	offset = f->channels == 0 ?
		(f->mpeg == 0 ? 21 : 13) : (f->mpeg == 0 ? 36 : 21);
	if(offset + 120 > f->length)
		return -1;

	/* Set buffer ptr */
	buffer += offset;

	/* Ignore Xing header begining with LAME */
	if(strncasecmp((char*)buffer, "LAME", 4) == 0)
		return 0;

	/* Check header ID */
	if(strncasecmp((char*)buffer, "Xing", 4) != 0 &&
	   strncasecmp((char*)buffer, "Info", 4) != 0)
		return -1;

	/* Get flags */
	buffer += 4;
	flags = (unsigned char) READ32(buffer);

	/* Number of Frames */
	if(flags & 0x0001)
		d->nb_frame  = READ32(buffer);

	/* Number of Bytes */
	if(flags & 0x0002)
		d->nb_bytes  = READ32(buffer);

	/* TOC entries */
	if(flags & 0x0004)
	{
		/* Copy TOC */
		d->toc = malloc(100);
		if(d->toc != NULL)
			memcpy(d->toc, buffer, 100);

		/* Update position */
		buffer += 100;
	}

	/* Quality indicator */
	if(flags & 0x0008)
		d->quality  = READ32(buffer);

	return 0;
}

static int demux_mp3_parse_vbri(struct mp3_frame *f,
				const unsigned char *buffer, size_t len,
				struct demux *d)
{
	unsigned int size;

	/* Needs all frame */
	if(f->length > len && f->length < 62)
		return -1;

	/* Go to VBRI header */
	buffer += 36;

	/* Check header ID */
	if(strncasecmp((char*)buffer, "VBRI", 4) != 0)
		return -1;

	/* Update buffer position */
	buffer += 4;

	/* Get version ID */
	d->version = READ16(buffer);

	/* Get delay */
	d->delay = READ16(buffer);

	/* Quality indicator */
	d->quality = READ16(buffer);

	/* Number of Bytes */
	d->nb_bytes = READ32(buffer);
	if(d->nb_bytes == 0)
		return 0;

	/* Number of Frames */
	d->nb_frame = READ32(buffer);
	if(d->nb_frame == 0)
		return 0;

	/* Number of entries within TOC */
	d->toc_count = READ16(buffer);
	if(d->toc_count == 0)
		return 0;

	/* Scale factor of TOC */
	d->toc_scale = READ16(buffer);
	if(d->toc_scale == 0)
		return 0;

	/* Size per table entry in bytes (max 4) */
	d->toc_size = READ16(buffer);
	if(d->toc_size > 4 || d->toc_size == 0)
		return 0;

	/* Frames per table entry */
	d->toc_frames = READ16(buffer);
	if(d->toc_frames == 0 || d->toc_frames * (d->toc_count+1) < d->nb_frame)
		return 0;


	/* Calculate TOC size */
	size = d->toc_size * d->toc_count;
	if(f->length < 62 + size)
		return 0;


	/* Copy TOC */
	d->vbri_toc = malloc(size);
	if(d->vbri_toc != NULL)
		memcpy(d->vbri_toc, buffer, size);

	return 0;
}

int demux_mp3_open(struct demux **demux, struct stream_handle *stream,
		   unsigned long *samplerate, unsigned char *channels)
{
	struct demux *d;
	const unsigned char *buffer;
	unsigned long id3_size = 0;
	struct mp3_frame frame;
	long first = -1;
	size_t len;
	int i;

	if(stream == NULL)
		return -1;

	/* Get stream buffer */
	buffer = stream_get_buffer(stream);

	/* Read 10 first bytes for ID3 header */
	if(stream_read(stream, 10) != 10)
		return -1;

	/* Check ID3V2 tag */
	if(memcmp(buffer, "ID3", 3) == 0)
	{
		/* Get ID3 size */
		id3_size = (buffer[6] << 21) | (buffer[7] << 14) |
		       (buffer[8] << 7) | buffer[9];
		id3_size += 10;

		/* Add footer size */
		if(buffer[5] & 0x20)
			id3_size += 10;

		/* Skip ID3 in file */
		stream_seek(stream, id3_size, SEEK_CUR);
	}

	/* Complete input buffer */
	len = stream_complete(stream, 0);

	/* Sync to first frame */
	for(i = 0; i < len - 3; i++)
	{
		if(buffer[i] == 0xFF && (buffer[i+1] & 0xE0) == 0xE0)
		{
			/* Check header */
			if(demux_mp3_parse_header(&buffer[i], 4, &frame)
			   != 0)
				continue;

			/* Check next frame */
			if(i + frame.length + 2 > len ||
			   buffer[i+frame.length] != 0xFF ||
			   (buffer[i+frame.length+1] & 0xE0) != 0xE0)
				continue;

			first = i;
			break;
		}
	}
	if(first < 0)
		return -1;

	/* Allocate structure */
	*demux = malloc(sizeof(struct demux));
	if(*demux == NULL)
		return -1;
	d = *demux;

	/* Init structure */
	memset(d, 0, sizeof(struct demux));
	d->stream = stream;
	d->buffer = buffer;
	d->length = stream_get_size(stream);

	/* Move to first frame */
	stream_seek(stream, first, SEEK_CUR);
	len = stream_complete(stream, 0);

	/* Parse Xing/Lame/VBRI header */
	if(demux_mp3_parse_xing(&frame, buffer, len, d) == 0 ||
	   demux_mp3_parse_vbri(&frame, buffer, len, d) == 0)
	{
		/* Move to next frame */
		first += frame.length;
		stream_seek(stream, first, SEEK_CUR);
	}

	/* Update position of stream */
	d->offset = first + id3_size;

	/* Calculate stream duration */
	if(d->nb_frame > 0)
	{
		/* Calculate duration with XING/VBRI headers */
		d->duration = frame.samples * d->nb_frame / frame.samplerate;
	}
	else if(d->length > 0)
	{
		/* Estimate duration assuming bitrate is constant */
		d->duration = (d->length - d->offset) / (frame.bitrate * 125);
	}

	/* Fill format */
	d->format.samplerate = frame.samplerate;
	d->format.channels = frame.channels;
	d->format.bitrate = frame.bitrate;
	d->format.length = d->duration;

	/* Update samplerate and channels */
	*samplerate = frame.samplerate;
	if(frame.channels == 0)
		*channels = 1;
	else
		*channels = 2;

	return 0;
}

struct file_format *demux_mp3_get_format(struct demux *d)
{
	return &d->format;
}

int demux_mp3_get_dec_config(struct demux *d, int *codec,
			     const unsigned char **config, size_t *size)
{
	*codec = CODEC_MP3;
	*config = NULL;
	*size = 0;
	return 0;
}

ssize_t demux_mp3_next_frame(struct demux *d)
{
	return stream_complete(d->stream, 0);
}

void demux_mp3_set_used(struct demux *d, size_t len)
{
	if(len <= stream_get_len(d->stream))
		stream_seek(d->stream, len, SEEK_CUR);
}

unsigned long demux_mp3_set_pos(struct demux *d, unsigned long pos)
{
	unsigned long f_pos;
	unsigned long f_size;
	float p, fa, fb, fx;
	float a, b;
	int i, j;

	/* Calculate new position */
	if(d->vbri_toc != NULL)
	{
		/* Use TOC from VBRI header */
		i = pos * (d->toc_count - 1) / d->duration;
		i = i > d->toc_count - 1 ? d->toc_count - 1 : i;
		a = i * d->duration * d->toc_count * 1.0;

		fa = 0.0;
		for(j = i; j >= 0; j--)
			fa += (d->vbri_toc[j] * d->toc_scale);

		if (i + 1 < d->toc_count)
		{
			b = (i + 1) * d->duration / d->toc_count;
			fb = fa + (d->vbri_toc[i+1] * d->toc_scale);
		}
		else
		{
			b = d->duration;
			fb = d->nb_bytes;
		}

		f_pos = fa + ((fb - fa) / (b - a)) * ((float)(pos) - a);
	}
	else if(d->toc != NULL)
	{
		/* Use TOC from Xing header */
		p = pos * 100.0 / d->duration;
		if(p > 100.0)
			p = 100.0;
		i = (int) p > 99 ? 99 : (int) p;

		fa = d->toc[i];
		fb = i < 99 ? d->toc[i+1] : 256.0;
		fx = fa + (fb - fa) * (p - i);

		/* Get position */
		f_size = d->nb_bytes > 0 ? d->nb_bytes :
					   d->length - d->offset;
		f_pos = (1.0 / 256.0) * fx * f_size;
	}
	else
	{
		/* Compute aprox position */
		f_pos = (d->length - d->offset) * pos / d->duration;
		if(f_pos > d->length)
			return -1;
	}

	/* Add id3_tag offset */
	f_pos += d->offset;

	/* Seek in file */
	if(stream_seek(d->stream, f_pos, SEEK_SET) != 0)
		return -1;

	return pos;
}

void demux_mp3_close(struct demux *d)
{
	if(d == NULL)
		return;

	/* Free TOC */
	if(d->toc != NULL)
		free(d->toc);
	if(d->vbri_toc != NULL)
		free(d->vbri_toc);

	/* Free handle */
	free(d);
}

struct demux_handle demux_mp3 = {
	.demux = NULL,
	.open = &demux_mp3_open,
	.get_format = &demux_mp3_get_format,
	.get_dec_config = &demux_mp3_get_dec_config,
	.next_frame = &demux_mp3_next_frame,
	.set_used = &demux_mp3_set_used,
	.set_pos = &demux_mp3_set_pos,
	.close = &demux_mp3_close,
};

