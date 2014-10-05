/*
 * files.c - A Media library / File manager module
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
#include <libgen.h>
#include <pthread.h>
#include <unistd.h>

#include "files_list.h"
#include "module.h"
#include "utils.h"
#include "file.h"
#include "fs.h"

#define PLAYLIST_ALLOC_SIZE 32

struct files_playlist {
	char *filename;
	struct json *tag;
};

struct files_handle {
	/* Output handle */
	struct output_handle *output;
	/* Database handle */
	struct db_handle *db;
	/* Current file player */
	struct file_handle *file;
	struct output_stream_handle *stream;
	unsigned long pos;
	/* Previous file player */
	struct file_handle *prev_file;
	struct output_stream_handle *prev_stream;
	/* Player status */
	int is_playing;
	/* Playlist */
	struct files_playlist *playlist;
	int playlist_alloc;
	int playlist_len;
	int playlist_cur;
	/* Thread */
	pthread_t thread;
	pthread_mutex_t mutex;
	int stop;
	/* Configuration */
	char *cover_path;
	char *mount_path;
	char *path;
};

/* Data structure used for playlist add from database */
struct files_add_from_db_data {
	struct files_handle *h;
	int first_idx;
};

static void *files_thread(void *user_data);
static int files_play(struct files_handle *h, int index);
static int files_stop(struct files_handle *h);
static int files_set_config(struct files_handle *h, const struct json *c);

static int files_open(struct files_handle **handle, struct module_attr *attr)
{
	struct files_handle *h;

	/* Allocate structure */
	*handle = malloc(sizeof(struct files_handle));
	if(*handle == NULL)
		return -1;
	h = *handle;

	/* Init structure */
	h->output = attr->output;
	h->db = attr->db;
	h->file = NULL;
	h->prev_file = NULL;
	h->stream = NULL;
	h->prev_stream = NULL;
	h->is_playing = 0;
	h->playlist_cur = -1;
	h->stop = 0;
	h->cover_path = NULL;
	h->mount_path = NULL;
	h->path = NULL;

	/* Allocate playlist */
	h->playlist = malloc(PLAYLIST_ALLOC_SIZE *
			     sizeof(struct files_playlist));
	if(h->playlist != NULL)
		h->playlist_alloc = PLAYLIST_ALLOC_SIZE;
	h->playlist_len = 0;

	/* Set configuration */
	files_set_config(h, attr->config);

	/* Init database */
	files_list_init(h->db, h->path);

	/* Init thread */
	pthread_mutex_init(&h->mutex, NULL);

	/* Create thread */
	if(pthread_create(&h->thread, NULL, files_thread, h) != 0)
		return -1;

	return 0;
}

static int files_new_player(struct files_handle *h)
{
	unsigned long samplerate;
	unsigned char channels;

	/* Start new player */
	if(file_open(&h->file, h->playlist[h->playlist_cur].filename) != 0)
	{
		file_close(h->file);
		h->file = NULL;
		h->stream = NULL;
		return -1;
	}

	/* Set current position to 0 */
	h->pos = 0;

	/* Get samplerate and channels */
	samplerate = file_get_samplerate(h->file);
	channels = file_get_channels(h->file);

	/* Open new Audio stream output and play */
	h->stream = output_add_stream(h->output, NULL, samplerate, channels, 0,
				      0, &file_read, h->file);
	output_play_stream(h->output, h->stream);

	return 0;
}

static void files_play_next(struct files_handle *h)
{
	/* Close previous stream */
	if(h->prev_stream != NULL)
		output_remove_stream(h->output, h->prev_stream);

	/* Close previous file */
	file_close(h->prev_file);

	/* Move current stream to previous */
	h->prev_stream = h->stream;
	h->prev_file = h->file;

	/* Open next file in playlist */
	while(h->playlist_cur <= h->playlist_len)
	{
		h->playlist_cur++;
		if(h->playlist_cur >= h->playlist_len)
		{
			h->playlist_cur = -1;
			h->stream = NULL;
			h->file = NULL;
			break;
		}

		if(files_new_player(h) != 0)
			continue;

		break;
	}
}

