
GLIB_GENMARSHAL = `pkg-config --variable=glib_genmarshal glib-2.0`
GLIB_MKENUMS = `pkg-config --variable=glib_mkenums glib-2.0`

AM_CPPFLAGS = \
	-DPKGDATADIR=\"$(pkgdatadir)\" \
	$(GOBJECT_CFLAGS) \
	$(GTHREAD_CFLAGS) \
	$(CLUTTER_CFLAGS) \
	$(MOZILLA_CFLAGS) \
	$(PLACES_CFLAGS) \
	-DMOZHOME=\"$(MOZHOME)\" \
	-Wall
AM_LDFLAGS = \
	$(GOBJECT_LIBS) \
	$(GTHREAD_LIBS) \
	$(CLUTTER_LIBS) \
	$(MOZILLA_LIBS) \
	$(PLACES_LIBS)

if SUPPORT_PLUGINS
AM_CPPFLAGS += $(GTK_CFLAGS)
AM_LDFLAGS += $(GTK_LIBS)
endif

BUILT_SOURCES = \
	clutter-mozembed-marshal.h \
	clutter-mozembed-marshal.c

STAMP_FILES = stamp-clutter-mozembed-marshal.h

clutter-mozembed-marshal.h: stamp-clutter-mozembed-marshal.h
	@true
stamp-clutter-mozembed-marshal.h: Makefile clutter-mozembed-marshal.list
	$(GLIB_GENMARSHAL) \
		--prefix=_clutter_mozembed_marshal \
		--header \
	$(srcdir)/clutter-mozembed-marshal.list > xgen-tmh && \
	(cmp -s xgen-tmh clutter-mozembed-marshal.h || \
	 cp -f xgen-tmh clutter-mozembed-marshal.h) && \
	rm -f xgen-tmh && \
	echo timestamp > $(@F)

clutter-mozembed-marshal.c: Makefile clutter-mozembed-marshal.list
	(echo "#include \"clutter-mozembed-marshal.h\"" ; \
	 $(GLIB_GENMARSHAL) \
		--prefix=_clutter_mozembed_marshal \
		--body \
	 $(srcdir)/clutter-mozembed-marshal.list ) > xgen-tmc && \
	cp -f xgen-tmc clutter-mozembed-marshal.c && \
	rm -f xgen-tmc

source_h = \
	clutter-mozembed.h
source_c = \
	clutter-mozembed.c

bin_PROGRAMS = clutter-mozheadless

clutter_mozheadless_SOURCES = \
	clutter-mozheadless.c \
	clutter-mozheadless.h

lib_LTLIBRARIES = libcluttermozembed.la
libcluttermozembed_la_SOURCES = $(source_c) $(source_h) $(BUILT_SOURCES)

library_includedir=$(includedir)/cluttermozembed
library_include_HEADERS = $(source_h)

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = cluttermozembed.pc

CLEANFILES = $(STAMP_FILES) $(BUILT_SOURCES)

EXTRA_DIST = \
	clutter-mozembed-marshal.list

