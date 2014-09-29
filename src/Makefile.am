bin_PROGRAMS = aircat

aircat_SOURCES = main.c \
		 modules.c \
		 config_file.c \
		 httpd.c \
		 avahi.c \
		 http.c \
		 fs/fs.c \
		 fs/fs_posix.c \
		 fs/fs_http.c \
		 stream.c \
		 demux.c \
		 demux_mp3.c \
		 demux_mp4.c \
		 file.c \
		 meta/meta.c \
		 shoutcast.c \
		 rtsp.c \
		 rtp.c \
		 sdp.c \
		 decoder.c \
		 decoder_pcm.c \
	 	 decoder_aac.c \
		 decoder_mp3.c \
		 decoder_alac.c \
		 outputs.c \
		 output_alsa.c \
		 resample.c \
		 cache.c \
		 db.c \
		 id3.c \
		 timers.c \
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
	       $(libsqlite_LIBS) \
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
		 $(libsqlite_CFLAGS) \
		 -Wall

# C++ support and TagLib support
aircat_CPPFLAGS = -I$(top_srcdir)/include \
		  $(libtag_CFLAGS)
aircat_SOURCES += meta/meta_taglib.cpp

EXTRA_DIST = modules.h \
	     outputs.h \
	     output_alsa.h \
	     fs/fs_posix.h \
	     fs/fs_http.h \
	     demux_mp3.h \
	     demux_mp4.h \
	     decoder_pcm.h \
	     decoder_aac.h \
	     decoder_mp3.h \
	     decoder_alac.h \
	     timers.h \
	     id3.h

