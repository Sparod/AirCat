/*
 * config_file.h - Configuration file reader/writer
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

#ifndef _CONFIG_FILE_H
#define _CONFIG_FILE_H

struct config {
	/* General configuration */
	char *name;
	char *password;
	long port;
	/* Radio configuration */
	int radio_enabled;
	/* RAOP configuration */
	int raop_enabled;
	char *raop_name;
	char *raop_password;
} config;

extern struct config config;

int config_load(const char *file);
int config_save(const char *file);
void config_default(void);
void config_free(void);

#endif

