/* GStreamer
 * Copyright (C) <2006> Wim Taymans <wim@fluendo.com>
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

#include "fnv1hash.h"
#include "gstrtptheorapay.h"

#define THEORA_ID_LEN	42

GST_DEBUG_CATEGORY_STATIC (rtptheorapay_debug);
#define GST_CAT_DEFAULT (rtptheorapay_debug)

/* references:
 * http://svn.xiph.org/trunk/theora/doc/draft-ietf-avt-rtp-theora-01.txt
 */

/* elementfactory information */
static const GstElementDetails gst_rtp_theorapay_details =
GST_ELEMENT_DETAILS ("RTP packet depayloader",
    "Codec/Payloader/Network",
    "Payload-encode Theora video into RTP packets (draft-01 RFC XXXX)",
    "Wim Taymans <wim@fluendo.com>");

static GstStaticPadTemplate gst_rtp_theora_pay_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-rtp, "
        "media = (string) \"video\", "
        "payload = (int) " GST_RTP_PAYLOAD_DYNAMIC_STRING ", "
        "clock-rate = (int) 90000, " "encoding-name = (string) \"THEORA\", "
        "delivery-method = (string) \"inline\""
        /* All required parameters
         *
         * "sampling = (string) { "YCbCr-4:2:0", "YCbCr-4:2:2", "YCbCr-4:4:4" } "
         * "width = (string) [1, 1048561] (multiples of 16) "
         * "height = (string) [1, 1048561] (multiples of 16) "
         * "delivery-method = (string) { inline, in_band, out_band/<specific_name> } "
         * "configuration = (string) ANY"
         */
        /* All optional parameters
         *
         * "configuration-uri ="
         */
    )
    );

static GstStaticPadTemplate gst_rtp_theora_pay_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-theora")
    );

GST_BOILERPLATE (GstRtpTheoraPay, gst_rtp_theora_pay, GstBaseRTPPayload,
    GST_TYPE_BASE_RTP_PAYLOAD);

static gboolean gst_rtp_theora_pay_setcaps (GstBaseRTPPayload * basepayload,
    GstCaps * caps);
static GstStateChangeReturn gst_rtp_theora_pay_change_state (GstElement *
    element, GstStateChange transition);
static GstFlowReturn gst_rtp_theora_pay_handle_buffer (GstBaseRTPPayload * pad,
    GstBuffer * buffer);

static void
gst_rtp_theora_pay_base_init (gpointer klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_rtp_theora_pay_src_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_rtp_theora_pay_sink_template));

  gst_element_class_set_details (element_class, &gst_rtp_theorapay_details);
}

static void
gst_rtp_theora_pay_class_init (GstRtpTheoraPayClass * klass)
{
  GObjectClass *gobject_class;

  GstElementClass *gstelement_class;

  GstBaseRTPPayloadClass *gstbasertppayload_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;
  gstbasertppayload_class = (GstBaseRTPPayloadClass *) klass;

  parent_class = g_type_class_peek_parent (klass);

  gstelement_class->change_state = gst_rtp_theora_pay_change_state;

  gstbasertppayload_class->set_caps = gst_rtp_theora_pay_setcaps;
  gstbasertppayload_class->handle_buffer = gst_rtp_theora_pay_handle_buffer;

  GST_DEBUG_CATEGORY_INIT (rtptheorapay_debug, "rtptheorapay", 0,
      "Theora RTP Payloader");
}

static void
gst_rtp_theora_pay_init (GstRtpTheoraPay * rtptheorapay,
    GstRtpTheoraPayClass * klass)
{
  /* needed because of GST_BOILERPLATE */
}

static void
gst_rtp_theora_pay_cleanup (GstRtpTheoraPay * rtptheorapay)
{
  g_list_foreach (rtptheorapay->headers, (GFunc) gst_mini_object_unref, NULL);
  g_list_free (rtptheorapay->headers);
  rtptheorapay->headers = NULL;

  if (rtptheorapay->packet)
    gst_buffer_unref (rtptheorapay->packet);
  rtptheorapay->packet = NULL;
}

static gboolean
gst_rtp_theora_pay_setcaps (GstBaseRTPPayload * basepayload, GstCaps * caps)
{
  GstRtpTheoraPay *rtptheorapay;

  rtptheorapay = GST_RTP_THEORA_PAY (basepayload);

  rtptheorapay->need_headers = TRUE;

  return TRUE;
}

