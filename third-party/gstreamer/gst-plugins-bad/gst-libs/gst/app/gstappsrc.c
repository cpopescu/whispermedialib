/* GStreamer
 * Copyright (C) 2007 David Schleef <ds@schleef.org>
 *           (C) 2008 Wim Taymans <wim.taymans@gmail.com>
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include <gst/base/gstbasesrc.h>

#include <string.h>

#include "gstapp-marshal.h"
#include "gstappsrc.h"


GST_DEBUG_CATEGORY (app_src_debug);
#define GST_CAT_DEFAULT app_src_debug

static const GstElementDetails app_src_details = GST_ELEMENT_DETAILS ("AppSrc",
    "Generic/Src",
    "Allow the application to feed buffers to a pipeline",
    "David Schleef <ds@schleef.org>, Wim Taymans <wim.taymans@gmail.com");

enum
{
  /* signals */
  SIGNAL_NEED_DATA,
  SIGNAL_ENOUGH_DATA,
  SIGNAL_SEEK_DATA,

  /* actions */
  SIGNAL_PUSH_BUFFER,
  SIGNAL_END_OF_STREAM,

  LAST_SIGNAL
};

#define DEFAULT_PROP_SIZE          -1
#define DEFAULT_PROP_STREAM_TYPE   GST_APP_STREAM_TYPE_STREAM
#define DEFAULT_PROP_MAX_BYTES     200000
#define DEFAULT_PROP_FORMAT        GST_FORMAT_BYTES
#define DEFAULT_PROP_BLOCK         FALSE

enum
{
  PROP_0,
  PROP_CAPS,
  PROP_SIZE,
  PROP_STREAM_TYPE,
  PROP_MAX_BYTES,
  PROP_FORMAT,
  PROP_BLOCK,

  PROP_LAST
};

static GstStaticPadTemplate gst_app_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);


#define GST_TYPE_APP_STREAM_TYPE (stream_type_get_type ())
static GType
stream_type_get_type (void)
{
  static GType stream_type_type = 0;
  static const GEnumValue stream_type[] = {
    {GST_APP_STREAM_TYPE_STREAM, "Stream", "stream"},
    {GST_APP_STREAM_TYPE_SEEKABLE, "Seekable", "seekable"},
    {GST_APP_STREAM_TYPE_RANDOM_ACCESS, "Random Access", "random-access"},
    {0, NULL, NULL},
  };

  if (!stream_type_type) {
    stream_type_type = g_enum_register_static ("GstAppStreamType", stream_type);
  }
  return stream_type_type;
}

static void gst_app_src_uri_handler_init (gpointer g_iface,
    gpointer iface_data);

static void gst_app_src_dispose (GObject * object);
static void gst_app_src_finalize (GObject * object);

static void gst_app_src_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_app_src_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static GstFlowReturn gst_app_src_create (GstBaseSrc * bsrc,
    guint64 offset, guint size, GstBuffer ** buf);
static gboolean gst_app_src_start (GstBaseSrc * bsrc);
static gboolean gst_app_src_stop (GstBaseSrc * bsrc);
static gboolean gst_app_src_unlock (GstBaseSrc * bsrc);
static gboolean gst_app_src_unlock_stop (GstBaseSrc * bsrc);
static gboolean gst_app_src_do_seek (GstBaseSrc * src, GstSegment * segment);
static gboolean gst_app_src_is_seekable (GstBaseSrc * src);
static gboolean gst_app_src_check_get_range (GstBaseSrc * src);
static gboolean gst_app_src_do_get_size (GstBaseSrc * src, guint64 * size);

static guint gst_app_src_signals[LAST_SIGNAL] = { 0 };

static void
_do_init (GType filesrc_type)
{
  static const GInterfaceInfo urihandler_info = {
    gst_app_src_uri_handler_init,
    NULL,
    NULL
  };
  g_type_add_interface_static (filesrc_type, GST_TYPE_URI_HANDLER,
      &urihandler_info);
}

GST_BOILERPLATE_FULL (GstAppSrc, gst_app_src, GstBaseSrc, GST_TYPE_BASE_SRC,
    _do_init);

