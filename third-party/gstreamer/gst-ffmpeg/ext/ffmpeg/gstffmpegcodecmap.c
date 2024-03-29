/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 * This file:
 * Copyright (c) 2002-2004 Ronald Bultje <rbultje@ronald.bitfreak.net>
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
#include "config.h"
#endif
#include <gst/gst.h>
#ifdef HAVE_FFMPEG_UNINSTALLED
#include <avcodec.h>
#else
#include <ffmpeg/avcodec.h>
#endif
#include <string.h>

#include "gstffmpeg.h"
#include "gstffmpegcodecmap.h"

/*
 * Read a palette from a caps.
 */

static void
gst_ffmpeg_get_palette (const GstCaps * caps, AVCodecContext * context)
{
  GstStructure *str = gst_caps_get_structure (caps, 0);
  const GValue *palette_v;
  const GstBuffer *palette;

  /* do we have a palette? */
  if ((palette_v = gst_structure_get_value (str, "palette_data")) && context) {
    palette = gst_value_get_buffer (palette_v);
    if (GST_BUFFER_SIZE (palette) >= AVPALETTE_SIZE) {
      if (context->palctrl)
        av_free (context->palctrl);
      context->palctrl = av_malloc (sizeof (AVPaletteControl));
      context->palctrl->palette_changed = 1;
      memcpy (context->palctrl->palette, GST_BUFFER_DATA (palette),
          AVPALETTE_SIZE);
    }
  }
}

static void
gst_ffmpeg_set_palette (GstCaps * caps, AVCodecContext * context)
{
  if (context->palctrl) {
    GstBuffer *palette = gst_buffer_new_and_alloc (AVPALETTE_SIZE);

    memcpy (GST_BUFFER_DATA (palette), context->palctrl->palette,
        AVPALETTE_SIZE);
    gst_caps_set_simple (caps, "palette_data", GST_TYPE_BUFFER, palette, NULL);
  }
}

/* this macro makes a caps width fixed or unfixed width/height
 * properties depending on whether we've got a context.
 *
 * See below for why we use this.
 *
 * We should actually do this stuff at the end, like in riff-media.c,
 * but I'm too lazy today. Maybe later.
 */
static GstCaps *
gst_ff_vid_caps_new (AVCodecContext * context, enum CodecID codec_id,
    const char *mimetype, const char *fieldname, ...)
{
  GstStructure *structure = NULL;
  GstCaps *caps = NULL;
  va_list var_args;
  gint i;

  if (context != NULL) {
    caps = gst_caps_new_simple (mimetype,
        "width", G_TYPE_INT, context->width,
        "height", G_TYPE_INT, context->height,
        "framerate", GST_TYPE_FRACTION,
        context->time_base.den, context->time_base.num, NULL);
  } else {
    switch (codec_id) {
      case CODEC_ID_H263:
      {
        /* 128x96, 176x144, 352x288, 704x576, and 1408x1152. slightly reordered
         * because we want automatic negotiation to go as close to 320x240 as
         * possible. */
        const static gint widths[] = { 352, 704, 176, 1408, 128 };
        const static gint heights[] = { 288, 576, 144, 1152, 96 };
        GstCaps *temp;
        gint n_sizes = G_N_ELEMENTS (widths);

        caps = gst_caps_new_empty ();
        for (i = 0; i < n_sizes; i++) {
          temp = gst_caps_new_simple (mimetype,
              "width", G_TYPE_INT, widths[i],
              "height", G_TYPE_INT, heights[i],
              "framerate", GST_TYPE_FRACTION_RANGE, 0, 1, G_MAXINT, 1, NULL);

          gst_caps_append (caps, temp);
        }
        break;
      }
      case CODEC_ID_DVVIDEO:
      {
        const static gint widths[] = { 720, 720 };
        const static gint heights[] = { 576, 480 };
        GstCaps *temp;
        gint n_sizes = G_N_ELEMENTS (widths);

        caps = gst_caps_new_empty ();
        for (i = 0; i < n_sizes; i++) {
          temp = gst_caps_new_simple (mimetype,
              "width", G_TYPE_INT, widths[i],
              "height", G_TYPE_INT, heights[i],
              "framerate", GST_TYPE_FRACTION_RANGE, 0, 1, G_MAXINT, 1, NULL);

          gst_caps_append (caps, temp);
	}
        break;
      }
      default:
        caps = gst_caps_new_simple (mimetype,
            "width", GST_TYPE_INT_RANGE, 16, 4096,
            "height", GST_TYPE_INT_RANGE, 16, 4096,
            "framerate", GST_TYPE_FRACTION_RANGE, 0, 1, G_MAXINT, 1, NULL);
        break;
    }
  }

  for (i = 0; i < gst_caps_get_size (caps); i++) {
    structure = gst_caps_get_structure (caps, i);
    va_start (var_args, fieldname);
    gst_structure_set_valist (structure, fieldname, var_args);
    va_end (var_args);
  }

  return caps;
}

/* same for audio - now with channels/sample rate
 */

static GstCaps *
gst_ff_aud_caps_new (AVCodecContext * context, enum CodecID codec_id,
    const char *mimetype, const char *fieldname, ...)
{
  GstCaps *caps = NULL;
  GstStructure *structure = NULL;
  va_list var_args;

  if (context != NULL) {
    caps = gst_caps_new_simple (mimetype,
        "rate", G_TYPE_INT, context->sample_rate,
        "channels", G_TYPE_INT, context->channels, NULL);
  } else {
    gint maxchannels;
    /* Until decoders/encoders expose the maximum number of channels
     * they support, we whitelist them here. */
    switch (codec_id) {
    case CODEC_ID_AC3:
    case CODEC_ID_AAC:
    case CODEC_ID_DTS:
      maxchannels = 6;
      break;
    default:
      maxchannels = 2;
    }

    caps = gst_caps_new_simple (mimetype,
        "rate", GST_TYPE_INT_RANGE, 8000, 96000,
        "channels", GST_TYPE_INT_RANGE, 1, maxchannels, NULL);

  }

  structure = gst_caps_get_structure (caps, 0);

  if (structure) {
    va_start (var_args, fieldname);
    gst_structure_set_valist (structure, fieldname, var_args);
    va_end (var_args);
  }

  return caps;
}

/* Convert a FFMPEG codec ID and optional AVCodecContext
 * to a GstCaps. If the context is ommitted, no fixed values
 * for video/audio size will be included in the GstCaps
 *
 * CodecID is primarily meant for compressed data GstCaps!
 *
 * encode is a special parameter. gstffmpegdec will say
 * FALSE, gstffmpegenc will say TRUE. The output caps
 * depends on this, in such a way that it will be very
 * specific, defined, fixed and correct caps for encoders,
 * yet very wide, "forgiving" caps for decoders. Example
 * for mp3: decode: audio/mpeg,mpegversion=1,layer=[1-3]
 * but encode: audio/mpeg,mpegversion=1,layer=3,bitrate=x,
 * rate=x,channels=x.
 */

