
plugin_LTLIBRARIES = libgstinterleave.la

libgstinterleave_la_SOURCES = plugin.c interleave.c deinterleave.c
libgstinterleave_la_CFLAGS = $(GST_CFLAGS) $(GST_BASE_CFLAGS) $(GST_PLUGINS_BASE_CFLAGS)
libgstinterleave_la_LIBADD = $(GST_LIBS) $(GST_BASE_LIBS) $(GST_PLUGINS_BASE_LIBS) -lgstaudio-$(GST_MAJORMINOR)
libgstinterleave_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

noinst_HEADERS = plugin.h interleave.h deinterleave.h
