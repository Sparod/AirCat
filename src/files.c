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

#include <json.h>
#include <json_tokener.h>

#include "config_file.h"
#include "files.h"
#include "file.h"
#include "tag.h"

struct files_handle {
	/* File player */
	struct file_handle *file;
	struct output_handle *output;
	struct output_stream *stream;
	int is_playing;
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

	return 0;
}

int files_play(struct files_handle *h, const char *filename)
{
	unsigned long samplerate;
	unsigned char channels;
	char *real_path;
	int len;

	if(filename == NULL)
		return -1;

	/* Files module must be enabled */
	if(config.files_enabled != 1)
		return 0;

	/* Stop previous playing */
	files_stop(h);

	/* Make real path */
	len = strlen(config.files_path) + strlen(filename) + 2;
	real_path = calloc(sizeof(char), len);
	if(real_path == NULL)
		return -1;
	sprintf(real_path, "%s/%s", config.files_path, filename);

	/* Start new player */
	if(file_open(&h->file, real_path) != 0)
		return -1;

	/* Get samplerate and channels */
	samplerate = file_get_samplerate(h->file);
	channels = file_get_channels(h->file);

	/* Open new Audio stream output and play */
	h->stream = output_add_stream(h->output, samplerate, channels,
				      &file_read, h->file);
	output_play_stream(h->output, h->stream);

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

	h->is_playing = 0;

	//output_stop_stream(h->output, h->stream);
	if(h->stream != NULL)
		output_remove_stream(h->output, h->stream);
	h->stream = NULL;
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

static json_object *files_get_file_json_object(const char *file,
					       const char *filename)
{
	json_object *tmp = NULL;
	struct tag *meta;

	/* Create temporary object */
	tmp = json_object_new_object();
	if(tmp == NULL)
		return NULL;

	/* Add filename */
	json_object_object_add(tmp, "file", json_object_new_string(filename));

	/* Get tag data */
	meta = tag_read(file, 0);
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

		tag_free(meta);
	}

	return tmp;
}

char *files_get_json_list(struct files_handle *h, const char *path)
{
	char *ext[] = { ".mp3", ".m4a", ".ogg", ".wav", NULL };
	struct dirent *entry;
	DIR *dir;
	struct stat s;
	struct json_object *root = NULL, *dir_list, *file_list, *tmp;
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
	file_list = json_object_new_array();;
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
					/* Create temporary object */
					tmp = files_get_file_json_object(str,
								 entry->d_name);
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

	if(h->stream != NULL)
		output_remove_stream(h->output, h->stream);

	/* Stop and close file player */
	if(h->file != NULL)
		file_close(h->file);

	free(h);

	return 0;
}

