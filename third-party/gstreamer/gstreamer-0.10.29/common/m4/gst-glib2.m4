dnl check for a minimum version of GLib

dnl AG_GST_GLIB_CHECK([minimum-version-required])

AC_DEFUN([AG_GST_GLIB_CHECK],
[
  AC_REQUIRE([AS_NANO])

  dnl Minimum required version of GLib
  GLIB_REQ=[$1]
  if test "x$GLIB_REQ" = "x"
  then
    AC_MSG_ERROR([Please specify a required version for GLib 2.0])
  fi
  AC_SUBST(GLIB_REQ)

  dnl Check for glib with everything
  AG_GST_PKG_CHECK_MODULES(GLIB,
    glib-2.0 >= $GLIB_REQ gobject-2.0 gthread-2.0 gmodule-no-export-2.0)

  if test "x$HAVE_GLIB" = "xno"; then
    AC_MSG_ERROR([This package requires GLib >= $GLIB_REQ to compile.])
  fi

  dnl Add define to tell GLib that threading is always enabled within GStreamer
  dnl code (optimisation, bypasses checks if the threading system is enabled
  dnl when using threading primitives)
  GLIB_CFLAGS="$GLIB_CFLAGS -DG_THREADS_MANDATORY"

  AC_ARG_ENABLE(gobject-cast-checks,
    AS_HELP_STRING([--enable-gobject-cast-checks[=@<:@no/auto/yes@:>@]],
      [Enable GObject cast checks]),, 
    [enable_gobject_cast_checks=auto])

  if test "x$enable_gobject_cast_checks" = "xauto"; then
    dnl For releases, turn off the cast checks checks
    if test "x$PACKAGE_VERSION_NANO" = "x1"; then
      enable_gobject_cast_checks=yes
    else
      enable_gobject_cast_checks=no
    fi
  fi

  if test "x$enable_gobject_cast_checks" = "xno"; then
    GLIB_CFLAGS="$GLIB_CFLAGS -DG_DISABLE_CAST_CHECKS"
  fi

  dnl for the poor souls who for example have glib in /usr/local
  AS_SCRUB_INCLUDE(GLIB_CFLAGS)
])
