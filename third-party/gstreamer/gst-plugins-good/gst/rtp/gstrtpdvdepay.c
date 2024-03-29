/* Farsight
 * Copyright (C) 2006 Marcel Moreaux <marcelm@spacelabs.nl>
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

/*
 * RTP DV depayloader.
 *
 * Important note for NTSC-users:
 *
 * Because the author uses PAL video, and he does not have proper DV
 * documentation (the DV format specification is not freely available),
 * this code may very well contain PAL-specific assumptions.
 */

#include <stdlib.h>
#include <string.h>
#include <gst/gst.h>

#include "gstrtpdvdepay.h"

GST_DEBUG_CATEGORY (rtpdvdepay_debug);
#define GST_CAT_DEFAULT (rtpdvdepay_debug)
/* Filter signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  ARG_0,
};

static GstStaticPadTemplate src_factory = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-dv")
    );

static GstStaticPadTemplate sink_factory = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-rtp, "
        "media = (string) { \"video\", \"audio\" },"
        "payload = (int) " GST_RTP_PAYLOAD_DYNAMIC_STRING ", "
        "encoding-name = (string) \"DV\", "
        "clock-rate = (int) 90000,"
        "encode = (string) { \"SD-VCR/525-60\", \"SD-VCR/625-50\", \"HD-VCR/1125-60\","
        "\"HD-VCR/1250-50\", \"SDL-VCR/525-60\", \"SDL-VCR/625-50\","
        "\"306M/525-60\", \"306M/625-50\", \"314M-25/525-60\","
        "\"314M-25/625-50\", \"314M-50/525-60\", \"314M-50/625-50\" }"
        /* optional parameters can't go in the template
         * "audio = (string) { \"bundled\", \"none\" }"
         */
    )
    );

static GstStateChangeReturn
gst_rtp_dv_depay_change_state (GstElement * element, GstStateChange transition);

static GstBuffer *gst_rtp_dv_depay_process (GstBaseRTPDepayload * base,
    GstBuffer * in);
static gboolean gst_rtp_dv_depay_setcaps (GstBaseRTPDepayload * depayload,
    GstCaps * caps);

GST_BOILERPLATE (GstRTPDVDepay, gst_rtp_dv_depay, GstBaseRTPDepayload,
    GST_TYPE_BASE_RTP_DEPAYLOAD)

     static void gst_rtp_dv_depay_base_init (gpointer g_class)
{
  static GstElementDetails plugin_details = {
    "RTP DV Depayloader",
    "Codec/Depayloader/Network",
    "Depayloads DV from RTP packets (RFC 3189)",
    "Marcel Moreaux <marcelm@spacelabs.nl>, Wim Taymans <wim.taymans@gmail.com>"
  };
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&src_factory));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sink_factory));

  gst_element_class_set_details (element_class, &plugin_details);
}

/* initialize the plugin's class */
static void
gst_rtp_dv_depay_class_init (GstRTPDVDepayClass * klass)
{
  GstElementClass *gstelement_class = (GstElementClass *) klass;
  GstBaseRTPDepayloadClass *gstbasertpdepayload_class =
      (GstBaseRTPDepayloadClass *) klass;
  gstbasertpdepayload_class = (GstBaseRTPDepayloadClass *) klass;

  gstelement_class->change_state =
      GST_DEBUG_FUNCPTR (gst_rtp_dv_depay_change_state);

  gstbasertpdepayload_class->process =
      GST_DEBUG_FUNCPTR (gst_rtp_dv_depay_process);
  gstbasertpdepayload_class->set_caps =
      GST_DEBUG_FUNCPTR (gst_rtp_dv_depay_setcaps);

  GST_DEBUG_CATEGORY_INIT (rtpdvdepay_debug, "rtpdvdepay", 0,
      "DV RTP Depayloader");
}

/* initialize the new element
 * instantiate pads and add them to element
 * set functions
 * initialize structure
 */
static void
gst_rtp_dv_depay_init (GstRTPDVDepay * filter, GstRTPDVDepayClass * klass)
{
}

