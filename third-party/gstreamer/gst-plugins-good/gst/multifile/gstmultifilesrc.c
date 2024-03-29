/* GStreamer
 * Copyright (C) 2006 David A. Schleef <ds@schleef.org>
 *
 * gstmultifilesrc.c:
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
 * SECTION:element-multifilesrc
 * @short_description: Read buffers from sequentially-named files
 * @see_also: #GstFileSrc
 *
 * <refsect2>
 * <para>
 * Reads buffers from sequentially named files. If used together with an image
 * decoder, one needs to use the GstMultiFileSrc::caps property or a capsfilter
 * to force to caps containing a framerate. Otherwise image decoders send EOS
 * after the first picture.
 * </para>
 * <title>Example launch line</title>
 * <para>
 * <programlisting>
 * gst-launch multifilesrc location="img.%04d.png" index=0 caps="image/png,framerate=\(fraction\)12/1" ! \
 *     pngdec ! ffmpegcolorspace ! theoraenc ! oggmux ! \
 *     filesink location="images.ogg"
 * </programlisting>
 * This pipeline creates a video file "images.ogg" by joining multiple PNG
 * files named img.0000.png, img.0001.png, etc.
 * </para>
 * <para>
 * File names are created by replacing "%%d" with the index using printf().
 * </para>
 * </refsect2>
*/

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include "gstmultifilesrc.h"


static GstFlowReturn gst_multi_file_src_create (GstPushSrc * src,
    GstBuffer ** buffer);

static void gst_multi_file_src_dispose (GObject * object);

static void gst_multi_file_src_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_multi_file_src_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static GstCaps *gst_multi_file_src_getcaps (GstBaseSrc * src);


static GstStaticPadTemplate gst_multi_file_src_pad_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

GST_DEBUG_CATEGORY_STATIC (gst_multi_file_src_debug);
#define GST_CAT_DEFAULT gst_multi_file_src_debug

static const GstElementDetails gst_multi_file_src_details =
GST_ELEMENT_DETAILS ("Multi-File Source",
    "Source/File",
    "Read a sequentially named set of files into buffers",
    "David Schleef <ds@schleef.org>");

enum
{
  ARG_0,
  ARG_LOCATION,
  ARG_INDEX,
  ARG_CAPS
};

#define DEFAULT_LOCATION "%05d"
#define DEFAULT_INDEX 0


GST_BOILERPLATE (GstMultiFileSrc, gst_multi_file_src, GstPushSrc,
    GST_TYPE_PUSH_SRC);

static void
gst_multi_file_src_base_init (gpointer g_class)
{
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (g_class);

  GST_DEBUG_CATEGORY_INIT (gst_multi_file_src_debug, "multifilesrc", 0,
      "multifilesrc element");

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&gst_multi_file_src_pad_template));
  gst_element_class_set_details (gstelement_class, &gst_multi_file_src_details);
}

