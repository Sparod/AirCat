/*
 * decoder_alac.c - A AppleLossless Decoder
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
 *
 * Inspired of:
 * ALAC (Apple Lossless Audio Codec) decoder
 * Copyright (c) 2005 David Hammerton
 * http://crazney.net/programs/itunes/alac.html
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "decoder_alac.h"

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#define PACKET_SIZE 16384
#define BUFFER_SIZE PACKET_SIZE

#define _Swap32(v) do { \
                   v = (((v) & 0x000000FF) << 0x18) | \
                       (((v) & 0x0000FF00) << 0x08) | \
                       (((v) & 0x00FF0000) >> 0x08) | \
                       (((v) & 0xFF000000) >> 0x18); } while(0)

#define _Swap16(v) do { \
                   v = (((v) & 0x00FF) << 0x08) | \
                       (((v) & 0xFF00) >> 0x08); } while (0)


#define SIGN_EXTENDED32(val, bits) ((val << (32 - bits)) >> (32 - bits))

#define SIGN_ONLY(v) \
                     ((v < 0) ? (-1) : \
                                ((v > 0) ? (1) : \
                                           (0)))

struct {signed int x:24;} se_struct_24;

#define SignExtend24(val) (se_struct_24.x = val)

#define RICE_THRESHOLD 8 // maximum number of bits for a rice prefix.

struct alac_decoder {
	/* Input buffers */
	unsigned char *input_buffer;
	int input_buffer_bitaccumulator;
	/* Decoder buffers */
	int32_t *predicterror_buffer[2];
	int32_t *outputsamples_buffer[2];
	int32_t *uncompressed_bytes_buffer[2];
	/* Basic infos */
	uint32_t samplerate;
	int numchannels;
	/* Frame size */
	uint8_t sample_size;
	int bytespersample;
	uint32_t samples_per_frame;
	/* Rice parameters */
	uint8_t rice_historymult;
	uint8_t rice_initialhistory;
	uint8_t rice_kmodifier;
	/* Unused parameters */
	uint8_t info_7a;
	uint8_t info_7f;
	uint16_t info_80;
	uint32_t info_82;
	uint32_t info_86;
};

struct decoder {
	/* PCM output */
	unsigned char buffer[BUFFER_SIZE];
	unsigned long pcm_length;
	unsigned long pcm_remain;
	/* ALAC params */
	struct alac_decoder alac;
};

static const int host_bigendian = 0;

static int decoder_alac_init(struct alac_decoder *alac,
			     const unsigned char *in_buffer, size_t in_size);
static void decoder_alac_decode_frame(struct alac_decoder *alac,
				      unsigned char *in_buffer,
				      void *out_buffer, int *output_size);

int decoder_alac_open(struct decoder **decoder, const unsigned char *config,
		      size_t config_size, unsigned long *samplerate,
		      unsigned char *channels)
{
	struct decoder *dec;

	/* Alloc structure */
	*decoder = malloc(sizeof(struct decoder));
	if(*decoder == NULL)
		return -1;
	dec = *decoder;

	/* Init decoder structure */
	dec->pcm_length = 0;
	dec->pcm_remain = 0;

	/* Init alac decoder with 0 */
	memset(&dec->alac, 0, sizeof(struct alac_decoder));

	/* Init ALAC parameters */
	if(decoder_alac_init(&dec->alac, config, config_size) < 0)
		return -1;

	/* Get samplerate and channels */
	if(samplerate != NULL)
		*samplerate = dec->alac.samplerate;
	if(channels != NULL)
		*channels = dec->alac.numchannels;

	return 0;
}

