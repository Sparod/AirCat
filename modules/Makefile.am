AM_CFLAGS = -I$(top_srcdir)/include  $(libjsonc_CFLAGS)
LIBS = $(libjsonc_LIBS)

moduledir = $(modules_DIR)

# Files module
libmodule_files_la_SOURCES = files.c

# Radio module
libmodule_radio_la_SOURCES = radio/radio.c \
			     radio/radio_list.c

# Airtunes module
libmodule_airtunes_la_SOURCES = airtunes/airtunes.c \
				airtunes/dmap.c \
				airtunes/raop.c \
				airtunes/raop_tcp.c

module_LTLIBRARIES = libmodule_files.la \
		     libmodule_radio.la \
		     libmodule_airtunes.la

EXTRA_DIST = radio/radio_list.h \
	     airtunes/dmap.h \
	     airtunes/raop.h \
	     airtunes/raop_tcp.h
