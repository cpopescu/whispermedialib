plugin_LTLIBRARIES = libgstsouphttpsrc.la

libgstsouphttpsrc_la_SOURCES = gstsouphttpsrc.c

libgstsouphttpsrc_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) $(GST_CFLAGS) $(SOUP_CFLAGS) 
libgstsouphttpsrc_la_LIBADD = $(GST_PLUGINS_BASE_LIBS) -lgsttag-@GST_MAJORMINOR@ $(GST_BASE_LIBS) $(SOUP_LIBS)
libgstsouphttpsrc_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

noinst_HEADERS = gstsouphttpsrc.h
