plugin_LTLIBRARIES = libgstapetag.la

libgstapetag_la_SOURCES = gstapedemux.c
libgstapetag_la_CFLAGS = \
	-I$(top_srcdir)/gst-libs \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_BASE_CFLAGS) \
	$(GST_CFLAGS)
libgstapetag_la_LIBADD = \
	$(GST_PLUGINS_BASE_LIBS) -lgsttag-@GST_MAJORMINOR@ \
	$(GST_BASE_LIBS) \
	$(GST_LIBS)
libgstapetag_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

noinst_HEADERS = gstapedemux.h
