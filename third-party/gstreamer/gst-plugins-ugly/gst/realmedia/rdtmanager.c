/* GStreamer
 * Copyright (C) <2005,2006> Wim Taymans <wim@fluendo.com>
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
/*
 * Unless otherwise indicated, Source Code is licensed under MIT license.
 * See further explanation attached in License Statement (distributed in the file
 * LICENSE).
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
 * of the Software, and to permit persons to whom the Software is furnished to do
 * so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */
/* Element-Checklist-Version: 5 */

/**
 * SECTION:element-rdtmanager
 * @see_also: GstRtspSrc
 *
 * A simple RTP session manager used internally by rtspsrc.
 *
 * Last reviewed on 2006-06-20 (0.10.4)
 */

/* #define HAVE_RTCP */

#include "rdtmanager.h"

GST_DEBUG_CATEGORY_STATIC (rdtmanager_debug);
#define GST_CAT_DEFAULT (rdtmanager_debug)

/* elementfactory information */
static const GstElementDetails rdtmanager_details =
GST_ELEMENT_DETAILS ("RTP Decoder",
    "Codec/Parser/Network",
    "Accepts raw RTP and RTCP packets and sends them forward",
    "Wim Taymans <wim@fluendo.com>");

/* GstRDTManager signals and args */
enum
{
  SIGNAL_REQUEST_PT_MAP,
  SIGNAL_CLEAR_PT_MAP,

  SIGNAL_ON_NEW_SSRC,
  SIGNAL_ON_SSRC_COLLISION,
  SIGNAL_ON_SSRC_VALIDATED,
  SIGNAL_ON_SSRC_ACTIVE,
  SIGNAL_ON_SSRC_SDES,
  SIGNAL_ON_BYE_SSRC,
  SIGNAL_ON_BYE_TIMEOUT,
  SIGNAL_ON_TIMEOUT,
  LAST_SIGNAL
};

#define DEFAULT_LATENCY_MS      200

enum
{
  PROP_0,
  PROP_LATENCY
};

static GstStaticPadTemplate gst_rdt_manager_recv_rtp_sink_template =
GST_STATIC_PAD_TEMPLATE ("recv_rtp_sink_%d",
    GST_PAD_SINK,
    GST_PAD_REQUEST,
    GST_STATIC_CAPS ("application/x-rdt")
    );

static GstStaticPadTemplate gst_rdt_manager_recv_rtcp_sink_template =
GST_STATIC_PAD_TEMPLATE ("recv_rtcp_sink_%d",
    GST_PAD_SINK,
    GST_PAD_REQUEST,
    GST_STATIC_CAPS ("application/x-rtcp")
    );

static GstStaticPadTemplate gst_rdt_manager_recv_rtp_src_template =
GST_STATIC_PAD_TEMPLATE ("recv_rtp_src_%d_%d_%d",
    GST_PAD_SRC,
    GST_PAD_SOMETIMES,
    GST_STATIC_CAPS ("application/x-rdt")
    );

static GstStaticPadTemplate gst_rdt_manager_rtcp_src_template =
GST_STATIC_PAD_TEMPLATE ("rtcp_src_%d",
    GST_PAD_SRC,
    GST_PAD_REQUEST,
    GST_STATIC_CAPS ("application/x-rtcp")
    );

static void gst_rdt_manager_finalize (GObject * object);
static void gst_rdt_manager_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_rdt_manager_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);

static GstClock *gst_rdt_manager_provide_clock (GstElement * element);
static GstStateChangeReturn gst_rdt_manager_change_state (GstElement * element,
    GstStateChange transition);
static GstPad *gst_rdt_manager_request_new_pad (GstElement * element,
    GstPadTemplate * templ, const gchar * name);
static void gst_rdt_manager_release_pad (GstElement * element, GstPad * pad);

static GstFlowReturn gst_rdt_manager_chain_rtp (GstPad * pad,
    GstBuffer * buffer);
static GstFlowReturn gst_rdt_manager_chain_rtcp (GstPad * pad,
    GstBuffer * buffer);


/* Manages the receiving end of the packets.
 *
 * There is one such structure for each RTP session (audio/video/...).
 * We get the RTP/RTCP packets and stuff them into the session manager. 
 */
struct _GstRDTManagerSession
{
  /* session id */
  gint id;
  /* the parent bin */
  GstRDTManager *dec;