static void files_play_prev(struct files_handle *h)
{
	/* Close previous stream */
	if(h->prev_stream != NULL)
		output_remove_stream(h->output, h->prev_stream);

	/* Close previous file */
	file_close(h->prev_file);

	/* Move current stream to previous */
	h->prev_stream = h->stream;
	h->prev_file = h->file;

	/* Open next file in playlist */
	while(h->playlist_cur >= 0)
	{
		h->playlist_cur--;
		if(h->playlist_cur < 0)
		{
			h->playlist_cur = -1;
			h->stream = NULL;
			h->file = NULL;
			break;
		}

		if(files_new_player(h) != 0)
			continue;

		break;
	}
}

static void *files_thread(void *user_data)
{
	struct files_handle *h = (struct files_handle *) user_data;
	unsigned long played;

	while(!h->stop)
	{
		/* Lock playlist */
		pthread_mutex_lock(&h->mutex);

		if(h->playlist_cur != -1 &&
		   h->playlist_cur+1 <= h->playlist_len)
		{
			/* Get current played from stream */
			played = output_get_status_stream(h->output, h->stream,
						  OUTPUT_STREAM_PLAYED) / 1000;

			/* Check position */
			if(h->file != NULL && 
			   (played + h->pos >= file_get_length(h->file)-1
			   || file_get_status(h->file) == FILE_EOF))
			{
				files_play_next(h);
			}
		}

		/* Unlock playlist */
		pthread_mutex_unlock(&h->mutex);

		/* Sleep during 100ms */
		usleep(100000);
	}

	return NULL;
}

static inline void files_free_playlist(struct files_playlist *p)
{
	if(p->filename != NULL)
		free(p->filename);
	if(p->tag != NULL)
		json_free(p->tag);
}

static int files_add(struct files_handle *h, unsigned long media_id,
		     const char *file_path, int path_len)
{
	struct files_playlist *p;
	int len;

	if(file_path == NULL)
		return -1;

	/* Lock playlist */
	pthread_mutex_lock(&h->mutex);

	/* Add more space to playlist */
	if(h->playlist_len == h->playlist_alloc)
	{
		/* Reallocate playlist */
		p = realloc(h->playlist,
			    (h->playlist_alloc + PLAYLIST_ALLOC_SIZE) *
			    sizeof(struct files_playlist));
		if(p == NULL)
		{
			/* Unlock playlist */
			pthread_mutex_unlock(&h->mutex);
			return -1;
		}

		h->playlist = p;
		h->playlist_alloc += PLAYLIST_ALLOC_SIZE;
	}

	/* Fill the new playlist entry */
	p = &h->playlist[h->playlist_len];
	p->filename = strdup(file_path);
	p->tag = files_list_file(h->db, h->cover_path, media_id,
				 file_path+path_len);

	/* Increment playlist len */
	h->playlist_len++;

	/* Unlock playlist */
	pthread_mutex_unlock(&h->mutex);

	return h->playlist_len - 1;
}

static int files_file_only(const struct fs_dirent *d)
{
	return (d->stat.st_mode & S_IFREG) && files_ext_check(d->name) ? 1 : 0;
}

static int files_add_from_db(void *user_data, uint64_t media_id,
			     const char *file, int media_len, int path_len)
{
	struct files_add_from_db_data *d = user_data;
	int idx;

	/* Add file to playlist */
	idx = files_add(d->h, media_id, file, media_len);
	if(d->first_idx == -1 && idx >= 0)
		d->first_idx = idx;

	return 0;
}

