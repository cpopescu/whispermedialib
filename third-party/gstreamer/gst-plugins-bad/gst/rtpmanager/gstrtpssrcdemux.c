/* GStreamer
 * Copyright (C) <2007> Wim Taymans <wim.taymans@gmail.com>
 *
 * RTP SSRC demuxer
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
 * SECTION:element-gstrtpssrcdemux
 *
 * gstrtpssrcdemux acts as a demuxer for RTP packets based on the SSRC of the
 * packets. Its main purpose is to allow an application to easily receive and
 * decode an RTP stream with multiple SSRCs.
 * 
 * For each SSRC that is detected, a new pad will be created and the
 * #GstRtpSsrcDemux::new-ssrc-pad signal will be emitted. 
 * 
 * <refsect2>
 * <title>Example pipelines</title>
 * |[
 * gst-launch udpsrc caps="application/x-rtp" ! gstrtpssrcdemux ! fakesink
 * ]| Takes an RTP stream and send the RTP packets with the first detected SSRC
 * to fakesink, discarding the other SSRCs.
 * </refsect2>
 *
 * Last reviewed on 2007-05-28 (0.10.5)
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>
#include <gst/rtp/gstrtpbuffer.h>
#include <gst/rtp/gstrtcpbuffer.h>

#include "gstrtpbin-marshal.h"
#include "gstrtpssrcdemux.h"

GST_DEBUG_CATEGORY_STATIC (gst_rtp_ssrc_demux_debug);
#define GST_CAT_DEFAULT gst_rtp_ssrc_demux_debug

/* generic templates */
static GstStaticPadTemplate rtp_ssrc_demux_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-rtp")
    );

static GstStaticPadTemplate rtp_ssrc_demux_rtcp_sink_template =
GST_STATIC_PAD_TEMPLATE ("rtcp_sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-rtcp")
    );

static GstStaticPadTemplate rtp_ssrc_demux_src_template =
GST_STATIC_PAD_TEMPLATE ("src_%d",
    GST_PAD_SRC,
    GST_PAD_SOMETIMES,
    GST_STATIC_CAPS ("application/x-rtp")
    );

static GstStaticPadTemplate rtp_ssrc_demux_rtcp_src_template =
GST_STATIC_PAD_TEMPLATE ("rtcp_src_%d",
    GST_PAD_SRC,
    GST_PAD_SOMETIMES,
    GST_STATIC_CAPS ("application/x-rtcp")
    );

static GstElementDetails gst_rtp_ssrc_demux_details = {
  "RTP SSRC Demux",
  "Demux/Network/RTP",
  "Splits RTP streams based on the SSRC",
  "Wim Taymans <wim.taymans@gmail.com>"
};

#define GST_PAD_LOCK(obj)   (g_mutex_lock ((obj)->padlock))
#define GST_PAD_UNLOCK(obj) (g_mutex_unlock ((obj)->padlock))

/* signals */
enum
{
  SIGNAL_NEW_SSRC_PAD,
  LAST_SIGNAL
};

GST_BOILERPLATE (GstRtpSsrcDemux, gst_rtp_ssrc_demux, GstElement,
    GST_TYPE_ELEMENT);


/* GObject vmethods */
static void gst_rtp_ssrc_demux_dispose (GObject * object);
static void gst_rtp_ssrc_demux_finalize (GObject * object);

/* GstElement vmethods */
static GstStateChangeReturn gst_rtp_ssrc_demux_change_state (GstElement *
    element, GstStateChange transition);

/* sinkpad stuff */
static GstFlowReturn gst_rtp_ssrc_demux_chain (GstPad * pad, GstBuffer * buf);
static gboolean gst_rtp_ssrc_demux_sink_event (GstPad * pad, GstEvent * event);

static GstFlowReturn gst_rtp_ssrc_demux_rtcp_chain (GstPad * pad,
    GstBuffer * buf);
static gboolean gst_rtp_ssrc_demux_rtcp_sink_event (GstPad * pad,
    GstEvent * event);

/* srcpad stuff */
static gboolean gst_rtp_ssrc_demux_src_event (GstPad * pad, GstEvent * event);
static GList *gst_rtp_ssrc_demux_internal_links (GstPad * pad);
static gboolean gst_rtp_ssrc_demux_src_query (GstPad * pad, GstQuery * query);

static guint gst_rtp_ssrc_demux_signals[LAST_SIGNAL] = { 0 };

/*
 * Item for storing GstPad <-> SSRC pairs.
 */
