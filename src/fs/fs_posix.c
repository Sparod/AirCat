/*
 * fs_posix.c - A POSIX implementation for FS
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
#include <unistd.h>
#include <dirent.h>
#include <fcntl.h>
#include <mntent.h>

#include "fs_posix.h"

void fs_posix_init(void)
{
	return;
}

void fs_posix_free(void)
{
	return;
}

static int fs_posix_open(struct fs_file *f, const char *url, int flags,
			 mode_t mode)
{
	/* Open file */
	f->fd = open(url, flags, mode);
	if(f->fd < 0)
		return -1;

	return 0;
}

static int fs_posix_creat(struct fs_file *f, const char *url, mode_t mode)
{
	/* Create file */
	f->fd = creat(url, mode);
	if(f->fd < 0)
		return -1;

	return 0;
}

static ssize_t fs_posix_read_to(struct fs_file *f, void *buf, size_t count,
				long timeout)
{
	struct timeval tv;
	ssize_t len = 0;
	fd_set readfs;

	/* Use timeout */
	if(timeout >= 0)
	{
		/* Prepare a select */
		FD_ZERO(&readfs);
		FD_SET(f->fd, &readfs);

		/* Set timeout */
		tv.tv_sec = 0;
		tv.tv_usec = timeout * 1000;

		if(select(f->fd + 1, &readfs, NULL, NULL, &tv) < 0)
			return -1;
	}

	/* Read from file */
	if(timeout == -1 || FD_ISSET(f->fd, &readfs))
	{
		len = read(f->fd, buf, count);

		/* End of stream */
		if(len <= 0)
			return -1;
	}

	return len;
}

static ssize_t fs_posix_read(struct fs_file *f, void *buf, size_t count)
{
	return fs_posix_read_to(f, buf, count, -1);
}

static ssize_t fs_posix_write_to(struct fs_file *f, const void *buf,
				 size_t count, long timeout)
{
	struct timeval tv;
	ssize_t len = 0;
	fd_set writefs;

	/* Use timeout */
	if(timeout >= 0)
	{
		/* Prepare a select */
		FD_ZERO(&writefs);
		FD_SET(f->fd, &writefs);

		/* Set timeout */
		tv.tv_sec = 0;
		tv.tv_usec = timeout * 1000;

		if(select(f->fd + 1, NULL, &writefs, NULL, &tv) < 0)
			return -1;
	}

	/* Read from file */
	if(timeout == -1 || FD_ISSET(f->fd, &writefs))
	{
		len = write(f->fd, buf, count);

		/* End of stream */
		if(len <= 0)
			return -1;
	}

	return len;
}

static ssize_t fs_posix_write(struct fs_file *f, const void *buf, size_t count)
{
	return fs_posix_write_to(f, buf, count, -1);
}

static off_t fs_posix_lseek(struct fs_file *f, off_t offset, int whence)
{
	return lseek(f->fd, offset, whence);
}

static int fs_posix_ftruncate(struct fs_file *f, off_t length)
{
	return ftruncate(f->fd, length);
}

static void fs_posix_close(struct fs_file *f)
{
	if(f->fd < 0)
		return;

	/* Close file */
	close(f->fd);
}

static int fs_posix_opendir(struct fs_dir *d, const char *url)
{
	/* Open directory */
	d->data = opendir(url);
	if(d->data == NULL)
		return -1;
	d->fd = dirfd(d->data);

	return 0;
}

static int fs_posix_mount(struct fs_dir *d)
{
	/* Open mount table */
	d->data = setmntent("/etc/mtab", "r");
	if(d->data == NULL)
		return -1;
	d->fd = -1;

	return 0;
}

static struct fs_dirent *fs_posix_readdir(struct fs_dir *d)
{
	struct dirent *dir;
	struct mntent mnt;

	if(d->data == NULL)
		return NULL;

