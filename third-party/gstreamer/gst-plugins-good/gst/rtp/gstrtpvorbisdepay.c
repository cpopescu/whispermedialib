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

#include <gst/tag/tag.h>
#include <gst/rtp/gstrtpbuffer.h>

#include <string.h>
#include "gstrtpvorbisdepay.h"

GST_DEBUG_CATEGORY_STATIC (rtpvorbisdepay_debug);
#define GST_CAT_DEFAULT (rtpvorbisdepay_debug)

/* references:
 * http://svn.xiph.org/trunk/vorbis/doc/draft-ietf-avt-rtp-vorbis-04.txt
 */

/* elementfactory information */
static const GstElementDetails gst_rtp_vorbis_depay_details =
GST_ELEMENT_DETAILS ("RTP packet depayloader",
    "Codec/Depayloader/Network",
    "Extracts Vorbis Audio from RTP packets (draft-04 of RFC XXXX)",
    "Wim Taymans <wim@fluendo.com>");

static GstStaticPadTemplate gst_rtp_vorbis_depay_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-rtp, "
        "media = (string) \"audio\", "
        "payload = (int) " GST_RTP_PAYLOAD_DYNAMIC_STRING ", "
        "clock-rate = (int) [1, MAX ], " "encoding-name = (string) \"VORBIS\""
        /* All required parameters 
         *
         * "encoding-params = (string) <num channels>"
         * "configuration = (string) ANY" 
         */
        /* All optional parameters
         *
         * "delivery-method = (string) { inline, in_band, out_band/<specific_name> } " 
         * "configuration-uri =" 
         */
    )
    );

static GstStaticPadTemplate gst_rtp_vorbis_depay_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-vorbis")
    );

GST_BOILERPLATE (GstRtpVorbisDepay, gst_rtp_vorbis_depay, GstBaseRTPDepayload,
    GST_TYPE_BASE_RTP_DEPAYLOAD);

static gboolean gst_rtp_vorbis_depay_setcaps (GstBaseRTPDepayload * depayload,
    GstCaps * caps);
static GstBuffer *gst_rtp_vorbis_depay_process (GstBaseRTPDepayload * depayload,
    GstBuffer * buf);

static void gst_rtp_vorbis_depay_finalize (GObject * object);

static GstStateChangeReturn gst_rtp_vorbis_depay_change_state (GstElement *
    element, GstStateChange transition);


static void
gst_rtp_vorbis_depay_base_init (gpointer klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_rtp_vorbis_depay_sink_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_rtp_vorbis_depay_src_template));

  gst_element_class_set_details (element_class, &gst_rtp_vorbis_depay_details);
}

static void
gst_rtp_vorbis_depay_class_init (GstRtpVorbisDepayClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstBaseRTPDepayloadClass *gstbasertpdepayload_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;
  gstbasertpdepayload_class = (GstBaseRTPDepayloadClass *) klass;

  gobject_class->finalize = gst_rtp_vorbis_depay_finalize;

  gstelement_class->change_state = gst_rtp_vorbis_depay_change_state;

  gstbasertpdepayload_class->process = gst_rtp_vorbis_depay_process;
  gstbasertpdepayload_class->set_caps = gst_rtp_vorbis_depay_setcaps;

  GST_DEBUG_CATEGORY_INIT (rtpvorbisdepay_debug, "rtpvorbisdepay", 0,
      "Vorbis RTP Depayloader");
}

