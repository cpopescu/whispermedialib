/* GStreamer
 *
 * Copyright 2007 Nokia Corporation
 * Copyright 2007 Collabora Ltd,
 *  @author: Philippe Kalaf <philippe.kalaf@collabora.co.uk>
 *
 * Copyright (C) <2005> Wim Taymans <wim@fluendo.com>
 *               <2007> Edward Hervey <bilboed@bilboed.com>
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
#include "gstrtph263depay.h"

GST_DEBUG_CATEGORY_STATIC (rtph263depay_debug);
#define GST_CAT_DEFAULT (rtph263depay_debug)

/* elementfactory information */
static const GstElementDetails gst_rtp_h263depay_details =
GST_ELEMENT_DETAILS ("RTP H263 packet depayloader",
    "Codec/Depayloader/Network",
    "Extracts H263 video from RTP packets (RFC 2190)",
    "Philippe Kalaf <philippe.kalaf@collabora.co.uk>, "
    "Edward Hervey <bilboed@bilboed.com>");

#define GST_RFC2190A_HEADER_LEN 4
#define GST_RFC2190B_HEADER_LEN 8
#define GST_RFC2190C_HEADER_LEN 12

static GstStaticPadTemplate gst_rtp_h263_depay_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-h263, "
        "variant = (string) \"itu\", " "h263version = (string) \"h263\"")
    );

static GstStaticPadTemplate gst_rtp_h263_depay_sink_template =
    GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-rtp, "
        "media = (string) \"video\", "
        "payload = (int) " GST_RTP_PAYLOAD_H263_STRING ", "
        "clock-rate = (int) 90000, " "encoding-name = (string) \"H263\"; "
        "application/x-rtp, "
        "media = (string) \"video\", "
        "payload = (int) " GST_RTP_PAYLOAD_DYNAMIC_STRING ", "
        "clock-rate = (int) 90000, " "encoding-name = (string) \"H263\"")
    );

GST_BOILERPLATE (GstRtpH263Depay, gst_rtp_h263_depay, GstBaseRTPDepayload,
    GST_TYPE_BASE_RTP_DEPAYLOAD);

static void gst_rtp_h263_depay_finalize (GObject * object);

static GstStateChangeReturn gst_rtp_h263_depay_change_state (GstElement *
    element, GstStateChange transition);

static GstBuffer *gst_rtp_h263_depay_process (GstBaseRTPDepayload * depayload,
    GstBuffer * buf);
gboolean gst_rtp_h263_depay_setcaps (GstBaseRTPDepayload * filter,
    GstCaps * caps);

static void
gst_rtp_h263_depay_base_init (gpointer klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_rtp_h263_depay_src_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_rtp_h263_depay_sink_template));

  gst_element_class_set_details (element_class, &gst_rtp_h263depay_details);
}

static void
gst_rtp_h263_depay_class_init (GstRtpH263DepayClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstBaseRTPDepayloadClass *gstbasertpdepayload_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;
  gstbasertpdepayload_class = (GstBaseRTPDepayloadClass *) klass;

  parent_class = g_type_class_peek_parent (klass);

  gstbasertpdepayload_class->process = gst_rtp_h263_depay_process;
  gstbasertpdepayload_class->set_caps = gst_rtp_h263_depay_setcaps;

  gobject_class->finalize = gst_rtp_h263_depay_finalize;

  gstelement_class->change_state = gst_rtp_h263_depay_change_state;

  GST_DEBUG_CATEGORY_INIT (rtph263depay_debug, "rtph263depay", 0,
      "H263 Video RTP Depayloader");
}

static void
gst_rtp_h263_depay_init (GstRtpH263Depay * rtph263depay,
    GstRtpH263DepayClass * klass)
{
  rtph263depay->adapter = gst_adapter_new ();

  rtph263depay->offset = 0;
  rtph263depay->leftover = 0;
}

