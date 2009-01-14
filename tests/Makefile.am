
AM_CPPFLAGS = \
	$(CLUTTER_CFLAGS) \
	$(MOZILLA_CFLAGS) \
	-I$(top_builddir)/cluttermozembed
AM_LDFLAGS = \
	$(CLUTTER_LIBS) \
	$(MOZILLA_LIBS)

noinst_PROGRAMS = \
	test-mozembed
#	web-browser

test_mozembed_SOURCES = test-mozembed.c
test_mozembed_LDADD = ../cluttermozembed/libcluttermozembed.la

#web_browser_SOURCES = web-browser.c web-browser.h
#web_browser_LDADD = ../cluttermozembed/libcluttermozembed.la

