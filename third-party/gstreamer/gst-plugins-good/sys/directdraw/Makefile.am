plugin_LTLIBRARIES = libgstdirectdrawsink.la

libgstdirectdrawsink_la_SOURCES =  gstdirectdrawsink.c gstdirectdrawplugin.c
libgstdirectdrawsink_la_CFLAGS = $(GST_CFLAGS) $(GST_BASE_CFLAGS) \
	$(GST_PLUGINS_BASE_CFLAGS) $(DIRECTDRAW_CFLAGS)
libgstdirectdrawsink_la_LIBADD = $(DIRECTDRAW_LIBS) \
	$(GST_BASE_LIBS) $(GST_PLUGINS_BASE_LIBS) -lgstvideo-$(GST_MAJORMINOR) \
	-lgstinterfaces-$(GST_MAJORMINOR)
libgstdirectdrawsink_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS) $(DIRECTDRAW_LDFLAGS)

noinst_HEADERS= gstdirectdrawsink.h