static gboolean
parse_encode (GstRTPDVDepay * rtpdvdepay, const gchar * encode)
{
  rtpdvdepay->width = 720;
  if (!strcmp (encode, "314M-25/525-60")) {
    rtpdvdepay->frame_size = 240000;
    rtpdvdepay->height = 480;
    rtpdvdepay->rate_num = 30000;
    rtpdvdepay->rate_denom = 1001;
  } else if (!strcmp (encode, "SD-VCR/525-60")) {
    rtpdvdepay->frame_size = 120000;
    rtpdvdepay->height = 480;
    rtpdvdepay->rate_num = 30000;
    rtpdvdepay->rate_denom = 1001;
  } else if (!strcmp (encode, "314M-50/625-50")) {
    rtpdvdepay->frame_size = 288000;
    rtpdvdepay->height = 576;
    rtpdvdepay->rate_num = 25;
    rtpdvdepay->rate_denom = 1;
  } else if (!strcmp (encode, "SD-VCR/625-50")) {
    rtpdvdepay->frame_size = 144000;
    rtpdvdepay->height = 576;
    rtpdvdepay->rate_num = 25;
    rtpdvdepay->rate_denom = 1;
  } else if (!strcmp (encode, "314M-25/625-50")) {
    rtpdvdepay->frame_size = 144000;
    rtpdvdepay->height = 576;
    rtpdvdepay->rate_num = 25;
    rtpdvdepay->rate_denom = 1;
  } else
    rtpdvdepay->frame_size = -1;

  return rtpdvdepay->frame_size != -1;
}

static gboolean
gst_rtp_dv_depay_setcaps (GstBaseRTPDepayload * depayload, GstCaps * caps)
{
  GstStructure *structure;
  GstRTPDVDepay *rtpdvdepay;
  GstCaps *srccaps;
  gint clock_rate = 90000;      /* default */
  gboolean systemstream, ret;
  const gchar *encode, *media;

  rtpdvdepay = GST_RTP_DV_DEPAY (depayload);

  structure = gst_caps_get_structure (caps, 0);

  if (gst_structure_has_field (structure, "clock-rate"))
    gst_structure_get_int (structure, "clock-rate", &clock_rate);

  /* we really need the encode property to figure out the frame size, it's also
   * required by the spec */
  if (!(encode = gst_structure_get_string (structure, "encode")))
    goto no_encode;

  /* figure out the size of one frame */
  if (!parse_encode (rtpdvdepay, encode))
    goto unknown_encode;

  /* check the media, this tells us that the stream has video or not */
  if (!(media = gst_structure_get_string (structure, "media")))
    goto no_media;

  systemstream = FALSE;

  if (!strcmp (media, "audio")) {
    /* we need a demuxer for audio only */
    systemstream = TRUE;
  } else if (!strcmp (media, "video")) {
    const gchar *audio;

    /* check the optional audio field, if it's present and set to bundled, we
     * are dealing with a system stream. */
    if ((audio = gst_structure_get_string (structure, "audio"))) {
      if (!strcmp (audio, "bundled"))
        systemstream = TRUE;
    }
  }

  /* allocate accumulator */
  rtpdvdepay->acc = gst_buffer_new_and_alloc (rtpdvdepay->frame_size);

  /* Initialize the new accumulator frame.
   * If the previous frame exists, copy that into the accumulator frame.
   * This way, missing packets in the stream won't show up badly. */
  memset (GST_BUFFER_DATA (rtpdvdepay->acc), 0, rtpdvdepay->frame_size);

  srccaps = gst_caps_new_simple ("video/x-dv",
      "systemstream", G_TYPE_BOOLEAN, systemstream,
      "width", G_TYPE_INT, rtpdvdepay->width,
      "height", G_TYPE_INT, rtpdvdepay->height,
      "framerate", GST_TYPE_FRACTION, rtpdvdepay->rate_num,
      rtpdvdepay->rate_denom, NULL);
  ret = gst_pad_set_caps (depayload->srcpad, srccaps);
  gst_caps_unref (srccaps);

  depayload->clock_rate = clock_rate;
  rtpdvdepay->negotiated = TRUE;

  return ret;

  /* ERRORS */
no_encode:
  {
    GST_ERROR_OBJECT (rtpdvdepay, "required encode property not found in caps");
    return FALSE;
  }
unknown_encode:
  {
    GST_ERROR_OBJECT (rtpdvdepay, "unknown encode property %s found", encode);
    return FALSE;
  }
no_media:
  {
    GST_ERROR_OBJECT (rtpdvdepay, "required media property not found in caps");
    return FALSE;
  }
}

/* A DV frame consists of a bunch of 80-byte DIF blocks.
 * Each DIF block contains a 3-byte header telling where in the DV frame the
 * DIF block should go. We use this information to calculate its position.
 */
