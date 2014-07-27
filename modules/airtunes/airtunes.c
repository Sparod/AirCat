/*
 * airtunes.c - A RAOP/Airplay server
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
#include <pthread.h>

#include <openssl/bio.h>
#include <openssl/ssl.h>
#include <openssl/err.h>

#include "module.h"
#include "avahi.h"
#include "rtsp.h"
#include "raop.h"
#include "sdp.h"
#include "output.h"
#include "utils.h"

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#define BUFFER_SIZE 512
#define MAX_VOLUME OUTPUT_VOLUME_MAX

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

enum {
	AIRTUNES_STARTING,
	AIRTUNES_RUNNING,
	AIRTUNES_STOPPING,
	AIRTUNES_STOPPED
};

struct airtunes_stream {
	/* Output stream */
	struct output_stream_handle *stream;
	/* Stream id */
	char *id;
	/* Stream name */
	char *name;
	/* Stream status */
	unsigned long played;
	unsigned long position;
	unsigned long duration;
	/* Stream volume */
	unsigned long volume;
	/* Stream meta */
	char *title;
	char *artist;
	char *album;
	/* Cover art */
	unsigned char *img;
	unsigned long img_size;
	unsigned long img_len;
	char *img_type;
	/* Next stream */
	struct airtunes_stream *next;
};

struct airtunes_client_data {
	/* Audio output handlers */
	struct raop_handle *raop;
	struct output_stream_handle *stream;
	/* AES key and IV */
	unsigned char *aes_key;
	unsigned char aes_iv[16];
	/* Format */
	int codec;
	char *format;
	unsigned long samplerate;
	unsigned char channels;
	/* RAOP transport protocol */
	int transport;
	/* RAOP server port */
	unsigned int port;
	/* RTCP ports */
	unsigned int control_port;
	unsigned int timing_port;
	/* Stream infortmations */
	struct airtunes_stream *infos;
};

struct airtunes_handle{
	/* Avahi client */
	struct avahi_handle *avahi;
	int local_avahi;
	unsigned char hw_addr[6];
	/* Output module */
	struct output_handle *output;
	/* Airtunes */
	char *name;
	unsigned int port;
	char *password;
	int status;
	int reload;
	/* RSA private key */
	RSA *rsa;
	/* RTSP server */
	struct rtsp_handle *rtsp;
	/* Thread */
	pthread_t thread;
	pthread_mutex_t mutex;
	/* Stream infos */
	struct airtunes_stream *streams;
};

/* RTSP Callback */
static int airtunes_request_callback(struct rtsp_client *c, int request, 
				     const char *url, void *user_data);
static int airtunes_read_callback(struct rtsp_client *c, unsigned char *buffer,
				  size_t size, int end_of_stream,
				  void *user_data);
static int airtunes_close_callback(struct rtsp_client *c, void *user_data);
static int airtunes_set_config(struct airtunes_handle *h, const struct json *c);
static int airtunes_start(struct airtunes_handle *h);

static int airtunes_open(struct airtunes_handle **handle,
			 struct module_attr *attr)
{
	char buf[6] = {0x00, 0x51, 0x52, 0x53, 0x54, 0x55};
	struct airtunes_handle *h;

	/* Allocate structure */
	*handle = malloc(sizeof(struct airtunes_handle));
	if(*handle == NULL)
		return -1;
	h = *handle;

	/* Init structure */
	h->avahi = attr->avahi;
	h->output = attr->output;
	h->local_avahi = 0;
	h->status = AIRTUNES_STOPPED;
	h->reload = 0;
	h->name = NULL;
	h->port = 5000;
	h->password = NULL;
	h->streams = NULL;
	memcpy(h->hw_addr, buf, 6);

	/* Allocate a local avahi client if no avahi has been passed */
	if(h->avahi == NULL)
	{
		if(avahi_open(&h->avahi) < 0)
			return -1;
		h->local_avahi = 1;
	}

	/* Load RSA private key */
	BIO *tBio = BIO_new_mem_buf(AIRPORT_PRIVATE_KEY, -1);
	h->rsa = PEM_read_bio_RSAPrivateKey(tBio, NULL, NULL, NULL);
	BIO_free(tBio);

	/* Init thread mutex */
	pthread_mutex_init(&h->mutex, NULL);

	/* Set configuration */
	airtunes_set_config(h, attr->config);

