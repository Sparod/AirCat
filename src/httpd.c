/*
 * httpd.c - An HTTP server
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>

#include <microhttpd.h>
#include <json.h>
#include <json_tokener.h>

#include "config_file.h"
#include "radio.h"
#include "airtunes.h"
#include "files.h"

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "httpd.h"

#define OPAQUE "11733b200778ce33060f31c9af70a870ba96ddd4"

enum {
	HTTP_GET = 1,
	HTTP_PUT = 2,
	HTTP_POST = 4,
	HTTP_DELETE = 8
};

struct mime_type {
	const char *ext;
	const char *mime;
} mime_type[] = {
	{"html", "text/html"},
	{"htm",  "text/html"},
	{"gif",  "image/gif"},
	{"jpg",  "image/jpeg"},
	{"jpeg", "image/jpeg"},
	{"png",  "image/png"},
	{"svg",  "image/svg+xm"},
	{"ico",  "image/vnd.microsoft.icon"},
	{"bmp",  "image/x-ms-bmp"},
	{0, 0}
};

struct request_data {
	void *data;
	void (*free)(void *);
};

struct request_attr {
	struct httpd_handle *handle;
	struct MHD_Connection *connection;
	const char *url;
	int method;
	const char *upload_data;
	size_t *upload_data_size;
	struct request_data **req_data;
};

struct url_table {
	const char *url;
	int strict_cmp;
	int method;
	int (*process)(struct request_attr *);
};

struct httpd_handle {
	/* MicroHTTPD handle */
	struct MHD_Daemon *httpd;
	char *realm;
	char *opaque;
	/* Radio module */
	struct radio_handle *radio;
	/* Airtunes module */
	struct airtunes_handle *airtunes;
	/* Files module */
	struct files_handle *files;
	/* Config file */
	char *config_file;
};

static int httpd_request(void * user_data, struct MHD_Connection *c,
			 const char *url, const char *method,
			 const char *version, const char *upload_data,
			 size_t *upload_data_size, void ** ptr);
static void httpd_completed(void *user_data, struct MHD_Connection *c,
			    void **ptr, enum MHD_RequestTerminationCode toe);

int httpd_open(struct httpd_handle **handle, struct httpd_attr *attr)
{
	struct httpd_handle *h;

	/* Allocate structure */
	*handle = malloc(sizeof(struct httpd_handle));
	if(*handle == NULL)
		return -1;
	h = *handle;

	/* Init structure */
	if(config.name != NULL)
		h->realm = strdup(config.name);
	else
		h->realm = strdup("AirCat");
	h->opaque = strdup(OPAQUE);
	h->config_file = strdup(attr->config_filename);
	h->radio = attr->radio;
	h->airtunes = attr->airtunes;
	h->files = attr->files;
	h->httpd = NULL;

	return 0;
}

int httpd_start(struct httpd_handle *h)
{
	if(h == NULL)
		return -1;

	/* Verify if HTTP server is running */
	if(h->httpd != NULL)
		return 0;

	/* Start HTTP server */
	h->httpd = MHD_start_daemon(MHD_USE_SELECT_INTERNALLY, config.port, 
			    NULL, NULL,
			    &httpd_request, h,
			    MHD_OPTION_NOTIFY_COMPLETED, &httpd_completed, NULL,
			    MHD_OPTION_END);
	if(h->httpd == NULL)
		return -1;

	return 0;
}

int httpd_stop(struct httpd_handle *h)
{
	if(h == NULL)
		return -1;

	/* Verify if HTTP server is running */
	if(h->httpd == NULL)
		return 0;

	/* Stop HTTP server */
	MHD_stop_daemon(h->httpd);
	h->httpd = NULL;

	return 0;
}

