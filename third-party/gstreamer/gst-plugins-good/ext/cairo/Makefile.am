plugin_LTLIBRARIES = libgstcairo.la

noinst_HEADERS = gsttimeoverlay.h gsttextoverlay.h

libgstcairo_la_SOURCES = \
	gstcairo.c \
	gsttimeoverlay.c \
	gsttextoverlay.c

libgstcairo_la_CFLAGS = \
        $(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_BASE_CFLAGS) \
	$(GST_CFLAGS) $(CAIRO_CFLAGS)
libgstcairo_la_LIBADD = \
  $(GST_BASE_LIBS) \
  $(GST_LIBS) $(CAIRO_LIBS) $(LIBM)
libgstcairo_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
