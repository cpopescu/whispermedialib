plugin_LTLIBRARIES = libgstsid.la

libgstsid_la_SOURCES = gstsiddec.cc
libgstsid_la_CXXFLAGS = $(GST_CFLAGS) $(GST_BASE_CFLAGS) $(GST_PLUGINS_BASE_CFLAGS) $(GST_CXXFLAGS) $(SIDPLAY_CFLAGS)
libgstsid_la_LIBADD = $(GST_LIBS) $(GST_BASE_LIBS) $(GST_PLUGINS_BASE_LIBS) $(SIDPLAY_LIBS)
libgstsid_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

noinst_HEADERS = gstsiddec.h