static int httpd_strcmp(const char *str1, const char *str2, int strict_cmp)
{
	if(strict_cmp)
		return strcmp(str1, str2);

	while(*str1 != 0)
	{
		if(*str2 == 0)
			return *str1;
		if(*str1 != *str2)
			return *str1-*str2;
		str1++, str2++;
	}
	return 0;
}

int httpd_close(struct httpd_handle *h)
{
	if(h == NULL)
		return 0;

	/* Stop HTTP server */
	if(h->httpd != NULL)
		httpd_stop(h);

	/* Free Realm */
	if(h->realm != NULL)
		free(h->realm);

	/* Free opaque */
	if(h->opaque != NULL)
		free(h->opaque);

	/* Free config file */
	if(h->config_file != NULL)
		free(h->config_file);

	free(h);

	return 0;
}

static int httpd_get_method_code(const char *method)
{
	if(strcmp(method, MHD_HTTP_METHOD_GET) == 0)
		return HTTP_GET;
	if(strcmp(method, MHD_HTTP_METHOD_PUT) == 0)
		return HTTP_PUT;
	if(strcmp(method, MHD_HTTP_METHOD_POST) == 0)
		return HTTP_POST;
	if(strcmp(method, MHD_HTTP_METHOD_DELETE) == 0)
		return HTTP_DELETE;

	return 0;
}

static int httpd_response(struct MHD_Connection *c, int code, char *msg)
{
	struct MHD_Response *response;
	int ret;

	/* Create HTTP response with message */
	response = MHD_create_response_from_data(strlen(msg), msg, MHD_NO, 
						 MHD_NO);

	/* Queue it */
	ret = MHD_queue_response(c, code, response);

	/* Destroy local response */
	MHD_destroy_response(response);

	return ret;
}

struct dir_data {
	DIR *dir;
	char *url;
};

static ssize_t httpd_dir_list_cb(void *user_data, uint64_t pos, char *buffer, 
				  size_t size)
{
	struct dir_data *d = (struct dir_data*) user_data;
	struct dirent *entry;

	if(d == NULL)
		return 0;

	/* Get next file in directory without . as first character */
	do
	{
		entry = readdir(d->dir);
		if(entry == NULL)
			return MHD_CONTENT_READER_END_OF_STREAM;
	}
	while(entry->d_name[0] == '.');

	return snprintf(buffer, size, "<a href=\"%s/%s\">%s</a><br/>",
			d->url, entry->d_name, entry->d_name);
}

static void httpd_dir_free_cb(void *user_data)
{
	struct dir_data *d = (struct dir_data*) user_data;

	if(d != NULL)
	{
		/* Close directory handler */
		if(d->dir != NULL)
			closedir(d->dir);
		/* Free url string */
		if(d->url != NULL)
			free(d->url);
		/* Free dir structure */
		free(d);
	}
}

static ssize_t httpd_file_read_cb(void *user_data, uint64_t pos, char *buffer, 
				  size_t size)
{
	FILE *fp = (FILE*) user_data;

	if(fp == NULL)
		return 0;

	/* Seek in file */
	fseek(fp, pos, SEEK_SET);

	/* Read from file */
	return fread (buffer, 1, size, fp);
}

static void httpd_file_free_cb(void *user_data)
{
	FILE *fp = (FILE*) user_data;

	/* Close file */
	if(fp != NULL)
		fclose(fp);
}

