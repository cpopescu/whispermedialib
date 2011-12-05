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

/**
 * SECTION:element-appsink
 * @see_also: #GstBaseSrc
 *
 * Appsink is a sink plugin that supports many different methods for making
 * the application get a handle on the GStreamer data in a pipeline.
 *
 * Last reviewed on 2008-05-03 (0.10.8)
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include <gst/base/gstbasesink.h>
#include <gst/gstbuffer.h>

#include <string.h>

#include "gstappsink.h"


GST_DEBUG_CATEGORY (app_sink_debug);
#define GST_CAT_DEFAULT app_sink_debug

static const GstElementDetails app_sink_details =
GST_ELEMENT_DETAILS ("AppSink",
    "Generic/Sink",
    "Allow the application to get access to raw buffer",
    "David Schleef <ds@schleef.org>, Wim Taymans <wim.taymans@gmail.com");

enum
{
  /* signals */
  SIGNAL_EOS,
  SIGNAL_NEW_PREROLL,
  SIGNAL_NEW_BUFFER,

  /* actions */
  SIGNAL_PULL_PREROLL,
  SIGNAL_PULL_BUFFER,

  LAST_SIGNAL
};

#define DEFAULT_PROP_EOS		TRUE
#define DEFAULT_PROP_EMIT_SIGNALS	FALSE
#define DEFAULT_PROP_MAX_BUFFERS	0
#define DEFAULT_PROP_DROP		FALSE

enum
{
  PROP_0,
  PROP_CAPS,
  PROP_EOS,
  PROP_EMIT_SIGNALS,
  PROP_MAX_BUFFERS,
  PROP_DROP,
  PROP_LAST
};

static GstStaticPadTemplate gst_app_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

static void gst_app_sink_dispose (GObject * object);
static void gst_app_sink_finalize (GObject * object);

static void gst_app_sink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_app_sink_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static gboolean gst_app_sink_unlock_start (GstBaseSink * bsink);
static gboolean gst_app_sink_unlock_stop (GstBaseSink * bsink);
static gboolean gst_app_sink_start (GstBaseSink * psink);
static gboolean gst_app_sink_stop (GstBaseSink * psink);
static gboolean gst_app_sink_event (GstBaseSink * sink, GstEvent * event);
static GstFlowReturn gst_app_sink_preroll (GstBaseSink * psink,
    GstBuffer * buffer);
static GstFlowReturn gst_app_sink_render (GstBaseSink * psink,
    GstBuffer * buffer);
static GstCaps *gst_app_sink_getcaps (GstBaseSink * psink);

static guint gst_app_sink_signals[LAST_SIGNAL] = { 0 };

GST_BOILERPLATE (GstAppSink, gst_app_sink, GstBaseSink, GST_TYPE_BASE_SINK);

void
gst_app_marshal_OBJECT__VOID (GClosure * closure,
    GValue * return_value,
    guint n_param_values,
    const GValue * param_values,
    gpointer invocation_hint, gpointer marshal_data)
{
  typedef GstBuffer *(*GMarshalFunc_OBJECT__VOID) (gpointer data1,
      gpointer data2);
  register GMarshalFunc_OBJECT__VOID callback;
  register GCClosure *cc = (GCClosure *) closure;
  register gpointer data1, data2;
  GstBuffer *v_return;

  g_return_if_fail (return_value != NULL);
  g_return_if_fail (n_param_values == 1);

  if (G_CCLOSURE_SWAP_DATA (closure)) {
    data1 = closure->data;
    data2 = g_value_peek_pointer (param_values + 0);
  } else {
    data1 = g_value_peek_pointer (param_values + 0);
    data2 = closure->data;
  }
  callback =
      (GMarshalFunc_OBJECT__VOID) (marshal_data ? marshal_data : cc->callback);

  v_return = callback (data1, data2);

  gst_value_take_buffer (return_value, v_return);
}

static void
gst_app_sink_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  GST_DEBUG_CATEGORY_INIT (app_sink_debug, "appsink", 0, "appsink element");

  gst_element_class_set_details (element_class, &app_sink_details);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_app_sink_template));
}

