plugin_LTLIBRARIES = libgstdeinterlace2.la

libgstdeinterlace2_la_SOURCES = \
	gstdeinterlace2.c \
	tvtime/greedy.c \
	tvtime/greedyh.c \
	tvtime/vfir.c

#	tvtime/tomsmocomp.c

libgstdeinterlace2_la_CFLAGS = $(GST_CFLAGS) \
	$(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) $(LIBOIL_CFLAGS)
libgstdeinterlace2_la_LIBADD = $(GST_LIBS) \
	$(GST_PLUGINS_BASE_LIBS) -lgstvideo-$(GST_MAJORMINOR) $(GST_BASE_LIBS) $(LIBOIL_LIBS)
libgstdeinterlace2_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

EXTRA_DIST = tvtime/tomsmocomp.c

noinst_HEADERS = \
	gstdeinterlace2.h \
	tvtime/mmx.h \
	tvtime/sse.h \
	tvtime/greedyh.asm \
	tvtime/greedyhmacros.h \
	tvtime/plugins.h \
	tvtime/x86-64_macros.inc \
	tvtime/tomsmocomp/SearchLoop0A.inc \
	tvtime/tomsmocomp/SearchLoopBottom.inc \
	tvtime/tomsmocomp/SearchLoopEdgeA8.inc \
	tvtime/tomsmocomp/SearchLoopEdgeA.inc \
	tvtime/tomsmocomp/SearchLoopOddA2.inc \
	tvtime/tomsmocomp/SearchLoopOddA6.inc \
	tvtime/tomsmocomp/SearchLoopOddAH2.inc \
	tvtime/tomsmocomp/SearchLoopOddAH.inc \
	tvtime/tomsmocomp/SearchLoopOddA.inc \
	tvtime/tomsmocomp/SearchLoopTop.inc \
	tvtime/tomsmocomp/SearchLoopVAH.inc \
	tvtime/tomsmocomp/SearchLoopVA.inc \
	tvtime/tomsmocomp/StrangeBob.inc \
	tvtime/tomsmocomp/TomsMoCompAll2.inc \
	tvtime/tomsmocomp/TomsMoCompAll.inc \
	tvtime/tomsmocomp/tomsmocompmacros.h \
	tvtime/tomsmocomp/WierdBob.inc

