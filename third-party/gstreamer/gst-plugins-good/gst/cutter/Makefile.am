plugin_LTLIBRARIES = libgstcutter.la

libgstcutter_la_SOURCES = gstcutter.c
libgstcutter_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) $(GST_CFLAGS)
libgstcutter_la_LIBADD = $(GST_PLUGINS_BASE_LIBS) -lgstaudio-$(GST_MAJORMINOR) $(GST_BASE_LIBS) $(LIBM)
libgstcutter_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

noinst_HEADERS = gstcutter.h filter.func

EXTRA_DIST = README