struct _GstRtpSsrcDemuxPad
{
  guint32 ssrc;
  GstPad *rtp_pad;
  GstCaps *caps;
  GstPad *rtcp_pad;
  GstClockTime first_ts;
};

/* find a src pad for a given SSRC, returns NULL if the SSRC was not found
 */
static GstRtpSsrcDemuxPad *
find_demux_pad_for_ssrc (GstRtpSsrcDemux * demux, guint32 ssrc)
{
  GSList *walk;

  for (walk = demux->srcpads; walk; walk = g_slist_next (walk)) {
    GstRtpSsrcDemuxPad *pad = (GstRtpSsrcDemuxPad *) walk->data;

    if (pad->ssrc == ssrc)
      return pad;
  }
  return NULL;
}

/* with PAD_LOCK */
static GstRtpSsrcDemuxPad *
create_demux_pad_for_ssrc (GstRtpSsrcDemux * demux, guint32 ssrc,
    GstClockTime timestamp)
{
  GstPad *rtp_pad, *rtcp_pad;
  GstElementClass *klass;
  GstPadTemplate *templ;
  gchar *padname;
  GstRtpSsrcDemuxPad *demuxpad;

  GST_DEBUG_OBJECT (demux, "creating pad for SSRC %08x", ssrc);

  klass = GST_ELEMENT_GET_CLASS (demux);
  templ = gst_element_class_get_pad_template (klass, "src_%d");
  padname = g_strdup_printf ("src_%d", ssrc);
  rtp_pad = gst_pad_new_from_template (templ, padname);
  g_free (padname);

  templ = gst_element_class_get_pad_template (klass, "rtcp_src_%d");
  padname = g_strdup_printf ("rtcp_src_%d", ssrc);
  rtcp_pad = gst_pad_new_from_template (templ, padname);
  g_free (padname);

  /* we use the first timestamp received to calculate the difference between
   * timestamps on all streams */
  GST_DEBUG_OBJECT (demux, "SSRC %08x, first timestamp %" GST_TIME_FORMAT,
      ssrc, GST_TIME_ARGS (timestamp));

  /* wrap in structure and add to list */
  demuxpad = g_new0 (GstRtpSsrcDemuxPad, 1);
  demuxpad->ssrc = ssrc;
  demuxpad->rtp_pad = rtp_pad;
  demuxpad->rtcp_pad = rtcp_pad;
  demuxpad->first_ts = timestamp;

  GST_DEBUG_OBJECT (demux, "first timestamp %" GST_TIME_FORMAT,
      GST_TIME_ARGS (timestamp));

  gst_pad_set_element_private (rtp_pad, demuxpad);
  gst_pad_set_element_private (rtcp_pad, demuxpad);

  demux->srcpads = g_slist_prepend (demux->srcpads, demuxpad);

  /* copy caps from input */
  gst_pad_set_caps (rtp_pad, GST_PAD_CAPS (demux->rtp_sink));
  gst_pad_use_fixed_caps (rtp_pad);
  gst_pad_set_caps (rtcp_pad, GST_PAD_CAPS (demux->rtcp_sink));
  gst_pad_use_fixed_caps (rtcp_pad);

  gst_pad_set_event_function (rtp_pad, gst_rtp_ssrc_demux_src_event);
  gst_pad_set_query_function (rtp_pad, gst_rtp_ssrc_demux_src_query);
  gst_pad_set_internal_link_function (rtp_pad,
      gst_rtp_ssrc_demux_internal_links);
  gst_pad_set_active (rtp_pad, TRUE);

  gst_pad_set_internal_link_function (rtcp_pad,
      gst_rtp_ssrc_demux_internal_links);
  gst_pad_set_active (rtcp_pad, TRUE);

  gst_element_add_pad (GST_ELEMENT_CAST (demux), rtp_pad);
  gst_element_add_pad (GST_ELEMENT_CAST (demux), rtcp_pad);

  g_signal_emit (G_OBJECT (demux),
      gst_rtp_ssrc_demux_signals[SIGNAL_NEW_SSRC_PAD], 0, ssrc, rtp_pad);

  return demuxpad;
}

static void
gst_rtp_ssrc_demux_base_init (gpointer g_class)
{
  GstElementClass *gstelement_klass = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_pad_template (gstelement_klass,
      gst_static_pad_template_get (&rtp_ssrc_demux_sink_template));
  gst_element_class_add_pad_template (gstelement_klass,
      gst_static_pad_template_get (&rtp_ssrc_demux_rtcp_sink_template));
  gst_element_class_add_pad_template (gstelement_klass,
      gst_static_pad_template_get (&rtp_ssrc_demux_src_template));
  gst_element_class_add_pad_template (gstelement_klass,
      gst_static_pad_template_get (&rtp_ssrc_demux_rtcp_src_template));

  gst_element_class_set_details (gstelement_klass, &gst_rtp_ssrc_demux_details);
}

