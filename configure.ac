AC_PREREQ(2.59)
AC_INIT(aircat, 0.0.1, http://www.sparod.com/aircat/)
AC_CONFIG_SRCDIR(src/main.c)
AC_CONFIG_MACRO_DIR([m4])
AM_INIT_AUTOMAKE([subdir-objects])
AC_CONFIG_HEADERS([config.h])

NAME="aircat"
AC_SUBST(NAME)

# Checks for programs
AC_PROG_CC
AC_PROG_CXX
AC_PROG_INSTALL
AC_PROG_MAKE_SET
AM_PROG_CC_C_O

# Checks for header files.
AC_HEADER_STDC
#strcasecmp

# Check for libssl for HTTPS support
PKG_CHECK_MODULES(libssl, libssl >= 0.9.8o, [
	AC_DEFINE([HAVE_OPENSSL], 1, ["Use openssl"])
])

# Check for libmad for MP3 decoder
PKG_CHECK_MODULES(libmad, mad >= 0.15.0b)

# Check for libfaad for AAC decoder
AC_CHECK_HEADERS(neaacdec.h,
  [AC_CHECK_LIB([faad], [NeAACDecOpen], [
    libfaad_LIBS="-lfaad"
  ])
])
AC_SUBST(libfaad_LIBS)

# Check for libsamplerate for samplerate conversion
#PKG_CHECK_MODULES(libsamplerate, samplerate >= 0.1.7)

# Check for libsoxr for samplerate conversion
PKG_CHECK_MODULES(libsoxr, soxr >= 0.1.0)

# Check for libasound2 for Alsa output
PKG_CHECK_MODULES(libasound2, alsa >= 1.0.0)

# Check for libavahi-client for Avahi support
PKG_CHECK_MODULES(libavahi, avahi-client >= 0.6.31)

# Check for libmicrohttpd for HTTP server
PKG_CHECK_MODULES(libmicrohttpd, libmicrohttpd >= 0.9.0)

# Check for libjson-c for JSON parsing
PKG_CHECK_MODULES(libjsonc, json >= 0.10)

# Check for libtag for Audio tag file support
PKG_CHECK_MODULES(libtag, taglib >= 1.7, [
	AC_DEFINE([HAVE_TAGLIB], 1, ["Use taglib"])
])

# Check for sqlite3 for database support
PKG_CHECK_MODULES(libsqlite, sqlite3 >= 3.6.19)

# Check for libsmbclient for samba support
use_samba=no
PKG_CHECK_MODULES(libsmbclient, smbclient >= 3.6.6, [use_samba=yes], [
	AC_CHECK_LIB([smbclient], [main], [
		use_samba=yes;libsmbclient_LIBS="-lsmbclient"])
])
if test "x$use_samba" != "xno"; then
	AC_DEFINE([HAVE_LIBSMBCLIENT], 1, [Use libsmbclient])
fi

# Init the Libtool
LT_INIT([dlopen])

# Modules path
modules_DIR="${libdir}/${NAME}"
AC_SUBST(modules_DIR)

AC_CONFIG_FILES([Makefile
		 doc/Makefile
		 www/Makefile
		 include/Makefile
		 modules/Makefile
		 src/Makefile])
AC_OUTPUT