static int httpd_file_response(struct MHD_Connection *c, const char *url)
{
	struct MHD_Response *response;
	struct dir_data *d_data;
	struct stat s;
	char *path;
	char *ext;
	FILE *fp;
	int ret;
	int i;

	/* Verify web path */
	if(config.web_path == NULL)
		return httpd_response(c, 500, "Web path not configured!");

	/* Create file path */
	path = malloc(strlen(config.web_path) + strlen(url) + 12);
	if(path == NULL)
		return httpd_response(c, 500, "Internal Error");
	sprintf(path, "%s%s", config.web_path, url);

	/* Get file properties */
	if(stat(path, &s) != 0)
	{
		free(path);
		return httpd_response(c, 404, "File not found");
	}
	/* Check if it is a directory */
	if(S_ISDIR(s.st_mode))
	{
		/* Check if file index.html is in directory */
		strcat(path, "/index.html");
		if(stat(path, &s) != 0)
		{
			/* Prepare directory structure */
			d_data = malloc(sizeof(struct dir_data));
			if(d_data == NULL)
				return httpd_response(c, 500, "Internal Error");

			/* List files in directory */
			path[strlen(path)-11] = 0;
			d_data->dir = opendir(path);
			free(path);
			if(d_data->dir == NULL)
			{
				free(d_data);
				return httpd_response(c, 404, "No directory");
			}

			/* Copy URL */
			ret = strlen(url);
			if(url[ret-1] == '/')
				ret--;
			d_data->url = strndup(url, ret);

			/* Create HTTP response with file list */
			response = MHD_create_response_from_callback(
							MHD_SIZE_UNKNOWN, 8192,
							&httpd_dir_list_cb,
							d_data,
							&httpd_dir_free_cb);

			/* Queue it */
			ret = MHD_queue_response(c, 200, response);

			/* Destroy local response */
			MHD_destroy_response(response);

			return ret;
		}
	}

	/* Open File */
	fp = fopen(path, "rb");
	if(fp == NULL)
	{
		free(path);
		return httpd_response(c, 404, "File not found");
	}

	/* Create HTTP response with file content */
	response = MHD_create_response_from_callback(s.st_size, 8192,
						     &httpd_file_read_cb, fp,
						     &httpd_file_free_cb);

	/* Get mime type */
	ext = strrchr(path, '.');
	if(ext != NULL && strlen(ext+1) <= 4)
	{
		for(i = 0; mime_type[i].ext != NULL; i++)
		{
		
			if(strcmp(mime_type[i].ext, ext+1) == 0)
			{
				MHD_add_response_header(response,
						   MHD_HTTP_HEADER_CONTENT_TYPE,
						   mime_type[i].mime);
				break;
			}
		}
	}
	free(path);

	/* Queue it */
	ret = MHD_queue_response(c, 200, response);

	/* Destroy local response */
	MHD_destroy_response(response);

	return ret;
}

/*
 * /!\ This function frees the json string!
 */
static int httpd_json_response(struct MHD_Connection *c, int code, char *json,
			       int len)
{
	struct MHD_Response *response;
	int ret;

	/* Create HTTP response with JSON data */
	response = MHD_create_response_from_data(len, json, MHD_YES, MHD_NO);

	/* Add header to specify JSON is type content */
	MHD_add_response_header(response, MHD_HTTP_HEADER_CONTENT_TYPE,
				"application/json");
	/* Add header to allow cross domain AJAX request */
	MHD_add_response_header(response, "Access-Control-Allow-Origin", "*");

	/* Queue it */
	ret = MHD_queue_response(c, code, response);

	/* Destroy local response */
	MHD_destroy_response(response);

	return ret;
}

static int httpd_json_msg(struct MHD_Connection *c, int code, const char *msg)
{
	int len = strlen(msg) + 14;
	char *json;

	json = malloc(sizeof(char)*len);
	if(json == NULL)
		return MHD_NO;

	sprintf(json, "{ \"msg\": \"%s\" }", msg);

	if(len == 14)
		len = 1;

	return httpd_json_response(c, code, json, len-1);
}

/******************************************************************************
 *                                JSON Parser                                 *
 ******************************************************************************/

struct json_data {
	struct json_tokener *tokener;
	struct json_object *object;
};

static void httpd_free_json(struct json_data *json)
{
	if(json != NULL)
	{
		if(json->tokener != NULL)
			json_tokener_free(json->tokener);
		if(json->object != NULL)
			json_object_put(json->object);
		free(json);
	}
}