static int files_add_multiple(struct files_handle *h, const char *resource,
			      int64_t media_id, int play)
{
	struct files_add_from_db_data d;
	struct fs_dirent **list = NULL;
	struct stat st;
	char *f_path;
	char *m_path;
	char *path;
	int path_len;
	int count;
	int idx;
	int i;

	if(resource == NULL)
		return -1;

	/* Special path: add files from database */
	if(resource[0] == '?')
	{
		/* Prepare data */
		d.h = h;
		d.first_idx = -1;

		/* Add file(s) to playlist */
		if(files_list_list(h->db, &resource[1], files_add_from_db, &d)
		   != 0)
			return -1;

		/* Play first file */
		if(play && d.first_idx >= 0)
			files_play(h, d.first_idx);

		return 0;
	}

	/* Get media path associated to media_id */
	m_path = files_list_get_media(h->db, media_id);
	if(m_path == NULL)
		return -1;
	path_len = strlen(m_path);

	/* Make complete path */
	if(asprintf(&path, "%s/%s", m_path, resource) < 0)
	{
		free(m_path);
		return -1;
	}

	/* Stat file */
	if(stat(path, &st) != 0)
	{
		free(m_path);
		free(path);
		return -1;
	}

	/* List files if folder */
	if(st.st_mode & S_IFDIR)
	{
		/* Scan folder */
		count = fs_scandir(path, &list, files_file_only, fs_alphasort);

		/* Add all files in folder */
		for(i = 0; i < count; i++)
		{
			/* Make file path */
			if(asprintf(&f_path, "%s/%s", path, list[i]->name) < 0)
				goto next;

			/* Add file to playlist */
			idx = files_add(h, media_id, f_path, path_len);

			/* Play first file */
			if(i == 0 && play && idx >= 0)
				files_play(h, idx);

			/* Free file path */
			free(f_path);
next:
			/* Free entry */
			free(list[i]);
		}

		/* Free list */
		free(list);
	}
	else if(st.st_mode & S_IFREG)
	{
		/* Add file to playlist */
		idx = files_add(h, media_id, path, path_len);

		/* Play first file */
		if(play && idx >= 0)
			files_play(h, idx);
	}

	/* Free path */
	free(m_path);
	free(path);

	return 0;
}

static int files_remove(struct files_handle *h, int index)
{
	/* Lock playlist */
	pthread_mutex_lock(&h->mutex);

	/* Check if it is current file */
	if(h->playlist_cur == index)
	{
		/* Unlock playlist */
		pthread_mutex_unlock(&h->mutex);

		files_stop(h);

		/* Lock playlist */
		pthread_mutex_lock(&h->mutex);

		h->playlist_cur = -1;
	}
	else if(h->playlist_cur > index)
	{
		h->playlist_cur--;
	}

	/* Free index playlist structure */
	files_free_playlist(&h->playlist[index]);

	/* Remove the index from playlist */
	memmove(&h->playlist[index], &h->playlist[index+1], 
		(h->playlist_len - index - 1) * sizeof(struct files_playlist));
	h->playlist_len--;

	/* Unlock playlist */
	pthread_mutex_unlock(&h->mutex);

	return 0;
}

static void files_flush(struct files_handle *h)
{
	/* Stop playing before flush */
	files_stop(h);

	/* Lock playlist */
	pthread_mutex_lock(&h->mutex);

	/* Flush all playlist */
	for(; h->playlist_len--;)
	{
		files_free_playlist(&h->playlist[h->playlist_len]);
	}
	h->playlist_len = 0;
	h->playlist_cur = -1;

	/* Unlock playlist */
	pthread_mutex_unlock(&h->mutex);
}

static int files_play(struct files_handle *h, int index)
{
	/* Get last played index */
	if(index == -1 && (index = h->playlist_cur) < 0)
		index = 0;

	/* Check playlist index */
	if(index >= h->playlist_len)
		return -1;

	/* Stop previous playing */
	files_stop(h);

	/* Lock playlist */
	pthread_mutex_lock(&h->mutex);

	/* Start new player */
	h->playlist_cur = index;
	if(files_new_player(h) != 0)
	{
		/* Unlock playlist */
		pthread_mutex_unlock(&h->mutex);

		h->playlist_cur = -1;
		h->is_playing = 0;
		return -1;
	}

	h->is_playing = 1;

	/* Unlock playlist */
	pthread_mutex_unlock(&h->mutex);

	return 0;
}

static int files_pause(struct files_handle *h)
{
	if(h == NULL || h->output == NULL)
		return 0;

	/* Lock playlist */
	pthread_mutex_lock(&h->mutex);

	if(h->stream != NULL)
	{
		if(!h->is_playing)
		{
			h->is_playing = 1;
			output_play_stream(h->output, h->stream);
		}
		else
		{
			h->is_playing = 0;
			output_pause_stream(h->output, h->stream);
		}
	}

	/* Unlock playlist */
	pthread_mutex_unlock(&h->mutex);

	return 0;
}

