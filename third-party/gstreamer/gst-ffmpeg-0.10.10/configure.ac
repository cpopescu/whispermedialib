AC_PREREQ(2.52)

dnl initialize autoconf
dnl when going to/from release please set the nano (fourth number) right !
dnl releases only do Wall, cvs and prerelease does Werror too
AC_INIT(GStreamer FFMpeg, 0.10.10,
    http://bugzilla.gnome.org/enter_bug.cgi?product=GStreamer,
    gst-ffmpeg)

AG_GST_INIT

dnl initialize automake
AM_INIT_AUTOMAKE

dnl define PACKAGE_VERSION_* variables
AS_VERSION

dnl check if this is a release version
AS_NANO(GST_CVS="no", GST_CVS="yes")

dnl can autoconf find the source ?
AC_CONFIG_SRCDIR([ext/ffmpeg/gstffmpeg.c])

dnl define the output header for config
AM_CONFIG_HEADER([config.h])

dnl AM_MAINTAINER_MODE only provides the option to configure to enable it
AM_MAINTAINER_MODE

dnl sets host_* variables
AC_CANONICAL_HOST

dnl our libraries and install dirs use major.minor as a version
GST_MAJORMINOR=$PACKAGE_VERSION_MAJOR.$PACKAGE_VERSION_MINOR
dnl we override it here if we need to for the release candidate of new series
GST_MAJORMINOR=0.10
AC_SUBST(GST_MAJORMINOR)

AC_LIBTOOL_WIN32_DLL
AM_PROG_LIBTOOL

dnl *** required versions of GStreamer stuff ***
GST_REQ=0.10.22
LIBOIL_MAJORMINOR=0.3
LIBOIL_REQ=0.3.6

dnl *** autotools stuff ****

dnl allow for different autotools
AS_AUTOTOOLS_ALTERNATE

dnl Add parameters for aclocal
AC_SUBST(ACLOCAL_AMFLAGS, "-I m4 -I common/m4")

dnl *** check for arguments to configure ***

AG_GST_ARG_WITH_PKG_CONFIG_PATH
AG_GST_ARG_WITH_PACKAGE_NAME
AG_GST_ARG_WITH_PACKAGE_ORIGIN
AG_GST_ARG_VALGRIND

dnl *** checks for platform ***

dnl * hardware/architecture *

dnl common/m4/gst-arch.m4
dnl check CPU type
AG_GST_ARCH

dnl *** checks for programs ***

dnl find a compiler
AC_PROG_CC

# FIXME: are these two lines needed ?
AM_PROG_CC_STDC
AS="${CC}"

dnl check if the compiler supports '-c' and '-o' options
AM_PROG_CC_C_O

AC_PATH_PROG(VALGRIND_PATH, valgrind, no)
AM_CONDITIONAL(HAVE_VALGRIND, test ! "x$VALGRIND_PATH" = "xno")

dnl check for documentation tools
AG_GST_DOCBOOK_CHECK
GTK_DOC_CHECK([1.3])
AS_PATH_PYTHON([2.1])
AG_GST_PLUGIN_DOCS([1.3],[2.1])

dnl *** checks for libraries ***

dnl check for libm, for sin()
AC_CHECK_LIBM
AC_SUBST(LIBM)

dnl *** checks for header files ***

dnl check if we have ANSI C header files
AC_HEADER_STDC

dnl *** checks for types/defines ***

dnl *** checks for structures ***

dnl *** checks for compiler characteristics ***

dnl *** checks for library functions ***

dnl *** checks for dependancy libraries ***

dnl checks for gstreamer
dnl uninstalled is selected preferentially -- see pkg-config(1)
AG_GST_CHECK_GST($GST_MAJORMINOR, [$GST_REQ])
AG_GST_CHECK_GST_BASE($GST_MAJORMINOR, [$GST_REQ])
AG_GST_CHECK_GST_PLUGINS_BASE($GST_MAJORMINOR, [$GST_REQ])
AG_GST_CHECK_GST_CHECK($GST_MAJORMINOR, [$GST_REQ], no)
AM_CONDITIONAL(HAVE_GST_CHECK, test "x$HAVE_GST_CHECK" = "xyes")

AC_MSG_NOTICE(Using GStreamer Core Plugins in $GST_PLUGINS_DIR)
AC_MSG_NOTICE(Using GStreamer Base Plugins in $GSTPB_PLUGINS_DIR)

dnl liboil is required for cpu detection for libpostproc
dnl FIXME : In theory we should be able to compile libpostproc with cpudetect
dnl capabilities, which would enable us to get rid of this
PKG_CHECK_MODULES(LIBOIL, liboil-$LIBOIL_MAJORMINOR >= $LIBOIL_REQ, HAVE_LIBOIL=yes, HAVE_LIBOIL=no)
if test "x$HAVE_LIBOIL" != "xyes"
then
  AC_MSG_ERROR([liboil-$LIBOIL_REQ or later is required])
  AC_ERROR
