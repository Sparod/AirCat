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

#include "config_file.h"
#include "utils.h"
#include "json.h"

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "httpd.h"

#define OPAQUE "11733b200778ce33060f31c9af70a870ba96ddd4"

/* Maximum time to wait end of connections for URL group remove:
 *   HTTPD_REMOVE_RETRY = number of retry,
 *   HTTPD_REMOVE_WAIT  = time to wait between to retry (in ms).
 * Total time is given by HTTPD_REMOVE_RETRY * HTTPD_REMOVE_WAIT in ms.
 * Default time is 1s.
 */
#define HTTPD_REMOVE_RETRY 100
#define HTTPD_REMOVE_WAIT 10

/* Sessions parameters
 *    HTTPD_SESSION_NAME   = name of cookie which handle session ID,
 *    HTTPD_SESSION_EXPIRE = expiration time for session handling (in s).
 *    HTTPD_SESSION_ABORT  = minimum time life for a session (in s): if no more
 *                           is available for a new session (count >=
 *                           HTTPD_SESSION_MAX), oldest session is removed
 *                           except if its age is under HTTPD_SESSION_ABORT.
 *    HTTPD_SESSION_MAX    = maximum session number handled by server.
 * Default expiration time is 1h.
 * Default minimum time life is 10min.
 */
#define HTTPD_SESSION_NAME "session"
#define HTTPD_SESSION_EXPIRE 3600
#define HTTPD_SESSION_ABORT 600
#define HTTPD_SESSION_MAX 200

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

struct httpd_value {
	char *key;
	char *value;
	struct httpd_value *next;
};

struct httpd_session {
	/* Session ID */
	char id[33];
	/* Session values */
	struct httpd_value *values;
	pthread_mutex_t values_mutex;
	/* Last time of activity on this session */
	time_t time;
	/* Counter for active connection on this session */
	int count;
	/* Next session in list */
	struct httpd_session *next;
};

struct httpd_req_data {
	/* Handle of HTTP Server */
	struct httpd_handle *handle;
	struct MHD_Connection *connection;
	/* Associated session */
	struct httpd_session *session;
	/* JSON uploaded data */
	struct json_tokener *json_tokener;
	struct json *json;
	/* POST uploaded data */
	struct MHD_PostProcessor *post_proc;
	struct httpd_value *post;
};

struct httpd_urls {
	/* Root name of URL group */
	char *name;
	/* URL group */
	void *user_data;
	struct url_table *urls;
	/* Mutex and counter for active connections */
	int abort;
	int count;
	pthread_mutex_t mutex;
	/* Next URL group */
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
	/* Session list */
	struct httpd_session *sessions;
	pthread_mutex_t session_mutex;
	unsigned long session_count;
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
	h->sessions = NULL;
	h->session_count = 0;

	/* Init mutex */
	pthread_mutex_init(&h->mutex, NULL);
	pthread_mutex_init(&h->session_mutex, NULL);

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
			    MHD_OPTION_NOTIFY_COMPLETED, &httpd_completed, h,
			    MHD_OPTION_THREAD_POOL_SIZE, 10,
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

	/* Init mutex */
	pthread_mutex_init(&u->mutex, NULL);
	u->count = 0;
	u->abort = 0;

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
	struct httpd_urls *up, *u = NULL;
	int count = 1;
	int i;

	if(name == NULL)
		return 0;

	/* Lock URLs list access */
	pthread_mutex_lock(&h->mutex);

	/* Find URL group in list */
	for(u = h->urls; u != NULL; u = u->next)
	{
		if(strcmp(u->name, name) == 0)
			break;
	}

	/* Unlock URLs list access */
	pthread_mutex_unlock(&h->mutex);

	/* Check URL group */
	if(u == NULL)
		return 0;

	/* Lock this URL group access */
	pthread_mutex_lock(&u->mutex);

	/* Abort all connections for this URL group */
	u->abort = 1;

	/* Unlock this URL group access */
	pthread_mutex_unlock(&u->mutex);

	/* Wait end of all connections */
	for(i = 0; i < HTTPD_REMOVE_RETRY; i++)
	{
		/* Lock specific URL */
		pthread_mutex_lock(&u->mutex);

		/* Get connection counter */
		count = u->count;

		/* Unlock specific URL */
		pthread_mutex_unlock(&u->mutex);

		/* No more connections */
		if(count == 0)
			break;

		/* Sleep */
		usleep(HTTPD_REMOVE_WAIT * 1000);
	}