	/* Start forced: FIXME */
	if(airtunes_start(h) != 0)
		return -1;

	return 0;
}

static void *airtunes_thread(void *user_data)
{
	struct airtunes_handle *h = (struct airtunes_handle*) user_data;
	char *name;
	int port;
	int i;

	/* Open RTSP server */
	port = h->port;
	if(rtsp_open(&h->rtsp, port, 2, &airtunes_request_callback, 
		     &airtunes_read_callback, &airtunes_close_callback, h) < 0)
	{
		h->status = AIRTUNES_STOPPED;
		return NULL;
	}

	/* Lock mutex */
	pthread_mutex_lock(&h->mutex);

	/* Register the service with Avahi */
	asprintf(&name, "%02x%02x%02x%02x%02x%02x@%s", h->hw_addr[0],
		 h->hw_addr[1], h->hw_addr[2], h->hw_addr[3], h->hw_addr[4],
		 h->hw_addr[5], h->name);
	avahi_add_service(h->avahi, name, "_raop._tcp", h->port, "tp=TCP,UDP",
			  "sm=false", "sv=false", "ek=1", "et=0,1", "cn=0,1",
			  "ch=2", "ss=16", "sr=44100", "pw=false", "vn=3",
			  "md=0,1,2", "txtvers=1", NULL);

	/* Unlock mutex */
	pthread_mutex_unlock(&h->mutex);

	/* Change status of airtunes */
	if(h->status != AIRTUNES_STOPPING)
		h->status = AIRTUNES_RUNNING;

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

	/* Free name */
	free(name);

	/* Close RTSP server */
	rtsp_close(h->rtsp);

	/* Change status */
	h->status = AIRTUNES_STOPPED;

	return NULL;
}

static int airtunes_start(struct airtunes_handle *h)
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

static int airtunes_stop(struct airtunes_handle *h)
{
	if(h->status == AIRTUNES_STOPPING || h->status == AIRTUNES_STOPPED)
		return 0;

	/* Stop server */
	h->status = AIRTUNES_STOPPING;

	return 0;
}

static int airtunes_set_config(struct airtunes_handle *h, const struct json *c)
{
	const char *name;
	const char *password;

	if(h == NULL)
		return -1;

	/* Lock mutex */
	pthread_mutex_lock(&h->mutex);

	/* Free previous values */
	if(h->name != NULL)
		free(h->name);
	if(h->password != NULL)
		free(h->password);
	h->name = NULL;
	h->password = NULL;

	/* Parse config */
	if(c != NULL)
	{
		/* Get name and password */
		name = json_get_string(c, "name");
		if(name != NULL)
			h->name = strdup(name);
		password = json_get_string(c, "password");
		if(password != NULL && *password != '\0')
			h->password = strdup(password);
	}

	/* Set default values */
	if(h->name == NULL)
		h->name = strdup("AirCat");

	/* Unlock mutex */
	pthread_mutex_unlock(&h->mutex);

	return 0;
}

static struct json *airtunes_get_config(struct airtunes_handle *h)
{
	struct json *c;

	c = json_new();
	if(c == NULL)
		return NULL;

	/* Lock mutex */
	pthread_mutex_lock(&h->mutex);

	/* Set name and password */
	json_set_string(c, "name", h->name);
	json_set_string(c, "password", h->password);

	/* Unlock mutex */
	pthread_mutex_unlock(&h->mutex);

	return c;
}

static struct airtunes_stream *airtunes_add_stream(struct airtunes_handle *h)
{
	struct airtunes_stream *s;

	/* Allocate stream */
	s = malloc(sizeof(struct airtunes_stream));
	if(s != NULL)
	{
		/* Init stream */
		memset(s, 0, sizeof(struct airtunes_stream));

		/* Add an id to stream */
		s->id = random_string(10);

		/* Lock mutex */
		pthread_mutex_lock(&h->mutex);

		/* Add stream to list */
		s->next = h->streams;
		h->streams = s;

		/* Unlock mutex */
		pthread_mutex_unlock(&h->mutex);
	}

	return s;
}

static void airtunes_free_stream(struct airtunes_stream *s)
{
	if(s == NULL)
		return;

	if(s->id != NULL)
		free(s->id);
	if(s->name != NULL)
		free(s->name);
	if(s->title != NULL)
		free(s->title);
	if(s->artist != NULL)
		free(s->artist);
	if(s->album != NULL)
		free(s->album);
	if(s->img != NULL)
		free(s->img);
	if(s->img_type != NULL)
		free(s->img_type);

	free(s);
}

