/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 * Copyright (C) <2005> Edgard Lima <edgard.lima@indt.org.br>
 * Copyright (C) <2005> Zeeshan Ali <zeenix@gmail.com>
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
#include <gst/rtp/gstrtpbuffer.h>
#include "gstrtppcmudepay.h"

/* elementfactory information */
static const GstElementDetails gst_rtp_pcmudepay_details =
GST_ELEMENT_DETAILS ("RTP packet depayloader",
    "Codec/Depayloader/Network",
    "Extracts PCMU audio from RTP packets",
    "Edgard Lima <edgard.lima@indt.org.br>, Zeeshan Ali <zeenix@gmail.com>");

/* RtpPcmuDepay signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  ARG_0
};

static GstStaticPadTemplate gst_rtp_pcmu_depay_sink_template =
    GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-rtp, "
        "media = (string) \"audio\", "
        "payload = (int) " GST_RTP_PAYLOAD_DYNAMIC_STRING ", "
        "clock-rate = (int) 8000, " "encoding-name = (string) \"PCMU\";"
        "application/x-rtp, "
        "media = (string) \"audio\", "
        "payload = (int) " GST_RTP_PAYLOAD_PCMU_STRING ", "
        "clock-rate = (int) 8000")
    );

static GstStaticPadTemplate gst_rtp_pcmu_depay_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-mulaw, channels = (int) 1, rate = (int) 8000")
    );

static GstBuffer *gst_rtp_pcmu_depay_process (GstBaseRTPDepayload * depayload,
    GstBuffer * buf);
static gboolean gst_rtp_pcmu_depay_setcaps (GstBaseRTPDepayload * depayload,
    GstCaps * caps);

GST_BOILERPLATE (GstRtpPcmuDepay, gst_rtp_pcmu_depay, GstBaseRTPDepayload,
    GST_TYPE_BASE_RTP_DEPAYLOAD);

static void
gst_rtp_pcmu_depay_base_init (gpointer klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_rtp_pcmu_depay_src_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_rtp_pcmu_depay_sink_template));
  gst_element_class_set_details (element_class, &gst_rtp_pcmudepay_details);
}

static void
gst_rtp_pcmu_depay_class_init (GstRtpPcmuDepayClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstBaseRTPDepayloadClass *gstbasertpdepayload_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;
  gstbasertpdepayload_class = (GstBaseRTPDepayloadClass *) klass;

  parent_class = g_type_class_peek_parent (klass);

  gstbasertpdepayload_class->process = gst_rtp_pcmu_depay_process;
  gstbasertpdepayload_class->set_caps = gst_rtp_pcmu_depay_setcaps;
}

static void
gst_rtp_pcmu_depay_init (GstRtpPcmuDepay * rtppcmudepay,
    GstRtpPcmuDepayClass * klass)
{
  GstBaseRTPDepayload *depayload;

  depayload = GST_BASE_RTP_DEPAYLOAD (rtppcmudepay);

  gst_pad_use_fixed_caps (GST_BASE_RTP_DEPAYLOAD_SRCPAD (depayload));
}

static gboolean
gst_rtp_pcmu_depay_setcaps (GstBaseRTPDepayload * depayload, GstCaps * caps)
{
  GstCaps *srccaps;
  GstStructure *structure;
  gboolean ret;
  gint clock_rate = 8000;       /* default */

  structure = gst_caps_get_structure (caps, 0);

  gst_structure_get_int (structure, "clock-rate", &clock_rate);
  depayload->clock_rate = clock_rate;

  srccaps = gst_caps_new_simple ("audio/x-mulaw",
      "channels", G_TYPE_INT, 1, "rate", G_TYPE_INT, clock_rate, NULL);
  ret = gst_pad_set_caps (GST_BASE_RTP_DEPAYLOAD_SRCPAD (depayload), srccaps);
  gst_caps_unref (srccaps);

  return ret;
}

static GstBuffer *
gst_rtp_pcmu_depay_process (GstBaseRTPDepayload * depayload, GstBuffer * buf)
{
  GstCaps *srccaps;
  GstBuffer *outbuf = NULL;
  guint len;

  GST_DEBUG ("process : got %d bytes, mark %d ts %u seqn %d",
      GST_BUFFER_SIZE (buf),
      gst_rtp_buffer_get_marker (buf),
      gst_rtp_buffer_get_timestamp (buf), gst_rtp_buffer_get_seq (buf));

  srccaps = GST_PAD_CAPS (GST_BASE_RTP_DEPAYLOAD_SRCPAD (depayload));
  if (!srccaps) {
    /* Set the default caps */
    srccaps = gst_caps_new_simple ("audio/x-mulaw",
        "channels", G_TYPE_INT, 1, "rate", G_TYPE_INT, 8000, NULL);
    gst_pad_set_caps (GST_BASE_RTP_DEPAYLOAD_SRCPAD (depayload), srccaps);
    gst_caps_unref (srccaps);
  }

  len = gst_rtp_buffer_get_payload_len (buf);
  outbuf = gst_rtp_buffer_get_payload_buffer (buf);

  GST_BUFFER_DURATION (outbuf) =
      gst_util_uint64_scale_int (len, GST_SECOND, depayload->clock_rate);

  return outbuf;
}

gboolean
gst_rtp_pcmu_depay_plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "rtppcmudepay",
      GST_RANK_MARGINAL, GST_TYPE_RTP_PCMU_DEPAY);
}