fi

AC_SUBST(LIBOIL_CFLAGS)
AC_SUBST(LIBOIL_LIBS)

dnl *** set variables based on configure arguments ***

dnl set location of plugin directory
AG_GST_SET_PLUGINDIR

dnl define an ERROR_CFLAGS Makefile variable
AG_GST_SET_ERROR_CFLAGS($GST_CVS)

dnl define correct level for debugging messages
AG_GST_SET_LEVEL_DEFAULT($GST_CVS)

dnl *** finalize CFLAGS, LDFLAGS, LIBS ***

dnl Overview:
dnl GST_OPTION_CFLAGS:  common flags for profiling, debugging, errors, ...
dnl GST_*:              flags shared by built objects to link against GStreamer
dnl GST_ALL_LDFLAGS:    linker flags shared by all
dnl GST_LIB_LDFLAGS:    additional linker flags for all libaries
dnl GST_LT_LDFLAGS:     library versioning of our libraries
dnl GST_PLUGIN_LDFLAGS: flags to be used for all plugins

dnl GST_OPTION_CFLAGS
if test "x$USE_DEBUG" = xyes; then
   PROFILE_CFLAGS="-g"
fi
AC_SUBST(PROFILE_CFLAGS)

if test "x$GST_CVS" = "xyes"; then
  DEPRECATED_CFLAGS="-DGST_DISABLE_DEPRECATED"
else
  DEPRECATED_CFLAGS=""
fi
AC_SUBST(DEPRECATED_CFLAGS)

dnl every flag in GST_OPTION_CFLAGS can be overridden at make time
GST_OPTION_CFLAGS="\$(ERROR_CFLAGS) \$(DEBUG_CFLAGS) \$(PROFILE_CFLAGS) \$(GCOV_CFLAGS) \$(OPT_CFLAGS) \$(DEPRECATED_CFLAGS)"

AC_SUBST(GST_OPTION_CFLAGS)

dnl FIXME: do we want to rename to GST_ALL_* ?
dnl prefer internal headers to already installed ones
dnl also add builddir include for enumtypes and marshal
dnl add GST_OPTION_CFLAGS, but overridable
GST_CFLAGS="-I\$(top_srcdir)/gst-libs -I\$(top_builddir)/gst-libs $GST_CFLAGS \$(GST_OPTION_CFLAGS)"
AC_SUBST(GST_CFLAGS)
AC_SUBST(GST_LIBS)

GST_ALL_LDFLAGS="-no-undefined"
AC_SUBST(GST_ALL_LDFLAGS)

dnl this really should only contain flags, not libs - they get added before
dnl whatevertarget_LIBS and -L flags here affect the rest of the linking
GST_PLUGIN_LDFLAGS="-module -avoid-version -export-symbols-regex '^[_]*gst_plugin_desc\$\$' $GST_ALL_LDFLAGS"
AC_SUBST(GST_PLUGIN_LDFLAGS)

dnl Add MacOSX specific flags
AC_CANONICAL_HOST
case $host_os in
     darwin*)
	DARWIN_LDFLAGS="-Wl,-read_only_relocs,suppress"
	;;
     *)
        DARWIN_LDFLAGS=""
	;;
esac
AC_SUBST(DARWIN_LDFLAGS)

dnl *** Check for bz2
AG_GST_CHECK_LIBHEADER(BZ2, bz2, BZ2_bzlibVersion, , bzlib.h, have_bz2=yes, have_bz2=no)
AM_CONDITIONAL(HAVE_BZ2, test "x$have_bz2" = "xyes")
if test "x$have_bz2" = "xno"; then
  AC_WARN([libbz2 not found, matroska demuxer will not be able to read bz2 tracks])
fi

dnl *** configure external libs ***

HAVE_FFMPEG_UNINSTALLED=1

AC_ARG_WITH(system-ffmpeg,
            [AC_HELP_STRING([--with-system-ffmpeg], [use system FFmpeg libraries])])

