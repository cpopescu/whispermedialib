plugin_LTLIBRARIES = libgstmpeg2dec.la

libgstmpeg2dec_la_SOURCES = gstmpeg2dec.c
libgstmpeg2dec_la_CFLAGS = $(GST_CFLAGS) $(MPEG2DEC_CFLAGS)
libgstmpeg2dec_la_LIBADD = $(MPEG2DEC_LIBS) $(GST_LIBS)
libgstmpeg2dec_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

noinst_HEADERS = gstmpeg2dec.h
