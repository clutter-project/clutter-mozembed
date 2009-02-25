
AM_CPPFLAGS = \
	$(CLUTTER_CFLAGS) \
	$(MOZILLA_CFLAGS) \
	-I$(top_srcdir)/cluttermozembed
AM_LDFLAGS = \
	$(CLUTTER_LIBS) \
	$(MOZILLA_LIBS)

noinst_PROGRAMS = \
	test-mozembed \
	test-previews
#	web-browser

test_mozembed_SOURCES = test-mozembed.c
test_mozembed_LDADD = $(top_builddir)/cluttermozembed/libcluttermozembed.la

test_previews_SOURCES = test-previews.c
test_previews_LDADD = $(top_builddir)/cluttermozembed/libcluttermozembed.la

#web_browser_SOURCES = web-browser.c web-browser.h
#web_browser_LDADD = $(top_builddir)/cluttermozembed/libcluttermozembed.la

