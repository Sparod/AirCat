/*
 * modules.c - Plugin handler
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

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <stdio.h>
#include <stdlib.h>
#include <dlfcn.h>
#include <dirent.h>
#include <string.h>

#include "modules.h"

struct module_list *modules_load(const char *path)
{
	struct module_list *list = NULL, *l;
	struct dirent *entry;
	struct module *mod;
	char *file;
	void *lib;
	DIR *dir;
	int len;

	/* Open directory */
	dir = opendir(path);
	if(dir == NULL)
		return NULL;

	/* Check all files */
	while((entry = readdir(dir)) != NULL)
	{
		/* Check file name */
		len = strlen(entry->d_name);
		if(strncmp(entry->d_name, "lib", 3) != 0 ||
		   strncmp(&entry->d_name[len-3], ".so", 3) != 0)
			continue;

		/* Generate file path */
		if(asprintf(&file, "%s/%s", path, entry->d_name) < 0)
			continue;

		/* Open module */
		lib = dlopen(file, RTLD_LAZY);
		free(file);
		if(lib == NULL)
			continue;

		/* Get module entry */
		mod = dlsym(lib, "module_entry");
		if(mod == NULL)
		{
			dlclose(lib);
			continue;
		}

		/* Prepare module entry */
		l = malloc(sizeof(struct module_list));
		if(l == NULL)
		{
			dlclose(lib);
			continue;
		}
		l->id = strdup(mod->name);
		l->name = NULL;
		l->description = NULL;
		l->enabled = 1;
		l->lib = lib;
		l->mod = mod;

		/* Add to list */
		l->next = list;
		list = l;
	}

	/* Close directory */
	closedir(dir);

	return list;
}

#define FREE_STRING(s) if(s != NULL) free(s);

void modules_free(struct module_list *list)
{
	struct module_list *l;

	while(list != NULL)
	{
		l = list;
		list = list->next;

		/* Close module */
		dlclose(l->lib);

		/* Free strings */
		FREE_STRING(l->id);
		FREE_STRING(l->name);
		FREE_STRING(l->description);

		/* Free entry */
		free(l);
	}
}