static void
gst_rtp_theora_pay_reset_packet (GstRtpTheoraPay * rtptheorapay, guint8 TDT)
{
  guint payload_len;

  GST_DEBUG_OBJECT (rtptheorapay, "reset packet");

  rtptheorapay->payload_pos = 4;
  payload_len = gst_rtp_buffer_get_payload_len (rtptheorapay->packet);
  rtptheorapay->payload_left = payload_len - 4;
  rtptheorapay->payload_duration = 0;
  rtptheorapay->payload_F = 0;
  rtptheorapay->payload_TDT = TDT;
  rtptheorapay->payload_pkts = 0;
}

static void
gst_rtp_theora_pay_init_packet (GstRtpTheoraPay * rtptheorapay, guint8 TDT,
    GstClockTime timestamp)
{
  GST_DEBUG_OBJECT (rtptheorapay, "starting new packet, TDT: %d", TDT);

  if (rtptheorapay->packet)
    gst_buffer_unref (rtptheorapay->packet);

  /* new packet allocate max packet size */
  rtptheorapay->packet =
      gst_rtp_buffer_new_allocate_len (GST_BASE_RTP_PAYLOAD_MTU
      (rtptheorapay), 0, 0);
  gst_rtp_theora_pay_reset_packet (rtptheorapay, TDT);

  GST_BUFFER_TIMESTAMP (rtptheorapay->packet) = timestamp;
}

static GstFlowReturn
gst_rtp_theora_pay_flush_packet (GstRtpTheoraPay * rtptheorapay)
{
  GstFlowReturn ret;

  guint8 *payload;

  guint hlen;

  /* check for empty packet */
  if (!rtptheorapay->packet || rtptheorapay->payload_pos <= 4)
    return GST_FLOW_OK;

  GST_DEBUG_OBJECT (rtptheorapay, "flushing packet");

  /* fix header */
  payload = gst_rtp_buffer_get_payload (rtptheorapay->packet);
  /*
   *  0                   1                   2                   3
   *  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
   * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   * |                     Ident                     | F |TDT|# pkts.|
   * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   *
   * F: Fragment type (0=none, 1=start, 2=cont, 3=end)
   * TDT: Theora data type (0=theora, 1=config, 2=comment, 3=reserved)
   * pkts: number of packets.
   */
  payload[0] = (rtptheorapay->payload_ident >> 16) & 0xff;
  payload[1] = (rtptheorapay->payload_ident >> 8) & 0xff;
  payload[2] = (rtptheorapay->payload_ident) & 0xff;
  payload[3] = (rtptheorapay->payload_F & 0x3) << 6 |
      (rtptheorapay->payload_TDT & 0x3) << 4 |
      (rtptheorapay->payload_pkts & 0xf);

  /* shrink the buffer size to the last written byte */
  hlen = gst_rtp_buffer_calc_header_len (0);
  GST_BUFFER_SIZE (rtptheorapay->packet) = hlen + rtptheorapay->payload_pos;

  GST_BUFFER_DURATION (rtptheorapay->packet) = rtptheorapay->payload_duration;

  /* push, this gives away our ref to the packet, so clear it. */
  ret =
      gst_basertppayload_push (GST_BASE_RTP_PAYLOAD (rtptheorapay),
      rtptheorapay->packet);
  rtptheorapay->packet = NULL;

  return ret;
}

static gchar *
encode_base64 (const guint8 * in, guint size, guint * len)
{
  gchar *ret, *d;

  static const gchar *v =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

  *len = ((size + 2) / 3) * 4;
  d = ret = (gchar *) g_malloc (*len + 1);
  for (; size; in += 3) {       /* process tuplets */
    *d++ = v[in[0] >> 2];       /* byte 1: high 6 bits (1) */
    /* byte 2: low 2 bits (1), high 4 bits (2) */
    *d++ = v[((in[0] << 4) + (--size ? (in[1] >> 4) : 0)) & 0x3f];
    /* byte 3: low 4 bits (2), high 2 bits (3) */
    *d++ = size ? v[((in[1] << 2) + (--size ? (in[2] >> 6) : 0)) & 0x3f] : '=';
    /* byte 4: low 6 bits (3) */
    *d++ = size ? v[in[2] & 0x3f] : '=';
    if (size)
      size--;                   /* count third character if processed */
  }
  *d = '\0';                    /* tie off string */

  return ret;                   /* return the resulting string */
}

