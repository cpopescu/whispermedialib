# FIXME: clean up this crap
OBJC=gcc

plugin_LTLIBRARIES = libgstosxvideosink.la

libgstosxvideosink_la_SOURCES = osxvideosink.m cocoawindow.m 
libgstosxvideosink_la_CFLAGS = $(GST_CFLAGS) $(GST_BASE_CFLAGS) \
	$(GST_PLUGINS_BASE_CFLAGS)
libgstosxvideosink_la_LIBADD =  \
	$(GST_BASE_LIBS) $(GST_PLUGINS_BASE_LIBS) -lgstvideo-$(GST_MAJORMINOR) \
	-lgstinterfaces-$(GST_MAJORMINOR)

libgstosxvideosink_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS) -Wl,-framework -Wl,Cocoa -Wl,-framework -Wl,QuickTime -Wl,-framework -Wl,OpenGL

AM_OBJCFLAGS=$(CFLAGS) $(GST_CFLAGS) $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS)

noinst_HEADERS = osxvideosink.h cocoawindow.h
