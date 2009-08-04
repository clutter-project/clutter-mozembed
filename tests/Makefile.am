
AM_CPPFLAGS = \
	$(CLUTTER_CFLAGS) \
	$(MOZILLA_CFLAGS) \
	$(GTK_CFLAGS) \
	-I$(top_srcdir)/clutter-mozembed
AM_LDFLAGS = \
	$(CLUTTER_LIBS) \
	$(MOZILLA_LIBS) \
	$(GTK_LIBS)

noinst_PROGRAMS = \
	test-mozembed \
	test-previews
#	web-browser

test_libs = $(top_builddir)/clutter-mozembed/libclutter-mozembed-@CME_API_VERSION@.la

test_mozembed_SOURCES = test-mozembed.c
test_mozembed_LDADD = $(test_libs)

test_previews_SOURCES = test-previews.c
test_previews_LDADD = $(test_libs)

#web_browser_SOURCES = web-browser.c web-browser.h
#web_browser_LDADD = $(test_libs)

