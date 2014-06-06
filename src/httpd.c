/*
 * httpd.c - An HTTP server
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
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <pthread.h>

#include <microhttpd.h>
#include <json.h>
#include <json_tokener.h>

#include "config_file.h"

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "httpd.h"

#define OPAQUE "11733b200778ce33060f31c9af70a870ba96ddd4"

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
	const char *res;
	int method;
	const char *upload_data;
	size_t *upload_data_size;
	struct request_data **req_data;
};

struct httpd_urls {
	char *name;
	void *user_data;
	struct url_table *urls;
	struct httpd_urls *next;
};

struct httpd_handle {
	/* MicroHTTPD handle */
	struct MHD_Daemon *httpd;
	char *opaque;
	/* Configuration */
	char *name;
	char *path;
	char *password;
	unsigned int port;
	/* URLs list */
	struct httpd_urls *urls;
	pthread_mutex_t mutex;
};

static int httpd_request(void * user_data, struct MHD_Connection *c,
			 const char *url, const char *method,
			 const char *version, const char *upload_data,
			 size_t *upload_data_size, void ** ptr);
static void httpd_completed(void *user_data, struct MHD_Connection *c,
			    void **ptr, enum MHD_RequestTerminationCode toe);

int httpd_open(struct httpd_handle **handle, struct json *config)
{
	struct httpd_handle *h;

	/* Allocate structure */
	*handle = malloc(sizeof(struct httpd_handle));
	if(*handle == NULL)
		return -1;
	h = *handle;

	/* Init structure */
	h->opaque = strdup(OPAQUE);
	h->httpd = NULL;
	h->name = NULL;
	h->path = NULL;
	h->password = NULL;
	h->port = 0;
	h->urls = NULL;

	/* Init mutex */
	pthread_mutex_init(&h->mutex, NULL);

	/* Set configuration */
	httpd_set_config(h, config);

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
	h->httpd = MHD_start_daemon(MHD_USE_SELECT_INTERNALLY, h->port, 
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

int httpd_set_config(struct httpd_handle *h, struct json *cfg)
{
	const char *str;

	if(h == NULL)
		return -1;

	/* Free previous values */
	if(h->name != NULL)
		free(h->name);
	if(h->path != NULL)
		free(h->path);
	if(h->password != NULL)
		free(h->password);
	h->name = NULL;
	h->path = NULL;
	h->password = NULL;
	h->port = 0;

	/* Get configuration */
	if(cfg != NULL)
	{
		/* Get values from configuration */
		str = json_get_string(cfg, "name");
		if(str != NULL)
			h->name = strdup(str);
		str = json_get_string(cfg, "web_path");
		if(str != NULL)
			h->path = strdup(str);
		str = json_get_string(cfg, "password");
		if(str != NULL && *str != '\0')
			h->password = strdup(str);
		h->port = json_get_int(cfg, "port");
	}

	/* Set default values */
	if(h->name == NULL)
		h->name = strdup("AirCat");
	if(h->path == NULL)
		h->path = strdup("/var/aircat/www");
	if(h->port == 0)
		h->port = 8080;

	return 0;
}

struct json *httpd_get_config(struct httpd_handle *h)
{
	struct json *j;

	/* Create a new configuration */
	j = json_new();
	if(j == NULL)
		return NULL;

	/* Set current parameters */
	json_set_string(j, "name", h->name);
	json_set_string(j, "web_path", h->path);
	json_set_string(j, "password", h->password);
	json_set_int(j, "port", h->port);

	return j;
}

int httpd_add_urls(struct httpd_handle *h, const char *name,
		   struct url_table *urls, void *user_data)
{
	struct httpd_urls *u;

	if(name == NULL)
		return -1;

	/* Create a new URLs group */
	u = malloc(sizeof(struct httpd_urls));
	if(u == NULL)
		return -1;

	/* Fill new grouo */
	u->name = strdup(name);
	u->urls = urls;
	u->user_data = user_data;

	/* Remove old URLs group */
	httpd_remove_urls(h, name);

	/* Lock URLs list access */
	pthread_mutex_lock(&h->mutex);

	/* Add new group */
	u->next = h->urls;
	h->urls = u;

	/* Unlock URLs list access */
	pthread_mutex_unlock(&h->mutex);

	return 0;
}

static void httpd_free_urls(struct httpd_urls *u)
{
	if(u == NULL)
		return;

	if(u->name != NULL)
		free(u->name);

	free(u);
}

int httpd_remove_urls(struct httpd_handle *h, const char *name)
{
	struct httpd_urls **up, *u;

	if(name == NULL)
		return -1;

	/* Lock URLs list access */
	pthread_mutex_lock(&h->mutex);

	/* Remove client from list */
	up = &h->urls;
	while((*up) != NULL)
	{
		u = *up;
		if(strcmp(u->name, name) == 0)
		{
			*up = u->next;
			httpd_free_urls(u);
			break;
		}
		else
			up = &u->next;
	}

	/* Unlock URLs list access */
	pthread_mutex_unlock(&h->mutex);

	return 0;
}

int httpd_close(struct httpd_handle *h)
{
	struct httpd_urls *u;

	if(h == NULL)
		return 0;

	/* Stop HTTP server */
	if(h->httpd != NULL)
		httpd_stop(h);

	/* Free URLs grouo */
	while(h->urls != NULL)
	{
		u = h->urls;
		h->urls = u->next;
		httpd_free_urls(u);
	}

	/* Free configuration */
	if(h->name != NULL)
		free(h->name);
	if(h->path != NULL)
		free(h->path);
	if(h->password != NULL)
		free(h->password);

	/* Free opaque */
	if(h->opaque != NULL)
		free(h->opaque);

	free(h);

	return 0;
}

/******************************************************************************
 *                              Common functions                              *
 ******************************************************************************/

static int httpd_get_method_code(const char *method)
{
	if(strcmp(method, MHD_HTTP_METHOD_GET) == 0)
		return HTTPD_GET;
	if(strcmp(method, MHD_HTTP_METHOD_PUT) == 0)
		return HTTPD_PUT;
	if(strcmp(method, MHD_HTTP_METHOD_POST) == 0)
		return HTTPD_POST;
	if(strcmp(method, MHD_HTTP_METHOD_DELETE) == 0)
		return HTTPD_DELETE;

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

static int httpd_data_response(struct MHD_Connection *c, int code,
			       unsigned char *buffer, size_t len)
{
	struct MHD_Response *response;
	int ret;

	if(buffer == NULL || len == 0)
		return httpd_response(c, code, "");

	/* Create HTTP response with message */
	response = MHD_create_response_from_data(len, buffer, MHD_YES, MHD_NO);

	/* Queue it */
	ret = MHD_queue_response(c, code, response);

	/* Destroy local response */
	MHD_destroy_response(response);

	return ret;
}

/******************************************************************************
 *                                File/Dir part                               *
 ******************************************************************************/

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
	return fread(buffer, 1, size, fp);
}

static void httpd_file_free_cb(void *user_data)
{
	FILE *fp = (FILE*) user_data;

	/* Close file */
	if(fp != NULL)
		fclose(fp);
}

static int httpd_file_response(struct MHD_Connection *c, const char *web_path,
			       const char *url)
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
	if(web_path == NULL)
		return httpd_response(c, 500, "Web path not configured!");

	/* Create file path */
	path = malloc(strlen(web_path) + strlen(url) + 12);
	if(path == NULL)
		return httpd_response(c, 500, "Internal Error");
	sprintf(path, "%s%s", web_path, url);

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

	/* Check file size */
	if(s.st_size != 0)
	{
		/* Open File */
		fp = fopen(path, "rb");
		if(fp == NULL)
		{
			free(path);
			return httpd_response(c, 404, "File not found");
		}

		/* Create HTTP response with file content */
		response = MHD_create_response_from_callback(s.st_size, 8192,
							   &httpd_file_read_cb,
							   fp,
							   &httpd_file_free_cb);
	}
	else
		response = MHD_create_response_from_data(0, NULL, 0, 0);

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

/******************************************************************************
 *                                JSON Parser                                 *
 ******************************************************************************/

struct json_data {
	struct json_tokener *tokener;
	struct json *object;
};

static void httpd_free_json(struct json_data *json)
{
	if(json != NULL)
	{
		if(json->tokener != NULL)
			json_tokener_free(json->tokener);
		if(json->object != NULL)
			json_free(json->object);
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
		json->object = (struct json *) json_tokener_parse_ex(
							  json->tokener, buffer,
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
 *                          Main HTTP request parser                          *
 ******************************************************************************/

enum {
	HTTPD_URL_NOT_FOUND,
	HTTPD_CONTINUE,
	HTTPD_YES = MHD_YES,
	HTTPD_NO = MHD_NO
};

static int httpd_parse_url(struct MHD_Connection *c, const char *url,
			   int method, const char * upload_data,
			   size_t * upload_data_size, void ** ptr,
			   const char *root_url, const struct url_table *urls,
			   void *user_data)
{
	struct request_data **r_data = (struct request_data **)ptr;
	struct httpd_req req = HTTPD_REQ_INIT;
	struct json_data *j_data = NULL;
	unsigned char *resp = NULL;
	size_t resp_len = 0;
	int code = 0;
	int len;
	int ret;
	int i;

	/* Check URL root */
	len = strlen(root_url);
	if(url == NULL || strncmp(url+1, root_url, len) != 0 ||
	   (*root_url != '\0' && url[len+1] != 0 && url[len+1] != '/'))
		return HTTPD_URL_NOT_FOUND;

	/* Parse all URLs */
	for(i = 0; urls[i].url != NULL; i++)
	{
		if(httpd_strcmp(urls[i].url, url+len+1, !urls[i].extended) == 0)
		{
			/* Verify method */
			if((urls[i].method & method) == 0)
				return httpd_response(c, 406,
						      "Method not acceptable!");

			/* Extract and check ressource name */
			if(urls[i].extended)
			{
				req.resource = url + len + strlen(urls[i].url);
				if(*req.resource++ == '/' && *req.resource == 0)
					return httpd_response(c, 400,
								 "Bad request");
				if(*req.resource == '/')
					req.resource++;
			}
			else
				req.resource = NULL;

			/* Get uploaded data */
			if(method != HTTPD_GET)
			{
				/* POST or PUT */
				if(urls[i].upload == HTTPD_JSON)
				{
					/* Parse JSON */
					ret = httpd_parse_json(r_data,
							      upload_data,
							      upload_data_size);
					if(ret == 1)
						return HTTPD_CONTINUE;
					else if(ret < 0)
						return httpd_response(c, 500,
							     "Internal error!");

					/* Get JSON object */
					j_data = (struct json_data*)
							      ((*r_data)->data);
					req.json = j_data->object;

					/* Check JSON object */
					if(req.json == NULL)
						return httpd_response(c, 400,
								 "Bad request");
				}
				else
				{
					req.data = NULL;
					req.len = 0;
				}
			}

			/* Process URL */
			req.url = url;
			req.method = method;
			code = urls[i].process(user_data, &req, &resp,
					       &resp_len);

			return httpd_data_response(c, code, resp, resp_len);
		}
	}

	return HTTPD_URL_NOT_FOUND;
}

static int httpd_request(void *user_data, struct MHD_Connection *c, 
			 const char *url, const char *method, 
			 const char *version, const char *upload_data,
			 size_t *upload_data_size, void **ptr)
{
	struct httpd_handle *h = (struct httpd_handle*) user_data;
	struct MHD_Response *response;
	struct httpd_urls *u;
	int method_code = 0;
	char *username;
	int ret;

	/* Authentication check */
	if(*ptr == NULL && h->password != NULL)
	{
		/* Get username */
		username = MHD_digest_auth_get_username(c);
		if(username != NULL)
		{
			/* Check password */
			ret = MHD_digest_auth_check(c, h->name, username,
						    h->password, 300);
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
			ret = MHD_queue_auth_fail_response(c, h->name,
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

	/* Lock URLs list access */
	pthread_mutex_lock(&h->mutex);

	/* Parse URL */
	u = h->urls;
	while(u != NULL)
	{
		ret = httpd_parse_url(c, url, method_code, upload_data,
				      upload_data_size, ptr, u->name, u->urls,
				      u->user_data);
		if(ret != HTTPD_URL_NOT_FOUND)
		{
			/* Unlock URLs list access */
			pthread_mutex_unlock(&h->mutex);

			return ret;
		}
		u = u->next;
	}

	/* Unlock URLs list access */
	pthread_mutex_unlock(&h->mutex);

	/* Accept only GET method if not a special URL */
	if(method_code != HTTPD_GET)
		return httpd_response(c, 406, "Method not acceptable!");

	/* Response with the requested file */
	return httpd_file_response(c, h->path, url);
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

