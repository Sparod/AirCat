/*
 * files.h - A File manager module
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

#include <json.h>
#include <json_tokener.h>

#include "config_file.h"
#include "utils.h"
#include "files.h"
#include "file.h"

#define PLAYLIST_ALLOC_SIZE 32

struct files_playlist {
	char *filename;
	struct file_format *format;
};

struct files_handle {
	/* File player */
	struct file_handle *file;
	struct output_handle *output;
	struct output_stream *stream;
	int is_playing;
	/* Playlist */
	struct files_playlist *playlist;
	int playlist_alloc;
	int playlist_len;
	int playlist_cur;
};

int files_open(struct files_handle **handle, struct output_handle *o)
{
	struct files_handle *h;

	/* Allocate structure */
	*handle = malloc(sizeof(struct files_handle));
	if(*handle == NULL)
		return -1;
	h = *handle;

	/* Init structure */
	h->file = NULL;
	h->output = o;
	h->stream = NULL;
	h->is_playing = 0;
	h->playlist_cur = -1;

	/* Allocate playlist */
	h->playlist = malloc(PLAYLIST_ALLOC_SIZE *
			     sizeof(struct files_playlist));
	if(h->playlist != NULL)
		h->playlist_alloc = PLAYLIST_ALLOC_SIZE;
	h->playlist_len = 0;

	return 0;
}

static inline void files_free_playlist(struct files_playlist *p)
{
	if(p->filename != NULL)
		free(p->filename);
	if(p->format != NULL)
		file_format_free(p->format);
}

int files_add(struct files_handle *h, const char *filename)
{
	struct files_playlist *p;
	char *real_path;
	int len;

	if(filename == NULL)
		return -1;

	/* Make real path */
	len = strlen(config.files_path) + strlen(filename) + 2;
	real_path = calloc(sizeof(char), len);
	if(real_path == NULL)
		return -1;
	sprintf(real_path, "%s/%s", config.files_path, filename);

	/* Add more space to playlist */
	if(h->playlist_len == h->playlist_alloc)
	{
		/* Reallocate playlist */
		p = realloc(h->playlist, PLAYLIST_ALLOC_SIZE *
			    sizeof(struct files_playlist));
		if(p == NULL)
			return -1;

		h->playlist = p;
		h->playlist_alloc += PLAYLIST_ALLOC_SIZE;
	}

	/* Fill the new playlist entry */
	p = &h->playlist[h->playlist_len];
	p->filename = strdup(real_path);
	p->format = file_format_parse(real_path, TAG_PICTURE);

	/* Increment playlist len */
	h->playlist_len++;

	return h->playlist_len - 1;
}