static void
gst_rtp_ssrc_demux_class_init (GstRtpSsrcDemuxClass * klass)
{
  GObjectClass *gobject_klass;
  GstElementClass *gstelement_klass;

  gobject_klass = (GObjectClass *) klass;
  gstelement_klass = (GstElementClass *) klass;

  gobject_klass->dispose = GST_DEBUG_FUNCPTR (gst_rtp_ssrc_demux_dispose);
  gobject_klass->finalize = GST_DEBUG_FUNCPTR (gst_rtp_ssrc_demux_finalize);

  /**
   * GstRtpSsrcDemux::new-ssrc-pad:
   * @demux: the object which received the signal
   * @ssrc: the SSRC of the pad
   * @pad: the new pad.
   *
   * Emited when a new SSRC pad has been created.
   */
  gst_rtp_ssrc_demux_signals[SIGNAL_NEW_SSRC_PAD] =
      g_signal_new ("new-ssrc-pad",
      G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
      G_STRUCT_OFFSET (GstRtpSsrcDemuxClass, new_ssrc_pad),
      NULL, NULL, gst_rtp_bin_marshal_VOID__UINT_OBJECT,
      G_TYPE_NONE, 2, G_TYPE_UINT, GST_TYPE_PAD);

  gstelement_klass->change_state =
      GST_DEBUG_FUNCPTR (gst_rtp_ssrc_demux_change_state);

  GST_DEBUG_CATEGORY_INIT (gst_rtp_ssrc_demux_debug,
      "rtpssrcdemux", 0, "RTP SSRC demuxer");
}

static void
gst_rtp_ssrc_demux_init (GstRtpSsrcDemux * demux,
    GstRtpSsrcDemuxClass * g_class)
{
  GstElementClass *klass = GST_ELEMENT_GET_CLASS (demux);

  demux->rtp_sink =
      gst_pad_new_from_template (gst_element_class_get_pad_template (klass,
          "sink"), "sink");
  gst_pad_set_chain_function (demux->rtp_sink, gst_rtp_ssrc_demux_chain);
  gst_pad_set_event_function (demux->rtp_sink, gst_rtp_ssrc_demux_sink_event);
  gst_element_add_pad (GST_ELEMENT_CAST (demux), demux->rtp_sink);

  demux->rtcp_sink =
      gst_pad_new_from_template (gst_element_class_get_pad_template (klass,
          "rtcp_sink"), "rtcp_sink");
  gst_pad_set_chain_function (demux->rtcp_sink, gst_rtp_ssrc_demux_rtcp_chain);
  gst_pad_set_event_function (demux->rtcp_sink,
      gst_rtp_ssrc_demux_rtcp_sink_event);
  gst_element_add_pad (GST_ELEMENT_CAST (demux), demux->rtcp_sink);

  demux->padlock = g_mutex_new ();

  gst_segment_init (&demux->segment, GST_FORMAT_UNDEFINED);
}

static void
gst_rtp_ssrc_demux_reset (GstRtpSsrcDemux * demux)
{
  GSList *walk;

  for (walk = demux->srcpads; walk; walk = g_slist_next (walk)) {
    GstRtpSsrcDemuxPad *dpad = (GstRtpSsrcDemuxPad *) walk->data;

    gst_pad_set_active (dpad->rtp_pad, FALSE);
    gst_pad_set_active (dpad->rtcp_pad, FALSE);

    gst_element_remove_pad (GST_ELEMENT_CAST (demux), dpad->rtp_pad);
    gst_element_remove_pad (GST_ELEMENT_CAST (demux), dpad->rtcp_pad);
    g_free (dpad);
  }
  g_slist_free (demux->srcpads);
  demux->srcpads = NULL;
}

