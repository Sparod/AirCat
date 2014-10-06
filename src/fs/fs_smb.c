/*
 * fs_smb.c - A SMB (Windows file sharing) implementation for FS
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
#include <errno.h>
#include <pthread.h>

static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef HAVE_LIBSMBCLIENT

#include <libsmbclient.h>

#include "fs_smb.h"

#define FS_SMB_TIMEOUT 10

static void fs_smb_get_auth(const char *srv, const char *shr,
			    char *wg, int wglen, char *un, int unlen,
			    char *pw, int pwlen)
{
	/* Authentication is done directly in URL */
	return;
}

void fs_smb_init(void)
{
	/* Initialize smbclient */
	smbc_init(fs_smb_get_auth, 0);

	return;
}

void fs_smb_free(void)
{
	return;
}

static int fs_smb_open(struct fs_file *f, const char *url, int flags,
		       mode_t mode)
{
	/* Lock libsmbclient access */
	pthread_mutex_lock(&mutex);

	/* Open file */
	f->fd = smbc_open(url, flags, mode);

	/* Unlock libsmbclient access */
	pthread_mutex_unlock(&mutex);

	/* Bad file descriptor */
	if(f->fd < SMBC_BASE_FD)
		return -1;

	return 0;
}

static int fs_smb_creat(struct fs_file *f, const char *url, mode_t mode)
{
	/* Lock libsmbclient access */
	pthread_mutex_lock(&mutex);

	/* Create file */
	f->fd = smbc_creat(url, mode);

	/* Unlock libsmbclient access */
	pthread_mutex_unlock(&mutex);

	/* Bad file descriptor */
	if(f->fd < SMBC_BASE_FD)
		return -1;

	return 0;
}

static ssize_t fs_smb_read(struct fs_file *f, void *buf, size_t count)
{
	ssize_t len;

	/* Lock libsmbclient access */
	pthread_mutex_lock(&mutex);

	/* Wait until data or error */
	while((len = smbc_read(f->fd, buf, count)) < 0)
	{
		/* Skip timeout */
		if(errno != EAGAIN)
			break;
	}

	/* Unlock libsmbclient access */
	pthread_mutex_unlock(&mutex);

	return len;
}

static ssize_t fs_smb_read_to(struct fs_file *f, void *buf, size_t count,
				long timeout)
{
	ssize_t len;

	/* No timeout */
	if(timeout == -1)
		return fs_smb_read(f, buf, count);

	/* Lock libsmbclient access */
	pthread_mutex_lock(&mutex);

	/* Wait timeout */
	do {
		/* Attempt to read */
		len = smbc_read(f->fd, buf, count);
		if(len < 0)
		{
			if(errno == EAGAIN)
				len = 0;
			break;
		}
		timeout -= FS_SMB_TIMEOUT;
	} while(timeout > 0);

	/* Unlock libsmbclient access */
	pthread_mutex_unlock(&mutex);

	return len;
}

static ssize_t fs_smb_write(struct fs_file *f, const void *buf, size_t count)
{
	ssize_t len;

	/* Lock libsmbclient access */
	pthread_mutex_lock(&mutex);

	/* Wait until data or error */
	while((len = smbc_write(f->fd, buf, count)) < 0)
	{
		/* Skip timeout */
		if(errno != EAGAIN)
			break;
	}

	/* Unlock libsmbclient access */
	pthread_mutex_unlock(&mutex);

	return len;
}

static ssize_t fs_smb_write_to(struct fs_file *f, const void *buf,
				 size_t count, long timeout)
{
	ssize_t len;

	/* No timeout */
	if(timeout == -1)
		return fs_smb_write(f, buf, count);

	/* Lock libsmbclient access */
	pthread_mutex_lock(&mutex);

	/* Wait timeout */
	do {
		/* Attempt to write */
		len = smbc_write(f->fd, buf, count);
		if(len < 0)
		{
			if(errno == EAGAIN)
				len = 0;
			break;
		}
		timeout -= FS_SMB_TIMEOUT;
	} while(timeout > 0);

	/* Unlock libsmbclient access */
	pthread_mutex_unlock(&mutex);

	return len;
}

