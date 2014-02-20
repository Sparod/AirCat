/*
 * airtunes.c - A RAOP/Airplay server
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
#include <pthread.h>

#include <openssl/bio.h>
#include <openssl/ssl.h>
#include <openssl/err.h>

#include "avahi.h"
#include "rtsp.h"
#include "raop.h"
#include "sdp.h"
#include "alsa.h"

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "airtunes.h"

#define BUFFER_SIZE 512

#define AIRPORT_PRIVATE_KEY \
"-----BEGIN RSA PRIVATE KEY-----\n" \
"MIIEpQIBAAKCAQEA59dE8qLieItsH1WgjrcFRKj6eUWqi+bGLOX1HL3U3GhC/j0Qg90u3sG/1CUt\n" \
"wC5vOYvfDmFI6oSFXi5ELabWJmT2dKHzBJKa3k9ok+8t9ucRqMd6DZHJ2YCCLlDRKSKv6kDqnw4U\n" \
"wPdpOMXziC/AMj3Z/lUVX1G7WSHCAWKf1zNS1eLvqr+boEjXuBOitnZ/bDzPHrTOZz0Dew0uowxf\n" \
"/+sG+NCK3eQJVxqcaJ/vEHKIVd2M+5qL71yJQ+87X6oV3eaYvt3zWZYD6z5vYTcrtij2VZ9Zmni/\n" \
"UAaHqn9JdsBWLUEpVviYnhimNVvYFZeCXg/IdTQ+x4IRdiXNv5hEewIDAQABAoIBAQDl8Axy9XfW\n" \
"BLmkzkEiqoSwF0PsmVrPzH9KsnwLGH+QZlvjWd8SWYGN7u1507HvhF5N3drJoVU3O14nDY4TFQAa\n" \
"LlJ9VM35AApXaLyY1ERrN7u9ALKd2LUwYhM7Km539O4yUFYikE2nIPscEsA5ltpxOgUGCY7b7ez5\n" \
"NtD6nL1ZKauw7aNXmVAvmJTcuPxWmoktF3gDJKK2wxZuNGcJE0uFQEG4Z3BrWP7yoNuSK3dii2jm\n" \
"lpPHr0O/KnPQtzI3eguhe0TwUem/eYSdyzMyVx/YpwkzwtYL3sR5k0o9rKQLtvLzfAqdBxBurciz\n" \
"aaA/L0HIgAmOit1GJA2saMxTVPNhAoGBAPfgv1oeZxgxmotiCcMXFEQEWflzhWYTsXrhUIuz5jFu\n" \
"a39GLS99ZEErhLdrwj8rDDViRVJ5skOp9zFvlYAHs0xh92ji1E7V/ysnKBfsMrPkk5KSKPrnjndM\n" \
"oPdevWnVkgJ5jxFuNgxkOLMuG9i53B4yMvDTCRiIPMQ++N2iLDaRAoGBAO9v//mU8eVkQaoANf0Z\n" \
"oMjW8CN4xwWA2cSEIHkd9AfFkftuv8oyLDCG3ZAf0vrhrrtkrfa7ef+AUb69DNggq4mHQAYBp7L+\n" \
"k5DKzJrKuO0r+R0YbY9pZD1+/g9dVt91d6LQNepUE/yY2PP5CNoFmjedpLHMOPFdVgqDzDFxU8hL\n" \
"AoGBANDrr7xAJbqBjHVwIzQ4To9pb4BNeqDndk5Qe7fT3+/H1njGaC0/rXE0Qb7q5ySgnsCb3DvA\n" \
"cJyRM9SJ7OKlGt0FMSdJD5KG0XPIpAVNwgpXXH5MDJg09KHeh0kXo+QA6viFBi21y340NonnEfdf\n" \
"54PX4ZGS/Xac1UK+pLkBB+zRAoGAf0AY3H3qKS2lMEI4bzEFoHeK3G895pDaK3TFBVmD7fV0Zhov\n" \
"17fegFPMwOII8MisYm9ZfT2Z0s5Ro3s5rkt+nvLAdfC/PYPKzTLalpGSwomSNYJcB9HNMlmhkGzc\n" \
"1JnLYT4iyUyx6pcZBmCd8bD0iwY/FzcgNDaUmbX9+XDvRA0CgYEAkE7pIPlE71qvfJQgoA9em0gI\n" \
"LAuE4Pu13aKiJnfft7hIjbK+5kyb3TysZvoyDnb3HOKvInK7vXbKuU4ISgxB2bB3HcYzQMGsz1qJ\n" \
"2gG0N5hvJpzwwhbhXqFKA4zaaSrw622wDniAK5MlIE0tIAKKP4yxNGjoD2QYjhBGuhvkWKaXTyY=\n" \
"-----END RSA PRIVATE KEY-----"

static RSA *rsa = NULL;

struct airtunes_server_data {
	unsigned char hw_addr[6];
	char *name;
	char *password;
};

struct airtunes_client_data {
	/* Audio output handlers */
	struct raop_handle *raop;
	struct alsa_handle *alsa;
	/* AES key and IV */
	unsigned char *aes_key;
	unsigned char aes_iv[16];
	/* Format */
	int codec;
	char *format;
	/* RAOP transport protocol */
	int transport;
	/* RAOP server port */
	unsigned int port;
	/* RTCP ports */
	unsigned int control_port;
	unsigned int timing_port;
};