static long decoder_alac_fill_output(struct decoder *dec,
				     unsigned char *output_buffer,
				     size_t output_size)
{
#ifdef USE_FLOAT
	float *p = (float*) output_buffer;
#else
	int32_t *p = (int32_t*) output_buffer;
#endif 
	unsigned long pos;
	unsigned long size;
	int i;

	pos = (dec->pcm_length - dec->pcm_remain) * 2;
	if(output_size < dec->pcm_remain)
		size = output_size;
	else
		size = dec->pcm_remain;

	/* Copy samples to output buffer */
	for(i = 0; i < size * 2; i += 2)
	{
#ifdef USE_FLOAT
		*p++ = (float)((int32_t) (dec->buffer[i+1+pos] << 24) |
				       (dec->buffer[i+pos] << 16)) / 0x7fffffff;
#else
		*p++ = (int32_t) (dec->buffer[i+1+pos] << 24) |
				 (dec->buffer[i+pos] << 16);
#endif
	}

	dec->pcm_remain -= size;

	return size;
}

int decoder_alac_decode(struct decoder *dec, unsigned char *in_buffer,
			size_t in_size, unsigned char *out_buffer,
			size_t out_size, struct decoder_info *info)
{
	int decode_size;
	int size = 0;

	/* Reset position of PCM output buffer */
	if(in_buffer == NULL && out_buffer == NULL)
	{
		if(out_size > dec->pcm_length)
			out_size = dec->pcm_length;
		dec->pcm_remain = dec->pcm_length - out_size;
		return out_size;
	}

	/* Empty remaining PCM before decoding another frame */
	if(dec->pcm_remain > 0 || in_buffer == NULL)
	{
		size = decoder_alac_fill_output(dec, out_buffer, out_size);

		/* Fill decoder info */
		info->used = 0;
		info->remaining = dec->pcm_remain;
		info->samplerate = dec->alac.samplerate;
		info->channels = dec->alac.numchannels;

		return size;
	}

	/* Check input size */
	if(in_size == 0)
		return 0;

	/* Decode the frame */
	decoder_alac_decode_frame(&dec->alac, in_buffer, dec->buffer,
					  &decode_size);
					  
	if(decode_size <= 0)
		return -1;

	/* Fill output buffer with PCM */
	dec->pcm_remain = decode_size / 2;
	dec->pcm_length = decode_size / 2;

	size = decoder_alac_fill_output(dec, out_buffer, out_size);

	/* Fill decoder info */
	info->used = in_size;
	info->remaining = dec->pcm_remain;
	info->samplerate = dec->alac.samplerate;
	info->channels = dec->alac.numchannels;

	return size;
}

int decoder_alac_close(struct decoder *dec)
{
	if(dec == NULL)
		return 0;

	/* Free decoder */
	free(dec);

	return 0;
}

struct decoder_handle decoder_alac = {
	.dec = NULL,
	.open = &decoder_alac_open,
	.decode = &decoder_alac_decode,
	.close = &decoder_alac_close,
};

static int decoder_alac_init(struct alac_decoder *alac,
			     const unsigned char *in_buffer, size_t in_size)
{
	const unsigned char *ptr = in_buffer;
	int i;

	if(in_size < 55)
		return -1;

	/* Get ALAC parameters from buffer */
	ptr += 4; /* size */
	ptr += 4; /* frma */
	ptr += 4; /* alac */
	ptr += 4; /* size */
	ptr += 4; /* alac */
	ptr += 4; /* 0 ? */
	alac->samples_per_frame = *(uint32_t*)ptr; /* buffer size / 2 ? */
	if (!host_bigendian)
		_Swap32(alac->samples_per_frame);
	ptr += 4;
	alac->info_7a = *(uint8_t*)ptr;
	ptr += 1;
	alac->sample_size = *(uint8_t*)ptr;
	ptr += 1;
	alac->rice_historymult = *(uint8_t*)ptr;
	ptr += 1;
	alac->rice_initialhistory = *(uint8_t*)ptr;
	ptr += 1;
	alac->rice_kmodifier = *(uint8_t*)ptr;
	ptr += 1;
	alac->numchannels = *(uint8_t*)ptr;
	ptr += 1;
	alac->info_80 = *(uint16_t*)ptr;
	if (!host_bigendian)
		_Swap16(alac->info_80);
	ptr += 2;
	alac->info_82 = *(uint32_t*)ptr;
	if (!host_bigendian)
		_Swap32(alac->info_82);
	ptr += 4;
	alac->info_86 = *(uint32_t*)ptr;
	if (!host_bigendian)
		_Swap32(alac->info_86);
	ptr += 4;
	alac->samplerate = *(uint32_t*)ptr;
	if (!host_bigendian)
		_Swap32(alac->samplerate);

	alac->bytespersample = (alac->sample_size / 8) * alac->numchannels;

	/* Allocate buffers */
	for(i = 0; i < 2; i++)
	{
		alac->predicterror_buffer[i] = malloc(alac->samples_per_frame * 4);
		alac->outputsamples_buffer[i] = malloc(alac->samples_per_frame * 4);
		alac->uncompressed_bytes_buffer[i] = malloc(alac->samples_per_frame * 4);
	}

	return 0;
}

