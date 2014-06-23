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
#include <pthread.h>

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
	void *handle;
	struct module *mod;
	struct output_handle *out;
	/* Next module in list */
	struct module_list *next;
};

struct modules_handle {
	int module_count;
	struct module_list *list;
	struct json *configs;
	/* Thread safe */
	pthread_mutex_t mutex;
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
		l->id = strdup(mod->id);
		l->name = strdup(mod->name);
		l->description = strdup(mod->description);
		l->enabled = 1;
		l->opened = 0;
		l->lib = lib;
		l->mod = mod;
		l->handle = NULL;
		l->out = NULL;

		/* Add to list */
		l->next = h->list;
		h->list = l;
		h->module_count++;
	}

	/* Close directory */
	closedir(dir);

	/* Init thread mutex */
	pthread_mutex_init(&h->mutex, NULL);

	/* Set configuration */
	modules_set_config(h, config, NULL);

	return 0;
}

int modules_set_config(struct modules_handle *h, struct json *cfg,
		       const char *name)
{
	struct module_list *l;
	struct json *c;

	/* Lock modules access */
	pthread_mutex_lock(&h->mutex);

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
		/* Check if name requested */
		if(name != NULL && *name != '\0' && strcmp(name, l->id) != 0)
			continue;

		/* Get JSON */
		c = json_get(h->configs, l->id);

		/* Add enabled status */
		if(cfg != NULL)
			l->enabled = json_get_bool(c, "enabled");

		/* Update module configuration */
		if(l->enabled != 0 && l->opened != 0 &&
		   l->mod->set_config != NULL && l->handle != NULL)
		{
			l->mod->set_config(l->handle, c);
		}
	}

	/* Unlock modules access */
	pthread_mutex_unlock(&h->mutex);

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

	/* Lock modules access */
	pthread_mutex_lock(&h->mutex);

	/* Get modules configuration */
	for(l = h->list; l != NULL; l = l->next)
	{
		/* Get configuration */
		cfg = NULL;
		if(l->enabled != 0 && l->opened != 0 &&
		   l->mod->get_config != NULL && l->handle != NULL)
		{
			/* Get configuration */
			cfg = l->mod->get_config(l->handle);
		}
		if(cfg == NULL)
			cfg = json_new();

		/* Add enabled status */
		json_set_bool(cfg, "enabled", l->enabled);

		/* Update in local configuration */
		json_add(h->configs, l->id, cfg);
	}

	/* Modules config */
	if(name != NULL && *name != '\0')
	{
		cfg = json_new();
		json_add(cfg, name, json_copy(json_get(h->configs, name)));
	}
	else
		cfg = json_copy(h->configs);
	json_add(c, "configs", cfg);

	/* Unlock modules access */
	pthread_mutex_unlock(&h->mutex);

	return c;
}

char **modules_list_modules(struct modules_handle *h, int *count)
{
	struct module_list *l;
	char **ids = NULL;

	/* Lock modules access */
	pthread_mutex_lock(&h->mutex);

	if(h->module_count == 0)
		goto end;

	ids = malloc(sizeof(char*) * h->module_count);
	if(ids == NULL)
		goto end;

	/* Copy all ids */
	for(*count = 0, l = h->list; l != NULL; l = l->next)
	{
		ids[*count] = strdup(l->id);
		*count += 1;
	}
end:
	/* Unlock modules access */
	pthread_mutex_unlock(&h->mutex);

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
		     struct avahi_handle *avahi, struct outputs_handle *outputs)
{
	struct module_attr attr;
	struct module_list *l;
	struct json *cfg;
	int ret;

	if(h == NULL)
		return;

	/* Lock modules access */
	pthread_mutex_lock(&h->mutex);

	for(l = h->list; l != NULL; l = l->next)
	{
		if(l->enabled == 0 && l->opened != 0)
		{
			/* Remove module URLs from HTTP server */
			if(httpd_remove_urls(httpd, l->id) != 0)
			{
				/* Cannot remove URLs from HTTP server: retry on
				 * next refresh call.
				 * Keeps the module in disabling status.
				 */
				continue;
			}

			/* Get module config */
			if(l->mod->get_config != NULL && l->handle != NULL)
			{
				cfg = l->mod->get_config(l->handle);
				json_add(h->configs, l->id, cfg);
			}

			/*Close the module */
			if(l->mod->close != NULL && l->handle != NULL)
				l->mod->close(l->handle);
			l->handle = NULL;

			/* Free output handler */
			if(l->out != NULL)
				output_close(l->out);

			l->opened = 0;
		}
		else if(l->enabled != 0 && l->opened == 0)
		{
			/* Create an output handler for module */
			output_open(&l->out, outputs, l->name);

			/* Prepare attributes */
			attr.avahi = avahi;
			attr.output = l->out;

			/* Get module configuration from file */
			attr.config = json_get(h->configs, l->id);

			/* Open module */
			if(l->mod->open != NULL)
			{
				ret = l->mod->open(&l->handle, &attr);
				if(ret != 0)
				{
					fprintf(stderr,
						"Failed to open %s module!\n",
						l->id);
					if(l->mod->close != NULL)
						l->mod->close(l->handle);
					l->handle = NULL;
					continue;
				}
			}

			/* Add module URLs to HTTP server */
			if(l->mod->urls != NULL)
				httpd_add_urls(httpd, l->id, l->mod->urls,
					       l->handle);

			l->opened = 1;
		}
	}

