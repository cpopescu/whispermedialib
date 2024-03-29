/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 * Copyright (C) <2005> Edgard Lima <edgard.lima@indt.org.br>
 * Copyright (C) <2005> Nokia Corporation <kai.vehmanen@nokia.com>
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

#include <stdlib.h>
#include <string.h>
#include <gst/rtp/gstrtpbuffer.h>

#include "gstrtppcmupay.h"

/* elementfactory information */
static const GstElementDetails gst_rtp_pcmu_pay_details =
GST_ELEMENT_DETAILS ("RTP packet payloader",
    "Codec/Payloader/Network",
    "Payload-encodes PCMU audio into a RTP packet",
    "Edgard Lima <edgard.lima@indt.org.br>");

static GstStaticPadTemplate gst_rtp_pcmu_pay_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-mulaw, channels=(int)1, rate=(int)8000")
    );

static GstStaticPadTemplate gst_rtp_pcmu_pay_src_template =
    GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-rtp, "
        "media = (string) \"audio\", "
        "payload = (int) " GST_RTP_PAYLOAD_PCMU_STRING ", "
        "clock-rate = (int) 8000, " "encoding-name = (string) \"PCMU\"; "
        "application/x-rtp, "
        "media = (string) \"audio\", "
        "payload = (int) " GST_RTP_PAYLOAD_DYNAMIC_STRING ", "
        "clock-rate = (int) 8000, " "encoding-name = (string) \"PCMU\"")
    );

static gboolean gst_rtp_pcmu_pay_setcaps (GstBaseRTPPayload * payload,
    GstCaps * caps);

GST_BOILERPLATE (GstRtpPcmuPay, gst_rtp_pcmu_pay, GstBaseRTPAudioPayload,
    GST_TYPE_BASE_RTP_AUDIO_PAYLOAD);

static void
gst_rtp_pcmu_pay_base_init (gpointer klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_rtp_pcmu_pay_sink_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_rtp_pcmu_pay_src_template));
  gst_element_class_set_details (element_class, &gst_rtp_pcmu_pay_details);
}

static void
gst_rtp_pcmu_pay_class_init (GstRtpPcmuPayClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstBaseRTPPayloadClass *gstbasertppayload_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;
  gstbasertppayload_class = (GstBaseRTPPayloadClass *) klass;

  parent_class = g_type_class_peek_parent (klass);

  gstbasertppayload_class->set_caps = gst_rtp_pcmu_pay_setcaps;
}

static void
gst_rtp_pcmu_pay_init (GstRtpPcmuPay * rtppcmupay, GstRtpPcmuPayClass * klass)
{
  GstBaseRTPAudioPayload *basertpaudiopayload;

  basertpaudiopayload = GST_BASE_RTP_AUDIO_PAYLOAD (rtppcmupay);

  GST_BASE_RTP_PAYLOAD (rtppcmupay)->clock_rate = 8000;

  /* tell basertpaudiopayload that this is a sample based codec */
  gst_base_rtp_audio_payload_set_sample_based (basertpaudiopayload);

  /* octet-per-sample is 1 for PCM */
  gst_base_rtp_audio_payload_set_sample_options (basertpaudiopayload, 1);
}

static gboolean
gst_rtp_pcmu_pay_setcaps (GstBaseRTPPayload * payload, GstCaps * caps)
{
  payload->pt = GST_RTP_PAYLOAD_PCMU;
  gst_basertppayload_set_options (payload, "audio", FALSE, "PCMU", 8000);

  gst_basertppayload_set_outcaps (payload, NULL);

  return TRUE;
}

gboolean
gst_rtp_pcmu_pay_plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "rtppcmupay",
      GST_RANK_NONE, GST_TYPE_RTP_PCMU_PAY);
}
