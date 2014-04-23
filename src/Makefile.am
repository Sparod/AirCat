bin_PROGRAMS = aircat

aircat_SOURCES = main.c \
		 utils.c \
		 config_file.c \
		 httpd.c \
		 radio.c \
		 radio_list.c \
		 airtunes.c \
		 avahi.c \
		 http.c \
		 files.c \
		 file.c \
		 file_format.c \
		 shoutcast.c \
		 rtsp.c \
		 rtp.c \
		 sdp.c \
		 raop_tcp.c \
		 raop.c \
		 decoder.c \
	 	 decoder_aac.c \
		 decoder_mp3.c \
		 decoder_alac.c \
		 resample.c \
		 output.c \
		 output_alsa.c

aircat_LDADD = $(libssl_LIBS) \
	       $(libmad_LIBS) \
	       $(libsoxr_LIBS) \
	       $(libasound2_LIBS) \
	       $(libavahi_LIBS) \
	       $(libmicrohttpd_LIBS) \
	       $(libjsonc_LIBS) \
	       $(libtag_LIBS) \
	       -lpthread

aircat_CFLAGS =  $(libssl_CFLAGS) \
		 $(libmad_CFLAGS) \
		 $(libsoxr_CFLAGS) \
		 $(libasound2_CFLAGS) \
		 $(libavahi_CFLAGS) \
		 $(libmicrohttpd_CFLAGS) \
		 $(libjsonc_CFLAGS) \
		 -Wall

# C++ support and TagLib support
aircat_CPPFLAGS = $(libtag_CFLAGS)
aircat_SOURCES += file_format_taglib.cpp

EXTRA_DIST = http.h \
	     utils.h \
	     config_file.h \
	     httpd.h \
	     radio.h \
	     radio_list.h \
	     airtunes.h \
	     avahi.h \
	     files.h \
	     file.h \
	     file_format.h \
	     shoutcast.h \
	     rtsp.h \
	     rtp.h \
	     sdp.h \
	     raop_tcp.h \
	     raop.h \
	     decoder.h \
	     decoder_aac.h \
	     decoder_mp3.h \
	     decoder_alac.h \
	     resample.h \
	     output.h \
	     output_alsa.h

