AC_PREREQ(2.53)
AC_INIT(cluttermozembed, 0.0, http://www.moblin.org/)
AM_INIT_AUTOMAKE()
AC_CONFIG_SRCDIR(cluttermozembed/clutter-mozembed.c)
AM_CONFIG_HEADER(config.h)
AM_MAINTAINER_MODE

AC_ISC_POSIX
AC_PROG_CC
AC_STDC_HEADERS
AC_PROG_LIBTOOL

PKG_PROG_PKG_CONFIG()

AC_ARG_ENABLE(plugins,
      AS_HELP_STRING([--enable-plugins], 
                     ["Support displaying mozilla plugins"]),
      [ENABLED_PLUGINS=$enableval],
      [plugins=no])

if test "x$ENABLED_PLUGINS" = "xyes"; then
PKG_CHECK_MODULES(GTK, gtk+-2.0)
AC_DEFINE([SUPPORT_PLUGINS], [], [Enables support for mozilla plugins])
fi
AM_CONDITIONAL([SUPPORT_PLUGINS], [test "x$ENABLED_PLUGINS" = "xyes"])

PKG_CHECK_MODULES(GOBJECT, gobject-2.0 >= 2.12.0 gio-2.0)
PKG_CHECK_MODULES(GTHREAD, gthread-2.0)
PKG_CHECK_MODULES(CLUTTER, clutter-0.9 >= 0.9.0)
PKG_CHECK_MODULES(MOZILLA, mozilla-js mozilla-headless >= 1.9.2a1pre)
PKG_CHECK_MODULES(MHS, mhs-1.0)

MOZHOME=`${PKG_CONFIG} --variable=prefix mozilla-headless`"/lib/xulrunner-"`${PKG_CONFIG} --modversion mozilla-headless`
AC_SUBST([MOZHOME])

AC_OUTPUT([
Makefile
cluttermozembed/Makefile
cluttermozembed/cluttermozembed.pc
tests/Makefile
])