	/* Unlock modules access */
	pthread_mutex_unlock(&h->mutex);

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
		if(l->mod->close != NULL && l->handle != NULL)
			l->mod->close(l->handle);

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

static int modules_httpd_enable(struct modules_handle *h, struct httpd_req *req,
				unsigned char **buffer, size_t *size)
{
	struct module_list *l;

	if(req->resource == NULL || *req->resource == '\0')
		return 400;

	/* Lock modules access */
	pthread_mutex_lock(&h->mutex);

	/* Process each module in list */
	for(l = h->list; l != NULL; l = l->next)
	{
		if(strcmp(req->resource, l->id) == 0)
		{
			/* Enable module */
			l->enabled = 1;

			/* Unlock modules access */
			pthread_mutex_unlock(&h->mutex);

			return 200;
		}
	}

	/* Unlock modules access */
	pthread_mutex_unlock(&h->mutex);

	return 404;
}

static int modules_httpd_disable(struct modules_handle *h,
				 struct httpd_req *req, unsigned char **buffer,
				 size_t *size)
{
	struct module_list *l;

	if(req->resource == NULL || *req->resource == '\0')
		return 400;

	/* Lock modules access */
	pthread_mutex_lock(&h->mutex);

	/* Process each module in list */
	for(l = h->list; l != NULL; l = l->next)
	{
		if(strcmp(req->resource, l->id) == 0)
		{
			/* Disable module */
			l->enabled = 0;

			/* Unlock modules access */
			pthread_mutex_unlock(&h->mutex);

			return 200;
		}
	}

	/* Unlock modules access */
	pthread_mutex_unlock(&h->mutex);

	return 404;
}

static int modules_httpd_config(struct modules_handle *h, struct httpd_req *req,
				unsigned char **buffer, size_t *size)
{
	struct json *json;
	const char *str;

	if(req->method == HTTPD_GET)
	{
		/* Create a JSON object */
		json = modules_get_config(h, req->resource);
		if(json == NULL)
			return 404;

		/* Get string */
		str = strdup(json_export(json));
		*buffer = (unsigned char*) str;
		if(str != NULL)
			*size = strlen(str);

		/* Free configuration */
		json_free(json);
	}
	else
		modules_set_config(h, req->json, req->resource);

	return 200;
}

static int modules_httpd_list(struct modules_handle *h, struct httpd_req *req,
			      unsigned char **buffer, size_t *size)
{
	struct json *json, *tmp;
	struct module_list *l;
	char *str;

	/* Create a new JSON array */
	json = json_new_array();
	if(json != NULL)
	{
		/* Lock modules access */
		pthread_mutex_lock(&h->mutex);

		/* Process each module in list */
		for(l = h->list; l != NULL; l = l->next)
		{
			/* Create a temporary JSON object */
			tmp = json_new();
			if(tmp == NULL)
				continue;

			/* Add fields */
			json_set_bool(tmp, "enabled", l->enabled);
			json_set_string(tmp, "id", l->id);
			json_set_string(tmp, "name", l->name);
			json_set_string(tmp, "description", l->description);

			/* Add object to array */
			if(json_array_add(json, tmp) != 0)
				json_free(tmp);
		}

		/* Unlock modules access */
		pthread_mutex_unlock(&h->mutex);
	}

	/* Get JSON string */
	str = strdup(json_export(json));
	*buffer = (unsigned char*) str;
	if(str != NULL)
		*size = strlen(str);

	/* Free JSON object */
	json_free(json);

	return 200;
}

struct url_table modules_urls[] = {
	{"/enable/",  HTTPD_EXT_URL, HTTPD_PUT,             0,
						 (void*) &modules_httpd_enable},
	{"/disable/", HTTPD_EXT_URL, HTTPD_PUT,             0,
						(void*) &modules_httpd_disable},
	{"/config",   HTTPD_EXT_URL, HTTPD_PUT | HTTPD_GET, 0,
						 (void*) &modules_httpd_config},
	{"/list",    0,              HTTPD_GET,             0,
						   (void*) &modules_httpd_list},
	{0, 0, 0, 0}
};