GstCaps *
gst_ffmpeg_codecid_to_caps (enum CodecID codec_id,
    AVCodecContext * context, gboolean encode)
{
  GstCaps *caps = NULL;
  gboolean buildcaps = FALSE;

  switch (codec_id) {
    case CODEC_ID_MPEG1VIDEO:
      /* FIXME: bitrate */
      caps = gst_ff_vid_caps_new (context, codec_id, "video/mpeg",
          "mpegversion", G_TYPE_INT, 1,
          "systemstream", G_TYPE_BOOLEAN, FALSE, NULL);
      break;

    case CODEC_ID_MPEG2VIDEO:
      if (encode) {
        /* FIXME: bitrate */
        caps = gst_ff_vid_caps_new (context, codec_id, "video/mpeg",
            "mpegversion", G_TYPE_INT, 2,
            "systemstream", G_TYPE_BOOLEAN, FALSE, NULL);
      } else {
        /* decode both MPEG-1 and MPEG-2; width/height/fps are all in
         * the MPEG video stream headers, so may be omitted from caps. */
        caps = gst_caps_new_simple ("video/mpeg",
            "mpegversion", GST_TYPE_INT_RANGE, 1, 2,
            "systemstream", G_TYPE_BOOLEAN, FALSE, NULL);
      }
      break;

    case CODEC_ID_MPEG2VIDEO_XVMC:
      /* this is a special ID - don't need it in GStreamer, I think */
      break;

    case CODEC_ID_H263:
      if (encode) {
        caps = gst_ff_vid_caps_new (context, codec_id, "video/x-h263",
            "variant", G_TYPE_STRING, "itu",
            "h263version", G_TYPE_STRING, "h263", NULL);
      } else {
        /* don't pass codec_id, we can decode other variants with the H263
         * decoder that don't have specific size requirements
         */
        caps = gst_ff_vid_caps_new (context, CODEC_ID_NONE, "video/x-h263",
            "variant", G_TYPE_STRING, "itu", NULL);
      }
      break;

    case CODEC_ID_H263P:
      caps = gst_ff_vid_caps_new (context, codec_id, "video/x-h263",
          "variant", G_TYPE_STRING, "itu",
          "h263version", G_TYPE_STRING, "h263p", NULL);
      break;

    case CODEC_ID_H263I:
      caps = gst_ff_vid_caps_new (context, codec_id, "video/x-intel-h263",
          "variant", G_TYPE_STRING, "intel", NULL);
      break;

    case CODEC_ID_H261:
      caps = gst_ff_vid_caps_new (context, codec_id, "video/x-h261", NULL);
      break;

    case CODEC_ID_RV10:
    case CODEC_ID_RV20:
    case CODEC_ID_RV30:
    case CODEC_ID_RV40:
    {
      gint version;

      switch (codec_id) {
        case CODEC_ID_RV40:
          version = 4;
          break;
        case CODEC_ID_RV30:
          version = 3;
          break;
        case CODEC_ID_RV20:
          version = 2;
          break;
        default:
          version = 1;
          break;
      }

      /* FIXME: context->sub_id must be filled in during decoding */
      caps = gst_ff_vid_caps_new (context, codec_id, "video/x-pn-realvideo",
          "systemstream", G_TYPE_BOOLEAN, FALSE,
          "rmversion", G_TYPE_INT, version, NULL);
      if (context) {
        gst_caps_set_simple (caps, "format", G_TYPE_INT, context->sub_id, NULL);
        if (context->extradata_size >= 8) {
          gst_caps_set_simple (caps,
              "subformat", G_TYPE_INT, GST_READ_UINT32_BE (context->extradata),
              NULL);
        }
      }
    }
      break;

    case CODEC_ID_MP2:
      /* FIXME: bitrate */
      caps = gst_ff_aud_caps_new (context, codec_id, "audio/mpeg",
          "mpegversion", G_TYPE_INT, 1, "layer", G_TYPE_INT, 2, NULL);
      break;

    case CODEC_ID_MP3:
      if (encode) {
        /* FIXME: bitrate */
        caps = gst_ff_aud_caps_new (context, codec_id, "audio/mpeg",
            "mpegversion", G_TYPE_INT, 1, "layer", G_TYPE_INT, 3, NULL);
      } else {
        /* Decodes MPEG-1 layer 1/2/3. Samplerate, channels et al are
         * in the MPEG audio header, so may be omitted from caps. */
        caps = gst_caps_new_simple ("audio/mpeg",
            "mpegversion", G_TYPE_INT, 1,
            "layer", GST_TYPE_INT_RANGE, 1, 3, NULL);
      }
      break;

    case CODEC_ID_MUSEPACK7:
      caps =
          gst_ff_aud_caps_new (context, codec_id,
          "audio/x-ffmpeg-parsed-musepack", "streamversion", G_TYPE_INT, 7,
          NULL);
      break;

    case CODEC_ID_MUSEPACK8:
      caps =
          gst_ff_aud_caps_new (context, codec_id,
          "audio/x-ffmpeg-parsed-musepack", "streamversion", G_TYPE_INT, 8,
          NULL);
      break;

    case CODEC_ID_AC3:
      /* FIXME: bitrate */
      caps = gst_ff_aud_caps_new (context, codec_id, "audio/x-ac3", NULL);
      break;

    case CODEC_ID_ATRAC3:
      caps = gst_ff_aud_caps_new (context, codec_id, "audio/atrac3", NULL);
      break;

    case CODEC_ID_DTS:
      caps = gst_ff_aud_caps_new (context, codec_id, "audio/x-dts", NULL);
      break;

    case CODEC_ID_APE:
      caps =
          gst_ff_aud_caps_new (context, codec_id, "audio/x-ffmpeg-parsed-ape",
          NULL);
      if (context) {
        gst_caps_set_simple (caps,
            "depth", G_TYPE_INT, context->bits_per_sample, NULL);
      }
      break;

      /* MJPEG is normal JPEG, Motion-JPEG and Quicktime MJPEG-A. MJPEGB
       * is Quicktime's MJPEG-B. LJPEG is lossless JPEG. I don't know what
       * sp5x is, but it's apparently something JPEG... We don't separate
       * between those in GStreamer. Should we (at least between MJPEG,
       * MJPEG-B and sp5x decoding...)? */
    case CODEC_ID_MJPEG:
    case CODEC_ID_LJPEG:
      caps = gst_ff_vid_caps_new (context, codec_id, "image/jpeg", NULL);
      break;

    case CODEC_ID_SP5X:
      caps = gst_ff_vid_caps_new (context, codec_id, "video/sp5x", NULL);
      break;

    case CODEC_ID_MJPEGB:
      caps = gst_ff_vid_caps_new (context, codec_id, "video/x-mjpeg-b", NULL);
      break;

    case CODEC_ID_MPEG4:
      if (encode && context != NULL) {
        /* I'm not exactly sure what ffmpeg outputs... ffmpeg itself uses
         * the AVI fourcc 'DIVX', but 'mp4v' for Quicktime... */
        switch (context->codec_tag) {
          case GST_MAKE_FOURCC ('D', 'I', 'V', 'X'):
            caps = gst_ff_vid_caps_new (context, codec_id, "video/x-divx",
                "divxversion", G_TYPE_INT, 5, NULL);
            break;
          case GST_MAKE_FOURCC ('m', 'p', '4', 'v'):
          default:
            /* FIXME: bitrate */
            caps = gst_ff_vid_caps_new (context, codec_id, "video/mpeg",
                "systemstream", G_TYPE_BOOLEAN, FALSE,
                "mpegversion", G_TYPE_INT, 4, NULL);
            break;
        }
      } else {
        /* The trick here is to separate xvid, divx, mpeg4, 3ivx et al */
        caps = gst_ff_vid_caps_new (context, codec_id, "video/mpeg",
            "mpegversion", G_TYPE_INT, 4,
            "systemstream", G_TYPE_BOOLEAN, FALSE, NULL);
        if (encode) {
          gst_caps_append (caps, gst_ff_vid_caps_new (context, codec_id,
                  "video/x-divx", "divxversion", G_TYPE_INT, 5, NULL));
        } else {
          gst_caps_append (caps, gst_ff_vid_caps_new (context, codec_id,
                  "video/x-divx", "divxversion", GST_TYPE_INT_RANGE, 4, 5,
                  NULL));
          gst_caps_append (caps, gst_ff_vid_caps_new (context, codec_id,
                  "video/x-xvid", NULL));
          gst_caps_append (caps, gst_ff_vid_caps_new (context, codec_id,
                  "video/x-3ivx", NULL));
        }
      }
      break;

    case CODEC_ID_RAWVIDEO:
      caps = gst_ffmpeg_codectype_to_caps (CODEC_TYPE_VIDEO, context, codec_id);
      break;

    case CODEC_ID_MSMPEG4V1:
    case CODEC_ID_MSMPEG4V2:
    case CODEC_ID_MSMPEG4V3:
    {
      gint version = 41 + codec_id - CODEC_ID_MSMPEG4V1;

      /* encode-FIXME: bitrate */
      caps = gst_ff_vid_caps_new (context, codec_id, "video/x-msmpeg",
          "msmpegversion", G_TYPE_INT, version, NULL);
      if (!encode && codec_id == CODEC_ID_MSMPEG4V3) {
        gst_caps_append (caps, gst_ff_vid_caps_new (context, codec_id,
                "video/x-divx", "divxversion", G_TYPE_INT, 3, NULL));
      }
    }
      break;

    case CODEC_ID_WMV1:
    case CODEC_ID_WMV2:
    {
      gint version = (codec_id == CODEC_ID_WMV1) ? 1 : 2;

      caps = gst_ff_vid_caps_new (context, codec_id, "video/x-wmv",
          "wmvversion", G_TYPE_INT, version, NULL);
    }
      break;

    case CODEC_ID_FLV1:
      caps = gst_ff_vid_caps_new (context, codec_id, "video/x-flash-video",
          "flvversion", G_TYPE_INT, 1, NULL);
      break;

    case CODEC_ID_SVQ1:
      caps = gst_ff_vid_caps_new (context, codec_id, "video/x-svq",
          "svqversion", G_TYPE_INT, 1, NULL);
      break;

    case CODEC_ID_SVQ3:
      caps = gst_ff_vid_caps_new (context, codec_id, "video/x-svq",
          "svqversion", G_TYPE_INT, 3, NULL);
      break;

    case CODEC_ID_DVAUDIO:
      caps = gst_ff_aud_caps_new (context, codec_id, "audio/x-dv", NULL);
      break;

    case CODEC_ID_DVVIDEO:
    {
      if (encode && context) {
        guint32 fourcc;

        switch (context->pix_fmt) {
          case PIX_FMT_YUV422:
            fourcc = GST_MAKE_FOURCC ('Y', 'U', 'Y', '2');
            break;
          case PIX_FMT_YUV420P:
            fourcc = GST_MAKE_FOURCC ('I', '4', '2', '0');
            break;
          case PIX_FMT_YUV411P:
            fourcc = GST_MAKE_FOURCC ('Y', '4', '1', 'B');
            break;
          case PIX_FMT_YUV422P:
            fourcc = GST_MAKE_FOURCC ('Y', '4', '2', 'B');
            break;
          case PIX_FMT_YUV410P:
            fourcc = GST_MAKE_FOURCC ('Y', 'U', 'V', '9');
            break;
          default:
            GST_WARNING
                ("Couldnt' find fourcc for pixfmt %d, defaulting to I420",
                context->pix_fmt);
            fourcc = GST_MAKE_FOURCC ('I', '4', '2', '0');
            break;
        }
        caps = gst_ff_vid_caps_new (context, codec_id, "video/x-dv",
            "systemstream", G_TYPE_BOOLEAN, FALSE,
            "format", GST_TYPE_FOURCC, fourcc, NULL);
      } else {
        caps = gst_ff_vid_caps_new (context, codec_id, "video/x-dv",
            "systemstream", G_TYPE_BOOLEAN, FALSE, NULL);
      }
    }
      break;

    case CODEC_ID_WMAV1:
    case CODEC_ID_WMAV2:
    {
      gint version = (codec_id == CODEC_ID_WMAV1) ? 1 : 2;

      if (context) {
        caps = gst_ff_aud_caps_new (context, codec_id, "audio/x-wma",
            "wmaversion", G_TYPE_INT, version,
            "block_align", G_TYPE_INT, context->block_align,
            "bitrate", G_TYPE_INT, context->bit_rate, NULL);
      } else {
        caps = gst_ff_aud_caps_new (context, codec_id, "audio/x-wma",
            "wmaversion", G_TYPE_INT, version,
            "block_align", GST_TYPE_INT_RANGE, 0, G_MAXINT,
            "bitrate", GST_TYPE_INT_RANGE, 0, G_MAXINT, NULL);
      }
    }
      break;

    case CODEC_ID_MACE3:
    case CODEC_ID_MACE6:
    {
      gint version = (codec_id == CODEC_ID_MACE3) ? 3 : 6;

      caps = gst_ff_aud_caps_new (context, codec_id, "audio/x-mace",
          "maceversion", G_TYPE_INT, version, NULL);
    }
      break;

    case CODEC_ID_HUFFYUV:
      caps = gst_ff_vid_caps_new (context, codec_id, "video/x-huffyuv", NULL);
      if (context) {
        gst_caps_set_simple (caps,
            "bpp", G_TYPE_INT, context->bits_per_sample, NULL);
      }
      break;

    case CODEC_ID_CYUV:
      caps =
          gst_ff_vid_caps_new (context, codec_id, "video/x-compressed-yuv",
          NULL);
      break;

    case CODEC_ID_H264:
      caps = gst_ff_vid_caps_new (context, codec_id, "video/x-h264", NULL);
      break;

    case CODEC_ID_INDEO3:
      caps = gst_ff_vid_caps_new (context, codec_id, "video/x-indeo",
          "indeoversion", G_TYPE_INT, 3, NULL);
      break;

    case CODEC_ID_INDEO2:
      caps = gst_ff_vid_caps_new (context, codec_id, "video/x-indeo",
          "indeoversion", G_TYPE_INT, 2, NULL);
      break;

    case CODEC_ID_FLASHSV:
      caps =
          gst_ff_vid_caps_new (context, codec_id, "video/x-flash-screen", NULL);
      break;

    case CODEC_ID_VP3:
      caps = gst_ff_vid_caps_new (context, codec_id, "video/x-vp3", NULL);
      break;

    case CODEC_ID_VP5:
      caps = gst_ff_vid_caps_new (context, codec_id, "video/x-vp5", NULL);
      break;

    case CODEC_ID_VP6:
      caps = gst_ff_vid_caps_new (context, codec_id, "video/x-vp6", NULL);
      break;

    case CODEC_ID_VP6F:
      caps = gst_ff_vid_caps_new (context, codec_id, "video/x-vp6-flash", NULL);
      break;

    case CODEC_ID_VP6A:
      caps = gst_ff_vid_caps_new (context, codec_id, "video/x-vp6-alpha", NULL);
      break;

    case CODEC_ID_THEORA:
      caps = gst_ff_vid_caps_new (context, codec_id, "video/x-theora", NULL);
      break;

    case CODEC_ID_AAC:
      caps = gst_ff_aud_caps_new (context, codec_id, "audio/mpeg",
                                  "mpegversion", G_TYPE_INT, 4, NULL);
      break;

    case CODEC_ID_ASV1:
      caps = gst_ff_vid_caps_new (context, codec_id, "video/x-asus",
          "asusversion", G_TYPE_INT, 1, NULL);
      break;
    case CODEC_ID_ASV2:
      caps = gst_ff_vid_caps_new (context, codec_id, "video/x-asus",
          "asusversion", G_TYPE_INT, 2, NULL);
      break;

    case CODEC_ID_FFV1:
      caps = gst_ff_vid_caps_new (context, codec_id, "video/x-ffv",
          "ffvversion", G_TYPE_INT, 1, NULL);
      break;

    case CODEC_ID_4XM:
      caps = gst_ff_vid_caps_new (context, codec_id, "video/x-4xm", NULL);
      break;

    case CODEC_ID_XAN_WC3:
    case CODEC_ID_XAN_WC4:
      caps = gst_ff_vid_caps_new (context, codec_id, "video/x-xan",
          "wcversion", G_TYPE_INT, 3 - CODEC_ID_XAN_WC3 + codec_id, NULL);
      break;

    case CODEC_ID_CLJR:
      caps =
          gst_ff_vid_caps_new (context, codec_id,
          "video/x-cirrus-logic-accupak", NULL);
      break;

    case CODEC_ID_FRAPS:
    case CODEC_ID_MDEC:
    case CODEC_ID_ROQ:
    case CODEC_ID_INTERPLAY_VIDEO:
      buildcaps = TRUE;
      break;

    case CODEC_ID_VCR1:
      caps = gst_ff_vid_caps_new (context, codec_id, "video/x-ati-vcr",
          "vcrversion", G_TYPE_INT, 1, NULL);
      break;

    case CODEC_ID_RPZA:
      caps =
          gst_ff_vid_caps_new (context, codec_id, "video/x-apple-video", NULL);
      break;

    case CODEC_ID_CINEPAK:
      caps = gst_ff_vid_caps_new (context, codec_id, "video/x-cinepak", NULL);
      break;

      /* WS_VQA belogns here (order) */

    case CODEC_ID_MSRLE:
      caps = gst_ff_vid_caps_new (context, codec_id, "video/x-rle",
          "layout", G_TYPE_STRING, "microsoft", NULL);
      if (context) {
        gst_caps_set_simple (caps,
            "depth", G_TYPE_INT, (gint) context->bits_per_sample, NULL);
      } else {
        gst_caps_set_simple (caps, "depth", GST_TYPE_INT_RANGE, 1, 64, NULL);
      }
      break;

    case CODEC_ID_QTRLE:
      caps = gst_ff_vid_caps_new (context, codec_id, "video/x-rle",
          "layout", G_TYPE_STRING, "quicktime", NULL);
      if (context) {
        gst_caps_set_simple (caps,
            "depth", G_TYPE_INT, (gint) context->bits_per_sample, NULL);
      } else {
        gst_caps_set_simple (caps, "depth", GST_TYPE_INT_RANGE, 1, 64, NULL);
      }
      break;

    case CODEC_ID_MSVIDEO1:
      caps = gst_ff_vid_caps_new (context, codec_id, "video/x-msvideocodec",
          "msvideoversion", G_TYPE_INT, 1, NULL);
      break;

    case CODEC_ID_WMV3:
      caps = gst_ff_vid_caps_new (context, codec_id, "video/x-wmv",
          "wmvversion", G_TYPE_INT, 3, NULL);
      break;
    case CODEC_ID_VC1:
      caps = gst_ff_vid_caps_new (context, codec_id, "video/x-wmv",
          "wmvversion", G_TYPE_INT, 3, "fourcc", GST_TYPE_FOURCC,
          GST_MAKE_FOURCC ('W', 'V', 'C', '1'), NULL);
      break;
    case CODEC_ID_QDM2:
      caps = gst_ff_aud_caps_new (context, codec_id, "audio/x-qdm2", NULL);
      break;

    case CODEC_ID_MSZH:
      caps = gst_ff_vid_caps_new (context, codec_id, "video/x-mszh", NULL);
      break;

    case CODEC_ID_ZLIB:
      caps = gst_ff_vid_caps_new (context, codec_id, "video/x-zlib", NULL);
      break;

    case CODEC_ID_TRUEMOTION1:
      caps = gst_ff_vid_caps_new (context, codec_id, "video/x-truemotion",
          "trueversion", G_TYPE_INT, 1, NULL);
      break;
    case CODEC_ID_TRUEMOTION2:
      caps = gst_ff_vid_caps_new (context, codec_id, "video/x-truemotion",
          "trueversion", G_TYPE_INT, 2, NULL);
      break;

    case CODEC_ID_ULTI:
      caps = gst_ff_vid_caps_new (context, codec_id, "video/x-ultimotion",
          NULL);
      break;

    case CODEC_ID_TSCC:
      caps = gst_ff_vid_caps_new (context, codec_id, "video/x-camtasia", NULL);
      if (context) {
        gst_caps_set_simple (caps,
            "depth", G_TYPE_INT, (gint) context->bits_per_sample, NULL);
      } else {
        gst_caps_set_simple (caps, "depth", GST_TYPE_INT_RANGE, 8, 32, NULL);
      }
      break;

    case CODEC_ID_KMVC:
      caps = gst_ff_vid_caps_new (context, codec_id, "video/x-kmvc", NULL);
      break;

    case CODEC_ID_NUV:
      caps = gst_ff_vid_caps_new (context, codec_id, "video/x-nuv", NULL);
      break;

    case CODEC_ID_GIF:
      caps = gst_ff_vid_caps_new (context, codec_id, "image/gif", NULL);
      break;

    case CODEC_ID_PNG:
      caps = gst_ff_vid_caps_new (context, codec_id, "image/png", NULL);
      break;

    case CODEC_ID_PPM:
      caps = gst_ff_vid_caps_new (context, codec_id, "image/ppm", NULL);
      break;

    case CODEC_ID_PBM:
      caps = gst_ff_vid_caps_new (context, codec_id, "image/pbm", NULL);
      break;

    case CODEC_ID_SMC:
      caps = gst_ff_vid_caps_new (context, codec_id, "video/x-smc", NULL);
      break;

    case CODEC_ID_QDRAW:
      caps = gst_ff_vid_caps_new (context, codec_id, "video/x-qdrw", NULL);
      break;

  case CODEC_ID_DNXHD:
    caps = gst_ff_vid_caps_new (context, codec_id, "video/x-dnxhd", NULL);
    break;

  case CODEC_ID_MIMIC:
    caps = gst_ff_vid_caps_new (context, codec_id, "video/x-mimic", NULL);
    break;

  case CODEC_ID_VMNC:
    caps = gst_ff_vid_caps_new (context, codec_id, "video/x-vmnc", NULL);
    break;

    case CODEC_ID_TRUESPEECH:
    caps = gst_ff_aud_caps_new (context, codec_id, "audio/x-truespeech", NULL);
    break;

    case CODEC_ID_WS_VQA:
    case CODEC_ID_IDCIN:
    case CODEC_ID_8BPS:
    case CODEC_ID_FLIC:
    case CODEC_ID_VMDVIDEO:
    case CODEC_ID_VMDAUDIO:
    case CODEC_ID_SONIC:
    case CODEC_ID_SONIC_LS:
    case CODEC_ID_SNOW:
    case CODEC_ID_VIXL:
    case CODEC_ID_QPEG:
    case CODEC_ID_XVID:
    case CODEC_ID_PGM:
    case CODEC_ID_PGMYUV:
    case CODEC_ID_PAM:
    case CODEC_ID_FFVHUFF:
    case CODEC_ID_LOCO:
    case CODEC_ID_WNV1:
    case CODEC_ID_AASC:
    case CODEC_ID_MP3ADU:
    case CODEC_ID_MP3ON4:
    case CODEC_ID_WESTWOOD_SND1:
    case CODEC_ID_CSCD:
    case CODEC_ID_MMVIDEO:
    case CODEC_ID_ZMBV:
    case CODEC_ID_AVS:
    case CODEC_ID_CAVS:
      buildcaps = TRUE;
      break;

      /* weird quasi-codecs for the demuxers only */
    case CODEC_ID_PCM_S16LE:
    case CODEC_ID_PCM_S16BE:
    case CODEC_ID_PCM_U16LE:
    case CODEC_ID_PCM_U16BE:
    case CODEC_ID_PCM_S8:
    case CODEC_ID_PCM_U8:
    {
      gint width = 0, depth = 0, endianness = 0;
      gboolean signedness = FALSE;      /* blabla */

      switch (codec_id) {
        case CODEC_ID_PCM_S16LE:
          width = 16;
          depth = 16;
          endianness = G_LITTLE_ENDIAN;
          signedness = TRUE;
          break;
        case CODEC_ID_PCM_S16BE:
          width = 16;
          depth = 16;
          endianness = G_BIG_ENDIAN;
          signedness = TRUE;
          break;
        case CODEC_ID_PCM_U16LE:
          width = 16;
          depth = 16;
          endianness = G_LITTLE_ENDIAN;
          signedness = FALSE;
          break;
        case CODEC_ID_PCM_U16BE:
          width = 16;
          depth = 16;
          endianness = G_BIG_ENDIAN;
          signedness = FALSE;
          break;
        case CODEC_ID_PCM_S8:
          width = 8;
          depth = 8;
          endianness = G_BYTE_ORDER;
          signedness = TRUE;
          break;
        case CODEC_ID_PCM_U8:
          width = 8;
          depth = 8;
          endianness = G_BYTE_ORDER;
          signedness = FALSE;
          break;
        default:
          g_assert (0);         /* don't worry, we never get here */
          break;
      }

      caps = gst_ff_aud_caps_new (context, codec_id, "audio/x-raw-int",
          "width", G_TYPE_INT, width,
          "depth", G_TYPE_INT, depth,
          "endianness", G_TYPE_INT, endianness,
          "signed", G_TYPE_BOOLEAN, signedness, NULL);
    }
      break;

    case CODEC_ID_PCM_MULAW:
      caps = gst_ff_aud_caps_new (context, codec_id, "audio/x-mulaw", NULL);
      break;

    case CODEC_ID_PCM_ALAW:
      caps = gst_ff_aud_caps_new (context, codec_id, "audio/x-alaw", NULL);
      break;

    case CODEC_ID_ADPCM_IMA_QT:
    case CODEC_ID_ADPCM_IMA_WAV:
    case CODEC_ID_ADPCM_IMA_DK3:
    case CODEC_ID_ADPCM_IMA_DK4:
    case CODEC_ID_ADPCM_IMA_WS:
    case CODEC_ID_ADPCM_IMA_SMJPEG:
    case CODEC_ID_ADPCM_IMA_AMV:
    case CODEC_ID_ADPCM_MS:
    case CODEC_ID_ADPCM_4XM:
    case CODEC_ID_ADPCM_XA:
    case CODEC_ID_ADPCM_ADX:
    case CODEC_ID_ADPCM_EA:
    case CODEC_ID_ADPCM_G726:
    case CODEC_ID_ADPCM_CT:
    case CODEC_ID_ADPCM_SWF:
    case CODEC_ID_ADPCM_YAMAHA:
    case CODEC_ID_ADPCM_SBPRO_2:
    case CODEC_ID_ADPCM_SBPRO_3:
    case CODEC_ID_ADPCM_SBPRO_4:
    case CODEC_ID_ADPCM_EA_R1:
    case CODEC_ID_ADPCM_EA_R2:
    case CODEC_ID_ADPCM_EA_R3:
    case CODEC_ID_ADPCM_THP:
    {
      gchar *layout = NULL;

      switch (codec_id) {
        case CODEC_ID_ADPCM_IMA_QT:
          layout = "quicktime";
          break;
        case CODEC_ID_ADPCM_IMA_WAV:
          layout = "dvi";
          break;
        case CODEC_ID_ADPCM_IMA_DK3:
          layout = "dk3";
          break;
        case CODEC_ID_ADPCM_IMA_DK4:
          layout = "dk4";
          break;
        case CODEC_ID_ADPCM_IMA_WS:
          layout = "westwood";
          break;
        case CODEC_ID_ADPCM_IMA_SMJPEG:
          layout = "smjpeg";
          break;
        case CODEC_ID_ADPCM_IMA_AMV:
          layout = "amv";
          break;
        case CODEC_ID_ADPCM_MS:
          layout = "microsoft";
          break;
        case CODEC_ID_ADPCM_4XM:
          layout = "4xm";
          break;
        case CODEC_ID_ADPCM_XA:
          layout = "xa";
          break;
        case CODEC_ID_ADPCM_ADX:
          layout = "adx";
          break;
        case CODEC_ID_ADPCM_EA:
          layout = "ea";
          break;
        case CODEC_ID_ADPCM_G726:
          layout = "g726";
          break;
        case CODEC_ID_ADPCM_CT:
          layout = "ct";
          break;
        case CODEC_ID_ADPCM_SWF:
          layout = "swf";
          break;
        case CODEC_ID_ADPCM_YAMAHA:
          layout = "yamaha";
          break;
        case CODEC_ID_ADPCM_SBPRO_2:
          layout = "sbpro2";
          break;
        case CODEC_ID_ADPCM_SBPRO_3:
          layout = "sbpro3";
          break;
        case CODEC_ID_ADPCM_SBPRO_4:
          layout = "sbpro4";
          break;
        case CODEC_ID_ADPCM_EA_R1:
          layout = "ea-r1";
          break;
        case CODEC_ID_ADPCM_EA_R2:
          layout = "ea-r3";
          break;
        case CODEC_ID_ADPCM_EA_R3:
          layout = "ea-r3";
          break;
        case CODEC_ID_ADPCM_THP:
          layout = "thp";
          break;
        default:
          g_assert (0);         /* don't worry, we never get here */
          break;
      }

      /* FIXME: someone please check whether we need additional properties
       * in this caps definition. */
      caps = gst_ff_aud_caps_new (context, codec_id, "audio/x-adpcm",
          "layout", G_TYPE_STRING, layout, NULL);
      if (context)
        gst_caps_set_simple (caps,
            "block_align", G_TYPE_INT, context->block_align,
            "bitrate", G_TYPE_INT, context->bit_rate, NULL);
    }
      break;

    case CODEC_ID_AMR_NB:
      caps = gst_ff_aud_caps_new (context, codec_id, "audio/AMR", NULL);
      break;

    case CODEC_ID_AMR_WB:
      caps = gst_ff_aud_caps_new (context, codec_id, "audio/AMR-WB", NULL);
      break;

    case CODEC_ID_NELLYMOSER:
      caps =
          gst_ff_aud_caps_new (context, codec_id, "audio/x-nellymoser", NULL);
      break;

    case CODEC_ID_RA_144:
    case CODEC_ID_RA_288:
    case CODEC_ID_COOK:
    {
      gint version = 0;

      switch (codec_id) {
        case CODEC_ID_RA_144:
          version = 1;
          break;
        case CODEC_ID_RA_288:
          version = 2;
          break;
        case CODEC_ID_COOK:
          version = 8;
          break;
        default:
          break;
      }

      /* FIXME: properties? */
      caps = gst_ff_aud_caps_new (context, codec_id, "audio/x-pn-realaudio",
          "raversion", G_TYPE_INT, version, NULL);
      if (context) {
        gst_caps_set_simple (caps,
            "leaf_size", G_TYPE_INT, context->block_align,
            "bitrate", G_TYPE_INT, context->bit_rate, NULL);
      }
    }
      break;

    case CODEC_ID_ROQ_DPCM:
    case CODEC_ID_INTERPLAY_DPCM:
    case CODEC_ID_XAN_DPCM:
    case CODEC_ID_SOL_DPCM:
    {
      gchar *layout = NULL;

      switch (codec_id) {
        case CODEC_ID_ROQ_DPCM:
          layout = "roq";
          break;
        case CODEC_ID_INTERPLAY_DPCM:
          layout = "interplay";
          break;
        case CODEC_ID_XAN_DPCM:
          layout = "xan";
          break;
        case CODEC_ID_SOL_DPCM:
          layout = "sol";
          break;
        default:
          g_assert (0);         /* don't worry, we never get here */
          break;
      }

      /* FIXME: someone please check whether we need additional properties
       * in this caps definition. */
      caps = gst_ff_aud_caps_new (context, codec_id, "audio/x-dpcm",
          "layout", G_TYPE_STRING, layout, NULL);
      if (context)
        gst_caps_set_simple (caps,
            "block_align", G_TYPE_INT, context->block_align,
            "bitrate", G_TYPE_INT, context->bit_rate, NULL);
    }
      break;

    case CODEC_ID_SHORTEN:
      caps = gst_caps_new_simple ("audio/x-shorten", NULL);
      break;

    case CODEC_ID_ALAC:
      caps = gst_ff_aud_caps_new (context, codec_id, "audio/x-alac", NULL);
      if (context) {
        gst_caps_set_simple (caps,
            "samplesize", G_TYPE_INT, context->bits_per_sample, NULL);
      }
      break;

    case CODEC_ID_FLAC:
      /* Note that ffmpeg has no encoder yet, but just for safety. In the
       * encoder case, we want to add things like samplerate, channels... */
      if (!encode) {
        caps = gst_caps_new_simple ("audio/x-flac", NULL);
      }
      break;

    case CODEC_ID_DVD_SUBTITLE:
    case CODEC_ID_DVB_SUBTITLE:
      caps = NULL;
      break;
    case CODEC_ID_BMP:
      caps = gst_caps_new_simple ("image/bmp", NULL);
      break;
    case CODEC_ID_TTA:
      caps = gst_ff_aud_caps_new (context, codec_id, "audio/x-tta", NULL);
      if (context) {
        gst_caps_set_simple (caps,
            "samplesize", G_TYPE_INT, context->bits_per_sample, NULL);
      }
      break;
    default:
      GST_DEBUG ("Unknown codec ID %d, please add mapping here", codec_id);
      break;
  }

  if (buildcaps) {
    AVCodec *codec;

    if ((codec = avcodec_find_decoder (codec_id)) ||
        (codec = avcodec_find_encoder (codec_id))) {
      gchar *mime = NULL;

      GST_LOG ("Could not create stream format caps for %s", codec->name);

      switch (codec->type) {
        case CODEC_TYPE_VIDEO:
          mime = g_strdup_printf ("video/x-gst_ff-%s", codec->name);
          caps = gst_ff_vid_caps_new (context, codec_id, mime, NULL);
          g_free (mime);
          break;
        case CODEC_TYPE_AUDIO:
          mime = g_strdup_printf ("audio/x-gst_ff-%s", codec->name);
          caps = gst_ff_aud_caps_new (context, codec_id, mime, NULL);
          if (context)
            gst_caps_set_simple (caps,
                "block_align", G_TYPE_INT, context->block_align,
                "bitrate", G_TYPE_INT, context->bit_rate, NULL);
          g_free (mime);
          break;
        default:
          break;
      }
    }
  }

  if (caps != NULL) {

    /* set private data */
    if (context && context->extradata_size > 0) {
      GstBuffer *data = gst_buffer_new_and_alloc (context->extradata_size);

      memcpy (GST_BUFFER_DATA (data), context->extradata,
          context->extradata_size);
      gst_caps_set_simple (caps, "codec_data", GST_TYPE_BUFFER, data, NULL);
      gst_buffer_unref (data);
    }

    /* palette */
    if (context) {
      gst_ffmpeg_set_palette (caps, context);
    }

    GST_LOG ("caps for codec_id=%d: %" GST_PTR_FORMAT, codec_id, caps);

  } else {
    GST_LOG ("No caps found for codec_id=%d", codec_id);
  }

  return caps;
}

