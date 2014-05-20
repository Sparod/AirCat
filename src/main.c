/*
 * main.c - Main program routines
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
#include <getopt.h>
#include <signal.h>

#include "config_file.h"
#include "output.h"
#include "avahi.h"
#include "httpd.h"

#include "module.h"
#include "airtunes.h"
#include "radio.h"
#include "files.h"

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifndef CONFIG_PATH
	#define CONFIG_PATH "/etc/aircat/"
#endif

#ifndef VERSION
	#define VERSION "1.0.0"
#endif

/* Common modules */
static struct output_handle *output = NULL;
static struct avahi_handle *avahi = NULL;
static struct httpd_handle *httpd = NULL;
static struct config_handle *config = NULL;
/* Modules */
static struct module *modules = NULL;
static int modules_count = 0;

/* URLs */
struct url_table config_urls[];

/* Program args */
static char *config_file = NULL;	/* Alternative configuration file */
static int verbose = 0;			/* Verbosity */
static int stop_signal = 0;		/* Stop signal */

static void print_usage(const char *name)
{
	printf("Usage: %s [OPTIONS]\n"
		"\n"
		"Options:\n"
		"-c      --config=FILE        Use FILE as configuration file\n"
		"-h      --help               Print this usage and exit\n"
		"-v      --verbose            Active verbose output\n"
		"        --version            Print version and exit\n",
		 name);
}

static void print_version(void)
{
	printf("AirCat " VERSION "\n");
}

static void parse_opt(int argc, char * const argv[])
{
	int c;

	/* Get options */
	while(1)
	{
		int option_index = 0;
		static const char *short_options = "c:hv";
		static struct option long_options[] =
		{
			{"version",      no_argument,        0, 0},
			{"config",       required_argument,  0, 'c'},
			{"help",         no_argument,        0, 'h'},
			{"verbose",      no_argument,        0, 'v'},
			{0, 0, 0, 0}
		};

		/* Get next option */
		c = getopt_long (argc, argv, short_options, long_options, &option_index);
		if(c == EOF)
			break;

		/* Parse option */
		switch(c)
		{
			case 0:
				switch(option_index)
				{
					case 0:
						/* Version */
						print_version();
						exit(EXIT_SUCCESS);
						break;
				}
				break;
			case 'c':
				/* Config file */
				config_file = strdup(optarg);
				break;
			case 'v':
				/* Verbose */
				verbose = 1;
				break;
			case 'h':
				/* Help */
				print_usage(argv[0]);
				exit(EXIT_SUCCESS);
				break;
			default:
				print_usage(argv[0]);
				exit(EXIT_FAILURE);
		}
	}
}

void signal_handler(int signum)
{
	if(signum == SIGINT || signum == SIGTERM)
	{
		printf("Received Stop signal...\n");
		stop_signal = 1;
	}
}