if test "x$with_system_ffmpeg" = "xyes"; then
  PKG_CHECK_MODULES(FFMPEG, libavformat libavcodec libavutil) 
  PKG_CHECK_MODULES(POSTPROC, libpostproc libavcodec libavutil)
  PKG_CHECK_MODULES(SWSCALE, libswscale libavutil)
  saved_CPPFLAGS="$CPPFLAGS"
  CPPFLAGS="$CPPFLAGS $FFMPEG_CFLAGS"
  AC_CHECK_HEADERS([avi.h])
  CPPFLAGS="$saved_CPPFLAGS"
  AC_DEFINE([FFMPEG_SOURCE], ["system install"], [Describes where the FFmpeg libraries come from.])
  HAVE_FFMPEG_UNINSTALLED=0
  AC_MSG_NOTICE([Using system-installed FFMpeg code])
  AC_MSG_WARN([
  ======================================================================
   WARNING: you have chosen to build gst-ffmpeg against a random
   external version of ffmpeg instead of building it against the tested
   internal ffmpeg snapshot that is included with gst-ffmpeg.
   
   This is a very bad idea.  So bad in fact that words cannot express
   just how bad it is.  Suffice to say that it is BAD.
   
   The GStreamer developers cannot and will not support a gst-ffmpeg
   built this way.  Any bug reports that indicate there is an external
   version of ffmpeg involved will be closed immediately without further
   investigation.
   
   The reason such a setup can't be supported is that the ffmpeg API
   and ABI is in constant flux, yet there aren't any official releases
   of the ffmpeg library to develop against.  This makes it impossible
   to guarantee that gst-ffmpeg will work reliably, or even compile,
   with a randomly picked version ffmpeg.  Even if gst-ffmpeg compiles
   and superficially appears to work fine against your chosen external
   ffmpeg version, that might just not be the case on other systems, or
   even the same system at a later time, or when using decoders,
   encoders, demuxers or muxers that have not been tested.
   
   Please do not create or distribute binary packages of gst-ffmpeg
   that link against an external ffmpeg. Thank you!
  ======================================================================
  ])

  dnl No, this is not too extreme, we want people to see and read the above
  sleep 15