static uint32_t decoder_alac_readbits_16(struct alac_decoder *alac, int bits)
{
	uint32_t result;
	int new_accumulator;

	result = (alac->input_buffer[0] << 16) | (alac->input_buffer[1] << 8) | (alac->input_buffer[2]);

	/* shift left by the number of bits we've already read,
	* so that the top 'n' bits of the 24 bits we read will
	* be the return bits */
	result = result << alac->input_buffer_bitaccumulator;

	result = result & 0x00ffffff;

	/* and then only want the top 'n' bits from that, where
	* n is 'bits' */
	result = result >> (24 - bits);

	new_accumulator = (alac->input_buffer_bitaccumulator + bits);

	/* increase the buffer pointer if we've read over n bytes. */
	alac->input_buffer += (new_accumulator >> 3);

	/* and the remainder goes back into the bit accumulator */
	alac->input_buffer_bitaccumulator = (new_accumulator & 7);

	return result;
}

static uint32_t decoder_alac_readbits(struct alac_decoder *alac, int bits)
{
	int32_t result = 0;

	if (bits > 16)
	{
		bits -= 16;
		result = decoder_alac_readbits_16(alac, 16) << bits;
	}

	result |= decoder_alac_readbits_16(alac, bits);

	return result;
}

static int decoder_alac_readbit(struct alac_decoder *alac)
{
	int new_accumulator;
	int result;

	result = alac->input_buffer[0];
	result = result << alac->input_buffer_bitaccumulator;
	result = result >> 7 & 1;

	new_accumulator = (alac->input_buffer_bitaccumulator + 1);

	alac->input_buffer += (new_accumulator / 8);

	alac->input_buffer_bitaccumulator = (new_accumulator % 8);

	return result;
}

static void decoder_alac_unreadbits(struct alac_decoder *alac, int bits)
{
	int new_accumulator = (alac->input_buffer_bitaccumulator - bits);

	alac->input_buffer += (new_accumulator >> 3);

	alac->input_buffer_bitaccumulator = (new_accumulator & 7);
	if (alac->input_buffer_bitaccumulator < 0)
		alac->input_buffer_bitaccumulator *= -1;
}

/* various implementations of count_leading_zero:
 * the first one is the original one, the simplest and most
 * obvious for what it's doing. never use this.
 * then there are the asm ones. fill in as necessary
 * and finally an unrolled and optimised c version
 * to fall back to
 */
#if 0
/* hideously inefficient. could use a bitmask search,
 * alternatively bsr on x86,
 */
static int decoder_alac_count_leading_zeros(int32_t input)
{
	int i = 0;
	while (!(0x80000000 & input) && i < 32)
	{
		i++;
		input = input << 1;
	}
	return i;
}
#elif defined(__GNUC__)
/* for some reason the unrolled version (below) is
 * actually faster than this. yay intel!
 */
