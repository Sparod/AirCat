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

#define FREE_STRING(s) if(s != NULL) free(s);

struct module_list {
	/* Module properties */
	char *id;
	char *name;
	char *description;
	int enabled;
	int opened;
	/* Module pointers */
	void *lib;
	struct module *mod;
	/* Next module in list */
	struct module_list *next;
};

struct modules_handle {
	int module_count;
	struct module_list *list;
	struct json *configs;
};

int modules_open(struct modules_handle **handle, struct json *config,
		 const char *path)
{
	struct modules_handle *h;
	struct module_list *l;
	struct dirent *entry;
	struct module *mod;
	char *file;
	void *lib;
	DIR *dir;
	int len;

	/* Allocate structure */
	*handle = malloc(sizeof(struct modules_handle));
	if(*handle == NULL)
		return -1;
	h = *handle;

	/* Init structure */
	h->list = NULL;
	h->module_count = 0;
	h->configs = NULL;

	/* Open directory */
	dir = opendir(path);
	if(dir == NULL)
		return -1;

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
		l->opened = 0;
		l->lib = lib;
		l->mod = mod;

		/* Add to list */
		l->next = h->list;
		h->list = l;
		h->module_count++;
	}

	/* Close directory */
	closedir(dir);

	/* Set configuration */
	modules_set_config(h, config, NULL);

	return 0;
}

int modules_set_config(struct modules_handle *h, struct json *cfg,
		       const char *name)
{
	struct module_list *l;
	struct json *c;

	/* Free last JSON module configs */
	json_free(h->configs);

	/* Get JSON module configs */
	h->configs = json_copy(json_get(cfg, "configs"));

	/* Set to default */
	if(h->configs == NULL)
		h->configs = json_new();

	/* Update modules configuration */
	for(l = h->list; l != NULL; l = l->next)
	{
		/* Get JSON */
		c = json_get(h->configs, l->id);

		/* Add enabled status */
		if(cfg != NULL)
			l->enabled = json_get_bool(c, "enabled");

		/* Update module configuration */
		if(l->enabled != 0 && l->opened != 0 &&
		   l->mod->set_config != NULL && l->mod->handle != NULL)
		{
			l->mod->set_config(l->mod->handle, c);
		}
	}

	return 0;
}

struct json *modules_get_config(struct modules_handle *h, const char *name)
{
	struct module_list *l;
	struct json *cfg, *c;

	/* Create a new JSON object */
	c = json_new();
	if(c == NULL)
		return NULL;

	/* Get modules configuration */
	for(l = h->list; l != NULL; l = l->next)
	{
		/* Get configuration */
		cfg = NULL;
		if(l->enabled != 0 && l->opened != 0 &&
		   l->mod->get_config != NULL && l->mod->handle != NULL)
		{
			/* Get configuration */
			cfg = l->mod->get_config(l->mod->handle);
		}
		if(cfg == NULL)
			cfg = json_new();

		/* Add enabled status */
		json_set_bool(cfg, "enabled", l->enabled);

		/* Update in local configuration */
		json_add(h->configs, l->id, cfg);
	}

	/* Modules config */
	json_add(c, "configs", json_copy(h->configs));

	return c;
}

char **modules_list_modules(struct modules_handle *h, int *count)
{
	struct module_list *l;
	char **ids;

	if(h->module_count == 0)
		return NULL;

	ids = malloc(sizeof(char*) * h->module_count);
	if(ids == NULL)
		return NULL;

	/* Copy all ids */
	for(*count = 0, l = h->list; l != NULL; l = l->next)
	{
		ids[*count] = strdup(l->id);
		*count += 1;
	}

	return ids;
}

void modules_free_list(char **list, int count)
{
	int i;

	if(list == NULL || count == 0)
		return;

	/* Free all ids */
	for(i = 0; i < count; i++)
		FREE_STRING(list[i]);

	/* Free the list */
	free(list);
}

void modules_refresh(struct modules_handle *h, struct httpd_handle *httpd, 
		     struct avahi_handle *avahi, struct output_handle *output)
{
	struct module_attr attr;
	struct module_list *l;
	struct json *cfg;
	int ret;

	if(h == NULL)
		return;

	for(l = h->list; l != NULL; l = l->next)
	{
		if(l->enabled == 0 && l->opened != 0)
		{
			/* Remove module URLs from HTTP server */
			httpd_remove_urls(httpd, l->id);

			/* Get module config */
			if(l->mod->get_config != NULL && l->mod->handle != NULL)
			{
				cfg = l->mod->get_config(l->mod->handle);
				json_add(h->configs, l->id, cfg);
			}

			/*Close the module */
			if(l->mod->close != NULL && l->mod->handle != NULL)
				l->mod->close(l->mod->handle);
			l->mod->handle = NULL;

			l->opened = 0;
		}
		else if(l->enabled != 0 && l->opened == 0)
		{
			/* Prepare attributes */
			attr.avahi = avahi;
			attr.output = output;

			/* Get module configuration from file */
			attr.config = json_get(h->configs, l->id);
			attr.config = NULL;

			/* Open module */
			if(l->mod->open != NULL)
			{
				ret = l->mod->open(&l->mod->handle, &attr);
				if(ret != 0)
				{
					fprintf(stderr,
						"Failed to open %s module!\n",
						l->id);
					if(l->mod->close != NULL)
						l->mod->close(l->mod->handle);
					l->mod->handle = NULL;
					continue;
				}
			}

			/* Add module URLs to HTTP server */
			if(l->mod->urls != NULL)
				httpd_add_urls(httpd, l->id, l->mod->urls,
					       l->mod->handle);

			l->opened = 1;
		}
	}
}

void modules_close(struct modules_handle *h)
{
	struct module_list *l;

	if(h == NULL)
		return;

	/* Free modules list */
	while(h->list != NULL)
	{
		l = h->list;
		h->list = l->next;

		/*Close the module */
		if(l->mod->close != NULL && l->mod->handle != NULL)
			l->mod->close(l->mod->handle);

		/* Remove module */
		dlclose(l->lib);

		/* Free strings */
		FREE_STRING(l->id);
		FREE_STRING(l->name);
		FREE_STRING(l->description);

		/* Free entry */
		free(l);
	}

	/* Free JSON configs */
	json_free(h->configs);

	free(h);
}