static int files_stop(struct files_handle *h)
{
	if(h == NULL)
		return 0;

	/* Lock playlist */
	pthread_mutex_lock(&h->mutex);

	/* Stop stream */
	h->is_playing = 0;

	/* Close stream */
	if(h->stream != NULL)
		output_remove_stream(h->output, h->stream);
	if(h->prev_stream != NULL)
		output_remove_stream(h->output, h->prev_stream);
	h->stream = NULL;
	h->prev_stream = NULL;

	/* Close file */
	file_close(h->file);
	file_close(h->prev_file);
	h->file = NULL;
	h->prev_file = NULL;

	/* Rreset playlist position */
	h->playlist_cur = -1;

	/* Unlock playlist */
	pthread_mutex_unlock(&h->mutex);

	return 0;
}

static int files_prev(struct files_handle *h)
{
	/* Lock playlist */
	pthread_mutex_lock(&h->mutex);

	if(h->playlist_cur != -1 && h->playlist_cur >= 0)
	{
		/* Start next file in playlist */
		files_play_prev(h);

		/* Close previous stream */
		if(h->prev_stream != NULL)
			output_remove_stream(h->output, h->prev_stream);

		/* Close previous file */
		file_close(h->prev_file);

		h->prev_stream = NULL;
		h->prev_file = NULL;
	}

	/* Unlock playlist */
	pthread_mutex_unlock(&h->mutex);

	return 0;
}

static int files_next(struct files_handle *h)
{
	/* Lock playlist */
	pthread_mutex_lock(&h->mutex);

	if(h->playlist_cur != -1 && h->playlist_cur+1 <= h->playlist_len)
	{
		/* Start next file in playlist */
		files_play_next(h);

		/* Close previous stream */
		if(h->prev_stream != NULL)
			output_remove_stream(h->output, h->prev_stream);

		/* Close previous file */
		file_close(h->prev_file);

		h->prev_stream = NULL;
		h->prev_file = NULL;
	}

	/* Unlock playlist */
	pthread_mutex_unlock(&h->mutex);

	return 0;
}

static int files_seek(struct files_handle *h, unsigned long pos)
{
	/* Lock playlist */
	pthread_mutex_lock(&h->mutex);

	/* Pause stream */
	output_pause_stream(h->output, h->stream);

	/* Flush stream */
	output_flush_stream(h->output, h->stream);

	/* Seek and get new exact position */
	h->pos = file_set_pos(h->file, pos);

	/* Play stream */
	output_play_stream(h->output, h->stream);

	/* Unlock playlist */
	pthread_mutex_unlock(&h->mutex);

	return 0;
}

static char *files_get_json_status(struct files_handle *h)
{
	unsigned long played;
	struct json *tmp;
	char *str = NULL;
	int idx;

	/* Lock playlist */
	pthread_mutex_lock(&h->mutex);

	idx = h->playlist_cur;
	if(idx < 0)
	{
		/* Unlock playlist */
		pthread_mutex_unlock(&h->mutex);

		return strdup("{ \"file\": null }");
	}

	/* Create basic JSON object */
	str = basename(h->playlist[idx].filename);
	tmp = json_copy(h->playlist[idx].tag);
	if(tmp != NULL)
	{
		/* Add curent postion and audio file length */
		played = output_get_status_stream(h->output, h->stream,
						  OUTPUT_STREAM_PLAYED) / 1000;
		json_set_int(tmp, "pos", played + h->pos);
		json_set_int(tmp, "length", file_get_length(h->file));
	}

	/* Unlock playlist */
	pthread_mutex_unlock(&h->mutex);

	/* Get JSON string */
	str = strdup(json_export(tmp));

	/* Free JSON object */
	json_free(tmp);

	return str;
}

static char *files_get_json_playlist(struct files_handle *h)
{
	struct json *root, *tmp;
	char *str;
	int i;

	/* Create JSON object */
	root = json_new_array();
	if(root == NULL)
		return NULL;

	/* Lock playlist */
	pthread_mutex_lock(&h->mutex);

	/* Fill the JSON array with playlist */
	for(i = 0; i < h->playlist_len; i++)
	{
		/* Create temporary object */
		str = basename(h->playlist[i].filename);
		tmp = json_copy(h->playlist[i].tag);
		if(tmp == NULL)
			continue;

		/* Add to list */
		if(json_array_add(root, tmp) != 0)
			json_free(tmp);
	}

	/* Unlock playlist */
	pthread_mutex_unlock(&h->mutex);

	/* Get JSON string */
	str = strdup(json_export(root));

	/* Free JSON object */
	json_free(root);

	return str;
}

