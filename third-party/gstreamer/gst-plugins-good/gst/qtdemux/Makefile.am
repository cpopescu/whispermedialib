
plugin_LTLIBRARIES = libgstqtdemux.la

libgstqtdemux_la_CFLAGS = ${GST_CFLAGS} $(GST_PLUGINS_BASE_CFLAGS) 
libgstqtdemux_la_LIBADD = \
	$(GST_PLUGINS_BASE_LIBS) -lgstrtp-@GST_MAJORMINOR@ -lgsttag-@GST_MAJORMINOR@ \
	$(GST_BASE_LIBS) $(ZLIB_LIBS)
libgstqtdemux_la_LDFLAGS = ${GST_PLUGIN_LDFLAGS} 
libgstqtdemux_la_SOURCES = quicktime.c gstrtpxqtdepay.c qtdemux.c qtdemux_types.c qtdemux_dump.c

noinst_HEADERS = \
	qtdemux.h        \
	qtdemux_types.h  \
	qtdemux_dump.h   \
	qtdemux_fourcc.h \
	qtpalette.h      \
	gstrtpxqtdepay.h
