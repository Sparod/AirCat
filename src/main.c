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

#include "airtunes.h"
#include "avahi.h"

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

int main(int argc, char* argv[])
{
	struct avahi_handle *avahi;
	struct airtunes_handle *airtunes;

	struct timeval timeout;
	fd_set fds;

	/* Open Avahi Client */
	avahi_open(&avahi);

	/* Open Airtunes Server */
	airtunes_open(&airtunes, avahi);

	/* Start Airtunes Server */
	airtunes_start(airtunes);

	/* Wait an input on stdin (only for test purpose) */
	while(1)
	{
		FD_ZERO(&fds);
		FD_SET(0, &fds); 
		timeout.tv_sec = 1;
		timeout.tv_usec = 0;

		if(select(1, &fds, NULL, NULL, &timeout) < 0)
			break;

		if(FD_ISSET(0, &fds))
			break;
	}

	/* Stop Airtunes Server */
	airtunes_stop(airtunes);

	/* Close Airtunes Server */
	airtunes_close(airtunes);

	/* Close Avahi Client */
	avahi_close(avahi);

	return EXIT_SUCCESS;
}