static void airtunes_remove_stream(struct airtunes_handle *h,
				   struct airtunes_stream *s)
{
	struct airtunes_stream **sp;

	if(h == NULL || s == NULL)
		return;

	/* Lock mutex */
	pthread_mutex_lock(&h->mutex);

	/* Remove stream from list */
	sp = &h->streams;
	while((*sp) != NULL)
	{
		if(*sp == s)
		{
			*sp = s->next;
			airtunes_free_stream(s);
			break;
		}
		else
			sp = &((*sp)->next);
	}

	/* Unlock mutex */
	pthread_mutex_unlock(&h->mutex);
}

static int airtunes_close(struct airtunes_handle *h)
{
	struct airtunes_stream *s;

	if(h == NULL)
		return 0;

	/* Stop server */
	airtunes_stop(h);

	/* Wait end of thread */
	pthread_join(h->thread, NULL);

	/* Close Avahi client if local */
	if(h->local_avahi)
		avahi_close(h->avahi);

	/* Free possible remaining streams */
	while(h->streams != NULL)
	{
		s = h->streams;
		h->streams = s->next;
		airtunes_free_stream(s);
	}

	/* Free strings */
	if(h->name != NULL)
		free(h->name);
	if(h->password != NULL)
		free(h->password);

	/* Free RSA */
	if(h->rsa != NULL)
		RSA_free(h->rsa);

	/* Free structure */
	free(h);

	return 0;
}

static int airtunes_do_apple_response(struct rtsp_client *c, 
				      unsigned char *hw_addr, RSA *rsa)
{
	unsigned char response_tmp[38];
	const char *challenge;
	char *rsa_response;
	char *response;
	char *decoded;
	int pos = 0;
	int len;

	challenge = rtsp_get_header(c, "Apple-Challenge", 1);

	if(challenge != NULL)
	{
		/* Decode base64 string */
		decoded = strdup(challenge);
		rtsp_decode_base64(decoded);
		len = strlen(decoded);

		/* Make the response */
		memcpy(response_tmp, decoded, 16);
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
		free(decoded);
		free(rsa_response);

		return 0;
	}

	return -1;
}

#define RESPONSE_BEGIN(c, s) rtsp_create_response(c, 200, "OK"); \
		airtunes_do_apple_response(c, s, h->rsa); \
		rtsp_add_response(c, "Server", "AirCat/1.0"); \
		rtsp_add_response(c, "CSeq", rtsp_get_header(c, "CSeq", 1));

static void airtunes_get_rtp_info(struct rtsp_client *c, uint16_t *seq,
				  uint32_t *timestamp)
{
	const char *header, *str;

	header = rtsp_get_header(c, "RTP-Info", 0);
	if(header == NULL)
		return;

	/* Get next sequence number */
	str = strstr(header, "seq=");
	if(str != NULL && seq != NULL)
		*seq = strtoul(str+4, NULL, 10);

	/* Get next timestamp */
	str = strstr(header, "rtptime=");
	if(str != NULL && timestamp != NULL)
		*timestamp = strtoul(str+8, NULL, 10);
}