struct airtunes_handle{
	/* Avahi client */
	struct avahi_handle *avahi;
	int local_avahi;
	/* Airtunes */
	char *name;
	unsigned int port;
	char *password;
	int status;
	int reload;
	/* RTSP server */
	struct rtsp_handle *rtsp;
	struct airtunes_server_data rtsp_data;
	/* Thread */
	pthread_t thread;
};

/* RTSP Callback */
static int airtunes_request_callback(struct rtsp_client *c, int request, 
				     const char *url, void *user_data);
static int airtunes_read_callback(struct rtsp_client *c, unsigned char *buffer,
				  size_t size, int end_of_stream,
				  void *user_data);
static int airtunes_close_callback(struct rtsp_client *c, void *user_data);

int airtunes_open(struct airtunes_handle **handle, struct avahi_handle *a)
{
	char buf[6] = {0x00, 0x51, 0x52, 0x53, 0x54, 0x55};
	struct airtunes_handle *h;

	/* Allocate structure */
	*handle = malloc(sizeof(struct airtunes_handle));
	if(*handle == NULL)
		return -1;
	h = *handle;

	/* Init structure */
	h->avahi = a;
	h->local_avahi = 0;
	h->status = AIRTUNES_STOPPED;
	h->reload = 0;
	h->name = strdup("AirCat");
	h->port = 5000;
	h->password = NULL;

	/* Allocate a local avahi client if no avahi has been passed */
	if(h->avahi == NULL)
	{
		if(avahi_open(&h->avahi) < 0)
			return -1;
		h->local_avahi = 1;
	}

	/* Load RSA private key */
	if(rsa == NULL)
	{
		BIO *tBio = BIO_new_mem_buf(AIRPORT_PRIVATE_KEY, -1);
		rsa = PEM_read_bio_RSAPrivateKey(tBio, NULL, NULL, NULL);
		BIO_free(tBio);
	}

	/* Get HW MAC */
	memcpy(h->rtsp_data.hw_addr, buf, 6);
	h->rtsp_data.name = h->name;
	h->rtsp_data.password = h->password;

	return 0;
}

void airtunes_set_name(struct airtunes_handle *h, const char *name)
{
	if(h == NULL || name == NULL)
		return;

	if(h->name != NULL)
		free(h->name);

	h->name = strdup(name);
	h->rtsp_data.name = h->name;
}

void airtunes_set_port(struct airtunes_handle *h, unsigned int port)
{
	if(h == NULL || h->port == port)
		return;

	h->port = port;
}

void airtunes_set_password(struct airtunes_handle *h, const char *password)
{
	if(h == NULL || (h->password != NULL &&
	   strcmp(h->password, password) == 0))
		return;

	if(h->password != NULL)
		free(h->password);

	if(password != NULL)
		h->password = strdup(password);
	else
		h->password = NULL;

	h->rtsp_data.password = h->password;
}

