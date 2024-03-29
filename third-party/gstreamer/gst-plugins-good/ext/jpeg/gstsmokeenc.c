/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
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
#include <string.h>

#include "gstsmokeenc.h"
#include <gst/video/video.h>

/* elementfactory information */
static const GstElementDetails gst_smokeenc_details =
GST_ELEMENT_DETAILS ("Smoke video encoder",
    "Codec/Encoder/Video",
    "Encode images into the Smoke format",
    "Wim Taymans <wim@fluendo.com>");

GST_DEBUG_CATEGORY_STATIC (smokeenc_debug);
#define GST_CAT_DEFAULT smokeenc_debug


/* SmokeEnc signals and args */
enum
{
  FRAME_ENCODED,
  /* FILL ME */
  LAST_SIGNAL
};

#define DEFAULT_PROP_MIN_QUALITY 10
#define DEFAULT_PROP_MAX_QUALITY 85
#define DEFAULT_PROP_THRESHOLD 3000
#define DEFAULT_PROP_KEYFRAME 20

enum
{
  PROP_0,
  PROP_MIN_QUALITY,
  PROP_MAX_QUALITY,
  PROP_THRESHOLD,
  PROP_KEYFRAME
      /* FILL ME */
};

static void gst_smokeenc_base_init (gpointer g_class);
static void gst_smokeenc_class_init (GstSmokeEnc * klass);
static void gst_smokeenc_init (GstSmokeEnc * smokeenc);
static void gst_smokeenc_finalize (GObject * object);

static GstStateChangeReturn
gst_smokeenc_change_state (GstElement * element, GstStateChange transition);

static GstFlowReturn gst_smokeenc_chain (GstPad * pad, GstBuffer * buf);
static gboolean gst_smokeenc_setcaps (GstPad * pad, GstCaps * caps);

static gboolean gst_smokeenc_resync (GstSmokeEnc * smokeenc);
static void gst_smokeenc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_smokeenc_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static GstElementClass *parent_class = NULL;

GType
gst_smokeenc_get_type (void)
{
  static GType smokeenc_type = 0;

  if (!smokeenc_type) {
    static const GTypeInfo smokeenc_info = {
      sizeof (GstSmokeEncClass),
      (GBaseInitFunc) gst_smokeenc_base_init,
      NULL,
      (GClassInitFunc) gst_smokeenc_class_init,
      NULL,
      NULL,
      sizeof (GstSmokeEnc),
      0,
      (GInstanceInitFunc) gst_smokeenc_init,
    };

    smokeenc_type =
        g_type_register_static (GST_TYPE_ELEMENT, "GstSmokeEnc", &smokeenc_info,
        0);
  }
  return smokeenc_type;
}

static GstStaticPadTemplate gst_smokeenc_sink_pad_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_YUV ("I420"))
    );

static GstStaticPadTemplate gst_smokeenc_src_pad_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-smoke, "
        "width = (int) [ 16, 4096 ], "
        "height = (int) [ 16, 4096 ], " "framerate = (fraction) [ 0/1, MAX ]")
    );

static void
gst_smokeenc_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_smokeenc_sink_pad_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_smokeenc_src_pad_template));
  gst_element_class_set_details (element_class, &gst_smokeenc_details);
}