static int decoder_alac_count_leading_zeros(int input)
{
	return __builtin_clz(input);
}
#elif defined(_MSC_VER) && defined(_M_IX86)
static int decoder_alac_count_leading_zeros(int input)
{
	int output = 0;
	if (!input)
		return 32;
	__asm
	{
		mov eax, input;
		mov edx, 0x1f;
		bsr ecx, eax;
		sub edx, ecx;
		mov output, edx;
	}
	return output;
}
#else
#warning using generic count leading zeroes. You may wish to write one for your CPU / compiler
static int decoder_alac_count_leading_zeros(int input)
{
	int output = 0;
	int curbyte = 0;

	curbyte = input >> 24;
	if (curbyte)
		goto found;
	output += 8;

	curbyte = input >> 16;
	if (curbyte & 0xff)
		goto found;
	output += 8;

	curbyte = input >> 8;
	if (curbyte & 0xff)
		goto found;
	output += 8;

	curbyte = input;
	if (curbyte & 0xff)
		goto found;
	output += 8;

	return output;

found:
	if (!(curbyte & 0xf0))
		output += 4;
	else
		curbyte >>= 4;

	if (curbyte & 0x8)
		return output;
	if (curbyte & 0x4)
		return output + 1;
	if (curbyte & 0x2)
		return output + 2;
	if (curbyte & 0x1)
		return output + 3;

	/* shouldn't get here: */
	return output + 4;
}
#endif

static int32_t decoder_alac_entropy_decode_value(struct alac_decoder *alac, int readSampleSize, int k, int rice_kmodifier_mask)
{
	int32_t x = 0; // decoded value
	int32_t value;
	int extraBits;

	// read x, number of 1s before 0 represent the rice value.
	while (x <= RICE_THRESHOLD && decoder_alac_readbit(alac))
		x++;

	if (x > RICE_THRESHOLD)
	{
		// read the number from the bit stream (raw value)
		value = decoder_alac_readbits(alac, readSampleSize);

		// mask value
		value &= (((uint32_t)0xffffffff) >> (32 - readSampleSize));
		x = value;
	}
	else
	{
		if (k != 1)
		{
			extraBits = decoder_alac_readbits(alac, k);
			// x = x * (2^k - 1)
			x *= (((1 << k) - 1) & rice_kmodifier_mask);

			if (extraBits > 1)
				x += extraBits - 1;
			else
				decoder_alac_unreadbits(alac, 1);
		}
	}

	return x;
}

static void decoder_alac_entropy_rice_decode(struct alac_decoder *alac, int32_t* outputBuffer, int outputSize, int readSampleSize, int rice_initialhistory, int rice_kmodifier, int rice_historymult, int rice_kmodifier_mask)
{
	int32_t decodedValue;
	int32_t finalValue;
	int32_t blockSize;
	int32_t k;
	int history = rice_initialhistory;
	int signModifier = 0;
	int outputCount;

	for (outputCount = 0; outputCount < outputSize; outputCount++)
	{
		k = 31 - rice_kmodifier - decoder_alac_count_leading_zeros((history >> 9) + 3);

		if (k < 0)
			k += rice_kmodifier;
		else
			k = rice_kmodifier;

		// note: don't use rice_kmodifier_mask here (set mask to 0xFFFFFFFF)
		decodedValue = decoder_alac_entropy_decode_value(alac, readSampleSize, k, 0xFFFFFFFF);

		decodedValue += signModifier;
		finalValue = (decodedValue + 1) / 2; // inc by 1 and shift out sign bit
		if (decodedValue & 1) // the sign is stored in the low bit
			finalValue *= -1;

		outputBuffer[outputCount] = finalValue;

		signModifier = 0;

		// update history
		history += (decodedValue * rice_historymult) - ((history * rice_historymult) >> 9);

		if (decodedValue > 0xFFFF)
			history = 0xFFFF;

		// special case, for compressed blocks of 0
		if ((history < 128) && (outputCount + 1 < outputSize))
		{
			signModifier = 1;

			k = decoder_alac_count_leading_zeros(history) + ((history + 16) / 64) - 24;

			// note: blockSize is always 16bit
			blockSize = decoder_alac_entropy_decode_value(alac, 16, k, rice_kmodifier_mask);

			// got blockSize 0s
			if (blockSize > 0)
			{
				memset(&outputBuffer[outputCount + 1], 0, blockSize * sizeof(*outputBuffer));
				outputCount += blockSize;
			}

			if (blockSize > 0xFFFF)
				signModifier = 0;

			history = 0;
		}
	}
}

