/*
 * Farsight Voice+Video library
 *
 *  Copyright 2007 Collabora Ltd, 
 *  Copyright 2007 Nokia Corporation
 *   @author: Philippe Kalaf <philippe.kalaf@collabora.co.uk>.
 *  Copyright 2007 Wim Taymans <wim.taymans@gmail.com>
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
 *
 */

/**
 * SECTION:element-gstrtpjitterbuffer
 *
 * This element reorders and removes duplicate RTP packets as they are received
 * from a network source. It will also wait for missing packets up to a
 * configurable time limit using the #GstRtpJitterBuffer:latency property.
 * Packets arriving too late are considered to be lost packets.
 * 
 * This element acts as a live element and so adds #GstRtpJitterBuffer:latency
 * to the pipeline.
 * 
 * The element needs the clock-rate of the RTP payload in order to estimate the
 * delay. This information is obtained either from the caps on the sink pad or,
 * when no caps are present, from the #GstRtpJitterBuffer::request-pt-map signal.
 * To clear the previous pt-map use the #GstRtpJitterBuffer::clear-pt-map signal.
 * 
 * This element will automatically be used inside gstrtpbin.
 * 
 * <refsect2>
 * <title>Example pipelines</title>
 * |[
 * gst-launch rtspsrc location=rtsp://192.168.1.133:8554/mpeg1or2AudioVideoTest ! gstrtpjitterbuffer ! rtpmpvdepay ! mpeg2dec ! xvimagesink
 * ]| Connect to a streaming server and decode the MPEG video. The jitterbuffer is
 * inserted into the pipeline to smooth out network jitter and to reorder the
 * out-of-order RTP packets.
 * </refsect2>
 *
 * Last reviewed on 2007-05-28 (0.10.5)
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdlib.h>
#include <string.h>
#include <gst/rtp/gstrtpbuffer.h>

#include "gstrtpbin-marshal.h"

#include "gstrtpjitterbuffer.h"
#include "rtpjitterbuffer.h"

GST_DEBUG_CATEGORY (rtpjitterbuffer_debug);
#define GST_CAT_DEFAULT (rtpjitterbuffer_debug)

/* low and high threshold tell the queue when to start and stop buffering */
#define LOW_THRESHOLD 0.2
#define HIGH_THRESHOLD 0.8

/* elementfactory information */
static const GstElementDetails gst_rtp_jitter_buffer_details =
GST_ELEMENT_DETAILS ("RTP packet jitter-buffer",
    "Filter/Network/RTP",
    "A buffer that deals with network jitter and other transmission faults",
    "Philippe Kalaf <philippe.kalaf@collabora.co.uk>, "
    "Wim Taymans <wim.taymans@gmail.com>");

/* RTPJitterBuffer signals and args */
enum
{
  SIGNAL_REQUEST_PT_MAP,
  SIGNAL_CLEAR_PT_MAP,
  LAST_SIGNAL
};

#define DEFAULT_LATENCY_MS      200
#define DEFAULT_DROP_ON_LATENCY FALSE
#define DEFAULT_TS_OFFSET       0
#define DEFAULT_DO_LOST         FALSE

enum
{
  PROP_0,
  PROP_LATENCY,
  PROP_DROP_ON_LATENCY,
  PROP_TS_OFFSET,
  PROP_DO_LOST,
  PROP_LAST
};

#define JBUF_LOCK(priv)   (g_mutex_lock ((priv)->jbuf_lock))

#define JBUF_LOCK_CHECK(priv,label) G_STMT_START {    \
  JBUF_LOCK (priv);                                   \
  if (priv->srcresult != GST_FLOW_OK)                 \
    goto label;                                       \
} G_STMT_END

#define JBUF_UNLOCK(priv) (g_mutex_unlock ((priv)->jbuf_lock))
#define JBUF_WAIT(priv)   (g_cond_wait ((priv)->jbuf_cond, (priv)->jbuf_lock))

#define JBUF_WAIT_CHECK(priv,label) G_STMT_START {    \
  JBUF_WAIT(priv);                                    \
  if (priv->srcresult != GST_FLOW_OK)                 \
    goto label;                                       \
} G_STMT_END

#define JBUF_SIGNAL(priv) (g_cond_signal ((priv)->jbuf_cond))

struct _GstRtpJitterBufferPrivate
{
  GstPad *sinkpad, *srcpad;

  RTPJitterBuffer *jbuf;
  GMutex *jbuf_lock;
  GCond *jbuf_cond;
  gboolean waiting;
  gboolean discont;

  /* properties */
  guint latency_ms;
  gboolean drop_on_latency;
  gint64 ts_offset;
  gboolean do_lost;

  /* the last seqnum we pushed out */
  guint32 last_popped_seqnum;
  /* the next expected seqnum */
  guint32 next_seqnum;
  /* last output time */
  GstClockTime last_out_time;

  /* state */
  gboolean eos;

  /* clock rate and rtp timestamp offset */
  gint last_pt;
  gint32 clock_rate;
  gint64 clock_base;
  gint64 prev_ts_offset;

  /* when we are shutting down */
  GstFlowReturn srcresult;
  gboolean blocked;

  /* for sync */
  GstSegment segment;
  GstClockID clock_id;
  /* the latency of the upstream peer, we have to take this into account when
   * synchronizing the buffers. */
  GstClockTime peer_latency;

  /* some accounting */
  guint64 num_late;
  guint64 num_duplicates;
};

#define GST_RTP_JITTER_BUFFER_GET_PRIVATE(o) \
  (G_TYPE_INSTANCE_GET_PRIVATE ((o), GST_TYPE_RTP_JITTER_BUFFER, \
                                GstRtpJitterBufferPrivate))

static GstStaticPadTemplate gst_rtp_jitter_buffer_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-rtp, "
        "clock-rate = (int) [ 1, 2147483647 ]"
        /* "payload = (int) , "
         * "encoding-name = (string) "
         */ )
    );

static GstStaticPadTemplate gst_rtp_jitter_buffer_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-rtp"
        /* "payload = (int) , "
         * "clock-rate = (int) , "
         * "encoding-name = (string) "
         */ )
    );

static guint gst_rtp_jitter_buffer_signals[LAST_SIGNAL] = { 0 };

GST_BOILERPLATE (GstRtpJitterBuffer, gst_rtp_jitter_buffer, GstElement,
    GST_TYPE_ELEMENT);