/* Convert a FFMPEG Pixel Format and optional AVCodecContext
 * to a GstCaps. If the context is ommitted, no fixed values
 * for video/audio size will be included in the GstCaps
 *
 * See below for usefullness
 */

static GstCaps *
gst_ffmpeg_pixfmt_to_caps (enum PixelFormat pix_fmt, AVCodecContext * context,
    enum CodecID codec_id)
{
  GstCaps *caps = NULL;

  int bpp = 0, depth = 0, endianness = 0;
  gulong g_mask = 0, r_mask = 0, b_mask = 0, a_mask = 0;
  guint32 fmt = 0;

  switch (pix_fmt) {
    case PIX_FMT_YUVJ420P:
    case PIX_FMT_YUV420P:
      fmt = GST_MAKE_FOURCC ('I', '4', '2', '0');
      break;
    case PIX_FMT_YUV422:
      fmt = GST_MAKE_FOURCC ('Y', 'U', 'Y', '2');
      break;
    case PIX_FMT_RGB24:
      bpp = depth = 24;
      endianness = G_BIG_ENDIAN;
      r_mask = 0xff0000;
      g_mask = 0x00ff00;
      b_mask = 0x0000ff;
      break;
    case PIX_FMT_BGR24:
      bpp = depth = 24;
      endianness = G_BIG_ENDIAN;
      r_mask = 0x0000ff;
      g_mask = 0x00ff00;
      b_mask = 0xff0000;
      break;
    case PIX_FMT_YUVJ422P:
    case PIX_FMT_YUV422P:
      fmt = GST_MAKE_FOURCC ('Y', '4', '2', 'B');
      break;
    case PIX_FMT_YUVJ444P:
    case PIX_FMT_YUV444P:
      fmt = GST_MAKE_FOURCC ('Y', '4', '4', '4');
      break;
    case PIX_FMT_RGBA32:
      bpp = 32;
      depth = 32;
      endianness = G_BIG_ENDIAN;
#if (G_BYTE_ORDER == G_BIG_ENDIAN)
      r_mask = 0x00ff0000;
      g_mask = 0x0000ff00;
      b_mask = 0x000000ff;
      a_mask = 0xff000000;
#else
      r_mask = 0x0000ff00;
      g_mask = 0x00ff0000;
      b_mask = 0xff000000;
      a_mask = 0x000000ff;
#endif
      break;
    case PIX_FMT_YUV410P:
      fmt = GST_MAKE_FOURCC ('Y', 'U', 'V', '9');
      break;
    case PIX_FMT_YUV411P:
      fmt = GST_MAKE_FOURCC ('Y', '4', '1', 'B');
      break;
    case PIX_FMT_RGB565:
      bpp = depth = 16;
      endianness = G_BYTE_ORDER;
      r_mask = 0xf800;
      g_mask = 0x07e0;
      b_mask = 0x001f;
      break;
    case PIX_FMT_RGB555:
      bpp = 16;
      depth = 15;
      endianness = G_BYTE_ORDER;
      r_mask = 0x7c00;
      g_mask = 0x03e0;
      b_mask = 0x001f;
      break;
    case PIX_FMT_PAL8:
      bpp = depth = 8;
      endianness = G_BYTE_ORDER;
      break;
    case PIX_FMT_GRAY8:
      bpp = depth = 8;
      caps = gst_ff_vid_caps_new (context, codec_id, "video/x-raw-gray",
          "bpp", G_TYPE_INT, bpp, "depth", G_TYPE_INT, depth, NULL);
      break;
    default:
      /* give up ... */
      break;
  }

