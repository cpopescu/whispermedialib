plugin_LTLIBRARIES = libgstbayer.la

libgstbayer_la_SOURCES = gstbayer.c gstbayer2rgb.c
libgstbayer_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) \
    $(GST_CFLAGS)
libgstbayer_la_LIBADD = $(GST_PLUGINS_BASE_LIBS) -lgstvideo-$(GST_MAJORMINOR) \
    $(GST_BASE_LIBS)
libgstbayer_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