	/* Check counter: return failure if time elapsed */
	if(count > 0)
		return -1;

	/* Lock URLs list access */
	pthread_mutex_lock(&h->mutex);

	/* Remove URL group from list */
	for(up = h->urls; up != NULL; up = up->next)
	{
		if(up->next == u)
			break;
	}
	if(up != NULL)
		up->next = u->next;
	else if(h->urls == u)
		h->urls = u->next;
	httpd_free_urls(u);

	/* Unlock URLs list access */
	pthread_mutex_unlock(&h->mutex);

	return 0;
}

int httpd_close(struct httpd_handle *h)
{
	struct httpd_session *s;
	struct httpd_urls *u;

	if(h == NULL)
		return 0;

	/* Stop HTTP server */
	if(h->httpd != NULL)
		httpd_stop(h);

	/* Free session list */
	while(h->sessions != NULL)
	{
		s = h->sessions;
		h->sessions = s->next;
		free(s);
	}

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

/******************************************************************************
 *                              Session handling                              *
 ******************************************************************************/

static void httpd_free_session(struct httpd_session *s)
{
	struct httpd_value *v;

	if(s == NULL)
		return;

	/* Free values */
	while(s->values != NULL)
	{
		v = s->values;
		s->values = v->next;

		/* Free strings */
		if(v->key != NULL)
			free(v->key);
		if(v->value != NULL)
			free(v->value);
		free(v);
	}

	/* Free value */
	free(s);
}

static void httpd_expire_session(struct httpd_handle *h, int force)
{
	struct httpd_session *s, **sp, **op = NULL;
	time_t now, oldest;

	/* Get current time */
	now = time(NULL);
	oldest = now;

	/* Lock session list access */
	pthread_mutex_lock(&h->session_mutex);

	/* Find expired session */
	sp = &h->sessions;
	while((*sp) != NULL)
	{
		s = *sp;
		if(s->count == 0 && s->time + HTTPD_SESSION_EXPIRE < now)
		{
			/* Free session */
			*sp = s->next;
			httpd_free_session(s);

			/* Update session count */
			h->session_count--;
		}
		else if(s->count == 0 && s->time + HTTPD_SESSION_ABORT < oldest)
		{
			/* Save ref and update min time */
			op = sp;
			oldest = s->time;
		}
		else
			sp = &s->next;
	}

	/* Free oldest session if found */
	if(force && h->session_count >= HTTPD_SESSION_MAX && op != NULL)
	{
		/* Free session */
		s = *op;
		*op = s->next;
		httpd_free_session(s);

		/* Update session count */
		h->session_count--;
	}

	/* Unlock session list access */
	pthread_mutex_unlock(&h->session_mutex);
}

static void httpd_release_session(struct httpd_handle *h,
				  struct httpd_session *s)
{
	/* Lock session list access */
	pthread_mutex_lock(&h->session_mutex);

	/* Decrement active connections */
	s->count--;

	/* Unlock session list access */
	pthread_mutex_unlock(&h->session_mutex);
}

static struct httpd_session *httpd_find_session(struct httpd_handle *h,
						const char *id)
{
	struct httpd_session *s;
	time_t now;

	/* Check id */
	if(id == NULL)
		return NULL;

	/* Get current time */
	now = time(NULL);

	/* Lock session list access */
	pthread_mutex_lock(&h->session_mutex);

	/* Find session ID in list */
	for(s = h->sessions; s != NULL; s = s->next)
	{
		if(strcmp(s->id, id) == 0 &&
		   s->time + HTTPD_SESSION_EXPIRE > now)
		{
			/* Update time */
			s->time = now;
				/* Increment active connection */
			s->count++;

			break;
		}
	}

	/* Unlock session list access */
	pthread_mutex_unlock(&h->session_mutex);

	return s;
}

static struct httpd_session *httpd_new_session(struct httpd_handle *h)
{
	struct httpd_session *s;
	int count = 0;
	char *str;

	/* Process expired sessions and force free if no more space */
	httpd_expire_session(h, 1);

	/* Lock session list access */
	pthread_mutex_lock(&h->session_mutex);

	/* Get session count */
	count = h->session_count;

	/* Unlock session list access */
	pthread_mutex_unlock(&h->session_mutex);

	/* Check session count */
	if(count >= HTTPD_SESSION_MAX)
		return NULL;

	/* Create a new session */
	s = malloc(sizeof(struct httpd_session));
	if(s == NULL)
		return NULL;

	/* Generate a random ID */
	str = random_string(32);
	strcpy(s->id, str);
	free(str);

	/* Set time and active counter */
	s->time = time(NULL);
	s->count = 1;

	/* Set values session */
	s->values = NULL;
	pthread_mutex_init(&s->values_mutex, NULL);

	/* Lock session list access */
	pthread_mutex_lock(&h->session_mutex);

	/* Add to list */
	s->next = h->sessions;
	h->sessions = s;

	/* Update session count */
	h->session_count++;

	/* Unlock session list access */
	pthread_mutex_unlock(&h->session_mutex);

	return s;
}

static int httpd_get_session(void *user_data, enum MHD_ValueKind kind,
			     const char *key, const char *value)
{
	struct httpd_req_data *req = (struct httpd_req_data *) user_data;

	/* Check key */
	if(strcmp(key, HTTPD_SESSION_NAME) != 0)
		return MHD_YES;

	/* Find the session */
	req->session = httpd_find_session(req->handle, value);
	if(req->session == NULL)
		return MHD_YES;

	/* A session has been found: abort parsing */
	return MHD_YES;
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
 *                            Uploaded data Parser                            *
 ******************************************************************************/

static int httpd_parse_json(struct httpd_req_data *req, const char *buffer,
			    size_t *len)
{
	/* Allocate tokener */
	if(req->json_tokener == NULL)
	{
		/* Create a new JSON tokener */
		req->json_tokener = json_tokener_new();
		if(req->json_tokener == NULL)
			return -1;

		/* Continue */
		return 1;
	}

	/* Some data are still available: parse it */
	if(*len != 0)
	{
		/* Append buffer and parse it */
		req->json = (struct json *) json_tokener_parse_ex(
							      req->json_tokener,
							      buffer, *len);
		*len = 0;

		/* Continue */
		return 1;
	}

	/* End of stream */
	if(req->json_tokener != NULL)
	{
		json_tokener_free(req->json_tokener);
		req->json_tokener = NULL;
	}

	return 0;
}

static int httpd_post_iterator(void *user_data, enum MHD_ValueKind kind,
				const char *key, const char *filename,
				const char *content_type,
				const char *transfer_encoding, const char *data,
				uint64_t off, size_t size)
{
	struct httpd_value **post = (struct httpd_value **) user_data;
	struct httpd_value *v;

	/* Check key and content type (accept only string */
	if(key == NULL || content_type != NULL)
		return MHD_YES;

	/* Do not support yet long strings */
	if(off > 0)
		return MHD_YES;

	/* Allocate new value */
	v = malloc(sizeof(struct httpd_value));
	if(v == NULL)
		return MHD_YES;

	/* Fill the value */
	v->key = strdup(key);
	v->value = strndup(data, size);

	/* Add to value list */
	v->next = *post;
	*post = v;

	return MHD_YES;
}

static int httpd_parse_post(struct httpd_req_data *req, const char *buffer,
			    size_t *len)
{
	/* Allocate POST processor */
	if(req->post_proc == NULL)
	{
		/* Create a new POST processor */
		req->post_proc = MHD_create_post_processor(req->connection,
							   512,
							   &httpd_post_iterator,
							   &req->post);
		if(req->post_proc == NULL)
			return -1;

		/* Continue */
		return 1;
	}

	/* Some data are still available: parse it */
	if(*len != 0)
	{
		/* Contine parsing */
		MHD_post_process(req->post_proc, buffer, *len);
		*len = 0;

		/* Continue */
		return 1;
	}

	/* End of stream */
	if(req->post_proc != NULL)
	{
		MHD_destroy_post_processor(req->post_proc);
		req->post_proc = NULL;
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

struct url_table *httpd_find_url(const char *url, const char *root_url,
				 struct url_table *urls)
{
	int len;
	int i;

	/* Check URL root */
	len = strlen(root_url);
	if(url == NULL || strncmp(url+1, root_url, len) != 0 ||
	   (*root_url != '\0' && url[len+1] != 0 && url[len+1] != '/'))
		return NULL;

	/* Parse all URLs */
	for(i = 0; urls[i].url != NULL; i++)
	{
		if(httpd_strcmp(urls[i].url, url+len+1, !urls[i].extended) == 0)
		{
			return &urls[i];
		}
	}

	return NULL;
}

static int httpd_process_url(struct MHD_Connection *c, const char *url,
			     int method, const char *root_url, 
			     const struct url_table *u, void *user_data,
			     const char *upload_data, size_t *upload_data_size,
			     struct httpd_req_data *r_data)
{
	struct httpd_req req = HTTPD_REQ_INIT;
	struct MHD_Response *response;
	unsigned char *resp = NULL;
	const char *resource;
	size_t resp_len = 0;
	char str[256];
	int code = 0;
	int ret;

	/* Check ressource name*/
	resource = url + strlen(root_url) + strlen(u->url);
	if(u->extended)
	{
		/* Get resource */
		if(*resource++ == '/' && *resource == 0)
			return httpd_response(c, 400, "Bad request");
		if(*resource == '/')
			resource++;
	}

	/* Get uploaded data */
	if(method != HTTPD_GET)
	{
		/* POST or PUT */
		if(u->upload == HTTPD_JSON)
		{
			/* Parse JSON */
			ret = httpd_parse_json(r_data, upload_data,
					       upload_data_size);
			if(ret == 1)
				return HTTPD_CONTINUE;
			else if(ret < 0)
				return httpd_response(c, 500,
						      "Internal error!");

			/* Get JSON object */
			req.json = r_data->json;

			/* Check JSON object */
			if(req.json == NULL)
				return httpd_response(c, 400, "Bad request");
		}
		else if(u->upload == HTTPD_POST)
		{
			/* Parse POST */
			ret = httpd_parse_post(r_data, upload_data,
					       upload_data_size);
			if(ret == 1)
				return HTTPD_CONTINUE;
			else if(ret < 0)
				return httpd_response(c, 500,
						      "Internal error!");
		}
		else
		{
			/* Get raw data */
			req.data = NULL;
			req.len = 0;
		}
	}

	/* Create new session */
	if(r_data != NULL && r_data->session == NULL)
	{
		/* Create a new session if no existing has been found */
		r_data->session = httpd_new_session(r_data->handle);
	}

	/* Fill request structure */
	req.url = url;
	req.method = method;
	req.resource = resource;
	req.priv_data = r_data;

	/* Process URL */
	code = u->process(user_data, &req, &resp, &resp_len);

	/* Create HTTP response with message */
	if(resp == NULL || resp_len == 0)
		response = MHD_create_response_from_data(0, "",
							 MHD_NO, MHD_NO);
	else
		response = MHD_create_response_from_data(resp_len, resp,
							 MHD_YES, MHD_NO);

	/* Add session in cookie header. "path=/" is appended to string
	 * to apply this cookie for all the domain.
	 */
	if(r_data->session != NULL)
	{
		snprintf(str, sizeof(str), "%s=%s; path=/", HTTPD_SESSION_NAME,
			 r_data->session->id);
		MHD_add_response_header(response, MHD_HTTP_HEADER_SET_COOKIE,
					str);
	}

	/* Queue it */
	ret = MHD_queue_response(c, code, response);

	/* Destroy local response */
	MHD_destroy_response(response);

	return ret;
}

static int httpd_auth_by_http(struct httpd_handle *h, struct MHD_Connection *c)
{
	struct MHD_Response *response;
	char *username;
	int ret;

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

	return HTTPD_CONTINUE;
}

static int httpd_request(void *user_data, struct MHD_Connection *c, 
			 const char *url, const char *method, 
			 const char *version, const char *upload_data,
			 size_t *upload_data_size, void **ptr)
{
	struct httpd_handle *h = (struct httpd_handle*) user_data;
	struct httpd_urls *current_urls = NULL;
	struct url_table *current_url = NULL;
	struct httpd_req_data *req = NULL;
	int method_code = 0;
	int ret;

#if (MHD_VERSION >= 0x00093500) && (MHD_VERSION < 0x00093700)
	/* Cause to a regression, version 9.35 to 9.36 included don't unescape
	 * URL anymore. So URL needs to be unescaped here.
	 */
	char buf[3] = { 0, 0, 0 };
	unsigned long n;
	char *u = url;
	char *w = url;
	char *end;

	/* Process all URL */
	while(*u != '\0')
	{
		/* A special character to unescape */
		if(*u == '%')
		{
			/* End of string */
			if(u[1] == '\0' || u[2] == '\0')
			{
				*w = '\0';
				break;
			}

			/* Get value */
			buf[0] = u[1];
			buf[1] = u[2];
			n = strtoul(buf, &end, 16);
			if(*end == '\0')
			{
				*w = (char) ((unsigned char) n);
				w++;
				u += 3;
				continue;
			}
		}

		/* Increment */
		*w = *u;
		u++;
		w++;
	}

	/* Terminate string */
	*w = '\0';
#endif

	/* HTTP authentication check */
	if(*ptr == NULL && h->password != NULL)
	{
		ret = httpd_auth_by_http(h, c);
		if(ret != HTTPD_CONTINUE)
			return ret;
	}

	/* Get methode code */
	method_code = httpd_get_method_code(method);
	if(method_code == 0)
		return httpd_response(c, 405, "Method not allowed!");

	/* Allocate data handlers if first call */
	if(*ptr == NULL)
	{
		/* Allocate request data */
		*ptr = calloc(1, sizeof(struct httpd_req_data));
	}
	req = *ptr;

	/* Get session */
	if(req != NULL && req->session == NULL)
	{
		/* Set handle in request data */
		req->handle = h;
		req->connection = c;

		/* Process expired sessions */
		httpd_expire_session(h, 0);

		/* Get Session cookie from connection */
		MHD_get_connection_values(c, MHD_COOKIE_KIND,
					  &httpd_get_session, req);
	}

	/* Lock URLs list access */
	pthread_mutex_lock(&h->mutex);

	/* Find URL in list */
	current_urls = h->urls;
	while(current_urls != NULL)
	{
		current_url = httpd_find_url(url, current_urls->name,
					     current_urls->urls);
		if(current_url != NULL)
			break;

		current_urls = current_urls->next;
	}

	/* No URL found */
	if(current_urls == NULL || current_url == NULL)
	{
		/* Unlock URLs list access */
		pthread_mutex_unlock(&h->mutex);

		goto process_file;
	}

	/* Lock specific URL */
	pthread_mutex_lock(&current_urls->mutex);

	/* Check abort signal */
	if(current_urls->abort)
	{
		/* Unlock specific URL */
		pthread_mutex_unlock(&current_urls->mutex);

		/* Unlock URLs list access */
		pthread_mutex_unlock(&h->mutex);

		goto process_file;
	}

	/* Increment connection counter */
	current_urls->count++;

	/* Unlock specific URL */
	pthread_mutex_unlock(&current_urls->mutex);

	/* Unlock URLs list access */
	pthread_mutex_unlock(&h->mutex);

	/* Check method */
	if((current_url->method & method_code) == 0)
		return httpd_response(c, 406, "Method not acceptable!");

	/* Process URL */
	ret =  httpd_process_url(c, url, method_code, current_urls->name,
				 current_url, current_urls->user_data,
				 upload_data, upload_data_size,
				 *ptr);

	/* Lock specific URL */
	pthread_mutex_lock(&current_urls->mutex);

	/* Decrement connection counter */
	current_urls->count--;

	/* Unlock specific URL */
	pthread_mutex_unlock(&current_urls->mutex);

	return ret;

process_file:
	/* Accept only GET method if not a special URL */
	if(method_code != HTTPD_GET)
		return httpd_response(c, 406, "Method not acceptable!");

	/* Response with the requested file */
	return httpd_file_response(c, h->path, url);
}

static void httpd_completed(void *user_data, struct MHD_Connection *c,
			    void **ptr, enum MHD_RequestTerminationCode toe)
{
	struct httpd_req_data *req = *((struct httpd_req_data **)ptr);
	struct httpd_handle *h = (struct httpd_handle*) user_data;
	struct httpd_value *v;

	if(req == NULL)
		return;

	/* Free session */
	if(req->session != NULL)
		httpd_release_session(h, req->session);

	/* Free JSON data */
	if(req->json_tokener != NULL)
		json_tokener_free(req->json_tokener);
	if(req->json != NULL)
		json_free(req->json);

	/* Free POST data */
	if(req->post_proc != NULL)
		MHD_destroy_post_processor(req->post_proc);
	while(req->post != NULL)
	{
		v = req->post;
		req->post = v->next;

		/* Free value */
		if(v->key != NULL)
			free(v->key);
		if(v->value != NULL)
			free(v->value);
		free(v);
	}

	/* Free request data */
	free(req);
	*ptr = NULL;
}

/******************************************************************************
 *                        Callback specific functions                         *
 ******************************************************************************/

const char *httpd_get_query(struct httpd_req *req, const char *key)
{
	struct httpd_req_data *r;

	if(req == NULL)
		return NULL;

	/* Get req_data */
	r = (struct httpd_req_data *) req->priv_data;
	if(r == NULL)
		return NULL;

	/* Return query value */
	return MHD_lookup_connection_value(r->connection, MHD_GET_ARGUMENT_KIND,
					   key);
}

int httpd_set_session_value(struct httpd_req *req, const char *key,
			    const char *value)
{
	struct httpd_value **vp, *v = NULL;
	struct httpd_req_data *r;

	if(req == NULL || req->priv_data == NULL || key == NULL)
		return -1;

	/* Get req_data */
	r = (struct httpd_req_data *) req->priv_data;
	if(r == NULL || r->session == NULL)
		return -1;

	/* Lock session values access */
	pthread_mutex_lock(&r->session->values_mutex);

	/* Find value */
	vp = &r->session->values;
	while((*vp) != NULL)
	{
		v = *vp;
		if(strcmp(v->key, key) == 0)
			break;
		else
		{
			vp = &v->next;
			v = NULL;
		}
	}

	/* Remove value from list */
	if(value == NULL)
	{
		/* No value in list */
		if(v == NULL)
			goto end;

		/* Free both strings */
		if(v->value != NULL)
			free(v->value);
		if(v->key != NULL)
			free(v->key);

		/* Update list and free */
		*vp = v->next;
		free(v);

		/* Exit */
		goto end;
	}

	/* Check entry */
	if(v != NULL)
	{
		/* Reset entry */
		if(v->value != NULL)
			free(v->value);
		v->value = NULL;
	}
	else
	{
		/* Create new entry */
		v = calloc(1, sizeof(struct httpd_value));
		if(v == NULL)
		{
			/* Unlock session values access */
			pthread_mutex_unlock(&r->session->values_mutex);

			return -1;
		}

		/* Copy key string */
		v->key = strdup(key);

		/* Add to value list */
		v->next = r->session->values;
		r->session->values = v;
	}

	/* Copy new string */
	if(value != NULL)
		v->value = strdup(value);

end:
	/* Unlock session values access */
	pthread_mutex_unlock(&r->session->values_mutex);

	return 0;
}

char *httpd_get_session_value(struct httpd_req *req, const char *key)
{
	struct httpd_value *v;
	struct httpd_req_data *r;
	char *str = NULL;

	if(req == NULL || req->priv_data == NULL || key == NULL)
		return NULL;

	/* Get req_data */
	r = (struct httpd_req_data *) req->priv_data;
	if(r == NULL || r->session == NULL)
		return NULL;

	/* Lock session values access */
	pthread_mutex_lock(&r->session->values_mutex);

	/* Find value */
	for(v = r->session->values; v != NULL; v = v->next)
	{
		if(strcmp(v->key, key) == 0)
		{
			/* Copy string */
			if(v->value != NULL)
				str = strdup(v->value);
			break;
		}
	}

	/* Unlock session values access */
	pthread_mutex_unlock(&r->session->values_mutex);

	return str;
}

const char *httpd_get_post_value(struct httpd_req *req, const char *key)
{
	struct httpd_req_data *r;
	struct httpd_value *v;

	if(req == NULL || req->priv_data == NULL || key == NULL)
		return NULL;

	/* Get req_data */
	r = (struct httpd_req_data *) req->priv_data;
	if(r == NULL)
		return NULL;

	/* Find value */
	for(v = r->post; v != NULL; v = v->next)
	{
		if(strcmp(v->key, key) == 0)
			return v->value;
	}

	return NULL;
}