static int httpd_parse_json(struct request_data **data, const char *buffer,
			    size_t *len)
{
	struct json_data *json;

	/* Allocate data handlers if first call */
	if(*data == NULL)
	{
		/* Allocate request data */
		*data = malloc(sizeof(struct request_data));
		if(*data == NULL)
			return -1;

		/* Allocate JSON data and set free function */
		(*data)->free = (void(*)(void*))&httpd_free_json;
		(*data)->data = malloc(sizeof(struct json_data));
		if((*data)->data == NULL)
			return -1;

		/* Prepare JSON handler */
		json = (struct json_data*) (*data)->data;
		json->object = NULL;
		json->tokener = json_tokener_new();
		if(json->tokener == NULL)
			return -1;

		/* Continue */
		return 1;
	}

	/* Get pointer of JSON data */
	json = (struct json_data*) (*data)->data;
	if(json == NULL || json->tokener == NULL)
		return -1;

	/* Some data are still available: parse it */
	if(*len != 0)
	{
		/* Append buffer and parse it */
		json->object = json_tokener_parse_ex(json->tokener, buffer,
						     *len);
		*len = 0;

		/* Continue */
		return 1;
	}

	/* End of stream */
	if(json != NULL && json->tokener != NULL)
	{
		json_tokener_free(json->tokener);
		json->tokener = NULL;
	}

	return 0;
}

/******************************************************************************
 *                          Configuration Part                                *
 ******************************************************************************/

static int httpd_config_reload(struct request_attr *attr)
{
	if(attr->handle->config_file != NULL &&
	   config_load(attr->handle->config_file) == 0)
		return httpd_json_msg(attr->connection, 200, "");

	return httpd_json_msg(attr->connection, 500, "File cannot be loaded!");
}

static int httpd_config_save(struct request_attr *attr)
{
	if(attr->handle->config_file != NULL &&
	   config_save(attr->handle->config_file) == 0)
		return httpd_json_msg(attr->connection, 200, "");

	return httpd_json_msg(attr->connection, 500, "File cannot be saved!");
}

static int httpd_config_default(struct request_attr *attr)
{
	config_default();
	return httpd_json_msg(attr->connection, 200, "");
}

struct config_tab {
	const char *name;
	enum {NODE, STRING, BOOLEAN, NUMBER} type;
	void *value;
};

struct config_tab config_radio_tab[] = {
	{"enabled", BOOLEAN, &config.radio_enabled},
	{0, 0, 0}
};

struct config_tab config_raop_tab[] = {
	{"enabled", BOOLEAN, &config.raop_enabled},
	{"name", STRING, &config.raop_name},
	{"password", STRING, &config.raop_password},
	{0, 0, 0}
};

struct config_tab config_tab[] = {
	{"name", STRING, &config.name},
	{"password", STRING, &config.password},
	{"port", NUMBER, &config.port},
	{"radio", NODE, &config_radio_tab},
	{"raop", NODE, &config_raop_tab},
	{0, 0, 0}
};

static void httpd_get_config(struct config_tab *tab, struct json_object *root)
{
	struct json_object *tmp;
	const char *str;
	int i;

	/* Process config_tab */
	for(i = 0; tab[i].name != NULL; i++)
	{
		/* Prepate JSON object */
		tmp = NULL;

		/* Get configuration and create new JSON objects */
		switch(tab[i].type)
		{
			case NODE:
				tmp = json_object_new_object();
				httpd_get_config(tab[i].value, tmp);
				break;
			case STRING:
				str = *((char**)tab[i].value);
				if(str != NULL)
					tmp = json_object_new_string(str);
				break;
			case NUMBER:
				tmp = json_object_new_int(
						        *((long*)tab[i].value));
				break;
			case BOOLEAN:
				tmp = json_object_new_boolean(
						         *((int*)tab[i].value));
				break;
			default:
				continue;
		}

		/* Add JSON object to root */
		json_object_object_add(root, tab[i].name, tmp);
	}
}