static int airtunes_request_callback(struct rtsp_client *c, int request,
				     const char *url, void *user_data)
{
	struct airtunes_handle *h = (struct airtunes_handle *) user_data;
	struct airtunes_client_data *cdata = (struct airtunes_client_data *)
							  rtsp_get_user_data(c);
	struct raop_attr attr;
	char buffer[BUFFER_SIZE];
	char *username;
	const char *str;
	uint16_t seq = 0;
	char *p;
	int len;

	/* Allocate structure to handle session */
	if(cdata == NULL)
	{
		cdata = malloc(sizeof(struct airtunes_client_data));
		memset(cdata, 0, sizeof(struct airtunes_client_data));
		rtsp_set_user_data(c, cdata);

		/* Add info stream */
		cdata->infos = airtunes_add_stream(h);
		str = rtsp_get_name(c);
		if(str != NULL)
		{
			/* Remove ".local" from name */
			len = strlen(str) - (strstr(str, ".local") ? 6 : 0);
			cdata->infos->name = strndup(str, len);
		}
	}

	/* Lock mutex */
	pthread_mutex_lock(&h->mutex);

	/* Verify password */
	if(h->password != NULL)
	{
		username = rtsp_digest_auth_get_username(c);
		if(username == NULL || rtsp_digest_auth_check(c, username, 
							      h->password,
							      h->name) != 0)
		{
			rtsp_create_digest_auth_response(c, h->name, "", 0);
			airtunes_do_apple_response(c, h->hw_addr,
						   h->rsa);
			rtsp_add_response(c, "Server", "AirCat/1.0");
			rtsp_add_response(c, "CSeq", rtsp_get_header(c, "CSeq",
									    1));

			/* Unlock mutex */
			pthread_mutex_unlock(&h->mutex);

			return 0;
		}
	}

	/* Unlock mutex */
	pthread_mutex_unlock(&h->mutex);

	switch(request)
	{
		case RTSP_OPTIONS:
			RESPONSE_BEGIN(c, h->hw_addr);
			rtsp_add_response(c, "Public", "ANNOUNCE, SETUP, "
						       "RECORD, PAUSE, FLUSH, "
						       "TEARDOWN, OPTIONS, "
						       "GET_PARAMETER, "
						       "SET_PARAMETER");
			break;
		case RTSP_ANNOUNCE:
			/* Prepare answer */
			RESPONSE_BEGIN(c, h->hw_addr);
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
			attr.codec = cdata->codec;
			attr.format = cdata->format;
			attr.ip = rtsp_get_ip(c);

			/* Launch RAOP Server */
			raop_open(&cdata->raop, &attr);
			cdata->port = attr.port;

			/* Get stream format */
			cdata->samplerate = raop_get_samplerate(cdata->raop);
			cdata->channels = raop_get_channels(cdata->raop);

			/* Create audio stream output */
			cdata->stream = output_add_stream(h->output,
							  cdata->infos->name,
							  cdata->samplerate,
							  cdata->channels, 0, 0,
							  &raop_read,
							  cdata->raop);

			/* Copy output stream handle in stream structure */
			cdata->infos->stream = cdata->stream;

			/* Send answer */
			RESPONSE_BEGIN(c, h->hw_addr);
			rtsp_add_response(c, "Audio-Jack-Status",
						      "connected; type=analog");
			snprintf(buffer, BUFFER_SIZE, "%s;server_port=%d;",
				 rtsp_get_header(c, "Transport", 0),
				 cdata->port);
			rtsp_add_response(c, "Transport", buffer);
			rtsp_add_response(c, "Session", "1");
			break;
		case RTSP_RECORD:
			/* Get flush sequence */
			airtunes_get_rtp_info(c, &seq, NULL);
			raop_flush(cdata->raop, seq);

			/* Start stream */
			output_play_stream(h->output, cdata->stream);
			RESPONSE_BEGIN(c, h->hw_addr);
			break;
		case RTSP_SET_PARAMETER:
			RESPONSE_BEGIN(c, h->hw_addr);
			break;
		case RTSP_GET_PARAMETER:
			RESPONSE_BEGIN(c, h->hw_addr);
			break;
		case RTSP_FLUSH:
			/* Get flush sequence number */
			airtunes_get_rtp_info(c, &seq, NULL);

			/* Flush stream and RAOP */
			output_pause_stream(h->output, cdata->stream);
			output_flush_stream(h->output, cdata->stream);
			raop_flush(cdata->raop, seq);
			cdata->infos->played = 0;
			output_play_stream(h->output, cdata->stream);
			RESPONSE_BEGIN(c, h->hw_addr);
			break;
		case RTSP_TEARDOWN:
			/* Stop stream */
			output_remove_stream(h->output, cdata->stream);
			cdata->infos->stream = NULL;
			cdata->stream = NULL;

			/* Close raop */
			raop_close(cdata->raop);
			cdata->raop = NULL;

			RESPONSE_BEGIN(c, h->hw_addr);
			break;
		default:
			return -1;
	}

	return 0;
}