static off_t fs_smb_lseek(struct fs_file *f, off_t offset, int whence)
{
	off_t ret;

	/* Lock libsmbclient access */
	pthread_mutex_lock(&mutex);

	/* Lseek */
	ret = smbc_lseek(f->fd, offset, whence);

	/* Unlock libsmbclient access */
	pthread_mutex_unlock(&mutex);

	return ret;
}

static int fs_smb_ftruncate(struct fs_file *f, off_t length)
{
	int ret;

	/* Lock libsmbclient access */
	pthread_mutex_lock(&mutex);

	/* Ftruncate */
	ret = smbc_ftruncate(f->fd, length);

	/* Unlock libsmbclient access */
	pthread_mutex_unlock(&mutex);

	return ret;
}

static void fs_smb_close(struct fs_file *f)
{
	if(f->fd < SMBC_BASE_FD)
		return;

	/* Lock libsmbclient access */
	pthread_mutex_lock(&mutex);

	/* Close file */
	smbc_close(f->fd);

	/* Unlock libsmbclient access */
	pthread_mutex_unlock(&mutex);
}

static int fs_smb_mkdir(const char *url, mode_t mode)
{
	int ret;

	/* Lock libsmbclient access */
	pthread_mutex_lock(&mutex);

	/* Mkdir */
	ret = smbc_mkdir(url, mode);

	/* Unlock libsmbclient access */
	pthread_mutex_unlock(&mutex);

	return ret;
}

static int fs_smb_unlink(const char *url)
{
	int ret;

	/* Lock libsmbclient access */
	pthread_mutex_lock(&mutex);

	/* Unlink */
	ret = smbc_unlink(url);

	/* Unlock libsmbclient access */
	pthread_mutex_unlock(&mutex);

	return ret;
}

static int fs_smb_rmdir(const char *url)
{
	int ret;

	/* Lock libsmbclient access */
	pthread_mutex_lock(&mutex);

	/* Rmdir */
	ret = smbc_rmdir(url);

	/* Unlock libsmbclient access */
	pthread_mutex_unlock(&mutex);

	return ret;
}

static int fs_smb_rename(const char *oldurl, const char *newurl)
{
	int ret;

	/* Lock libsmbclient access */
	pthread_mutex_lock(&mutex);

	/* Rename */
	ret = smbc_rename(oldurl, newurl);

	/* Unlock libsmbclient access */
	pthread_mutex_unlock(&mutex);

	return ret;
}

static int fs_smb_chmod(const char *url, mode_t mode)
{
	int ret;

	/* Lock libsmbclient access */
	pthread_mutex_lock(&mutex);

	/* Chmod */
	ret = smbc_chmod(url, mode);

	/* Unlock libsmbclient access */
	pthread_mutex_unlock(&mutex);

	return ret;
}

static int fs_smb_opendir(struct fs_dir *d, const char *url)
{
	/* Lock libsmbclient access */
	pthread_mutex_lock(&mutex);

	/* Open directory */
	d->fd = smbc_opendir(url);

	/* Unlock libsmbclient access */
	pthread_mutex_unlock(&mutex);

	/* Bad file descriptor */
	if(d->fd < SMBC_BASE_FD)
		return -1;

	return 0;
}

static int fs_smb_mount(struct fs_dir *d)
{
	return fs_smb_opendir(d, "smb://");
}

static struct fs_dirent *fs_smb_readdir(struct fs_dir *d)
{
	struct smbc_dirent *dir;

	if(d->fd < SMBC_BASE_FD)
		return NULL;

	/* Lock libsmbclient access */
	pthread_mutex_lock(&mutex);

	/* Read directory entry */
	dir = smbc_readdir(d->fd);
	if(dir == NULL)
	{
		/* Unlock libsmbclient access */
		pthread_mutex_unlock(&mutex);
		return NULL;
	}

	/* Fill dirent */
	d->c_dirent.inode = 0;
	d->c_dirent.offset = 0;
	d->c_dirent.comment_len = dir->commentlen;
	d->c_dirent.comment = dir->comment;
	d->c_dirent.name_len = dir->namelen;
	strncpy(d->c_dirent.name, dir->name, 256);

