
jardata = \
	locale/branding/brand.dtd \
	locale/branding/brand.properties

chromedir = $(pkgdatadir)/chrome
chrome_DATA = \
	branding.jar \
	branding.manifest

resdir = $(pkgdatadir)
res_DATA = \
	mimeTypes.rdf

branding.jar: $(jardata) branding.manifest
	rm -f $@ && zip $@ $(jardata)

MAINTAINERCLEANFILES = Makefile.in
EXTRA_DIST = $(jardata) branding.jar branding.manifest
CLEANFILES = branding.jar