static void
gst_rtp_vorbis_depay_init (GstRtpVorbisDepay * rtpvorbisdepay,
    GstRtpVorbisDepayClass * klass)
{
  rtpvorbisdepay->adapter = gst_adapter_new ();
}
static void
gst_rtp_vorbis_depay_finalize (GObject * object)
{
  GstRtpVorbisDepay *rtpvorbisdepay = GST_RTP_VORBIS_DEPAY (object);

  g_object_unref (rtpvorbisdepay->adapter);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static const guint8 a2bin[256] = {
  64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
  64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
  64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 62, 64, 64, 64, 63,
  52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 64, 64, 64, 64, 64, 64,
  64, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14,
  15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 64, 64, 64, 64, 64,
  64, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40,
  41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 64, 64, 64, 64, 64,
  64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
  64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
  64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
  64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
  64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
  64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
  64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
  64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64
};

static guint
decode_base64 (const gchar * in, guint8 * out)
{
  guint8 v1, v2;
  guint len = 0;

  v1 = a2bin[(gint) * in];
  while (v1 <= 63) {
    /* read 4 bytes, write 3 bytes, invalid base64 are zeroes */
    v2 = a2bin[(gint) * ++in];
    *out++ = (v1 << 2) | ((v2 & 0x3f) >> 4);
    v1 = (v2 > 63 ? 64 : a2bin[(gint) * ++in]);
    *out++ = (v2 << 4) | ((v1 & 0x3f) >> 2);
    v2 = (v1 > 63 ? 64 : a2bin[(gint) * ++in]);
    *out++ = (v1 << 6) | (v2 & 0x3f);
    v1 = (v2 > 63 ? 64 : a2bin[(gint) * ++in]);
    len += 3;
  }
  /* move to '\0' */
  while (*in != '\0')
    in++;

  /* subtract padding */
  while (len > 0 && *--in == '=')
    len--;

  return len;
}

static gboolean
gst_rtp_vorbis_depay_parse_configuration (GstRtpVorbisDepay * rtpvorbisdepay,
    const gchar * configuration)
{
  GstBuffer *buf;
  guint32 num_headers;
  guint8 *data;
  guint size;
  gint i, j;

  /* deserialize base64 to buffer */
  size = strlen (configuration);
  GST_DEBUG_OBJECT (rtpvorbisdepay, "base64 config size %u", size);

  data = g_malloc (size);
  size = decode_base64 (configuration, data);

  GST_DEBUG_OBJECT (rtpvorbisdepay, "config size %u", size);

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
   */
  if (size < 4)
    goto too_small;

  num_headers = GST_READ_UINT32_BE (data);
  size -= 4;
  data += 4;

  GST_DEBUG_OBJECT (rtpvorbisdepay, "have %u headers", num_headers);

  /*  0                   1                   2                   3
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
  for (i = 0; i < num_headers; i++) {
    guint32 ident;
    guint16 length;
    guint8 n_headers, b;
    GstRtpVorbisConfig *conf;
    guint *h_sizes;

    if (size < 6)
      goto too_small;

    ident = (data[0] << 16) | (data[1] << 8) | data[2];
    length = (data[3] << 8) | data[4];
    n_headers = data[5];
    size -= 6;
    data += 6;

    GST_DEBUG_OBJECT (rtpvorbisdepay,
        "header %d, ident 0x%08x, length %u, left %u", i, ident, length, size);

    if (size < length)
      goto too_small;

    /* read header sizes we read 2 sizes, the third size (for which we allocate
     * space) must be derived from the total packed header length. */
    h_sizes = g_newa (guint, n_headers + 1);
    for (j = 0; j < n_headers; j++) {
      guint h_size;

      h_size = 0;
      do {
        if (size < 1)
          goto too_small;
        b = *data++;
        size--;
        h_size = (h_size << 7) | (b & 0x7f);
      } while (b & 0x80);
      GST_DEBUG_OBJECT (rtpvorbisdepay, "headers %d: size: %u", j, h_size);
      h_sizes[j] = h_size;
      length -= h_size;
    }
    /* last header length is the remaining space */
    GST_DEBUG_OBJECT (rtpvorbisdepay, "last header size: %u", length);
    h_sizes[j] = length;

    GST_DEBUG_OBJECT (rtpvorbisdepay, "preparing headers");
    conf = g_new0 (GstRtpVorbisConfig, 1);
    conf->ident = ident;

    for (j = 0; j <= n_headers; j++) {
      guint h_size;

      h_size = h_sizes[j];
      if (size < h_size)
        goto too_small;

      GST_DEBUG_OBJECT (rtpvorbisdepay, "reading header %d, size %u", j,
          h_size);

      buf = gst_buffer_new_and_alloc (h_size);
      memcpy (GST_BUFFER_DATA (buf), data, h_size);
      conf->headers = g_list_append (conf->headers, buf);
      data += h_size;
      size -= h_size;
    }
    rtpvorbisdepay->configs = g_list_append (rtpvorbisdepay->configs, conf);
  }

  return TRUE;

  /* ERRORS */
too_small:
  {
    GST_DEBUG_OBJECT (rtpvorbisdepay, "configuration too small");
    return FALSE;
  }
}

