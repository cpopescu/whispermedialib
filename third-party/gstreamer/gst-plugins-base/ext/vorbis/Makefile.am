plugin_LTLIBRARIES = libgstvorbis.la

libgstvorbis_la_SOURCES = vorbis.c \
	vorbisdec.c vorbisenc.c vorbisparse.c vorbistag.c

libgstvorbis_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_CFLAGS) $(VORBIS_CFLAGS) 
## AM_PATH_VORBIS also sets VORBISENC_LIBS
libgstvorbis_la_LIBADD = \
	$(top_builddir)/gst-libs/gst/tag/libgsttag-@GST_MAJORMINOR@.la \
	$(top_builddir)/gst-libs/gst/audio/libgstaudio-@GST_MAJORMINOR@.la \
	$(GST_LIBS) \
	$(VORBIS_LIBS) $(VORBISENC_LIBS)
libgstvorbis_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

noinst_HEADERS = vorbisenc.h vorbisdec.h vorbisparse.h vorbistag.h