static void decoder_alac_predictor_decompress_fir_adapt(int32_t *error_buffer, int32_t *buffer_out, int output_size, int readsamplesize, int16_t *predictor_coef_table, int predictor_coef_num, int predictor_quantitization)
{
	int32_t prev_value;
	int32_t error_value;
	int32_t val;
	int predictor_num;
	int error_val;
	int outval;
	int sign;
	int sum;
	int i, j;

	/* first sample always copies */
	*buffer_out = *error_buffer;

	if (!predictor_coef_num)
	{
		if (output_size <= 1)
			return;
		memcpy(buffer_out+1, error_buffer+1, (output_size-1) * 4);
		return;
	}

	if (predictor_coef_num == 0x1f) /* 11111 - max value of predictor_coef_num */
	{
		/* second-best case scenario for fir decompression,
		 * error describes a small difference from the previous sample only
		 */
		if (output_size <= 1)
			return;

		for (i = 0; i < output_size - 1; i++)
		{
			prev_value = buffer_out[i];
			error_value = error_buffer[i+1];
			buffer_out[i+1] = SIGN_EXTENDED32((prev_value + error_value), readsamplesize);
		}
		return;
	}

	/* read warm-up samples */
	if (predictor_coef_num > 0)
	{
		for (i = 0; i < predictor_coef_num; i++)
		{
			val = buffer_out[i] + error_buffer[i+1];
			val = SIGN_EXTENDED32(val, readsamplesize);
			buffer_out[i+1] = val;
		}
	}

#if 0
	/* 4 and 8 are very common cases (the only ones i've seen). these
	 * should be unrolled and optimised
	 */
	if (predictor_coef_num == 4)
	{
		/* FIXME: optimised general case */
		return;
	}

	if (predictor_coef_table == 8)
	{
		/* FIXME: optimised general case */
		return;
	}
#endif

	/* general case */
	if (predictor_coef_num > 0)
	{
		for (i = predictor_coef_num + 1; i < output_size; i++)
		{
			sum = 0;
			error_val = error_buffer[i];

			for (j = 0; j < predictor_coef_num; j++)
			{
				sum += (buffer_out[predictor_coef_num-j] - buffer_out[0]) * predictor_coef_table[j];
			}

			outval = (1 << (predictor_quantitization-1)) + sum;
			outval = outval >> predictor_quantitization;
			outval = outval + buffer_out[0] + error_val;
			outval = SIGN_EXTENDED32(outval, readsamplesize);

			buffer_out[predictor_coef_num+1] = outval;

			if (error_val > 0)
			{
				predictor_num = predictor_coef_num - 1;

				while (predictor_num >= 0 && error_val > 0)
				{
					val = buffer_out[0] - buffer_out[predictor_coef_num - predictor_num];
					sign = SIGN_ONLY(val);

					predictor_coef_table[predictor_num] -= sign;

					val *= sign; /* absolute value */

					error_val -= ((val >> predictor_quantitization) * (predictor_coef_num - predictor_num));

					predictor_num--;
				}
			}
			else if (error_val < 0)
			{
				predictor_num = predictor_coef_num - 1;

				while (predictor_num >= 0 && error_val < 0)
				{
					val = buffer_out[0] - buffer_out[predictor_coef_num - predictor_num];
					sign = - SIGN_ONLY(val);

					predictor_coef_table[predictor_num] -= sign;

					val *= sign; /* neg value */

					error_val -= ((val >> predictor_quantitization) * (predictor_coef_num - predictor_num));

					predictor_num--;
				}
			}
			buffer_out++;
		}
	}
}