static void
gst_rtp_h263_depay_finalize (GObject * object)
{
  GstRtpH263Depay *rtph263depay;

  rtph263depay = GST_RTP_H263_DEPAY (object);

  g_object_unref (rtph263depay->adapter);
  rtph263depay->adapter = NULL;

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

gboolean
gst_rtp_h263_depay_setcaps (GstBaseRTPDepayload * filter, GstCaps * caps)
{
  GstCaps *srccaps;
  GstStructure *structure = gst_caps_get_structure (caps, 0);
  gint clock_rate = 90000;      /* default */

  gst_structure_get_int (structure, "clock-rate", &clock_rate);
  filter->clock_rate = clock_rate;

  srccaps = gst_caps_new_simple ("video/x-h263",
      "variant", G_TYPE_STRING, "itu",
      "h263version", G_TYPE_STRING, "h263", NULL);
  gst_pad_set_caps (GST_BASE_RTP_DEPAYLOAD_SRCPAD (filter), srccaps);
  gst_caps_unref (srccaps);

  return TRUE;
}

static GstBuffer *
gst_rtp_h263_depay_process (GstBaseRTPDepayload * depayload, GstBuffer * buf)
{

  GstRtpH263Depay *rtph263depay;
  GstBuffer *outbuf;
  gint payload_len;
  guint8 *payload;
  guint header_len;

  guint SBIT, EBIT;
  gboolean F, P, M;
  gboolean I;

  rtph263depay = GST_RTP_H263_DEPAY (depayload);

  if (!gst_rtp_buffer_validate (buf))
    goto bad_packet;

  /* flush remaining data on discont */
  if (GST_BUFFER_IS_DISCONT (buf)) {
    GST_LOG_OBJECT (depayload, "Discont buffer, flushing adapter");
    gst_adapter_clear (rtph263depay->adapter);
    rtph263depay->offset = 0;
    rtph263depay->leftover = 0;
  }

  payload_len = gst_rtp_buffer_get_payload_len (buf);
  payload = gst_rtp_buffer_get_payload (buf);

  M = gst_rtp_buffer_get_marker (buf);

  /* Let's see what mode we are using */
  F = (payload[0] & 0x80) == 0x80;
  P = (payload[0] & 0x40) == 0x40;

  /* Bit shifting */
  SBIT = (payload[0] & 0x38) >> 3;
  EBIT = (payload[0] & 0x07);

  /* Figure out header length and I-flag */
  if (F == 0) {
    /* F == 0 and P == 0 or 1
     * mode A */
    header_len = GST_RFC2190A_HEADER_LEN;
    GST_LOG ("Mode A");

    /* 0                   1                   2                   3
     * 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
     * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
     * |F|P|SBIT |EBIT | SRC |I|U|S|A|R      |DBQ| TRB |    TR         |
     * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
     */
    I = (payload[1] & 0x10) == 0x10;

  } else {
    if (P == 0) {
      /* F == 1 and P == 0
       * mode B */
      header_len = GST_RFC2190B_HEADER_LEN;
      GST_LOG ("Mode B");

      /* 0                   1                   2                   3
       * 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
       * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
       * |F|P|SBIT |EBIT | SRC | QUANT   |  GOBN   |   MBA           |R  |
       * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
       * |I|U|S|A| HMV1        | VMV1        | HMV2        | VMV2        |
       * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
       */
      I = (payload[4] & 0x80) == 0x80;
    } else {
      /* F == 1 and P == 1
       * mode C */
      header_len = GST_RFC2190C_HEADER_LEN;
      GST_LOG ("Mode C");

      /* 0                   1                   2                   3
       * 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
       * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
       * |F|P|SBIT |EBIT | SRC | QUANT   |  GOBN   |   MBA           |R  |
       * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
       * |I|U|S|A| HMV1        | VMV1        | HMV2        | VMV2        |
       * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
       * | RR                                  |DBQ| TRB |    TR         |
       * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
       */
      I = (payload[4] & 0x80) == 0x80;
    }
  }

  GST_LOG ("F/P/M/I : %d/%d/%d/%d", F, P, M, I);
  GST_LOG ("SBIT : %d , EBIT : %d", SBIT, EBIT);
  GST_LOG ("payload_len : %d, header_len : %d , leftover : 0x%x",
      payload_len, header_len, rtph263depay->leftover);
#if 0
  gst_util_dump_mem (payload, header_len);
#endif

  /* skip header */
  payload += header_len;
  payload_len -= header_len;

  if (SBIT) {
    /* take the leftover and merge it at the beginning */
    GST_LOG ("payload[0] : 0x%x", payload[0]);
    payload[0] &= 0xFF >> SBIT;
    GST_LOG ("payload[0] : 0x%x", payload[0]);
    payload[0] |= rtph263depay->leftover;
    GST_LOG ("payload[0] : 0x%x", payload[0]);
    rtph263depay->leftover = 0;
    rtph263depay->offset = 0;
  }

  if (!EBIT) {
    GstBuffer *tmp = gst_buffer_new_and_alloc (payload_len);

    /* Copy the entire buffer */
    memcpy (GST_BUFFER_DATA (tmp), payload, payload_len);
    gst_adapter_push (rtph263depay->adapter, tmp);
  } else {
    GstBuffer *tmp = gst_buffer_new_and_alloc (payload_len - 1);

    /* Copy the entire buffer except for the last byte */
    memcpy (GST_BUFFER_DATA (tmp), payload, payload_len - 1);
    gst_adapter_push (rtph263depay->adapter, tmp);

    /* Put the last byte into the leftover */
    GST_DEBUG ("payload[payload_len - 1] : 0x%x", payload[payload_len - 1]);
    GST_DEBUG ("mask : 0x%x", 0xFF << EBIT);
    rtph263depay->leftover = (payload[payload_len - 1] >> EBIT) << EBIT;
    rtph263depay->offset = 1;
    GST_DEBUG ("leftover : 0x%x", rtph263depay->leftover);
  }

  if (M) {
    /* frame is completed */
    guint avail;
    guint32 timestamp;

    if (rtph263depay->offset) {
      /* push in the leftover */
      GstBuffer *buf = gst_buffer_new_and_alloc (1);

      GST_DEBUG ("Pushing leftover in adapter");
      GST_BUFFER_DATA (buf)[0] = rtph263depay->leftover;
      gst_adapter_push (rtph263depay->adapter, buf);
    }

    avail = gst_adapter_available (rtph263depay->adapter);
    outbuf = gst_adapter_take_buffer (rtph263depay->adapter, avail);

    if (I)
      GST_BUFFER_FLAG_SET (outbuf, GST_BUFFER_FLAG_DELTA_UNIT);

    GST_DEBUG ("Pushing out a buffer of %d bytes", avail);

    timestamp = gst_rtp_buffer_get_timestamp (buf);
    gst_base_rtp_depayload_push_ts (depayload, timestamp, outbuf);
    rtph263depay->offset = 0;
    rtph263depay->leftover = 0;

  }

  return NULL;

bad_packet:
  {
    GST_ELEMENT_WARNING (rtph263depay, STREAM, DECODE,
        ("Packet did not validate"), (NULL));
    return NULL;
  }
}

static GstStateChangeReturn
gst_rtp_h263_depay_change_state (GstElement * element,
    GstStateChange transition)
{
  GstRtpH263Depay *rtph263depay;
  GstStateChangeReturn ret;

  rtph263depay = GST_RTP_H263_DEPAY (element);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      gst_adapter_clear (rtph263depay->adapter);
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_NULL:
      break;
    default:
      break;
  }
  return ret;
}

gboolean
gst_rtp_h263_depay_plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "rtph263depay",
      GST_RANK_MARGINAL, GST_TYPE_RTP_H263_DEPAY);
}