  gboolean active;
  /* we only support one ssrc and one pt */
  guint32 ssrc;
  guint8 pt;
  GstCaps *caps;

  /* the pads of the session */
  GstPad *recv_rtp_sink;
  GstPad *recv_rtp_src;
  GstPad *recv_rtcp_sink;
  GstPad *rtcp_src;
};

/* find a session with the given id */
static GstRDTManagerSession *
find_session_by_id (GstRDTManager * rdtmanager, gint id)
{
  GSList *walk;

  for (walk = rdtmanager->sessions; walk; walk = g_slist_next (walk)) {
    GstRDTManagerSession *sess = (GstRDTManagerSession *) walk->data;

    if (sess->id == id)
      return sess;
  }
  return NULL;
}

/* create a session with the given id */
static GstRDTManagerSession *
create_session (GstRDTManager * rdtmanager, gint id)
{
  GstRDTManagerSession *sess;

  sess = g_new0 (GstRDTManagerSession, 1);
  sess->id = id;
  sess->dec = rdtmanager;
  rdtmanager->sessions = g_slist_prepend (rdtmanager->sessions, sess);

  return sess;
}

static void
free_session (GstRDTManagerSession * session)
{
  g_free (session);
}

static guint gst_rdt_manager_signals[LAST_SIGNAL] = { 0 };

GST_BOILERPLATE (GstRDTManager, gst_rdt_manager, GstElement, GST_TYPE_ELEMENT);

static void
gst_rdt_manager_base_init (gpointer klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  /* sink pads */
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_rdt_manager_recv_rtp_sink_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_rdt_manager_recv_rtcp_sink_template));
  /* src pads */
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_rdt_manager_recv_rtp_src_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_rdt_manager_rtcp_src_template));

  gst_element_class_set_details (element_class, &rdtmanager_details);
}

/* BOXED:UINT,UINT */
#define g_marshal_value_peek_uint(v)     g_value_get_uint (v)

void
gst_rdt_manager_marshal_BOXED__UINT_UINT (GClosure * closure,
    GValue * return_value,
    guint n_param_values,
    const GValue * param_values,
    gpointer invocation_hint, gpointer marshal_data)
{
  typedef gpointer (*GMarshalFunc_BOXED__UINT_UINT) (gpointer data1,
      guint arg_1, guint arg_2, gpointer data2);
  register GMarshalFunc_BOXED__UINT_UINT callback;
  register GCClosure *cc = (GCClosure *) closure;
  register gpointer data1, data2;
  gpointer v_return;

  g_return_if_fail (return_value != NULL);
  g_return_if_fail (n_param_values == 3);

  if (G_CCLOSURE_SWAP_DATA (closure)) {
    data1 = closure->data;
    data2 = g_value_peek_pointer (param_values + 0);
  } else {
    data1 = g_value_peek_pointer (param_values + 0);
    data2 = closure->data;
  }
  callback =
      (GMarshalFunc_BOXED__UINT_UINT) (marshal_data ? marshal_data : cc->
      callback);

  v_return = callback (data1,
      g_marshal_value_peek_uint (param_values + 1),
      g_marshal_value_peek_uint (param_values + 2), data2);

  g_value_take_boxed (return_value, v_return);
}

void
gst_rdt_manager_marshal_VOID__UINT_UINT (GClosure * closure,
    GValue * return_value,
    guint n_param_values,
    const GValue * param_values,
    gpointer invocation_hint, gpointer marshal_data)
{
  typedef void (*GMarshalFunc_VOID__UINT_UINT) (gpointer data1,
      guint arg_1, guint arg_2, gpointer data2);
  register GMarshalFunc_VOID__UINT_UINT callback;
  register GCClosure *cc = (GCClosure *) closure;
  register gpointer data1, data2;

  g_return_if_fail (n_param_values == 3);

  if (G_CCLOSURE_SWAP_DATA (closure)) {
    data1 = closure->data;
    data2 = g_value_peek_pointer (param_values + 0);
  } else {
    data1 = g_value_peek_pointer (param_values + 0);
    data2 = closure->data;
  }
  callback =
      (GMarshalFunc_VOID__UINT_UINT) (marshal_data ? marshal_data : cc->
      callback);

  callback (data1,
      g_marshal_value_peek_uint (param_values + 1),
      g_marshal_value_peek_uint (param_values + 2), data2);
}

