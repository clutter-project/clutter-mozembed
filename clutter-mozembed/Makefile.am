
GLIB_GENMARSHAL = `pkg-config --variable=glib_genmarshal glib-2.0`
GLIB_MKENUMS = `pkg-config --variable=glib_mkenums glib-2.0`

AM_CPPFLAGS = \
	-DPKGDATADIR=\"$(pkgdatadir)\" \
	-DMOZHOME=\"$(MOZHOME)\" \
	-DCMH_BIN=\"$(libexecdir)/clutter-mozheadless\"

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

if SUPPORT_IM
AM_CFLAGS += $(CLUTTERIMCONTEXT_CFLAGS)
AM_CXXFLAGS += $(CLUTTERIMCONTEXT_CFLAGS)
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

clutter-mozheadless-comms.c: clutter-mozembed-comms.c
	cp $^ $@

source_h = \
	clutter-mozembed.h \
	clutter-mozembed-download.h
source_priv_h = \
	clutter-mozembed-private.h
source_c = \
	clutter-mozembed-plugin-container.c\
	clutter-mozembed.c \
	clutter-mozembed-comms.c \
	clutter-mozembed-comms.h \
	clutter-mozembed-download.c

libexec_PROGRAMS = clutter-mozheadless

clutter_mozheadless_LDADD = \
	@GOBJECT_LIBS@ \
	@MOZILLA_LIBS@ \
	@MHS_LIBS@

if SUPPORT_PLUGINS
clutter_mozheadless_LDADD += @GTK_LIBS@
endif

clutter_mozheadless_SOURCES = \
	clutter-mozembed-comms.h \
	clutter-mozheadless.c \
	clutter-mozheadless.h \
	clutter-mozheadless-certs.cc \
	clutter-mozheadless-certs.h \
	clutter-mozheadless-comms.c \
	clutter-mozheadless-cookies.cc \
	clutter-mozheadless-cookies.h \
	clutter-mozheadless-downloads.cc \
	clutter-mozheadless-downloads.h \
	clutter-mozheadless-history.cc \
	clutter-mozheadless-history.h \
	clutter-mozheadless-login-manager-storage.cc \
	clutter-mozheadless-login-manager-storage.h \
	clutter-mozheadless-permission-manager.cc \
	clutter-mozheadless-permission-manager.h \
	clutter-mozheadless-prefs.cc \
	clutter-mozheadless-prefs.h \
	clutter-mozheadless-private-browsing.cc \
	clutter-mozheadless-private-browsing.h \
	clutter-mozheadless-protocol-service.cc \
	clutter-mozheadless-protocol-service.h

lib_LTLIBRARIES = libclutter-mozembed-@CME_API_VERSION@.la

libclutter_mozembed_@CME_API_VERSION@_la_LIBADD = \
	@GOBJECT_LIBS@ \
	@GTHREAD_LIBS@ \
	@CLUTTER_LIBS@

if SUPPORT_PLUGINS
libclutter_mozembed_@CME_API_VERSION@_la_LIBADD += @GTK_LIBS@
endif

if SUPPORT_IM
libclutter_mozembed_@CME_API_VERSION@_la_LIBADD += @CLUTTERIMCONTEXT_LIBS@
endif

libclutter_mozembed_@CME_API_VERSION@_la_SOURCES = \
	$(source_c) \
	$(source_h) \
	$(source_priv_h) \
	$(BUILT_SOURCES)

library_includedir=$(includedir)/clutter-$(CLUTTER_API_VERSION)/clutter-mozembed
library_include_HEADERS = $(source_h)

CLEANFILES = $(STAMP_FILES) $(BUILT_SOURCES) clutter-mozheadless-comms.c

EXTRA_DIST = \
	clutter-mozembed-marshal.list

