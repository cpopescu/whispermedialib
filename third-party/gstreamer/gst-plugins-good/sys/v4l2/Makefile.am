plugin_LTLIBRARIES = libgstvideo4linux2.la

# overlay is still not supported in current implementation
# if USE_XVIDEO
#xv_source = gstv4l2xoverlay.c
#xv_libs = $(X_LIBS) $(XVIDEO_LIBS)
#else
#xv_source =
#xv_libs =
#endif

libgstvideo4linux2_la_SOURCES = gstv4l2.c \
				gstv4l2colorbalance.c \
				gstv4l2object.c \
				gstv4l2src.c \
				gstv4l2tuner.c \
				gstv4l2vidorient.c \
				v4l2_calls.c \
				v4l2src_calls.c \
				$(xv_source)

libgstvideo4linux2_la_CFLAGS =   $(GST_PLUGINS_BASE_CFLAGS) \
				 $(GST_BASE_CFLAGS) \
				 $(GST_CFLAGS) \
				 $(X_CFLAGS)
			        
libgstvideo4linux2_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

libgstvideo4linux2_la_LIBADD =   $(GST_PLUGINS_BASE_LIBS) \
				 $(GST_BASE_LIBS) \
				 -lgstinterfaces-$(GST_MAJORMINOR) \
				 $(GST_LIBS) \
				 $(xv_libs)

noinst_HEADERS = gstv4l2object.h v4l2_calls.h \
		 gstv4l2src.h v4l2src_calls.h \
		 gstv4l2tuner.h gstv4l2xoverlay.h \
		 gstv4l2colorbalance.h gstv4l2vidorient.h
