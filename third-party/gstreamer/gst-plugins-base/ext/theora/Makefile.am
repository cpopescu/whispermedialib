plugin_LTLIBRARIES = libgsttheora.la

noinst_HEADERS = gsttheoraenc.h gsttheoradec.h gsttheoraparse.h

libgsttheora_la_SOURCES = theora.c theoraenc.c theoradec.c theoraparse.c
libgsttheora_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_CFLAGS) $(THEORA_CFLAGS)
libgsttheora_la_LIBADD = \
	$(top_builddir)/gst-libs/gst/tag/libgsttag-$(GST_MAJORMINOR).la \
	$(GST_LIBS) \
	$(THEORA_LIBS)
libgsttheora_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
