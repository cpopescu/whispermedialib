plugin_LTLIBRARIES = libgstvideotestsrc.la

libgstvideotestsrc_la_SOURCES = \
			gstvideotestsrc.c \
			videotestsrc.c

libgstvideotestsrc_la_CFLAGS = $(GST_CFLAGS) $(LIBOIL_CFLAGS)
libgstvideotestsrc_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstvideotestsrc_la_LIBADD = $(GST_LIBS) $(GST_BASE_LIBS) $(LIBOIL_LIBS)

noinst_HEADERS = gstvideotestsrc.h videotestsrc.h
