plugin_LTLIBRARIES = libgstdtsdec.la

libgstdtsdec_la_SOURCES = gstdtsdec.c
libgstdtsdec_la_CFLAGS = $(GST_CFLAGS) $(LIBOIL_CFLAGS) $(GST_PLUGINS_BASE_CFLAGS)
libgstdtsdec_la_LIBADD = $(DTS_LIBS) $(LIBOIL_LIBS) $(GST_PLUGINS_BASE_LIBS)  \
	-lgstaudio-@GST_MAJORMINOR@
libgstdtsdec_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

noinst_HEADERS = gstdtsdec.h
