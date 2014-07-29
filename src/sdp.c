/*
 * sdp.c - A SDP tool
 *
 * Copyright (c) 2013   A. Dilly
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
#include "sdp.h"

static int sdp_next_line(char c, char **value, char *buffer, int len)
{
	int i = 0;

	if(len < 1 || buffer[0] != c)
		return 0;

	if(len > 2 && buffer[1] == '=')
	{
		/* Find end of line or string */
		for(i = 2; i < len; i++)
			if(buffer[i] == '\n' || buffer[i] == '\0')
				break;
		/* Replace \n by a \0 */
		buffer[i-1] = '\0';
		buffer[i] = '\0';
		/* Copy the string */
		*value = strdup(&buffer[2]);
		i++;
	}

	return i;
}

static int sdp_next_lines(char c, char ***table, int *nb, char *buffer, int len)
{
	int count = 0;
	int i, k = 0;
	char **v;

	/* Count nb of lines */
	while(k < len && buffer[k] == c)
	{
		for(; k < len; k++)
			if(buffer[k] == '\n' || buffer[k] == '\0')
				break;
		k++;
		count++;
	}
	*nb = count;
	if(count == 0)
		return 0;

	/* Allocate table */
	v = malloc(sizeof(char*)*count);

	/* Fill table */
	k = 0;
	for(i = 0; i < count; i++)
		k += sdp_next_line(c, v+i, &buffer[k], len-k);

	*table = v;

	return k;
}

static int sdp_count_lines(char c, char *buffer, int len)
{
	int count = 0;
	int k = 0;

	/* Count nb of lines */
	while(k < len)
	{
		if(buffer[k] == c)
			count++;

		for(; k < len; k++)
			if(buffer[k] == '\n' || buffer[k] == '\0')
				break;
		k++;
	}

	return count;
}

struct sdp *sdp_parse(char *buffer, size_t len)
{
	struct sdp *s;
	int i, j;

	/* Verify buffer */
	if(buffer == NULL || len == 0)
		return NULL;

	/* Allocate structure */
	s = malloc(sizeof(struct sdp));
	if(s == NULL)
		return NULL;

	/* Init structure */
	memset(s, 0, sizeof(struct sdp));

	/* Parse buffer and fill structure */
	i = sdp_next_line('v', &s->version, buffer, len);
	i += sdp_next_line('o', &s->origin, &buffer[i], len-i);
	i += sdp_next_line('s', &s->session, &buffer[i], len-i);
	i += sdp_next_line('i', &s->title, &buffer[i], len-i);
	i += sdp_next_line('u', &s->uri, &buffer[i], len-i);
	i += sdp_next_lines('e', &s->email, &s->nb_email, &buffer[i], len-i);
	i += sdp_next_lines('p', &s->phone, &s->nb_phone, &buffer[i], len-i);
	i += sdp_next_line('c', &s->connect, &buffer[i], len-i);
	i += sdp_next_lines('b', &s->bandw, &s->nb_bandw, &buffer[i], len-i);
	
	/* Times */
	s->nb_times = sdp_count_lines('t', &buffer[i], len-i);
	if(s->nb_times > 0)
	{
		s->times = malloc(sizeof(struct sdp_time)*s->nb_times);
		memset(s->times, 0, sizeof(struct sdp_time)*s->nb_times);
		for(j = 0; j < s->nb_times; j++)
		{
			i += sdp_next_line('t', &s->times[j].time, &buffer[i], len-i);
			i += sdp_next_lines('r', &s->times[j].repeat, &s->times[j].nb_repeat, &buffer[i], len-i);
		}
	}
	
	i += sdp_next_line('z', &s->zone, &buffer[i], len-i);
	i += sdp_next_line('k', &s->key, &buffer[i], len-i);
	i += sdp_next_lines('a', &s->attr, &s->nb_attr, &buffer[i], len-i);
	
