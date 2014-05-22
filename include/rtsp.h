/*
 * rtsp.h - A Tiny RTSP Server
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
#ifndef TINY_RTSP_H
#define TINY_RTSP_H

enum {RTSP_ANNOUNCE, RTSP_DESCRIBE, RTSP_OPTIONS, RTSP_SETUP, RTSP_RECORD, RTSP_SET_PARAMETER, RTSP_GET_PARAMETER, RTSP_FLUSH, RTSP_PLAY, RTSP_PAUSE, RTSP_TEARDOWN, RTSP_UNKNOWN};

struct rtsp_handle;
struct rtsp_client;

int rtsp_open(struct rtsp_handle **h, unsigned int port, unsigned int max_user, void *callback, void *read_callback, void *close_callback, void *user_data);
int rtsp_loop(struct rtsp_handle *h, unsigned int timeout);
int rtsp_close(struct rtsp_handle *h);

int rtsp_create_response(struct rtsp_client *c, unsigned int code, const char *value);
int rtsp_add_response(struct rtsp_client *c, const char *name, const char *value);

int rtsp_set_response(struct rtsp_client *c, char *str);

int rtsp_set_packet(struct rtsp_client *c, unsigned char *buffer, size_t len);

char *rtsp_get_header(struct rtsp_client *c, const char *name, int case_sensitive);
unsigned char *rtsp_get_ip(struct rtsp_client *c);
unsigned int rtsp_get_port(struct rtsp_client *c);
unsigned char *rtsp_get_server_ip(struct rtsp_client *c);
unsigned int rtsp_get_server_port(struct rtsp_client *c);
int rtsp_get_request(struct rtsp_client *c);
void *rtsp_get_user_data(struct rtsp_client *c);
void rtsp_set_user_data(struct rtsp_client *c, void *user_data);

/* Authentication part */
char *rtsp_basic_auth_get_username_password(struct rtsp_client *c, char **password);
int rtsp_create_basic_auth_response(struct rtsp_client *c, const char *realm);
char *rtsp_digest_auth_get_username(struct rtsp_client *c);
int rtsp_digest_auth_check(struct rtsp_client *c, const char *username, const char *password, const char *realm);
int rtsp_create_digest_auth_response(struct rtsp_client *c, const char *realm, const char *opaque, int signal_stale);

char *rtsp_encode_base64(const char *buffer, int length);
void rtsp_decode_base64(char *buffer);

#endif