static void
gst_rtp_ssrc_demux_dispose (GObject * object)
{
  GstRtpSsrcDemux *demux;

  demux = GST_RTP_SSRC_DEMUX (object);

  gst_rtp_ssrc_demux_reset (demux);

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
gst_rtp_ssrc_demux_finalize (GObject * object)
{
  GstRtpSsrcDemux *demux;

  demux = GST_RTP_SSRC_DEMUX (object);
  g_mutex_free (demux->padlock);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static gboolean
gst_rtp_ssrc_demux_sink_event (GstPad * pad, GstEvent * event)
{
  GstRtpSsrcDemux *demux;
  gboolean res = FALSE;

  demux = GST_RTP_SSRC_DEMUX (gst_pad_get_parent (pad));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_FLUSH_STOP:
      gst_segment_init (&demux->segment, GST_FORMAT_UNDEFINED);
    case GST_EVENT_NEWSEGMENT:
    default:
    {
      GSList *walk;

      res = TRUE;
      GST_PAD_LOCK (demux);
      for (walk = demux->srcpads; walk; walk = g_slist_next (walk)) {
        GstRtpSsrcDemuxPad *pad = (GstRtpSsrcDemuxPad *) walk->data;

        gst_event_ref (event);
        res &= gst_pad_push_event (pad->rtp_pad, event);
      }
      GST_PAD_UNLOCK (demux);
      gst_event_unref (event);
      break;
    }
  }

  gst_object_unref (demux);
  return res;
}

static gboolean
gst_rtp_ssrc_demux_rtcp_sink_event (GstPad * pad, GstEvent * event)
{
  GstRtpSsrcDemux *demux;
  gboolean res = FALSE;

  demux = GST_RTP_SSRC_DEMUX (gst_pad_get_parent (pad));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_NEWSEGMENT:
    default:
    {
      GSList *walk;

      res = TRUE;
      GST_PAD_LOCK (demux);
      for (walk = demux->srcpads; walk; walk = g_slist_next (walk)) {
        GstRtpSsrcDemuxPad *pad = (GstRtpSsrcDemuxPad *) walk->data;

        gst_event_ref (event);
        res &= gst_pad_push_event (pad->rtcp_pad, event);
      }
      GST_PAD_UNLOCK (demux);
      gst_event_unref (event);
      break;
    }
  }
  gst_object_unref (demux);
  return res;
}

static GstFlowReturn
gst_rtp_ssrc_demux_chain (GstPad * pad, GstBuffer * buf)
{
  GstFlowReturn ret;
  GstRtpSsrcDemux *demux;
  guint32 ssrc;
  GstRtpSsrcDemuxPad *dpad;

  demux = GST_RTP_SSRC_DEMUX (GST_OBJECT_PARENT (pad));

  if (!gst_rtp_buffer_validate (buf))
    goto invalid_payload;

  ssrc = gst_rtp_buffer_get_ssrc (buf);

  GST_DEBUG_OBJECT (demux, "received buffer of SSRC %08x", ssrc);

  GST_PAD_LOCK (demux);
  dpad = find_demux_pad_for_ssrc (demux, ssrc);
  if (dpad == NULL) {
    if (!(dpad =
            create_demux_pad_for_ssrc (demux, ssrc,
                GST_BUFFER_TIMESTAMP (buf))))
      goto create_failed;
  }
  GST_PAD_UNLOCK (demux);

  /* push to srcpad */
  ret = gst_pad_push (dpad->rtp_pad, buf);

  return ret;

  /* ERRORS */
invalid_payload:
  {
    /* this is fatal and should be filtered earlier */
    GST_ELEMENT_ERROR (demux, STREAM, DECODE, (NULL),
        ("Dropping invalid RTP payload"));
    gst_buffer_unref (buf);
    return GST_FLOW_ERROR;
  }
create_failed:
  {
    GST_ELEMENT_ERROR (demux, STREAM, DECODE, (NULL),
        ("Could not create new pad"));
    GST_PAD_UNLOCK (demux);
    gst_buffer_unref (buf);
    return GST_FLOW_ERROR;
  }
}

