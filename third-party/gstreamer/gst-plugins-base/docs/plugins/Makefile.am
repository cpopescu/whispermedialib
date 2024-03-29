GST_DOC_SCANOBJ = $(top_srcdir)/common/gstdoc-scangobj

## Process this file with automake to produce Makefile.in

# The name of the module, e.g. 'glib'.
#DOC_MODULE=gst-plugins-libs-@GST_MAJORMINOR@
MODULE=gst-plugins-base
DOC_MODULE=$(MODULE)-plugins

# for upload.mak
DOC=$(MODULE)-plugins
FORMATS=html
html: html-build.stamp
include $(top_srcdir)/common/upload.mak

# generated basefiles
#basefiles = \
##		$(DOC_MODULE).types \
#		$(DOC_MODULE)-sections.txt \
#		$(DOC_MODULE)-docs.sgml

# ugly hack to make -unused.sgml work
#unused-build.stamp:
#	BUILDDIR=`pwd` && \
#	cd $(srcdir)/tmpl && \
#	ln -sf gstreamer-libs-unused.sgml \
#		$$BUILDDIR/tmpl/gstreamer-libs-@GST_MAJORMINOR@-unused.sgml
#	touch unused-build.stamp

# these rules are added to create parallel docs using GST_MAJORMINOR
#$(basefiles): gstreamer-libs-@GST_MAJORMINOR@%: gstreamer-libs%
#	cp $< $@

#CLEANFILES = $(basefiles)

# The top-level SGML file. Change it if you want.
DOC_MAIN_SGML_FILE=$(DOC_MODULE)-docs.sgml

# The directory containing the source code. Relative to $(top_srcdir).
# gtk-doc will search all .c & .h files beneath here for inline comments
# documenting functions and macros.
DOC_SOURCE_DIR = $(top_srcdir)

# Extra options to supply to gtkdoc-scan.
SCAN_OPTIONS=

# Extra options to supply to gtkdoc-mkdb.
MKDB_OPTIONS=--sgml-mode

# Extra options to supply to gtkdoc-fixref.
FIXXREF_OPTIONS=--extra-dir=$(GLIB_PREFIX)/share/gtk-doc/html \
	--extra-dir=$(GST_PREFIX)/share/gtk-doc/html \
        	--extra-dir=$(datadir)/gtk-doc/html

# Used for dependencies.
HFILE_GLOB=$(DOC_SOURCE_DIR)/*/*/*.h
CFILE_GLOB=$(DOC_SOURCE_DIR)/*/*/*.c

# this is a wingo addition
# thomasvs: another nice wingo addition would be an explanation on why
# this is useful ;)

SCANOBJ_DEPS =

# Header files to ignore when scanning.
IGNORE_HFILES = avcodec.h gstffmpegcodecmap.h dsputil.h
IGNORE_CFILES = utils.c mem.c imgconvert.c

# we add all .h files of elements that have signals/args we want
# sadly this also pulls in the private methods - maybe we should
# move those around in the source ?
# also, we should add some stuff here conditionally based on whether
# or not the plugin will actually build
# but I'm not sure about that - it might be this Just Works given that
# the registry won't have the element

EXTRA_HFILES = \
	$(top_srcdir)/ext/alsa/gstalsamixerelement.h \
	$(top_srcdir)/ext/alsa/gstalsasink.h \
	$(top_srcdir)/ext/alsa/gstalsasrc.h \
	$(top_srcdir)/ext/cdparanoia/gstcdparanoiasrc.h \
	$(top_srcdir)/ext/gio/gstgiosink.h \
	$(top_srcdir)/ext/gio/gstgiosrc.h \
	$(top_srcdir)/ext/gio/gstgiostreamsink.h \
	$(top_srcdir)/ext/gio/gstgiostreamsrc.h \
	$(top_srcdir)/ext/gnomevfs/gstgnomevfssink.h \
	$(top_srcdir)/ext/gnomevfs/gstgnomevfssrc.h \
	$(top_srcdir)/ext/ogg/gstoggdemux.h \
	$(top_srcdir)/ext/ogg/gstoggmux.h \
	$(top_srcdir)/ext/pango/gstclockoverlay.h \
	$(top_srcdir)/ext/pango/gsttextoverlay.h \
	$(top_srcdir)/ext/pango/gsttextrender.h \
	$(top_srcdir)/ext/pango/gsttimeoverlay.h \
	$(top_srcdir)/ext/theora/gsttheoradec.h \
	$(top_srcdir)/ext/theora/gsttheoraenc.h \
	$(top_srcdir)/ext/theora/gsttheoraparse.h \
	$(top_srcdir)/ext/vorbis/vorbisdec.h \
	$(top_srcdir)/ext/vorbis/vorbisenc.h \
	$(top_srcdir)/ext/vorbis/vorbisparse.h \
	$(top_srcdir)/ext/vorbis/vorbistag.h \
	$(top_srcdir)/gst/adder/gstadder.h \
	$(top_srcdir)/gst/audioconvert/audioconvert.h \
	$(top_srcdir)/gst/audioconvert/gstaudioconvert.h \
	$(top_srcdir)/gst/audioresample/gstaudioresample.h \
	$(top_srcdir)/gst/audiotestsrc/gstaudiotestsrc.h \
	$(top_srcdir)/gst/ffmpegcolorspace/gstffmpegcolorspace.h \
	$(top_srcdir)/gst/gdp/gstgdpdepay.h \
	$(top_srcdir)/gst/gdp/gstgdppay.h \
	$(top_srcdir)/gst/playback/gstplay-enum.h \
	$(top_srcdir)/gst/tcp/gstmultifdsink.h \
	$(top_srcdir)/gst/tcp/gsttcpserversink.h \
	$(top_srcdir)/gst/tcp/gsttcp-enumtypes.h \
	$(top_srcdir)/gst/tcp/gsttcp.h \
	$(top_srcdir)/gst/videorate/gstvideorate.h \
	$(top_srcdir)/gst/videoscale/gstvideoscale.h \
	$(top_srcdir)/gst/videotestsrc/gstvideotestsrc.h \
	$(top_srcdir)/gst/volume/gstvolume.h \
	$(top_srcdir)/sys/ximage/ximagesink.h \
	$(top_srcdir)/sys/xvimage/xvimagesink.h

# Images to copy into HTML directory.
HTML_IMAGES =

# Extra SGML files that are included by $(DOC_MAIN_SGML_FILE).
content_files =

# Other files to distribute.
extra_files =

# CFLAGS and LDFLAGS for compiling scan program. Only needed if your app/lib
# contains GtkObjects/GObjects and you want to document signals and properties.
GTKDOC_CFLAGS = $(GST_BASE_CFLAGS) -I$(top_builddir) -I$(top_builddir)/gst-libs
GTKDOC_LIBS = $(SCANOBJ_DEPS) $(GST_BASE_LIBS)

GTKDOC_CC=$(LIBTOOL) --mode=compile $(CC)
GTKDOC_LD=$(LIBTOOL) --mode=link $(CC)

# If you need to override some of the declarations, place them in this file
# and uncomment this line.
#DOC_OVERRIDES = $(DOC_MODULE)-overrides.txt
DOC_OVERRIDES =

include $(top_srcdir)/common/gtk-doc-plugins.mak