static gboolean
gst_rtp_theora_pay_finish_headers (GstBaseRTPPayload * basepayload)
{
  GstRtpTheoraPay *rtptheorapay = GST_RTP_THEORA_PAY (basepayload);

  GList *walk;

  guint length, size, n_headers, configlen;

  gchar *wstr, *hstr, *configuration;

  guint8 *data, *config;

  guint32 ident;

  GST_DEBUG_OBJECT (rtptheorapay, "finish headers");

  if (!rtptheorapay->headers) {
    GST_DEBUG_OBJECT (rtptheorapay, "We need 2 headers but have none");
    goto no_headers;
  }

  /* +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   * |                     Number of packed headers                  |
   * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   * |                          Packed header                        |
   * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   * |                          Packed header                        |
   * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   * |                          ....                                 |
   * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   *
   * We only construct a config containing 1 packed header like this:
   *
   *  0                   1                   2                   3
   *  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
   * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   * |                   Ident                       | length       ..
   * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   * ..              | n. of headers |    length1    |    length2   ..
   * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   * ..              |             Identification Header            ..
   * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   * .................................................................
   * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   * ..              |         Comment Header                       ..
   * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   * .................................................................
   * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   * ..                        Comment Header                        |
   * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   * |                          Setup Header                        ..
   * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   * .................................................................
   * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   * ..                         Setup Header                         |
   * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   */

  /* we need 4 bytes for the number of headers (which is always 1), 3 bytes for
   * the ident, 2 bytes for length, 1 byte for n. of headers. */
  size = 4 + 3 + 2 + 1;

  /* count the size of the headers first and update the hash */
  length = 0;
  n_headers = 0;
  ident = fnv1_hash_32_new ();
  for (walk = rtptheorapay->headers; walk; walk = g_list_next (walk)) {
    GstBuffer *buf = GST_BUFFER_CAST (walk->data);

    guint bsize;

    bsize = GST_BUFFER_SIZE (buf);
    length += bsize;
    n_headers++;

    /* count number of bytes needed for length fields, we don't need this for
     * the last header. */
    if (g_list_next (walk)) {
      do {
        size++;
        bsize >>= 7;
      } while (bsize);
    }
    /* update hash */
    ident = fnv1_hash_32_update (ident, GST_BUFFER_DATA (buf),
        GST_BUFFER_SIZE (buf));
  }

  /* packet length is header size + packet length */
  configlen = size + length;
  config = data = g_malloc (configlen);

  /* number of packed headers, we only pack 1 header */
  data[0] = 0;
  data[1] = 0;
  data[2] = 0;
  data[3] = 1;

  ident = fnv1_hash_32_to_24 (ident);
  rtptheorapay->payload_ident = ident;
  GST_DEBUG_OBJECT (rtptheorapay, "ident 0x%08x", ident);

  /* take lower 3 bytes */
  data[4] = (ident >> 16) & 0xff;
  data[5] = (ident >> 8) & 0xff;
  data[6] = ident & 0xff;

  /* store length of all theora headers */
  data[7] = ((length) >> 8) & 0xff;
  data[8] = (length) & 0xff;

  /* store number of headers minus one. */
  data[9] = n_headers - 1;
  data += 10;

  /* store length for each header */
  for (walk = rtptheorapay->headers; walk; walk = g_list_next (walk)) {
    GstBuffer *buf = GST_BUFFER_CAST (walk->data);

    guint bsize, size, temp;

    /* only need to store the length when it's not the last header */
    if (!g_list_next (walk))
      break;

    bsize = GST_BUFFER_SIZE (buf);

    /* calc size */
    size = 0;
    do {
      size++;
      bsize >>= 7;
    } while (bsize);
    temp = size;

    bsize = GST_BUFFER_SIZE (buf);
    /* write the size backwards */
    while (size) {
      size--;
      data[size] = bsize & 0x7f;
      bsize >>= 7;
    }
    data += temp;
  }

  /* copy header data */
  for (walk = rtptheorapay->headers; walk; walk = g_list_next (walk)) {
    GstBuffer *buf = GST_BUFFER_CAST (walk->data);

    memcpy (data, GST_BUFFER_DATA (buf), GST_BUFFER_SIZE (buf));
    data += GST_BUFFER_SIZE (buf);
  }

  /* serialize to base64 */
  configuration = encode_base64 (config, configlen, &size);
  g_free (config);

  /* configure payloader settings */
  wstr = g_strdup_printf ("%d", rtptheorapay->width);
  hstr = g_strdup_printf ("%d", rtptheorapay->height);
  gst_basertppayload_set_options (basepayload, "video", TRUE, "THEORA", 90000);
  gst_basertppayload_set_outcaps (basepayload,
      "sampling", G_TYPE_STRING, "YCbCr-4:2:0",
      "width", G_TYPE_STRING, wstr,
      "height", G_TYPE_STRING, hstr,
      "configuration", G_TYPE_STRING, configuration,
      "delivery-method", G_TYPE_STRING, "inline",
      /* don't set the other defaults 
       */
      NULL);
  g_free (wstr);
  g_free (hstr);
  g_free (configuration);

  return TRUE;

  /* ERRORS */
no_headers:
  {
    GST_DEBUG_OBJECT (rtptheorapay, "finish headers");
    return FALSE;
  }
}