static gboolean
gst_rtp_vorbis_depay_setcaps (GstBaseRTPDepayload * depayload, GstCaps * caps)
{
  GstStructure *structure;
  GstRtpVorbisDepay *rtpvorbisdepay;
  GstCaps *srccaps;
  const gchar *delivery_method;
  const gchar *configuration;
  gint clock_rate;

  rtpvorbisdepay = GST_RTP_VORBIS_DEPAY (depayload);

  structure = gst_caps_get_structure (caps, 0);

  /* get clockrate */
  if (!gst_structure_get_int (structure, "clock-rate", &clock_rate))
    goto no_rate;

  /* see how the configuration parameters will be transmitted */
  delivery_method = gst_structure_get_string (structure, "delivery-method");

  if (delivery_method) {
    if (!g_ascii_strcasecmp (delivery_method, "inline")) {
      /* configure string is in the caps */
    } else if (!g_ascii_strcasecmp (delivery_method, "in_band")) {
      /* headers will (also) be transmitted in the RTP packets */
      goto unsupported_delivery_method;
    } else if (g_str_has_prefix (delivery_method, "out_band/")) {
      /* some other method of header delivery. */
      goto unsupported_delivery_method;
    } else
      goto unsupported_delivery_method;
  }

  /* read and parse configuration string */
  configuration = gst_structure_get_string (structure, "configuration");
  if (configuration == NULL)
    goto no_configuration;

  if (!gst_rtp_vorbis_depay_parse_configuration (rtpvorbisdepay, configuration))
    goto invalid_configuration;

  /* caps seem good, configure element */
  depayload->clock_rate = clock_rate;

  /* set caps on pad and on header */
  srccaps = gst_caps_new_simple ("audio/x-vorbis", NULL);
  gst_pad_set_caps (depayload->srcpad, srccaps);
  gst_caps_unref (srccaps);

  return TRUE;

  /* ERRORS */
unsupported_delivery_method:
  {
    GST_ERROR_OBJECT (rtpvorbisdepay,
        "unsupported delivery-method \"%s\" specified", delivery_method);
    return FALSE;
  }
no_configuration:
  {
    GST_ERROR_OBJECT (rtpvorbisdepay, "no configuration specified");
    return FALSE;
  }
invalid_configuration:
  {
    GST_ERROR_OBJECT (rtpvorbisdepay, "invalid configuration specified");
    return FALSE;
  }
no_rate:
  {
    GST_ERROR_OBJECT (rtpvorbisdepay, "no clock-rate specified");
    return FALSE;
  }
}

static gboolean
gst_rtp_vorbis_depay_switch_codebook (GstRtpVorbisDepay * rtpvorbisdepay,
    guint32 ident)
{
  GList *walk;
  gboolean res = FALSE;

  GST_DEBUG_OBJECT (rtpvorbisdepay, "Looking up code book ident 0x%08x", ident);
  for (walk = rtpvorbisdepay->configs; walk; walk = g_list_next (walk)) {
    GstRtpVorbisConfig *conf = (GstRtpVorbisConfig *) walk->data;

    if (conf->ident == ident) {
      GList *headers;

      /* FIXME, remove pads, create new pad.. */

      /* push out all the headers */
      for (headers = conf->headers; headers; headers = g_list_next (headers)) {
        GstBuffer *header = GST_BUFFER_CAST (headers->data);

        gst_buffer_ref (header);
        gst_base_rtp_depayload_push (GST_BASE_RTP_DEPAYLOAD (rtpvorbisdepay),
            header);
      }
      /* remember the current config */
      rtpvorbisdepay->config = conf;
      res = TRUE;
    }
  }
  if (!res) {
    /* we don't know about the headers, figure out an alternative method for
     * getting the codebooks. FIXME, fail for now. */
  }
  return res;
}