static void decoder_alac_deinterlace_16(int32_t *buffer_a, int32_t *buffer_b, int16_t *buffer_out, int numchannels, int numsamples, uint8_t interlacing_shift, uint8_t interlacing_leftweight)
{
	int32_t difference, midright;
	int16_t left, right;
	int i;

	if (numsamples <= 0)
		return;

	/* weighted interlacing */
	if (interlacing_leftweight)
	{
		for (i = 0; i < numsamples; i++)
		{
			midright = buffer_a[i];
			difference = buffer_b[i];

			right = midright - ((difference * interlacing_leftweight) >> interlacing_shift);
			left = right + difference;

			/* output is always little endian */
			if (host_bigendian)
			{
				_Swap16(left);
				_Swap16(right);
			}

			buffer_out[i*numchannels] = left;
			buffer_out[i*numchannels + 1] = right;
		}
		return;
	}

	/* otherwise basic interlacing took place */
	for (i = 0; i < numsamples; i++)
	{
		left = buffer_a[i];
		right = buffer_b[i];

		/* output is always little endian */
		if (host_bigendian)
		{
			_Swap16(left);
			_Swap16(right);
		}

		buffer_out[i*numchannels] = left;
		buffer_out[i*numchannels + 1] = right;
	}
}

static void decoder_alac_deinterlace_24(int32_t *buffer_a, int32_t *buffer_b, int uncompressed_bytes, int32_t *uncompressed_bytes_buffer_a, int32_t *uncompressed_bytes_buffer_b, void *buffer_out, int numchannels, int numsamples, uint8_t interlacing_shift, uint8_t interlacing_leftweight)
{
	int32_t difference, midright;
	int32_t left, right;
	int i;

	if (numsamples <= 0)
		return;

	/* weighted interlacing */
	if (interlacing_leftweight)
	{
		for (i = 0; i < numsamples; i++)
		{
			midright = buffer_a[i];
			difference = buffer_b[i];

			right = midright - ((difference * interlacing_leftweight) >> interlacing_shift);
			left = right + difference;

			if (uncompressed_bytes)
			{
				uint32_t mask = ~(0xFFFFFFFF << (uncompressed_bytes * 8));
				left <<= (uncompressed_bytes * 8);
				right <<= (uncompressed_bytes * 8);

				left |= uncompressed_bytes_buffer_a[i] & mask;
				right |= uncompressed_bytes_buffer_b[i] & mask;
			}

			((uint8_t*)buffer_out)[i * numchannels * 3] = (left) & 0xFF;
			((uint8_t*)buffer_out)[i * numchannels * 3 + 1] = (left >> 8) & 0xFF;
			((uint8_t*)buffer_out)[i * numchannels * 3 + 2] = (left >> 16) & 0xFF;

			((uint8_t*)buffer_out)[i * numchannels * 3 + 3] = (right) & 0xFF;
			((uint8_t*)buffer_out)[i * numchannels * 3 + 4] = (right >> 8) & 0xFF;
			((uint8_t*)buffer_out)[i * numchannels * 3 + 5] = (right >> 16) & 0xFF;
		}
		return;
	}

	/* otherwise basic interlacing took place */
	for (i = 0; i < numsamples; i++)
	{
		left = buffer_a[i];
		right = buffer_b[i];

		if (uncompressed_bytes)
		{
			uint32_t mask = ~(0xFFFFFFFF << (uncompressed_bytes * 8));
			left <<= (uncompressed_bytes * 8);
			right <<= (uncompressed_bytes * 8);

			left |= uncompressed_bytes_buffer_a[i] & mask;
			right |= uncompressed_bytes_buffer_b[i] & mask;
		}

		((uint8_t*)buffer_out)[i * numchannels * 3] = (left) & 0xFF;
		((uint8_t*)buffer_out)[i * numchannels * 3 + 1] = (left >> 8) & 0xFF;
		((uint8_t*)buffer_out)[i * numchannels * 3 + 2] = (left >> 16) & 0xFF;

		((uint8_t*)buffer_out)[i * numchannels * 3 + 3] = (right) & 0xFF;
		((uint8_t*)buffer_out)[i * numchannels * 3 + 4] = (right >> 8) & 0xFF;
		((uint8_t*)buffer_out)[i * numchannels * 3 + 5] = (right >> 16) & 0xFF;
	}
}