static void
gst_app_sink_class_init (GstAppSinkClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;
  GstBaseSinkClass *basesink_class = (GstBaseSinkClass *) klass;

  gobject_class->dispose = gst_app_sink_dispose;
  gobject_class->finalize = gst_app_sink_finalize;

  gobject_class->set_property = gst_app_sink_set_property;
  gobject_class->get_property = gst_app_sink_get_property;

  g_object_class_install_property (gobject_class, PROP_CAPS,
      g_param_spec_boxed ("caps", "Caps",
          "The allowed caps for the sink pad", GST_TYPE_CAPS,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_EOS,
      g_param_spec_boolean ("eos", "EOS",
          "Check if the sink is EOS or not started", DEFAULT_PROP_EOS,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_EMIT_SIGNALS,
      g_param_spec_boolean ("emit-signals", "Emit signals",
          "Emit new-preroll and new-buffer signals", DEFAULT_PROP_EMIT_SIGNALS,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_MAX_BUFFERS,
      g_param_spec_uint ("max-buffers", "Max Buffers",
          "The maximum number of buffers to queue internally (0 = unlimited)",
          0, G_MAXUINT, DEFAULT_PROP_MAX_BUFFERS,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_DROP,
      g_param_spec_boolean ("drop", "Drop",
          "Drop old buffers when the buffer queue is filled", DEFAULT_PROP_DROP,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /**
   * GstAppSink::eos:
   * @appsink: the appsink element that emited the signal
   *
   * Signal that the end-of-stream has been reached. This signal is emited from
   * the steaming thread.
   */
  gst_app_sink_signals[SIGNAL_EOS] =
      g_signal_new ("eos", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
      G_STRUCT_OFFSET (GstAppSinkClass, eos),
      NULL, NULL, g_cclosure_marshal_VOID__VOID, G_TYPE_NONE, 0, G_TYPE_NONE);
  /**
   * GstAppSink::new-preroll:
   * @appsink: the appsink element that emited the signal
   *
   * Signal that a new preroll buffer is available. 
   *
   * This signal is emited from the steaming thread and only when the
   * "emit-signals" property is %TRUE. 
   *
   * The new preroll buffer can be retrieved with the "pull-preroll" action
   * signal or gst_app_sink_pull_preroll() either from this signal callback
   * or from any other thread.
   *
   * Note that this signal is only emited when the "emit-signals" property is
   * set to %TRUE, which it is not by default for performance reasons.
   */
  gst_app_sink_signals[SIGNAL_NEW_PREROLL] =
      g_signal_new ("new-preroll", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
      G_STRUCT_OFFSET (GstAppSinkClass, new_preroll),
      NULL, NULL, g_cclosure_marshal_VOID__VOID, G_TYPE_NONE, 0, G_TYPE_NONE);
  /**
   * GstAppSink::new-buffer:
   * @appsink: the appsink element that emited the signal
   *
   * Signal that a new buffer is available.
   *
   * This signal is emited from the steaming thread and only when the
   * "emit-signals" property is %TRUE. 
   *
   * The new preroll buffer can be retrieved with the "pull-buffer" action
   * signal or gst_app_sink_pull_buffer() either from this signal callback
   * or from any other thread.
   *
   * Note that this signal is only emited when the "emit-signals" property is
   * set to %TRUE, which it is not by default for performance reasons.
   */
  gst_app_sink_signals[SIGNAL_NEW_BUFFER] =
      g_signal_new ("new-buffer", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
      G_STRUCT_OFFSET (GstAppSinkClass, new_buffer),
      NULL, NULL, g_cclosure_marshal_VOID__VOID, G_TYPE_NONE, 0, G_TYPE_NONE);

  /**
   * GstAppSink::pull-preroll:
   * @appsink: the appsink element to emit this signal on
   *
   * Get the last preroll buffer in @appsink. This was the buffer that caused the
   * appsink to preroll in the PAUSED state. This buffer can be pulled many times
   * and remains available to the application even after EOS.
   *
   * This function is typically used when dealing with a pipeline in the PAUSED
   * state. Calling this function after doing a seek will give the buffer right
   * after the seek position.
   *
   * Note that the preroll buffer will also be returned as the first buffer
   * when calling gst_app_sink_pull_buffer() or the "pull-buffer" action signal.
   *
   * If an EOS event was received before any buffers, this function returns
   * %NULL. Use gst_app_sink_is_eos () to check for the EOS condition. 
   *
   * This function blocks until a preroll buffer or EOS is received or the appsink
   * element is set to the READY/NULL state. 
   *
   * Returns: a #GstBuffer or NULL when the appsink is stopped or EOS.
   */
  gst_app_sink_signals[SIGNAL_PULL_PREROLL] =
      g_signal_new ("pull-preroll", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION, G_STRUCT_OFFSET (GstAppSinkClass,
          pull_preroll), NULL, NULL, gst_app_marshal_OBJECT__VOID,
      GST_TYPE_BUFFER, 0, G_TYPE_NONE);
  /**
   * GstAppSink::pull-buffer:
   * @appsink: the appsink element to emit this signal on
   *
   * This function blocks until a buffer or EOS becomes available or the appsink
   * element is set to the READY/NULL state. 
   *
   * This function will only return buffers when the appsink is in the PLAYING
   * state. All rendered buffers will be put in a queue so that the application
   * can pull buffers at its own rate. 
   *
   * Note that when the application does not pull buffers fast enough, the
   * queued buffers could consume a lot of memory, especially when dealing with
   * raw video frames. It's possible to control the behaviour of the queue with
   * the "drop" and "max-buffers" properties.
   *
   * If an EOS event was received before any buffers, this function returns
   * %NULL. Use gst_app_sink_is_eos () to check for the EOS condition. 
   *
   * Returns: a #GstBuffer or NULL when the appsink is stopped or EOS.
   */
  gst_app_sink_signals[SIGNAL_PULL_PREROLL] =
      g_signal_new ("pull-buffer", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION, G_STRUCT_OFFSET (GstAppSinkClass,
          pull_buffer), NULL, NULL, gst_app_marshal_OBJECT__VOID,
      GST_TYPE_BUFFER, 0, G_TYPE_NONE);

  basesink_class->unlock = gst_app_sink_unlock_start;
  basesink_class->unlock_stop = gst_app_sink_unlock_stop;
  basesink_class->start = gst_app_sink_start;
  basesink_class->stop = gst_app_sink_stop;
  basesink_class->event = gst_app_sink_event;
  basesink_class->preroll = gst_app_sink_preroll;
  basesink_class->render = gst_app_sink_render;
  basesink_class->get_caps = gst_app_sink_getcaps;

  klass->pull_preroll = gst_app_sink_pull_preroll;
  klass->pull_buffer = gst_app_sink_pull_buffer;
}

static void
gst_app_sink_init (GstAppSink * appsink, GstAppSinkClass * klass)
{
  appsink->mutex = g_mutex_new ();
  appsink->cond = g_cond_new ();
  appsink->queue = g_queue_new ();

  appsink->emit_signals = DEFAULT_PROP_EMIT_SIGNALS;
  appsink->max_buffers = DEFAULT_PROP_MAX_BUFFERS;
  appsink->drop = DEFAULT_PROP_DROP;
}

static void
gst_app_sink_dispose (GObject * obj)
{
  GstAppSink *appsink = GST_APP_SINK (obj);
  GstBuffer *buffer;

  GST_OBJECT_LOCK (appsink);
  if (appsink->caps) {
    gst_caps_unref (appsink->caps);
    appsink->caps = NULL;
  }
  GST_OBJECT_UNLOCK (appsink);

  g_mutex_lock (appsink->mutex);
  if (appsink->preroll) {
    gst_buffer_unref (appsink->preroll);
    appsink->preroll = NULL;
  }
  while ((buffer = g_queue_pop_head (appsink->queue)))
    gst_buffer_unref (buffer);
  g_mutex_unlock (appsink->mutex);

  G_OBJECT_CLASS (parent_class)->dispose (obj);
}

static void
gst_app_sink_finalize (GObject * obj)
{
  GstAppSink *appsink = GST_APP_SINK (obj);

  g_mutex_free (appsink->mutex);
  g_cond_free (appsink->cond);
  g_queue_free (appsink->queue);

  G_OBJECT_CLASS (parent_class)->finalize (obj);
}

static void
gst_app_sink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstAppSink *appsink = GST_APP_SINK (object);

  switch (prop_id) {
    case PROP_CAPS:
      gst_app_sink_set_caps (appsink, gst_value_get_caps (value));
      break;
    case PROP_EMIT_SIGNALS:
      gst_app_sink_set_emit_signals (appsink, g_value_get_boolean (value));
      break;
    case PROP_MAX_BUFFERS:
      gst_app_sink_set_max_buffers (appsink, g_value_get_uint (value));
      break;
    case PROP_DROP:
      gst_app_sink_set_drop (appsink, g_value_get_boolean (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_app_sink_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstAppSink *appsink = GST_APP_SINK (object);

  switch (prop_id) {
    case PROP_CAPS:
    {
      GstCaps *caps;

      caps = gst_app_sink_get_caps (appsink);
      gst_value_set_caps (value, caps);
      if (caps)
        gst_caps_unref (caps);
      break;
    }
    case PROP_EOS:
      g_value_set_boolean (value, gst_app_sink_is_eos (appsink));
      break;
    case PROP_EMIT_SIGNALS:
      g_value_set_boolean (value, gst_app_sink_get_emit_signals (appsink));
      break;
    case PROP_MAX_BUFFERS:
      g_value_set_uint (value, gst_app_sink_get_max_buffers (appsink));
      break;
    case PROP_DROP:
      g_value_set_boolean (value, gst_app_sink_get_drop (appsink));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
gst_app_sink_unlock_start (GstBaseSink * bsink)
{
  GstAppSink *appsink = GST_APP_SINK (bsink);

  g_mutex_lock (appsink->mutex);
  GST_DEBUG_OBJECT (appsink, "unlock start");
  appsink->flushing = TRUE;
  g_cond_signal (appsink->cond);
  g_mutex_unlock (appsink->mutex);

  return TRUE;
}

static gboolean
gst_app_sink_unlock_stop (GstBaseSink * bsink)
{
  GstAppSink *appsink = GST_APP_SINK (bsink);

  g_mutex_lock (appsink->mutex);
  GST_DEBUG_OBJECT (appsink, "unlock stop");
  appsink->flushing = FALSE;
  g_cond_signal (appsink->cond);
  g_mutex_unlock (appsink->mutex);

  return TRUE;
}

static void
gst_app_sink_flush_unlocked (GstAppSink * appsink)
{
  GstBuffer *buffer;

  GST_DEBUG_OBJECT (appsink, "flush stop appsink");
  appsink->is_eos = FALSE;
  gst_buffer_replace (&appsink->preroll, NULL);
  while ((buffer = g_queue_pop_head (appsink->queue)))
    gst_buffer_unref (buffer);
  g_cond_signal (appsink->cond);
}

static gboolean
gst_app_sink_start (GstBaseSink * psink)
{
  GstAppSink *appsink = GST_APP_SINK (psink);

  g_mutex_lock (appsink->mutex);
  GST_DEBUG_OBJECT (appsink, "starting");
  appsink->started = TRUE;
  g_mutex_unlock (appsink->mutex);

  return TRUE;
}

static gboolean
gst_app_sink_stop (GstBaseSink * psink)
{
  GstAppSink *appsink = GST_APP_SINK (psink);

  g_mutex_lock (appsink->mutex);
  GST_DEBUG_OBJECT (appsink, "stopping");
  appsink->flushing = TRUE;
  appsink->started = FALSE;
  gst_app_sink_flush_unlocked (appsink);
  g_mutex_unlock (appsink->mutex);

  return TRUE;
}

static gboolean
gst_app_sink_event (GstBaseSink * sink, GstEvent * event)
{
  GstAppSink *appsink = GST_APP_SINK (sink);

  switch (event->type) {
    case GST_EVENT_EOS:

      g_mutex_lock (appsink->mutex);
      GST_DEBUG_OBJECT (appsink, "receiving EOS");
      appsink->is_eos = TRUE;
      g_cond_signal (appsink->cond);
      g_mutex_unlock (appsink->mutex);

      /* emit EOS now */
      g_signal_emit (appsink, gst_app_sink_signals[SIGNAL_EOS], 0);
      break;
    case GST_EVENT_FLUSH_START:
      /* we don't have to do anything here, the base class will call unlock
       * which will make sure we exit the _render method */
      GST_DEBUG_OBJECT (appsink, "received FLUSH_START");
      break;
    case GST_EVENT_FLUSH_STOP:
      g_mutex_lock (appsink->mutex);
      GST_DEBUG_OBJECT (appsink, "received FLUSH_STOP");
      gst_app_sink_flush_unlocked (appsink);
      g_mutex_unlock (appsink->mutex);
      break;
    default:
      break;
  }
  return TRUE;
}

static GstFlowReturn
gst_app_sink_preroll (GstBaseSink * psink, GstBuffer * buffer)
{
  GstAppSink *appsink = GST_APP_SINK (psink);
  gboolean emit;

  g_mutex_lock (appsink->mutex);
  if (appsink->flushing)
    goto flushing;

  GST_DEBUG_OBJECT (appsink, "setting preroll buffer %p", buffer);
  gst_buffer_replace (&appsink->preroll, buffer);
  g_cond_signal (appsink->cond);
  emit = appsink->emit_signals;
  g_mutex_unlock (appsink->mutex);

  if (emit)
    g_signal_emit (appsink, gst_app_sink_signals[SIGNAL_NEW_PREROLL], 0);

  return GST_FLOW_OK;

flushing:
  {
    GST_DEBUG_OBJECT (appsink, "we are flushing");
    g_mutex_unlock (appsink->mutex);
    return GST_FLOW_WRONG_STATE;
  }
}

static GstFlowReturn
gst_app_sink_render (GstBaseSink * psink, GstBuffer * buffer)
{
  GstAppSink *appsink = GST_APP_SINK (psink);
  gboolean emit;

  g_mutex_lock (appsink->mutex);
  if (appsink->flushing)
    goto flushing;

  GST_DEBUG_OBJECT (appsink, "pushing render buffer %p on queue (%d)",
      buffer, appsink->queue->length);

  while (appsink->max_buffers > 0 &&
      appsink->queue->length >= appsink->max_buffers) {
    if (appsink->drop) {
      GstBuffer *buf;

      /* we need to drop the oldest buffer and try again */
      buf = g_queue_pop_head (appsink->queue);
      GST_DEBUG_OBJECT (appsink, "dropping old buffer %p", buf);
      gst_buffer_unref (buf);
    } else {
      GST_DEBUG_OBJECT (appsink, "waiting for free space, length %d >= %d",
          appsink->queue->length, appsink->max_buffers);
      /* wait for a buffer to be removed or flush */
      g_cond_wait (appsink->cond, appsink->mutex);
      if (appsink->flushing)
        goto flushing;
    }
  }
  /* we need to ref the buffer when pushing it in the queue */
  g_queue_push_tail (appsink->queue, gst_buffer_ref (buffer));
  g_cond_signal (appsink->cond);
  emit = appsink->emit_signals;
  g_mutex_unlock (appsink->mutex);

  if (emit)
    g_signal_emit (appsink, gst_app_sink_signals[SIGNAL_NEW_BUFFER], 0);

  return GST_FLOW_OK;

flushing:
  {
    GST_DEBUG_OBJECT (appsink, "we are flushing");
    g_mutex_unlock (appsink->mutex);
    return GST_FLOW_WRONG_STATE;
  }
}

static GstCaps *
gst_app_sink_getcaps (GstBaseSink * psink)
{
  GstCaps *caps;

  GstAppSink *appsink = GST_APP_SINK (psink);

  GST_OBJECT_LOCK (appsink);
  if ((caps = appsink->caps))
    gst_caps_ref (caps);
  GST_DEBUG_OBJECT (appsink, "got caps %" GST_PTR_FORMAT, caps);
  GST_OBJECT_UNLOCK (appsink);

  return caps;
}

/* external API */

/**
 * gst_app_sink_set_caps:
 * @appsink: a #GstAppSink
 * @caps: caps to set
 *
 * Set the capabilities on the appsink element.  This function takes
 * a copy of the caps structure. After calling this method, the sink will only
 * accept caps that match @caps. If @caps is non-fixed, you must check the caps
 * on the buffers to get the actual used caps. 
 */
void
gst_app_sink_set_caps (GstAppSink * appsink, const GstCaps * caps)
{
  GstCaps *old;

  g_return_if_fail (appsink != NULL);
  g_return_if_fail (GST_IS_APP_SINK (appsink));

  GST_OBJECT_LOCK (appsink);
  GST_DEBUG_OBJECT (appsink, "setting caps to %" GST_PTR_FORMAT, caps);
  if ((old = appsink->caps) != caps) {
    if (caps)
      appsink->caps = gst_caps_copy (caps);
    else
      appsink->caps = NULL;
    if (old)
      gst_caps_unref (old);
  }
  GST_OBJECT_UNLOCK (appsink);
}

/**
 * gst_app_sink_get_caps:
 * @appsink: a #GstAppSink
 *
 * Get the configured caps on @appsink.
 *
 * Returns: the #GstCaps accepted by the sink. gst_caps_unref() after usage.
 */
GstCaps *
gst_app_sink_get_caps (GstAppSink * appsink)
{
  GstCaps *caps;

  g_return_val_if_fail (appsink != NULL, NULL);
  g_return_val_if_fail (GST_IS_APP_SINK (appsink), NULL);

  GST_OBJECT_LOCK (appsink);
  if ((caps = appsink->caps))
    gst_caps_ref (caps);
  GST_DEBUG_OBJECT (appsink, "getting caps of %" GST_PTR_FORMAT, caps);
  GST_OBJECT_UNLOCK (appsink);

  return caps;
}

/**
 * gst_app_sink_is_eos:
 * @appsink: a #GstAppSink
 *
 * Check if @appsink is EOS, which is when no more buffers can be pulled because
 * an EOS event was received.
 *
 * This function also returns %TRUE when the appsink is not in the PAUSED or
 * PLAYING state.
 *
 * Returns: %TRUE if no more buffers can be pulled and the appsink is EOS.
 */
gboolean
gst_app_sink_is_eos (GstAppSink * appsink)
{
  gboolean ret;

  g_return_val_if_fail (appsink != NULL, FALSE);
  g_return_val_if_fail (GST_IS_APP_SINK (appsink), FALSE);

  g_mutex_lock (appsink->mutex);
  if (!appsink->started)
    goto not_started;

  if (appsink->is_eos && g_queue_is_empty (appsink->queue)) {
    GST_DEBUG_OBJECT (appsink, "we are EOS and the queue is empty");
    ret = TRUE;
  } else {
    GST_DEBUG_OBJECT (appsink, "we are not yet EOS");
    ret = FALSE;
  }
  g_mutex_unlock (appsink->mutex);

  return ret;

not_started:
  {
    GST_DEBUG_OBJECT (appsink, "we are stopped, return TRUE");
    g_mutex_unlock (appsink->mutex);
    return TRUE;
  }
}

/**
 * gst_app_sink_set_emit_signals:
 * @appsink: a #GstAppSink
 * @emit: the new state
 *
 * Make appsink emit the "new-preroll" and "new-buffer" signals. This option is
 * by default disabled because signal emission is expensive and unneeded when
 * the application prefers to operate in pull mode.
 */
void
gst_app_sink_set_emit_signals (GstAppSink * appsink, gboolean emit)
{
  g_return_if_fail (GST_IS_APP_SINK (appsink));

  g_mutex_lock (appsink->mutex);
  appsink->emit_signals = emit;
  g_mutex_unlock (appsink->mutex);
}

/**
 * gst_app_sink_get_emit_signals:
 * @appsink: a #GstAppSink
 *
 * Check if appsink will emit the "new-preroll" and "new-buffer" signals.
 *
 * Returns: %TRUE if @appsink is emiting the "new-preroll" and "new-buffer"
 * signals.
 */
gboolean
gst_app_sink_get_emit_signals (GstAppSink * appsink)
{
  gboolean result;

  g_return_val_if_fail (GST_IS_APP_SINK (appsink), FALSE);

  g_mutex_lock (appsink->mutex);
  result = appsink->emit_signals;
  g_mutex_unlock (appsink->mutex);

  return result;
}

/**
 * gst_app_sink_set_max_buffers:
 * @appsink: a #GstAppSink
 * @max: the maximum number of buffers to queue
 *
 * Set the maximum amount of buffers that can be queued in @appsink. After this
 * amount of buffers are queued in appsink, any more buffers will block upstream
 * elements until a buffer is pulled from @appsink.
 */
void
gst_app_sink_set_max_buffers (GstAppSink * appsink, guint max)
{
  g_return_if_fail (GST_IS_APP_SINK (appsink));

  g_mutex_lock (appsink->mutex);
  if (max != appsink->max_buffers) {
    appsink->max_buffers = max;
    /* signal the change */
    g_cond_signal (appsink->cond);
  }
  g_mutex_unlock (appsink->mutex);
}

/**
 * gst_app_sink_get_max_buffers:
 * @appsink: a #GstAppSink
 *
 * Get the maximum amount of buffers that can be queued in @appsink.
 *
 * Returns: The maximum amount of buffers that can be queued.
 */
guint
gst_app_sink_get_max_buffers (GstAppSink * appsink)
{
  guint result;

  g_return_val_if_fail (GST_IS_APP_SINK (appsink), 0);

  g_mutex_lock (appsink->mutex);
  result = appsink->max_buffers;
  g_mutex_unlock (appsink->mutex);

  return result;
}

/**
 * gst_app_sink_set_drop:
 * @appsink: a #GstAppSink
 * @emit: the new state
 *
 * Instruct @appsink to drop old buffers when the maximum amount of queued
 * buffers is reached.
 */
void
gst_app_sink_set_drop (GstAppSink * appsink, gboolean drop)
{
  g_return_if_fail (GST_IS_APP_SINK (appsink));

  g_mutex_lock (appsink->mutex);
  if (appsink->drop != drop) {
    appsink->drop = drop;
    /* signal the change */
    g_cond_signal (appsink->cond);
  }
  g_mutex_unlock (appsink->mutex);
}

/**
 * gst_app_sink_get_drop:
 * @appsink: a #GstAppSink
 *
 * Check if @appsink will drop old buffers when the maximum amount of queued
 * buffers is reached.
 *
 * Returns: %TRUE if @appsink is dropping old buffers when the queue is
 * filled.
 */
gboolean
gst_app_sink_get_drop (GstAppSink * appsink)
{
  gboolean result;

  g_return_val_if_fail (GST_IS_APP_SINK (appsink), FALSE);

  g_mutex_lock (appsink->mutex);
  result = appsink->drop;
  g_mutex_unlock (appsink->mutex);

  return result;
}

/**
 * gst_app_sink_pull_preroll:
 * @appsink: a #GstAppSink
 *
 * Get the last preroll buffer in @appsink. This was the buffer that caused the
 * appsink to preroll in the PAUSED state. This buffer can be pulled many times
 * and remains available to the application even after EOS.
 *
 * This function is typically used when dealing with a pipeline in the PAUSED
 * state. Calling this function after doing a seek will give the buffer right
 * after the seek position.
 *
 * Note that the preroll buffer will also be returned as the first buffer
 * when calling gst_app_sink_pull_buffer().
 *
 * If an EOS event was received before any buffers, this function returns
 * %NULL. Use gst_app_sink_is_eos () to check for the EOS condition. 
 *
 * This function blocks until a preroll buffer or EOS is received or the appsink
 * element is set to the READY/NULL state. 
 *
 * Returns: a #GstBuffer or NULL when the appsink is stopped or EOS.
 */
GstBuffer *
gst_app_sink_pull_preroll (GstAppSink * appsink)
{
  GstBuffer *buf = NULL;

  g_return_val_if_fail (appsink != NULL, NULL);
  g_return_val_if_fail (GST_IS_APP_SINK (appsink), NULL);

  g_mutex_lock (appsink->mutex);

  while (TRUE) {
    GST_DEBUG_OBJECT (appsink, "trying to grab a buffer");
    if (!appsink->started)
      goto not_started;

    if (appsink->preroll != NULL)
      break;

    if (appsink->is_eos)
      goto eos;

    /* nothing to return, wait */
    GST_DEBUG_OBJECT (appsink, "waiting for the preroll buffer");
    g_cond_wait (appsink->cond, appsink->mutex);
  }
  buf = gst_buffer_ref (appsink->preroll);
  GST_DEBUG_OBJECT (appsink, "we have the preroll buffer %p", buf);
  g_mutex_unlock (appsink->mutex);

  return buf;

  /* special conditions */
eos:
  {
    GST_DEBUG_OBJECT (appsink, "we are EOS, return NULL");
    g_mutex_unlock (appsink->mutex);
    return NULL;
  }
not_started:
  {
    GST_DEBUG_OBJECT (appsink, "we are stopped, return NULL");
    g_mutex_unlock (appsink->mutex);
    return NULL;
  }
}

/**
 * gst_app_sink_pull_buffer:
 * @appsink: a #GstAppSink
 *
 * This function blocks until a buffer or EOS becomes available or the appsink
 * element is set to the READY/NULL state. 
 *
 * This function will only return buffers when the appsink is in the PLAYING
 * state. All rendered buffers will be put in a queue so that the application
 * can pull buffers at its own rate. Note that when the application does not
 * pull buffers fast enough, the queued buffers could consume a lot of memory,
 * especially when dealing with raw video frames.
 *
 * If an EOS event was received before any buffers, this function returns
 * %NULL. Use gst_app_sink_is_eos () to check for the EOS condition. 
 *
 * Returns: a #GstBuffer or NULL when the appsink is stopped or EOS.
 */
GstBuffer *
gst_app_sink_pull_buffer (GstAppSink * appsink)
{
  GstBuffer *buf = NULL;

  g_return_val_if_fail (appsink != NULL, NULL);
  g_return_val_if_fail (GST_IS_APP_SINK (appsink), NULL);

  g_mutex_lock (appsink->mutex);

  while (TRUE) {
    GST_DEBUG_OBJECT (appsink, "trying to grab a buffer");
    if (!appsink->started)
      goto not_started;

    if (!g_queue_is_empty (appsink->queue))
      break;

    if (appsink->is_eos)
      goto eos;

    /* nothing to return, wait */
    GST_DEBUG_OBJECT (appsink, "waiting for a buffer");
    g_cond_wait (appsink->cond, appsink->mutex);
  }
  buf = g_queue_pop_head (appsink->queue);
  GST_DEBUG_OBJECT (appsink, "we have a buffer %p", buf);
  g_cond_signal (appsink->cond);
  g_mutex_unlock (appsink->mutex);

  return buf;

  /* special conditions */
eos:
  {
    GST_DEBUG_OBJECT (appsink, "we are EOS, return NULL");
    g_mutex_unlock (appsink->mutex);
    return NULL;
  }
not_started:
  {
    GST_DEBUG_OBJECT (appsink, "we are stopped, return NULL");
    g_mutex_unlock (appsink->mutex);
    return NULL;
  }
}