void *airtunes_thread(void *user_data)
{
	struct airtunes_handle *h = (struct airtunes_handle*) user_data;
	char name[256];
	int port;
	int i;

	/* Open RTSP server */
	port = h->port;
	if(rtsp_open(&h->rtsp, port, 2, &airtunes_request_callback, 
		     &airtunes_read_callback, &airtunes_close_callback,
		     &h->rtsp_data) < 0)
	{
		h->status = AIRTUNES_STOPPED;
		return NULL;
	}

	/* Register the service with Avahi */
	for(i = 0; i < 6; i++)
		snprintf(name+(2*i), 256, "%02x", h->rtsp_data.hw_addr[i]);
	snprintf(name+12, 244, "@%s", h->name);
	avahi_add_service(h->avahi, name, "_raop._tcp", h->port, "tp=TCP,UDP",
			  "sm=false", "sv=false", "ek=1", "et=0,1", "cn=1",
			  "ch=2", "ss=16", "sr=44100", "pw=false", "vn=3",
			  "md=0,1,2", "txtvers=1", NULL);

	/* Change status of airtunes */
	if(h->status != AIRTUNES_STOPPING)
	{
		h->status = AIRTUNES_RUNNING;
	}

	/* Run RTSP server loop */
	while(h->status != AIRTUNES_STOPPING && h->status != AIRTUNES_STOPPED)
	{
		if(rtsp_loop(h->rtsp, 1000) != 0)
			break;
		if(h->local_avahi)
			avahi_loop(h->avahi, 10);
	}
	h->status = AIRTUNES_STOPPING;

	/* Remove the service */
	avahi_remove_service(h->avahi, name, port);
	if(h->local_avahi)
		avahi_loop(h->avahi, 10);

	/* Close RTSP server */
	rtsp_close(h->rtsp);

	/* Change status */
	h->status = AIRTUNES_STOPPED;

	return NULL;
}

int airtunes_start(struct airtunes_handle *h)
{
	if(h->status == AIRTUNES_RUNNING)
		return 0;

	/* Start server */
	if(pthread_create(&h->thread, NULL, airtunes_thread, h) != 0)
	{
		h->status = AIRTUNES_STOPPED;
		return -1;
	}
	h->status = AIRTUNES_STARTING;

	return 0;
}

int airtunes_stop(struct airtunes_handle *h)
{
	if(h->status == AIRTUNES_STOPPING || h->status == AIRTUNES_STOPPED)
		return 0;

	/* Stop server */
	h->status = AIRTUNES_STOPPING;

	return 0;
}

int airtunes_status(struct airtunes_handle *h)
{
	return h->status;
}

int airtunes_close(struct airtunes_handle *h)
{
	if(h == NULL)
		return 0;

	/* Stop server */
	airtunes_stop(h);

	/* Wait end of thread */
	pthread_join(h->thread, NULL);

	/* Close Avahi client if local */
	if(h->local_avahi)
		avahi_close(h->avahi);

	/* Free strings */
	if(h->name != NULL)
		free(h->name);
	if(h->password != NULL)
		free(h->password);

	/* Free structure */
	free(h);

	return 0;
}

static int airtunes_do_apple_response(struct rtsp_client *c, 
				      unsigned char *hw_addr)
{
	char *challenge;
	unsigned char response_tmp[38];
	char *rsa_response;
	char *response;
	int pos = 0;
	int len;

	challenge = rtsp_get_header(c, "Apple-Challenge", 1);

	if(challenge != NULL)
	{
		/* Decode base64 string */
		rtsp_decode_base64(challenge);
		len = strlen(challenge);

		/* Make the response */
		memcpy(response_tmp, challenge, 16);
		pos += 16;
		memcpy(response_tmp+pos, rtsp_get_server_ip(c), 4);
		pos += 4;
		memcpy(response_tmp+pos, hw_addr, 6);
		pos += 6;

		/* Pad to 32 chars */
		len = 32 - pos;
		if(len > 0)
		{
			memset(response_tmp+pos, 0, len);
			pos += len;
		}

		/* Encrypt with RSA */
		/* Get size and alloc buffer */
		len = RSA_size(rsa);
		rsa_response = malloc(len);
		/* Encrypt */
		RSA_private_encrypt(pos, response_tmp,
				    (unsigned char*)rsa_response, rsa,
				    RSA_PKCS1_PADDING);

		/* Encode base64 */
		response = rtsp_encode_base64(rsa_response, len);
		len = strlen(response);

		/* Change '=' if it exists by '\0' */
		if(response[len-1] == '=')
			response[len-1] = '\0';

		/* Add Apple-response to RTSP response */
		rtsp_add_response(c, "Apple-Response", response);

		/* Free variables */
		free(response);
		free(rsa_response);

		return 0;
	}

	return -1;
}

