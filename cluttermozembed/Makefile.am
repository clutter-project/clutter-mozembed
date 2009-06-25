
GLIB_GENMARSHAL = `pkg-config --variable=glib_genmarshal glib-2.0`
GLIB_MKENUMS = `pkg-config --variable=glib_mkenums glib-2.0`

AM_CPPFLAGS = \
	-DPKGDATADIR=\"$(pkgdatadir)\" \
	-DMOZHOME=\"$(MOZHOME)\"

AM_CFLAGS = \
	$(GOBJECT_CFLAGS) \
	$(GTHREAD_CFLAGS) \
	$(CLUTTER_CFLAGS) \
	$(MOZILLA_CFLAGS) \
	$(MHS_CFLAGS) \
	-Wall

AM_CXXFLAGS = \
	$(GOBJECT_CFLAGS) \
	$(GTHREAD_CFLAGS) \
	$(CLUTTER_CFLAGS) \
	$(MOZILLA_CFLAGS) \
	$(MHS_CFLAGS) \
	-Wall -fno-rtti -fno-exceptions

if SUPPORT_PLUGINS
AM_CPPFLAGS += $(GTK_CFLAGS)
AM_CXXFLAGS += $(GTK_CFLAGS)
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
	clutter-mozembed.h \
	clutter-mozembed-download.h
source_priv_h = \
	clutter-mozembed-private.h
source_c = \
	clutter-mozembed.c \
	clutter-mozembed-download.c

bin_PROGRAMS = clutter-mozheadless

clutter_mozheadless_LDADD = \
	@GOBJECT_LIBS@ \
	@MOZILLA_LIBS@ \
	@MHS_LIBS@

if SUPPORT_PLUGINS
clutter_mozheadless_LDADD += @GTK_LIBS@
endif

clutter_mozheadless_SOURCES = \
	clutter-mozheadless.c \
	clutter-mozheadless.h \
	clutter-mozheadless-downloads.cc \
	clutter-mozheadless-downloads.h \
	clutter-mozheadless-history.cc \
	clutter-mozheadless-history.h \
	clutter-mozheadless-login-manager-storage.cc \
	clutter-mozheadless-login-manager-storage.h \
	clutter-mozheadless-prefs.cc \
	clutter-mozheadless-prefs.h \
	clutter-mozheadless-cookies.cc \
	clutter-mozheadless-cookies.h

lib_LTLIBRARIES = libcluttermozembed.la

libcluttermozembed_la_LIBADD = \
	@GOBJECT_LIBS@ \
	@GTHREAD_LIBS@ \
	@CLUTTER_LIBS@

if SUPPORT_PLUGINS
libcluttermozembed_la_LIBADD += @GTK_LIBS@
endif

libcluttermozembed_la_SOURCES = \
	$(source_c) \
	$(source_h) \
	$(source_priv_h) \
	$(BUILT_SOURCES)

library_includedir=$(includedir)/cluttermozembed
library_include_HEADERS = $(source_h)

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = cluttermozembed.pc

CLEANFILES = $(STAMP_FILES) $(BUILT_SOURCES)

EXTRA_DIST = \
	clutter-mozembed-marshal.list