static void
gst_rdt_manager_class_init (GstRDTManagerClass * g_class)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstRDTManagerClass *klass;

  klass = (GstRDTManagerClass *) g_class;
  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  gobject_class->finalize = gst_rdt_manager_finalize;
  gobject_class->set_property = gst_rdt_manager_set_property;
  gobject_class->get_property = gst_rdt_manager_get_property;

  g_object_class_install_property (gobject_class, PROP_LATENCY,
      g_param_spec_uint ("latency", "Buffer latency in ms",
          "Amount of ms to buffer", 0, G_MAXUINT, DEFAULT_LATENCY_MS,
          G_PARAM_READWRITE));

  /**
   * GstRDTManager::request-pt-map:
   * @rdtmanager: the object which received the signal
   * @session: the session
   * @pt: the pt
   *
   * Request the payload type as #GstCaps for @pt in @session.
   */
  gst_rdt_manager_signals[SIGNAL_REQUEST_PT_MAP] =
      g_signal_new ("request-pt-map", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, G_STRUCT_OFFSET (GstRDTManagerClass, request_pt_map),
      NULL, NULL, gst_rdt_manager_marshal_BOXED__UINT_UINT, GST_TYPE_CAPS, 2,
      G_TYPE_UINT, G_TYPE_UINT);

  /**
   * GstRDTManager::clear-pt-map:
   * @rtpbin: the object which received the signal
   *
   * Clear all previously cached pt-mapping obtained with
   * GstRDTManager::request-pt-map.
   */
  gst_rdt_manager_signals[SIGNAL_CLEAR_PT_MAP] =
      g_signal_new ("clear-pt-map", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, G_STRUCT_OFFSET (GstRDTManagerClass, clear_pt_map),
      NULL, NULL, g_cclosure_marshal_VOID__VOID, G_TYPE_NONE, 0, G_TYPE_NONE);

  /**
   * GstRDTManager::on-bye-ssrc:
   * @rtpbin: the object which received the signal
   * @session: the session
   * @ssrc: the SSRC 
   *
   * Notify of an SSRC that became inactive because of a BYE packet.
   */
  gst_rdt_manager_signals[SIGNAL_ON_BYE_SSRC] =
      g_signal_new ("on-bye-ssrc", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, G_STRUCT_OFFSET (GstRDTManagerClass, on_bye_ssrc),
      NULL, NULL, gst_rdt_manager_marshal_VOID__UINT_UINT, G_TYPE_NONE, 2,
      G_TYPE_UINT, G_TYPE_UINT);
  /**
   * GstRDTManager::on-bye-timeout:
   * @rtpbin: the object which received the signal
   * @session: the session
   * @ssrc: the SSRC 
   *
   * Notify of an SSRC that has timed out because of BYE
   */
  gst_rdt_manager_signals[SIGNAL_ON_BYE_TIMEOUT] =
      g_signal_new ("on-bye-timeout", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, G_STRUCT_OFFSET (GstRDTManagerClass, on_bye_timeout),
      NULL, NULL, gst_rdt_manager_marshal_VOID__UINT_UINT, G_TYPE_NONE, 2,
      G_TYPE_UINT, G_TYPE_UINT);
  /**
   * GstRDTManager::on-timeout:
   * @rtpbin: the object which received the signal
   * @session: the session
   * @ssrc: the SSRC 
   *
   * Notify of an SSRC that has timed out
   */
  gst_rdt_manager_signals[SIGNAL_ON_TIMEOUT] =
      g_signal_new ("on-timeout", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, G_STRUCT_OFFSET (GstRDTManagerClass, on_timeout),
      NULL, NULL, gst_rdt_manager_marshal_VOID__UINT_UINT, G_TYPE_NONE, 2,
      G_TYPE_UINT, G_TYPE_UINT);

  gstelement_class->provide_clock =
      GST_DEBUG_FUNCPTR (gst_rdt_manager_provide_clock);
  gstelement_class->change_state =
      GST_DEBUG_FUNCPTR (gst_rdt_manager_change_state);
  gstelement_class->request_new_pad =
      GST_DEBUG_FUNCPTR (gst_rdt_manager_request_new_pad);
  gstelement_class->release_pad =
      GST_DEBUG_FUNCPTR (gst_rdt_manager_release_pad);

  GST_DEBUG_CATEGORY_INIT (rdtmanager_debug, "rdtmanager", 0, "RTP decoder");
}

