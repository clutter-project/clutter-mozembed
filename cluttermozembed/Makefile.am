
AM_CPPFLAGS = \
	-DPKGDATADIR=\"$(pkgdatadir)\" \
	$(GOBJECT_CFLAGS) \
	$(GTHREAD_CFLAGS) \
	$(CLUTTER_CFLAGS) \
	$(MOZILLA_CFLAGS) \
	-DSDKDIR=\"$(SDKDIR)\" \
	-Wall
AM_LDFLAGS = \
	$(GOBJECT_LIBS) \
	$(GTHREAD_LIBS) \
	$(CLUTTER_LIBS) \
	$(MOZILLA_LIBS)

source_h = \
	clutter-mozembed.h
source_c = \
	clutter-mozembed.c

bin_PROGRAMS = mozheadless

mozheadless_SOURCES = mozheadless.c

lib_LTLIBRARIES = libcluttermozembed.la
libcluttermozembed_la_SOURCES = $(source_c) $(source_h)

library_includedir=$(includedir)/cluttermozembed
library_include_HEADERS = $(source_h)

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = cluttermozembed.pc