	/* Get type */
	switch(dir->smbc_type)
	{
		case SMBC_FILE:
			d->c_dirent.type = FS_REG;
			break;
		case SMBC_LINK:
			d->c_dirent.type = FS_LNK;
			break;
		case SMBC_WORKGROUP:
			d->c_dirent.type = FS_NET;
			break;
		case SMBC_SERVER:
			d->c_dirent.type = FS_SRV;
			break;
		default:
			d->c_dirent.type = FS_DIR;
	}

	/* Generate path */
	strncpy(&d->url[d->url_len], dir->name, dir->namelen);
	d->url[d->url_len+dir->namelen] = '\0';

	/* Stat directory */
	smbc_stat(d->url, &d->c_dirent.stat);

	/* Unlock libsmbclient access */
	pthread_mutex_unlock(&mutex);

	return &d->c_dirent;
}

static off_t fs_smb_telldir(struct fs_dir *d)
{
	off_t ret;

	if(d->fd < SMBC_BASE_FD)
		return -1;

	/* Lock libsmbclient access */
	pthread_mutex_lock(&mutex);

	/* Telldir */
	ret = smbc_telldir(d->fd);

	/* Unlock libsmbclient access */
	pthread_mutex_unlock(&mutex);

	return ret;
}

static void fs_smb_closedir(struct fs_dir *d)
{
	if(d->fd < SMBC_BASE_FD)
		return;

	/* Close directory */
	smbc_closedir(d->fd);
}

static int fs_smb_stat(const char *url, struct stat *buf)
{
	int ret;

	/* Lock libsmbclient access */
	pthread_mutex_lock(&mutex);

	/* Stat file */
	ret = smbc_stat(url, buf);

	/* Unlock libsmbclient access */
	pthread_mutex_unlock(&mutex);

	return ret;
}

static int fs_smb_fstat(struct fs_file *f, struct stat *buf)
{
	int ret;

	/* Lock libsmbclient access */
	pthread_mutex_lock(&mutex);

	/* Fstat file */
	ret = smbc_fstat(f->fd, buf);

	/* Unlock libsmbclient access */
	pthread_mutex_unlock(&mutex);

	return ret;
}

static int fs_smb_statvfs(const char *url, struct statvfs *buf)
{
	int ret;

	/* Lock libsmbclient access */
	pthread_mutex_lock(&mutex);

	/* Statvfs file */
	ret = smbc_statvfs((char*)url, buf);

	/* Unlock libsmbclient access */
	pthread_mutex_unlock(&mutex);

	return ret;
}

static int fs_smb_fstatvfs(struct fs_file *f, struct statvfs *buf)
{
	int ret;

	/* Lock libsmbclient access */
	pthread_mutex_lock(&mutex);

	/* Fstatvfs file */
	ret = smbc_fstatvfs(f->fd, buf);

	/* Unlock libsmbclient access */
	pthread_mutex_unlock(&mutex);

	return ret;
}

struct fs_handle fs_smb = {
	.open = fs_smb_open,
	.creat = fs_smb_creat,
	.read = fs_smb_read,
	.read_to = fs_smb_read_to,
	.write = fs_smb_write,
	.write_to = fs_smb_write_to,
	.lseek = fs_smb_lseek,
	.ftruncate = fs_smb_ftruncate,
	.close = fs_smb_close,
	.mkdir = fs_smb_mkdir,
	.unlink = fs_smb_unlink,
	.rmdir = fs_smb_rmdir,
	.rename = fs_smb_rename,
	.chmod = fs_smb_chmod,
	.opendir = fs_smb_opendir,
	.mount = fs_smb_mount,
	.readdir = fs_smb_readdir,
	.telldir = fs_smb_telldir,
	.closedir = fs_smb_closedir,
	.stat = fs_smb_stat,
	.fstat = fs_smb_fstat,
	.statvfs = fs_smb_statvfs,
	.fstatvfs = fs_smb_fstatvfs,
};

#endif