int files_remove(struct files_handle *h, int index)
{
	/* Check if it is current file */
	if(h->playlist_cur == index)
	{
		files_stop(h);
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

	return 0;
}

void files_flush(struct files_handle *h)
{
	/* Stop playing before flush */
	files_stop(h);

	/* Flush all playlist */
	for(; h->playlist_len--;)
	{
		files_free_playlist(&h->playlist[h->playlist_len]);
	}
	h->playlist_len = 0;
	h->playlist_cur = -1;
}

int files_play(struct files_handle *h, int index)
{
	unsigned long samplerate;
	unsigned char channels;

	/* Files module must be enabled */
	if(config.files_enabled != 1)
		return 0;

	/* Get last played index */
	if(index == -1 && (index = h->playlist_cur) < 0)
		index = 0;

	/* Check playlist index */
	if(index >= h->playlist_len)
		return -1;

	/* Stop previous playing */
	files_stop(h);

	/* Start new player */
	if(file_open(&h->file, h->playlist[index].filename) != 0)
		return -1;

	/* Get samplerate and channels */
	samplerate = file_get_samplerate(h->file);
	channels = file_get_channels(h->file);

	/* Open new Audio stream output and play */
	h->stream = output_add_stream(h->output, samplerate, channels,
				      &file_read, h->file);
	output_play_stream(h->output, h->stream);

	h->playlist_cur = index;
	h->is_playing = 1;

	return 0;
}

int files_pause(struct files_handle *h)
{
	if(h == NULL || h->output == NULL || h->stream == NULL)
		return 0;

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

	return 0;
}

int files_stop(struct files_handle *h)
{
	if(h == NULL || h->file == NULL)
		return 0;

	/* Stop stream */
	h->is_playing = 0;

	/* Close stream */
	if(h->stream != NULL)
		output_remove_stream(h->output, h->stream);
	h->stream = NULL;

	/* Close file */
	file_close(h->file);
	h->file = NULL;

	return 0;
}

#define ADD_STRING(root, key, value) if(value != NULL) \
	     json_object_object_add(root, key, json_object_new_string(value)); \
	else \
	     json_object_object_add(root, key, NULL);

#define ADD_INT(root, key, value) \
		  json_object_object_add(root, key, json_object_new_int(value));

static json_object *files_get_file_json_object(const char *filename,
					       struct file_format *meta,
					       int add_pic)
{
	json_object *tmp = NULL;
	char *pic = NULL;

	if(filename == NULL)
		return NULL;

	/* Create temporary object */
	tmp = json_object_new_object();
	if(tmp == NULL)
		return NULL;

	/* Add filename */
	json_object_object_add(tmp, "file", json_object_new_string(filename));

	/* Get tag data */
	if(meta != NULL)
	{
		/* Add all tags */
		ADD_STRING(tmp, "title", meta->title);
		ADD_STRING(tmp, "artist", meta->artist);
		ADD_STRING(tmp, "album", meta->album);
		ADD_STRING(tmp, "comment", meta->comment);
		ADD_STRING(tmp, "genre", meta->genre);
		ADD_INT(tmp, "track", meta->track);
		ADD_INT(tmp, "year", meta->year);

		/* Get picture */
		if(add_pic && meta->picture.data != NULL)
			pic = base64_encode((const char *)meta->picture.data,
					    meta->picture.size);

		/* Add picture to object */
		ADD_STRING(tmp, "picture", pic);
		ADD_STRING(tmp, "mime", meta->picture.mime);
		if(pic != NULL)
			free(pic);
	}

	return tmp;
}

char *files_get_json_status(struct files_handle *h, int add_pic)
{
	struct json_object *tmp;
	char *str = NULL;
	int idx;

	idx = h->playlist_cur;
	if(idx < 0)
		return strdup("{ \"file\": null }");

	/* Create basic JSON object */
	str = basename(h->playlist[idx].filename);
	tmp = files_get_file_json_object(str, h->playlist[idx].format, add_pic);
	if(tmp != NULL)
	{
		/* Add curent postion and audio file length */
		ADD_INT(tmp, "pos", file_get_pos(h->file));
		ADD_INT(tmp, "length", file_get_length(h->file));
	}

	/* Get JSON string */
	str = strdup(json_object_to_json_string(tmp));

	/* Free JSON object */
	json_object_put(tmp);

	return str;
}

char *files_get_json_playlist(struct files_handle *h)
{
	struct json_object *root, *tmp;
	char *str;
	int i;

	/* Create JSON object */
	root = json_object_new_array();
	if(root == NULL)
		return NULL;

	/* Fill the JSON array with playlist */
	for(i = 0; i < h->playlist_len; i++)
	{
		/* Create temporary object */
		str = basename(h->playlist[i].filename);
		tmp = files_get_file_json_object(str, h->playlist[i].format, 0);
		if(tmp == NULL)
			continue;

		/* Add to list */
		if(json_object_array_add(root, tmp) != 0)
			json_object_put(tmp);
	}

	/* Get JSON string */
	str = strdup(json_object_to_json_string(root));

	/* Free JSON object */
	json_object_put(root);

	return str;
}

char *files_get_json_list(struct files_handle *h, const char *path)
{
	char *ext[] = { ".mp3", ".m4a", ".ogg", ".wav", NULL };
	struct json_object *root = NULL, *dir_list, *file_list, *tmp;
	struct dirent *entry;
	struct stat s;
	DIR *dir;
	struct file_format *format;
	char *real_path;
	char *str = NULL;
	int len, i;

	/* Make real path */
	if(path == NULL)
	{
		real_path = strdup(config.files_path);
	}
	else
	{
		len = strlen(config.files_path) + strlen(path) + 2;
		real_path = calloc(sizeof(char), len);
		if(real_path == NULL)
			return NULL;
		sprintf(real_path, "%s/%s", config.files_path, path);
	}

	/* Open directory */
	dir = opendir(real_path);
	if(dir == NULL)
		goto end;

	/* Create JSON object */
	root = json_object_new_object();
	dir_list = json_object_new_array();
	if(root == NULL || dir_list == NULL)
		goto end;
	file_list = json_object_new_array();
	if(file_list == NULL)
	{
		json_object_put(dir_list);
		goto end;
	}

	/* List files  */
	while((entry = readdir(dir)) != NULL)
	{
		if(entry->d_name[0] == '.')
			continue;

		/* Make complete filanme path */
		len = strlen(real_path) + strlen(entry->d_name) + 2;
		str = calloc(sizeof(char), len);
		if(str == NULL)
			continue;
		sprintf(str, "%s/%s", real_path, entry->d_name);

		/* Stat file */
		stat(str, &s);

		/* Add to array */
		if(s.st_mode & S_IFREG)
		{
			/* Verify extension */
			len = strlen(entry->d_name);
			for(i = 0; ext[i] != NULL; i++)
			{
				if(strcmp(&entry->d_name[len-4], ext[i]) == 0)
				{
					/* Read meta data from file */
					format = file_format_parse(str,
								   TAG_PICTURE);

					/* Create temporary object */
					tmp = files_get_file_json_object(
								  entry->d_name,
								  format, 1);
					if(format != NULL)
						file_format_free(format);
					if(tmp == NULL)
						continue;

					if(json_object_array_add(file_list, tmp)
					   != 0)
						json_object_put(tmp);
					break;
				}
			}
		}
		else if(s.st_mode & S_IFDIR)
		{
			/* Create temporary object */
			tmp = json_object_new_string(entry->d_name);
			if(tmp == NULL)
				continue;

			if(json_object_array_add(dir_list, tmp) != 0)
				json_object_put(tmp);
		}

		/* Free complete filenmae */
		free(str);
	}

	/* Add both arrays to JSON object */
	json_object_object_add(root, "directory", dir_list);
	json_object_object_add(root, "file", file_list);

	/* Get JSON string */
	str = strdup(json_object_to_json_string(root));

end:
	if(root != NULL)
		json_object_put(root);

	if(real_path != NULL)
		free(real_path);

	if(dir != NULL)
		closedir(dir);

	return str;
}

int files_close(struct files_handle *h)
{
	if(h == NULL)
		return 0;

	/* Stop playing */
	files_stop(h);

	/* Free playlist */
	if(h->playlist != NULL)
	{
		files_flush(h);
		free(h->playlist);
	}

	free(h);

	return 0;
}

