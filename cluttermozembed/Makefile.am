
AM_CPPFLAGS = \
	-DPKGDATADIR=\"$(pkgdatadir)\" \
	$(GOBJECT_CFLAGS) \
	$(GTHREAD_CFLAGS) \
	$(GTK_CFLAGS) \
	$(CLUTTER_CFLAGS) \
	$(MOZILLA_CFLAGS) \
	-DMOZHOME=\"$(MOZHOME)\" \
	-Wall
AM_LDFLAGS = \
	$(GOBJECT_LIBS) \
	$(GTHREAD_LIBS) \
	$(GTK_LIBS) \
	$(CLUTTER_LIBS) \
	$(MOZILLA_LIBS)

source_h = \
	clutter-mozembed.h
source_c = \
	clutter-mozembed.c

bin_PROGRAMS = clutter-mozheadless

clutter_mozheadless_SOURCES = \
	clutter-mozheadless.c \
	clutter-mozheadless.h

lib_LTLIBRARIES = libcluttermozembed.la
libcluttermozembed_la_SOURCES = $(source_c) $(source_h)

library_includedir=$(includedir)/cluttermozembed
library_include_HEADERS = $(source_h)

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = cluttermozembed.pc

