plugin_LTLIBRARIES = libgstosxaudio.la

libgstosxaudio_la_SOURCES = gstosxringbuffer.c		\
			    gstosxaudioelement.c	\
			    gstosxaudiosink.c \
			    gstosxaudiosrc.c

libgstosxaudio_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_CFLAGS) \
	-Wno-deprecated-declarations
libgstosxaudio_la_LIBADD = 			\
	-lgstinterfaces-@GST_MAJORMINOR@	\
	-lgstaudio-@GST_MAJORMINOR@		\
	$(GST_PLUGINS_BASE_LIBS) \
        $(GST_LIBS)
libgstosxaudio_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS) -Wl,-framework -Wl,CoreAudio

noinst_HEADERS = gstosxaudiosink.h	\
		gstosxaudioelement.h	\
		gstosxringbuffer.h	\
		gstosxaudiosrc.h



