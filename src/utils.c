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

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <string.h>
#include <dirent.h>
#include <sys/types.h>
#include <sys/stat.h>

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef HAVE_OPENSSL
#include <openssl/md5.h>
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

unsigned char *md5_encode(const unsigned char *buffer, long length)
{
	unsigned char *hash = NULL;

#ifdef HAVE_OPENSSL
	/* Allocate hash */
	hash = malloc(MD5_DIGEST_LENGTH);
	if(hash == NULL)
		return NULL;

	/* Calculate md5 */
	MD5(buffer, length, hash);
#endif

	return hash;
}

char *md5_encode_str(const unsigned char *buffer, long length)
{
	char *hash = NULL;

#ifdef HAVE_OPENSSL
	unsigned char md5[MD5_DIGEST_LENGTH] = "";
	char *p;
	char v;
	int i;

	/* Calculate md5 */
	MD5(buffer, length, md5);

	/* Allocate string */
	hash = malloc((MD5_DIGEST_LENGTH * 2) + 1);
	if(hash == NULL)
		return NULL;

	/* Convert hash */
	for(i = 0, p = hash; i < MD5_DIGEST_LENGTH; i++)
	{
		v = (md5[i] >> 4) & 0x0F;
		*p++ = v > 9 ? v + 0x57 : v + 0x30;
		v = md5[i] & 0x0F;
		*p++ = v > 9 ? v + 0x57 : v + 0x30;
	}
	*p = '\0';
#endif

	return hash;
}

int parse_url(const char *url, int *protocol, char **hostname,
	      unsigned int *port, char **username, char **password,
	      char **resource)
{
	const char *_hostname;
	const char *_port;
	const char *_password;
	const char *_resource;

	if(url == NULL)
		return -1;

	/* Set default values */
	*protocol = URL_HTTP;
	*port = 80;

	/* Get protocol type HTTP(S) */
	if(strncmp(url, "http://", 7) == 0)
		url += 7;
	else if(strncmp(url, "https://", 8) == 0)
	{
		url += 8;
		*port = 443;
		*protocol = URL_HTTPS;
	}

	/* Scheme: http://username:password@hostname:port/resource?data */

	/* Get resource Separate in two url (search first '/') */
	_resource = strchr(url, '/');
	if(_resource != NULL)
		*resource = strdup(_resource + 1);
	else
		_resource = url + strlen(url);

	/* Separate auth and hostname part (search first '@') */
	_hostname = strchr(url, '@');
	if(_hostname != NULL)
	{
		/* Get username and password */
		_password = strchr(url, ':');
		if(_password != NULL && _password < _hostname)
		{
			*password = strndup(_password + 1,
					    _hostname - _password - 1);
		}
		else
			_password = _hostname;

		*username = strndup(url, _password - url);
	}
	else
		_hostname = url - 1;

	/* Get port */
	_port = strchr(_hostname, ':');
	if(_port != NULL)
		*port = strtol(_port + 1, NULL, 10);
	else
		_port = _resource;

	/* Get hostname */
	*hostname = strndup(_hostname + 1, _port - _hostname - 1);

	/* Need at least an hostname */
	if(*hostname == NULL)
		return -1;

	return 0;
}

char *random_string(int size)
{
	static int init = 1;
	char *str;
	int i;

	/* Initialize random number generator */
	if(init) srand(time(NULL) + getpid()), init = 0;

	/* Allocate string */
	str = malloc(size + 1);
	if(str == NULL)
		return NULL;

	/* Generate string */
	for(i = 0; i < size; i++)
	{
		str[i] = base64_table[rand() % 62];
	}

	/* Finish string */
	str[size] = '\0';

	return str;
}

int _alphasort(const struct _dirent **a, const struct _dirent **b)
{
	return strcoll((*a)->name, (*b)->name);
}

int _alphasort_reverse(const struct _dirent **a, const struct _dirent **b)
{
	return strcoll((*b)->name, (*a)->name);
}

int _alphasort_first(const struct _dirent **a, const struct _dirent **b)
{
	if(((*a)->mode & S_IFMT) != ((*b)->mode & S_IFMT))
		return ((*a)->mode & S_IFDIR) ? 0 : 1;

	return strcoll((*a)->name, (*b)->name);
}

int _alphasort_last(const struct _dirent **a, const struct _dirent **b)
{
	if(((*a)->mode & S_IFMT) != ((*b)->mode & S_IFMT))
		return ((*b)->mode & S_IFDIR) ? 0 : 1;

	return strcoll((*b)->name, (*a)->name);
}

int _file_only(const struct dirent *d, const struct stat *s)
{
	return s->st_mode & S_IFREG ? 1 : 0;
}

int _dir_only(const struct dirent *d, const struct stat *s)
{
	return s->st_mode & S_IFDIR ? 1 : 0;
}

int _scandir(const char *path, struct _dirent ***list,
	     int (*selector)(const struct dirent *, const struct stat *),
	     int (*compar)(const struct _dirent **, const struct _dirent **))
{
	struct _dirent **_list = NULL;
	struct _dirent **new;
	struct dirent *dir;
	struct stat st;
	size_t count = 0;
	size_t size = 0;
	size_t nlen = 0;
	char *file;
	DIR *dp;

	/* Open directory */
	dp = opendir(path);
	if (dp == NULL)
		return -1;

	/* List all files */
	while((dir = readdir(dp)) != NULL)
	{
		/* Skip . and .. */
		if(dir->d_name[0] == '.' && (dir->d_name[1] == '\0' ||
		   (dir->d_name[1] == '.' && dir->d_name[2] == '\0')))
			continue;

		/* Generate complete file path */
		asprintf(&file, "%s/%s", path, dir->d_name);
		if(file == NULL)
			break;

		/* Get stat of file */
		stat(file, &st);
		free(file);

		/* Check if entry will be added */
		if(selector != NULL && selector(dir, &st) == 0)
			continue;

		/* Reallocate list */
		if(count == size)
		{
			if(size == 0)
				size = 10;
			else
				size *= 2;
			new = realloc(_list, size * sizeof(struct _dirent *));
			if(new == NULL)
				break;
			_list = new;
		}

		/* Allocate new entry */
		nlen = _D_ALLOC_NAMLEN(dir);
		_list[count] = malloc(sizeof(struct _dirent) - 256 + nlen);
		if(_list[count] == NULL)
			break;

		/* Copy data */
		_list[count]->inode = dir->d_ino;
		_list[count]->mode = st.st_mode;
		_list[count]->size = st.st_size;
		_list[count]->atime = st.st_atime;
		_list[count]->mtime = st.st_mtime;
		_list[count]->ctime = st.st_ctime;
		memcpy(_list[count]->name, dir->d_name, nlen);
		count++;
	}

	/* Close directory */
	closedir(dp);

	/* Sort list */
	qsort(_list, count, sizeof(struct _dirent *), (__compar_fn_t) compar);

	/* Return values */
	*list = _list;
	return count;
}