#define RESPONSE_BEGIN(c, s) rtsp_create_response(c, 200, "OK"); \
		airtunes_do_apple_response(c, s); \
		rtsp_add_response(c, "Server", "AirCat/1.0"); \
		rtsp_add_response(c, "CSeq", rtsp_get_header(c, "CSeq", 1));

static int airtunes_request_callback(struct rtsp_client *c, int request, const char *url, void *user_data)
{
	struct airtunes_server_data *sdata = (struct airtunes_server_data*) user_data;
	struct airtunes_client_data *cdata = (struct airtunes_client_data*) rtsp_get_user_data(c);
	struct raop_attr attr;
	char buffer[BUFFER_SIZE];
	char *username;
	char *str, *p;

	/* Allocate structure to handle session */
	if(cdata == NULL)
	{
		cdata = malloc(sizeof(struct airtunes_client_data));
		memset(cdata, 0, sizeof(struct airtunes_client_data));
		rtsp_set_user_data(c, cdata);
	}

	/* Verify password */
	if(sdata->password != NULL)
	{
		username = rtsp_digest_auth_get_username(c);
		if(username == NULL || rtsp_digest_auth_check(c, username, sdata->password, sdata->name) != 0)
		{
			rtsp_create_digest_auth_response(c, sdata->name, "", 0);
			airtunes_do_apple_response(c, sdata->hw_addr);
			rtsp_add_response(c, "Server", "AirCat/1.0");
			rtsp_add_response(c, "CSeq", rtsp_get_header(c, "CSeq", 1));
			return 0;
		}
	}

	switch(request)
	{
		case RTSP_OPTIONS:
			RESPONSE_BEGIN(c, sdata->hw_addr);
			rtsp_add_response(c, "Public", "ANNOUNCE, SETUP, RECORD, PAUSE, FLUSH, TEARDOWN, OPTIONS, GET_PARAMETER, SET_PARAMETER");
			break;
		case RTSP_ANNOUNCE:
			/* Prepare answer */
			RESPONSE_BEGIN(c, sdata->hw_addr);
			break;
		case RTSP_SETUP:
			/* Get port configuration from Transport */
			str = rtsp_get_header(c, "Transport", 0);

			/* Transport type: TCP or UDP */
			if(strstr(str, "TCP") != NULL)
				cdata->transport = RAOP_TCP;
			else
				cdata->transport = RAOP_UDP;

			/* Control port */
			p = strstr(str, "control_port=");
			if(p != NULL)
				cdata->control_port = strtoul(p+13, NULL, 10);

			/* Timing port */
			p = strstr(str, "timing_port=");
			if(p != NULL)
				cdata->timing_port = strtoul(p+12, NULL, 10);

			/* Prepare RAOP Server */
			attr.transport = cdata->transport;
			attr.port = cdata->port = 6000;
			attr.control_port = cdata->control_port;
			attr.timing_port = cdata->timing_port;
			attr.aes_key = cdata->aes_key;
			attr.aes_iv = cdata->aes_iv;
			attr.codec = cdata->codec = RAOP_ALAC;
			attr.format = cdata->format;
			attr.ip = rtsp_get_ip(c);

			/* Launch RAOP Server */
			raop_open(&cdata->raop, &attr);
			cdata->port = attr.port;

			/* Create audio stream output */
			alsa_open(&cdata->alsa, 44100, 2, 500, &raop_read, cdata->raop);

			/* Send answer */
			RESPONSE_BEGIN(c, sdata->hw_addr);
			rtsp_add_response(c, "Audio-Jack-Status", "connected; type=analog");
			snprintf(buffer, BUFFER_SIZE, "%s;server_port=%d;", rtsp_get_header(c, "Transport", 0), cdata->port);
			rtsp_add_response(c, "Transport", buffer);
			rtsp_add_response(c, "Session", "1");
			break;
		case RTSP_RECORD:
			/* Start stream */
			alsa_play(cdata->alsa);
			RESPONSE_BEGIN(c, sdata->hw_addr);
			break;
		case RTSP_SET_PARAMETER:
			RESPONSE_BEGIN(c, sdata->hw_addr);
			break;
		case RTSP_GET_PARAMETER:
			RESPONSE_BEGIN(c, sdata->hw_addr);
			break;
		case RTSP_FLUSH:
			RESPONSE_BEGIN(c, sdata->hw_addr);
			break;
		case RTSP_TEARDOWN:
			/* Stop stream and close raop */
			alsa_stop(cdata->alsa);
			alsa_close(cdata->alsa);
			cdata->alsa = NULL;
			raop_close(cdata->raop);
			cdata->raop = NULL;

			RESPONSE_BEGIN(c, sdata->hw_addr);
			break;
		default:
			return -1;
	}

	return 0;
}

