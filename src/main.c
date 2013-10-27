/*
 * main.c - Main program routines
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
#include <getopt.h>

#include "config_file.h"
#include "airtunes.h"
#include "avahi.h"

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifndef CONFIG_PATH
	#define CONFIG_PATH "/etc/aircat/"
#endif

#ifndef VERSION
	#define VERSION "1.0.0"
#endif

static struct avahi_handle *avahi = NULL;	/* Avahi client handler */
static struct airtunes_handle *airtunes = NULL;	/* Airtunes / RAOP handler */
static char *config_file = NULL;		/* Alternative configuration file */
static int verbose = 0;				/* Verbosity */

static void set_default_config(void)
{
	config.name = strdup("AirCat");
	config.password = NULL;
	config.port = 8080;
	config.radio_enabled = 1;
	config.raop_enabled = 1;
	config.raop_name = NULL;
	config.raop_password = NULL;
}

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
				config_file = optarg;
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

int main(int argc, char* argv[])
{
	/* Default AirCat configuration: overwritten by config_load() */
	set_default_config();

	/* Parse options */
	parse_opt(argc, argv);

	/* Load configuration file */
	if(config_file != NULL)
		config_load(config_file);
	else
		config_load(CONFIG_PATH "/aircat.conf");

	/* Open Avahi Client */
	avahi_open(&avahi);

	/* Open Airtunes Server */
	airtunes_open(&airtunes, avahi);

	/* Start Airtunes Server */
	airtunes_start(airtunes);

	/* Wait an input on stdin (only for test purpose) */
	(void) getc(stdin);

	/* Stop Airtunes Server */
	airtunes_stop(airtunes);

	/* Close Airtunes Server */
	airtunes_close(airtunes);

	/* Close Avahi Client */
	avahi_close(avahi);

	return EXIT_SUCCESS;
}
