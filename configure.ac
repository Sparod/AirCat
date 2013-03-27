AC_PREREQ(2.59)
AC_INIT(aircat, 0.0.1, http://www.sparod.com/aircat/)
AC_CONFIG_SRCDIR(src/main.c)
AM_INIT_AUTOMAKE(AC_PACKAGE_NAME, AC_PACKAGE_VERSION)
AM_CONFIG_HEADER(config.h)

AM_INIT_AUTOMAKE()

# Checks for programs
AC_PROG_CC
AC_PROG_INSTALL
AC_PROG_MAKE_SET

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
  [AC_CHECK_LIB(faad, NeAACDecOpen)])

AC_CONFIG_FILES([Makefile
		 doc/Makefile
		 src/Makefile])
AC_OUTPUT