static int files_set_config(struct files_handle *h, const struct json *c)
{
	const char *cover_path;
	const char *mount_path;
	const char *path;

	if(h == NULL)
		return -1;

	/* Free previous values */
	if(h->path != NULL)
		free(h->path);
	if(h->mount_path != NULL)
		free(h->mount_path);
	if(h->cover_path != NULL)
		free(h->cover_path);
	h->cover_path = NULL;
	h->mount_path = NULL;
	h->path = NULL;

	/* Parse configuration */
	if(c != NULL)
	{
		/* Get files path */
		path = json_get_string(c, "path");
		if(path != NULL)
			h->path = strdup(path);

		/* Get mount path */
		mount_path = json_get_string(c, "mount_path");
		if(mount_path != NULL)
			h->mount_path = strdup(mount_path);

		/* Get cover path */
		cover_path = json_get_string(c, "cover_path");
		if(cover_path != NULL)
			h->cover_path = strdup(cover_path);
	}

	/* Set default values */
	if(h->path == NULL)
		h->path = strdup("/var/aircat/files/media");
	if(h->mount_path == NULL)
		h->mount_path = strdup("/media");
	if(h->cover_path == NULL)
		h->cover_path = strdup("/var/aircat/files/cover");

	return 0;
}

static struct json *files_get_config(struct files_handle *h)
{
	struct json *c;

	/* Create a new config */
	c = json_new();
	if(c == NULL)
		return NULL;

	/* Set current files path */
	json_set_string(c, "path", h->path);
	json_set_string(c, "mount_path", h->mount_path);
	json_set_string(c, "cover_path", h->cover_path);

	return c;
}

static int files_close(struct files_handle *h)
{
	if(h == NULL)
		return 0;

	/* Stop playing */
	files_stop(h);

	/* Stop thread */
	h->stop = 1;
	if(pthread_join(h->thread, NULL) < 0)
		return -1;

	/* Free playlist */
	if(h->playlist != NULL)
	{
		files_flush(h);
		free(h->playlist);
	}

	/* Free files path */
	if(h->path != NULL)
		free(h->path);
	if(h->mount_path != NULL)
		free(h->mount_path);
	if(h->cover_path != NULL)
		free(h->cover_path);

	free(h);

	return 0;
}

static int files_httpd_playlist_play(void *user_data, struct httpd_req *req,
				     struct httpd_res **res)
{
	struct files_handle *h = user_data;
	int idx;

	/* Get index from URL */
	idx = atoi(req->resource);
	if(idx < 0)
	{
		*res = httpd_new_response("Bad index", 0, 0);
		return 400;
	}

	/* Play selected file in playlist */
	if(files_play(h, idx) != 0)
	{
		*res = httpd_new_response("Playlist error", 0, 0);
		return 500;
	}

	return 200;
}

static int files_httpd_playlist_remove(void *user_data, struct httpd_req *req,
				       struct httpd_res **res)
{
	struct files_handle *h = user_data;
	int idx;

	/* Get index from URL */
	idx = atoi(req->resource);
	if(idx < 0)
	{
		*res = httpd_new_response("Bad index", 0, 0);
		return 400;
	}

	/* Remove from playlist */
	if(files_remove(h, idx) != 0)
	{
		*res = httpd_new_response("Playlist error", 0, 0);
		return 500;
	}

	return 200;
}

static int files_httpd_playlist_flush(void *user_data, struct httpd_req *req,
				      struct httpd_res **res)
{
	struct files_handle *h = user_data;

	/* Flush playlist */
	files_flush(h);

	return 200;
}

static int files_httpd_playlist(void *user_data, struct httpd_req *req,
				struct httpd_res **res)
{
	struct files_handle *h = user_data;
	char *list = NULL;