  if (caps == NULL) {
    if (bpp != 0) {
      if (r_mask != 0) {
	if (a_mask) {
          caps = gst_ff_vid_caps_new (context, codec_id, "video/x-raw-rgb",
            "bpp", G_TYPE_INT, bpp,
            "depth", G_TYPE_INT, depth,
            "red_mask", G_TYPE_INT, r_mask,
            "green_mask", G_TYPE_INT, g_mask,
            "blue_mask", G_TYPE_INT, b_mask,
            "alpha_mask", G_TYPE_INT, a_mask,
            "endianness", G_TYPE_INT, endianness, NULL);
	}
	else {
          caps = gst_ff_vid_caps_new (context, codec_id, "video/x-raw-rgb",
            "bpp", G_TYPE_INT, bpp,
            "depth", G_TYPE_INT, depth,
            "red_mask", G_TYPE_INT, r_mask,
            "green_mask", G_TYPE_INT, g_mask,
            "blue_mask", G_TYPE_INT, b_mask,
            "endianness", G_TYPE_INT, endianness, NULL);
	}
      } else {
        caps = gst_ff_vid_caps_new (context, codec_id, "video/x-raw-rgb",
            "bpp", G_TYPE_INT, bpp,
            "depth", G_TYPE_INT, depth,
            "endianness", G_TYPE_INT, endianness, NULL);
        if (caps && context) {
          gst_ffmpeg_set_palette (caps, context);
        }
      }
    } else if (fmt) {
      caps = gst_ff_vid_caps_new (context, codec_id, "video/x-raw-yuv",
          "format", GST_TYPE_FOURCC, fmt, NULL);
    }
  }

  if (caps != NULL) {
    char *str = gst_caps_to_string (caps);

    GST_DEBUG ("caps for pix_fmt=%d: %s", pix_fmt, str);
    g_free (str);
  } else {
    GST_LOG ("No caps found for pix_fmt=%d", pix_fmt);
  }

  return caps;
}

/* Convert a FFMPEG Sample Format and optional AVCodecContext
 * to a GstCaps. If the context is ommitted, no fixed values
 * for video/audio size will be included in the GstCaps
 *
 * See below for usefullness
 */

static GstCaps *
gst_ffmpeg_smpfmt_to_caps (enum SampleFormat sample_fmt,
    AVCodecContext * context, enum CodecID codec_id)
{
  GstCaps *caps = NULL;

  int bpp = 0;
  gboolean signedness = FALSE;

  switch (sample_fmt) {
    case SAMPLE_FMT_S16:
      signedness = TRUE;
      bpp = 16;
      break;

    default:
      /* .. */
      break;
  }

  if (bpp) {
    caps = gst_ff_aud_caps_new (context, codec_id, "audio/x-raw-int",
        "signed", G_TYPE_BOOLEAN, signedness,
        "endianness", G_TYPE_INT, G_BYTE_ORDER,
        "width", G_TYPE_INT, bpp, "depth", G_TYPE_INT, bpp, NULL);
  }

  if (caps != NULL) {
    char *str = gst_caps_to_string (caps);

    GST_LOG ("caps for sample_fmt=%d: %s", sample_fmt, str);
    g_free (str);
  } else {
    GST_LOG ("No caps found for sample_fmt=%d", sample_fmt);
  }

  return caps;
}

/* Convert a FFMPEG codec Type and optional AVCodecContext
 * to a GstCaps. If the context is ommitted, no fixed values
 * for video/audio size will be included in the GstCaps
 *
 * CodecType is primarily meant for uncompressed data GstCaps!
 */

GstCaps *
gst_ffmpeg_codectype_to_caps (enum CodecType codec_type,
    AVCodecContext * context, enum CodecID codec_id)
{
  GstCaps *caps;

  switch (codec_type) {
    case CODEC_TYPE_VIDEO:
      if (context) {
        caps = gst_ffmpeg_pixfmt_to_caps (context->pix_fmt,
            context->width == -1 ? NULL : context, codec_id);
      } else {
        GstCaps *temp;
        enum PixelFormat i;

        caps = gst_caps_new_empty ();
        for (i = 0; i < PIX_FMT_NB; i++) {
          temp = gst_ffmpeg_pixfmt_to_caps (i, NULL, codec_id);
          if (temp != NULL) {
            gst_caps_append (caps, temp);
          }
        }
      }
      break;

    case CODEC_TYPE_AUDIO:
      if (context) {
        caps =
            gst_ffmpeg_smpfmt_to_caps (context->sample_fmt, context, codec_id);
      } else {
        GstCaps *temp;
        enum SampleFormat i;

        caps = gst_caps_new_empty ();
        for (i = 0; i <= SAMPLE_FMT_S16; i++) {
          temp = gst_ffmpeg_smpfmt_to_caps (i, NULL, codec_id);
          if (temp != NULL) {
            gst_caps_append (caps, temp);
          }
        }
      }
      break;

    default:
      /* .. */
      caps = NULL;
      break;
  }

  return caps;
}

/* Convert a GstCaps (audio/raw) to a FFMPEG SampleFmt
 * and other audio properties in a AVCodecContext.
 *
 * For usefullness, see below
 */

static void
gst_ffmpeg_caps_to_smpfmt (const GstCaps * caps,
    AVCodecContext * context, gboolean raw)
{
  GstStructure *structure;
  gint depth = 0, width = 0, endianness = 0;
  gboolean signedness = FALSE;

  g_return_if_fail (gst_caps_get_size (caps) == 1);
  structure = gst_caps_get_structure (caps, 0);

  gst_structure_get_int (structure, "channels", &context->channels);
  gst_structure_get_int (structure, "rate", &context->sample_rate);
  gst_structure_get_int (structure, "block_align", &context->block_align);
  gst_structure_get_int (structure, "bitrate", &context->bit_rate);

  if (!raw)
    return;

  if (gst_structure_get_int (structure, "width", &width) &&
      gst_structure_get_int (structure, "depth", &depth) &&
      gst_structure_get_boolean (structure, "signed", &signedness) &&
      gst_structure_get_int (structure, "endianness", &endianness)) {
    if (width == 16 && depth == 16 &&
        endianness == G_BYTE_ORDER && signedness == TRUE) {
      context->sample_fmt = SAMPLE_FMT_S16;
    }
  }
}


/* Convert a GstCaps (video/raw) to a FFMPEG PixFmt
 * and other video properties in a AVCodecContext.
 *
 * For usefullness, see below
 */

static void
gst_ffmpeg_caps_to_pixfmt (const GstCaps * caps,
    AVCodecContext * context, gboolean raw)
{
  GstStructure *structure;
  const GValue *fps;
  const GValue *par = NULL;

  GST_DEBUG ("converting caps %" GST_PTR_FORMAT, caps);
  g_return_if_fail (gst_caps_get_size (caps) == 1);
  structure = gst_caps_get_structure (caps, 0);

  gst_structure_get_int (structure, "width", &context->width);
  gst_structure_get_int (structure, "height", &context->height);
  gst_structure_get_int (structure, "bpp", &context->bits_per_sample);

  fps = gst_structure_get_value (structure, "framerate");
  if (fps != NULL && GST_VALUE_HOLDS_FRACTION (fps)) {

    /* somehow these seem mixed up.. */
    context->time_base.den = gst_value_get_fraction_numerator (fps);
    context->time_base.num = gst_value_get_fraction_denominator (fps);

    GST_DEBUG ("setting framerate %d/%d = %lf",
        context->time_base.den, context->time_base.num,
        1. * context->time_base.den / context->time_base.num);
  }

  par = gst_structure_get_value (structure, "pixel-aspect-ratio");
  if (par && GST_VALUE_HOLDS_FRACTION (par)) {

    context->sample_aspect_ratio.num = gst_value_get_fraction_numerator (par);
    context->sample_aspect_ratio.den = gst_value_get_fraction_denominator (par);

    GST_DEBUG ("setting pixel-aspect-ratio %d/%d = %lf",
        context->sample_aspect_ratio.den, context->sample_aspect_ratio.num,
        1. * context->sample_aspect_ratio.den /
        context->sample_aspect_ratio.num);
  }

  if (!raw)
    return;

  g_return_if_fail (fps != NULL && GST_VALUE_HOLDS_FRACTION (fps));

  if (strcmp (gst_structure_get_name (structure), "video/x-raw-yuv") == 0) {
    guint32 fourcc;

    if (gst_structure_get_fourcc (structure, "format", &fourcc)) {
      switch (fourcc) {
        case GST_MAKE_FOURCC ('Y', 'U', 'Y', '2'):
          context->pix_fmt = PIX_FMT_YUV422;
          break;
        case GST_MAKE_FOURCC ('I', '4', '2', '0'):
          context->pix_fmt = PIX_FMT_YUV420P;
          break;
        case GST_MAKE_FOURCC ('Y', '4', '1', 'B'):
          context->pix_fmt = PIX_FMT_YUV411P;
          break;
        case GST_MAKE_FOURCC ('Y', '4', '2', 'B'):
          context->pix_fmt = PIX_FMT_YUV422P;
          break;
        case GST_MAKE_FOURCC ('Y', 'U', 'V', '9'):
          context->pix_fmt = PIX_FMT_YUV410P;
          break;
#if 0
        case FIXME:
          context->pix_fmt = PIX_FMT_YUV444P;
          break;
#endif
      }
    }
  } else if (strcmp (gst_structure_get_name (structure),
          "video/x-raw-rgb") == 0) {
    gint bpp = 0, rmask = 0, endianness = 0;

    if (gst_structure_get_int (structure, "bpp", &bpp) &&
        gst_structure_get_int (structure, "endianness", &endianness)) {
      if (gst_structure_get_int (structure, "red_mask", &rmask)) {
        switch (bpp) {
          case 32:
#if (G_BYTE_ORDER == G_BIG_ENDIAN)
            if (rmask == 0x00ff0000)
#else
            if (rmask == 0x0000ff00)
#endif
              context->pix_fmt = PIX_FMT_RGBA32;
            break;
          case 24:
            if (rmask == 0x0000FF)
              context->pix_fmt = PIX_FMT_BGR24;
            else
              context->pix_fmt = PIX_FMT_RGB24;
            break;
          case 16:
            if (endianness == G_BYTE_ORDER)
              context->pix_fmt = PIX_FMT_RGB565;
            break;
          case 15:
            if (endianness == G_BYTE_ORDER)
              context->pix_fmt = PIX_FMT_RGB555;
            break;
          default:
            /* nothing */
            break;
        }
      } else {
        if (bpp == 8) {
          context->pix_fmt = PIX_FMT_PAL8;
          gst_ffmpeg_get_palette (caps, context);
        }
      }
    }
  } else if (strcmp (gst_structure_get_name (structure),
          "video/x-raw-gray") == 0) {
    gint bpp = 0;

    if (gst_structure_get_int (structure, "bpp", &bpp)) {
      switch (bpp) {
        case 8:
          context->pix_fmt = PIX_FMT_GRAY8;
          break;
      }
    }
  }
}

