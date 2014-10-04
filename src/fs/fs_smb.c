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
	/* Open file */
	f->fd = smbc_open(url, flags, mode);
	if(f->fd < SMBC_BASE_FD)
		return -1;

	return 0;
}

static int fs_smb_creat(struct fs_file *f, const char *url, mode_t mode)
{
	/* Create file */
	f->fd = smbc_creat(url, mode);
	if(f->fd < SMBC_BASE_FD)
		return -1;

	return 0;
}

static ssize_t fs_smb_read(struct fs_file *f, void *buf, size_t count)
{
	ssize_t len;

	/* Wait until data or error */
	while((len = smbc_read(f->fd, buf, count)) < 0)
	{
		/* Skip timeout */
		if(errno != EAGAIN)
			break;
	}

	return len;
}

static ssize_t fs_smb_read_to(struct fs_file *f, void *buf, size_t count,
				long timeout)
{
	ssize_t len;

	/* No timeout */
	if(timeout == -1)
		return fs_smb_read(f, buf, count);

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

	return len;
}

static ssize_t fs_smb_write(struct fs_file *f, const void *buf, size_t count)
{
	ssize_t len;

	/* Wait until data or error */
	while((len = smbc_write(f->fd, buf, count)) < 0)
	{
		/* Skip timeout */
		if(errno != EAGAIN)
			break;
	}

	return len;
}

static ssize_t fs_smb_write_to(struct fs_file *f, const void *buf,
				 size_t count, long timeout)
{
	ssize_t len;

	/* No timeout */
	if(timeout == -1)
		return fs_smb_write(f, buf, count);

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

	return len;
}

static off_t fs_smb_lseek(struct fs_file *f, off_t offset, int whence)
{
	return smbc_lseek(f->fd, offset, whence);
}

static int fs_smb_ftruncate(struct fs_file *f, off_t length)
{
	return smbc_ftruncate(f->fd, length);
}

static void fs_smb_close(struct fs_file *f)
{
	if(f->fd < SMBC_BASE_FD)
		return;

	/* Close file */
	smbc_close(f->fd);
}

static int fs_smb_opendir(struct fs_dir *d, const char *url)
{
	/* Open directory */
	d->fd = smbc_opendir(url);
	if(d->fd < SMBC_BASE_FD)
		return -1;

	return 0;
}

static struct fs_dirent *fs_smb_readdir(struct fs_dir *d)
{
	struct smbc_dirent *dir;

	if(d->fd < SMBC_BASE_FD)
		return NULL;

	/* Read directory entry */
	dir = smbc_readdir(d->fd);
	if(dir == NULL)
		return NULL;

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

	return &d->c_dirent;
}

static off_t fs_smb_telldir(struct fs_dir *d)
{
	if(d->fd < SMBC_BASE_FD)
		return -1;

	return smbc_telldir(d->fd);
}

static void fs_smb_closedir(struct fs_dir *d)
{
	if(d->fd < SMBC_BASE_FD)
		return;

	/* Close directory */
	smbc_closedir(d->fd);
}

static int fs_smb_fstat(struct fs_file *f, struct stat *buf)
{
	return smbc_fstat(f->fd, buf);
}

static int fs_smb_fstatvfs(struct fs_file *f, struct statvfs *buf)
{
	return smbc_fstatvfs(f->fd, buf);
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
	.mkdir = smbc_mkdir,
	.unlink = smbc_unlink,
	.rmdir = smbc_rmdir,
	.rename = smbc_rename,
	.chmod = smbc_chmod,
	.opendir = fs_smb_opendir,
	.readdir = fs_smb_readdir,
	.telldir = fs_smb_telldir,
	.closedir = fs_smb_closedir,
	.stat = smbc_stat,
	.fstat = fs_smb_fstat,
	.statvfs = (void *) smbc_statvfs,
	.fstatvfs = fs_smb_fstatvfs,
};

#endif