	/* Get playlist */
	list = files_get_json_playlist(h);
	if(list == NULL)
	{
		*res = httpd_new_response("Playlist error", 0, 0);
		return 500;
	}

	*res = httpd_new_response(list, 1, 0);
	return 200;
}

static int files_httpd_add_play(void *user_data, struct httpd_req *req,
				struct httpd_res **res)
{
	struct files_handle *h = user_data;
	struct json *list;
	const char *path, *value;
	unsigned long media_id = 1;
	int play = 0;
	int i, count;

	/* Get media_id */
	value = httpd_get_query(req, "media_id");
	if(value != NULL)
		media_id = strtoul(value, NULL, 10);

	/* Set play */
	if(req->url[11] == '/' || req->url[11] == '\0')
		play = 1;

	/* Add a selection to playlist */
	if(req->json != NULL && (count = json_array_length(req->json)) > 0)
	{
		/* Add each entry to playlist */
		for(i = 0; i < count; i++)
		{
			/* Get path from entry */
			path = json_to_string(json_array_get(req->json, i));
			if(path == NULL)
				continue;

			/* Add file to playlist */
			if(files_add_multiple(h, path, media_id, play) == 0)
				play = 0;
		}

		return 200;
	}

	/* Add file to playlist */
	if(files_add_multiple(h, req->resource, media_id, play) < 0)
	{
		*res = httpd_new_response("File is not supported", 0, 0);
		return 406;
	}

	return 200;
}

static int files_httpd_pause(void *user_data, struct httpd_req *req,
			     struct httpd_res **res)
{
	struct files_handle *h = user_data;

	/* Pause file playing */
	files_pause(h);

	return 200;
}

static int files_httpd_stop(void *user_data, struct httpd_req *req,
			    struct httpd_res **res)
{
	struct files_handle *h = user_data;

	/* Stop file playing */
	files_stop(h);

	return 200;
}

static int files_httpd_prev(void *user_data, struct httpd_req *req,
			    struct httpd_res **res)
{
	struct files_handle *h = user_data;

	/* Go to / play previous file in playlist */
	files_prev(h);

	return 200;
}

static int files_httpd_next(void *user_data, struct httpd_req *req,
			    struct httpd_res **res)
{
	struct files_handle *h = user_data;

	/* Go to / play next file in playlist */
	files_next(h);

	return 200;
}

static int files_httpd_status(void *user_data, struct httpd_req *req,
			      struct httpd_res **res)
{
	struct files_handle *h = user_data;
	char *str = NULL;

	/* Get status */
	str = files_get_json_status(h);
	if(str == NULL)
	{
		*res = httpd_new_response("Status error", 0, 0);
		return 500;
	}

	*res = httpd_new_response(str, 1, 0);
	return 200;
}

static int files_httpd_img(void *user_data, struct httpd_req *req,
			   struct httpd_res **res)
{
	struct files_handle *h = user_data;
	int code;

	/* Create a file response */
	*res = httpd_new_file_response(h->cover_path, req->resource, &code);

	return code;
}

static int files_httpd_seek(void *user_data, struct httpd_req *req,
			    struct httpd_res **res)
{
	struct files_handle *h = user_data;
	unsigned long pos;

	/* Get position from URL */
	pos = strtoul(req->resource, NULL, 10);

	/* Seek in stream */
	if(files_seek(h, pos) != 0)
	{
		*res = httpd_new_response("Bad position", 0, 0);
		return 400;
	}

	return 200;
}

static int files_httpd_info(void *user_data, struct httpd_req *req,
			    struct httpd_res **res)
{
	struct files_handle *h = user_data;
	struct json *info = NULL;
	unsigned long media_id = 1;
	const char *value;
	char *str;

	/* Get media_id */
	value = httpd_get_query(req, "media_id");
	if(value != NULL)
		media_id = strtoul(value, NULL, 10);

	/* Get file */
	info = files_list_file(h->db, h->cover_path, media_id, req->resource);
	if(info == NULL)
	{
		*res = httpd_new_response("Bad file", 0, 0);
		return 404;
	}

	/* Export JSON object */
	str = strdup(json_export(info));

	/* Free JSON object */
	json_free(info);

	*res = httpd_new_response(str, 1, 0);
	return 200;
}