	/* Read mount table entry */
	if(d->fd == -1)
	{
		/* Get next entry */
		while(getmntent_r(d->data, &mnt, d->c_dirent.name, 256) != NULL)
		{
			/* Check file system */
			if(strcmp(mnt.mnt_type, "rootfs") == 0 ||
			   strcmp(mnt.mnt_type, "sysfs") == 0 ||
			   strcmp(mnt.mnt_type, "proc") == 0 ||
			   strcmp(mnt.mnt_type, "devtmpfs") == 0 ||
			   strcmp(mnt.mnt_type, "devpts") == 0 ||
			   strcmp(mnt.mnt_type, "tmpfs") == 0 ||
			   strcmp(mnt.mnt_type, "rpc_pipefs") == 0 ||
			   strcmp(mnt.mnt_type, "nfsd") == 0 ||
			   strcmp(mnt.mnt_type, "binfmt_misc") == 0)
				continue;

			/* Fill directory entry */
			d->c_dirent.inode = 0;
			d->c_dirent.offset = 0;
			d->c_dirent.type = FS_DSK;
			d->c_dirent.comment_len = 0;
			d->c_dirent.comment = NULL;
			d->c_dirent.name_len = strlen(mnt.mnt_dir);
			if(d->c_dirent.name_len > 255)
				d->c_dirent.name_len = 255;
			strncpy(d->c_dirent.name, mnt.mnt_dir, 256);

			return &d->c_dirent;
		}

		return NULL;
	}

	/* Read directory entry */
	dir = readdir(d->data);
	if(dir == NULL)
		return NULL;

	/* Fill dirent */
	d->c_dirent.inode = dir->d_ino;
	d->c_dirent.offset = dir->d_off;
	d->c_dirent.comment_len = 0;
	d->c_dirent.comment = NULL;
	d->c_dirent.name_len = dir->d_reclen;
	strcpy(d->c_dirent.name, dir->d_name);

	/* Get type */
	switch(dir->d_type)
	{
		case DT_REG:
			d->c_dirent.type = FS_REG;
			break;
		case DT_DIR:
			d->c_dirent.type = FS_DIR;
			break;
		case DT_LNK:
			d->c_dirent.type = FS_LNK;
			break;
		default:
			d->c_dirent.type = FS_UNKNOWN;
	}

	/* Generate path */
	strncpy(&d->url[d->url_len], dir->d_name, dir->d_reclen);

	/* Stat directory */
	stat(d->url, &d->c_dirent.stat);

	return &d->c_dirent;
}

static off_t fs_posix_telldir(struct fs_dir *d)
{
	if(d->data == NULL || d->fd == -1)
		return -1;

	return telldir(d->data);
}

static void fs_posix_closedir(struct fs_dir *d)
{
	if(d->data == NULL)
		return;

	/* Close mount table */
	if(d->fd == -1)
	{
		endmntent(d->data);
		return;
	}

	/* Close directory */
	closedir(d->data);
}

static int fs_posix_fstat(struct fs_file *f, struct stat *buf)
{
	return fstat(f->fd, buf);
}

static int fs_posix_fstatvfs(struct fs_file *f, struct statvfs *buf)
{
	return fstatvfs(f->fd, buf);
}

struct fs_handle fs_posix = {
	.open = fs_posix_open,
	.creat = fs_posix_creat,
	.read = fs_posix_read,
	.read_to = fs_posix_read_to,
	.write = fs_posix_write,
	.write_to = fs_posix_write_to,
	.lseek = fs_posix_lseek,
	.ftruncate = fs_posix_ftruncate,
	.close = fs_posix_close,
	.mkdir = mkdir,
	.unlink = unlink,
	.rmdir = rmdir,
	.rename = rename,
	.chmod = chmod,
	.opendir = fs_posix_opendir,
	.mount = fs_posix_mount,
	.readdir = fs_posix_readdir,
	.telldir = fs_posix_telldir,
	.closedir = fs_posix_closedir,
	.stat = stat,
	.fstat = fs_posix_fstat,
	.statvfs = statvfs,
	.fstatvfs = fs_posix_fstatvfs,
};