/* Convert a GstCaps and a FFMPEG codec Type to a
 * AVCodecContext. If the context is ommitted, no fixed values
 * for video/audio size will be included in the context
 *
 * CodecType is primarily meant for uncompressed data GstCaps!
 */

void
gst_ffmpeg_caps_with_codectype (enum CodecType type,
    const GstCaps * caps, AVCodecContext * context)
{
  if (context == NULL)
    return;

  switch (type) {
    case CODEC_TYPE_VIDEO:
      gst_ffmpeg_caps_to_pixfmt (caps, context, TRUE);
      break;

    case CODEC_TYPE_AUDIO:
      gst_ffmpeg_caps_to_smpfmt (caps, context, TRUE);
      break;

    default:
      /* unknown */
      break;
  }
}

/*
 * caps_with_codecid () transforms a GstCaps for a known codec
 * ID into a filled-in context.
 * codec_data from caps will override possible extradata already in the context
 */

void
gst_ffmpeg_caps_with_codecid (enum CodecID codec_id,
    enum CodecType codec_type, const GstCaps * caps, AVCodecContext * context)
{
  GstStructure *str;
  const GValue *value;
  const GstBuffer *buf;

  if (!context || !gst_caps_get_size (caps))
    return;

  str = gst_caps_get_structure (caps, 0);

  /* extradata parsing (esds [mpeg4], wma/wmv, msmpeg4v1/2/3, etc.) */
  if ((value = gst_structure_get_value (str, "codec_data"))) {
    gint size;

    buf = GST_BUFFER_CAST (gst_value_get_mini_object (value));
    size = GST_BUFFER_SIZE (buf);

    /* free the old one if it is there */
    if (context->extradata)
      av_free (context->extradata);

    /* allocate with enough padding */
    context->extradata =
        av_mallocz (GST_ROUND_UP_16 (size + FF_INPUT_BUFFER_PADDING_SIZE));
    memcpy (context->extradata, GST_BUFFER_DATA (buf), size);
    context->extradata_size = size;
    GST_DEBUG ("have codec data of size %d", size);
  } else if (context->extradata == NULL) {
    /* no extradata, alloc dummy with 0 sized, some codecs insist on reading
     * extradata anyway which makes then segfault. */
    context->extradata =
        av_mallocz (GST_ROUND_UP_16 (FF_INPUT_BUFFER_PADDING_SIZE));
    context->extradata_size = 0;
    GST_DEBUG ("no codec data");
  }

  switch (codec_id) {
    case CODEC_ID_MPEG4:
    {
      const gchar *mime = gst_structure_get_name (str);

      if (!strcmp (mime, "video/x-divx"))
        context->codec_tag = GST_MAKE_FOURCC ('D', 'I', 'V', 'X');
      else if (!strcmp (mime, "video/x-xvid"))
        context->codec_tag = GST_MAKE_FOURCC ('X', 'V', 'I', 'D');
      else if (!strcmp (mime, "video/x-3ivx"))
        context->codec_tag = GST_MAKE_FOURCC ('3', 'I', 'V', '1');
      else if (!strcmp (mime, "video/mpeg"))
        context->codec_tag = GST_MAKE_FOURCC ('m', 'p', '4', 'v');
    }
      break;

    case CODEC_ID_SVQ3:
      /* FIXME: this is a workaround for older gst-plugins releases
       * (<= 0.8.9). This should be removed at some point, because
       * it causes wrong decoded frame order. */
      if (!context->extradata) {
        gint halfpel_flag, thirdpel_flag, low_delay, unknown_svq3_flag;
        guint16 flags;

        if (gst_structure_get_int (str, "halfpel_flag", &halfpel_flag) ||
            gst_structure_get_int (str, "thirdpel_flag", &thirdpel_flag) ||
            gst_structure_get_int (str, "low_delay", &low_delay) ||
            gst_structure_get_int (str, "unknown_svq3_flag",
                &unknown_svq3_flag)) {
          context->extradata = (guint8 *) av_mallocz (0x64);
          g_stpcpy ((gchar *) context->extradata, "SVQ3");
          flags = 1 << 3;
          flags |= low_delay;
          flags = flags << 2;
          flags |= unknown_svq3_flag;
          flags = flags << 6;
          flags |= halfpel_flag;
          flags = flags << 1;
          flags |= thirdpel_flag;
          flags = flags << 3;

          flags = GUINT16_FROM_LE (flags);

          memcpy ((gchar *) context->extradata + 0x62, &flags, 2);
          context->extradata_size = 0x64;
        }
      }
      break;

    case CODEC_ID_MSRLE:
    case CODEC_ID_QTRLE:
    case CODEC_ID_TSCC:
    case CODEC_ID_APE:
    {
      gint depth;

      if (gst_structure_get_int (str, "depth", &depth)) {
        context->bits_per_sample = depth;
      } else {
        GST_WARNING ("No depth field in caps %" GST_PTR_FORMAT, caps);
      }

    }
      break;

    case CODEC_ID_RV10:
    case CODEC_ID_RV20:
    case CODEC_ID_RV30:
    case CODEC_ID_RV40:
    {
      gint format;

      if (gst_structure_get_int (str, "format", &format))
        context->sub_id = format;

      break;
    }
    case CODEC_ID_COOK:
    case CODEC_ID_RA_288:
    case CODEC_ID_RA_144:
    {
      gint leaf_size;
      gint bitrate;

      if (gst_structure_get_int (str, "leaf_size", &leaf_size))
        context->block_align = leaf_size;
      if (gst_structure_get_int (str, "bitrate", &bitrate))
        context->bit_rate = bitrate;
    }
    case CODEC_ID_ALAC:
      gst_structure_get_int (str, "samplesize", &context->bits_per_sample);
      break;

    case CODEC_ID_DVVIDEO:
    {
      guint32 fourcc;

      if (gst_structure_get_fourcc (str, "format", &fourcc))
        switch (fourcc) {
          case GST_MAKE_FOURCC ('Y', 'U', 'Y', '2'):
            context->pix_fmt = PIX_FMT_YUV422;
            break;
          case GST_MAKE_FOURCC ('I', '4', '2', '0'):
            context->pix_fmt = PIX_FMT_YUV420P;
            break;
          case GST_MAKE_FOURCC ('Y', '4', '1', 'B'):
            context->pix_fmt = PIX_FMT_YUV411P;
            break;
          case GST_MAKE_FOURCC ('Y', '4', '2', 'B'):
            context->pix_fmt = PIX_FMT_YUV422P;
            break;
          case GST_MAKE_FOURCC ('Y', 'U', 'V', '9'):
            context->pix_fmt = PIX_FMT_YUV410P;
            break;
          default:
            GST_WARNING ("couldn't convert fourcc %" GST_FOURCC_FORMAT
                " to a pixel format", GST_FOURCC_ARGS (fourcc));
            break;
        }
    }
    default:
      break;
  }

  if (!gst_caps_is_fixed (caps))
    return;

  /* common properties (width, height, fps) */
  switch (codec_type) {
    case CODEC_TYPE_VIDEO:
      gst_ffmpeg_caps_to_pixfmt (caps, context, codec_id == CODEC_ID_RAWVIDEO);
      gst_ffmpeg_get_palette (caps, context);
      break;
    case CODEC_TYPE_AUDIO:
      gst_ffmpeg_caps_to_smpfmt (caps, context, FALSE);
      break;
    default:
      break;
  }
}

/* _formatid_to_caps () is meant for muxers/demuxers, it
 * transforms a name (ffmpeg way of ID'ing these, why don't
 * they have unique numerical IDs?) to the corresponding
 * caps belonging to that mux-format
 *
 * Note: we don't need any additional info because the caps
 * isn't supposed to contain any useful info besides the
 * media type anyway
 */

GstCaps *
gst_ffmpeg_formatid_to_caps (const gchar * format_name)
{
  GstCaps *caps = NULL;

  if (!strcmp (format_name, "mpeg")) {
    caps = gst_caps_new_simple ("video/mpeg",
        "systemstream", G_TYPE_BOOLEAN, TRUE, NULL);
  } else if (!strcmp (format_name, "mpegts")) {
    caps = gst_caps_new_simple ("video/mpegts",
        "systemstream", G_TYPE_BOOLEAN, TRUE, NULL);
  } else if (!strcmp (format_name, "rm")) {
    caps = gst_caps_new_simple ("application/x-pn-realmedia",
        "systemstream", G_TYPE_BOOLEAN, TRUE, NULL);
  } else if (!strcmp (format_name, "asf")) {
    caps = gst_caps_new_simple ("video/x-ms-asf", NULL);
  } else if (!strcmp (format_name, "avi")) {
    caps = gst_caps_new_simple ("video/x-msvideo", NULL);
  } else if (!strcmp (format_name, "wav")) {
    caps = gst_caps_new_simple ("audio/x-wav", NULL);
  } else if (!strcmp (format_name, "ape")) {
    caps = gst_caps_new_simple ("application/x-ape", NULL);
  } else if (!strcmp (format_name, "swf")) {
    caps = gst_caps_new_simple ("application/x-shockwave-flash", NULL);
  } else if (!strcmp (format_name, "au")) {
    caps = gst_caps_new_simple ("audio/x-au", NULL);
  } else if (!strcmp (format_name, "dv")) {
    caps = gst_caps_new_simple ("video/x-dv",
        "systemstream", G_TYPE_BOOLEAN, TRUE, NULL);
  } else if (!strcmp (format_name, "4xm")) {
    caps = gst_caps_new_simple ("video/x-4xm", NULL);
  } else if (!strcmp (format_name, "matroska")) {
    caps = gst_caps_new_simple ("video/x-matroska", NULL);
  } else if (!strcmp (format_name, "mp3")) {
    caps = gst_caps_new_simple ("application/x-id3", NULL);
  } else if (!strcmp (format_name, "flic")) {
    caps = gst_caps_new_simple ("video/x-fli", NULL);
  } else if (!strcmp (format_name, "flv")) {
    caps = gst_caps_new_simple ("video/x-flv", NULL);
  } else if (!strcmp (format_name, "tta")) {
    caps = gst_caps_new_simple ("audio/x-ttafile", NULL);
  } else if (!strcmp (format_name, "aiff")) {
    caps = gst_caps_new_simple ("audio/x-aiff", NULL);
  } else if (!strcmp (format_name, "mov_mp4_m4a_3gp_3g2")) {
    caps =
        gst_caps_from_string
        ("application/x-3gp; video/quicktime; audio/x-m4a");
  } else if (!strcmp (format_name, "mov")) {
    caps = gst_caps_new_simple ("video/quicktime", NULL);
  } else if (!strcmp (format_name, "mp4")) {
    caps = gst_caps_new_simple ("video/quicktime", NULL);
  } else if ((!strcmp (format_name, "3gp")) || (!strcmp (format_name, "3gp2"))) {
    caps = gst_caps_new_simple ("application/x-3gp", NULL);
  } else if (!strcmp (format_name, "aac")) {
    caps = gst_caps_new_simple ("audio/mpeg",
        "mpegversion", G_TYPE_INT, 4, NULL);
  } else if (!strcmp (format_name, "gif")) {
    caps = gst_caps_from_string ("image/gif");
  } else if (!strcmp (format_name, "ogg")) {
    caps = gst_caps_from_string ("application/ogg");
  } else if (!strcmp (format_name, "mxf")) {
    caps = gst_caps_from_string ("application/mxf");
  } else if (!strcmp (format_name, "gxf")) {
    caps = gst_caps_from_string ("application/gxf");
  } else if (!strcmp (format_name, "yuv4mpegpipe")) {
    caps = gst_caps_new_simple ("application/x-yuv4mpeg",
        "y4mversion", G_TYPE_INT, 2, NULL);
  } else if (!strcmp (format_name, "mpc")) {
    caps = gst_caps_from_string ("audio/x-musepack, streamversion = (int) 7");
  } else {
    gchar *name;

    GST_LOG ("Could not create stream format caps for %s", format_name);
    name = g_strdup_printf ("application/x-gst_ff-%s", format_name);
    caps = gst_caps_new_simple (name, NULL);
    g_free (name);
  }

  return caps;
}

gboolean
gst_ffmpeg_formatid_get_codecids (const gchar * format_name,
    enum CodecID ** video_codec_list, enum CodecID ** audio_codec_list)
{

  GST_LOG ("format_name : %s", format_name);

