/* GStreamer
 *
 * Copyright (C) 2007 Rene Stadler <mail@renestadler.de>
 * Copyright (C) 2007 Sebastian Dröge <slomo@circular-chaos.org>
 * 
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

/**
 * SECTION:element-giosrc
 * @short_description: Read from any GIO-supported location
 * @see_also: #GstFileSrc, #GstGnomeVFSSrc, #GstGioSink
 *
 * <refsect2>
 * <para>
 * This plugin reads data from a local or remote location specified
 * by an URI. This location can be specified using any protocol supported by
 * the GIO library or it's VFS backends. Common protocols are 'file', 'http',
 * 'ftp', or 'smb'.
 * </para>
 * <para>
 * Example pipeline:
 * <programlisting>
 * gst-launch -v giosrc location=file:///home/joe/foo.xyz ! fakesink
 * </programlisting>
 * The above pipeline will simply read a local file and do nothing with the
 * data read. Instead of giosrc, we could just as well have used the
 * filesrc element here.
 * </para>
 * <para>
 * Another example pipeline:
 * <programlisting>
 * gst-launch -v giosrc location=smb://othercomputer/foo.xyz ! filesink location=/home/joe/foo.xyz
 * </programlisting>
 * The above pipeline will copy a file from a remote host to the local file
 * system using the Samba protocol.
 * </para>
 * <para>
 * Yet another example pipeline:
 * <programlisting>
 * gst-launch -v giosrc location=http://music.foobar.com/demo.mp3 ! mad ! audioconvert ! audioresample ! alsasink
 * </programlisting>
 * The above pipeline will read and decode and play an mp3 file from a
 * web server using the http protocol.
 * </para>
 * </refsect2>
 */