static void
gst_app_src_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  GST_DEBUG_CATEGORY_INIT (app_src_debug, "appsrc", 0, "appsrc element");

  gst_element_class_set_details (element_class, &app_src_details);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_app_src_template));
}

static void
gst_app_src_class_init (GstAppSrcClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;
  GstBaseSrcClass *basesrc_class = (GstBaseSrcClass *) klass;

  gobject_class->dispose = gst_app_src_dispose;
  gobject_class->finalize = gst_app_src_finalize;

  gobject_class->set_property = gst_app_src_set_property;
  gobject_class->get_property = gst_app_src_get_property;

  g_object_class_install_property (gobject_class, PROP_CAPS,
      g_param_spec_boxed ("caps", "Caps",
          "The allowed caps for the src pad", GST_TYPE_CAPS,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_FORMAT,
      g_param_spec_enum ("format", "Format",
          "The format of the segment events and seek", GST_TYPE_FORMAT,
          DEFAULT_PROP_FORMAT, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_SIZE,
      g_param_spec_int64 ("size", "Size",
          "The size of the data stream (-1 if unknown)",
          -1, G_MAXINT64, DEFAULT_PROP_SIZE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_STREAM_TYPE,
      g_param_spec_enum ("stream-type", "Stream Type",
          "the type of the stream", GST_TYPE_APP_STREAM_TYPE,
          DEFAULT_PROP_STREAM_TYPE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_MAX_BYTES,
      g_param_spec_uint64 ("max-bytes", "Max bytes",
          "The maximum number of bytes to queue internally (0 = unlimited)",
          0, G_MAXUINT64, DEFAULT_PROP_MAX_BYTES,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_BLOCK,
      g_param_spec_boolean ("block", "Block",
          "Block push-buffer when max-bytes are queued",
          DEFAULT_PROP_BLOCK, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /**
   * GstAppSrc::need-data:
   * @appsrc: the appsrc element that emited the signal
   * @length: the amount of bytes needed.
   *
   * Signal that the source needs more data. In the callback or from another
   * thread you should call push-buffer or end-of-stream.
   *
   * @length is just a hint and when it is set to -1, any number of bytes can be
   * pushed into @appsrc.
   *
   * You can call push-buffer multiple times until the enough-data signal is
   * fired.
   */
  gst_app_src_signals[SIGNAL_NEED_DATA] =
      g_signal_new ("need-data", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
      G_STRUCT_OFFSET (GstAppSrcClass, need_data),
      NULL, NULL, gst_app_marshal_VOID__UINT, G_TYPE_NONE, 1, G_TYPE_UINT);

  /**
   * GstAppSrc::enough-data:
   * @appsrc: the appsrc element that emited the signal
   *
   * Signal that the source has enough data. It is recommended that the
   * application stops calling push-buffer until the need-data signal is
   * emited again to avoid excessive buffer queueing.
   */
  gst_app_src_signals[SIGNAL_ENOUGH_DATA] =
      g_signal_new ("enough-data", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
      G_STRUCT_OFFSET (GstAppSrcClass, enough_data),
      NULL, NULL, g_cclosure_marshal_VOID__VOID, G_TYPE_NONE, 0, G_TYPE_NONE);

  /**
   * GstAppSrc::seek-data:
   * @appsrc: the appsrc element that emited the signal
   * @offset: the offset to seek to
   *
   * Seek to the given offset. The next push-buffer should produce buffers from
   * the new @offset.
   * This callback is only called for seekable stream types.
   *
   * Returns: %TRUE if the seek succeeded.
   */
  gst_app_src_signals[SIGNAL_SEEK_DATA] =
      g_signal_new ("seek-data", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
      G_STRUCT_OFFSET (GstAppSrcClass, seek_data),
      NULL, NULL, gst_app_marshal_BOOLEAN__UINT64, G_TYPE_BOOLEAN, 1,
      G_TYPE_UINT64);

   /**
    * GstAppSrc::push-buffer:
    * @appsrc: the appsrc
    * @buffer: a buffer to push
    *
    * Adds a buffer to the queue of buffers that the appsrc element will
    * push to its source pad. This function will take ownership of @buffer.
    */
  gst_app_src_signals[SIGNAL_PUSH_BUFFER] =
      g_signal_new ("push-buffer", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION, G_STRUCT_OFFSET (GstAppSrcClass,
          push_buffer), NULL, NULL, gst_app_marshal_ENUM__OBJECT,
      GST_TYPE_FLOW_RETURN, 1, GST_TYPE_BUFFER);

   /**
    * GstAppSrc::end-of-stream:
    * @appsrc: the appsrc
    *
    * Notify @appsrc that no more buffer are available. 
    */
  gst_app_src_signals[SIGNAL_END_OF_STREAM] =
      g_signal_new ("end-of-stream", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION, G_STRUCT_OFFSET (GstAppSrcClass,
          end_of_stream), NULL, NULL, gst_app_marshal_ENUM__VOID,
      GST_TYPE_FLOW_RETURN, 0, G_TYPE_NONE);

  basesrc_class->create = gst_app_src_create;
  basesrc_class->start = gst_app_src_start;
  basesrc_class->stop = gst_app_src_stop;
  basesrc_class->unlock = gst_app_src_unlock;
  basesrc_class->unlock_stop = gst_app_src_unlock_stop;
  basesrc_class->do_seek = gst_app_src_do_seek;
  basesrc_class->is_seekable = gst_app_src_is_seekable;
  basesrc_class->check_get_range = gst_app_src_check_get_range;
  basesrc_class->get_size = gst_app_src_do_get_size;

  klass->push_buffer = gst_app_src_push_buffer;
  klass->end_of_stream = gst_app_src_end_of_stream;
}

static void
gst_app_src_init (GstAppSrc * appsrc, GstAppSrcClass * klass)
{
  appsrc->mutex = g_mutex_new ();
  appsrc->cond = g_cond_new ();
  appsrc->queue = g_queue_new ();

  appsrc->size = DEFAULT_PROP_SIZE;
  appsrc->stream_type = DEFAULT_PROP_STREAM_TYPE;
  appsrc->max_bytes = DEFAULT_PROP_MAX_BYTES;
  appsrc->format = DEFAULT_PROP_FORMAT;
  appsrc->block = DEFAULT_PROP_BLOCK;
}

static void
gst_app_src_flush_queued (GstAppSrc * src)
{
  GstBuffer *buf;

  while ((buf = g_queue_pop_head (src->queue)))
    gst_buffer_unref (buf);
}

static void
gst_app_src_dispose (GObject * obj)
{
  GstAppSrc *appsrc = GST_APP_SRC (obj);

  if (appsrc->caps) {
    gst_caps_unref (appsrc->caps);
    appsrc->caps = NULL;
  }
  gst_app_src_flush_queued (appsrc);

  G_OBJECT_CLASS (parent_class)->dispose (obj);
}

static void
gst_app_src_finalize (GObject * obj)
{
  GstAppSrc *appsrc = GST_APP_SRC (obj);

  g_mutex_free (appsrc->mutex);
  g_cond_free (appsrc->cond);
  g_queue_free (appsrc->queue);

  G_OBJECT_CLASS (parent_class)->finalize (obj);
}

static void
gst_app_src_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstAppSrc *appsrc = GST_APP_SRC (object);

  switch (prop_id) {
    case PROP_CAPS:
      gst_app_src_set_caps (appsrc, gst_value_get_caps (value));
      break;
    case PROP_SIZE:
      gst_app_src_set_size (appsrc, g_value_get_int64 (value));
      break;
    case PROP_STREAM_TYPE:
      gst_app_src_set_stream_type (appsrc, g_value_get_enum (value));
      break;
    case PROP_MAX_BYTES:
      gst_app_src_set_max_bytes (appsrc, g_value_get_uint64 (value));
      break;
    case PROP_FORMAT:
      appsrc->format = g_value_get_enum (value);
      break;
    case PROP_BLOCK:
      appsrc->block = g_value_get_boolean (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_app_src_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstAppSrc *appsrc = GST_APP_SRC (object);

  switch (prop_id) {
    case PROP_CAPS:
    {
      GstCaps *caps;

      /* we're missing a _take_caps() function to transfer ownership */
      caps = gst_app_src_get_caps (appsrc);
      gst_value_set_caps (value, caps);
      if (caps)
        gst_caps_unref (caps);
      break;
    }
    case PROP_SIZE:
      g_value_set_int64 (value, gst_app_src_get_size (appsrc));
      break;
    case PROP_STREAM_TYPE:
      g_value_set_enum (value, gst_app_src_get_stream_type (appsrc));
      break;
    case PROP_MAX_BYTES:
      g_value_set_uint64 (value, gst_app_src_get_max_bytes (appsrc));
      break;
    case PROP_FORMAT:
      g_value_set_enum (value, appsrc->format);
      break;
    case PROP_BLOCK:
      g_value_set_boolean (value, appsrc->block);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
gst_app_src_unlock (GstBaseSrc * bsrc)
{
  GstAppSrc *appsrc = GST_APP_SRC (bsrc);

  g_mutex_lock (appsrc->mutex);
  GST_DEBUG_OBJECT (appsrc, "unlock start");
  appsrc->flushing = TRUE;
  g_cond_broadcast (appsrc->cond);
  g_mutex_unlock (appsrc->mutex);

  return TRUE;
}

static gboolean
gst_app_src_unlock_stop (GstBaseSrc * bsrc)
{
  GstAppSrc *appsrc = GST_APP_SRC (bsrc);

  g_mutex_lock (appsrc->mutex);
  GST_DEBUG_OBJECT (appsrc, "unlock stop");
  appsrc->flushing = FALSE;
  g_cond_broadcast (appsrc->cond);
  g_mutex_unlock (appsrc->mutex);

  return TRUE;
}

static gboolean
gst_app_src_start (GstBaseSrc * bsrc)
{
  GstAppSrc *appsrc = GST_APP_SRC (bsrc);

  g_mutex_lock (appsrc->mutex);
  GST_DEBUG_OBJECT (appsrc, "starting");
  appsrc->started = TRUE;
  /* set the offset to -1 so that we always do a first seek. This is only used
   * in random-access mode. */
  appsrc->offset = -1;
  appsrc->flushing = FALSE;
  g_mutex_unlock (appsrc->mutex);

  gst_base_src_set_format (bsrc, appsrc->format);

  return TRUE;
}

static gboolean
gst_app_src_stop (GstBaseSrc * bsrc)
{
  GstAppSrc *appsrc = GST_APP_SRC (bsrc);

  g_mutex_lock (appsrc->mutex);
  GST_DEBUG_OBJECT (appsrc, "stopping");
  appsrc->is_eos = FALSE;
  appsrc->flushing = TRUE;
  appsrc->started = FALSE;
  gst_app_src_flush_queued (appsrc);
  g_mutex_unlock (appsrc->mutex);

  return TRUE;
}

static gboolean
gst_app_src_is_seekable (GstBaseSrc * src)
{
  GstAppSrc *appsrc = GST_APP_SRC (src);
  gboolean res = FALSE;

  switch (appsrc->stream_type) {
    case GST_APP_STREAM_TYPE_STREAM:
      break;
    case GST_APP_STREAM_TYPE_SEEKABLE:
    case GST_APP_STREAM_TYPE_RANDOM_ACCESS:
      res = TRUE;
      break;
  }
  return res;
}

static gboolean
gst_app_src_check_get_range (GstBaseSrc * src)
{
  GstAppSrc *appsrc = GST_APP_SRC (src);
  gboolean res = FALSE;

  switch (appsrc->stream_type) {
    case GST_APP_STREAM_TYPE_STREAM:
    case GST_APP_STREAM_TYPE_SEEKABLE:
      break;
    case GST_APP_STREAM_TYPE_RANDOM_ACCESS:
      res = TRUE;
      break;
  }
  return res;
}

static gboolean
gst_app_src_do_get_size (GstBaseSrc * src, guint64 * size)
{
  GstAppSrc *appsrc = GST_APP_SRC (src);

  *size = gst_app_src_get_size (appsrc);

  return TRUE;
}

/* will be called in push mode */
static gboolean
gst_app_src_do_seek (GstBaseSrc * src, GstSegment * segment)
{
  GstAppSrc *appsrc = GST_APP_SRC (src);
  gint64 desired_position;
  gboolean res = FALSE;

  desired_position = segment->last_stop;

  GST_DEBUG_OBJECT (appsrc, "seeking to %" G_GINT64_FORMAT ", format %s",
      desired_position, gst_format_get_name (segment->format));

  /* no need to try to seek in streaming mode */
  if (appsrc->stream_type == GST_APP_STREAM_TYPE_STREAM)
    return TRUE;

  g_signal_emit (appsrc, gst_app_src_signals[SIGNAL_SEEK_DATA], 0,
      desired_position, &res);

  if (res) {
    GST_DEBUG_OBJECT (appsrc, "flushing queue");
    gst_app_src_flush_queued (appsrc);
  } else {
    GST_WARNING_OBJECT (appsrc, "seek failed");
  }

  return res;
}

static GstFlowReturn
gst_app_src_create (GstBaseSrc * bsrc, guint64 offset, guint size,
    GstBuffer ** buf)
{
  GstAppSrc *appsrc = GST_APP_SRC (bsrc);
  GstFlowReturn ret;

  g_mutex_lock (appsrc->mutex);
  /* check flushing first */
  if (G_UNLIKELY (appsrc->flushing))
    goto flushing;

  if (appsrc->stream_type == GST_APP_STREAM_TYPE_RANDOM_ACCESS) {
    /* if we are dealing with a random-access stream, issue a seek if the offset
     * changed. */
    if (G_UNLIKELY (appsrc->offset != offset)) {
      gboolean res;

      g_mutex_unlock (appsrc->mutex);

      GST_DEBUG_OBJECT (appsrc,
          "we are at %" G_GINT64_FORMAT ", seek to %" G_GINT64_FORMAT,
          appsrc->offset, offset);

      g_signal_emit (appsrc, gst_app_src_signals[SIGNAL_SEEK_DATA], 0,
          offset, &res);

      if (G_UNLIKELY (!res))
        /* failing to seek is fatal */
        goto seek_error;

      g_mutex_lock (appsrc->mutex);

      appsrc->offset = offset;
    }
  }

  while (TRUE) {
    /* return data as long as we have some */
    if (!g_queue_is_empty (appsrc->queue)) {
      guint buf_size;

      *buf = g_queue_pop_head (appsrc->queue);
      buf_size = GST_BUFFER_SIZE (*buf);

      GST_DEBUG_OBJECT (appsrc, "we have buffer %p of size %u", *buf, buf_size);

      appsrc->queued_bytes -= buf_size;

      /* only update the offset when in random_access mode */
      if (appsrc->stream_type == GST_APP_STREAM_TYPE_RANDOM_ACCESS) {
        appsrc->offset += buf_size;
      }

      gst_buffer_set_caps (*buf, appsrc->caps);

      /* signal that we removed an item */
      g_cond_broadcast (appsrc->cond);

      ret = GST_FLOW_OK;
      break;
    } else {
      g_mutex_unlock (appsrc->mutex);

      /* we have no data, we need some. We fire the signal with the size hint. */
      g_signal_emit (appsrc, gst_app_src_signals[SIGNAL_NEED_DATA], 0, size,
          NULL);

      g_mutex_lock (appsrc->mutex);
      /* we can be flushing now because we released the lock */
      if (G_UNLIKELY (appsrc->flushing))
        goto flushing;

      /* if we have a buffer now, continue the loop and try to return it. In
       * random-access mode (where a buffer is normally pushed in the above
       * signal) we can still be empty because the pushed buffer got flushed or
       * when the application pushes the requested buffer later, we support both
       * possiblities. */
      if (!g_queue_is_empty (appsrc->queue))
        continue;

      /* no buffer yet, maybe we are EOS, if not, block for more data. */
    }

    /* check EOS */
    if (G_UNLIKELY (appsrc->is_eos))
      goto eos;

    /* nothing to return, wait a while for new data or flushing. */
    g_cond_wait (appsrc->cond, appsrc->mutex);
  }
  g_mutex_unlock (appsrc->mutex);

  return ret;

  /* ERRORS */
flushing:
  {
    GST_DEBUG_OBJECT (appsrc, "we are flushing");
    g_mutex_unlock (appsrc->mutex);
    return GST_FLOW_WRONG_STATE;
  }
eos:
  {
    GST_DEBUG_OBJECT (appsrc, "we are EOS");
    g_mutex_unlock (appsrc->mutex);
    return GST_FLOW_UNEXPECTED;
  }
seek_error:
  {
    GST_ELEMENT_ERROR (appsrc, RESOURCE, READ, ("failed to seek"),
        GST_ERROR_SYSTEM);
    return GST_FLOW_ERROR;
  }
}

/* external API */

/**
 * gst_app_src_set_caps:
 * @appsrc: a #GstAppSrc
 * @caps: caps to set
 *
 * Set the capabilities on the appsrc element.  This function takes
 * a copy of the caps structure. After calling this method, the source will
 * only produce caps that match @caps. @caps must be fixed and the caps on the
 * buffers must match the caps or left NULL.
 */
void
gst_app_src_set_caps (GstAppSrc * appsrc, const GstCaps * caps)
{
  GstCaps *old;

  g_return_if_fail (GST_IS_APP_SRC (appsrc));

  GST_OBJECT_LOCK (appsrc);
  GST_DEBUG_OBJECT (appsrc, "setting caps to %" GST_PTR_FORMAT, caps);
  if ((old = appsrc->caps) != caps) {
    if (caps)
      appsrc->caps = gst_caps_copy (caps);
    else
      appsrc->caps = NULL;
    if (old)
      gst_caps_unref (old);
  }
  GST_OBJECT_UNLOCK (appsrc);
}

/**
 * gst_app_src_get_caps:
 * @appsrc: a #GstAppSrc
 *
 * Get the configured caps on @appsrc.
 *
 * Returns: the #GstCaps produced by the source. gst_caps_unref() after usage.
 */
GstCaps *
gst_app_src_get_caps (GstAppSrc * appsrc)
{
  GstCaps *caps;

  g_return_val_if_fail (appsrc != NULL, NULL);
  g_return_val_if_fail (GST_IS_APP_SRC (appsrc), NULL);

  GST_OBJECT_LOCK (appsrc);
  if ((caps = appsrc->caps))
    gst_caps_ref (caps);
  GST_DEBUG_OBJECT (appsrc, "getting caps of %" GST_PTR_FORMAT, caps);
  GST_OBJECT_UNLOCK (appsrc);

  return caps;
}

/**
 * gst_app_src_set_size:
 * @appsrc: a #GstAppSrc
 * @size: the size to set
 *
 * Set the size of the stream in bytes. A value of -1 means that the size is
 * not known. 
 */
void
gst_app_src_set_size (GstAppSrc * appsrc, gint64 size)
{
  g_return_if_fail (appsrc != NULL);
  g_return_if_fail (GST_IS_APP_SRC (appsrc));

  GST_OBJECT_LOCK (appsrc);
  GST_DEBUG_OBJECT (appsrc, "setting size of %" G_GINT64_FORMAT, size);
  appsrc->size = size;
  GST_OBJECT_UNLOCK (appsrc);
}

/**
 * gst_app_src_get_size:
 * @appsrc: a #GstAppSrc
 *
 * Get the size of the stream in bytes. A value of -1 means that the size is
 * not known. 
 *
 * Returns: the size of the stream previously set with gst_app_src_set_size();
 */
gint64
gst_app_src_get_size (GstAppSrc * appsrc)
{
  gint64 size;

  g_return_val_if_fail (appsrc != NULL, -1);
  g_return_val_if_fail (GST_IS_APP_SRC (appsrc), -1);

  GST_OBJECT_LOCK (appsrc);
  size = appsrc->size;
  GST_DEBUG_OBJECT (appsrc, "getting size of %" G_GINT64_FORMAT, size);
  GST_OBJECT_UNLOCK (appsrc);

  return size;
}

/**
 * gst_app_src_set_stream_type:
 * @appsrc: a #GstAppSrc
 * @type: the new state
 *
 * Set the stream type on @appsrc. For seekable streams, the "seek" signal must
 * be connected to.
 *
 * A stream_type stream 
 */
void
gst_app_src_set_stream_type (GstAppSrc * appsrc, GstAppStreamType type)
{
  g_return_if_fail (appsrc != NULL);
  g_return_if_fail (GST_IS_APP_SRC (appsrc));

  GST_OBJECT_LOCK (appsrc);
  GST_DEBUG_OBJECT (appsrc, "setting stream_type of %d", type);
  appsrc->stream_type = type;
  GST_OBJECT_UNLOCK (appsrc);
}

/**
 * gst_app_src_get_stream_type:
 * @appsrc: a #GstAppSrc
 *
 * Get the stream type. Control the stream type of @appsrc
 * with gst_app_src_set_stream_type().
 *
 * Returns: the stream type.
 */
GstAppStreamType
gst_app_src_get_stream_type (GstAppSrc * appsrc)
{
  gboolean stream_type;

  g_return_val_if_fail (appsrc != NULL, FALSE);
  g_return_val_if_fail (GST_IS_APP_SRC (appsrc), FALSE);

  GST_OBJECT_LOCK (appsrc);
  stream_type = appsrc->stream_type;
  GST_DEBUG_OBJECT (appsrc, "getting stream_type of %d", stream_type);
  GST_OBJECT_UNLOCK (appsrc);

  return stream_type;
}

/**
 * gst_app_src_set_max_bytes:
 * @appsrc: a #GstAppSrc
 * @max: the maximum number of bytes to queue
 *
 * Set the maximum amount of bytes that can be queued in @appsrc.
 * After the maximum amount of bytes are queued, @appsrc will emit the
 * "enough-data" signal.
 */
void
gst_app_src_set_max_bytes (GstAppSrc * appsrc, guint64 max)
{
  g_return_if_fail (GST_IS_APP_SRC (appsrc));

  g_mutex_lock (appsrc->mutex);
  if (max != appsrc->max_bytes) {
    GST_DEBUG_OBJECT (appsrc, "setting max-bytes to %" G_GUINT64_FORMAT, max);
    appsrc->max_bytes = max;
    /* signal the change */
    g_cond_broadcast (appsrc->cond);
  }
  g_mutex_unlock (appsrc->mutex);
}

/**
 * gst_app_src_get_max_bytes:
 * @appsrc: a #GstAppSrc
 *
 * Get the maximum amount of bytes that can be queued in @appsrc.
 *
 * Returns: The maximum amount of bytes that can be queued.
 */
guint64
gst_app_src_get_max_bytes (GstAppSrc * appsrc)
{
  guint64 result;

  g_return_val_if_fail (GST_IS_APP_SRC (appsrc), 0);

  g_mutex_lock (appsrc->mutex);
  result = appsrc->max_bytes;
  GST_DEBUG_OBJECT (appsrc, "getting max-bytes of %" G_GUINT64_FORMAT, result);
  g_mutex_unlock (appsrc->mutex);

  return result;
}

/**
 * gst_app_src_push_buffer:
 * @appsrc: a #GstAppSrc
 * @buffer: a #GstBuffer to push
 *
 * Adds a buffer to the queue of buffers that the appsrc element will
 * push to its source pad.  This function takes ownership of the buffer.
 *
 * Returns: #GST_FLOW_OK when the buffer was successfuly queued.
 * #GST_FLOW_WRONG_STATE when @appsrc is not PAUSED or PLAYING.
 * #GST_FLOW_UNEXPECTED when EOS occured.
 */
GstFlowReturn
gst_app_src_push_buffer (GstAppSrc * appsrc, GstBuffer * buffer)
{
  gboolean first = TRUE;

  g_return_val_if_fail (appsrc, GST_FLOW_ERROR);
  g_return_val_if_fail (GST_IS_APP_SRC (appsrc), GST_FLOW_ERROR);
  g_return_val_if_fail (GST_IS_BUFFER (buffer), GST_FLOW_ERROR);

  g_mutex_lock (appsrc->mutex);

  while (TRUE) {
    /* can't accept buffers when we are flushing or EOS */
    if (appsrc->flushing)
      goto flushing;

    if (appsrc->is_eos)
      goto eos;

    if (appsrc->queued_bytes >= appsrc->max_bytes) {
      GST_DEBUG_OBJECT (appsrc, "queue filled (%" G_GUINT64_FORMAT " >= %"
          G_GUINT64_FORMAT ")", appsrc->queued_bytes, appsrc->max_bytes);

      if (first) {
        /* only signal on the first push */
        g_mutex_unlock (appsrc->mutex);

        g_signal_emit (appsrc, gst_app_src_signals[SIGNAL_ENOUGH_DATA], 0,
            NULL);

        g_mutex_lock (appsrc->mutex);
        /* continue to check for flushing/eos after releasing the lock */
        first = FALSE;
        continue;
      }
      if (appsrc->block) {
        GST_DEBUG_OBJECT (appsrc, "waiting for free space");
        /* we are filled, wait until a buffer gets popped or when we
         * flush. */
        g_cond_wait (appsrc->cond, appsrc->mutex);
      } else {
        /* no need to wait for free space, we just pump data into the queue */
        break;
      }
    } else
      break;
  }

  GST_DEBUG_OBJECT (appsrc, "queueing buffer %p", buffer);
  g_queue_push_tail (appsrc->queue, buffer);
  appsrc->queued_bytes += GST_BUFFER_SIZE (buffer);
  g_cond_broadcast (appsrc->cond);
  g_mutex_unlock (appsrc->mutex);

  return GST_FLOW_OK;

  /* ERRORS */
flushing:
  {
    GST_DEBUG_OBJECT (appsrc, "refuse buffer %p, we are flushing", buffer);
    gst_buffer_unref (buffer);
    return GST_FLOW_WRONG_STATE;
  }
eos:
  {
    GST_DEBUG_OBJECT (appsrc, "refuse buffer %p, we are EOS", buffer);
    gst_buffer_unref (buffer);
    return GST_FLOW_UNEXPECTED;
  }
}

/**
 * gst_app_src_end_of_stream:
 * @appsrc: a #GstAppSrc
 *
 * Indicates to the appsrc element that the last buffer queued in the
 * element is the last buffer of the stream.
 *
 * Returns: #GST_FLOW_OK when the EOS was successfuly queued.
 * #GST_FLOW_WRONG_STATE when @appsrc is not PAUSED or PLAYING.
 */
GstFlowReturn
gst_app_src_end_of_stream (GstAppSrc * appsrc)
{
  g_return_val_if_fail (appsrc, GST_FLOW_ERROR);
  g_return_val_if_fail (GST_IS_APP_SRC (appsrc), GST_FLOW_ERROR);

  g_mutex_lock (appsrc->mutex);
  /* can't accept buffers when we are flushing. We can accept them when we are 
   * EOS although it will not do anything. */
  if (appsrc->flushing)
    goto flushing;

  GST_DEBUG_OBJECT (appsrc, "sending EOS");
  appsrc->is_eos = TRUE;
  g_cond_broadcast (appsrc->cond);
  g_mutex_unlock (appsrc->mutex);

  return GST_FLOW_OK;

  /* ERRORS */
flushing:
  {
    GST_DEBUG_OBJECT (appsrc, "refuse EOS, we are flushing");
    return GST_FLOW_WRONG_STATE;
  }
}

/*** GSTURIHANDLER INTERFACE *************************************************/

static GstURIType
gst_app_src_uri_get_type (void)
{
  return GST_URI_SRC;
}

static gchar **
gst_app_src_uri_get_protocols (void)
{
  static gchar *protocols[] = { "appsrc", NULL };

  return protocols;
}
static const gchar *
gst_app_src_uri_get_uri (GstURIHandler * handler)
{
  return "appsrc";
}

static gboolean
gst_app_src_uri_set_uri (GstURIHandler * handler, const gchar * uri)
{
  gchar *protocol;
  gboolean ret;

  protocol = gst_uri_get_protocol (uri);
  ret = !strcmp (protocol, "appsrc");
  g_free (protocol);

  return ret;
}

static void
gst_app_src_uri_handler_init (gpointer g_iface, gpointer iface_data)
{
  GstURIHandlerInterface *iface = (GstURIHandlerInterface *) g_iface;

  iface->get_type = gst_app_src_uri_get_type;
  iface->get_protocols = gst_app_src_uri_get_protocols;
  iface->get_uri = gst_app_src_uri_get_uri;
  iface->set_uri = gst_app_src_uri_set_uri;
}