  if (!strcmp (format_name, "mp4")) {
    static enum CodecID mp4_video_list[] = {
      CODEC_ID_MPEG4, CODEC_ID_H264,
      CODEC_ID_MJPEG,
      CODEC_ID_NONE
    };
    static enum CodecID mp4_audio_list[] = {
      CODEC_ID_AAC, CODEC_ID_MP3,
      CODEC_ID_NONE
    };

    *video_codec_list = mp4_video_list;
    *audio_codec_list = mp4_audio_list;
  } else if (!strcmp (format_name, "mpeg")) {
    static enum CodecID mpeg_video_list[] = { CODEC_ID_MPEG1VIDEO,
      CODEC_ID_MPEG2VIDEO,
      CODEC_ID_H264,
      CODEC_ID_NONE
    };
    static enum CodecID mpeg_audio_list[] = { CODEC_ID_MP2,
      CODEC_ID_MP3,
      CODEC_ID_NONE
    };

    *video_codec_list = mpeg_video_list;
    *audio_codec_list = mpeg_audio_list;
  } else if (!strcmp (format_name, "mpegts")) {
    static enum CodecID mpegts_video_list[] = { CODEC_ID_MPEG1VIDEO,
      CODEC_ID_MPEG2VIDEO,
      CODEC_ID_H264,
      CODEC_ID_NONE
    };
    static enum CodecID mpegts_audio_list[] = { CODEC_ID_MP2,
      CODEC_ID_MP3,
      CODEC_ID_AC3,
      CODEC_ID_DTS,
      CODEC_ID_AAC,
      CODEC_ID_NONE
    };

    *video_codec_list = mpegts_video_list;
    *audio_codec_list = mpegts_audio_list;
  } else if (!strcmp (format_name, "vob")) {
    static enum CodecID vob_video_list[] =
        { CODEC_ID_MPEG2VIDEO, CODEC_ID_NONE };
    static enum CodecID vob_audio_list[] = { CODEC_ID_MP2, CODEC_ID_AC3,
      CODEC_ID_DTS, CODEC_ID_NONE
    };

    *video_codec_list = vob_video_list;
    *audio_codec_list = vob_audio_list;
  } else if (!strcmp (format_name, "flv")) {
/* WHISPERCAST ORIGINAL: Add H.264 to FLV (it is actually supported by ffmpeg)
    static enum CodecID flv_video_list[] = { CODEC_ID_FLV1, CODEC_ID_NONE };
*/
/* WHISPERCAST BEGIN: Add H.264 to FLV (it is actually supported by ffmpeg) */
    static enum CodecID flv_video_list[] =
      { CODEC_ID_FLV1, CODEC_ID_H264, CODEC_ID_NONE };
/* WHISPERCAST END: Add H.264 to FLV (it is actually supported by ffmpeg) */
/* WHISPERCAST ORIGINAL: Add AAC to FLV (it is actually supported by ffmpeg)
    static enum CodecID flv_audio_list[] = { CODEC_ID_MP3, CODEC_ID_NONE };
*/
    static enum CodecID flv_audio_list[] =
      { CODEC_ID_MP3, CODEC_ID_AAC, CODEC_ID_NONE };

    *video_codec_list = flv_video_list;
    *audio_codec_list = flv_audio_list;
  } else if (!strcmp (format_name, "asf")) {
    static enum CodecID asf_video_list[] =
        { CODEC_ID_WMV1, CODEC_ID_WMV2, CODEC_ID_MSMPEG4V3, CODEC_ID_NONE };
    static enum CodecID asf_audio_list[] =
        { CODEC_ID_WMAV1, CODEC_ID_WMAV2, CODEC_ID_MP3, CODEC_ID_NONE };

    *video_codec_list = asf_video_list;
    *audio_codec_list = asf_audio_list;
  } else if (!strcmp (format_name, "dv")) {
    static enum CodecID dv_video_list[] = { CODEC_ID_DVVIDEO, CODEC_ID_NONE };
    static enum CodecID dv_audio_list[] = { CODEC_ID_PCM_S16LE, CODEC_ID_NONE };

    *video_codec_list = dv_video_list;
    *audio_codec_list = dv_audio_list;
  } else if (!strcmp (format_name, "mov")) {
    static enum CodecID mov_video_list[] = {
      CODEC_ID_SVQ1, CODEC_ID_SVQ3, CODEC_ID_MPEG4,
      CODEC_ID_H263, CODEC_ID_H263P,
      CODEC_ID_H264, CODEC_ID_DVVIDEO,
      CODEC_ID_MJPEG,
      CODEC_ID_NONE
    };
    static enum CodecID mov_audio_list[] = {
      CODEC_ID_PCM_MULAW, CODEC_ID_PCM_ALAW, CODEC_ID_ADPCM_IMA_QT,
      CODEC_ID_MACE3, CODEC_ID_MACE6, CODEC_ID_AAC,
      CODEC_ID_AMR_NB, CODEC_ID_AMR_WB,
      CODEC_ID_PCM_S16BE, CODEC_ID_PCM_S16LE,
      CODEC_ID_MP3, CODEC_ID_NONE
    };

    *video_codec_list = mov_video_list;
    *audio_codec_list = mov_audio_list;
  } else if ((!strcmp (format_name, "3gp") || !strcmp (format_name, "3g2"))) {
    static enum CodecID tgp_video_list[] = {
      CODEC_ID_MPEG4, CODEC_ID_H263, CODEC_ID_H263P, CODEC_ID_H264,
      CODEC_ID_NONE
    };
    static enum CodecID tgp_audio_list[] = {
      CODEC_ID_AMR_NB, CODEC_ID_AMR_WB,
      CODEC_ID_AAC,
      CODEC_ID_NONE
    };

    *video_codec_list = tgp_video_list;
    *audio_codec_list = tgp_audio_list;
  } else if (!strcmp (format_name, "mmf")) {
    static enum CodecID mmf_audio_list[] = {
      CODEC_ID_ADPCM_YAMAHA, CODEC_ID_NONE
    };
    *video_codec_list = NULL;
    *audio_codec_list = mmf_audio_list;
  } else if (!strcmp (format_name, "amr")) {
    static enum CodecID amr_audio_list[] = {
      CODEC_ID_AMR_NB, CODEC_ID_AMR_WB,
      CODEC_ID_NONE
    };
    *video_codec_list = NULL;
    *audio_codec_list = amr_audio_list;
  } else if (!strcmp (format_name, "gif")) {
    static enum CodecID gif_image_list[] = {
      CODEC_ID_RAWVIDEO, CODEC_ID_NONE
    };
    *video_codec_list = gif_image_list;
    *audio_codec_list = NULL;
  } else {
    GST_LOG ("Format %s not found", format_name);
    return FALSE;
  }

  return TRUE;
}

/* Convert a GstCaps to a FFMPEG codec ID. Size et all
 * are omitted, that can be queried by the user itself,
 * we're not eating the GstCaps or anything
 * A pointer to an allocated context is also needed for
 * optional extra info
 */