static void
gst_multi_file_src_class_init (GstMultiFileSrcClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstPushSrcClass *gstpushsrc_class = GST_PUSH_SRC_CLASS (klass);
  GstBaseSrcClass *gstbasesrc_class = GST_BASE_SRC_CLASS (klass);

  gobject_class->set_property = gst_multi_file_src_set_property;
  gobject_class->get_property = gst_multi_file_src_get_property;

  g_object_class_install_property (gobject_class, ARG_LOCATION,
      g_param_spec_string ("location", "File Location",
          "Pattern to create file names of input files.  File names are "
          "created by calling sprintf() with the pattern and the current "
          "index.", DEFAULT_LOCATION, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, ARG_INDEX,
      g_param_spec_int ("index", "File Index",
          "Index to use with location property to create file names.  The "
          "index is incremented by one for each buffer read.",
          0, INT_MAX, DEFAULT_INDEX, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, ARG_CAPS,
      g_param_spec_boxed ("caps", "Caps",
          "Caps describing the format of the data.",
          GST_TYPE_CAPS, G_PARAM_READWRITE));

  gobject_class->dispose = gst_multi_file_src_dispose;

  gstpushsrc_class->create = gst_multi_file_src_create;

  gstbasesrc_class->get_caps = gst_multi_file_src_getcaps;

  if (sizeof (off_t) < 8) {
    GST_LOG ("No large file support, sizeof (off_t) = %" G_GSIZE_FORMAT,
        sizeof (off_t));
  }
}

static void
gst_multi_file_src_init (GstMultiFileSrc * multifilesrc,
    GstMultiFileSrcClass * g_class)
{
  GstPad *pad;

  pad = GST_BASE_SRC_PAD (multifilesrc);

  multifilesrc->index = DEFAULT_INDEX;
  multifilesrc->filename = g_strdup (DEFAULT_LOCATION);
  multifilesrc->successful_read = FALSE;
}

static void
gst_multi_file_src_dispose (GObject * object)
{
  GstMultiFileSrc *src = GST_MULTI_FILE_SRC (object);

  g_free (src->filename);
  src->filename = NULL;
  if (src->caps)
    gst_caps_unref (src->caps);

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static GstCaps *
gst_multi_file_src_getcaps (GstBaseSrc * src)
{
  GstMultiFileSrc *multi_file_src = GST_MULTI_FILE_SRC (src);

  GST_DEBUG_OBJECT (src, "returning %" GST_PTR_FORMAT, multi_file_src->caps);

  if (multi_file_src->caps) {
    return gst_caps_ref (multi_file_src->caps);
  } else {
    return gst_caps_new_any ();
  }
}

static gboolean
gst_multi_file_src_set_location (GstMultiFileSrc * src, const gchar * location)
{
  g_free (src->filename);
  if (location != NULL) {
    src->filename = g_strdup (location);
  } else {
    src->filename = NULL;
  }

  return TRUE;
}
static void
gst_multi_file_src_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstMultiFileSrc *src = GST_MULTI_FILE_SRC (object);

  switch (prop_id) {
    case ARG_LOCATION:
      gst_multi_file_src_set_location (src, g_value_get_string (value));
      break;
    case ARG_INDEX:
      src->index = g_value_get_int (value);
      break;
    case ARG_CAPS:
    {
      const GstCaps *caps = gst_value_get_caps (value);
      GstCaps *new_caps;

      if (caps == NULL) {
        new_caps = gst_caps_new_any ();
      } else {
        new_caps = gst_caps_copy (caps);
      }
      gst_caps_replace (&src->caps, new_caps);
      gst_pad_set_caps (GST_BASE_SRC_PAD (src), new_caps);
    }
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_multi_file_src_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstMultiFileSrc *src = GST_MULTI_FILE_SRC (object);

  switch (prop_id) {
    case ARG_LOCATION:
      g_value_set_string (value, src->filename);
      break;
    case ARG_INDEX:
      g_value_set_int (value, src->index);
      break;
    case ARG_CAPS:
      gst_value_set_caps (value, src->caps);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gchar *
gst_multi_file_src_get_filename (GstMultiFileSrc * multifilesrc)
{
  gchar *filename;

  filename = g_strdup_printf (multifilesrc->filename, multifilesrc->index);

  return filename;
}

static GstFlowReturn
gst_multi_file_src_create (GstPushSrc * src, GstBuffer ** buffer)
{
  GstMultiFileSrc *multifilesrc;
  gsize size;
  gchar *data;
  gchar *filename;
  GstBuffer *buf;
  gboolean ret;
  GError *error = NULL;

  multifilesrc = GST_MULTI_FILE_SRC (src);

  filename = gst_multi_file_src_get_filename (multifilesrc);

  GST_DEBUG_OBJECT (multifilesrc, "reading from file \"%s\".", filename);

  ret = g_file_get_contents (filename, &data, &size, &error);
  if (!ret) {
    if (multifilesrc->successful_read) {
      /* If we've read at least one buffer successfully, not finding the
       * next file is EOS. */
      g_free (filename);
      if (error != NULL)
        g_error_free (error);
      return GST_FLOW_UNEXPECTED;
    } else {
      goto handle_error;
    }
  }

  multifilesrc->successful_read = TRUE;
  multifilesrc->index++;

  buf = gst_buffer_new ();
  GST_BUFFER_DATA (buf) = (unsigned char *) data;
  GST_BUFFER_MALLOCDATA (buf) = GST_BUFFER_DATA (buf);
  GST_BUFFER_SIZE (buf) = size;
  GST_BUFFER_OFFSET (buf) = multifilesrc->offset;
  GST_BUFFER_OFFSET_END (buf) = multifilesrc->offset + size;
  multifilesrc->offset += size;
  gst_buffer_set_caps (buf, multifilesrc->caps);

  GST_DEBUG_OBJECT (multifilesrc, "read file \"%s\".", filename);

  g_free (filename);
  *buffer = buf;
  return GST_FLOW_OK;

handle_error:
  {
    if (error != NULL) {
      GST_ELEMENT_ERROR (multifilesrc, RESOURCE, READ,
          ("Error while reading from file \"%s\".", filename),
          ("%s", error->message));
      g_error_free (error);
    } else {
      GST_ELEMENT_ERROR (multifilesrc, RESOURCE, READ,
          ("Error while reading from file \"%s\".", filename),
          ("%s", g_strerror (errno)));
    }
    g_free (filename);
    return GST_FLOW_ERROR;
  }
}