static void
gst_rdt_manager_init (GstRDTManager * rdtmanager, GstRDTManagerClass * klass)
{
  rdtmanager->provided_clock = gst_system_clock_obtain ();
  rdtmanager->latency = DEFAULT_LATENCY_MS;
}

static void
gst_rdt_manager_finalize (GObject * object)
{
  GstRDTManager *rdtmanager;

  rdtmanager = GST_RDT_MANAGER (object);

  g_slist_foreach (rdtmanager->sessions, (GFunc) free_session, NULL);
  g_slist_free (rdtmanager->sessions);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static gboolean
gst_rdt_manager_query_src (GstPad * pad, GstQuery * query)
{
  GstRDTManager *rdtmanager;
  gboolean res;

  rdtmanager = GST_RDT_MANAGER (GST_PAD_PARENT (pad));

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_LATENCY:
    {
      /* we pretend to be live with a 3 second latency */
      gst_query_set_latency (query, TRUE, 3 * GST_SECOND, -1);
      res = TRUE;
      break;
    }
    default:
      res = gst_pad_query_default (pad, query);
      break;
  }
  return res;
}

static GstFlowReturn
gst_rdt_manager_chain_rtp (GstPad * pad, GstBuffer * buffer)
{
  GstFlowReturn res;
  GstRDTManager *rdtmanager;
  GstRDTManagerSession *session;
  guint32 ssrc;
  guint8 pt;

  rdtmanager = GST_RDT_MANAGER (GST_PAD_PARENT (pad));

  GST_DEBUG_OBJECT (rdtmanager, "got rtp packet");

  ssrc = 0;
  pt = 0;

  GST_DEBUG_OBJECT (rdtmanager, "SSRC %08x, PT %d", ssrc, pt);

  /* find session */
  session = gst_pad_get_element_private (pad);

  /* see if we have the pad */
  if (!session->active) {
    GstPadTemplate *templ;
    GstElementClass *klass;
    gchar *name;
    GstCaps *caps;
    GValue ret = { 0 };
    GValue args[3] = { {0}
    , {0}
    , {0}
    };

    GST_DEBUG_OBJECT (rdtmanager, "creating stream");

    session->ssrc = ssrc;
    session->pt = pt;

    /* get pt map */
    g_value_init (&args[0], GST_TYPE_ELEMENT);
    g_value_set_object (&args[0], rdtmanager);
    g_value_init (&args[1], G_TYPE_UINT);
    g_value_set_uint (&args[1], session->id);
    g_value_init (&args[2], G_TYPE_UINT);
    g_value_set_uint (&args[2], pt);

    g_value_init (&ret, GST_TYPE_CAPS);
    g_value_set_boxed (&ret, NULL);

    g_signal_emitv (args, gst_rdt_manager_signals[SIGNAL_REQUEST_PT_MAP], 0,
        &ret);

    caps = (GstCaps *) g_value_get_boxed (&ret);

    name = g_strdup_printf ("recv_rtp_src_%d_%u_%d", session->id, ssrc, pt);
    klass = GST_ELEMENT_GET_CLASS (rdtmanager);
    templ = gst_element_class_get_pad_template (klass, "recv_rtp_src_%d_%d_%d");
    session->recv_rtp_src = gst_pad_new_from_template (templ, name);
    g_free (name);

    gst_pad_set_caps (session->recv_rtp_src, caps);

    gst_pad_set_element_private (session->recv_rtp_src, session);
    gst_pad_set_query_function (session->recv_rtp_src,
        gst_rdt_manager_query_src);
    gst_pad_set_active (session->recv_rtp_src, TRUE);
    gst_element_add_pad (GST_ELEMENT_CAST (rdtmanager), session->recv_rtp_src);

    session->active = TRUE;
  }

  gst_buffer_set_caps (buffer, GST_PAD_CAPS (session->recv_rtp_src));

  res = gst_pad_push (session->recv_rtp_src, buffer);

  return res;
}

