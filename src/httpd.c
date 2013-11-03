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

#include <microhttpd.h>

#include "config_file.h"
#include "airtunes.h"

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "httpd.h"

#define OPAQUE "11733b200778ce33060f31c9af70a870ba96ddd4"

struct httpd_handle{
	/* MicroHTTPD handle */
	struct MHD_Daemon *httpd;
	char *realm;
	char *opaque;
	/* Airtunes client */
	struct airtunes_handle *airtunes;
};

static int httpd_request(void * user_data,
			 struct MHD_Connection *c,
			 const char * url,
			 const char * method,
			 const char * version,
			 const char * upload_data,
			 size_t * upload_data_size,
			 void ** ptr);

int httpd_open(struct httpd_handle **handle, char *name, struct airtunes_handle *airtunes)
{
	struct httpd_handle *h;

	/* Allocate structure */
	*handle = malloc(sizeof(struct httpd_handle));
	if(*handle == NULL)
		return -1;
	h = *handle;

	/* Init structure */
	if(name != NULL)
		h->realm = strdup(name);
	else
		h->realm = strdup("AirCat");
	h->opaque = strdup(OPAQUE);
	h->airtunes = airtunes;
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
	h->httpd = MHD_start_daemon(MHD_USE_SELECT_INTERNALLY, config.port, NULL, NULL, &httpd_request, h, MHD_OPTION_END);
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

	free(h);

	return 0;
}

static int httpd_request(void * user_data,
			 struct MHD_Connection *c,
			 const char * url,
			 const char * method,
			 const char * version,
			 const char * upload_data,
			 size_t * upload_data_size,
			 void ** ptr)
{
	struct httpd_handle *h = (struct httpd_handle*) user_data;
	struct MHD_Response *response;
	char *username;
	int ret;

	/* Authentication check */
	if(config.password != NULL)
	{
		/* Get username */
		username = MHD_digest_auth_get_username(c);
		if(username != NULL)
		{
			/* Check password */
			ret = MHD_digest_auth_check(c, h->realm, username, config.password, 300);
			free(username);
		}
		else
			ret = MHD_NO;

		/* Bad authentication */
		if((ret == MHD_INVALID_NONCE) || (ret == MHD_NO))
		{
			response = MHD_create_response_from_buffer(2, "KO", MHD_RESPMEM_PERSISTENT);
			ret = MHD_queue_auth_fail_response(c, h->realm, h->opaque, response, (ret == MHD_INVALID_NONCE) ? MHD_YES : MHD_NO);
			MHD_destroy_response(response);
			return ret;
		}
	}

	/* Accept only GET method if not a special URL */
	if (strcmp(method, "GET") != 0)
		return MHD_NO;

	/* Response with the requested file */
	response = MHD_create_response_from_data(2, "OK", MHD_NO, MHD_NO);
	ret = MHD_queue_response(c, MHD_HTTP_OK, response);
	MHD_destroy_response(response);

	return ret;
}
