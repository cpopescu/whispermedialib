plugin_LTLIBRARIES = libgstudp.la

# variables used for enum/marshal generation
glib_enum_headers = gstudp.h
glib_enum_define = GST_UDP
glib_enum_prefix = gst_udp

include $(top_srcdir)/common/glib-gen.mak

built_sources = gstudp-enumtypes.c gstudp-marshal.c
built_headers = gstudp-enumtypes.h gstudp-marshal.h

BUILT_SOURCES = $(built_sources) $(built_headers)

libgstudp_la_SOURCES = gstudp.c gstudpsrc.c gstudpsink.c gstmultiudpsink.c gstdynudpsink.c gstudpnetutils.c

# adding -D_GNU_SOURCE to get non-POSIX extensions like EAI_ADDRFAMILY
# with glibc >= 2.8 when including netdb.h (see glibc sources bug 6452)
libgstudp_la_CFLAGS = $(GST_CFLAGS) $(GST_PLUGINS_BASE_CFLAGS) -D_GNU_SOURCE
libgstudp_la_LIBADD = $(GST_BASE_LIBS) $(GST_PLUGINS_BASE_LIBS) $(WIN32_LIBS)\
		      -lgstnetbuffer-@GST_MAJORMINOR@
libgstudp_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

nodist_libgstudp_la_SOURCES = \
        $(built_sources)

noinst_HEADERS = gstudpsink.h gstudpsrc.h gstudp.h gstmultiudpsink.h gstdynudpsink.h gstudpnetutils.h

EXTRA_DIST = README gstudp-marshal.list

CLEANFILES = $(BUILT_SOURCES)

