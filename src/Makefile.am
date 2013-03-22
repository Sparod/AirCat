bin_PROGRAMS = aircat

aircat_SOURCES = main.c \
		 http.c

aircat_LDADD = $(libssl_LIBS)

AM_CPPFLAGS =  $(libssl_CFLAGS) -Wall

EXTRA_DIST = http.h

