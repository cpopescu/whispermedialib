plugindir = $(libdir)/gstreamer-@GST_MAJORMINOR@

plugin_LTLIBRARIES = libgstdfbvideosink.la

libgstdfbvideosink_la_SOURCES =  dfbvideosink.c
libgstdfbvideosink_la_CFLAGS = $(GST_CFLAGS) $(GST_PLUGINS_BASE_CFLAGS) \
			       $(DIRECTFB_CFLAGS)
libgstdfbvideosink_la_LIBADD = $(GST_PLUGINS_BASE_LIBS) \
			       -lgstvideo-$(GST_MAJORMINOR) \
			       -lgstinterfaces-$(GST_MAJORMINOR) \
			       $(DIRECTFB_LIBS) $(LIBOIL_LIBS)
libgstdfbvideosink_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

noinst_HEADERS = dfbvideosink.h

noinst_PROGRAMS = dfb-example
dfb_example_CFLAGS = $(GST_CFLAGS) $(DIRECTFB_CFLAGS)
dfb_example_LDADD = $(GST_LIBS) $(DIRECTFB_LIBS)
