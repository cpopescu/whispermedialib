
plugin_LTLIBRARIES = libgstspectrum.la

libgstspectrum_la_SOURCES = gstspectrum.c
libgstspectrum_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) \
	$(GST_CFLAGS)
libgstspectrum_la_LIBADD = $(GST_PLUGINS_BASE_LIBS) \
	-lgstfft-$(GST_MAJORMINOR) -lgstaudio-$(GST_MAJORMINOR) \
	$(GST_BASE_LIBS) $(GST_LIBS) $(LIBM)
libgstspectrum_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

noinst_HEADERS = gstspectrum.h

EXTRA_DIST = README
