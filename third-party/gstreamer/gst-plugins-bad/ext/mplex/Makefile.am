plugin_LTLIBRARIES = libgstmplex.la

libgstmplex_la_SOURCES = \
	gstmplex.cc \
	gstmplexibitstream.cc \
	gstmplexjob.cc \
	gstmplexoutputstream.cc

libgstmplex_la_CXXFLAGS = \
	$(GST_PLUGINS_BASE_CFLAGS) $(GST_CXXFLAGS) $(MPLEX_CFLAGS)
libgstmplex_la_LIBADD = \
	$(GST_PLUGINS_BASE_LIBS) $(GST_BASE_LIBS) $(GST_LIBS) $(MPLEX_LIBS)
libgstmplex_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

noinst_HEADERS = \
	gstmplex.hh \
	gstmplexibitstream.hh \
	gstmplexjob.hh \
	gstmplexoutputstream.hh
