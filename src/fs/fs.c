/*
 * fs.c - A Filesystem API to handle many protocol (HTTP, SMB, NFS, ...)
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

#include "fs_posix.h"
#include "fs_http.h"
#include "fs_smb.h"
#include "fs.h"

void fs_init(void)
{
	/* Initialize all file system */
	fs_posix_init();
	fs_http_init();
#ifdef HAVE_LIBSMBCLIENT
	fs_smb_init();
#endif
}

void fs_free(void)
{
	/* Free all file system */
#ifdef HAVE_LIBSMBCLIENT
	fs_smb_free();
#endif
	fs_http_free();
	fs_posix_free();
}

static struct fs_handle *fs_find_filesystem(const char *url)
{
	struct fs_handle *h = NULL;

	/* Process URL */
	if(strncmp(url, "http://", 7) == 0 || strncmp(url, "https://", 8) == 0)
	{
		/* HTTP(S) fs */
		h = &fs_http;
	}
#ifdef HAVE_LIBSMBCLIENT
	else if(strncmp(url, "smb://", 6) == 0)
	{
		/* SMB fs (Windows file sharing) */
		h = &fs_smb;
	}
#endif
	else if(strstr(url, "://") == NULL)
	{
		/* POSIX fs */
		h = &fs_posix;
	}

	return h;
}

struct fs_file *fs_open(const char *url, int flags, mode_t mode)
{
	struct fs_handle *h;
	struct fs_file *f;

	/* Get file system from URL */
	h = fs_find_filesystem(url);
	if(h == NULL)
		return NULL;

	/* Allocate file */
	f = malloc(sizeof(struct fs_file));
	if(f == NULL)
		return NULL;
	f->handle = h;

	/* Open file */
	if(h->open(f, url, flags, mode) != 0)
	{
		/* Free file */
		free(f);
		return NULL;
	}

	return f;
}

struct fs_file *fs_creat(const char *url, mode_t mode)
{
	struct fs_handle *h;
	struct fs_file *f;

	/* Get file system from URL */
	h = fs_find_filesystem(url);
	if(h == NULL)
		return NULL;

	/* Allocate file */
	f = malloc(sizeof(struct fs_file));
	if(f == NULL)
		return NULL;
	f->handle = h;

	/* Create file */
	if(h->creat(f, url, mode) != 0)
	{
		/* Free file */
		free(f);
		return NULL;
	}

	return f;
}

ssize_t fs_read(struct fs_file *f, void *buf, size_t count)
{
	if(f == NULL)
		return -1;

	return f->handle->read(f, buf, count);
}

ssize_t fs_read_timeout(struct fs_file *f, void *buf, size_t count,
			long timeout)
{
	if(f == NULL)
		return -1;

	return f->handle->read_to(f, buf, count, timeout);
}

ssize_t fs_write(struct fs_file *f, const void *buf, size_t count)
{
	if(f == NULL)
		return -1;

	return f->handle->write(f, buf, count);
}

ssize_t fs_write_timeout(struct fs_file *f, const void *buf, size_t count,
			 long timeout)
{
	if(f == NULL)
		return -1;

	return f->handle->write_to(f, buf, count, timeout);
}

off_t fs_lseek(struct fs_file *f, off_t offset, int whence)
{
	if(f == NULL)
		return -1;

	return f->handle->lseek(f, offset, whence);
}

int fs_ftruncate(struct fs_file *f, off_t length)
{
	if(f == NULL)
		return -1;

	return f->handle->ftruncate(f, length);
}

void fs_close(struct fs_file *f)
{
	if(f == NULL)
		return;

	/* Close file */
	f->handle->close(f);

	/* Free handle */
	free(f);
}

int fs_mkdir(const char *url, mode_t mode)
{
	struct fs_handle *h;

	/* Find file system from URL */
	h = fs_find_filesystem(url);
	if(h == NULL)
		return -1;

	/* Make directory */
	return h->mkdir(url, mode);
}

int fs_unlink(const char *url)
{
	struct fs_handle *h;

	/* Find file system from URL */
	h = fs_find_filesystem(url);
	if(h == NULL)
		return -1;

	/* Unlink */
	return h->unlink(url);
}

int fs_rmdir(const char *url)
{
	struct fs_handle *h;

	/* Find file system from URL */
	h = fs_find_filesystem(url);
	if(h == NULL)
		return -1;

	/* Remove directory */
	return h->rmdir(url);
}

int fs_rename(const char *oldurl, const char *newurl)
{
	struct fs_handle *h;

	/* Find file system from URL */
	h = fs_find_filesystem(oldurl);
	if(h == NULL)
		return -1;

	/* Rename */
	return h->rename(oldurl, newurl);
}

int fs_chmod(const char *url, mode_t mode)
{
	struct fs_handle *h;

	/* Get file system from URL */
	h = fs_find_filesystem(url);
	if(h == NULL)
		return -1;

	/* Change mode */
	return h->chmod(url, mode);
}

struct fs_dir *fs_opendir(const char *url)
{
	struct fs_handle *h;
	struct fs_dir *d;
	int len;

	/* Get file system from URL */
	h = fs_find_filesystem(url);
	if(h == NULL)
		return NULL;

	/* Allocate directory */
	d = malloc(sizeof(struct fs_dir));
	if(d == NULL)
		return NULL;
	d->handle = h;

	/* Open directory */
	if(h->opendir(d, url) != 0)
	{
		/* Free directory */
		free(d);
		return NULL;
	}

