plugin_LTLIBRARIES = libgstladspa.la

libgstladspa_la_SOURCES = gstsignalprocessor.c gstladspa.c search.c load.c
libgstladspa_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_CFLAGS)
libgstladspa_la_LIBADD = $(GST_PLUGINS_BASE_LIBS) -lgstaudio-$(GST_MAJORMINOR)
libgstladspa_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

noinst_HEADERS = gstsignalprocessor.h gstladspa.h utils.h
