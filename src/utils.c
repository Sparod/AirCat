/*
 * utils.c - Some common function
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "utils.h"

static char base64_table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
			     "abcdefghijklmnopqrstuvwxyz"
			     "0123456789+/";

char *base64_encode(const char *buffer, int length)
{
	unsigned char *s = (unsigned char*) buffer;
	char *output, *p;

	output = malloc(((4*((length+2)/3))+1)*sizeof(char));
	if(output == NULL)
		return NULL;

	p = output;

	/* Function from libbb of BusyBox */
	/* Transform the 3x8 bits to 4x6 bits */
	while (length > 0)
	{
		unsigned s1, s2;

		/* Are s[1], s[2] valid or should be assumed 0? */
		s1 = s2 = 0;
		length -= 3; /* can be >=0, -1, -2 */
		if (length >= -1)
		{
			s1 = s[1];
			if (length >= 0)
				s2 = s[2];
		}
		*p++ = base64_table[s[0] >> 2];
		*p++ = base64_table[((s[0] & 3) << 4) + (s1 >> 4)];
		*p++ = base64_table[((s1 & 0xf) << 2) + (s2 >> 6)];
		*p++ = base64_table[s2 & 0x3f];
		s += 3;
	}
	/* Zero-terminate */
	*p = '\0';
	/* If length is -2 or -1, pad last char or two */
	while (length)
	{
		*--p = base64_table[64];
		length++;
	}

	return output;
}

int base64_decode(char *buffer)
{
	const unsigned char *in = (const unsigned char *)buffer;
	const char *s = (const char *)buffer;
	unsigned ch = 0;
	int i = 0;

	while (*in) {
		int t = *in++;

		if (t >= '0' && t <= '9')
			t = t - '0' + 52;
		else if (t >= 'A' && t <= 'Z')
			t = t - 'A';
		else if (t >= 'a' && t <= 'z')
			t = t - 'a' + 26;
		else if (t == '+')
			t = 62;
		else if (t == '/')
			t = 63;
		else if (t == '=')
			t = 0;
		else
			continue;

		ch = (ch << 6) | t;
		i++;
		if (i == 4)
		{
			*buffer++ = (char) (ch >> 16);
			*buffer++ = (char) (ch >> 8);
			*buffer++ = (char) ch;
			i = 0;
		}
	}

	/* Padding */
	if (i != 0)
	{
		while (i--)
			ch = (ch << 6) | 0;
		*buffer++ = (char) (ch >> 16);
		*buffer++ = (char) (ch >> 8);
		*buffer++ = (char) ch;
	}

	*buffer = '\0';

	return (int)(buffer - s);
}