int main(int argc, char* argv[])
{
	struct module modules_list[] = {
		airtunes_module,
		radio_module,
		files_module
	};
	int nb_modules = sizeof(modules_list) / sizeof(struct module);
	struct module_attr attr;
	struct timeval timeout;
	struct config *cfg;
	fd_set fds;
	int i;

	/* Parse options */
	parse_opt(argc, argv);

	/* Set configuration filename */
	if(config_file == NULL)
		config_file = strdup(CONFIG_PATH "/aircat.conf");

	/* Open configuration */
	config_open(&config, config_file);

	/* Setup signal handler */
	signal(SIGINT, signal_handler);
	signal(SIGTERM, signal_handler);

	/* Load module list */
	modules = modules_list;
	modules_count = nb_modules;

	/* Open Avahi Client */
	avahi_open(&avahi);

	/* Open Output Module */
	output_open(&output, OUTPUT_ALSA, 44100, 2);

	/* Get HTTP configuration from file */
	cfg = config_get_config(config, "httpd");

	/* Open HTTP Server */
	httpd_open(&httpd, cfg);

	/* Free HTTP configuration */
	if(cfg != NULL)
		config_free_config(cfg);

	/* Open all modules */
	attr.avahi = avahi;
	attr.output = output;
	for(i = 0; i < modules_count; i++)
	{
		/* Open module */
		if(modules[i].open == NULL)
			continue;

		/* Get module configuration */
		attr.config = config_get_config(config, modules[i].name);

		/* Open module */
		if(modules[i].open(&modules[i].handle, &attr) != 0)
		{
			fprintf(stderr, "Failed to open %s module!\n",
				modules[i].name);
			if(modules[i].close != NULL)
				modules[i].close(&modules[i].handle);
			modules[i].handle = NULL;
		}

		/* Free module configuration */
		if(attr.config != NULL)
			config_free_config((struct config *)attr.config);

		/* Add URLs to HTTP server */
		if(modules[i].urls != NULL)
			httpd_add_urls(httpd, modules[i].name, modules[i].urls,
				       modules[i].handle);
	}

	/* Add basic URLs */
	httpd_add_urls(httpd, "config", config_urls, NULL);

	/* Start HTTP Server */
	httpd_start(httpd);

	/* Wait an input on stdin (only for test purpose) */
	while(!stop_signal)
	{
		FD_ZERO(&fds);
		FD_SET(0, &fds); 
		timeout.tv_sec = 1;
		timeout.tv_usec = 0;

		if(select(1, &fds, NULL, NULL, &timeout) < 0)
			break;

		if(FD_ISSET(0, &fds))
			break;

		/* Iterate Avahi client */
		avahi_loop(avahi, 10);
	}

	/* Stop HTTP Server */
	httpd_stop(httpd);

	/* Close HTTP Server */
	httpd_close(httpd);

	/* Close all modules */
	for(i = 0; i < modules_count; i++)
	{
		/* Save module configuration */
		if(modules[i].get_config != NULL)
		{
			cfg = modules[i].get_config(modules[i].handle);
			config_set_config(config, modules[i].name, cfg);
			config_free_config(cfg);
		}

		/* Close module */
		if(modules[i].close != NULL)
			modules[i].close(modules[i].handle);
	}

	/* Close Output Module */
	output_close(output);

	/* Close Avahi Client */
	avahi_close(avahi);

	/* Save configuration */
	config_save(config);

	/* Close Configuration */
	if(config_file != NULL)
		free(config_file);
	config_close(config);

	return EXIT_SUCCESS;
}

/******************************************************************************
 *                           Basic URLs for AirCat                            *
 ******************************************************************************/

static int config_httpd_default(void *h, struct httpd_req *req,
				unsigned char **buffer, size_t *size)
{
	int i;

	/* Set HTTP server to default */
	httpd_set_config(httpd, NULL);

	/* Set all modules to default */
	for(i = 0; i < modules_count; i++)
	{
		if(modules[i].set_config != NULL)
			modules[i].set_config(modules[i].handle, NULL);
	}

	return 200;
}

static int config_httpd_reload(void *h, struct httpd_req *req,
			       unsigned char **buffer, size_t *size)
{
	struct config *cfg;
	int i;

	/* Load config from file */
	config_load(config);

	/* Get HTTP configuration from file */
	cfg = config_get_config(config, "httpd");

	/* Set HTTP server configuration */
	httpd_set_config(httpd, cfg);

	/* Free configuration */
	if(cfg != NULL)
		config_free_config(cfg);

	/* Set configuration of all modules */
	for(i = 0; i < modules_count; i++)
	{
		/* Get module configuration from file */
		cfg = config_get_config(config, modules[i].name);

		/* Set configuration */
		if(modules[i].set_config != NULL)
			modules[i].set_config(modules[i].handle, cfg);

		/* Free module configuration */
		if(cfg != NULL)
			config_free_config(cfg);
	}

	return 200;
}

