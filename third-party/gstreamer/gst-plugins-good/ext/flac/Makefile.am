plugin_LTLIBRARIES = libgstflac.la

libgstflac_la_SOURCES = gstflac.c gstflacdec.c gstflacenc.c
libgstflac_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) $(GST_CFLAGS)
libgstflac_la_LIBADD = \
	$(GST_PLUGINS_BASE_LIBS) -lgsttag-$(GST_MAJORMINOR) \
	-lgstaudio-$(GST_MAJORMINOR) \
	$(GST_BASE_LIBS) $(GST_LIBS) $(FLAC_LIBS)
libgstflac_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

noinst_HEADERS = gstflacenc.h gstflacdec.h