	/* Copy URL with allocated space for name */
	len = strlen(url);
	d->url = malloc(len + 256 + 2);
	if(d->url != NULL)
	{
		d->url_len = len + 1;
		memcpy(d->url, url, len);
		d->url[len] = '/';
		d->url[len+1] = '\0';
	}

	return d;
}

struct fs_dir *fs_mount(const char *url)
{
	struct fs_handle *h;
	struct fs_dir *d;
	int len;

	/* Get file system from URL */
	h = fs_find_filesystem(url);
	if(h == NULL)
		return NULL;

	/* Allocate directory */
	d = malloc(sizeof(struct fs_dir));
	if(d == NULL)
		return NULL;
	d->handle = h;

	/* List mount/network */
	if(h->mount(d) != 0)
	{
		/* Free directory */
		free(d);
		return NULL;
	}

	/* Copy URL with allocated space for name */
	len = strlen(url);
	d->url = malloc(len + 256 + 2);
	if(d->url != NULL)
	{
		d->url_len = len + 1;
		memcpy(d->url, url, len);
		d->url[len] = '/';
		d->url[len+1] = '\0';
	}

	return d;
}

struct fs_dirent *fs_readdir(struct fs_dir *d)
{
	if(d == NULL)
		return NULL;

	return d->handle->readdir(d);
}

off_t fs_telldir(struct fs_dir *d)
{
	if(d == NULL)
		return -1;

	return d->handle->telldir(d);
}

void fs_closedir(struct fs_dir *d)
{
	if(d == NULL)
		return;

	/* Close directory */
	d->handle->closedir(d);

	/* Free URL */
	if(d->url)
		free(d->url);

	/* Free handle */
	free(d);
}

int fs_stat(const char *url, struct stat *buf)
{
	struct fs_handle *h;

	/* Find file system from URL */
	h = fs_find_filesystem(url);
	if(h == NULL)
		return -1;

	/* Stat */
	return h->stat(url, buf);
}

int fs_fstat(struct fs_file *f, struct stat *buf)
{
	if(f == NULL)
		return -1;

	return f->handle->fstat(f, buf);
}

int fs_statvfs(const char *url, struct statvfs *buf)
{
	struct fs_handle *h;

	/* Find file system from URL */
	h = fs_find_filesystem(url);
	if(h == NULL)
		return -1;

	/* Statvfs */
	return h->statvfs(url, buf);
}

int fs_fstatvfs(struct fs_file *f, struct statvfs *buf)
{
	if(f == NULL)
		return -1;

	return f->handle->fstatvfs(f, buf);
}

int fs_alphasort(const struct fs_dirent **a, const struct fs_dirent **b)
{
	return strcoll((*a)->name, (*b)->name);
}

int fs_alphasort_reverse(const struct fs_dirent **a, const struct fs_dirent **b)
{
	return strcoll((*b)->name, (*a)->name);
}

int fs_alphasort_first(const struct fs_dirent **a, const struct fs_dirent **b)
{
	if(((*a)->stat.st_mode & S_IFMT) != ((*b)->stat.st_mode & S_IFMT))
		return ((*a)->stat.st_mode & S_IFDIR) ? 0 : 1;

	return strcoll((*a)->name, (*b)->name);
}

int fs_alphasort_last(const struct fs_dirent **a, const struct fs_dirent **b)
{
	if(((*a)->stat.st_mode & S_IFMT) != ((*b)->stat.st_mode & S_IFMT))
		return ((*b)->stat.st_mode & S_IFDIR) ? 0 : 1;

	return strcoll((*b)->name, (*a)->name);
}

int fs_file_only(const struct fs_dirent *d)
{
	return d->stat.st_mode & S_IFREG ? 1 : 0;
}

int fs_dir_only(const struct fs_dirent *d)
{
	return d->stat.st_mode & S_IFDIR ? 1 : 0;
}

int fs_scandir(const char *path, struct fs_dirent ***list,
	       int (*selector)(const struct fs_dirent *),
	       int (*compar)(const struct fs_dirent **,
			     const struct fs_dirent **))
{
	struct fs_dirent **_list = NULL;
	struct fs_dirent **new;
	struct fs_dirent *d;
	struct fs_dir *dir;
	size_t count = 0;
	size_t size = 0;

	/* Open directory */
	dir = fs_opendir(path);
	if (dir == NULL)
		return -1;

	/* List all files */
	while((d = fs_readdir(dir)) != NULL)
	{
		/* Skip . and .. */
		if(d->name[0] == '.' && (d->name[1] == '\0' ||
		   (d->name[1] == '.' && d->name[2] == '\0')))
			continue;

		/* Check if entry will be added */
		if(selector != NULL && selector(d) == 0)
			continue;

		/* Reallocate list */
		if(count == size)
		{
			if(size == 0)
				size = 10;
			else
				size *= 2;
			new = realloc(_list, size * sizeof(struct fs_dirent *));
			if(new == NULL)
				break;
			_list = new;
		}

		/* Allocate new entry */
		_list[count] = malloc(sizeof(struct fs_dirent));
		if(_list[count] == NULL)
			break;

		/* Copy data */
		memcpy(_list[count], d, sizeof(struct fs_dirent));
		count++;
	}

	/* Close directory */
	fs_closedir(dir);

	/* Sort list */
	qsort(_list, count, sizeof(struct fs_dirent *), (__compar_fn_t) compar);

	/* Return values */
	*list = _list;
	return count;
}

