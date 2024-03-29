/* GStreamer
 * Copyright (C) <2006> Philippe Khalaf <burger@speedy.org>
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
#  include "config.h"
#endif

#include <string.h>
#include <stdlib.h>
#include <gst/rtp/gstrtpbuffer.h>
#include "gstrtpilbcdepay.h"

/* elementfactory information */
static const GstElementDetails gst_rtp_ilbc_depay_details =
GST_ELEMENT_DETAILS ("RTP iLBC packet depayloader",
    "Codec/Depayloader/Network",
    "Extracts iLBC audio from RTP packets",
    "Philippe Kalaf <philippe.kalaf@collabora.co.uk>");

/* RtpiLBCDepay signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

#define DEFAULT_MODE GST_ILBC_MODE_30

enum
{
  PROP_0,
  PROP_MODE
};

/* FIXME, mode should be string because it is a parameter in SDP fmtp */
static GstStaticPadTemplate gst_rtp_ilbc_depay_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-rtp, "
        "media = (string) \"audio\", "
        "payload = (int) " GST_RTP_PAYLOAD_DYNAMIC_STRING ", "
        "clock-rate = (int) 8000, "
        "encoding-name = (string) \"ILBC\", "
        "mode = (string) { \"20\", \"30\" }")
    );

static GstStaticPadTemplate gst_rtp_ilbc_depay_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-iLBC, " "mode = (int) { 20, 30 }")
    );

static void gst_ilbc_depay_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_ilbc_depay_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);

static GstBuffer *gst_rtp_ilbc_depay_process (GstBaseRTPDepayload * depayload,
    GstBuffer * buf);
static gboolean gst_rtp_ilbc_depay_setcaps (GstBaseRTPDepayload * depayload,
    GstCaps * caps);

GST_BOILERPLATE (GstRTPiLBCDepay, gst_rtp_ilbc_depay, GstBaseRTPDepayload,
    GST_TYPE_BASE_RTP_DEPAYLOAD);

#define GST_TYPE_ILBC_MODE (gst_ilbc_mode_get_type())
static GType
gst_ilbc_mode_get_type (void)
{
  static GType ilbc_mode_type = 0;
  static const GEnumValue ilbc_modes[] = {
    {GST_ILBC_MODE_20, "20ms frames", "20ms"},
    {GST_ILBC_MODE_30, "30ms frames", "30ms"},
    {0, NULL, NULL},
  };

  if (!ilbc_mode_type) {
    ilbc_mode_type = g_enum_register_static ("iLBCMode", ilbc_modes);
  }
  return ilbc_mode_type;
}

static void
gst_rtp_ilbc_depay_base_init (gpointer klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_rtp_ilbc_depay_src_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_rtp_ilbc_depay_sink_template));
  gst_element_class_set_details (element_class, &gst_rtp_ilbc_depay_details);
}

static void
gst_rtp_ilbc_depay_class_init (GstRTPiLBCDepayClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstBaseRTPDepayloadClass *gstbasertpdepayload_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;
  gstbasertpdepayload_class = (GstBaseRTPDepayloadClass *) klass;

  gobject_class->set_property = gst_ilbc_depay_set_property;
  gobject_class->get_property = gst_ilbc_depay_get_property;

  /* FIXME, mode is in the caps */
  g_object_class_install_property (gobject_class, PROP_MODE,
      g_param_spec_enum ("mode", "Mode", "iLBC frame mode",
          GST_TYPE_ILBC_MODE, DEFAULT_MODE, G_PARAM_READWRITE));

  gstbasertpdepayload_class->process = gst_rtp_ilbc_depay_process;
  gstbasertpdepayload_class->set_caps = gst_rtp_ilbc_depay_setcaps;
}

static void
gst_rtp_ilbc_depay_init (GstRTPiLBCDepay * rtpilbcdepay,
    GstRTPiLBCDepayClass * klass)
{
  GstBaseRTPDepayload *depayload;

  depayload = GST_BASE_RTP_DEPAYLOAD (rtpilbcdepay);

  /* Set default mode */
  rtpilbcdepay->mode = DEFAULT_MODE;
}

static gboolean
gst_rtp_ilbc_depay_setcaps (GstBaseRTPDepayload * depayload, GstCaps * caps)
{
  GstRTPiLBCDepay *rtpilbcdepay = GST_RTP_ILBC_DEPAY (depayload);
  GstCaps *srccaps;
  GstStructure *structure;
  const gchar *mode_str = NULL;
  gint mode;
  gboolean ret;

  structure = gst_caps_get_structure (caps, 0);

  mode = rtpilbcdepay->mode;

  /* parse mode, if we can */
  mode_str = gst_structure_get_string (structure, "mode");
  if (mode_str) {
    mode = strtol (mode_str, NULL, 10);
    if (mode != 20 && mode != 30)
      mode = rtpilbcdepay->mode;
  }

  rtpilbcdepay->mode = mode;

  srccaps = gst_caps_new_simple ("audio/x-iLBC",
      "mode", G_TYPE_INT, rtpilbcdepay->mode, NULL);
  ret = gst_pad_set_caps (GST_BASE_RTP_DEPAYLOAD_SRCPAD (depayload), srccaps);

  GST_DEBUG ("set caps on source: %" GST_PTR_FORMAT " (ret=%d)", srccaps, ret);
  gst_caps_unref (srccaps);

  /* always fixed clock rate of 8000 */
  depayload->clock_rate = 8000;

  return ret;
}

static GstBuffer *
gst_rtp_ilbc_depay_process (GstBaseRTPDepayload * depayload, GstBuffer * buf)
{
  GstBuffer *outbuf;

  GST_DEBUG ("process : got %d bytes, mark %d ts %u seqn %d",
      GST_BUFFER_SIZE (buf),
      gst_rtp_buffer_get_marker (buf),
      gst_rtp_buffer_get_timestamp (buf), gst_rtp_buffer_get_seq (buf));

  outbuf = gst_rtp_buffer_get_payload_buffer (buf);

  return outbuf;
}

static void
gst_ilbc_depay_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec)
{
  GstRTPiLBCDepay *rtpilbcdepay = GST_RTP_ILBC_DEPAY (object);

  switch (prop_id) {
    case PROP_MODE:
      rtpilbcdepay->mode = g_value_get_enum (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_ilbc_depay_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec)
{
  GstRTPiLBCDepay *rtpilbcdepay = GST_RTP_ILBC_DEPAY (object);

  switch (prop_id) {
    case PROP_MODE:
      g_value_set_enum (value, rtpilbcdepay->mode);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

gboolean
gst_rtp_ilbc_depay_plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "rtpilbcdepay",
      GST_RANK_MARGINAL, GST_TYPE_RTP_ILBC_DEPAY);
}