static int files_httpd_list(void *user_data, struct httpd_req *req,
			    struct httpd_res **res)
{
	struct files_handle *h = user_data;
	unsigned long page = 0, count = 0;
	unsigned long media_id = 1;
	int display = FILES_LIST_DISPLAY_DEFAULT;
	int sort = FILES_LIST_SORT_DEFAULT;
	const char *filter = NULL;
	const char *value;
	char *list = NULL;
	int artist_id = 0;
	int album_id = 0;
	int genre_id = 0;

	/* Get page */
	value = httpd_get_query(req, "page");
	if(value != NULL)
		page = strtoul(value, NULL, 10);

	/* Get entries per page */
	value = httpd_get_query(req, "count");
	if(value != NULL)
		count = strtoul(value, NULL, 10);

	/* Get media for browsing */
	value = httpd_get_query(req, "media_id");
	if(value != NULL)
		media_id = strtoul(value, NULL, 10);

	/* Flag which specify library listting */
	value = httpd_get_query(req, "type");
	if(value != NULL && strcmp(value, "library") == 0)
		media_id = 0;

	/* Get display mode */
	value = httpd_get_query(req, "display");
	if(value != NULL)
	{
		if(strcmp(value, "album") == 0)
			display = FILES_LIST_DISPLAY_ALBUM;
		else if(strcmp(value, "artist") == 0)
			display = FILES_LIST_DISPLAY_ARTIST;
		else if(strcmp(value, "genre") == 0)
			display = FILES_LIST_DISPLAY_GENRE;
	}

	/* Artist ID */
	value = httpd_get_query(req, "artist_id");
	if(value != NULL)
		artist_id = strtoul(value, NULL, 10);;

	/* Album ID */
	value = httpd_get_query(req, "album_id");
	if(value != NULL)
		album_id = strtoul(value, NULL, 10);

	/* Genre ID */
	value = httpd_get_query(req, "genre_id");
	if(value != NULL)
		genre_id = strtoul(value, NULL, 10);

	/* Get sort */
	value = httpd_get_query(req, "sort");
	if(value != NULL)
	{
		if(strcmp(value, "reverse") == 0)
			sort = FILES_LIST_SORT_REVERSE;
		else if(strcmp(value, "alpha") == 0)
			sort = FILES_LIST_SORT_ALPHA;
		else if(strcmp(value, "alpha_reverse") == 0)
			sort = FILES_LIST_SORT_ALPHA_REVERSE;
		else if(strcmp(value, "title") == 0)
			sort = FILES_LIST_SORT_TITLE;
		else if(strcmp(value, "album") == 0)
			sort = FILES_LIST_SORT_ALBUM;
		else if(strcmp(value, "artist") == 0)
			sort = FILES_LIST_SORT_ARTIST;
		else if(strcmp(value, "track") == 0)
			sort = FILES_LIST_SORT_TRACK;
		else if(strcmp(value, "year") == 0)
			sort = FILES_LIST_SORT_YEAR;
		else if(strcmp(value, "duration") == 0)
			sort = FILES_LIST_SORT_DURATION;
		else if(strcmp(value, "title_reverse") == 0)
			sort = FILES_LIST_SORT_TITLE_REVERSE;
		else if(strcmp(value, "album_reverse") == 0)
			sort = FILES_LIST_SORT_ALBUM_REVERSE;
		else if(strcmp(value, "artist_reverse") == 0)
			sort = FILES_LIST_SORT_ARTIST_REVERSE;
		else if(strcmp(value, "track_reverse") == 0)
			sort = FILES_LIST_SORT_TRACK_REVERSE;
		else if(strcmp(value, "year_reverse") == 0)
			sort = FILES_LIST_SORT_YEAR_REVERSE;
		else if(strcmp(value, "duration_reverse") == 0)
			sort = FILES_LIST_SORT_DURATION_REVERSE;
	}

	/* Filter */
	filter = httpd_get_query(req, "filter");

	/* Get file list */
	list = files_list_files(h->db, h->cover_path, media_id, req->resource,
				page, count, sort, display,
				(uint64_t) artist_id, (uint64_t) album_id,
				(uint64_t) genre_id, filter);
	if(list == NULL)
	{
		*res = httpd_new_response("Bad directory", 0, 0);
		return 404;
	}