static gboolean
gst_rtp_theora_pay_parse_id (GstBaseRTPPayload * basepayload, guint8 * data,
    guint size)
{
  GstRtpTheoraPay *rtptheorapay;

  gint width, height;

  rtptheorapay = GST_RTP_THEORA_PAY (basepayload);

  if (G_UNLIKELY (size < 42))
    goto too_short;

  if (G_UNLIKELY (memcmp (data, "\200theora", 7)))
    goto invalid_start;
  data += 7;

  if (G_UNLIKELY (data[0] != 3))
    goto invalid_version;
  if (G_UNLIKELY (data[1] != 2))
    goto invalid_version;
  data += 3;

  width = GST_READ_UINT16_BE (data) << 4;
  data += 2;
  height = GST_READ_UINT16_BE (data) << 4;
  data += 2;

  /* FIXME, parse pixel format */

  /* store values */
  rtptheorapay->width = width;
  rtptheorapay->height = height;

  return TRUE;

  /* ERRORS */
too_short:
  {
    GST_ELEMENT_ERROR (basepayload, STREAM, DECODE,
        (NULL),
        ("Identification packet is too short, need at least 42, got %d", size));
    return FALSE;
  }
invalid_start:
  {
    GST_ELEMENT_ERROR (basepayload, STREAM, DECODE,
        (NULL), ("Invalid header start in identification packet"));
    return FALSE;
  }
invalid_version:
  {
    GST_ELEMENT_ERROR (basepayload, STREAM, DECODE,
        (NULL), ("Invalid version"));
    return FALSE;
  }
}