	/* Medias */
	s->nb_medias = sdp_count_lines('m', &buffer[i], len-i);
	if(s->nb_medias > 0)
	{
		s->medias = malloc(sizeof(struct sdp_media)*s->nb_medias);
		memset(s->medias, 0, sizeof(struct sdp_media)*s->nb_medias);
		for(j = 0; j < s->nb_medias; j++)
		{
			i += sdp_next_line('m', &s->medias[j].media, &buffer[i], len-i);
			i += sdp_next_line('i', &s->medias[j].title, &buffer[i], len-i);
			i += sdp_next_line('c', &s->medias[j].connect, &buffer[i], len-i);
			i += sdp_next_lines('b', &s->medias[j].bandw, &s->medias[j].nb_bandw, &buffer[i], len-i);
			i += sdp_next_line('k', &s->medias[j].key, &buffer[i], len-i);
			i += sdp_next_lines('a', &s->medias[j].attr, &s->medias[j].nb_attr, &buffer[i], len-i);
		}
	}

	return s;
}

int sdp_generate(struct sdp *s, char *buffer, size_t len)
{
	int size = 0;
	int i, j;

	/* Protocol version, Originate id and Session name */
	size = snprintf(buffer, len, "v=%s\n"
				     "o=%s\n"
				     "s=%s",
				      s->version, s->origin, s->session);

	/* Session title (optional) */
	if(s->title != NULL)
		size += snprintf(buffer+size, len-size, "\ni=%s", s->title);

	/* URI (optional) */
	if(s->uri != NULL)
		size += snprintf(buffer+size, len-size, "\nu=%s", s->uri);

	/* Email address (optional) */
	for(i = 0; i < s->nb_email; i++)
		size += snprintf(buffer+size, len-size, "\ne=%s", s->email[i]);

	/* Phone number (optional) */
	for(i = 0; i < s->nb_phone; i++)
		size += snprintf(buffer+size, len-size, "\np=%s", s->phone[i]);

	/* Connection info (optional) */
	if(s->connect != NULL)
		size += snprintf(buffer+size, len-size, "\nc=%s", s->connect);

	/* Bandwidth (optional) */
	for(i = 0; i < s->nb_bandw; i++)
		size += snprintf(buffer+size, len-size, "\nb=%s", s->bandw[i]);

	/* Times description (optional) */
	for(i = 0; i < s->nb_times; i++)
	{
		/* Time name */
		size += snprintf(buffer+size, len-size, "\nt=%s", s->times[i].time);

		/* Repeat time (optional) */
		for(j = 0; j < s->times[i].nb_repeat; j++)
			size += snprintf(buffer+size, len-size, "\nr=%s", s->times[i].repeat[j]);
	}

	/* Time zone (optional) */
	if(s->zone != NULL)
		size += snprintf(buffer+size, len-size, "\nz=%s", s->zone);

	/* Encrypt key (optional) */
	if(s->key != NULL)
		size += snprintf(buffer+size, len-size, "\nk=%s", s->key);

	/* Session attributes (optional) */
	for(i = 0; i < s->nb_attr; i++)
		size += snprintf(buffer+size, len-size, "\na=%s", s->attr[i]);

	/* Medias description (optional) */
	for(i = 0; i < s->nb_medias; i++)
	{
		/* Media name */
		size += snprintf(buffer+size, len-size, "\nm=%s", s->medias[i].media);

		/* Media title (optional) */
		if(s->medias[i].title != NULL)
			size += snprintf(buffer+size, len-size, "\ni=%s", s->medias[i].title);

		/* Connection info (optional) */
		if(s->medias[i].connect != NULL)
			size += snprintf(buffer+size, len-size, "\nc=%s", s->medias[i].connect);

		/* Media bandwidth (optional) */
		for(j = 0; j < s->medias[i].nb_bandw; j++)
			size += snprintf(buffer+size, len-size, "\nb=%s", s->medias[i].bandw[j]);

		/* Encrypt key (optional) */
		if(s->medias[i].key != NULL)
			size += snprintf(buffer+size, len-size, "\nk=%s", s->medias[i].key);

		/* Media attributes (optional) */
		for(j = 0; j < s->medias[i].nb_attr; j++)
			size += snprintf(buffer+size, len-size, "\na=%s", s->medias[i].attr[j]);
	}

	return size;
}