	*res = httpd_new_response(list, 1, 0);
	return 200;
}

static int files_httpd_scan(void *user_data, struct httpd_req *req,
			    struct httpd_res **res)
{
	struct files_handle *h = user_data;
	struct json *j;
	unsigned long media_id = 1;
	const char *value;
	char *status;

	if(req->method == HTTPD_PUT)
	{
		/* Check if scan in progress */
		if(files_list_is_scanning() != 0)
		{
			*res = httpd_new_response("Scan already in progress", 0,
						  0);
			return 503;
		}

		/* Get media for scanning */
		value = httpd_get_query(req, "media_id");
		if(value != NULL)
			media_id = strtoul(value, NULL, 10);

		/* Scan all music folder */
		if(files_list_scan(h->db, h->cover_path, media_id, 1) != 0)
		{
			*res = httpd_new_response("Scan failed", 0, 0);
			return 500;
		}
	}
	else
	{
		/* Create a new JSON object */
		j = json_new();
		if(j == NULL)
			return 500;

		/* Get scan status */
		if(files_list_is_scanning() != 0)
		{
			json_set_string(j, "status", "in progress");

			/* Get current file */
			status = files_list_get_scan();
			json_set_string(j, "file", status);
			if(status != NULL)
				free(status);
		}
		else
		{
			json_set_string(j, "status", "done");
		}

		/* Export JSON object */
		*res = httpd_new_response((char*) json_export(j), 1, 1);

		/* Free JSON object */
		json_free(j);
	}

	return 200;
}

static int files_httpd_media(void *user_data, struct httpd_req *req,
			     struct httpd_res **res)
{
	struct files_handle *h = user_data;
	char *list = NULL;

	/* Get media list */
	list = files_list_media(h->db, h->path, h->mount_path);
	if(list == NULL)
		return 500;

	*res = httpd_new_response(list, 1, 0);
	return 200;
}

#define HTTPD_PG HTTPD_PUT | HTTPD_GET

static struct url_table files_url[] = {
	{"/playlist/add",     HTTPD_EXT_URL, HTTPD_PUT, HTTPD_JSON,
							 &files_httpd_add_play},
	{"/playlist/play/",   HTTPD_EXT_URL, HTTPD_PUT, 0,
						    &files_httpd_playlist_play},
	{"/playlist/remove/", HTTPD_EXT_URL, HTTPD_PUT, 0,
						  &files_httpd_playlist_remove},
	{"/playlist/flush",   0,             HTTPD_PUT, 0,
						   &files_httpd_playlist_flush},
	{"/playlist",         0,             HTTPD_GET, 0,
							 &files_httpd_playlist},
	{"/play",             HTTPD_EXT_URL, HTTPD_PUT, HTTPD_JSON,
							 &files_httpd_add_play},
	{"/pause",            0,             HTTPD_PUT, 0, &files_httpd_pause},
	{"/stop",             0,             HTTPD_PUT, 0, &files_httpd_stop},
	{"/prev",             0,             HTTPD_PUT, 0, &files_httpd_prev},
	{"/next",             0,             HTTPD_PUT, 0, &files_httpd_next},
	{"/seek/",            HTTPD_EXT_URL, HTTPD_PUT, 0, &files_httpd_seek},
	{"/status",           0            , HTTPD_GET, 0, &files_httpd_status},
	{"/img/",             HTTPD_EXT_URL, HTTPD_GET, 0, &files_httpd_img},
	{"/info/",            HTTPD_EXT_URL, HTTPD_GET, 0, &files_httpd_info},
	{"/list",             HTTPD_EXT_URL, HTTPD_GET, 0, &files_httpd_list},
	{"/scan",             0,             HTTPD_PG,  0, &files_httpd_scan},
	{"/media",            0,             HTTPD_GET, 0, &files_httpd_media},
	{0, 0, 0}
};

struct module module_entry = {
	.id = "files",
	.name = "File browser",
	.description = "Browse through local and remote folder and play any "
		       "music file.",
	.open = (void*) &files_open,
	.close = (void*) &files_close,
	.set_config = (void*) &files_set_config,
	.get_config = (void*) &files_get_config,
	.urls = (void*) &files_url,
};