static GstFlowReturn
gst_rdt_manager_chain_rtcp (GstPad * pad, GstBuffer * buffer)
{
  GstRDTManager *src;

#ifdef HAVE_RTCP
  gboolean valid;
  GstRTCPPacket packet;
  gboolean more;
#endif

  src = GST_RDT_MANAGER (GST_PAD_PARENT (pad));

  GST_DEBUG_OBJECT (src, "got rtcp packet");

#ifdef HAVE_RTCP
  valid = gst_rtcp_buffer_validate (buffer);
  if (!valid)
    goto bad_packet;

  /* position on first packet */
  more = gst_rtcp_buffer_get_first_packet (buffer, &packet);
  while (more) {
    switch (gst_rtcp_packet_get_type (&packet)) {
      case GST_RTCP_TYPE_SR:
      {
        guint32 ssrc, rtptime, packet_count, octet_count;
        guint64 ntptime;
        guint count, i;

        gst_rtcp_packet_sr_get_sender_info (&packet, &ssrc, &ntptime, &rtptime,
            &packet_count, &octet_count);

        GST_DEBUG_OBJECT (src,
            "got SR packet: SSRC %08x, NTP %" G_GUINT64_FORMAT
            ", RTP %u, PC %u, OC %u", ssrc, ntptime, rtptime, packet_count,
            octet_count);

        count = gst_rtcp_packet_get_rb_count (&packet);
        for (i = 0; i < count; i++) {
          guint32 ssrc, exthighestseq, jitter, lsr, dlsr;
          guint8 fractionlost;
          gint32 packetslost;

          gst_rtcp_packet_get_rb (&packet, i, &ssrc, &fractionlost,
              &packetslost, &exthighestseq, &jitter, &lsr, &dlsr);

          GST_DEBUG_OBJECT (src, "got RB packet %d: SSRC %08x, FL %u"
              ", PL %u, HS %u, JITTER %u, LSR %u, DLSR %u", ssrc, fractionlost,
              packetslost, exthighestseq, jitter, lsr, dlsr);
        }
        break;
      }
      case GST_RTCP_TYPE_RR:
      {
        guint32 ssrc;
        guint count, i;

        ssrc = gst_rtcp_packet_rr_get_ssrc (&packet);

        GST_DEBUG_OBJECT (src, "got RR packet: SSRC %08x", ssrc);

        count = gst_rtcp_packet_get_rb_count (&packet);
        for (i = 0; i < count; i++) {
          guint32 ssrc, exthighestseq, jitter, lsr, dlsr;
          guint8 fractionlost;
          gint32 packetslost;

          gst_rtcp_packet_get_rb (&packet, i, &ssrc, &fractionlost,
              &packetslost, &exthighestseq, &jitter, &lsr, &dlsr);

          GST_DEBUG_OBJECT (src, "got RB packet %d: SSRC %08x, FL %u"
              ", PL %u, HS %u, JITTER %u, LSR %u, DLSR %u", ssrc, fractionlost,
              packetslost, exthighestseq, jitter, lsr, dlsr);
        }
        break;
      }
      case GST_RTCP_TYPE_SDES:
      {
        guint chunks, i, j;
        gboolean more_chunks, more_items;

        chunks = gst_rtcp_packet_sdes_get_chunk_count (&packet);
        GST_DEBUG_OBJECT (src, "got SDES packet with %d chunks", chunks);

        more_chunks = gst_rtcp_packet_sdes_first_chunk (&packet);
        i = 0;
        while (more_chunks) {
          guint32 ssrc;

          ssrc = gst_rtcp_packet_sdes_get_ssrc (&packet);

          GST_DEBUG_OBJECT (src, "chunk %d, SSRC %08x", i, ssrc);

          more_items = gst_rtcp_packet_sdes_first_item (&packet);
          j = 0;
          while (more_items) {
            GstRTCPSDESType type;
            guint8 len;
            gchar *data;

            gst_rtcp_packet_sdes_get_item (&packet, &type, &len, &data);

            GST_DEBUG_OBJECT (src, "item %d, type %d, len %d, data %s", j,
                type, len, data);

            more_items = gst_rtcp_packet_sdes_next_item (&packet);
            j++;
          }
          more_chunks = gst_rtcp_packet_sdes_next_chunk (&packet);
          i++;
        }
        break;
      }
      case GST_RTCP_TYPE_BYE:
      {
        guint count, i;
        gchar *reason;

        reason = gst_rtcp_packet_bye_get_reason (&packet);
        GST_DEBUG_OBJECT (src, "got BYE packet (reason: %s)",
            GST_STR_NULL (reason));
        g_free (reason);

        count = gst_rtcp_packet_bye_get_ssrc_count (&packet);
        for (i = 0; i < count; i++) {
          guint32 ssrc;


          ssrc = gst_rtcp_packet_bye_get_nth_ssrc (&packet, i);

          GST_DEBUG_OBJECT (src, "SSRC: %08x", ssrc);
        }
        break;
      }
      case GST_RTCP_TYPE_APP:
        GST_DEBUG_OBJECT (src, "got APP packet");
        break;
      default:
        GST_WARNING_OBJECT (src, "got unknown RTCP packet");
        break;
    }
    more = gst_rtcp_packet_move_to_next (&packet);
  }
  gst_buffer_unref (buffer);
  return GST_FLOW_OK;

bad_packet:
  {
    GST_WARNING_OBJECT (src, "got invalid RTCP packet");
    return GST_FLOW_OK;
  }
#else
  return GST_FLOW_OK;
#endif
}