static void httpd_put_config(struct config_tab *tab, struct json_object *root,
			     const char *url)
{
	struct json_object *current;
	char **c_str;
	const char *str, *next = NULL;
	long *c_nb = NULL, nb;
	int *c_bool = NULL, bool;
	int i, len;

	/* Process URL if not NULL */
	if(url != NULL)
	{
		next = strchr(url, '/');
		if(next != NULL)
		{
			len = next-url;
			next++;
		}
		else
			len = strlen(url);
	}

	/* Process config_tab */
	for(i = 0; tab[i].name != NULL; i++)
	{
		/* Check if it is the entry searched */
		if(url != NULL && (strlen(tab[i].name) != len ||
		    strncmp(tab[i].name, url, len) != 0))
				continue;

		/* Get JSON object by name */
		if(json_object_object_get_ex(root, tab[i].name, &current) == 0)
			continue;

		/* Update configuration values */
		switch(tab[i].type)
		{
			case NODE:
				httpd_put_config(tab[i].value, current, next);
				break;
			case STRING:
				c_str = (char**) tab[i].value;
				str = json_object_get_string(current);
				if(*c_str == NULL ||
				    (str != NULL && strcmp(*c_str, str) != 0))
				{
					if(*c_str != NULL)
						free(*c_str);
					if(str != NULL && str[0] != 0)
						*c_str = strdup(str);
					else
						*c_str = NULL;
				}
				break;
			case NUMBER:
				c_nb = (long*) tab[i].value;
				nb = (long) json_object_get_int(current);
				*c_nb = nb;
				break;
			case BOOLEAN:
				c_bool = (int*) tab[i].value;
				bool = (int) json_object_get_boolean(current);
				*c_bool = bool != 0 ? 1 : 0;
				break;
			default:
				continue;
		}
	}
}

static int httpd_config(struct request_attr *attr)
{
	struct request_data *req_data;
	struct json_object *json_root;
	char *url = NULL;
	char *json;
	int ret;

	/* Make JSON response */
	if(attr->method == HTTP_GET)
	{
		/* Create a new JSON object */
		json_root = json_object_new_object();

		/* Fill it with configuration values from config_tab */
		httpd_get_config(config_tab, json_root);

		/* Get string from JSON object */
		json = strdup(json_object_to_json_string(json_root));

		/* Free JSON object */
		json_object_put(json_root);

		/* Respond with JSON strong */
		return httpd_json_response(attr->connection, 200, json,
					   strlen(json));
	}
	else
	{
		/* Parse JSON */
		ret = httpd_parse_json(attr->req_data, attr->upload_data,
				       attr->upload_data_size);
		if(ret == 1)
			return MHD_YES;
		else if(ret < 0)
			httpd_response(attr->connection, 500, "Internal error!");

		/* Get JSON object */
		req_data = *attr->req_data;
		json_root = ((struct json_data*)req_data->data)->object;

		/* Process JSON data */
		if(json_root != NULL)
		{
			/* Check URL */
			if(strlen(attr->url) > 8)
				url = (char*)attr->url + 8;

			/* Change configuration */
			httpd_put_config(config_tab, json_root, url);
		}

		/* Respond with success message */
		return httpd_json_msg(attr->connection, 200, "");
	}

	return httpd_json_msg(attr->connection, 400,
			      "Bad request on configuration!");
}

/******************************************************************************
 *                               Radio Part                                   *
 ******************************************************************************/

static int httpd_radio_cat_info(struct request_attr *attr)
{
	char *id = NULL;
	char *info;

	/* Get category name */
	if(attr->url != NULL)
		id = strstr(attr->url, "info/");
	if(id == NULL || id[5] == 0)
		return httpd_json_msg(attr->connection, 400, "Bad request");
	id += 5;

	/* Get info about category */
	info = radio_get_json_category_info(attr->handle->radio, id);
	if(info == NULL)
		return httpd_json_msg(attr->connection, 404, "Radio not found");

	return httpd_json_response(attr->connection, 200, info, strlen(info));
}

