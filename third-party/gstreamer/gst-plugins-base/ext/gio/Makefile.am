# plugindir is set in configure

plugin_LTLIBRARIES = libgstgio.la

# sources used to compile this plug-in
libgstgio_la_SOURCES = \
		gstgio.c \
		gstgiobasesink.c \
		gstgiobasesrc.c \
		gstgiosink.c \
		gstgiosrc.c \
		gstgiostreamsink.c \
		gstgiostreamsrc.c

libgstgio_la_CFLAGS = $(GST_CFLAGS) $(GST_PLUGINS_BASE_CFLAGS) $(GIO_CFLAGS)
libgstgio_la_LIBADD = $(GST_LIBS) $(GST_BASE_LIBS) $(GIO_LIBS)
libgstgio_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS) $(GIO_LDFLAGS)

# headers we need but don't want installed
noinst_HEADERS = \
		gstgio.h \
		gstgiobasesink.h \
		gstgiobasesrc.h \
		gstgiosink.h \
		gstgiosrc.h \
		gstgiostreamsink.h \
		gstgiostreamsrc.h

