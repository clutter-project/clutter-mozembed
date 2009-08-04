SUBDIRS=clutter-mozembed data tests

clutter-mozembed-$(CME_API_VERSION).pc: clutter-mozembed.pc
	cp $< $@

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = clutter-mozembed-$(CME_API_VERSION).pc

CLEANFILES = $(pkgconfig_DATA)

