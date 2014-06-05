bin_PROGRAMS = aircat

aircat_SOURCES = main.c \
		 modules.c \
		 config_file.c \
		 httpd.c \
		 avahi.c \
		 http.c \
		 file.c \
		 file_mp3.c \
		 file_mp4.c \
		 file_format.c \
		 shoutcast.c \
		 rtsp.c \
		 rtp.c \
		 sdp.c \
		 decoder.c \
	 	 decoder_aac.c \
		 decoder_mp3.c \
		 decoder_alac.c \
		 output.c \
		 output_alsa.c \
		 resample.c \
		 cache.c \
		 utils.c

aircat_LDADD = $(libssl_LIBS) \
	       $(libmad_LIBS) \
	       $(libfaad_LIBS) \
	       $(libsoxr_LIBS) \
	       $(libasound2_LIBS) \
	       $(libavahi_LIBS) \
	       $(libmicrohttpd_LIBS) \
	       $(libjsonc_LIBS) \
	       $(libtag_LIBS) \
	       -lpthread

aircat_LDFLAGS = -export-dynamic

aircat_CFLAGS = -DMODULES_PATH='"${modules_DIR}"' \
		 $(libssl_CFLAGS) \
		 $(libmad_CFLAGS) \
		 $(libsoxr_CFLAGS) \
		 $(libasound2_CFLAGS) \
		 $(libavahi_CFLAGS) \
		 $(libmicrohttpd_CFLAGS) \
		 $(libjsonc_CFLAGS) \
		 -Wall

# C++ support and TagLib support
aircat_CPPFLAGS = -I$(top_srcdir)/include \
		  $(libtag_CFLAGS)
aircat_SOURCES += file_format_taglib.cpp

EXTRA_DIST = file_private.h \
	     modules.h \
	     output_alsa.h \
	     decoder_aac.h \
	     decoder_mp3.h \
	     decoder_alac.h