static int airtunes_read_announce(struct airtunes_handle *h,
				  struct rtsp_client *c,
				  struct airtunes_client_data *cdata,
				  unsigned char *buffer, size_t size,
				  int end_of_stream)
{
	struct sdp_media *m = NULL;
	struct sdp *s = NULL;
	const char *rtpmap = NULL;
	char *p;
	int ret;
	int len;
	int i;

	/* Parse packet and get format and AES keys */
	s = sdp_parse((char*)buffer, size);
	if(s == NULL)
		return -1;

	/* Search "m=audio" line */
	for(i = 0; i < s->nb_medias; i++)
	{
		if(strncmp(s->medias[i].media, "audio", 5) == 0)
		{
			m = &s->medias[i];
			break;
		}
	}
	if(m == NULL)
	{
		sdp_free(s);
		return -1;
	}

	/* Get rtpmap, fmtp, rsaaeskey and aesiv */
	for(i = 0; i < m->nb_attr; i++)
	{
		if(strncmp(m->attr[i], "rtpmap", 6) == 0)
		{
			/* Get codec */
			rtpmap = m->attr[i] + 7;
			p = m->attr[i] + 10;

			/* Find codec */
			if(strncmp(p, "L16", 3) == 0)
			{
				/* 'rtpmap:96 L16/44100/2' for PCM */
				/* FIXME: On an Ipod Touch 2G with iOS 4.2.1,
				 * L16 mode act exactly as AppleLossless...
				 */
				cdata->codec = RAOP_PCM;
			}
			else if(strncmp(p, "AppleLossless", 13) == 0)
			{
				/* 'rtpmap:96 AppleLossless' for ALAC */
				cdata->codec = RAOP_ALAC;
			}
			else if(strncmp(p, "mpeg4-generic", 13) == 0)
			{
				/* 'rtpmap:96 mpeg4-generic/44100/2' for AAC */
				cdata->codec = RAOP_AAC;
			}
			else
			{
				/* Unknown codec: abort */
				sdp_free(s);
				return -1;
			}
		}
		else if(strncmp(m->attr[i], "fmtp", 4) == 0)
		{
			/* Get format string */
			cdata->format = strdup(m->attr[i] + 5);
		}
		else if(strncmp(m->attr[i], "rsaaeskey", 9) == 0)
		{
			/* Get AES key: first decode base64 */
			p = strdup(m->attr[i] + 10);
			rtsp_decode_base64(p);
			len = RSA_size(h->rsa);
			cdata->aes_key = malloc(len * sizeof(char));

			/* Decrypt AES key */
			ret = RSA_private_decrypt(len, (unsigned char*) p,
						(unsigned char*) cdata->aes_key,
						h->rsa, RSA_PKCS1_OAEP_PADDING);
			if(ret < 0)
			{
				free(p);
				sdp_free(s);
				return -1;
			}
			free(p);
		}
		else if(strncmp(m->attr[i], "aesiv", 5) == 0)
		{
			/* Get AES IV */
			p = strdup(m->attr[i] + 6);
			rtsp_decode_base64(p);
			memcpy(cdata->aes_iv, p, 16);
			free(p);
		}
	}

	/* Add a pseudo format for PCM */
	if(cdata->codec == RAOP_PCM && cdata->format == NULL && rtpmap != NULL)
		cdata->format = strdup(rtpmap);

	/* Free SDP parser */
	sdp_free(s);

	return 0;
}