static void
gst_rdt_manager_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstRDTManager *src;

  src = GST_RDT_MANAGER (object);

  switch (prop_id) {
    case PROP_LATENCY:
      src->latency = g_value_get_uint (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_rdt_manager_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstRDTManager *src;

  src = GST_RDT_MANAGER (object);

  switch (prop_id) {
    case PROP_LATENCY:
      g_value_set_uint (value, src->latency);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstClock *
gst_rdt_manager_provide_clock (GstElement * element)
{
  GstRDTManager *rdtmanager;

  rdtmanager = GST_RDT_MANAGER (element);

  return GST_CLOCK_CAST (gst_object_ref (rdtmanager->provided_clock));
}

static GstStateChangeReturn
gst_rdt_manager_change_state (GstElement * element, GstStateChange transition)
{
  GstStateChangeReturn ret;
  GstRDTManager *rdtmanager;

  rdtmanager = GST_RDT_MANAGER (element);

  switch (transition) {
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_PAUSED:
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
      /* we're NO_PREROLL when going to PAUSED */
      ret = GST_STATE_CHANGE_NO_PREROLL;
      break;
    default:
      break;
  }

  return ret;
}

/* Create a pad for receiving RTP for the session in @name
 */
static GstPad *
create_recv_rtp (GstRDTManager * rdtmanager, GstPadTemplate * templ,
    const gchar * name)
{
  guint sessid;
  GstRDTManagerSession *session;

  /* first get the session number */
  if (name == NULL || sscanf (name, "recv_rtp_sink_%d", &sessid) != 1)
    goto no_name;

  GST_DEBUG_OBJECT (rdtmanager, "finding session %d", sessid);

  /* get or create session */
  session = find_session_by_id (rdtmanager, sessid);
  if (!session) {
    GST_DEBUG_OBJECT (rdtmanager, "creating session %d", sessid);
    /* create session now */
    session = create_session (rdtmanager, sessid);
    if (session == NULL)
      goto create_error;
  }
  /* check if pad was requested */
  if (session->recv_rtp_sink != NULL)
    goto existed;

  GST_DEBUG_OBJECT (rdtmanager, "getting RTP sink pad");

  session->recv_rtp_sink = gst_pad_new_from_template (templ, name);
  gst_pad_set_element_private (session->recv_rtp_sink, session);
  gst_pad_set_chain_function (session->recv_rtp_sink,
      gst_rdt_manager_chain_rtp);
  gst_pad_set_active (session->recv_rtp_sink, TRUE);
  gst_element_add_pad (GST_ELEMENT_CAST (rdtmanager), session->recv_rtp_sink);

  return session->recv_rtp_sink;

  /* ERRORS */
no_name:
  {
    g_warning ("rdtmanager: invalid name given");
    return NULL;
  }
create_error:
  {
    /* create_session already warned */
    return NULL;
  }
existed:
  {
    g_warning ("rdtmanager: recv_rtp pad already requested for session %d",
        sessid);
    return NULL;
  }
}

/* Create a pad for receiving RTCP for the session in @name
 */
static GstPad *
create_recv_rtcp (GstRDTManager * rdtmanager, GstPadTemplate * templ,
    const gchar * name)
{
  guint sessid;
  GstRDTManagerSession *session;

  /* first get the session number */
  if (name == NULL || sscanf (name, "recv_rtcp_sink_%d", &sessid) != 1)
    goto no_name;

  GST_DEBUG_OBJECT (rdtmanager, "finding session %d", sessid);

  /* get the session, it must exist or we error */
  session = find_session_by_id (rdtmanager, sessid);
  if (!session)
    goto no_session;

  /* check if pad was requested */
  if (session->recv_rtcp_sink != NULL)
    goto existed;

  GST_DEBUG_OBJECT (rdtmanager, "getting RTCP sink pad");

  session->recv_rtcp_sink = gst_pad_new_from_template (templ, name);
  gst_pad_set_element_private (session->recv_rtp_sink, session);
  gst_pad_set_chain_function (session->recv_rtcp_sink,
      gst_rdt_manager_chain_rtcp);
  gst_pad_set_active (session->recv_rtcp_sink, TRUE);
  gst_element_add_pad (GST_ELEMENT_CAST (rdtmanager), session->recv_rtcp_sink);

  return session->recv_rtcp_sink;

  /* ERRORS */
no_name:
  {
    g_warning ("rdtmanager: invalid name given");
    return NULL;
  }
no_session:
  {
    g_warning ("rdtmanager: no session with id %d", sessid);
    return NULL;
  }
existed:
  {
    g_warning ("rdtmanager: recv_rtcp pad already requested for session %d",
        sessid);
    return NULL;
  }
}

/* Create a pad for sending RTCP for the session in @name
 */
static GstPad *
create_rtcp (GstRDTManager * rdtmanager, GstPadTemplate * templ,
    const gchar * name)
{
  guint sessid;
  GstRDTManagerSession *session;

  /* first get the session number */
  if (name == NULL || sscanf (name, "rtcp_src_%d", &sessid) != 1)
    goto no_name;

  /* get or create session */
  session = find_session_by_id (rdtmanager, sessid);
  if (!session)
    goto no_session;

  /* check if pad was requested */
  if (session->rtcp_src != NULL)
    goto existed;

  session->rtcp_src = gst_pad_new_from_template (templ, name);
  gst_pad_set_active (session->rtcp_src, TRUE);
  gst_element_add_pad (GST_ELEMENT_CAST (rdtmanager), session->rtcp_src);

  return session->rtcp_src;

  /* ERRORS */
no_name:
  {
    g_warning ("rdtmanager: invalid name given");
    return NULL;
  }
no_session:
  {
    g_warning ("rdtmanager: session with id %d does not exist", sessid);
    return NULL;
  }
existed:
  {
    g_warning ("rdtmanager: rtcp_src pad already requested for session %d",
        sessid);
    return NULL;
  }
}

/* 
 */
static GstPad *
gst_rdt_manager_request_new_pad (GstElement * element,
    GstPadTemplate * templ, const gchar * name)
{
  GstRDTManager *rdtmanager;
  GstElementClass *klass;
  GstPad *result;

  g_return_val_if_fail (templ != NULL, NULL);
  g_return_val_if_fail (GST_IS_RDT_MANAGER (element), NULL);

  rdtmanager = GST_RDT_MANAGER (element);
  klass = GST_ELEMENT_GET_CLASS (element);

  /* figure out the template */
  if (templ == gst_element_class_get_pad_template (klass, "recv_rtp_sink_%d")) {
    result = create_recv_rtp (rdtmanager, templ, name);
  } else if (templ == gst_element_class_get_pad_template (klass,
          "recv_rtcp_sink_%d")) {
    result = create_recv_rtcp (rdtmanager, templ, name);
  } else if (templ == gst_element_class_get_pad_template (klass, "rtcp_src_%d")) {
    result = create_rtcp (rdtmanager, templ, name);
  } else
    goto wrong_template;

  return result;

  /* ERRORS */
wrong_template:
  {
    g_warning ("rdtmanager: this is not our template");
    return NULL;
  }
}

static void
gst_rdt_manager_release_pad (GstElement * element, GstPad * pad)
{
}

gboolean
gst_rdt_manager_plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "rdtmanager",
      GST_RANK_NONE, GST_TYPE_RDT_MANAGER);
}