static GstFlowReturn
gst_rtp_ssrc_demux_rtcp_chain (GstPad * pad, GstBuffer * buf)
{
  GstFlowReturn ret;
  GstRtpSsrcDemux *demux;
  guint32 ssrc;
  GstRtpSsrcDemuxPad *dpad;
  GstRTCPPacket packet;

  demux = GST_RTP_SSRC_DEMUX (GST_OBJECT_PARENT (pad));

  if (!gst_rtcp_buffer_validate (buf))
    goto invalid_rtcp;

  if (!gst_rtcp_buffer_get_first_packet (buf, &packet))
    goto invalid_rtcp;

  /* first packet must be SR or RR or else the validate would have failed */
  switch (gst_rtcp_packet_get_type (&packet)) {
    case GST_RTCP_TYPE_SR:
      /* get the ssrc so that we can route it to the right source pad */
      gst_rtcp_packet_sr_get_sender_info (&packet, &ssrc, NULL, NULL, NULL,
          NULL);
      break;
    case GST_RTCP_TYPE_RR:
      ssrc = gst_rtcp_packet_rr_get_ssrc (&packet);
      break;
    default:
      goto invalid_rtcp;
  }

  GST_DEBUG_OBJECT (demux, "received RTCP of SSRC %08x", ssrc);

  GST_PAD_LOCK (demux);
  dpad = find_demux_pad_for_ssrc (demux, ssrc);
  if (dpad == NULL) {
    GST_DEBUG_OBJECT (demux, "creating pad for SSRC %08x", ssrc);
    if (!(dpad = create_demux_pad_for_ssrc (demux, ssrc, -1)))
      goto create_failed;
  }
  GST_PAD_UNLOCK (demux);

  /* push to srcpad */
  ret = gst_pad_push (dpad->rtcp_pad, buf);

  return ret;

  /* ERRORS */
invalid_rtcp:
  {
    /* this is fatal and should be filtered earlier */
    GST_ELEMENT_ERROR (demux, STREAM, DECODE, (NULL),
        ("Dropping invalid RTCP packet"));
    gst_buffer_unref (buf);
    return GST_FLOW_ERROR;
  }
create_failed:
  {
    GST_ELEMENT_ERROR (demux, STREAM, DECODE, (NULL),
        ("Could not create new pad"));
    GST_PAD_UNLOCK (demux);
    gst_buffer_unref (buf);
    return GST_FLOW_ERROR;
  }
}

static gboolean
gst_rtp_ssrc_demux_src_event (GstPad * pad, GstEvent * event)
{
  GstRtpSsrcDemux *demux;
  gboolean res = FALSE;

  demux = GST_RTP_SSRC_DEMUX (gst_pad_get_parent (pad));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_SEEK:
    default:
      res = gst_pad_event_default (pad, event);
      break;
  }
  gst_object_unref (demux);
  return res;
}

static GList *
gst_rtp_ssrc_demux_internal_links (GstPad * pad)
{
  GstRtpSsrcDemux *demux;
  GList *res = NULL;
  GSList *walk;

  demux = GST_RTP_SSRC_DEMUX (gst_pad_get_parent (pad));

  GST_PAD_LOCK (demux);
  for (walk = demux->srcpads; walk; walk = g_slist_next (walk)) {
    GstRtpSsrcDemuxPad *dpad = (GstRtpSsrcDemuxPad *) walk->data;

    if (pad == demux->rtp_sink) {
      res = g_list_prepend (res, dpad->rtp_pad);
    } else if (pad == demux->rtcp_sink) {
      res = g_list_prepend (res, dpad->rtcp_pad);
    } else if (pad == dpad->rtp_pad) {
      res = g_list_prepend (res, demux->rtp_sink);
      break;
    } else if (pad == dpad->rtcp_pad) {
      res = g_list_prepend (res, demux->rtcp_sink);
      break;
    }
  }
  GST_PAD_UNLOCK (demux);

  gst_object_unref (demux);
  return res;
}

static gboolean
gst_rtp_ssrc_demux_src_query (GstPad * pad, GstQuery * query)
{
  GstRtpSsrcDemux *demux;
  gboolean res = FALSE;

  demux = GST_RTP_SSRC_DEMUX (gst_pad_get_parent (pad));

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_LATENCY:
    {

      if ((res = gst_pad_peer_query (demux->rtp_sink, query))) {
        gboolean live;
        GstClockTime min_latency, max_latency;
        GstRtpSsrcDemuxPad *demuxpad;

        demuxpad = gst_pad_get_element_private (pad);

        gst_query_parse_latency (query, &live, &min_latency, &max_latency);

        GST_DEBUG_OBJECT (demux, "peer min latency %" GST_TIME_FORMAT,
            GST_TIME_ARGS (min_latency));

        GST_DEBUG_OBJECT (demux,
            "latency for SSRC %08x, latency %" GST_TIME_FORMAT, demuxpad->ssrc,
            GST_TIME_ARGS (demuxpad->first_ts));

        gst_query_set_latency (query, live, min_latency, max_latency);
      }
      break;
    }
    default:
      res = gst_pad_query_default (pad, query);
      break;
  }
  gst_object_unref (demux);

  return res;
}

static GstStateChangeReturn
gst_rtp_ssrc_demux_change_state (GstElement * element,
    GstStateChange transition)
{
  GstStateChangeReturn ret;
  GstRtpSsrcDemux *demux;

  demux = GST_RTP_SSRC_DEMUX (element);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
    case GST_STATE_CHANGE_READY_TO_PAUSED:
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      gst_rtp_ssrc_demux_reset (demux);
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
    default:
      break;
  }
  return ret;
}