static int airtunes_read_set_param(struct airtunes_handle *h,
				   struct rtsp_client *c,
				   struct airtunes_client_data *cdata,
				   unsigned char *buffer, size_t size,
				   int end_of_stream)
{
	unsigned long start, cur, end, len;
	const char *str, *slen;
	float vol;
	char *p;
	int ret;

	/* Get Ccontent-type */
	str = rtsp_get_header(c, "content-type", 0);
	if(str == NULL)
		return -1;

	/* Check content-type */
	if(strcmp(str, "text/parameters") == 0)
	{
		/* Progression or volume */
		if(size > 8 && strncmp(buffer, "volume: ", 8) == 0)
		{
			/* Get volume */
			vol = strtof(buffer + 8, NULL);

			/* Lock mutex */
			pthread_mutex_lock(&h->mutex);

			/* Convert float volume to int */
			if(vol == -144.0)
				cdata->infos->volume = 0;
			else
				cdata->infos->volume = (vol + 30.0) *
						       MAX_VOLUME / 30.0;

			/* Set stream volume */
			output_set_volume_stream(h->output, cdata->stream,
						 cdata->infos->volume);

			/* Unlock mutex */
			pthread_mutex_unlock(&h->mutex);
		}
		else if(size > 10 && strncmp(buffer, "progress: ", 10) == 0)
		{
			/* Get RTP time values */
			p = buffer + 10;
			start = strtoul(p, &p, 10);
			cur = strtoul(p+1, &p, 10);
			end = strtoul(p+1, NULL, 10);

			/* Lock mutex */
			pthread_mutex_lock(&h->mutex);

			/* Convert values in seconds */
			cdata->infos->duration = (end - start) /
						 cdata->samplerate;
			cdata->infos->position = (cur - start) /
						 cdata->samplerate;

			/* Get played samples */
			start = output_get_status_stream(h->output,
							 cdata->stream,
							 OUTPUT_STREAM_PLAYED);
			cdata->infos->played = start / 1000;

			/* Unlock mutex */
			pthread_mutex_unlock(&h->mutex);
		}
	}
	else if(strcmp(str, "application/x-dmap-tagged") == 0)
	{
		/* DMAP metadata */

		/* Free previous image */
		if(cdata->infos->img != NULL)
		{
			free(cdata->infos->img);
			cdata->infos->img = NULL;
		}
		if(cdata->infos->img_type != NULL)
		{
			free(cdata->infos->img_type);
			cdata->infos->img_type = NULL;
		}
		cdata->infos->img_size = 0;
		cdata->infos->img_len = 0;
	}
	else if(strncmp(str, "image/", 6) == 0)
	{
		/* Remove previous image if none */
		if(strncmp(str+6, "none", 4) == 0)
		{
			if(cdata->infos->img != NULL)
			{
				free(cdata->infos->img);
				cdata->infos->img = NULL;
			}
			if(cdata->infos->img_type != NULL)
			{
				free(cdata->infos->img_type);
				cdata->infos->img_type = NULL;
			}
			cdata->infos->img_size = 0;
			cdata->infos->img_len = 0;
			return 0;
		}

		/* Create new image */
		if(cdata->infos->img_len == 0)
		{
			/* Get image length */
			slen = rtsp_get_header(c, "content-length", 0);
			if(slen == NULL || (len = strtol(slen, NULL, 10)) == 0)
				return 0;

				/* Free previous image */
			if(cdata->infos->img != NULL)
				free(cdata->infos->img);
			if(cdata->infos->img_type != NULL)
			{
				free(cdata->infos->img_type);
				cdata->infos->img_type = NULL;
			}

			/* Allocate new image */
			cdata->infos->img = malloc(len);
			if(cdata->infos->img == NULL)
				return -1;
			cdata->infos->img_size = len;
			cdata->infos->img_len = 0;

			/* Copy content-type */
			cdata->infos->img_type = strdup(str);
		}

		/* Copy content */
		len = cdata->infos->img_size - cdata->infos->img_len;
		if(len > size)
			len = size;
		memcpy(&cdata->infos->img[cdata->infos->img_len], buffer, len);
		cdata->infos->img_len += len;
	}

	return 0;
}

static int airtunes_read_callback(struct rtsp_client *c, unsigned char *buffer,
				  size_t size, int end_of_stream,
				  void *user_data)
{
	struct airtunes_handle *h = (struct airtunes_handle *) user_data;
	struct airtunes_client_data *cdata = (struct airtunes_client_data*)
							  rtsp_get_user_data(c);

	int ret;

	if(cdata == NULL)
		return 0;

	switch(rtsp_get_request(c))
	{
		case RTSP_ANNOUNCE:
			/* Parse SDP configuration */
			ret = airtunes_read_announce(h, c, cdata, buffer, size,
						     end_of_stream);
			break;
		case RTSP_SET_PARAMETER:
			/* Set stream parameter */
			ret = airtunes_read_set_param(h, c, cdata, buffer, size,
						      end_of_stream);
			break;
		default:
			;
	}

	return ret;
}

static int airtunes_close_callback(struct rtsp_client *c, void *user_data)
{
	struct airtunes_client_data *cdata = (struct airtunes_client_data*)
							  rtsp_get_user_data(c);
	struct airtunes_handle *h = (struct airtunes_handle *) user_data;

	if(cdata != NULL)
	{
		/* Stop stream */
		output_remove_stream(h->output, cdata->stream);
		cdata->infos->stream = NULL;
		raop_close(cdata->raop);

		/* Remove info stream */
		airtunes_remove_stream(h, cdata->infos);

		/* Free client data */
		free(cdata);
		rtsp_set_user_data(c, NULL);
	}

	return 0;
}

