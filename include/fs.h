/*
 * fs.h - A Filesystem API to handle many protocol (HTTP, SMB, NFS, ...)
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
#ifndef _FS_H
#define _FS_H

#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/statvfs.h>

enum fs_type {
	FS_UNKNOWN,
	FS_REG,
	FS_DIR,
	FS_LNK,
	FS_NET,
	FS_SRV,
	FS_DSK,
};

struct fs_dirent {
	/* Basic values */
	ino_t inode;
	off_t offset;
	enum fs_type type;
	/* Custom values */
	unsigned int comment_len;
	char *comment;
	/* Stat on entry */
	struct stat stat;
	/* Name */
	unsigned int name_len;
	char name[256];
};

struct fs_file {
	/* Data */
	int fd;
	void *data;
	/* Filesystem handle */
	struct fs_handle *handle;
};

struct fs_dir {
	/* Data */
	int fd;
	void *data;
	/* URL with allocated space for name */
	char *url;
	size_t url_len;
	/* Current dirent */
	struct fs_dirent c_dirent;
	/* Filesystem handle */
	struct fs_handle *handle;
};

struct fs_handle {
	/* File I/O */
	int (*open)(struct fs_file *, const char *, int, mode_t);
	int (*creat)(struct fs_file *, const char *, mode_t);
	ssize_t (*read)(struct fs_file *, void *, size_t);
	ssize_t (*read_to)(struct fs_file *, void *, size_t, long);
	ssize_t (*write)(struct fs_file *, const void *, size_t);
	ssize_t (*write_to)(struct fs_file *, const void *, size_t, long);
	off_t (*lseek)(struct fs_file *, off_t, int);
	int (*ftruncate)(struct fs_file *, off_t);
	void (*close)(struct fs_file *);
	/* Filesystem I/O */
	int (*mkdir)(const char *, mode_t);
	int (*unlink)(const char *);
	int (*rmdir)(const char *);
	int (*rename)(const char *, const char *);
	int (*chmod)(const char *, mode_t);
	/* Directory I/O */
	int (*opendir)(struct fs_dir *, const char *);
	int (*mount)(struct fs_dir *);
	struct fs_dirent *(*readdir)(struct fs_dir *);
	void (*closedir)(struct fs_dir *);
	off_t (*telldir)(struct fs_dir *);
	/* Attributes */
	int (*stat)(const char *, struct stat *);
	int (*fstat)(struct fs_file *, struct stat *);
	int (*statvfs)(const char *, struct statvfs *);
	int (*fstatvfs)(struct fs_file *, struct statvfs *);
};

/* FS initializer */
void fs_init(void);
void fs_free(void);

/* File I/O */
struct fs_file *fs_open(const char *url, int flags, mode_t mode);
struct fs_file *fs_creat(const char *url, mode_t mode);
ssize_t fs_read(struct fs_file *f, void *buf, size_t bufsize);
ssize_t fs_read_timeout(struct fs_file *f, void *buf, size_t count,
			long timeout);
ssize_t fs_write(struct fs_file *f, const void *buf, size_t count);
ssize_t fs_write_timeout(struct fs_file *f, const void *buf, size_t count,
			 long timeout);
off_t fs_lseek(struct fs_file *f, off_t offset, int whence);
int fs_ftruncate(struct fs_file *f, off_t length);
void fs_close(struct fs_file *f);

/* Filesystem I/O */
int fs_mkdir(const char *url, mode_t mode);
int fs_unlink(const char *url);
int fs_rmdir(const char *url);
int fs_rename(const char *oldurl, const char *newurl);
int fs_chmod(const char *url, mode_t mode);

/* Directory I/O */
struct fs_dir *fs_opendir(const char *url);
struct fs_dir *fs_mount(const char *url);
struct fs_dirent *fs_readdir(struct fs_dir *d);
off_t fs_telldir(struct fs_dir *d);
void fs_closedir(struct fs_dir *d);

/* Attributes */
int fs_stat(const char *url, struct stat *buf);
int fs_fstat(struct fs_file *f, struct stat *buf);
int fs_statvfs(const char *url, struct statvfs *buf);
int fs_fstatvfs(struct fs_file *f, struct statvfs *buf);

#endif

