plugin_LTLIBRARIES = libgstogg.la

libgstogg_la_SOURCES = \
	gstogg.c \
	gstoggdemux.c \
	gstoggmux.c \
	gstogmparse.c \
	gstoggaviparse.c \
	gstoggparse.c

noinst_HEADERS = \
	gstoggdemux.h gstoggmux.h

libgstogg_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_CFLAGS) $(OGG_CFLAGS) 
libgstogg_la_LIBADD = \
	$(top_builddir)/gst-libs/gst/riff/libgstriff-$(GST_MAJORMINOR).la \
	$(top_builddir)/gst-libs/gst/tag/libgsttag-$(GST_MAJORMINOR).la \
	$(GST_BASE_LIBS) \
	$(OGG_LIBS)
libgstogg_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