static guint
calculate_difblock_location (guint8 * block)
{
  gint block_type, dif_sequence, dif_block;
  guint location;

  block_type = block[0] >> 5;
  dif_sequence = block[1] >> 4;
  dif_block = block[2];

  switch (block_type) {
    case 0:                    /* Header block */
      location = dif_sequence * 150 * 80;
      break;
    case 1:                    /* Subcode block */
      location = dif_sequence * 150 * 80 + (1 + dif_block) * 80;
      break;
    case 2:                    /* VAUX block */
      location = dif_sequence * 150 * 80 + (3 + dif_block) * 80;
      break;
    case 3:                    /* Audio block */
      location = dif_sequence * 150 * 80 + (6 + dif_block * 16) * 80;
      break;
    case 4:                    /* Video block */
      location = dif_sequence * 150 * 80 +
          (7 + (dif_block / 15) + dif_block) * 80;
      break;
    default:                   /* Something bogus */
      GST_DEBUG ("UNKNOWN BLOCK");
      location = -1;
      break;
  }
  return location;
}

/* Process one RTP packet. Accumulate RTP payload in the proper place in a DV
 * frame, and return that frame if we detect a new frame, or NULL otherwise.
 * We assume a DV frame is 144000 bytes. That should accomodate PAL as well as
 * NTSC.
 */
static GstBuffer *
gst_rtp_dv_depay_process (GstBaseRTPDepayload * base, GstBuffer * in)
{
  GstBuffer *out = NULL;
  guint8 *payload;
  guint32 rtp_ts;
  guint payload_len, location;
  GstRTPDVDepay *dvdepay = GST_RTP_DV_DEPAY (base);
  gboolean marker;

  if (!dvdepay->negotiated)
    goto not_negotiated;

  marker = gst_rtp_buffer_get_marker (in);

  /* Check if the received packet contains (the start of) a new frame, we do
   * this by checking the RTP timestamp. */
  rtp_ts = gst_rtp_buffer_get_timestamp (in);

  /* we cannot copy the packet yet if the marker is set, we will do that below
   * after taking out the data */
  if (dvdepay->prev_ts != -1 && rtp_ts != dvdepay->prev_ts && !marker) {
    /* the timestamp changed */
    GST_DEBUG_OBJECT (dvdepay, "new frame with ts %u, old ts %u", rtp_ts,
        dvdepay->prev_ts);

    /* return copy of accumulator. */
    out = gst_buffer_copy (dvdepay->acc);
  }

  /* Extract the payload */
  payload_len = gst_rtp_buffer_get_payload_len (in);
  payload = gst_rtp_buffer_get_payload (in);

  /* copy all DIF chunks in their place. */
  while (payload_len >= 80) {
    /* Calculate where in the frame the payload should go */
    location = calculate_difblock_location (payload);

    /* Check if we received a header. We will not pass on frames until
     * we've received a header, otherwise the DV decoder goes wacko. */
    if (location == 0)
      dvdepay->have_header = TRUE;

    /* And copy it in, provided the location is sane. */
    if (location >= 0 && location <= dvdepay->frame_size - 80)
      memcpy (GST_BUFFER_DATA (dvdepay->acc) + location, payload, 80);

    payload += 80;
    payload_len -= 80;
  }

  if (marker) {
    /* The marker marks the end of a frame that we need to push. The next frame
     * will change the timestamp but we won't copy the accumulator again because
     * we set the prev_ts to -1. */
    out = gst_buffer_copy (dvdepay->acc);
    GST_DEBUG_OBJECT (dvdepay, "marker bit complete frame %u", rtp_ts);
    dvdepay->prev_ts = -1;
  } else {
    /* save last timestamp */
    dvdepay->prev_ts = rtp_ts;
  }
  return out;

  /* ERRORS */
not_negotiated:
  {
    GST_ELEMENT_ERROR (dvdepay, STREAM, NOT_IMPLEMENTED,
        (NULL), ("not negotiated"));
    return NULL;
  }
}

static void
gst_rtp_dv_depay_reset (GstRTPDVDepay * depay)
{
  if (depay->acc)
    gst_buffer_unref (depay->acc);
  depay->acc = NULL;

  depay->prev_ts = -1;
  depay->have_header = FALSE;
  depay->frame_nr = 0;
  depay->negotiated = FALSE;
}

static GstStateChangeReturn
gst_rtp_dv_depay_change_state (GstElement * element, GstStateChange transition)
{
  GstStateChangeReturn ret;
  GstRTPDVDepay *depay = GST_RTP_DV_DEPAY (element);

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      gst_rtp_dv_depay_reset (depay);
      break;
    default:
      break;
  }

  ret = GST_CALL_PARENT_WITH_DEFAULT (GST_ELEMENT_CLASS, change_state,
      (element, transition), GST_STATE_CHANGE_FAILURE);

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      gst_rtp_dv_depay_reset (depay);
      break;
    default:
      break;
  }
  return ret;
}

gboolean
gst_rtp_dv_depay_plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "rtpdvdepay",
      GST_RANK_NONE, GST_TYPE_RTP_DV_DEPAY);
}