static int config_httpd_save(void *h, struct httpd_req *req,
			     unsigned char **buffer, size_t *size)
{
	struct config *cfg = NULL;
	int i;

	/* Get HTTP configuration from module */
	cfg = httpd_get_config(httpd);

	/* Set HTTP configuration in file */
	config_set_config(config, "httpd", cfg);

	/* Free configuration */
	if(cfg != NULL)
		config_free_config(cfg);

	/* Get all modules configuration */
	for(i = 0; i < modules_count; i++)
	{
		/* Get configuration from module */
		if(modules[i].get_config != NULL)
			cfg = modules[i].get_config(modules[i].handle);

		/* Set configuration in file */
		config_set_config(config, modules[i].name, cfg);

		/* Free configuration */
		if(cfg != NULL)
			config_free_config(cfg);
		cfg = NULL;
	}

	/* Save config to file */
	config_save(config);

	return 200;
}

static int config_httpd(void *h, struct httpd_req *req,
			     unsigned char **buffer, size_t *size)
{
	struct config *cfg = NULL;
	json_object *json, *tmp;
	struct lh_entry *entry;
	char *str;
	int i;

	if(req->method == HTTPD_GET)
	{
		/* Create a JSON object */
		json = json_object_new_object();

		/* Get HTTP configuration from module */
		cfg = httpd_get_config(httpd);
		if(cfg != NULL)
		{
			/* Get JSON object */
			tmp = config_to_json(cfg);

			/* Add object to main JSON object */
			if(req->resource == NULL || *req->resource == '\0' ||
			   strcmp(req->resource, "httpd") == 0)
			json_object_object_add(json, "httpd", tmp);

			/* Free configuration */
			config_free_config(cfg);
		}

		/* Get all modules configuration */
		for(i = 0; i < modules_count; i++)
		{
			/* Get configuration from module */
			if(modules[i].get_config != NULL)
				cfg = modules[i].get_config(modules[i].handle);
			if(cfg == NULL)
				continue;

			/* Get JSON object */
			tmp = config_to_json(cfg);

			/* Add object to main JSON object */
			if(req->resource == NULL || *req->resource == '\0' ||
			   strcmp(req->resource, modules[i].name) == 0)
			json_object_object_add(json, modules[i].name, tmp);

			/* Free configuration */
			config_free_config(cfg);
			cfg = NULL;
		}

		/* Get string */
		str = strdup(json_object_to_json_string(json));
		*buffer = (unsigned char*) str;
		*size = strlen(str);

		/* Free configuration */
		json_object_put(json);
	}
	else
	{
		/* Parse each JSON entry */
		entry = json_object_get_object(req->json)->head;
		while(entry)
		{
			/* Get JSON object and key */
			str = (char*) entry->k;
			tmp = (struct json_object*) entry->v;
			entry = entry->next;

			/* Check resource name */
			if(req->resource != NULL && *req->resource != '\0' &&
			   strcmp(req->resource, str) != 0)
				continue;

			/* Get HTTP configuration from module */
			if(strcmp(str, "httpd") == 0)
			{
				/* Get coniguration */
				cfg = config_json_to_config(tmp);

				/* Set configuration */
				httpd_set_config(httpd, cfg);

				/* Free configuration */
				config_free_config(cfg);
				continue;
			}

			/* Check in modules */
			for(i = 0; i < modules_count; i++)
			{
				if(strcmp(str, modules[i].name) == 0)
				{
					/* Get coniguration */
					cfg = config_json_to_config(tmp);

					/* Set configuration */
					if(modules[i].set_config != NULL)
						modules[i].set_config(
							      modules[i].handle,
							      cfg);

					/* Free configuration */
					config_free_config(cfg);
					continue;
				}
			}
		}
	}

	return 200;
}

struct url_table config_urls[] = {
	{"default", 0,             HTTPD_PUT,             0,
						 (void*) &config_httpd_default},
	{"reload",  0,             HTTPD_PUT,             0,
						  (void*) &config_httpd_reload},
	{"save",    0,             HTTPD_PUT,             0,
						    (void*) &config_httpd_save},
	{"",        HTTPD_EXT_URL, HTTPD_GET | HTTPD_PUT, HTTPD_JSON,
							 (void*) &config_httpd},
	{0, 0, 0, 0}
};