static int httpd_radio_info(struct request_attr *attr)
{
	char *id = NULL;
	char *info;

	/* Get radio name */
	if(attr->url != NULL)
		id = strstr(attr->url, "info/");
	if(id == NULL || id[5] == 0)
		return httpd_json_msg(attr->connection, 400, "Bad request");
	id += 5;

	/* Get info about radio */
	info = radio_get_json_radio_info(attr->handle->radio, id);
	if(info == NULL)
		return httpd_json_msg(attr->connection, 404, "Radio not found");

	return httpd_json_response(attr->connection, 200, info, strlen(info));
}

static int httpd_radio_list(struct request_attr *attr)
{
	char *list = NULL;
	char *id = NULL;

	/* Get radio / category path*/
	if(attr->url != NULL)
	{
		id = strstr(attr->url, "list/");
		if(id != NULL)
			id+=5;
	}

	if(id != NULL && *id == 0)
		return httpd_json_msg(attr->connection, 400, "Bad request");

	/* Get Radio list */
	list = radio_get_json_list(attr->handle->radio, id);
	if(list == NULL)
		return httpd_json_msg(attr->connection, 500, "No radio list");

	return httpd_json_response(attr->connection, 200, list, strlen(list));
}

/******************************************************************************
 *                             Airtunes Part                                  *
 ******************************************************************************/

static int httpd_raop_status(struct request_attr *attr)
{
	return httpd_json_msg(attr->connection, 200, "");
}

static int httpd_raop_img(struct request_attr *attr)
{
	return httpd_json_msg(attr->connection, 200, "");
}

static int httpd_raop_restart(struct request_attr *attr)
{
	//airtunes_restart(attr->handle->airtunes);
	return httpd_json_msg(attr->connection, 200, "");
}

/******************************************************************************
 *                               Files Part                                   *
 ******************************************************************************/

static int httpd_files_play(struct request_attr *attr)
{
	char *filename = NULL;

	/* Get list file in path*/
	if(attr->url != NULL)
	{
		filename = strstr(attr->url, "play/");
		if(filename != NULL)
			filename += 5;
	}

	if(filename != NULL && *filename == 0)
		return httpd_json_msg(attr->connection, 400, "Bad request");

	if(files_play(attr->handle->files, filename) != 0)
		return httpd_json_msg(attr->connection, 406,
						       "File is not supported");

	return httpd_json_msg(attr->connection, 200, "");
}

static int httpd_files_pause(struct request_attr *attr)
{
	files_pause(attr->handle->files);

	return httpd_json_msg(attr->connection, 200, "");
}

static int httpd_files_stop(struct request_attr *attr)
{
	files_stop(attr->handle->files);

	return httpd_json_msg(attr->connection, 200, "");
}

static int httpd_files_list(struct request_attr *attr)
{
	char *list = NULL;
	char *path = NULL;

	/* Get list file in path*/
	if(attr->url != NULL)
	{
		path = strstr(attr->url, "list/");
		if(path != NULL)
			path += 5;
	}

	if(path != NULL && *path == 0)
		return httpd_json_msg(attr->connection, 400, "Bad request");

	/* Get file list */
	list = files_get_json_list(attr->handle->files, path);
	if(list == NULL)
		return httpd_json_msg(attr->connection, 404, "Bad directory");

	return httpd_json_response(attr->connection, 200, list, strlen(list));
}

/******************************************************************************
 *                          Main HTTP request parser                          *
 ******************************************************************************/