static GstBuffer *
gst_rtp_vorbis_depay_process (GstBaseRTPDepayload * depayload, GstBuffer * buf)
{
  GstRtpVorbisDepay *rtpvorbisdepay;
  GstBuffer *outbuf;
  GstFlowReturn ret;
  gint payload_len;
  guint8 *payload, *to_free = NULL;
  guint32 timestamp;
  guint32 header, ident;
  guint8 F, VDT, packets;
  gboolean free_payload;

  rtpvorbisdepay = GST_RTP_VORBIS_DEPAY (depayload);

  if (!gst_rtp_buffer_validate (buf))
    goto bad_packet;

  payload_len = gst_rtp_buffer_get_payload_len (buf);

  GST_DEBUG_OBJECT (depayload, "got RTP packet of size %d", payload_len);

  /* we need at least 4 bytes for the packet header */
  if (G_UNLIKELY (payload_len < 4))
    goto packet_short;

  payload = gst_rtp_buffer_get_payload (buf);
  free_payload = FALSE;

  header = GST_READ_UINT32_BE (payload);
  /*
   *  0                   1                   2                   3
   *  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
   * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   * |                     Ident                     | F |VDT|# pkts.|
   * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   *
   * F: Fragment type (0=none, 1=start, 2=cont, 3=end)
   * VDT: Vorbis data type (0=vorbis, 1=config, 2=comment, 3=reserved)
   * pkts: number of packets.
   */
  VDT = (header & 0x30) >> 4;
  if (G_UNLIKELY (VDT == 3))
    goto ignore_reserved;

  GST_DEBUG_OBJECT (depayload, "header: 0x%08x", header);
  ident = (header >> 8) & 0xffffff;
  F = (header & 0xc0) >> 6;
  packets = (header & 0xf);

  if (VDT == 0) {
    gboolean do_switch = FALSE;

    /* we have a raw payload, find the codebook for the ident */
    if (!rtpvorbisdepay->config) {
      /* we don't have an active codebook, find the codebook and
       * activate it */
      GST_DEBUG_OBJECT (rtpvorbisdepay, "No active codebook, switching");
      do_switch = TRUE;
    } else if (rtpvorbisdepay->config->ident != ident) {
      /* codebook changed */
      GST_DEBUG_OBJECT (rtpvorbisdepay, "codebook changed, switching");
      do_switch = TRUE;
    }
    if (do_switch) {
      if (!gst_rtp_vorbis_depay_switch_codebook (rtpvorbisdepay, ident))
        goto switch_failed;
    }
  }

  /* skip header */
  payload += 4;
  payload_len -= 4;

  GST_DEBUG_OBJECT (depayload, "ident: %u, F: %d, VDT: %d, packets: %d", ident,
      F, VDT, packets);

  /* fragmented packets, assemble */
  if (F != 0) {
    GstBuffer *vdata;
    guint headerskip;

    if (F == 1) {
      /* if we start a packet, clear adapter and start assembling. */
      gst_adapter_clear (rtpvorbisdepay->adapter);
      GST_DEBUG_OBJECT (depayload, "start assemble");
      rtpvorbisdepay->assembling = TRUE;
    }

    if (!rtpvorbisdepay->assembling)
      goto no_output;

    /* first assembled packet, reuse 2 bytes to store the length */
    headerskip = (F == 1 ? 4 : 6);
    /* skip header and length. */
    vdata = gst_rtp_buffer_get_payload_subbuffer (buf, headerskip, -1);

    GST_DEBUG_OBJECT (depayload, "assemble vorbis packet");
    gst_adapter_push (rtpvorbisdepay->adapter, vdata);

    /* packet is not complete, we are done */
    if (F != 3)
      goto no_output;

    /* construct assembled buffer */
    payload_len = gst_adapter_available (rtpvorbisdepay->adapter);
    payload = gst_adapter_take (rtpvorbisdepay->adapter, payload_len);
    /* fix the length */
    payload[0] = ((payload_len - 2) >> 8) & 0xff;
    payload[1] = (payload_len - 2) & 0xff;
    to_free = payload;
  }

  GST_DEBUG_OBJECT (depayload, "assemble done");

  /* we not assembling anymore now */
  rtpvorbisdepay->assembling = FALSE;
  gst_adapter_clear (rtpvorbisdepay->adapter);

  /* payload now points to a length with that many vorbis data bytes.
   * Iterate over the packets and send them out.
   *
   *  0                   1                   2                   3
   *  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
   * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   * |             length            |          vorbis data         ..
   * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   * ..                        vorbis data                           |
   * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   * |            length             |   next vorbis packet data    ..
   * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   * ..                        vorbis data                           |
   * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+*
   */
  timestamp = gst_rtp_buffer_get_timestamp (buf);

  while (payload_len > 2) {
    guint16 length;

    length = GST_READ_UINT16_BE (payload);
    payload += 2;
    payload_len -= 2;

    GST_DEBUG_OBJECT (depayload, "read length %u, avail: %d", length,
        payload_len);

    /* skip packet if something odd happens */
    if (G_UNLIKELY (length > payload_len))
      goto length_short;

    /* create buffer for packet */
    if (G_UNLIKELY (to_free)) {
      outbuf = gst_buffer_new ();
      GST_BUFFER_DATA (outbuf) = payload;
      GST_BUFFER_MALLOCDATA (outbuf) = to_free;
      GST_BUFFER_SIZE (outbuf) = length;
      to_free = NULL;
    } else {
      outbuf = gst_buffer_new_and_alloc (length);
      memcpy (GST_BUFFER_DATA (outbuf), payload, length);
    }

    payload += length;
    payload_len -= length;

    if (timestamp != -1)
      /* push with timestamp of the last packet, which is the same timestamp that
       * should apply to the first assembled packet. */
      ret = gst_base_rtp_depayload_push_ts (depayload, timestamp, outbuf);
    else
      ret = gst_base_rtp_depayload_push (depayload, outbuf);

    if (ret != GST_FLOW_OK)
      break;

    /* make sure we don't set a timestamp on next buffers */
    timestamp = -1;
  }

  g_free (to_free);

  return NULL;

no_output:
  {
    return NULL;
  }
  /* ERORRS */
bad_packet:
  {
    GST_ELEMENT_WARNING (rtpvorbisdepay, STREAM, DECODE,
        (NULL), ("Packet did not validate"));
    return NULL;
  }
switch_failed:
  {
    GST_ELEMENT_ERROR (rtpvorbisdepay, STREAM, DECODE,
        (NULL), ("Could not switch codebooks"));
    return NULL;
  }
packet_short:
  {
    GST_ELEMENT_WARNING (rtpvorbisdepay, STREAM, DECODE,
        (NULL), ("Packet was too short (%d < 4)", payload_len));
    return NULL;
  }
ignore_reserved:
  {
    GST_WARNING_OBJECT (rtpvorbisdepay, "reserved VDT ignored");
    return NULL;
  }
length_short:
  {
    GST_ELEMENT_WARNING (rtpvorbisdepay, STREAM, DECODE,
        (NULL), ("Packet contains invalid data"));
    return NULL;
  }
}

static GstStateChangeReturn
gst_rtp_vorbis_depay_change_state (GstElement * element,
    GstStateChange transition)
{
  GstRtpVorbisDepay *rtpvorbisdepay;
  GstStateChangeReturn ret;

  rtpvorbisdepay = GST_RTP_VORBIS_DEPAY (element);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
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
gst_rtp_vorbis_depay_plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "rtpvorbisdepay",
      GST_RANK_MARGINAL, GST_TYPE_RTP_VORBIS_DEPAY);
}