enum CodecID
gst_ffmpeg_caps_to_codecid (const GstCaps * caps, AVCodecContext * context)
{
  enum CodecID id = CODEC_ID_NONE;
  const gchar *mimetype;
  const GstStructure *structure;
  gboolean video = FALSE, audio = FALSE;        /* we want to be sure! */

  g_return_val_if_fail (caps != NULL, CODEC_ID_NONE);
  g_return_val_if_fail (gst_caps_get_size (caps) == 1, CODEC_ID_NONE);
  structure = gst_caps_get_structure (caps, 0);

  mimetype = gst_structure_get_name (structure);

  if (!strcmp (mimetype, "video/x-raw-rgb") ||
      !strcmp (mimetype, "video/x-raw-yuv")) {
    id = CODEC_ID_RAWVIDEO;
    video = TRUE;
  } else if (!strcmp (mimetype, "audio/x-raw-int")) {
    gint depth, width, endianness;
    gboolean signedness;

    if (gst_structure_get_int (structure, "endianness", &endianness) &&
        gst_structure_get_boolean (structure, "signed", &signedness) &&
        gst_structure_get_int (structure, "width", &width) &&
        gst_structure_get_int (structure, "depth", &depth) && depth == width) {
      switch (depth) {
        case 8:
          if (signedness) {
            id = CODEC_ID_PCM_S8;
          } else {
            id = CODEC_ID_PCM_U8;
          }
          break;
        case 16:
          switch (endianness) {
            case G_BIG_ENDIAN:
              if (signedness) {
                id = CODEC_ID_PCM_S16BE;
              } else {
                id = CODEC_ID_PCM_U16BE;
              }
              break;
            case G_LITTLE_ENDIAN:
              if (signedness) {
                id = CODEC_ID_PCM_S16LE;
              } else {
                id = CODEC_ID_PCM_U16LE;
              }
              break;
          }
          break;
      }
      if (id != CODEC_ID_NONE)
        audio = TRUE;
    }
  } else if (!strcmp (mimetype, "audio/x-mulaw")) {
    id = CODEC_ID_PCM_MULAW;
    audio = TRUE;
  } else if (!strcmp (mimetype, "audio/x-alaw")) {
    id = CODEC_ID_PCM_ALAW;
    audio = TRUE;
  } else if (!strcmp (mimetype, "video/x-dv")) {
    gboolean sys_strm;

    if (gst_structure_get_boolean (structure, "systemstream", &sys_strm) &&
        !sys_strm) {
      id = CODEC_ID_DVVIDEO;
      video = TRUE;
    }
  } else if (!strcmp (mimetype, "audio/x-dv")) {        /* ??? */
    id = CODEC_ID_DVAUDIO;
    audio = TRUE;
  } else if (!strcmp (mimetype, "video/x-h263")) {
    const gchar *h263version =
        gst_structure_get_string (structure, "h263version");
    if (h263version && !strcmp (h263version, "h263p"))
      id = CODEC_ID_H263P;
    else
      id = CODEC_ID_H263;
    video = TRUE;
  } else if (!strcmp (mimetype, "video/x-intel-h263")) {
    id = CODEC_ID_H263I;
    video = TRUE;
  } else if (!strcmp (mimetype, "video/x-h261")) {
    id = CODEC_ID_H261;
    video = TRUE;
  } else if (!strcmp (mimetype, "video/mpeg")) {
    gboolean sys_strm;
    gint mpegversion;

    if (gst_structure_get_boolean (structure, "systemstream", &sys_strm) &&
        gst_structure_get_int (structure, "mpegversion", &mpegversion) &&
        !sys_strm) {
      switch (mpegversion) {
        case 1:
          id = CODEC_ID_MPEG1VIDEO;
          break;
        case 2:
          id = CODEC_ID_MPEG2VIDEO;
          break;
        case 4:
          id = CODEC_ID_MPEG4;
          break;
      }
    }
    if (id != CODEC_ID_NONE)
      video = TRUE;
  } else if (!strcmp (mimetype, "image/jpeg")) {
    id = CODEC_ID_MJPEG;        /* A... B... */
    video = TRUE;
  } else if (!strcmp (mimetype, "video/x-jpeg-b")) {
    id = CODEC_ID_MJPEGB;
    video = TRUE;
  } else if (!strcmp (mimetype, "video/x-wmv")) {
    gint wmvversion = 0;

    if (gst_structure_get_int (structure, "wmvversion", &wmvversion)) {
      switch (wmvversion) {
        case 1:
          id = CODEC_ID_WMV1;
          break;
        case 2:
          id = CODEC_ID_WMV2;
          break;
        case 3:
        {
          guint32 fourcc;

          if (gst_structure_get_fourcc (structure, "fourcc", &fourcc)) {
            if (fourcc == GST_MAKE_FOURCC ('W', 'V', 'C', '1'))
              id = CODEC_ID_VC1;
          } else
            id = CODEC_ID_WMV3;
        }
          break;
      }
    }
    if (id != CODEC_ID_NONE)
      video = TRUE;
  } else if (!strcmp (mimetype, "audio/x-vorbis")) {
    id = CODEC_ID_VORBIS;
    audio = TRUE;
  } else if (!strcmp (mimetype, "audio/x-qdm2")) {
    id = CODEC_ID_QDM2;
    audio = TRUE;
  } else if (!strcmp (mimetype, "audio/mpeg")) {
    gint layer = 0;
    gint mpegversion = 0;

    if (gst_structure_get_int (structure, "mpegversion", &mpegversion)) {
      switch (mpegversion) {
        case 2:                /* ffmpeg uses faad for both... */
        case 4:
          id = CODEC_ID_AAC;
          break;
        case 1:
          if (gst_structure_get_int (structure, "layer", &layer)) {
            switch (layer) {
              case 1:
              case 2:
                id = CODEC_ID_MP2;
                break;
              case 3:
                id = CODEC_ID_MP3;
                break;
            }
          }
      }
    }
    if (id != CODEC_ID_NONE)
      audio = TRUE;
  } else if (!strcmp (mimetype, "audio/x-musepack")) {
    gint streamversion = -1;

    if (gst_structure_get_int (structure, "streamversion", &streamversion)) {
      if (streamversion == 7)
        id = CODEC_ID_MUSEPACK7;
    } else {
      id = CODEC_ID_MUSEPACK7;
    }
  } else if (!strcmp (mimetype, "audio/x-wma")) {
    gint wmaversion = 0;

    if (gst_structure_get_int (structure, "wmaversion", &wmaversion)) {
      switch (wmaversion) {
        case 1:
          id = CODEC_ID_WMAV1;
          break;
        case 2:
          id = CODEC_ID_WMAV2;
          break;
      }
    }
    if (id != CODEC_ID_NONE)
      audio = TRUE;
  } else if (!strcmp (mimetype, "audio/x-ac3")) {
    id = CODEC_ID_AC3;
    audio = TRUE;
  } else if (!strcmp (mimetype, "audio/atrac3")) {
    id = CODEC_ID_ATRAC3;
    audio = TRUE;
  } else if (!strcmp (mimetype, "audio/x-dts")) {
    id = CODEC_ID_DTS;
    audio = TRUE;
  } else if (!strcmp (mimetype, "application/x-ape")) {
    id = CODEC_ID_APE;
    audio = TRUE;
  } else if (!strcmp (mimetype, "video/x-msmpeg")) {
    gint msmpegversion = 0;

    if (gst_structure_get_int (structure, "msmpegversion", &msmpegversion)) {
      switch (msmpegversion) {
        case 41:
          id = CODEC_ID_MSMPEG4V1;
          break;
        case 42:
          id = CODEC_ID_MSMPEG4V2;
          break;
        case 43:
          id = CODEC_ID_MSMPEG4V3;
          break;
      }
    }
    if (id != CODEC_ID_NONE)
      video = TRUE;
  } else if (!strcmp (mimetype, "video/x-svq")) {
    gint svqversion = 0;

    if (gst_structure_get_int (structure, "svqversion", &svqversion)) {
      switch (svqversion) {
        case 1:
          id = CODEC_ID_SVQ1;
          break;
        case 3:
          id = CODEC_ID_SVQ3;
          break;
      }
    }
    if (id != CODEC_ID_NONE)
      video = TRUE;
  } else if (!strcmp (mimetype, "video/x-huffyuv")) {
    id = CODEC_ID_HUFFYUV;
    video = TRUE;
  } else if (!strcmp (mimetype, "audio/x-mace")) {
    gint maceversion = 0;

    if (gst_structure_get_int (structure, "maceversion", &maceversion)) {
      switch (maceversion) {
        case 3:
          id = CODEC_ID_MACE3;
          break;
        case 6:
          id = CODEC_ID_MACE6;
          break;
      }
    }
    if (id != CODEC_ID_NONE)
      audio = TRUE;
  } else if (!strcmp (mimetype, "video/x-theora")) {
    id = CODEC_ID_THEORA;
    video = TRUE;
  } else if (!strcmp (mimetype, "video/x-vp3")) {
    id = CODEC_ID_VP3;
    video = TRUE;
  } else if (!strcmp (mimetype, "video/x-vp5")) {
    id = CODEC_ID_VP5;
    video = TRUE;
  } else if (!strcmp (mimetype, "video/x-vp6")) {
    id = CODEC_ID_VP6;
    video = TRUE;
  } else if (!strcmp (mimetype, "video/x-vp6-flash")) {
    id = CODEC_ID_VP6F;
    video = TRUE;
  } else if (!strcmp (mimetype, "video/x-vp6-alpha")) {
    id = CODEC_ID_VP6A;
    video = TRUE;
  } else if (!strcmp (mimetype, "video/x-flash-screen")) {
    id = CODEC_ID_FLASHSV;
    video = TRUE;
  } else if (!strcmp (mimetype, "video/x-indeo")) {
    gint indeoversion = 0;

    if (gst_structure_get_int (structure, "indeoversion", &indeoversion)) {
      switch (indeoversion) {
        case 3:
          id = CODEC_ID_INDEO3;
          break;
        case 2:
          id = CODEC_ID_INDEO2;
          break;
      }
      if (id != CODEC_ID_NONE)
        video = TRUE;
    }
  } else if (!strcmp (mimetype, "video/x-divx")) {
    gint divxversion = 0;

    if (gst_structure_get_int (structure, "divxversion", &divxversion)) {
      switch (divxversion) {
        case 3:
          id = CODEC_ID_MSMPEG4V3;
          break;
        case 4:
        case 5:
          id = CODEC_ID_MPEG4;
          break;
      }
    }
    if (id != CODEC_ID_NONE)
      video = TRUE;
  } else if (!strcmp (mimetype, "video/x-3ivx")) {
    id = CODEC_ID_MPEG4;
    video = TRUE;
  } else if (!strcmp (mimetype, "video/x-xvid")) {
    id = CODEC_ID_MPEG4;
    video = TRUE;
  } else if (!strcmp (mimetype, "video/x-ffv")) {
    gint ffvversion = 0;

    if (gst_structure_get_int (structure, "ffvversion", &ffvversion) &&
        ffvversion == 1) {
      id = CODEC_ID_FFV1;
      video = TRUE;
    }
  } else if (!strcmp (mimetype, "audio/x-adpcm")) {
    const gchar *layout;

    layout = gst_structure_get_string (structure, "layout");
    if (layout == NULL) {
      /* break */
    } else if (!strcmp (layout, "quicktime")) {
      id = CODEC_ID_ADPCM_IMA_QT;
    } else if (!strcmp (layout, "microsoft")) {
      id = CODEC_ID_ADPCM_MS;
    } else if (!strcmp (layout, "dvi")) {
      id = CODEC_ID_ADPCM_IMA_WAV;
    } else if (!strcmp (layout, "4xm")) {
      id = CODEC_ID_ADPCM_4XM;
    } else if (!strcmp (layout, "smjpeg")) {
      id = CODEC_ID_ADPCM_IMA_SMJPEG;
    } else if (!strcmp (layout, "dk3")) {
      id = CODEC_ID_ADPCM_IMA_DK3;
    } else if (!strcmp (layout, "dk4")) {
      id = CODEC_ID_ADPCM_IMA_DK4;
    } else if (!strcmp (layout, "westwood")) {
      id = CODEC_ID_ADPCM_IMA_WS;
    } else if (!strcmp (layout, "xa")) {
      id = CODEC_ID_ADPCM_XA;
    } else if (!strcmp (layout, "adx")) {
      id = CODEC_ID_ADPCM_ADX;
    } else if (!strcmp (layout, "ea")) {
      id = CODEC_ID_ADPCM_EA;
    } else if (!strcmp (layout, "g726")) {
      id = CODEC_ID_ADPCM_G726;
    } else if (!strcmp (layout, "ct")) {
      id = CODEC_ID_ADPCM_CT;
    } else if (!strcmp (layout, "swf")) {
      id = CODEC_ID_ADPCM_SWF;
    } else if (!strcmp (layout, "yamaha")) {
      id = CODEC_ID_ADPCM_YAMAHA;
    } else if (!strcmp (layout, "sbpro2")) {
      id = CODEC_ID_ADPCM_SBPRO_2;
    } else if (!strcmp (layout, "sbpro3")) {
      id = CODEC_ID_ADPCM_SBPRO_3;
    } else if (!strcmp (layout, "sbpro4")) {
      id = CODEC_ID_ADPCM_SBPRO_4;
    }
    if (id != CODEC_ID_NONE)
      audio = TRUE;
  } else if (!strcmp (mimetype, "video/x-4xm")) {
    id = CODEC_ID_4XM;
    video = TRUE;
  } else if (!strcmp (mimetype, "audio/x-dpcm")) {
    const gchar *layout;

    layout = gst_structure_get_string (structure, "layout");
    if (!layout) {
      /* .. */
    } else if (!strcmp (layout, "roq")) {
      id = CODEC_ID_ROQ_DPCM;
    } else if (!strcmp (layout, "interplay")) {
      id = CODEC_ID_INTERPLAY_DPCM;
    } else if (!strcmp (layout, "xan")) {
      id = CODEC_ID_XAN_DPCM;
    } else if (!strcmp (layout, "sol")) {
      id = CODEC_ID_SOL_DPCM;
    }
    if (id != CODEC_ID_NONE)
      audio = TRUE;
  } else if (!strcmp (mimetype, "audio/x-flac")) {
    id = CODEC_ID_FLAC;
    audio = TRUE;
  } else if (!strcmp (mimetype, "audio/x-shorten")) {
    id = CODEC_ID_SHORTEN;
    audio = TRUE;
  } else if (!strcmp (mimetype, "audio/x-alac")) {
    id = CODEC_ID_ALAC;
    audio = TRUE;
  } else if (!strcmp (mimetype, "video/x-cinepak")) {
    id = CODEC_ID_CINEPAK;
    video = TRUE;
  } else if (!strcmp (mimetype, "video/x-pn-realvideo")) {
    gint rmversion;

    if (gst_structure_get_int (structure, "rmversion", &rmversion)) {
      switch (rmversion) {
        case 1:
          id = CODEC_ID_RV10;
          break;
        case 2:
          id = CODEC_ID_RV20;
          break;
        case 4:
          id = CODEC_ID_RV40;
          break;
      }
    }
    if (id != CODEC_ID_NONE)
      video = TRUE;
  } else if (!strcmp (mimetype, "audio/x-pn-realaudio")) {
    gint raversion;

    if (gst_structure_get_int (structure, "raversion", &raversion)) {
      switch (raversion) {
        case 1:
          id = CODEC_ID_RA_144;
          break;
        case 2:
          id = CODEC_ID_RA_288;
          break;
        case 8:
          id = CODEC_ID_COOK;
          break;
      }
    }
    if (id != CODEC_ID_NONE)
      audio = TRUE;
  } else if (!strcmp (mimetype, "video/x-rle")) {
    const gchar *layout;

    if ((layout = gst_structure_get_string (structure, "layout"))) {
      if (!strcmp (layout, "microsoft")) {
        id = CODEC_ID_MSRLE;
        video = TRUE;
      }
    }
  } else if (!strcmp (mimetype, "video/x-xan")) {
    gint wcversion = 0;

    if ((gst_structure_get_int (structure, "wcversion", &wcversion))) {
      switch (wcversion) {
        case 3:
          id = CODEC_ID_XAN_WC3;
          video = TRUE;
          break;
        case 4:
          id = CODEC_ID_XAN_WC4;
          video = TRUE;
          break;
        default:
          break;
      }
    }
  } else if (!strcmp (mimetype, "audio/AMR")) {
    audio = TRUE;
    id = CODEC_ID_AMR_NB;
  } else if (!strcmp (mimetype, "audio/AMR-WB")) {
    id = CODEC_ID_AMR_WB;
    audio = TRUE;
  } else if (!strcmp (mimetype, "video/x-h264")) {
    id = CODEC_ID_H264;
    video = TRUE;
  } else if (!strcmp (mimetype, "video/x-flash-video")) {
    gint flvversion = 0;

    if ((gst_structure_get_int (structure, "flvversion", &flvversion))) {
      switch (flvversion) {
        case 1:
          id = CODEC_ID_FLV1;
          video = TRUE;
          break;
        default:
          break;
      }
    }

  } else if (!strcmp (mimetype, "audio/x-nellymoser")) {
    id = CODEC_ID_NELLYMOSER;
    audio = TRUE;
  } else if (!strncmp (mimetype, "audio/x-gst_ff-", 15)) {
    gchar ext[16];
    AVCodec *codec;

    if (strlen (mimetype) <= 30 &&
        sscanf (mimetype, "audio/x-gst_ff-%s", ext) == 1) {
      if ((codec = avcodec_find_decoder_by_name (ext)) ||
          (codec = avcodec_find_encoder_by_name (ext))) {
        id = codec->id;
        audio = TRUE;
      }
    }
  } else if (!strncmp (mimetype, "video/x-gst_ff-", 15)) {
    gchar ext[16];
    AVCodec *codec;

    if (strlen (mimetype) <= 30 &&
        sscanf (mimetype, "video/x-gst_ff-%s", ext) == 1) {
      if ((codec = avcodec_find_decoder_by_name (ext)) ||
          (codec = avcodec_find_encoder_by_name (ext))) {
        id = codec->id;
        video = TRUE;
      }
    }
  }

  if (context != NULL) {
    if (video == TRUE) {
      context->codec_type = CODEC_TYPE_VIDEO;
    } else if (audio == TRUE) {
      context->codec_type = CODEC_TYPE_AUDIO;
    } else {
      context->codec_type = CODEC_TYPE_UNKNOWN;
    }
    context->codec_id = id;
    gst_ffmpeg_caps_with_codecid (id, context->codec_type, caps, context);
  }

  if (id != CODEC_ID_NONE) {
    char *str = gst_caps_to_string (caps);

    GST_DEBUG ("The id=%d belongs to the caps %s", id, str);
    g_free (str);
  } else {
    gchar *str = gst_caps_to_string (caps);

    GST_WARNING ("Couldn't figure out the id for caps %s", str);
    g_free (str);
  }

  return id;
}

G_CONST_RETURN gchar *
gst_ffmpeg_get_codecid_longname (enum CodecID codec_id)
{
  AVCodec *codec;
  /* Let's use what ffmpeg can provide us */

  if ((codec = avcodec_find_decoder (codec_id)) ||
      (codec = avcodec_find_encoder (codec_id)))
    return codec->long_name;
  return NULL;
}

/*
 * Fill in pointers to memory in a AVPicture, where
 * everything is aligned by 4 (as required by X).
 * This is mostly a copy from imgconvert.c with some
 * small changes.
 */

#define FF_COLOR_RGB      0     /* RGB color space */
#define FF_COLOR_GRAY     1     /* gray color space */
#define FF_COLOR_YUV      2     /* YUV color space. 16 <= Y <= 235, 16 <= U, V <= 240 */
#define FF_COLOR_YUV_JPEG 3     /* YUV color space. 0 <= Y <= 255, 0 <= U, V <= 255 */

#define FF_PIXEL_PLANAR   0     /* each channel has one component in AVPicture */
#define FF_PIXEL_PACKED   1     /* only one components containing all the channels */
#define FF_PIXEL_PALETTE  2     /* one components containing indexes for a palette */

typedef struct PixFmtInfo
{
  const char *name;
  uint8_t nb_channels;          /* number of channels (including alpha) */
  uint8_t color_type;           /* color type (see FF_COLOR_xxx constants) */
  uint8_t pixel_type;           /* pixel storage type (see FF_PIXEL_xxx constants) */
  uint8_t is_alpha:1;           /* true if alpha can be specified */
  uint8_t x_chroma_shift;       /* X chroma subsampling factor is 2 ^ shift */
  uint8_t y_chroma_shift;       /* Y chroma subsampling factor is 2 ^ shift */
  uint8_t depth;                /* bit depth of the color components */
} PixFmtInfo;


