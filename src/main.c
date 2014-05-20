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
	/* Common modules */
	struct output_handle *output;
	struct avahi_handle *avahi;
	struct httpd_handle *httpd;
	struct config_handle *config;
	/* Modules */
	struct module modules[] = {
		airtunes_module,
		radio_module,
		files_module
	};
	struct module_attr attr;
	int modules_count = sizeof(modules)/sizeof(struct module);
	struct config *cfg;
	/* Select on stdin */
	struct timeval timeout;
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

	/* Open Avahi Client */
	avahi_open(&avahi);

	/* Open Output Module */
	output_open(&output, OUTPUT_ALSA, 44100, 2);

	/* Open HTTP Server */
	httpd_open(&httpd, config);

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