struct url_table url_table[] = {
	{"/config/reload", 1, HTTP_PUT, &httpd_config_reload},
	{"/config/save", 1, HTTP_PUT, &httpd_config_save},
	{"/config/default", 1, HTTP_PUT, &httpd_config_default},
	{"/config/", 0, HTTP_PUT, &httpd_config},
	{"/config", 1, HTTP_GET | HTTP_PUT, &httpd_config},
	{"/radio/category/info/", 0, HTTP_GET, &httpd_radio_cat_info},
	{"/radio/info/", 0, HTTP_GET, &httpd_radio_info},
	{"/radio/list", 0, HTTP_GET, &httpd_radio_list},
	{"/raop/status", 1, HTTP_GET, &httpd_raop_status},
	{"/raop/img", 1, HTTP_GET, &httpd_raop_img},
	{"/raop/restart", 1, HTTP_PUT, &httpd_raop_restart},
	{"/files/play/", 0, HTTP_PUT, &httpd_files_play},
	{"/files/pause", 1, HTTP_PUT, &httpd_files_pause},
	{"/files/stop", 1, HTTP_PUT, &httpd_files_stop},
	{"/files/list", 0, HTTP_GET, &httpd_files_list},
	{0, 0, 0}
};

static int httpd_request(void * user_data, struct MHD_Connection *c, 
			 const char * url, const char * method, 
			 const char * version, const char * upload_data,
			 size_t * upload_data_size, void ** ptr)
{
	struct httpd_handle *h = (struct httpd_handle*) user_data;
	struct MHD_Response *response;
	struct request_attr attr;
	int method_code = 0;
	char *username;
	int ret;
	int i;

	/* Authentication check */
	if(*ptr == NULL && config.password != NULL)
	{
		/* Get username */
		username = MHD_digest_auth_get_username(c);
		if(username != NULL)
		{
			/* Check password */
			ret = MHD_digest_auth_check(c, h->realm, username, 
						    config.password, 300);
			free(username);
		}
		else
			ret = MHD_NO;

		/* Bad authentication */
		if((ret == MHD_INVALID_NONCE) || (ret == MHD_NO))
		{
			/* Create HTTP response with failure page */
			response = MHD_create_response_from_buffer(2, "KO",
							MHD_RESPMEM_PERSISTENT);

			/* Queue it with Authentication headers */
			ret = MHD_queue_auth_fail_response(c, h->realm, 
				 h->opaque, response,
				 (ret == MHD_INVALID_NONCE) ? MHD_YES : MHD_NO);

			/* Destroy local response */
			MHD_destroy_response(response);
			return ret;
		}
	}

	/* Get methode code */
	method_code = httpd_get_method_code(method);
	if(method_code == 0)
		return httpd_response(c, 405, "Method not allowed!");

	/* Parse URL and do associated action */
	for(i = 0; url_table[i].url != NULL; i++)
	{
		if(httpd_strcmp(url_table[i].url, url,
				url_table[i].strict_cmp) == 0)
		{
			/* Verify method */
			if((url_table[i].method & method_code) == 0)
				return httpd_response(c, 406,
						      "Method not acceptable!");

			/* Prepare attributes for processing URL */
			attr.handle = h;
			attr.connection = c;
			attr.url = url;
			attr.method = method_code;
			attr.upload_data = upload_data;
			attr.upload_data_size = upload_data_size;
			attr.req_data = (struct request_data **) ptr;

			/* Process URL */
			return url_table[i].process(&attr);
		}
	}

	/* Accept only GET method if not a special URL */
	if(method_code != HTTP_GET)
		return httpd_response(c, 406, "Method not acceptable!");

	/* Response with the requested file */
	return httpd_file_response(c, url);
}

static void httpd_completed(void *user_data, struct MHD_Connection *c,
			    void **ptr, enum MHD_RequestTerminationCode toe)
{
	struct request_data *req = *((struct request_data **)ptr);

	if(req == NULL)
		return;

	/* Free request data */
	if(req->data != NULL)
	{
		if(req->free != NULL)
			req->free(req->data);
		else
			free(req->data);
	}

	free(req);
}
