plugin_LTLIBRARIES = libgstvideocrop.la

# Note: we only use defines from gst/video/video.h, but none
# of the functions, so we don't need to link to libgstvideo

libgstvideocrop_la_SOURCES = gstvideocrop.c
libgstvideocrop_la_CFLAGS = $(GST_CFLAGS) $(GST_BASE_CFLAGS) \
	$(GST_PLUGINS_BASE_CFLAGS)
libgstvideocrop_la_LIBADD = $(GST_BASE_LIBS)
libgstvideocrop_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

noinst_HEADERS = gstvideocrop.h
