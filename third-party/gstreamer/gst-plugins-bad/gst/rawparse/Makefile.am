
plugin_LTLIBRARIES = libgstrawparse.la

libgstrawparse_la_SOURCES = gstrawparse.c gstaudioparse.c gstvideoparse.c plugin.c
libgstrawparse_la_CFLAGS = $(GST_CFLAGS) $(GST_PLUGINS_BASE_CFLAGS)
libgstrawparse_la_LIBADD = $(GST_LIBS) $(GST_BASE_LIBS)
libgstrawparse_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

noinst_HEADERS = \
	gstaudioparse.h \
	gstrawparse.h \
	gstvideoparse.h
