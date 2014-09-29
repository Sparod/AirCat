/*
 * fs_smb.h - A SMB (Windows file sharing) implementation for FS
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
#ifndef _FS_SMB_H
#define _FS_SMB_H

#include "fs.h"

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef HAVE_LIBSMBCLIENT

void fs_smb_init(void);
void fs_smb_free(void);
extern struct fs_handle fs_smb;

#endif

#endif

