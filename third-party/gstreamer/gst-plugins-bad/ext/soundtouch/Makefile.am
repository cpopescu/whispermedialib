plugin_LTLIBRARIES = libgstsoundtouch.la

libgstsoundtouch_la_SOURCES = \
	plugin.c \
	gstpitch.cc \
	gstbpmdetect.cc

libgstsoundtouch_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) $(GST_CFLAGS) $(SOUNDTOUCH_CFLAGS)
libgstsoundtouch_la_CXXFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) $(GST_CXXFLAGS) $(SOUNDTOUCH_CFLAGS)
libgstsoundtouch_la_LIBADD = $(GST_PLUGINS_BASE_LIBS) -lgstaudio-$(GST_MAJORMINOR) $(GST_BASE_LIBS) $(GST_CONTROLLER_LIBS) $(GST_LIBS) $(SOUNDTOUCH_LIBS) -lBPM $(LIBM)
libgstsoundtouch_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

noinst_HEADERS = gstpitch.hh gstbpmdetect.hh
