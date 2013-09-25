/*
 * sdp.h - A SDP tool
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

#ifndef _SDP_H
#define _SDP_H

struct sdp_time {
	char *time;			// Time			(t=)
	char **repeat;			// Repeat times		(r=)
	int nb_repeat;
};

struct sdp_media {
	char *media;			// Media		(m=)
	char *title;			// Title / Info		(i=)
	char *connect;			// Connection info	(c=)
	char **bandw;			// Bandwidth		(b=)
	int nb_bandw;
	char *key;			// Encryption Key	(k=)
	char **attr;			// Media attributes	(a=)
	int nb_attr;
};

struct sdp {
	char *version;			// Protocol version =0	(v=)
	char *origin;			// Originator id	(o=)
	char *session;			// Session name		(s=)
	char *title;			// Session title	(i=)
	char *uri;			// URI			(u=)
	char **email;			// Email address	(e=)
	int nb_email;
	char **phone;			// Phone number		(p=)
	int nb_phone;
	char *connect;			// Connection info	(c=)
	char **bandw;			// Bandwidth		(b=)
	int nb_bandw;
	struct sdp_time *times;		// Time description	(t=)
	int nb_times;
	char *zone;			// Zone			(z=)
	char *key;			// Encryption key	(k=)
	char **attr;			// Session attributes	(a=)
	int nb_attr;
	struct sdp_media *medias;	// Media description	(m=)
	int nb_medias;
};

struct sdp *sdp_parse(char *buffer, size_t len);
int sdp_generate(struct sdp *s, char *buffer, size_t len);
void sdp_free(struct sdp *s);

#endif
