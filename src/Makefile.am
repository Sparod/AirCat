bin_PROGRAMS = aircat

aircat_SOURCES = main.c \
		 http.c \
		 file.c \
		 shoutcast.c \
		 decoder.c \
	 	 decoder_aac.c \
		 decoder_mp3.c \
		 alsa.c

aircat_LDADD = $(libssl_LIBS) $(libmad_LIBS) $(libasound2_LIBS)

AM_CPPFLAGS =  $(libssl_CFLAGS) $(libmad_CFLAGS) $(libasound2_CFLAGS) -Wall

EXTRA_DIST = http.h \
	     file.h \
	     shoutcast.h \
	     decoder.h \
	     decoder_aac.h \
	     decoder_mp3.h \
	     alsa.h