static void sdp_free_times(struct sdp *s)
{
	int i, j;

	if(s->nb_times == 0 || s->times == NULL)
		return;

	for(i = 0; i < s->nb_times; i++)
	{
		/* Time name */
		if(s->times[i].time != NULL)
			free(s->times[i].time);

		/* Time repeat */
		for(j = 0; j < s->times[i].nb_repeat; j++)
			if(s->times[i].repeat[j] != NULL)
				free(s->times[i].repeat[j]);
		if(s->times[i].repeat != NULL)
			free(s->times[i].repeat);
	}

	free(s->times);
	s->nb_times = 0;
	s->times = NULL;
}

static void sdp_free_medias(struct sdp *s)
{
	int i, j;

	if(s->nb_medias == 0 || s->medias == NULL)
		return;

	for(i = 0; i < s->nb_medias; i++)
	{
		/* Media name */
		if(s->medias[i].media != NULL)
			free(s->medias[i].media);

		/* Media title / info */
		if(s->medias[i].title != NULL)
			free(s->medias[i].title);

		/* Media connection */
		if(s->medias[i].connect != NULL)
			free(s->medias[i].connect);

		/* Media bandwidth */
		for(j = 0; j < s->medias[i].nb_bandw; j++)
			if(s->medias[i].bandw[j] != NULL)
				free(s->medias[i].bandw[j]);
		if(s->medias[i].bandw != NULL)
			free(s->medias[i].bandw);

		/* Media encryption key */
		if(s->medias[i].key != NULL)
			free(s->medias[i].key);

		/* Media attributes */
		for(j = 0; j < s->medias[i].nb_attr; j++)
			if(s->medias[i].attr[j] != NULL)
				free(s->medias[i].attr[j]);
		if(s->medias[i].attr != NULL)
			free(s->medias[i].attr);
	}

	free(s->medias);
	s->nb_medias = 0;
	s->medias = NULL;
}

void sdp_free(struct sdp *s)
{
	int i;

	if(s == NULL)
		return;

	/* Protocol version */
	if(s->version != NULL)
		free(s->version);

	/* Originator id */
	if(s->origin != NULL)
		free(s->origin);

	/* Session name */
	if(s->session != NULL)
		free(s->session);

	/* Session title */
	if(s->title != NULL)
		free(s->title);

	/* URI */
	if(s->uri != NULL)
		free(s->uri);

	/* Email address */
	for(i = 0; i < s->nb_email; i++)
		if(s->email[i] != NULL)
			free(s->email[i]);
	if(s->email != NULL)
		free(s->email);

	/* Phone number */
	for(i = 0; i < s->nb_phone; i++)
		if(s->phone[i] != NULL)
			free(s->phone[i]);
	if(s->phone != NULL)
		free(s->phone);

	/* Connection informations */
	if(s->connect != NULL)
		free(s->connect);

	/* Bandwidth */
	for(i = 0; i < s->nb_bandw; i++)
		if(s->bandw[i] != NULL)
			free(s->bandw[i]);
	if(s->bandw != NULL)
		free(s->bandw);

	/* Free times */
	sdp_free_times(s);

	/* Time zone */
	if(s->zone != NULL)
		free(s->zone);

	/* Encryption key */
	if(s->key != NULL)
		free(s->key);

	/* Session attributes */
	for(i = 0; i < s->nb_attr; i++)
		if(s->attr[i] != NULL)
			free(s->attr[i]);
	if(s->attr != NULL)
		free(s->attr);

	/* Free medias */
	sdp_free_medias(s);

	/* Free structure */
	free(s);
}

