bin_PROGRAMS = aircat

aircat_SOURCES = main.c \
		 http.c \
		 shoutcast.c \
		 decoder.c \
		 decoder_mp3.c

aircat_LDADD = $(libssl_LIBS) $(libmad_LIBS)

AM_CPPFLAGS =  $(libssl_CFLAGS) $(libmad_CFLAGS) -Wall

EXTRA_DIST = http.h \
	     shoutcast.h \
	     decoder.h \
	     decoder_mp3.h