else

  source "$srcdir/ffmpegrev"
  
  AC_MSG_NOTICE([Using ffmpeg revision $FFMPEG_REVISION])
  
  dnl libgstffmpeg.la: include dirs
  FFMPEG_CFLAGS="-I \$(top_srcdir)/gst-libs/ext/ffmpeg/libavformat \
                 -I \$(top_srcdir)/gst-libs/ext/ffmpeg/libavutil \
                 -I \$(top_srcdir)/gst-libs/ext/ffmpeg/libavcodec \
		 -I \$(top_srcdir)/gst-libs/ext/ffmpeg \
		 -I \$(top_builddir)/gst-libs/ext/ffmpeg \
                 -Wno-deprecated-declarations"

  dnl libgstffmpeg.la: libs to statically link to        
  FFMPEG_LIBS="\$(top_builddir)/gst-libs/ext/ffmpeg/libavformat/libavformat.a \
               \$(top_builddir)/gst-libs/ext/ffmpeg/libavcodec/libavcodec.a \
               \$(top_builddir)/gst-libs/ext/ffmpeg/libavutil/libavutil.a"
  dnl
  POSTPROC_CFLAGS="-I \$(top_srcdir)/gst-libs/ext/ffmpeg/libpostproc	\
                   -I \$(top_srcdir)/gst-libs/ext/ffmpeg/libavutil 	\
                   -I \$(top_srcdir)/gst-libs/ext/ffmpeg/libavcodec 	\
                   -I \$(top_srcdir)/gst-libs/ext/ffmpeg 	\
		   -I \$(top_builddir)/gst-libs/ext/ffmpeg \
                   -Wno-deprecated-declarations"

  dnl libgstpostproc.la: libs to statically link to
  POSTPROC_LIBS="\$(top_builddir)/gst-libs/ext/ffmpeg/libpostproc/libpostproc.a \
  		 \$(top_builddir)/gst-libs/ext/ffmpeg/libavutil/libavutil.a"
  				 
  dnl
  SWSCALE_CFLAGS="-I \$(top_srcdir)/gst-libs/ext/ffmpeg/libswscale	\
                  -I \$(top_srcdir)/gst-libs/ext/ffmpeg/libavutil 	\
                  -I \$(top_srcdir)/gst-libs/ext/ffmpeg 	\
		   -I \$(top_builddir)/gst-libs/ext/ffmpeg \
                  -Wno-deprecated-declarations"

  dnl libgstswscale.la: libs to statically link to
  SWSCALE_LIBS="\$(top_builddir)/gst-libs/ext/ffmpeg/libswscale/libswscale.a \
                \$(top_builddir)/gst-libs/ext/ffmpeg/libavutil/libavutil.a"

  FFMPEG_SUBDIRS=gst-libs
  AC_DEFINE(HAVE_AVI_H)
  AC_DEFINE([FFMPEG_SOURCE], ["local snapshot"], [Describes where the FFmpeg libraries come from.])

  AC_ARG_WITH(ffmpeg-extra-configure, 
      AC_HELP_STRING([--with-ffmpeg-extra-configure="xxx"],
      [extra configure options for internal ffmpeg ./configure script]),,
      with_ffmpeg_extra_configure=no)

  # basic arguments
  embffmpeg_configure_args="--prefix=$prefix"

  # Enable shared and static so that we get .a files, but with PIC code.
  embffmpeg_configure_args="$embffmpeg_configure_args --disable-ffserver --disable-ffplay\
        --enable-postproc --enable-gpl --enable-static --enable-shared \
	--disable-encoder=flac --disable-decoder=cavs --disable-protocols --disable-devices\
	 --disable-network --disable-hwaccels"

  # if we are cross-compiling, tell ffmpeg so
  if test "x$cross_compiling" = xyes; then
    embffmpeg_configure_args="$embffmpeg_configure_args --enable-cross-compile \
        --target-os=$host_os --arch=$host_cpu --cross-prefix=$host_alias-"
  fi

  case $host_os in
    # Unfortunately, in Mac OS 10.5 the current rev of ffmpeg builds
    # some non-PIC code into the .a file. See
    # http://trac.macosforge.org/projects/macports/ticket/13725 for more
    # info.
    darwin*) 
      embffmpeg_configure_args="$embffmpeg_configure_args --disable-mmx --disable-altivec"
      ;;
    mingw32*)
      embffmpeg_configure_args="$embffmpeg_configure_args --enable-memalign-hack"
      WIN32_LIBS="-lws2_32"
      ;;
    *)
      WIN32_LIBS=
      ;;
  esac

  dnl checks for extra enable/disable flags
  FFMPEG_OPTS="`$srcdir/gst-libs/ext/ffmpeg/configure --help`"
  # Let's check if we can disable the building of the ffmpeg binary
  can_disable=`echo "$FFMPEG_OPTS" | grep 'disable-ffmpeg'`
  if test "$can_disable" != ""; then
    embffmpeg_configure_args="$embffmpeg_configure_args --disable-ffmpeg"
  fi
  dnl check if libswscale needs enabling explicitly
  can_enable=`echo "$FFMPEG_OPTS" | grep 'enable-swscale'`
  if test "$can_enable" != ""; then
    embffmpeg_configure_args="$embffmpeg_configure_args --enable-swscale"
  fi

  # append extra configure options to embffmpeg_configure_args if needed
  if test "x$with_ffmpeg_extra_configure" != "xno"; then
    embffmpeg_configure_args="$embffmpeg_configure_args $with_ffmpeg_extra_configure"
  fi

  AC_SUBST(FFMPEG_CO_DIR)
  AC_SUBST(FFMPEG_SVN)
  AC_SUBST(FFMPEG_REVISION)
  AC_SUBST(FFMPEG_EXTERNALS_REVISION)
  AC_CONFIG_COMMANDS([configure-embedded-ffmpeg],
    [echo "Configuring included FFmpeg instance with args $embffmpeg_configure_args"
     origdir=`pwd`
     dnl Don't put path on the configure call when not needed, as FFmpeg's configure relies on it
     dnl to detect out-of-tree builds
     if test -z "$srcdir" -o "$srcdir" = .; then
       confcmd=./configure
     else
       confcmd="$origdir"/"$ac_top_srcdir"/gst-libs/ext/ffmpeg/configure
     fi

     AS_MKDIR_P(["$ac_top_build_prefix"gst-libs/ext/ffmpeg])
     cd "$ac_top_build_prefix"gst-libs/ext/ffmpeg &&
         $confcmd $embffmpeg_configure_args ||
         AC_MSG_ERROR([Failed to configure embedded FFmpeg tree])
     cd "$origdir"
    ],
    [embffmpeg_configure_args="$embffmpeg_configure_args"])
  AC_MSG_NOTICE([Using included FFMpeg code])
fi

AC_SUBST(FFMPEG_CFLAGS)
AC_SUBST(FFMPEG_LIBS)
AC_SUBST(FFMPEG_SUBDIRS)
AC_SUBST(POSTPROC_CFLAGS)
AC_SUBST(POSTPROC_LIBS)
AC_SUBST(SWSCALE_CFLAGS)
AC_SUBST(SWSCALE_LIBS)
AC_SUBST(WIN32_LIBS)
 
if test x$HAVE_FFMPEG_UNINSTALLED = x1; then
  AC_DEFINE(HAVE_FFMPEG_UNINSTALLED, [], [Defined if building against uninstalled FFmpeg source])
fi
AM_CONDITIONAL(HAVE_FFMPEG_UNINSTALLED, test x$HAVE_FFMPEG_UNINSTALLED = x1)

SHAVE_INIT([common],[enable])

AC_CONFIG_FILES(
Makefile
common/Makefile
common/m4/Makefile
common/shave
common/shave-libtool
gst-ffmpeg.spec
ext/Makefile
ext/ffmpeg/Makefile
ext/libpostproc/Makefile
ext/libswscale/Makefile
gst-libs/Makefile
gst-libs/ext/Makefile
docs/Makefile
docs/version.entities
tests/Makefile
tests/check/Makefile
tests/files/Makefile
)
AC_OUTPUT

