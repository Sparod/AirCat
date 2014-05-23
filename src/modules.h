/*
 * modules.h - Plugin handler
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

#ifndef _MODULES_H
#define _MODULES_H

#include "module.h"

struct module_list {
	void *lib;
	struct module *mod;
	struct module_list *next;
};

struct module_list *modules_load(const char *path);
void modules_free(struct module_list *list);

#endif