/* this table gives more information about formats */
static PixFmtInfo pix_fmt_info[PIX_FMT_NB];
void
gst_ffmpeg_init_pix_fmt_info ()
{
  /* YUV formats */
  pix_fmt_info[PIX_FMT_YUV420P].name = g_strdup ("yuv420p");
  pix_fmt_info[PIX_FMT_YUV420P].nb_channels = 3;
  pix_fmt_info[PIX_FMT_YUV420P].color_type = FF_COLOR_YUV;
  pix_fmt_info[PIX_FMT_YUV420P].pixel_type = FF_PIXEL_PLANAR;
  pix_fmt_info[PIX_FMT_YUV420P].depth = 8,
      pix_fmt_info[PIX_FMT_YUV420P].x_chroma_shift = 1,
      pix_fmt_info[PIX_FMT_YUV420P].y_chroma_shift = 1;

  pix_fmt_info[PIX_FMT_YUV422P].name = g_strdup ("yuv422p");
  pix_fmt_info[PIX_FMT_YUV422P].nb_channels = 3;
  pix_fmt_info[PIX_FMT_YUV422P].color_type = FF_COLOR_YUV;
  pix_fmt_info[PIX_FMT_YUV422P].pixel_type = FF_PIXEL_PLANAR;
  pix_fmt_info[PIX_FMT_YUV422P].depth = 8;
  pix_fmt_info[PIX_FMT_YUV422P].x_chroma_shift = 1;
  pix_fmt_info[PIX_FMT_YUV422P].y_chroma_shift = 0;

  pix_fmt_info[PIX_FMT_YUV444P].name = g_strdup ("yuv444p");
  pix_fmt_info[PIX_FMT_YUV444P].nb_channels = 3;
  pix_fmt_info[PIX_FMT_YUV444P].color_type = FF_COLOR_YUV;
  pix_fmt_info[PIX_FMT_YUV444P].pixel_type = FF_PIXEL_PLANAR;
  pix_fmt_info[PIX_FMT_YUV444P].depth = 8;
  pix_fmt_info[PIX_FMT_YUV444P].x_chroma_shift = 0;
  pix_fmt_info[PIX_FMT_YUV444P].y_chroma_shift = 0;

  pix_fmt_info[PIX_FMT_YUV422].name = g_strdup ("yuv422");
  pix_fmt_info[PIX_FMT_YUV422].nb_channels = 1;
  pix_fmt_info[PIX_FMT_YUV422].color_type = FF_COLOR_YUV;
  pix_fmt_info[PIX_FMT_YUV422].pixel_type = FF_PIXEL_PACKED;
  pix_fmt_info[PIX_FMT_YUV422].depth = 8;
  pix_fmt_info[PIX_FMT_YUV422].x_chroma_shift = 1;
  pix_fmt_info[PIX_FMT_YUV422].y_chroma_shift = 0;

  pix_fmt_info[PIX_FMT_YUV410P].name = g_strdup ("yuv410p");
  pix_fmt_info[PIX_FMT_YUV410P].nb_channels = 3;
  pix_fmt_info[PIX_FMT_YUV410P].color_type = FF_COLOR_YUV;
  pix_fmt_info[PIX_FMT_YUV410P].pixel_type = FF_PIXEL_PLANAR;
  pix_fmt_info[PIX_FMT_YUV410P].depth = 8;
  pix_fmt_info[PIX_FMT_YUV410P].x_chroma_shift = 2;
  pix_fmt_info[PIX_FMT_YUV410P].y_chroma_shift = 2;

  pix_fmt_info[PIX_FMT_YUV411P].name = g_strdup ("yuv411p");
  pix_fmt_info[PIX_FMT_YUV411P].nb_channels = 3;
  pix_fmt_info[PIX_FMT_YUV411P].color_type = FF_COLOR_YUV;
  pix_fmt_info[PIX_FMT_YUV411P].pixel_type = FF_PIXEL_PLANAR;
  pix_fmt_info[PIX_FMT_YUV411P].depth = 8;
  pix_fmt_info[PIX_FMT_YUV411P].x_chroma_shift = 2;
  pix_fmt_info[PIX_FMT_YUV411P].y_chroma_shift = 0;

  /* JPEG YUV */
  pix_fmt_info[PIX_FMT_YUVJ420P].name = g_strdup ("yuvj420p");
  pix_fmt_info[PIX_FMT_YUVJ420P].nb_channels = 3;
  pix_fmt_info[PIX_FMT_YUVJ420P].color_type = FF_COLOR_YUV_JPEG;
  pix_fmt_info[PIX_FMT_YUVJ420P].pixel_type = FF_PIXEL_PLANAR;
  pix_fmt_info[PIX_FMT_YUVJ420P].depth = 8;
  pix_fmt_info[PIX_FMT_YUVJ420P].x_chroma_shift = 1;
  pix_fmt_info[PIX_FMT_YUVJ420P].y_chroma_shift = 1;

  pix_fmt_info[PIX_FMT_YUVJ422P].name = g_strdup ("yuvj422p");
  pix_fmt_info[PIX_FMT_YUVJ422P].nb_channels = 3;
  pix_fmt_info[PIX_FMT_YUVJ422P].color_type = FF_COLOR_YUV_JPEG;
  pix_fmt_info[PIX_FMT_YUVJ422P].pixel_type = FF_PIXEL_PLANAR;
  pix_fmt_info[PIX_FMT_YUVJ422P].depth = 8;
  pix_fmt_info[PIX_FMT_YUVJ422P].x_chroma_shift = 1;
  pix_fmt_info[PIX_FMT_YUVJ422P].y_chroma_shift = 0;

  pix_fmt_info[PIX_FMT_YUVJ444P].name = g_strdup ("yuvj444p");
  pix_fmt_info[PIX_FMT_YUVJ444P].nb_channels = 3;
  pix_fmt_info[PIX_FMT_YUVJ444P].color_type = FF_COLOR_YUV_JPEG;
  pix_fmt_info[PIX_FMT_YUVJ444P].pixel_type = FF_PIXEL_PLANAR;
  pix_fmt_info[PIX_FMT_YUVJ444P].depth = 8;
  pix_fmt_info[PIX_FMT_YUVJ444P].x_chroma_shift = 0;
  pix_fmt_info[PIX_FMT_YUVJ444P].y_chroma_shift = 0;

  /* RGB formats */
  pix_fmt_info[PIX_FMT_RGB24].name = g_strdup ("rgb24");
  pix_fmt_info[PIX_FMT_RGB24].nb_channels = 3;
  pix_fmt_info[PIX_FMT_RGB24].color_type = FF_COLOR_RGB;
  pix_fmt_info[PIX_FMT_RGB24].pixel_type = FF_PIXEL_PACKED;
  pix_fmt_info[PIX_FMT_RGB24].depth = 8;
  pix_fmt_info[PIX_FMT_RGB24].x_chroma_shift = 0;
  pix_fmt_info[PIX_FMT_RGB24].y_chroma_shift = 0;

  pix_fmt_info[PIX_FMT_BGR24].name = g_strdup ("bgr24");
  pix_fmt_info[PIX_FMT_BGR24].nb_channels = 3;
  pix_fmt_info[PIX_FMT_BGR24].color_type = FF_COLOR_RGB;
  pix_fmt_info[PIX_FMT_BGR24].pixel_type = FF_PIXEL_PACKED;
  pix_fmt_info[PIX_FMT_BGR24].depth = 8;
  pix_fmt_info[PIX_FMT_BGR24].x_chroma_shift = 0;
  pix_fmt_info[PIX_FMT_BGR24].y_chroma_shift = 0;

  pix_fmt_info[PIX_FMT_RGBA32].name = g_strdup ("rgba32");
  pix_fmt_info[PIX_FMT_RGBA32].nb_channels = 4;
  pix_fmt_info[PIX_FMT_RGBA32].is_alpha = 1;
  pix_fmt_info[PIX_FMT_RGBA32].color_type = FF_COLOR_RGB;
  pix_fmt_info[PIX_FMT_RGBA32].pixel_type = FF_PIXEL_PACKED;
  pix_fmt_info[PIX_FMT_RGBA32].depth = 8;
  pix_fmt_info[PIX_FMT_RGBA32].x_chroma_shift = 0;
  pix_fmt_info[PIX_FMT_RGBA32].y_chroma_shift = 0;

  pix_fmt_info[PIX_FMT_RGB565].name = g_strdup ("rgb565");
  pix_fmt_info[PIX_FMT_RGB565].nb_channels = 3;
  pix_fmt_info[PIX_FMT_RGB565].color_type = FF_COLOR_RGB;
  pix_fmt_info[PIX_FMT_RGB565].pixel_type = FF_PIXEL_PACKED;
  pix_fmt_info[PIX_FMT_RGB565].depth = 5;
  pix_fmt_info[PIX_FMT_RGB565].x_chroma_shift = 0;
  pix_fmt_info[PIX_FMT_RGB565].y_chroma_shift = 0;

  pix_fmt_info[PIX_FMT_RGB555].name = g_strdup ("rgb555");
  pix_fmt_info[PIX_FMT_RGB555].nb_channels = 4;
  pix_fmt_info[PIX_FMT_RGB555].is_alpha = 1;
  pix_fmt_info[PIX_FMT_RGB555].color_type = FF_COLOR_RGB;
  pix_fmt_info[PIX_FMT_RGB555].pixel_type = FF_PIXEL_PACKED;
  pix_fmt_info[PIX_FMT_RGB555].depth = 5;
  pix_fmt_info[PIX_FMT_RGB555].x_chroma_shift = 0;
  pix_fmt_info[PIX_FMT_RGB555].y_chroma_shift = 0;

  /* gray / mono formats */
  pix_fmt_info[PIX_FMT_GRAY8].name = g_strdup ("gray");
  pix_fmt_info[PIX_FMT_GRAY8].nb_channels = 1;
  pix_fmt_info[PIX_FMT_GRAY8].color_type = FF_COLOR_GRAY;
  pix_fmt_info[PIX_FMT_GRAY8].pixel_type = FF_PIXEL_PLANAR;
  pix_fmt_info[PIX_FMT_GRAY8].depth = 8;

  pix_fmt_info[PIX_FMT_MONOWHITE].name = g_strdup ("monow");
  pix_fmt_info[PIX_FMT_MONOWHITE].nb_channels = 1;
  pix_fmt_info[PIX_FMT_MONOWHITE].color_type = FF_COLOR_GRAY;
  pix_fmt_info[PIX_FMT_MONOWHITE].pixel_type = FF_PIXEL_PLANAR;
  pix_fmt_info[PIX_FMT_MONOWHITE].depth = 1;

  pix_fmt_info[PIX_FMT_MONOBLACK].name = g_strdup ("monob");
  pix_fmt_info[PIX_FMT_MONOBLACK].nb_channels = 1;
  pix_fmt_info[PIX_FMT_MONOBLACK].color_type = FF_COLOR_GRAY;
  pix_fmt_info[PIX_FMT_MONOBLACK].pixel_type = FF_PIXEL_PLANAR;
  pix_fmt_info[PIX_FMT_MONOBLACK].depth = 1;

  /* paletted formats */
  pix_fmt_info[PIX_FMT_PAL8].name = g_strdup ("pal8");
  pix_fmt_info[PIX_FMT_PAL8].nb_channels = 4;
  pix_fmt_info[PIX_FMT_PAL8].is_alpha = 1;
  pix_fmt_info[PIX_FMT_PAL8].color_type = FF_COLOR_RGB;
  pix_fmt_info[PIX_FMT_PAL8].pixel_type = FF_PIXEL_PALETTE;
  pix_fmt_info[PIX_FMT_PAL8].depth = 8;
};

int
gst_ffmpeg_avpicture_get_size (int pix_fmt, int width, int height)
{
  AVPicture dummy_pict;

  return gst_ffmpeg_avpicture_fill (&dummy_pict, NULL, pix_fmt, width, height);
}

#define GEN_MASK(x) ((1<<(x))-1)
#define ROUND_UP_X(v,x) (((v) + GEN_MASK(x)) & ~GEN_MASK(x))
#define ROUND_UP_2(x) ROUND_UP_X (x, 1)
#define ROUND_UP_4(x) ROUND_UP_X (x, 2)
#define ROUND_UP_8(x) ROUND_UP_X (x, 3)
#define DIV_ROUND_UP_X(v,x) (((v) + GEN_MASK(x)) >> (x))

int
gst_ffmpeg_avpicture_fill (AVPicture * picture,
    uint8_t * ptr, enum PixelFormat pix_fmt, int width, int height)
{
  int size, w2, h2, size2;
  int stride, stride2;
  PixFmtInfo *pinfo;

  pinfo = &pix_fmt_info[pix_fmt];

  switch (pix_fmt) {
    case PIX_FMT_YUV420P:
    case PIX_FMT_YUV422P:
    case PIX_FMT_YUV444P:
    case PIX_FMT_YUV410P:
    case PIX_FMT_YUV411P:
    case PIX_FMT_YUVJ420P:
    case PIX_FMT_YUVJ422P:
    case PIX_FMT_YUVJ444P:
      stride = ROUND_UP_4 (width);
      h2 = ROUND_UP_X (height, pinfo->y_chroma_shift);
      size = stride * h2;
      w2 = DIV_ROUND_UP_X (width, pinfo->x_chroma_shift);
      stride2 = ROUND_UP_4 (w2);
      h2 = DIV_ROUND_UP_X (height, pinfo->y_chroma_shift);
      size2 = stride2 * h2;
      picture->data[0] = ptr;
      picture->data[1] = picture->data[0] + size;
      picture->data[2] = picture->data[1] + size2;
      picture->linesize[0] = stride;
      picture->linesize[1] = stride2;
      picture->linesize[2] = stride2;
      return size + 2 * size2;
    case PIX_FMT_RGB24:
    case PIX_FMT_BGR24:
      stride = ROUND_UP_4 (width * 3);
      size = stride * height;
      picture->data[0] = ptr;
      picture->data[1] = NULL;
      picture->data[2] = NULL;
      picture->linesize[0] = stride;
      return size;
      /*case PIX_FMT_AYUV4444:
         case PIX_FMT_BGR32:
         case PIX_FMT_BGRA32:
         case PIX_FMT_RGB32: */
    case PIX_FMT_RGBA32:
      stride = width * 4;
      size = stride * height;
      picture->data[0] = ptr;
      picture->data[1] = NULL;
      picture->data[2] = NULL;
      picture->linesize[0] = stride;
      return size;
    case PIX_FMT_RGB555:
    case PIX_FMT_RGB565:
    case PIX_FMT_YUV422:
    case PIX_FMT_UYVY422:
      stride = ROUND_UP_4 (width * 2);
      size = stride * height;
      picture->data[0] = ptr;
      picture->data[1] = NULL;
      picture->data[2] = NULL;
      picture->linesize[0] = stride;
      return size;
    case PIX_FMT_UYVY411:
      /* FIXME, probably not the right stride */
      stride = ROUND_UP_4 (width);
      size = stride * height;
      picture->data[0] = ptr;
      picture->data[1] = NULL;
      picture->data[2] = NULL;
      picture->linesize[0] = width + width / 2;
      return size + size / 2;
    case PIX_FMT_GRAY8:
      stride = ROUND_UP_4 (width);
      size = stride * height;
      picture->data[0] = ptr;
      picture->data[1] = NULL;
      picture->data[2] = NULL;
      picture->linesize[0] = stride;
      return size;
    case PIX_FMT_MONOWHITE:
    case PIX_FMT_MONOBLACK:
      stride = ROUND_UP_4 ((width + 7) >> 3);
      size = stride * height;
      picture->data[0] = ptr;
      picture->data[1] = NULL;
      picture->data[2] = NULL;
      picture->linesize[0] = stride;
      return size;
    case PIX_FMT_PAL8:
      /* already forced to be with stride, so same result as other function */
      stride = ROUND_UP_4 (width);
      size = stride * height;
      picture->data[0] = ptr;
      picture->data[1] = ptr + size;    /* palette is stored here as 256 32 bit words */
      picture->data[2] = NULL;
      picture->linesize[0] = stride;
      picture->linesize[1] = 4;
      return size + 256 * 4;
    default:
      picture->data[0] = NULL;
      picture->data[1] = NULL;
      picture->data[2] = NULL;
      picture->data[3] = NULL;
      return -1;
  }

  return 0;
}