static GstFlowReturn
gst_rtp_theora_pay_handle_buffer (GstBaseRTPPayload * basepayload,
    GstBuffer * buffer)
{
  GstRtpTheoraPay *rtptheorapay;

  GstFlowReturn ret;

  guint size, newsize;

  guint8 *data;

  guint packet_len;

  GstClockTime duration, newduration, timestamp;

  gboolean flush;

  guint8 TDT;

  guint plen;

  guint8 *ppos, *payload;

  gboolean fragmented;

  rtptheorapay = GST_RTP_THEORA_PAY (basepayload);

  size = GST_BUFFER_SIZE (buffer);
  data = GST_BUFFER_DATA (buffer);
  duration = GST_BUFFER_DURATION (buffer);
  timestamp = GST_BUFFER_TIMESTAMP (buffer);

  GST_DEBUG_OBJECT (rtptheorapay, "size %u, duration %" GST_TIME_FORMAT,
      size, GST_TIME_ARGS (duration));

  if (G_UNLIKELY (size < 1 || size > 0xffff))
    goto wrong_size;

  /* find packet type */
  if (data[0] & 0x80) {
    /* header */
    if (data[0] == 0x80) {
      /* identification, we need to parse this in order to get the clock rate.
       */
      if (G_UNLIKELY (!gst_rtp_theora_pay_parse_id (basepayload, data, size)))
        goto parse_id_failed;
      TDT = 1;
    } else if (data[0] == 0x81) {
      /* comment */
      TDT = 2;
    } else if (data[0] == 0x82) {
      /* setup */
      TDT = 1;
    } else
      goto unknown_header;
  } else
    /* data */
    TDT = 0;

  if (rtptheorapay->need_headers) {
    /* we need to collect the headers and construct a config string from them */
    if (TDT != 0) {
      GST_DEBUG_OBJECT (rtptheorapay, "collecting header, buffer %p", buffer);
      /* append header to the list of headers */
      rtptheorapay->headers = g_list_append (rtptheorapay->headers, buffer);
      ret = GST_FLOW_OK;
      goto done;
    } else {
      if (!gst_rtp_theora_pay_finish_headers (basepayload))
        goto header_error;
      rtptheorapay->need_headers = FALSE;
    }
  }

  /* size increases with packet length and 2 bytes size eader. */
  newduration = rtptheorapay->payload_duration;
  if (duration != GST_CLOCK_TIME_NONE)
    newduration += duration;

  newsize = rtptheorapay->payload_pos + 2 + size;
  packet_len = gst_rtp_buffer_calc_packet_len (newsize, 0, 0);

  /* check buffer filled against length and max latency */
  flush = gst_basertppayload_is_filled (basepayload, packet_len, newduration);
  /* we can store up to 15 theora packets in one RTP packet. */
  flush |= (rtptheorapay->payload_pkts == 15);
  /* flush if we have a new TDT */
  if (rtptheorapay->packet)
    flush |= (rtptheorapay->payload_TDT != TDT);
  if (flush)
    ret = gst_rtp_theora_pay_flush_packet (rtptheorapay);

  /* create new packet if we must */
  if (!rtptheorapay->packet) {
    gst_rtp_theora_pay_init_packet (rtptheorapay, TDT, timestamp);
  }

  payload = gst_rtp_buffer_get_payload (rtptheorapay->packet);
  ppos = payload + rtptheorapay->payload_pos;
  fragmented = FALSE;

  ret = GST_FLOW_OK;

  /* put buffer in packet, it either fits completely or needs to be fragmented
   * over multiple RTP packets. */
  while (size) {
    plen = MIN (rtptheorapay->payload_left - 2, size);

    GST_DEBUG_OBJECT (rtptheorapay, "append %u bytes", plen);

    /* data is copied in the payload with a 2 byte length header */
    ppos[0] = (plen >> 8) & 0xff;
    ppos[1] = (plen & 0xff);
    memcpy (&ppos[2], data, plen);

    size -= plen;
    data += plen;

    rtptheorapay->payload_pos += plen + 2;
    rtptheorapay->payload_left -= plen + 2;

    if (fragmented) {
      if (size == 0)
        /* last fragment, set F to 0x3. */
        rtptheorapay->payload_F = 0x3;
      else
        /* fragment continues, set F to 0x2. */
        rtptheorapay->payload_F = 0x2;
    } else {
      if (size > 0) {
        /* fragmented packet starts, set F to 0x1, mark ourselves as
         * fragmented. */
        rtptheorapay->payload_F = 0x1;
        fragmented = TRUE;
      }
    }
    if (fragmented) {
      /* fragmented packets are always flushed and have ptks of 0 */
      rtptheorapay->payload_pkts = 0;
      ret = gst_rtp_theora_pay_flush_packet (rtptheorapay);

      if (size > 0) {
        /* start new packet and get pointers. TDT stays the same. */
        gst_rtp_theora_pay_init_packet (rtptheorapay,
            rtptheorapay->payload_TDT, timestamp);
        payload = gst_rtp_buffer_get_payload (rtptheorapay->packet);
        ppos = payload + rtptheorapay->payload_pos;
      }
    } else {
      /* unfragmented packet, update stats for next packet, size == 0 and we
       * exit the while loop */
      rtptheorapay->payload_pkts++;
      if (duration != GST_CLOCK_TIME_NONE)
        rtptheorapay->payload_duration += duration;
    }
  }
  gst_buffer_unref (buffer);

done:
  return ret;

  /* ERRORS */
wrong_size:
  {
    GST_ELEMENT_WARNING (rtptheorapay, STREAM, DECODE,
        ("Invalid packet size (1 < %d <= 0xffff)", size), (NULL));
    gst_buffer_unref (buffer);
    return GST_FLOW_OK;
  }
parse_id_failed:
  {
    gst_buffer_unref (buffer);
    return GST_FLOW_ERROR;
  }
unknown_header:
  {
    GST_ELEMENT_WARNING (rtptheorapay, STREAM, DECODE,
        (NULL), ("Ignoring unknown header received"));
    gst_buffer_unref (buffer);
    return GST_FLOW_OK;
  }
header_error:
  {
    GST_ELEMENT_WARNING (rtptheorapay, STREAM, DECODE,
        (NULL), ("Error initializing header config"));
    gst_buffer_unref (buffer);
    return GST_FLOW_OK;
  }
}

static GstStateChangeReturn
gst_rtp_theora_pay_change_state (GstElement * element,
    GstStateChange transition)
{
  GstRtpTheoraPay *rtptheorapay;

  GstStateChangeReturn ret;

  rtptheorapay = GST_RTP_THEORA_PAY (element);

  switch (transition) {
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      gst_rtp_theora_pay_cleanup (rtptheorapay);
      break;
    default:
      break;
  }
  return ret;
}

gboolean
gst_rtp_theora_pay_plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "rtptheorapay",
      GST_RANK_NONE, GST_TYPE_RTP_THEORA_PAY);
}
