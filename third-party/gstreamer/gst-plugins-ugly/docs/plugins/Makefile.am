GST_DOC_SCANOBJ = $(top_srcdir)/common/gstdoc-scangobj

## Process this file with automake to produce Makefile.in

# The name of the module, e.g. 'glib'.
#DOC_MODULE=gst-plugins-libs-@GST_MAJORMINOR@
MODULE=gst-plugins-ugly
DOC_MODULE=$(MODULE)-plugins

# for upload.mak
DOC=$(MODULE)-plugins
FORMATS=html
html: html-build.stamp
include $(top_srcdir)/common/upload-doc.mak

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

# FIXME :
# there's something wrong with gstreamer-sections.txt not being in the dist
# maybe it doesn't resolve; we're adding it below for now
#EXTRA_DIST = gstreamer.types.in gstreamer.hierarchy $(DOC_MODULE)-sections.txt gstreamer-sections.txt $(DOC_MAIN_SGML_FILE)

# Extra options to supply to gtkdoc-mkdb.
MKDB_OPTIONS=--sgml-mode --source-suffixes=c,h,cc,m

# Extra options to supply to gtkdoc-fixref.
FIXXREF_OPTIONS=--extra-dir=$(GLIB_PREFIX)/share/gtk-doc/html \
	--extra-dir=$(GST_PREFIX)/share/gtk-doc/html
	--extra-dir=$(GSTPB_PREFIX)/share/gtk-doc/html

# Used for dependencies.
HFILE_GLOB=$(DOC_SOURCE_DIR)/*/*/*.h
CFILE_GLOB=$(DOC_SOURCE_DIR)/*/*/*.c $(DOC_SOURCE_DIR)/*/*/*.cc

# this is a wingo addition
# thomasvs: another nice wingo addition would be an explanation on why
# this is useful ;)

SCANOBJ_DEPS =

# Header files to ignore when scanning.
IGNORE_HFILES = 
IGNORE_CFILES =

# we add all .h files of elements that have signals/args we want
# sadly this also pulls in the private methods - maybe we should
# move those around in the source ?
# also, we should add some stuff here conditionally based on whether
# or not the plugin will actually build
# but I'm not sure about that - it might be this Just Works given that
# the registry won't have the element

# FIXME: not ported yet
#	$(top_srcdir)/ext/gnomevfs/gstgnomevfssink.c 

EXTRA_HFILES = \
	$(top_srcdir)/ext/a52dec/gsta52dec.h \
	$(top_srcdir)/ext/amrnb/amrnbdec.h \
	$(top_srcdir)/ext/amrnb/amrnbenc.h \
	$(top_srcdir)/ext/amrnb/amrnbparse.h \
	$(top_srcdir)/ext/cdio/gstcdiocddasrc.h \
	$(top_srcdir)/ext/lame/gstlame.h \
	$(top_srcdir)/ext/mad/gstmad.h \
	$(top_srcdir)/ext/sidplay/gstsiddec.h \
	$(top_srcdir)/gst/asfdemux/gstrtspwms.h \
	$(top_srcdir)/gst/mpegaudioparse/gstxingmux.h \
	$(top_srcdir)/gst/realmedia/rademux.h \
	$(top_srcdir)/gst/realmedia/rdtmanager.h \
	$(top_srcdir)/gst/realmedia/rtspreal.h \
	$(top_srcdir)/gst/synaesthesia/gstsynaesthesia.h

# Images to copy into HTML directory.
HTML_IMAGES =

# Extra SGML files that are included by $(DOC_MAIN_SGML_FILE).
content_files =

# Other files to distribute.
extra_files =

# CFLAGS and LDFLAGS for compiling scan program. Only needed if your app/lib
# contains GtkObjects/GObjects and you want to document signals and properties.
GTKDOC_CFLAGS = $(GST_BASE_CFLAGS) -I$(top_builddir)
GTKDOC_LIBS = $(SCANOBJ_DEPS) $(GST_BASE_LIBS)

GTKDOC_CC=$(LIBTOOL) --mode=compile $(CC)
GTKDOC_LD=$(LIBTOOL) --mode=link $(CC)

# If you need to override some of the declarations, place them in this file
# and uncomment this line.
#DOC_OVERRIDES = $(DOC_MODULE)-overrides.txt
DOC_OVERRIDES =

include $(top_srcdir)/common/gtk-doc-plugins.mak