/* FIXME: We would like to mount the enclosing volume of an URL
 *        if it isn't mounted yet but this is possible async-only.
 *        Unfortunately this requires a running main loop from the
 *        default context and we can't guarantuee this!
 *
 *        We would also like to do authentication while mounting.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "gstgiosrc.h"
#include <string.h>

GST_DEBUG_CATEGORY_STATIC (gst_gio_src_debug);
#define GST_CAT_DEFAULT gst_gio_src_debug

enum
{
  ARG_0,
  ARG_LOCATION,
  ARG_FILE
};

GST_BOILERPLATE_FULL (GstGioSrc, gst_gio_src, GstGioBaseSrc,
    GST_TYPE_GIO_BASE_SRC, gst_gio_uri_handler_do_init);

static void gst_gio_src_finalize (GObject * object);

static void gst_gio_src_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_gio_src_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static gboolean gst_gio_src_start (GstBaseSrc * base_src);

static gboolean gst_gio_src_check_get_range (GstBaseSrc * base_src);

static void
gst_gio_src_base_init (gpointer gclass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (gclass);

  GST_DEBUG_CATEGORY_INIT (gst_gio_src_debug, "gio_src", 0, "GIO source");

  gst_element_class_set_details_simple (element_class, "GIO source",
      "Source/File",
      "Read from any GIO-supported location",
      "Ren\xc3\xa9 Stadler <mail@renestadler.de>, "
      "Sebastian Dröge <slomo@circular-chaos.org>");
}

static void
gst_gio_src_class_init (GstGioSrcClass * klass)
{
  GObjectClass *gobject_class;

  GstElementClass *gstelement_class;

  GstBaseSrcClass *gstbasesrc_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;
  gstbasesrc_class = (GstBaseSrcClass *) klass;

  gobject_class->finalize = gst_gio_src_finalize;
  gobject_class->set_property = gst_gio_src_set_property;
  gobject_class->get_property = gst_gio_src_get_property;

  g_object_class_install_property (gobject_class, ARG_LOCATION,
      g_param_spec_string ("location", "Location", "URI location to read from",
          NULL, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /**
   * GstGioSrc:file
   * 
   * %GFile to read from.
   * 
   * Since: 0.10.20
   **/
  g_object_class_install_property (gobject_class, ARG_FILE,
      g_param_spec_object ("file", "File", "GFile to read from",
          G_TYPE_FILE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gstbasesrc_class->start = GST_DEBUG_FUNCPTR (gst_gio_src_start);
  gstbasesrc_class->check_get_range =
      GST_DEBUG_FUNCPTR (gst_gio_src_check_get_range);
}

static void
gst_gio_src_init (GstGioSrc * src, GstGioSrcClass * gclass)
{
}

static void
gst_gio_src_finalize (GObject * object)
{
  GstGioSrc *src = GST_GIO_SRC (object);

  if (src->file) {
    g_object_unref (src->file);
    src->file = NULL;
  }

  GST_CALL_PARENT (G_OBJECT_CLASS, finalize, (object));
}

static void
gst_gio_src_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstGioSrc *src = GST_GIO_SRC (object);

  switch (prop_id) {
    case ARG_LOCATION:{
      const gchar *uri = NULL;

      if (GST_STATE (src) == GST_STATE_PLAYING ||
          GST_STATE (src) == GST_STATE_PAUSED) {
        GST_WARNING
            ("Setting a new location or GFile not supported in PLAYING or PAUSED state");
        break;
      }

      GST_OBJECT_LOCK (GST_OBJECT (src));
      if (src->file)
        g_object_unref (src->file);

      uri = g_value_get_string (value);

      if (uri) {
        src->file = g_file_new_for_uri (uri);

        if (!src->file) {
          GST_ERROR ("Could not create GFile for URI '%s'", uri);
        }
      } else {
        src->file = NULL;
      }
      GST_OBJECT_UNLOCK (GST_OBJECT (src));
      break;
    }
    case ARG_FILE:
      if (GST_STATE (src) == GST_STATE_PLAYING ||
          GST_STATE (src) == GST_STATE_PAUSED) {
        GST_WARNING
            ("Setting a new location or GFile not supported in PLAYING or PAUSED state");
        break;
      }

      GST_OBJECT_LOCK (GST_OBJECT (src));
      if (src->file)
        g_object_unref (src->file);

      src->file = g_value_dup_object (value);

      GST_OBJECT_UNLOCK (GST_OBJECT (src));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_gio_src_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstGioSrc *src = GST_GIO_SRC (object);

  switch (prop_id) {
    case ARG_LOCATION:{
      gchar *uri;

      GST_OBJECT_LOCK (GST_OBJECT (src));
      if (src->file) {
        uri = g_file_get_uri (src->file);
        g_value_set_string (value, uri);
        g_free (uri);
      } else {
        g_value_set_string (value, NULL);
      }
      GST_OBJECT_UNLOCK (GST_OBJECT (src));
      break;
    }
    case ARG_FILE:
      GST_OBJECT_LOCK (GST_OBJECT (src));
      g_value_set_object (value, src->file);
      GST_OBJECT_UNLOCK (GST_OBJECT (src));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
gst_gio_src_check_get_range (GstBaseSrc * base_src)
{
  GstGioSrc *src = GST_GIO_SRC (base_src);

  gchar *scheme;

  if (src->file == NULL)
    goto done;

  scheme = g_file_get_uri_scheme (src->file);
  if (scheme == NULL)
    goto done;

  if (strcmp (scheme, "file") == 0) {
    GST_LOG_OBJECT (src, "local URI, assuming random access is possible");
    g_free (scheme);
    return TRUE;
  } else if (strcmp (scheme, "http") == 0 || strcmp (scheme, "https") == 0) {
    GST_LOG_OBJECT (src, "blacklisted protocol '%s', "
        "no random access possible", scheme);
    g_free (scheme);
    return FALSE;
  }

  g_free (scheme);

done:

  GST_DEBUG_OBJECT (src, "undecided about random access, asking base class");

  return GST_CALL_PARENT_WITH_DEFAULT (GST_BASE_SRC_CLASS,
      check_get_range, (base_src), FALSE);
}


static gboolean
gst_gio_src_start (GstBaseSrc * base_src)
{
  GstGioSrc *src = GST_GIO_SRC (base_src);

  GError *err = NULL;

  GInputStream *stream;

  GCancellable *cancel = GST_GIO_BASE_SRC (src)->cancel;

  gchar *uri = NULL;

  if (src->file == NULL) {
    GST_ELEMENT_ERROR (src, RESOURCE, OPEN_READ, (NULL),
        ("No location or GFile given"));
    return FALSE;
  }

  uri = g_file_get_uri (src->file);
  if (!uri)
    uri = g_strdup ("(null)");

  stream = G_INPUT_STREAM (g_file_read (src->file, cancel, &err));

  if (stream == NULL && !gst_gio_error (src, "g_file_read", &err, NULL)) {
    if (GST_GIO_ERROR_MATCHES (err, NOT_FOUND)) {
      GST_ELEMENT_ERROR (src, RESOURCE, NOT_FOUND, (NULL),
          ("Could not open location %s for reading: %s", uri, err->message));
    } else {
      GST_ELEMENT_ERROR (src, RESOURCE, OPEN_READ, (NULL),
          ("Could not open location %s for reading: %s", uri, err->message));
    }

    g_free (uri);
    g_clear_error (&err);
    return FALSE;
  } else if (stream == NULL) {
    g_free (uri);
    return FALSE;
  }

  gst_gio_base_src_set_stream (GST_GIO_BASE_SRC (src), stream);

  GST_DEBUG_OBJECT (src, "opened location %s", uri);
  g_free (uri);

  return GST_BASE_SRC_CLASS (parent_class)->start (base_src);
}