static void
gst_smokeenc_class_init (GstSmokeEnc * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  parent_class = g_type_class_peek_parent (klass);

  gobject_class->finalize = gst_smokeenc_finalize;
  gobject_class->set_property = gst_smokeenc_set_property;
  gobject_class->get_property = gst_smokeenc_get_property;

  g_object_class_install_property (gobject_class, PROP_MIN_QUALITY,
      g_param_spec_int ("qmin", "Qmin", "Minimum quality",
          0, 100, DEFAULT_PROP_MIN_QUALITY, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, PROP_MAX_QUALITY,
      g_param_spec_int ("qmax", "Qmax", "Maximum quality",
          0, 100, DEFAULT_PROP_MAX_QUALITY, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, PROP_THRESHOLD,
      g_param_spec_int ("threshold", "Threshold", "Motion estimation threshold",
          0, 100000000, DEFAULT_PROP_THRESHOLD, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, PROP_KEYFRAME,
      g_param_spec_int ("keyframe", "Keyframe",
          "Insert keyframe every N frames", 1, 100000,
          DEFAULT_PROP_KEYFRAME, G_PARAM_READWRITE));

  gstelement_class->change_state =
      GST_DEBUG_FUNCPTR (gst_smokeenc_change_state);

  GST_DEBUG_CATEGORY_INIT (smokeenc_debug, "smokeenc", 0,
      "Smoke encoding element");
}

static void
gst_smokeenc_init (GstSmokeEnc * smokeenc)
{
  /* create the sink and src pads */
  smokeenc->sinkpad =
      gst_pad_new_from_static_template (&gst_smokeenc_sink_pad_template,
      "sink");
  gst_pad_set_chain_function (smokeenc->sinkpad, gst_smokeenc_chain);
  gst_pad_set_setcaps_function (smokeenc->sinkpad, gst_smokeenc_setcaps);
  gst_element_add_pad (GST_ELEMENT (smokeenc), smokeenc->sinkpad);

  smokeenc->srcpad =
      gst_pad_new_from_static_template (&gst_smokeenc_src_pad_template, "src");
  gst_pad_use_fixed_caps (smokeenc->srcpad);
  gst_element_add_pad (GST_ELEMENT (smokeenc), smokeenc->srcpad);

  smokeenc->min_quality = DEFAULT_PROP_MIN_QUALITY;
  smokeenc->max_quality = DEFAULT_PROP_MAX_QUALITY;
  smokeenc->threshold = DEFAULT_PROP_THRESHOLD;
  smokeenc->keyframe = DEFAULT_PROP_KEYFRAME;
}

static void
gst_smokeenc_finalize (GObject * object)
{
  GstSmokeEnc *enc = GST_SMOKEENC (object);

  if (enc->info)
    smokecodec_info_free (enc->info);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static gboolean
gst_smokeenc_setcaps (GstPad * pad, GstCaps * caps)
{
  GstSmokeEnc *smokeenc;
  GstStructure *structure;
  const GValue *framerate;
  gboolean ret;

  smokeenc = GST_SMOKEENC (gst_pad_get_parent (pad));

  structure = gst_caps_get_structure (caps, 0);
  framerate = gst_structure_get_value (structure, "framerate");
  if (framerate) {
    smokeenc->fps_num = gst_value_get_fraction_numerator (framerate);
    smokeenc->fps_denom = gst_value_get_fraction_denominator (framerate);
  } else {
    smokeenc->fps_num = 0;
    smokeenc->fps_denom = 1;
  }

  gst_structure_get_int (structure, "width", &smokeenc->width);
  gst_structure_get_int (structure, "height", &smokeenc->height);

  if ((smokeenc->width & 0x0f) != 0 || (smokeenc->height & 0x0f) != 0)
    goto width_or_height_notx16;

  if (smokeenc->srccaps)
    gst_caps_unref (smokeenc->srccaps);

  smokeenc->srccaps = gst_caps_new_simple ("video/x-smoke",
      "width", G_TYPE_INT, smokeenc->width,
      "height", G_TYPE_INT, smokeenc->height,
      "framerate", GST_TYPE_FRACTION, smokeenc->fps_num, smokeenc->fps_denom,
      NULL);

  ret = gst_smokeenc_resync (smokeenc);

  gst_object_unref (smokeenc);

  return ret;

width_or_height_notx16:
  {
    GST_WARNING_OBJECT (smokeenc, "width and height must be multiples of 16"
        ", %dx%d not allowed", smokeenc->width, smokeenc->height);
    gst_object_unref (smokeenc);
    return FALSE;
  }
}

static gboolean
gst_smokeenc_resync (GstSmokeEnc * smokeenc)
{
  int ret;

  GST_DEBUG ("resync: %dx%d@%d/%dfps", smokeenc->width, smokeenc->height,
      smokeenc->fps_num, smokeenc->fps_denom);

  if (smokeenc->info)
    smokecodec_info_free (smokeenc->info);

  ret = smokecodec_encode_new (&smokeenc->info, smokeenc->width,
      smokeenc->height, smokeenc->fps_num, smokeenc->fps_denom);

  if (ret != SMOKECODEC_OK)
    goto init_failed;

  smokecodec_set_quality (smokeenc->info, smokeenc->min_quality,
      smokeenc->max_quality);

  GST_DEBUG ("resync done");
  return TRUE;

  /* ERRORS */
init_failed:
  {
    GST_WARNING_OBJECT (smokeenc, "smokecodec_encode_new() failed: %d", ret);
    return FALSE;
  }
}

static GstFlowReturn
gst_smokeenc_chain (GstPad * pad, GstBuffer * buf)
{
  GstSmokeEnc *smokeenc;
  guchar *data, *outdata;
  gulong size;
  gint outsize;
  guint encsize;
  GstBuffer *outbuf;
  SmokeCodecFlags flags;
  GstFlowReturn ret;

  smokeenc = GST_SMOKEENC (GST_OBJECT_PARENT (pad));

  data = GST_BUFFER_DATA (buf);
  size = GST_BUFFER_SIZE (buf);

  GST_LOG_OBJECT (smokeenc, "got buffer of %lu bytes", size);

  if (smokeenc->need_header) {
    outbuf = gst_buffer_new_and_alloc (256);
    outdata = GST_BUFFER_DATA (outbuf);

    GST_BUFFER_TIMESTAMP (outbuf) = GST_BUFFER_TIMESTAMP (buf);
    GST_BUFFER_DURATION (outbuf) = GST_BUFFER_DURATION (buf);

    smokecodec_encode_id (smokeenc->info, outdata, &encsize);

    GST_BUFFER_SIZE (outbuf) = encsize;
    gst_buffer_set_caps (outbuf, GST_PAD_CAPS (smokeenc->srcpad));

    ret = gst_pad_push (smokeenc->srcpad, outbuf);
    if (ret != GST_FLOW_OK)
      goto done;

    smokeenc->need_header = FALSE;
  }

  encsize = outsize = smokeenc->width * smokeenc->height * 3;
  outbuf = gst_buffer_new_and_alloc (outsize);
  outdata = GST_BUFFER_DATA (outbuf);

  GST_BUFFER_TIMESTAMP (outbuf) = GST_BUFFER_TIMESTAMP (buf);
  GST_BUFFER_DURATION (outbuf) =
      gst_util_uint64_scale_int (GST_SECOND, smokeenc->fps_denom,
      smokeenc->fps_num);
  gst_buffer_set_caps (outbuf, smokeenc->srccaps);

  flags = 0;
  if ((smokeenc->frame % smokeenc->keyframe) == 0) {
    flags |= SMOKECODEC_KEYFRAME;
  }
  smokecodec_set_quality (smokeenc->info, smokeenc->min_quality,
      smokeenc->max_quality);
  smokecodec_set_threshold (smokeenc->info, smokeenc->threshold);
  smokecodec_encode (smokeenc->info, data, flags, outdata, &encsize);
  gst_buffer_unref (buf);

  GST_BUFFER_SIZE (outbuf) = encsize;
  GST_BUFFER_OFFSET (outbuf) = smokeenc->frame;
  GST_BUFFER_OFFSET_END (outbuf) = smokeenc->frame + 1;

  ret = gst_pad_push (smokeenc->srcpad, outbuf);

  smokeenc->frame++;

done:

  return ret;
}

static void
gst_smokeenc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstSmokeEnc *smokeenc;

  g_return_if_fail (GST_IS_SMOKEENC (object));
  smokeenc = GST_SMOKEENC (object);

  switch (prop_id) {
    case PROP_MIN_QUALITY:
      smokeenc->min_quality = g_value_get_int (value);
      break;
    case PROP_MAX_QUALITY:
      smokeenc->max_quality = g_value_get_int (value);
      break;
    case PROP_THRESHOLD:
      smokeenc->threshold = g_value_get_int (value);
      break;
    case PROP_KEYFRAME:
      smokeenc->keyframe = g_value_get_int (value);
      break;
    default:
      break;
  }
}

static void
gst_smokeenc_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstSmokeEnc *smokeenc;

  g_return_if_fail (GST_IS_SMOKEENC (object));
  smokeenc = GST_SMOKEENC (object);

  switch (prop_id) {
    case PROP_MIN_QUALITY:
      g_value_set_int (value, smokeenc->min_quality);
      break;
    case PROP_MAX_QUALITY:
      g_value_set_int (value, smokeenc->max_quality);
      break;
    case PROP_THRESHOLD:
      g_value_set_int (value, smokeenc->threshold);
      break;
    case PROP_KEYFRAME:
      g_value_set_int (value, smokeenc->keyframe);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstStateChangeReturn
gst_smokeenc_change_state (GstElement * element, GstStateChange transition)
{
  GstStateChangeReturn ret;
  GstSmokeEnc *enc;

  enc = GST_SMOKEENC (element);

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      /* reset the initial video state */
      enc->width = 0;
      enc->height = 0;
      enc->frame = 0;
      enc->need_header = TRUE;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);
  if (ret != GST_STATE_CHANGE_SUCCESS)
    return ret;

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      if (enc->srccaps) {
        gst_caps_unref (enc->srccaps);
        enc->srccaps = NULL;
      }
      break;
    default:
      break;
  }

  return ret;
}
