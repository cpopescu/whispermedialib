plugin_LTLIBRARIES = libgstcacasink.la

libgstcacasink_la_SOURCES = gstcacasink.c
libgstcacasink_la_CFLAGS = \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_CFLAGS) \
	$(LIBCACA_CFLAGS)
libgstcacasink_la_LIBADD = \
	$(GST_PLUGINS_BASE_LIBS) \
	$(GST_BASE_LIBS) \
	$(LIBCACA_LIBS)
libgstcacasink_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

noinst_HEADERS = gstcacasink.h