/* object overrides */
static void gst_rtp_jitter_buffer_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_rtp_jitter_buffer_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);
static void gst_rtp_jitter_buffer_finalize (GObject * object);

/* element overrides */
static GstStateChangeReturn gst_rtp_jitter_buffer_change_state (GstElement
    * element, GstStateChange transition);

/* pad overrides */
static GstCaps *gst_rtp_jitter_buffer_getcaps (GstPad * pad);

/* sinkpad overrides */
static gboolean gst_jitter_buffer_sink_setcaps (GstPad * pad, GstCaps * caps);
static gboolean gst_rtp_jitter_buffer_src_event (GstPad * pad,
    GstEvent * event);
static gboolean gst_rtp_jitter_buffer_sink_event (GstPad * pad,
    GstEvent * event);
static GstFlowReturn gst_rtp_jitter_buffer_chain (GstPad * pad,
    GstBuffer * buffer);

/* srcpad overrides */
static gboolean
gst_rtp_jitter_buffer_src_activate_push (GstPad * pad, gboolean active);
static void gst_rtp_jitter_buffer_loop (GstRtpJitterBuffer * jitterbuffer);
static gboolean gst_rtp_jitter_buffer_query (GstPad * pad, GstQuery * query);

static void
gst_rtp_jitter_buffer_clear_pt_map (GstRtpJitterBuffer * jitterbuffer);

static void
gst_rtp_jitter_buffer_base_init (gpointer klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_rtp_jitter_buffer_src_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_rtp_jitter_buffer_sink_template));
  gst_element_class_set_details (element_class, &gst_rtp_jitter_buffer_details);
}

