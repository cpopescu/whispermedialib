plugin_LTLIBRARIES = libgstesd.la

libgstesd_la_SOURCES = esdsink.c gstesd.c
libgstesd_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_CFLAGS) $(ESD_CFLAGS)
libgstesd_la_LIBADD = \
	$(GST_PLUGINS_BASE_LIBS) -lgstaudio-$(GST_MAJORMINOR) \
	$(ESD_LIBS)
libgstesd_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

noinst_HEADERS = esdsink.h esdmon.h
EXTRA_DIST =