#define ADD_STRING(j, k, s) json_object_object_add(j, k, \
			       s != NULL ? json_object_new_string(s) : NULL);
#define ADD_INT(j, k, i) json_object_object_add(j, k, json_object_new_int(i));

static int airtunes_httpd_status(struct airtunes_handle *h,
				 struct httpd_req *req, unsigned char **buffer,
				 size_t *size)
{
	struct airtunes_stream *s;
	json_object *root, *tmp;
	unsigned long played;
	char *str;

	/* Create JSON array */
	root = json_object_new_array();
	if(root == NULL)
		return 500;

	/* Lock mutex */
	pthread_mutex_lock(&h->mutex);

	/* Add infos to JSON status */
	for(s = h->streams; s != NULL; s = s->next)
	{
		/* Create JSON object */
		tmp = json_object_new_object();
		if(tmp == NULL)
			continue;

		/* Get played samples from output stream */
		played = output_get_status_stream(h->output, s->stream,
						  OUTPUT_STREAM_PLAYED) / 1000;

		/* Add values to it */
		ADD_STRING(tmp, "id", s->id);
		ADD_STRING(tmp, "name", s->name);
		ADD_STRING(tmp, "title", s->title);
		ADD_STRING(tmp, "artist", s->artist);
		ADD_STRING(tmp, "album", s->album);
		ADD_INT(tmp, "pos", s->position + played - s->played);
		ADD_INT(tmp, "length", s->duration);
		ADD_INT(tmp, "volume", s->volume);

		/* Add object to array */
		if(json_object_array_add(root, tmp) != 0)
			json_object_put(tmp);
	}

	/* Unlock mutex */
	pthread_mutex_unlock(&h->mutex);

	/* Get string from JSON object */
	str = strdup(json_object_to_json_string(root));

	/* Free JSON object */
	json_object_put(root);

	*buffer = str;
	*size = str != NULL ? strlen(str) : 0;
	return 200;
}

static int airtunes_httpd_img(struct airtunes_handle *h, struct httpd_req *req,
			      unsigned char **buffer, size_t *size)
{
	struct airtunes_stream *s;

	/* Check id from URL */
	if(req->resource == NULL)
	{
		*buffer = strdup("Bad index");
		*size = 10;
		return 400;
	}

	/* Lock mutex */
	pthread_mutex_lock(&h->mutex);

	/* Find stream */
	for(s = h->streams; s != NULL; s = s->next)
	{
		if(strcmp(s->id, req->resource) == 0)
			break;
	}
	if(s == NULL)
	{
		/* Unlock mutex */
		pthread_mutex_unlock(&h->mutex);

		*buffer = strdup("Stream not found");
		*size = 17;
		return 400;
	}

	/* Get image from stream */
	if(s->img != NULL && s->img_type != NULL && s->img_len == s->img_size)
	{
		*buffer = malloc(s->img_len);
		if(*buffer == NULL)
		{
			/* Unlock mutex */
			pthread_mutex_unlock(&h->mutex);
			return 500;
		}
		memcpy(*buffer, s->img, s->img_len);
		*size = s->img_len;
		req->content_type = strdup(s->img_type);
	}

	/* Unlock mutex */
	pthread_mutex_unlock(&h->mutex);

	return 200;
}

static int airtunes_httpd_restart(struct airtunes_handle *h,
				  struct httpd_req *req, unsigned char **buffer,
				  size_t *size)
{
	return 200;
}

static struct url_table airtunes_url[] = {
	{"/status",  HTTPD_STRICT_URL, HTTPD_GET, 0,
						(void*) &airtunes_httpd_status},
	{"/img",     HTTPD_EXT_URL,    HTTPD_GET, 0,
						   (void*) &airtunes_httpd_img},
	{"/restart", HTTPD_STRICT_URL, HTTPD_PUT, 0,
					       (void*) &airtunes_httpd_restart},
	{0, 0, 0}
};

struct module module_entry = {
	.id = "raop",
	.name = "Airtunes",
	.description = "Airtunes / Airplay module for Audio streaming over "
		       "network.",
	.open = (void*) &airtunes_open,
	.close = (void*) &airtunes_close,
	.set_config = (void*) &airtunes_set_config,
	.get_config = (void*) &airtunes_get_config,
	.urls = (void*) &airtunes_url,
};