static int airtunes_read_callback(struct rtsp_client *c, unsigned char *buffer, size_t size, int end_of_stream, void *user_data)
{
	struct airtunes_client_data *cdata = (struct airtunes_client_data*) rtsp_get_user_data(c);
	struct sdp_media *m = NULL;
	struct sdp *s;
	char *p;
	int i;
	int len;
	
	if(cdata == NULL)
		return 0;

	switch(rtsp_get_request(c))
	{
		case RTSP_ANNOUNCE:
			/* Parse packet and get format and AES keys */
			s = sdp_parse((char*)buffer, size);

			if(s != NULL && s->nb_medias > 0)
			{
				/* Search "m=audio" line */
				for(i = 0; i < s->nb_medias; i++)
				{
					if(strncmp(s->medias[i].media, "audio", 5) == 0)
					{
						m = &s->medias[i];
						break;
					}
				}

				/* Get fmtp */
				for(i = 0; i < m->nb_attr; i++)
				{
					if(strncmp(m->attr[i], "fmtp", 4) == 0)
					{
						cdata->format = strdup(m->attr[i]+5);
						break;
					}
				}

				/* Get AES key */
				for(i = 0; i < m->nb_attr; i++)
				{
					if(strncmp(m->attr[i], "rsaaeskey", 9) == 0)
					{
						/* Decode Base64 */
						p = strdup(m->attr[i]+10);
						rtsp_decode_base64(p);
						len = RSA_size(rsa);
						cdata->aes_key = malloc(len * sizeof(char));
						
						/* Decrypt AES key */
						if(RSA_private_decrypt(len, (unsigned char *)p, (unsigned char*) cdata->aes_key, rsa, RSA_PKCS1_OAEP_PADDING) < 0)
						{
							free(p);
							return -1;
						}

						free(p);
						break;
					}
				}

				/* Get aesiv */
				for(i = 0; i < m->nb_attr; i++)
				{
					if(strncmp(m->attr[i], "aesiv", 5) == 0)
					{
						/* Decode AES IV */
						p = strdup(m->attr[i]+6);
						rtsp_decode_base64(p);
						strncpy((char*)cdata->aes_iv, p, 16);

						free(p);
						break;
					}
				}

				/* Free SDP parser */
				sdp_free(s);
			}
			break;
		case RTSP_SET_PARAMETER:
			/* Get parameter */

			break;
		default:
			;
	}

	return 0;
}

static int airtunes_close_callback(struct rtsp_client *c, void *user_data)
{
	struct airtunes_client_data *cdata = (struct airtunes_client_data*) rtsp_get_user_data(c);

	if(cdata != NULL)
	{
		/* Stop stream */
		alsa_stop(cdata->alsa);
		alsa_close(cdata->alsa);
		raop_close(cdata->raop);

		/* Free client data */
		free(cdata);
		rtsp_set_user_data(c, NULL);
	}

	return 0;
}