void decoder_alac_decode_frame(struct alac_decoder *alac, unsigned char *inbuffer, void *outbuffer, int *outputsize)
{
	int32_t audiobits;
	int32_t outputsamples;
	int16_t predictor_coef_table[2][32];
	uint8_t interlacing_shift;
	uint8_t interlacing_leftweight;
	int predictor_coef_num[2];
	int prediction_type[2];
	int prediction_quantitization[2];
	int ricemodifier[2];
	int uncompressed_bytes;
	int isnotcompressed;
	int readsamplesize;
	int channels;
	int hassize;
	int i, j;

	outputsamples = alac->samples_per_frame;

	/* setup the stream */
	alac->input_buffer = inbuffer;
	alac->input_buffer_bitaccumulator = 0;

	channels = decoder_alac_readbits(alac, 3);

	*outputsize = outputsamples * alac->bytespersample;

	/* 2^result = something to do with output waiting.
	 * perhaps matters if we read > 1 frame in a pass?
	 */
	decoder_alac_readbits(alac, 4);
	decoder_alac_readbits(alac, 12); /* unknown, skip 12 bits */
	hassize = decoder_alac_readbits(alac, 1); /* the output sample size is stored soon */
	uncompressed_bytes = decoder_alac_readbits(alac, 2); /* number of bytes in the (compressed) stream that are not compressed */
	isnotcompressed = decoder_alac_readbits(alac, 1); /* whether the frame is compressed */

	if (hassize)
	{
		/* now read the number of samples,
		 * as a 32bit integer */
		outputsamples = decoder_alac_readbits(alac, 32);
		*outputsize = outputsamples * alac->bytespersample;
	}

	readsamplesize = alac->sample_size - (uncompressed_bytes * 8) + channels;

	if (!isnotcompressed)
	{
		/* Compressed case */
		interlacing_shift = decoder_alac_readbits(alac, 8);
		interlacing_leftweight = decoder_alac_readbits(alac, 8);

		for(i = 0; i <= channels; i++)
		{
			prediction_type[i] = decoder_alac_readbits(alac, 4);
			prediction_quantitization[i] = decoder_alac_readbits(alac, 4);

			ricemodifier[i] = decoder_alac_readbits(alac, 3);
			predictor_coef_num[i] = decoder_alac_readbits(alac, 5);

			/* read the predictor table */
			for (j = 0; j < predictor_coef_num[i]; j++)
			{
				predictor_coef_table[i][j] = (int16_t)decoder_alac_readbits(alac, 16);
			}
		}

		if (uncompressed_bytes)
		{
			for (i = 0; i < outputsamples; i++)
			{
				for(j = 0; j <= channels; j++)
				{
					alac->uncompressed_bytes_buffer[j][i] = decoder_alac_readbits(alac, uncompressed_bytes * 8);
				}
			}
		}

		for(i = 0; i <= channels; i++)
		{
			decoder_alac_entropy_rice_decode(alac, alac->predicterror_buffer[i], outputsamples, readsamplesize, alac->rice_initialhistory, alac->rice_kmodifier, ricemodifier[i] * alac->rice_historymult / 4, (1 << alac->rice_kmodifier) - 1);

			if (prediction_type[i] == 0)
			{
				/* adaptive fir */
				decoder_alac_predictor_decompress_fir_adapt(alac->predicterror_buffer[i], alac->outputsamples_buffer[i], outputsamples, readsamplesize, predictor_coef_table[i], predictor_coef_num[i], prediction_quantitization[i]);
			}
			else
			{
				fprintf(stderr, "FIXME: unhandled predicition type: %i\n", prediction_type[i]);
				/* i think the only other prediction type (or perhaps this is just a
				 * boolean?) runs adaptive fir twice.. like:
				 * decoder_alac_predictor_decompress_fir_adapt(predictor_error, tempout, ...)
				 * decoder_alac_predictor_decompress_fir_adapt(predictor_error, outputsamples ...)
				 * little strange..
				 */
			}
		}
	}
	else
	{
		/* not compressed, easy case */
		if (alac->sample_size <= 16)
		{
			for (i = 0; i < outputsamples; i++)
			{
				for(j = 0; j <= channels; j++)
				{
					audiobits = decoder_alac_readbits(alac, alac->sample_size);
					audiobits = SIGN_EXTENDED32(audiobits, alac->sample_size);
					alac->outputsamples_buffer[j][i] = audiobits;
				}
			}
		}
		else
		{
			for (i = 0; i < outputsamples; i++)
			{
				for(j = 0; j <= channels; j++)
				{
					audiobits = decoder_alac_readbits(alac, 16);
					audiobits = audiobits << (alac->sample_size - 16);
					audiobits |= decoder_alac_readbits(alac, alac->sample_size - 16);
					audiobits = SignExtend24(audiobits);
					alac->outputsamples_buffer[j][i] = audiobits;
				}
			}
		}
		uncompressed_bytes = 0; // always 0 for uncompressed
		interlacing_shift = 0;
		interlacing_leftweight = 0;
	}

	switch(alac->sample_size)
	{
		case 16:
		{
			if(channels == 0)
			{
				for (i = 0; i < outputsamples; i++)
				{
					int16_t sample = alac->outputsamples_buffer[0][i];
					if (host_bigendian)
						_Swap16(sample);
					((int16_t*)outbuffer)[i * alac->numchannels] = sample;
				}
			}
			else
				decoder_alac_deinterlace_16(alac->outputsamples_buffer[0], alac->outputsamples_buffer[1], (int16_t*)outbuffer, alac->numchannels, outputsamples, interlacing_shift, interlacing_leftweight);
			break;
		}
		case 24:
		{
			if(channels == 0)
			{
				for (i = 0; i < outputsamples; i++)
				{
					int32_t sample = alac->outputsamples_buffer[0][i];

					if (uncompressed_bytes)
					{
						uint32_t mask;
						sample = sample << (uncompressed_bytes * 8);
						mask = ~(0xFFFFFFFF << (uncompressed_bytes * 8));
						sample |= alac->uncompressed_bytes_buffer[0][i] & mask;
					}

					((uint8_t*)outbuffer)[i * alac->numchannels * 3] = (sample) & 0xFF;
					((uint8_t*)outbuffer)[i * alac->numchannels * 3 + 1] = (sample >> 8) & 0xFF;
					((uint8_t*)outbuffer)[i * alac->numchannels * 3 + 2] = (sample >> 16) & 0xFF;
				}
			}
			else
				decoder_alac_deinterlace_24(alac->outputsamples_buffer[0], alac->outputsamples_buffer[1], uncompressed_bytes, alac->uncompressed_bytes_buffer[0], alac->uncompressed_bytes_buffer[1], (int16_t*)outbuffer, alac->numchannels, outputsamples, interlacing_shift, interlacing_leftweight);
			break;
		}
		case 20:
		case 32:
			fprintf(stderr, "FIXME: unimplemented sample size %i\n", alac->sample_size);
			break;
		default:
			break;
	}
}