static void
gst_rtp_jitter_buffer_class_init (GstRtpJitterBufferClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  g_type_class_add_private (klass, sizeof (GstRtpJitterBufferPrivate));

  gobject_class->finalize = GST_DEBUG_FUNCPTR (gst_rtp_jitter_buffer_finalize);

  gobject_class->set_property = gst_rtp_jitter_buffer_set_property;
  gobject_class->get_property = gst_rtp_jitter_buffer_get_property;

  /**
   * GstRtpJitterBuffer::latency:
   * 
   * The maximum latency of the jitterbuffer. Packets will be kept in the buffer
   * for at most this time.
   */
  g_object_class_install_property (gobject_class, PROP_LATENCY,
      g_param_spec_uint ("latency", "Buffer latency in ms",
          "Amount of ms to buffer", 0, G_MAXUINT, DEFAULT_LATENCY_MS,
          G_PARAM_READWRITE));
  /**
   * GstRtpJitterBuffer::drop-on-latency:
   * 
   * Drop oldest buffers when the queue is completely filled. 
   */
  g_object_class_install_property (gobject_class, PROP_DROP_ON_LATENCY,
      g_param_spec_boolean ("drop-on-latency",
          "Drop buffers when maximum latency is reached",
          "Tells the jitterbuffer to never exceed the given latency in size",
          DEFAULT_DROP_ON_LATENCY, G_PARAM_READWRITE));
  /**
   * GstRtpJitterBuffer::ts-offset:
   * 
   * Adjust GStreamer output buffer timestamps in the jitterbuffer with offset.
   * This is mainly used to ensure interstream synchronisation.
   */
  g_object_class_install_property (gobject_class, PROP_TS_OFFSET,
      g_param_spec_int64 ("ts-offset", "Timestamp Offset",
          "Adjust buffer timestamps with offset in nanoseconds", G_MININT64,
          G_MAXINT64, DEFAULT_TS_OFFSET,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /**
   * GstRtpJitterBuffer::do-lost:
   * 
   * Send out a GstRTPPacketLost event downstream when a packet is considered
   * lost.
   */
  g_object_class_install_property (gobject_class, PROP_DO_LOST,
      g_param_spec_boolean ("do-lost", "Do Lost",
          "Send an event downstream when a packet is lost", DEFAULT_DO_LOST,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  /**
   * GstRtpJitterBuffer::request-pt-map:
   * @buffer: the object which received the signal
   * @pt: the pt
   *
   * Request the payload type as #GstCaps for @pt.
   */
  gst_rtp_jitter_buffer_signals[SIGNAL_REQUEST_PT_MAP] =
      g_signal_new ("request-pt-map", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, G_STRUCT_OFFSET (GstRtpJitterBufferClass,
          request_pt_map), NULL, NULL, gst_rtp_bin_marshal_BOXED__UINT,
      GST_TYPE_CAPS, 1, G_TYPE_UINT);
  /**
   * GstRtpJitterBuffer::clear-pt-map:
   * @buffer: the object which received the signal
   *
   * Invalidate the clock-rate as obtained with the
   * #GstRtpJitterBuffer::request-pt-map signal.
   */
  gst_rtp_jitter_buffer_signals[SIGNAL_CLEAR_PT_MAP] =
      g_signal_new ("clear-pt-map", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, G_STRUCT_OFFSET (GstRtpJitterBufferClass,
          clear_pt_map), NULL, NULL, g_cclosure_marshal_VOID__VOID,
      G_TYPE_NONE, 0, G_TYPE_NONE);

  gstelement_class->change_state = gst_rtp_jitter_buffer_change_state;

  klass->clear_pt_map = GST_DEBUG_FUNCPTR (gst_rtp_jitter_buffer_clear_pt_map);

  GST_DEBUG_CATEGORY_INIT
      (rtpjitterbuffer_debug, "gstrtpjitterbuffer", 0, "RTP Jitter Buffer");
}

static void
gst_rtp_jitter_buffer_init (GstRtpJitterBuffer * jitterbuffer,
    GstRtpJitterBufferClass * klass)
{
  GstRtpJitterBufferPrivate *priv;

  priv = GST_RTP_JITTER_BUFFER_GET_PRIVATE (jitterbuffer);
  jitterbuffer->priv = priv;

  priv->latency_ms = DEFAULT_LATENCY_MS;
  priv->drop_on_latency = DEFAULT_DROP_ON_LATENCY;
  priv->do_lost = DEFAULT_DO_LOST;

  priv->jbuf = rtp_jitter_buffer_new ();
  priv->jbuf_lock = g_mutex_new ();
  priv->jbuf_cond = g_cond_new ();

  priv->srcpad =
      gst_pad_new_from_static_template (&gst_rtp_jitter_buffer_src_template,
      "src");

  gst_pad_set_activatepush_function (priv->srcpad,
      GST_DEBUG_FUNCPTR (gst_rtp_jitter_buffer_src_activate_push));
  gst_pad_set_query_function (priv->srcpad,
      GST_DEBUG_FUNCPTR (gst_rtp_jitter_buffer_query));
  gst_pad_set_getcaps_function (priv->srcpad,
      GST_DEBUG_FUNCPTR (gst_rtp_jitter_buffer_getcaps));
  gst_pad_set_event_function (priv->srcpad,
      GST_DEBUG_FUNCPTR (gst_rtp_jitter_buffer_src_event));

  priv->sinkpad =
      gst_pad_new_from_static_template (&gst_rtp_jitter_buffer_sink_template,
      "sink");

  gst_pad_set_chain_function (priv->sinkpad,
      GST_DEBUG_FUNCPTR (gst_rtp_jitter_buffer_chain));
  gst_pad_set_event_function (priv->sinkpad,
      GST_DEBUG_FUNCPTR (gst_rtp_jitter_buffer_sink_event));
  gst_pad_set_setcaps_function (priv->sinkpad,
      GST_DEBUG_FUNCPTR (gst_jitter_buffer_sink_setcaps));
  gst_pad_set_getcaps_function (priv->sinkpad,
      GST_DEBUG_FUNCPTR (gst_rtp_jitter_buffer_getcaps));

  gst_element_add_pad (GST_ELEMENT (jitterbuffer), priv->srcpad);
  gst_element_add_pad (GST_ELEMENT (jitterbuffer), priv->sinkpad);
}

static void
gst_rtp_jitter_buffer_finalize (GObject * object)
{
  GstRtpJitterBuffer *jitterbuffer;

  jitterbuffer = GST_RTP_JITTER_BUFFER (object);

  g_mutex_free (jitterbuffer->priv->jbuf_lock);
  g_cond_free (jitterbuffer->priv->jbuf_cond);

  g_object_unref (jitterbuffer->priv->jbuf);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_rtp_jitter_buffer_clear_pt_map (GstRtpJitterBuffer * jitterbuffer)
{
  GstRtpJitterBufferPrivate *priv;

  priv = jitterbuffer->priv;

  /* this will trigger a new pt-map request signal, FIXME, do something better. */
  priv->clock_rate = -1;
}

static GstCaps *
gst_rtp_jitter_buffer_getcaps (GstPad * pad)
{
  GstRtpJitterBuffer *jitterbuffer;
  GstRtpJitterBufferPrivate *priv;
  GstPad *other;
  GstCaps *caps;
  const GstCaps *templ;

  jitterbuffer = GST_RTP_JITTER_BUFFER (gst_pad_get_parent (pad));
  priv = jitterbuffer->priv;

  other = (pad == priv->srcpad ? priv->sinkpad : priv->srcpad);

  caps = gst_pad_peer_get_caps (other);

  templ = gst_pad_get_pad_template_caps (pad);
  if (caps == NULL) {
    GST_DEBUG_OBJECT (jitterbuffer, "copy template");
    caps = gst_caps_copy (templ);
  } else {
    GstCaps *intersect;

    GST_DEBUG_OBJECT (jitterbuffer, "intersect with template");

    intersect = gst_caps_intersect (caps, templ);
    gst_caps_unref (caps);

    caps = intersect;
  }
  gst_object_unref (jitterbuffer);

  return caps;
}

static gboolean
gst_jitter_buffer_sink_parse_caps (GstRtpJitterBuffer * jitterbuffer,
    GstCaps * caps)
{
  GstRtpJitterBufferPrivate *priv;
  GstStructure *caps_struct;
  guint val;

  priv = jitterbuffer->priv;

  /* first parse the caps */
  caps_struct = gst_caps_get_structure (caps, 0);

  GST_DEBUG_OBJECT (jitterbuffer, "got caps");

  /* we need a clock-rate to convert the rtp timestamps to GStreamer time and to
   * measure the amount of data in the buffer */
  if (!gst_structure_get_int (caps_struct, "clock-rate", &priv->clock_rate))
    goto error;

  if (priv->clock_rate <= 0)
    goto wrong_rate;

  GST_DEBUG_OBJECT (jitterbuffer, "got clock-rate %d", priv->clock_rate);

  /* gah, clock-base is uint. If we don't have a base, we will use the first
   * buffer timestamp as the base time. This will screw up sync but it's better
   * than nothing. */
  if (gst_structure_get_uint (caps_struct, "clock-base", &val))
    priv->clock_base = val;
  else
    priv->clock_base = -1;

  GST_DEBUG_OBJECT (jitterbuffer, "got clock-base %" G_GINT64_FORMAT,
      priv->clock_base);

  /* first expected seqnum */
  if (gst_structure_get_uint (caps_struct, "seqnum-base", &val))
    priv->next_seqnum = val;
  else
    priv->next_seqnum = -1;

  GST_DEBUG_OBJECT (jitterbuffer, "got seqnum-base %d", priv->next_seqnum);

  return TRUE;

  /* ERRORS */
error:
  {
    GST_DEBUG_OBJECT (jitterbuffer, "No clock-rate in caps!");
    return FALSE;
  }
wrong_rate:
  {
    GST_DEBUG_OBJECT (jitterbuffer, "Invalid clock-rate %d", priv->clock_rate);
    return FALSE;
  }
}

static gboolean
gst_jitter_buffer_sink_setcaps (GstPad * pad, GstCaps * caps)
{
  GstRtpJitterBuffer *jitterbuffer;
  GstRtpJitterBufferPrivate *priv;
  gboolean res;

  jitterbuffer = GST_RTP_JITTER_BUFFER (gst_pad_get_parent (pad));
  priv = jitterbuffer->priv;

  res = gst_jitter_buffer_sink_parse_caps (jitterbuffer, caps);

  /* set same caps on srcpad on success */
  if (res)
    gst_pad_set_caps (priv->srcpad, caps);

  gst_object_unref (jitterbuffer);

  return res;
}

static void
gst_rtp_jitter_buffer_flush_start (GstRtpJitterBuffer * jitterbuffer)
{
  GstRtpJitterBufferPrivate *priv;

  priv = jitterbuffer->priv;

  JBUF_LOCK (priv);
  /* mark ourselves as flushing */
  priv->srcresult = GST_FLOW_WRONG_STATE;
  GST_DEBUG_OBJECT (jitterbuffer, "Disabling pop on queue");
  /* this unblocks any waiting pops on the src pad task */
  JBUF_SIGNAL (priv);
  /* unlock clock, we just unschedule, the entry will be released by the 
   * locking streaming thread. */
  if (priv->clock_id)
    gst_clock_id_unschedule (priv->clock_id);
  JBUF_UNLOCK (priv);
}

static void
gst_rtp_jitter_buffer_flush_stop (GstRtpJitterBuffer * jitterbuffer)
{
  GstRtpJitterBufferPrivate *priv;

  priv = jitterbuffer->priv;

  JBUF_LOCK (priv);
  GST_DEBUG_OBJECT (jitterbuffer, "Enabling pop on queue");
  /* Mark as non flushing */
  priv->srcresult = GST_FLOW_OK;
  gst_segment_init (&priv->segment, GST_FORMAT_TIME);
  priv->last_popped_seqnum = -1;
  priv->last_out_time = -1;
  priv->next_seqnum = -1;
  priv->clock_rate = -1;
  priv->eos = FALSE;
  rtp_jitter_buffer_flush (priv->jbuf);
  rtp_jitter_buffer_reset_skew (priv->jbuf);
  JBUF_UNLOCK (priv);
}

static gboolean
gst_rtp_jitter_buffer_src_activate_push (GstPad * pad, gboolean active)
{
  gboolean result = TRUE;
  GstRtpJitterBuffer *jitterbuffer = NULL;

  jitterbuffer = GST_RTP_JITTER_BUFFER (gst_pad_get_parent (pad));

  if (active) {
    /* allow data processing */
    gst_rtp_jitter_buffer_flush_stop (jitterbuffer);

    /* start pushing out buffers */
    GST_DEBUG_OBJECT (jitterbuffer, "Starting task on srcpad");
    gst_pad_start_task (jitterbuffer->priv->srcpad,
        (GstTaskFunction) gst_rtp_jitter_buffer_loop, jitterbuffer);
  } else {
    /* make sure all data processing stops ASAP */
    gst_rtp_jitter_buffer_flush_start (jitterbuffer);

    /* NOTE this will hardlock if the state change is called from the src pad
     * task thread because we will _join() the thread. */
    GST_DEBUG_OBJECT (jitterbuffer, "Stopping task on srcpad");
    result = gst_pad_stop_task (pad);
  }

  gst_object_unref (jitterbuffer);

  return result;
}

static GstStateChangeReturn
gst_rtp_jitter_buffer_change_state (GstElement * element,
    GstStateChange transition)
{
  GstRtpJitterBuffer *jitterbuffer;
  GstRtpJitterBufferPrivate *priv;
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;

  jitterbuffer = GST_RTP_JITTER_BUFFER (element);
  priv = jitterbuffer->priv;

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      JBUF_LOCK (priv);
      /* reset negotiated values */
      priv->clock_rate = -1;
      priv->clock_base = -1;
      priv->peer_latency = 0;
      priv->last_pt = -1;
      /* block until we go to PLAYING */
      priv->blocked = TRUE;
      /* reset skew detection initialy */
      rtp_jitter_buffer_reset_skew (priv->jbuf);
      JBUF_UNLOCK (priv);
      break;
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
      JBUF_LOCK (priv);
      /* unblock to allow streaming in PLAYING */
      priv->blocked = FALSE;
      JBUF_SIGNAL (priv);
      JBUF_UNLOCK (priv);
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      /* we are a live element because we sync to the clock, which we can only
       * do in the PLAYING state */
      if (ret != GST_STATE_CHANGE_FAILURE)
        ret = GST_STATE_CHANGE_NO_PREROLL;
      break;
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
      JBUF_LOCK (priv);
      /* block to stop streaming when PAUSED */
      priv->blocked = TRUE;
      JBUF_UNLOCK (priv);
      if (ret != GST_STATE_CHANGE_FAILURE)
        ret = GST_STATE_CHANGE_NO_PREROLL;
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      break;
    default:
      break;
  }

  return ret;
}

static gboolean
gst_rtp_jitter_buffer_src_event (GstPad * pad, GstEvent * event)
{
  gboolean ret = TRUE;
  GstRtpJitterBuffer *jitterbuffer;
  GstRtpJitterBufferPrivate *priv;

  jitterbuffer = GST_RTP_JITTER_BUFFER (gst_pad_get_parent (pad));
  priv = jitterbuffer->priv;

  GST_DEBUG_OBJECT (jitterbuffer, "received %s", GST_EVENT_TYPE_NAME (event));

  switch (GST_EVENT_TYPE (event)) {
    default:
      ret = gst_pad_push_event (priv->sinkpad, event);
      break;
  }
  gst_object_unref (jitterbuffer);

  return ret;
}

static gboolean
gst_rtp_jitter_buffer_sink_event (GstPad * pad, GstEvent * event)
{
  gboolean ret = TRUE;
  GstRtpJitterBuffer *jitterbuffer;
  GstRtpJitterBufferPrivate *priv;

  jitterbuffer = GST_RTP_JITTER_BUFFER (gst_pad_get_parent (pad));
  priv = jitterbuffer->priv;

  GST_DEBUG_OBJECT (jitterbuffer, "received %s", GST_EVENT_TYPE_NAME (event));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_NEWSEGMENT:
    {
      GstFormat format;
      gdouble rate, arate;
      gint64 start, stop, time;
      gboolean update;

      gst_event_parse_new_segment_full (event, &update, &rate, &arate, &format,
          &start, &stop, &time);

      /* we need time for now */
      if (format != GST_FORMAT_TIME)
        goto newseg_wrong_format;

      GST_DEBUG_OBJECT (jitterbuffer,
          "newsegment: update %d, rate %g, arate %g, start %" GST_TIME_FORMAT
          ", stop %" GST_TIME_FORMAT ", time %" GST_TIME_FORMAT,
          update, rate, arate, GST_TIME_ARGS (start), GST_TIME_ARGS (stop),
          GST_TIME_ARGS (time));

      /* now configure the values, we need these to time the release of the
       * buffers on the srcpad. */
      gst_segment_set_newsegment_full (&priv->segment, update,
          rate, arate, format, start, stop, time);

      /* FIXME, push SEGMENT in the queue. Sorting order might be difficult. */
      ret = gst_pad_push_event (priv->srcpad, event);
      break;
    }
    case GST_EVENT_FLUSH_START:
      gst_rtp_jitter_buffer_flush_start (jitterbuffer);
      ret = gst_pad_push_event (priv->srcpad, event);
      break;
    case GST_EVENT_FLUSH_STOP:
      ret = gst_pad_push_event (priv->srcpad, event);
      ret = gst_rtp_jitter_buffer_src_activate_push (priv->srcpad, TRUE);
      break;
    case GST_EVENT_EOS:
    {
      /* push EOS in queue. We always push it at the head */
      JBUF_LOCK (priv);
      /* check for flushing, we need to discard the event and return FALSE when
       * we are flushing */
      ret = priv->srcresult == GST_FLOW_OK;
      if (ret && !priv->eos) {
        GST_DEBUG_OBJECT (jitterbuffer, "queuing EOS");
        priv->eos = TRUE;
        JBUF_SIGNAL (priv);
      } else if (priv->eos) {
        GST_DEBUG_OBJECT (jitterbuffer, "dropping EOS, we are already EOS");
      } else {
        GST_DEBUG_OBJECT (jitterbuffer, "dropping EOS, reason %s",
            gst_flow_get_name (priv->srcresult));
      }
      JBUF_UNLOCK (priv);
      gst_event_unref (event);
      break;
    }
    default:
      ret = gst_pad_push_event (priv->srcpad, event);
      break;
  }

done:
  gst_object_unref (jitterbuffer);

  return ret;

  /* ERRORS */
newseg_wrong_format:
  {
    GST_DEBUG_OBJECT (jitterbuffer, "received non TIME newsegment");
    ret = FALSE;
    goto done;
  }
}

static gboolean
gst_rtp_jitter_buffer_get_clock_rate (GstRtpJitterBuffer * jitterbuffer,
    guint8 pt)
{
  GValue ret = { 0 };
  GValue args[2] = { {0}, {0} };
  GstCaps *caps;
  gboolean res;

  g_value_init (&args[0], GST_TYPE_ELEMENT);
  g_value_set_object (&args[0], jitterbuffer);
  g_value_init (&args[1], G_TYPE_UINT);
  g_value_set_uint (&args[1], pt);

  g_value_init (&ret, GST_TYPE_CAPS);
  g_value_set_boxed (&ret, NULL);

  g_signal_emitv (args, gst_rtp_jitter_buffer_signals[SIGNAL_REQUEST_PT_MAP], 0,
      &ret);

  g_value_unset (&args[0]);
  g_value_unset (&args[1]);
  caps = (GstCaps *) g_value_dup_boxed (&ret);
  g_value_unset (&ret);
  if (!caps)
    goto no_caps;

  res = gst_jitter_buffer_sink_parse_caps (jitterbuffer, caps);

  gst_caps_unref (caps);

  return res;

  /* ERRORS */
no_caps:
  {
    GST_DEBUG_OBJECT (jitterbuffer, "could not get caps");
    return FALSE;
  }
}

static GstFlowReturn
gst_rtp_jitter_buffer_chain (GstPad * pad, GstBuffer * buffer)
{
  GstRtpJitterBuffer *jitterbuffer;
  GstRtpJitterBufferPrivate *priv;
  guint16 seqnum;
  GstFlowReturn ret = GST_FLOW_OK;
  GstClockTime timestamp;
  guint64 latency_ts;
  gboolean tail;

  jitterbuffer = GST_RTP_JITTER_BUFFER (gst_pad_get_parent (pad));

  if (!gst_rtp_buffer_validate (buffer))
    goto invalid_buffer;

  priv = jitterbuffer->priv;

  if (priv->last_pt != gst_rtp_buffer_get_payload_type (buffer)) {
    GstCaps *caps;

    priv->last_pt = gst_rtp_buffer_get_payload_type (buffer);
    /* reset clock-rate so that we get a new one */
    priv->clock_rate = -1;
    /* Try to get the clock-rate from the caps first if we can. If there are no
     * caps we must fire the signal to get the clock-rate. */
    if ((caps = GST_BUFFER_CAPS (buffer))) {
      gst_jitter_buffer_sink_parse_caps (jitterbuffer, caps);
    }
  }

  if (priv->clock_rate == -1) {
    guint8 pt;

    /* no clock rate given on the caps, try to get one with the signal */
    pt = gst_rtp_buffer_get_payload_type (buffer);

    gst_rtp_jitter_buffer_get_clock_rate (jitterbuffer, pt);
    if (priv->clock_rate == -1)
      goto not_negotiated;
  }

  /* take the timestamp of the buffer. This is the time when the packet was
   * received and is used to calculate jitter and clock skew. We will adjust
   * this timestamp with the smoothed value after processing it in the
   * jitterbuffer. */
  timestamp = GST_BUFFER_TIMESTAMP (buffer);
  /* bring to running time */
  timestamp = gst_segment_to_running_time (&priv->segment, GST_FORMAT_TIME,
      timestamp);

  seqnum = gst_rtp_buffer_get_seq (buffer);
  GST_DEBUG_OBJECT (jitterbuffer,
      "Received packet #%d at time %" GST_TIME_FORMAT, seqnum,
      GST_TIME_ARGS (timestamp));

  JBUF_LOCK_CHECK (priv, out_flushing);
  /* don't accept more data on EOS */
  if (priv->eos)
    goto have_eos;

  /* let's check if this buffer is too late, we can only accept packets with
   * bigger seqnum than the one we last pushed. */
  if (priv->last_popped_seqnum != -1) {
    gint gap;

    gap = gst_rtp_buffer_compare_seqnum (priv->last_popped_seqnum, seqnum);

    if (gap <= 0) {
      /* priv->last_popped_seqnum >= seqnum, this packet is too late or the
       * sender might have been restarted with different seqnum. */
      if (gap < -100) {
        GST_DEBUG_OBJECT (jitterbuffer, "reset: buffer too old %d", gap);
        priv->last_popped_seqnum = -1;
        priv->next_seqnum = -1;
      } else {
        goto too_late;
      }
    } else {
      /* priv->last_popped_seqnum < seqnum, this is a new packet */
      if (gap > 3000) {
        GST_DEBUG_OBJECT (jitterbuffer, "reset: too many dropped packets %d",
            gap);
        priv->last_popped_seqnum = -1;
        priv->next_seqnum = -1;
      }
    }
  }

  /* let's drop oldest packet if the queue is already full and drop-on-latency
   * is set. We can only do this when there actually is a latency. When no
   * latency is set, we just pump it in the queue and let the other end push it
   * out as fast as possible. */
  if (priv->latency_ms && priv->drop_on_latency) {

    latency_ts =
        gst_util_uint64_scale_int (priv->latency_ms, priv->clock_rate, 1000);

    if (rtp_jitter_buffer_get_ts_diff (priv->jbuf) >= latency_ts) {
      GstBuffer *old_buf;

      GST_DEBUG_OBJECT (jitterbuffer, "Queue full, dropping old packet #%d",
          seqnum);

      old_buf = rtp_jitter_buffer_pop (priv->jbuf);
      gst_buffer_unref (old_buf);
    }
  }

  /* now insert the packet into the queue in sorted order. This function returns
   * FALSE if a packet with the same seqnum was already in the queue, meaning we
   * have a duplicate. */
  if (!rtp_jitter_buffer_insert (priv->jbuf, buffer, timestamp,
          priv->clock_rate, &tail))
    goto duplicate;

  /* signal addition of new buffer when the _loop is waiting. */
  if (priv->waiting)
    JBUF_SIGNAL (priv);

  /* let's unschedule and unblock any waiting buffers. We only want to do this
   * when the tail buffer changed */
  if (priv->clock_id && tail) {
    GST_DEBUG_OBJECT (jitterbuffer,
        "Unscheduling waiting buffer, new tail buffer");
    gst_clock_id_unschedule (priv->clock_id);
  }

  GST_DEBUG_OBJECT (jitterbuffer, "Pushed packet #%d, now %d packets",
      seqnum, rtp_jitter_buffer_num_packets (priv->jbuf));

finished:
  JBUF_UNLOCK (priv);

  gst_object_unref (jitterbuffer);

  return ret;

  /* ERRORS */
invalid_buffer:
  {
    /* this is not fatal but should be filtered earlier */
    GST_ELEMENT_WARNING (jitterbuffer, STREAM, DECODE, (NULL),
        ("Received invalid RTP payload, dropping"));
    gst_buffer_unref (buffer);
    gst_object_unref (jitterbuffer);
    return GST_FLOW_OK;
  }
not_negotiated:
  {
    GST_WARNING_OBJECT (jitterbuffer, "No clock-rate in caps!");
    gst_buffer_unref (buffer);
    gst_object_unref (jitterbuffer);
    return GST_FLOW_OK;
  }
out_flushing:
  {
    ret = priv->srcresult;
    GST_DEBUG_OBJECT (jitterbuffer, "flushing %s", gst_flow_get_name (ret));
    gst_buffer_unref (buffer);
    goto finished;
  }
have_eos:
  {
    ret = GST_FLOW_UNEXPECTED;
    GST_WARNING_OBJECT (jitterbuffer, "we are EOS, refusing buffer");
    gst_buffer_unref (buffer);
    goto finished;
  }
too_late:
  {
    GST_WARNING_OBJECT (jitterbuffer, "Packet #%d too late as #%d was already"
        " popped, dropping", seqnum, priv->last_popped_seqnum);
    priv->num_late++;
    gst_buffer_unref (buffer);
    goto finished;
  }
duplicate:
  {
    GST_WARNING_OBJECT (jitterbuffer, "Duplicate packet #%d detected, dropping",
        seqnum);
    priv->num_duplicates++;
    gst_buffer_unref (buffer);
    goto finished;
  }
}

static GstClockTime
apply_offset (GstRtpJitterBuffer * jitterbuffer, GstClockTime timestamp)
{
  GstRtpJitterBufferPrivate *priv;

  priv = jitterbuffer->priv;

  if (timestamp == -1)
    return -1;

  /* apply the timestamp offset */
  timestamp += priv->ts_offset;

  return timestamp;
}

/**
 * This funcion will push out buffers on the source pad.
 *
 * For each pushed buffer, the seqnum is recorded, if the next buffer B has a
 * different seqnum (missing packets before B), this function will wait for the
 * missing packet to arrive up to the timestamp of buffer B.
 */
static void
gst_rtp_jitter_buffer_loop (GstRtpJitterBuffer * jitterbuffer)
{
  GstRtpJitterBufferPrivate *priv;
  GstBuffer *outbuf;
  GstFlowReturn result;
  guint16 seqnum;
  guint32 next_seqnum;
  GstClockTime timestamp, out_time;
  gboolean discont = FALSE;
  gint gap;

  priv = jitterbuffer->priv;

  JBUF_LOCK_CHECK (priv, flushing);
again:
  GST_DEBUG_OBJECT (jitterbuffer, "Peeking item");
  while (TRUE) {
    /* always wait if we are blocked */
    if (!priv->blocked) {
      /* if we have a packet, we can exit the loop and grab it */
      if (rtp_jitter_buffer_num_packets (priv->jbuf) > 0)
        break;
      /* no packets but we are EOS, do eos logic */
      if (priv->eos)
        goto do_eos;
    }
    /* underrun, wait for packets or flushing now */
    priv->waiting = TRUE;
    JBUF_WAIT_CHECK (priv, flushing);
    priv->waiting = FALSE;
  }

  /* peek a buffer, we're just looking at the timestamp and the sequence number.
   * If all is fine, we'll pop and push it. If the sequence number is wrong we
   * wait on the timestamp. In the chain function we will unlock the wait when a
   * new buffer is available. The peeked buffer is valid for as long as we hold
   * the jitterbuffer lock. */
  outbuf = rtp_jitter_buffer_peek (priv->jbuf);

  /* get the seqnum and the next expected seqnum */
  seqnum = gst_rtp_buffer_get_seq (outbuf);
  next_seqnum = priv->next_seqnum;

  /* get the timestamp, this is already corrected for clock skew by the
   * jitterbuffer */
  timestamp = GST_BUFFER_TIMESTAMP (outbuf);

  GST_DEBUG_OBJECT (jitterbuffer,
      "Peeked buffer #%d, expect #%d, timestamp %" GST_TIME_FORMAT
      ", now %d left", seqnum, next_seqnum, GST_TIME_ARGS (timestamp),
      rtp_jitter_buffer_num_packets (priv->jbuf));

  /* apply our timestamp offset to the incomming buffer, this will be our output
   * timestamp. */
  out_time = apply_offset (jitterbuffer, timestamp);

  /* get the gap between this and the previous packet. If we don't know the
   * previous packet seqnum assume no gap. */
  if (next_seqnum != -1) {
    gap = gst_rtp_buffer_compare_seqnum (next_seqnum, seqnum);

    /* if we have a packet that we already pushed or considered dropped, pop it
     * off and get the next packet */
    if (gap < 0) {
      GST_DEBUG_OBJECT (jitterbuffer, "Old packet #%d, next #%d dropping",
          seqnum, next_seqnum);
      outbuf = rtp_jitter_buffer_pop (priv->jbuf);
      gst_buffer_unref (outbuf);
      goto again;
    }
  } else {
    GST_DEBUG_OBJECT (jitterbuffer, "no next seqnum known, first packet");
    gap = -1;
  }

  /* If we don't know what the next seqnum should be (== -1) we have to wait
   * because it might be possible that we are not receiving this buffer in-order,
   * a buffer with a lower seqnum could arrive later and we want to push that
   * earlier buffer before this buffer then.
   * If we know the expected seqnum, we can compare it to the current seqnum to
   * determine if we have missing a packet. If we have a missing packet (which
   * must be before this packet) we can wait for it until the deadline for this
   * packet expires. */
  if (gap != 0 && out_time != -1) {
    GstClockID id;
    GstClockTime sync_time;
    GstClockReturn ret;
    GstClock *clock;
    GstClockTime duration = GST_CLOCK_TIME_NONE;

    if (gap > 0) {
      /* we have a gap */
      GST_WARNING_OBJECT (jitterbuffer,
          "Sequence number GAP detected: expected %d instead of %d (%d missing)",
          next_seqnum, seqnum, gap);

      if (priv->last_out_time != -1) {
        GST_DEBUG_OBJECT (jitterbuffer,
            "out_time %" GST_TIME_FORMAT ", last %" GST_TIME_FORMAT,
            GST_TIME_ARGS (out_time), GST_TIME_ARGS (priv->last_out_time));
        /* interpolate between the current time and the last time based on
         * number of packets we are missing, this is the estimated duration
         * for the missing packet based on equidistant packet spacing. Also make
         * sure we never go negative. */
        if (out_time > priv->last_out_time)
          duration = (out_time - priv->last_out_time) / (gap + 1);
        else
          goto lost;

        GST_DEBUG_OBJECT (jitterbuffer, "duration %" GST_TIME_FORMAT,
            GST_TIME_ARGS (duration));
        /* add this duration to the timestamp of the last packet we pushed */
        out_time = (priv->last_out_time + duration);
      }
    } else {
      /* we don't know what the next_seqnum should be, wait for the last
       * possible moment to push this buffer, maybe we get an earlier seqnum
       * while we wait */
      GST_DEBUG_OBJECT (jitterbuffer, "First buffer %d, do sync", seqnum);
    }

    GST_OBJECT_LOCK (jitterbuffer);
    clock = GST_ELEMENT_CLOCK (jitterbuffer);
    if (!clock) {
      GST_OBJECT_UNLOCK (jitterbuffer);
      /* let's just push if there is no clock */
      goto push_buffer;
    }

    GST_DEBUG_OBJECT (jitterbuffer, "sync to timestamp %" GST_TIME_FORMAT,
        GST_TIME_ARGS (out_time));

    /* prepare for sync against clock */
    sync_time = out_time + GST_ELEMENT_CAST (jitterbuffer)->base_time;
    /* add latency, this includes our own latency and the peer latency. */
    sync_time += (priv->latency_ms * GST_MSECOND);
    sync_time += priv->peer_latency;

    /* create an entry for the clock */
    id = priv->clock_id = gst_clock_new_single_shot_id (clock, sync_time);
    GST_OBJECT_UNLOCK (jitterbuffer);

    /* release the lock so that the other end can push stuff or unlock */
    JBUF_UNLOCK (priv);

    ret = gst_clock_id_wait (id, NULL);

    JBUF_LOCK (priv);
    /* and free the entry */
    gst_clock_id_unref (id);
    priv->clock_id = NULL;

    /* at this point, the clock could have been unlocked by a timeout, a new
     * tail element was added to the queue or because we are shutting down. Check
     * for shutdown first. */
    if (priv->srcresult != GST_FLOW_OK)
      goto flushing;

    /* if we got unscheduled and we are not flushing, it's because a new tail
     * element became available in the queue. Grab it and try to push or sync. */
    if (ret == GST_CLOCK_UNSCHEDULED) {
      GST_DEBUG_OBJECT (jitterbuffer,
          "Wait got unscheduled, will retry to push with new buffer");
      goto again;
    }

  lost:
    /* we now timed out, this means we lost a packet or finished synchronizing
     * on the first buffer. */
    if (gap > 0) {
      GstEvent *event;

      /* we had a gap and thus we lost a packet. Create an event for this.  */
      GST_DEBUG_OBJECT (jitterbuffer, "Packet #%d lost", next_seqnum);
      priv->num_late++;
      discont = TRUE;

      if (priv->do_lost) {
        /* create paket lost event */
        event = gst_event_new_custom (GST_EVENT_CUSTOM_DOWNSTREAM,
            gst_structure_new ("GstRTPPacketLost",
                "seqnum", G_TYPE_UINT, (guint) next_seqnum,
                "timestamp", G_TYPE_UINT64, out_time,
                "duration", G_TYPE_UINT64, duration, NULL));
        gst_pad_push_event (priv->srcpad, event);
      }

      /* update our expected next packet */
      priv->last_popped_seqnum = next_seqnum;
      priv->last_out_time = out_time;
      priv->next_seqnum = (next_seqnum + 1) & 0xffff;
      /* look for next packet */
      goto again;
    }

    /* there was no known gap,just the first packet, exit the loop and push */
    GST_DEBUG_OBJECT (jitterbuffer, "First packet #%d synced", seqnum);

    /* get new timestamp, latency might have changed */
    out_time = apply_offset (jitterbuffer, timestamp);
  }
push_buffer:

  /* when we get here we are ready to pop and push the buffer */
  outbuf = rtp_jitter_buffer_pop (priv->jbuf);

  if (discont || priv->discont) {
    /* set DISCONT flag when we missed a packet. */
    outbuf = gst_buffer_make_metadata_writable (outbuf);
    GST_BUFFER_FLAG_SET (outbuf, GST_BUFFER_FLAG_DISCONT);
    priv->discont = FALSE;
  }

  /* apply timestamp with offset to buffer now */
  GST_BUFFER_TIMESTAMP (outbuf) = out_time;

  /* now we are ready to push the buffer. Save the seqnum and release the lock
   * so the other end can push stuff in the queue again. */
  priv->last_popped_seqnum = seqnum;
  priv->last_out_time = out_time;
  priv->next_seqnum = (seqnum + 1) & 0xffff;
  JBUF_UNLOCK (priv);

  /* push buffer */
  GST_DEBUG_OBJECT (jitterbuffer,
      "Pushing buffer %d, timestamp %" GST_TIME_FORMAT, seqnum,
      GST_TIME_ARGS (out_time));
  result = gst_pad_push (priv->srcpad, outbuf);
  if (result != GST_FLOW_OK)
    goto pause;

  return;

  /* ERRORS */
do_eos:
  {
    /* store result, we are flushing now */
    GST_DEBUG_OBJECT (jitterbuffer, "We are EOS, pushing EOS downstream");
    priv->srcresult = GST_FLOW_UNEXPECTED;
    gst_pad_pause_task (priv->srcpad);
    gst_pad_push_event (priv->srcpad, gst_event_new_eos ());
    JBUF_UNLOCK (priv);
    return;
  }
flushing:
  {
    GST_DEBUG_OBJECT (jitterbuffer, "we are flushing");
    gst_pad_pause_task (priv->srcpad);
    JBUF_UNLOCK (priv);
    return;
  }
pause:
  {
    const gchar *reason = gst_flow_get_name (result);

    GST_DEBUG_OBJECT (jitterbuffer, "pausing task, reason %s", reason);

    JBUF_LOCK (priv);
    /* store result */
    priv->srcresult = result;
    /* we don't post errors or anything because upstream will do that for us
     * when we pass the return value upstream. */
    gst_pad_pause_task (priv->srcpad);
    JBUF_UNLOCK (priv);
    return;
  }
}

static gboolean
gst_rtp_jitter_buffer_query (GstPad * pad, GstQuery * query)
{
  GstRtpJitterBuffer *jitterbuffer;
  GstRtpJitterBufferPrivate *priv;
  gboolean res = FALSE;

  jitterbuffer = GST_RTP_JITTER_BUFFER (gst_pad_get_parent (pad));
  priv = jitterbuffer->priv;

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_LATENCY:
    {
      /* We need to send the query upstream and add the returned latency to our
       * own */
      GstClockTime min_latency, max_latency;
      gboolean us_live;
      GstClockTime our_latency;

      if ((res = gst_pad_peer_query (priv->sinkpad, query))) {
        gst_query_parse_latency (query, &us_live, &min_latency, &max_latency);

        GST_DEBUG_OBJECT (jitterbuffer, "Peer latency: min %"
            GST_TIME_FORMAT " max %" GST_TIME_FORMAT,
            GST_TIME_ARGS (min_latency), GST_TIME_ARGS (max_latency));

        /* store this so that we can safely sync on the peer buffers. */
        JBUF_LOCK (priv);
        priv->peer_latency = min_latency;
        our_latency = ((guint64) priv->latency_ms) * GST_MSECOND;
        JBUF_UNLOCK (priv);

        GST_DEBUG_OBJECT (jitterbuffer, "Our latency: %" GST_TIME_FORMAT,
            GST_TIME_ARGS (our_latency));

        /* we add some latency but can buffer an infinite amount of time */
        min_latency += our_latency;
        max_latency = -1;

        GST_DEBUG_OBJECT (jitterbuffer, "Calculated total latency : min %"
            GST_TIME_FORMAT " max %" GST_TIME_FORMAT,
            GST_TIME_ARGS (min_latency), GST_TIME_ARGS (max_latency));

        gst_query_set_latency (query, TRUE, min_latency, max_latency);
      }
      break;
    }
    default:
      res = gst_pad_query_default (pad, query);
      break;
  }

  gst_object_unref (jitterbuffer);

  return res;
}

static void
gst_rtp_jitter_buffer_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec)
{
  GstRtpJitterBuffer *jitterbuffer;
  GstRtpJitterBufferPrivate *priv;

  jitterbuffer = GST_RTP_JITTER_BUFFER (object);
  priv = jitterbuffer->priv;

  switch (prop_id) {
    case PROP_LATENCY:
    {
      guint new_latency, old_latency;

      new_latency = g_value_get_uint (value);

      JBUF_LOCK (priv);
      old_latency = priv->latency_ms;
      priv->latency_ms = new_latency;
      JBUF_UNLOCK (priv);

      /* post message if latency changed, this will inform the parent pipeline
       * that a latency reconfiguration is possible/needed. */
      if (new_latency != old_latency) {
        GST_DEBUG_OBJECT (jitterbuffer, "latency changed to: %" GST_TIME_FORMAT,
            GST_TIME_ARGS (new_latency * GST_MSECOND));

        gst_element_post_message (GST_ELEMENT_CAST (jitterbuffer),
            gst_message_new_latency (GST_OBJECT_CAST (jitterbuffer)));
      }
      break;
    }
    case PROP_DROP_ON_LATENCY:
      JBUF_LOCK (priv);
      priv->drop_on_latency = g_value_get_boolean (value);
      JBUF_UNLOCK (priv);
      break;
    case PROP_TS_OFFSET:
      JBUF_LOCK (priv);
      priv->ts_offset = g_value_get_int64 (value);
      /* FIXME, we don't really have a method for signaling a timestamp
       * DISCONT without also making this a data discont. */
      /* priv->discont = TRUE; */
      JBUF_UNLOCK (priv);
      break;
    case PROP_DO_LOST:
      JBUF_LOCK (priv);
      priv->do_lost = g_value_get_boolean (value);
      JBUF_UNLOCK (priv);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_rtp_jitter_buffer_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec)
{
  GstRtpJitterBuffer *jitterbuffer;
  GstRtpJitterBufferPrivate *priv;

  jitterbuffer = GST_RTP_JITTER_BUFFER (object);
  priv = jitterbuffer->priv;

  switch (prop_id) {
    case PROP_LATENCY:
      JBUF_LOCK (priv);
      g_value_set_uint (value, priv->latency_ms);
      JBUF_UNLOCK (priv);
      break;
    case PROP_DROP_ON_LATENCY:
      JBUF_LOCK (priv);
      g_value_set_boolean (value, priv->drop_on_latency);
      JBUF_UNLOCK (priv);
      break;
    case PROP_TS_OFFSET:
      JBUF_LOCK (priv);
      g_value_set_int64 (value, priv->ts_offset);
      JBUF_UNLOCK (priv);
      break;
    case PROP_DO_LOST:
      JBUF_LOCK (priv);
      g_value_set_boolean (value, priv->do_lost);
      JBUF_UNLOCK (priv);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}
