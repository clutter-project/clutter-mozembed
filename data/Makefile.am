
jardata = \
	locale/branding/brand.dtd \
	locale/branding/brand.properties

resdir = $(pkgdatadir)/chrome
res_DATA = \
	branding.jar \
	branding.manifest

branding.jar: $(jardata) branding.manifest
	rm -f $@ && zip $@ $(jardata)

MAINTAINERCLEANFILES = Makefile.in
EXTRA_DIST = $(jardata) branding.jar branding.manifest
CLEANFILES = branding.jar

