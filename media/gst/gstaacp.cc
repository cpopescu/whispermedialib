// Copyright (c) 2009, Whispersoft s.r.l.
//
// This library is free software; you can redistribute it and/or
// modify it under the terms of the GNU Lesser General Public
// License as published by the Free Software Foundation; either
// version 2 of the License, or (at your option) any later version.
//
// This library is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
// Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public
// License along with this library; if not, write to the Free Software
// Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307
//
// Author: Mihai Ianculescu
//
#include <string.h>

#include <gst/gstplugin.h>

#include "media/gst/gstaacp.h"

#ifdef __cplusplus
extern "C" {
#endif

#include "third-party/aacplusenc/adts.h"

#ifdef __cplusplus
}
#endif

#define CORE_DELAY   (1600)
// ((1600 (core codec)*2 (multi rate) +
//              6*64 (sbr dec delay) - 2048 (sbr enc delay) + magic
#define INPUT_DELAY  ((CORE_DELAY)*2 +6*64-2048+1)
// the additional max resampler filter delay (source fs)
#define MAX_DS_FILTER_DELAY 16

// (96-64) makes AAC still some 64 core samples too early wrt SBR ...
//  maybe -32 would be even more correct, but 1024-32 would need
//   additional SBR bitstream delay by one frame
#define CORE_INPUT_OFFSET_PS (0)

#define SINK_CAPS                      \
  "audio/x-raw-int, "                  \
  "endianness = (int) BYTE_ORDER, "    \
  "signed = (boolean) true, "          \
  "width = (int) 16, "                 \
  "depth = (int) 16, "                 \
  "rate = (int) { "                    \
  "96000, "                            \
  "88200, "                            \
  "64000, "                            \
  "48000, "                            \
  "44100, "                            \
  "32000, "                            \
  "24000, "                            \
  "22050, "                            \
  "16000, "                            \
  "12000, "                            \
  "11025, "                            \
  "800, "                              \
  "7350 "                              \
  "}, "                                \
  "channels = (int) {1, 2}"

#define SRC_CAPS                    \
  "audio/mpeg, "                    \
  "mpegversion = (int) { 4 }, "     \
  "rate = (int) { "                 \
  "96000, "                         \
  "88200, "                         \
  "64000, "                         \
  "48000, "                         \
  "44100, "                         \
  "32000, "                         \
  "24000, "                         \
  "22050, "                         \
  "16000, "                         \
  "12000, "                         \
  "11025, "                         \
  "800, "                           \
  "7350 "                           \
  "}, "                             \
  "channels = (int) {1, 2}"         \

//
// This enum defines the format of the outgoing buffers
// to a new client.
//
typedef enum {
  GST_OUTPUTFORMAT_RAW,
  GST_OUTPUTFORMAT_ADTS
} GstOutputFormat;

static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE(
    (gchar*)("src"),
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS(SRC_CAPS));

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE(
    (gchar*)("sink"),
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS(SINK_CAPS));

static const GstElementDetails gst_aacp_details =
    GST_ELEMENT_DETAILS((gchar*)("AAC+ audio encoder"),
                        (gchar*)("Codec/Encoder/Audio"),
                        (gchar*)("Free AAC+ encoder"),
                        (gchar*)("Mihai Ianculescu <?@?.?>"));

enum {
  ARG_0,
  ARG_BITRATE,
  ARG_OUTPUTFORMAT,
};

static void gst_aacp_base_init(GstAacpClass* klass);
static void gst_aacp_class_init(GstAacpClass* klass);
static void gst_aacp_init(GstAacp* aacp);

static void gst_aacp_set_property(GObject* object,
                                  guint prop_id,
                                  const GValue* value,
                                  GParamSpec* pspec);
static void gst_aacp_get_property(GObject* object,
                                  guint prop_id,
                                  GValue* value,
                                  GParamSpec* pspec);

static gboolean gst_aacp_sink_event(GstPad* pad, GstEvent* event);
static gboolean gst_aacp_sink_setcaps(GstPad* pad, GstCaps* caps);
static GstFlowReturn gst_aacp_chain(GstPad* pad, GstBuffer* data);
static GstStateChangeReturn gst_aacp_change_state(GstElement* element,
                                                  GstStateChange transition);

static GstElementClass* parent_class = NULL;

GST_DEBUG_CATEGORY_STATIC(aacp_debug);
#define GST_CAT_DEFAULT aacp_debug

#define GST_TYPE_AACP_OUTPUTFORMAT (gst_aacp_outputformat_get_type ())
static GType
gst_aacp_outputformat_get_type(void) {
  static GType gst_aacp_outputformat_type = 0;

  if ( !gst_aacp_outputformat_type ) {
    static GEnumValue gst_aacp_outputformat[] = {
      {GST_OUTPUTFORMAT_RAW, "raw", "Raw AAC+"},
      {GST_OUTPUTFORMAT_ADTS, "adts", "Add ADTS headers"},
      {0, NULL, NULL},
    };

    gst_aacp_outputformat_type = g_enum_register_static("GstAacpOutputFormat",
        gst_aacp_outputformat);
  }

  return gst_aacp_outputformat_type;
}

GType gst_aacp_get_type(void) {
  static GType gst_aacp_type = 0;

  if ( !gst_aacp_type ) {
    static const GTypeInfo gst_aacp_info = {
      sizeof(GstAacpClass),
      (GBaseInitFunc) gst_aacp_base_init,
      NULL,
      (GClassInitFunc) gst_aacp_class_init,
      NULL,
      NULL,
      sizeof(GstAacp),
      0,
      (GInstanceInitFunc) gst_aacp_init,
    };

    gst_aacp_type = g_type_register_static(GST_TYPE_ELEMENT,
                                           "GstAacp",
                                           &gst_aacp_info,
                                           (GTypeFlags)0);
  }

  return gst_aacp_type;
}

static void gst_aacp_base_init(GstAacpClass* klass) {
  GstElementClass* element_class = GST_ELEMENT_CLASS(klass);

  gst_element_class_add_pad_template(
    element_class,
    gst_static_pad_template_get(&src_template));
  gst_element_class_add_pad_template(
    element_class,
    gst_static_pad_template_get(&sink_template));

  gst_element_class_set_details(element_class, &gst_aacp_details);

  GST_DEBUG_CATEGORY_INIT(aacp_debug,
                          (gchar*)("aacp"), 0,
                          (gchar*)("AAC+ encoding"));
}

static void gst_aacp_class_init(GstAacpClass* klass) {
  GObjectClass* gobject_class = G_OBJECT_CLASS(klass);
  GstElementClass* gstelement_class = GST_ELEMENT_CLASS(klass);

  parent_class = (GstElementClass*) g_type_class_peek_parent(klass);

  gobject_class->set_property = gst_aacp_set_property;
  gobject_class->get_property = gst_aacp_get_property;

  // properties
  g_object_class_install_property(
      gobject_class, ARG_BITRATE,
      g_param_spec_int("bitrate", "Bitrate (bps)", "Bitrate in bits/sec",
                       8 * 1000,
                       320 * 1000,
                       128 * 1000, (GParamFlags)G_PARAM_READWRITE));
  g_object_class_install_property(
      gobject_class, ARG_OUTPUTFORMAT,
      g_param_spec_enum("outputformat", "Output format",
                        "Format of the output frames",
                        GST_TYPE_AACP_OUTPUTFORMAT,
                        GST_OUTPUTFORMAT_ADTS,
                        (GParamFlags)G_PARAM_READWRITE));

  // virtual functions
  gstelement_class->change_state = GST_DEBUG_FUNCPTR(gst_aacp_change_state);
  init_plans();
}

static void gst_aacp_init(GstAacp* aacp) {
  aacp->handle = NULL;
  aacp->samplerate = -1;
  aacp->channels = -1;
  aacp->cache = NULL;
  aacp->cache_time = GST_CLOCK_TIME_NONE;
  aacp->cache_duration = 0;

  aacp->sinkpad =
    gst_pad_new_from_template(gst_static_pad_template_get(&sink_template),
                              "sink");
  gst_pad_set_chain_function(aacp->sinkpad,
      GST_DEBUG_FUNCPTR(gst_aacp_chain));
  gst_pad_set_setcaps_function(aacp->sinkpad,
      GST_DEBUG_FUNCPTR(gst_aacp_sink_setcaps));
  gst_pad_set_event_function(aacp->sinkpad,
      GST_DEBUG_FUNCPTR(gst_aacp_sink_event));
  gst_element_add_pad(GST_ELEMENT(aacp), aacp->sinkpad);

  aacp->srcpad =
    gst_pad_new_from_template(gst_static_pad_template_get(&src_template),
                              "src");
  gst_pad_use_fixed_caps(aacp->srcpad);
  gst_element_add_pad(GST_ELEMENT(aacp), aacp->srcpad);

  // default properties
  aacp->bitrate = 1000 * 128;
  aacp->outputformat = GST_OUTPUTFORMAT_ADTS;
}

static gboolean gst_aacp_sink_setcaps(GstPad* pad, GstCaps* caps) {
  GstAacp* aacp = GST_AACP(gst_pad_get_parent(pad));
  GstStructure* structure = gst_caps_get_structure(caps, 0);
  gint channels, samplerate, width;
  gulong bps = 0;
  gboolean result = FALSE;

  if ( !gst_caps_is_fixed (caps) )
    goto done;

  GST_OBJECT_LOCK(aacp);
  if ( aacp->handle ) {
    // GST_INFO_OBJECT (aacp, "DESTROY - AAC");
    AacEncClose(aacp->handle);
    aacp->handle = NULL;
  }
  if ( aacp->handle_sbr ) {
    // GST_INFO_OBJECT (aacp, "DESTROY - SBR");
    EnvClose(aacp->handle_sbr);
    aacp->handle_sbr = NULL;
  }
  if ( aacp->cache ) {
    gst_buffer_unref(aacp->cache);
    aacp->cache = NULL;
  }
  GST_OBJECT_UNLOCK(aacp);

  if ( !gst_structure_get_int(structure, "channels", &channels) ||
       !gst_structure_get_int(structure, "rate", &samplerate)) {
    goto done;
  }

  if ( gst_structure_has_name(structure, "audio/x-raw-int") ) {
    gst_structure_get_int(structure, "width", &width);
    switch ( width ) {
    case 16:
      bps = 2;
      break;
    case 24:
    case 32:
      bps = 4;
      break;
    default:
      gst_object_unref(aacp);
      g_return_val_if_reached(FALSE);
    }
  } else if ( gst_structure_has_name(structure, "audio/x-raw-float") ) {
    bps = 4;
  }

  if ( bps == 0 ) {
    goto done;
  }

  GST_OBJECT_LOCK(aacp);

  aacp->bps = bps;
  aacp->channels = channels;
  aacp->samplerate = samplerate;
  GST_OBJECT_UNLOCK(aacp);

  result = TRUE;

 done:
  gst_object_unref(aacp);
  return result;
}

static gboolean gst_aacp_configure_source_pad(GstAacp* aacp) {
  GstCaps* allowed_caps;
  GstCaps* srccaps;
  gboolean ret = FALSE;
  gint n, ver, mpegversion = 4;

  guint8 aac_object_type;
  guint8 aac_sample_rate_index;
  guint8 aac_num_channels;

  allowed_caps = gst_pad_get_allowed_caps(aacp->srcpad);
  GST_DEBUG_OBJECT(aacp, "allowed caps: %" GST_PTR_FORMAT, allowed_caps);

  if ( allowed_caps == NULL )
    return FALSE;

  if ( gst_caps_is_empty(allowed_caps) )
    goto empty_caps;

  if ( !gst_caps_is_any(allowed_caps) ) {
    for ( n = 0; n < gst_caps_get_size(allowed_caps); n++ ) {
      GstStructure* s = gst_caps_get_structure(allowed_caps, n);

      if ( gst_structure_get_int (s, "mpegversion", &ver) &&
           (ver == 4 || ver == 2) ) {
        mpegversion = ver;
        break;
      }
    }
  }
  gst_caps_unref(allowed_caps);

  GST_INFO_OBJECT(aacp, "input samplerate: %d", aacp->samplerate);
  AacInitDefaultConfig(&aacp->aac_config);

  /////////////////////////////

  aacp->aac_channels =
  aacp->sbr_channels = aacp->channels;

  aacp->use_ps = FALSE;
  if ( (aacp->channels == 2) &&
       (aacp->bitrate >= 16000) &&
       (aacp->bitrate < 44001) ) {
    aacp->use_ps = TRUE;
  }

  if ( aacp->use_ps ) {
    aacp->aac_channels = 1;
    aacp->sbr_channels = 2;
  }


  aacp->aac_samplerate = aacp->samplerate;

  aacp->aac_config.bitRate = aacp->bitrate;
  aacp->aac_config.nChannelsIn = aacp->channels;
  aacp->aac_config.nChannelsOut = aacp->aac_channels;

  aacp->aac_config.bandWidth = 0;

  // set up SBR configuration
  // unsigned int core_samplerate = 0;
  if ( !IsSbrSettingAvail(aacp->bitrate,
                          aacp->aac_channels,
                          aacp->aac_samplerate,
                          (unsigned int*) &aacp->aac_samplerate)) {
                          // (unsigned int*) &aacp->aac_samplerate)) {
    GST_WARNING_OBJECT(aacp, "couldn't initialize "
                       "(bitrate=%d,channels=%d,sample rate=%d)...",
                       aacp->bitrate, aacp->aac_channels, aacp->aac_samplerate);
    goto set_failed;
  }

  aac_object_type = 1;   // AAC Main profile
  // DOH!
  // This is the CORE samplerate index,
  // not the original samplerate index...
  switch ( aacp->aac_samplerate ) {
  case 96000: aac_sample_rate_index = 0; break;
  case 88200: aac_sample_rate_index = 1; break;
  case 64000: aac_sample_rate_index = 2; break;
  case 48000: aac_sample_rate_index = 3; break;;
  case 44100: aac_sample_rate_index = 4; break;
  case 32000: aac_sample_rate_index = 5; break;
  case 24000: aac_sample_rate_index = 6; break;
  case 22050: aac_sample_rate_index = 7; break;
  case 16000: aac_sample_rate_index = 8; break;
  case 12000: aac_sample_rate_index = 9; break;
  case 11025: aac_sample_rate_index = 10; break;
  case 8000 : aac_sample_rate_index = 11; break;
  case 7350 : aac_sample_rate_index = 12; break;
  default:
    g_assert(FALSE);  // impossible, if the caps nego was correct
  }
  aac_num_channels = aacp->channels;

  GST_INFO_OBJECT(aacp, "core samplerate: %d, use_ps: %d",
                   aacp->aac_samplerate, aacp->use_ps);

  aacp->anc_data = (guint8*)g_malloc0(MAX_PAYLOAD_SIZE);
  aacp->anc_count = 0;

  // GST_INFO_OBJECT(aacp, "allocating input buffer(%d)",
  // (AACENC_BLOCKSIZE*2 + MAX_DS_FILTER_DELAY + INPUT_DELAY)*MAX_CHANNELS);
  aacp->input_buffer = (gfloat*)g_malloc0(
    (AACENC_BLOCKSIZE * 2 + MAX_DS_FILTER_DELAY + INPUT_DELAY) *
    MAX_CHANNELS * sizeof(gfloat));

  {
    sbrConfiguration sbrConfig;

    aacp->offset_env_read = 0;
    aacp->offset_core_write = 0;

    if ( aacp->use_ps ) {
      aacp->offset_env_read = (MAX_DS_FILTER_DELAY + INPUT_DELAY)*MAX_CHANNELS;
      aacp->offset_core_write = CORE_INPUT_OFFSET_PS;
      aacp->offset_write = aacp->offset_env_read;
    }

    InitializeSbrDefaults(&sbrConfig);

    sbrConfig.usePs = aacp->use_ps;
    AdjustSbrSettings(&sbrConfig,
                      aacp->bitrate,
                      aacp->aac_channels,
                      aacp->aac_samplerate,
                      AACENC_TRANS_FAC,
                      24000);

    // GST_INFO_OBJECT (aacp, "EnvOpen input buffer (%d)",
    // aacp->offset_core_write);
    EnvOpen(&aacp->handle_sbr,
            aacp->input_buffer + aacp->offset_core_write,
            &sbrConfig,
            &aacp->aac_config.bandWidth);

    // set IIR 2:1 downsampling
    aacp->downsample_iir2 = TRUE;
    if ( aacp->use_ps ) {
      aacp->downsample_iir2 = FALSE;
    }
  }

  // set up 2:1 downsampling
  if (aacp->downsample_iir2) {
    InitIIR21_Resampler(&(aacp->IIR21_reSampler[0]));

#if ( MAX_CHANNELS == 2 )
    InitIIR21_Resampler(&(aacp->IIR21_reSampler[1]));
#endif

    g_assert(aacp->IIR21_reSampler[0].delay <= MAX_DS_FILTER_DELAY);
    aacp->offset_write += aacp->IIR21_reSampler[0].delay*MAX_CHANNELS;
  }

  // set up AAC encoder, now that sampling rate is known
  aacp->aac_config.sampleRate = aacp->aac_samplerate;

  if ( AacEncOpen(&aacp->handle, aacp->aac_config) != 0 ) {
    GST_WARNING_OBJECT(aacp, "couldn't open the encoder "
                       "(bitrate=%d,channels=%d,core sample rate=%d)...",
                       aacp->bitrate, aacp->aac_channels, aacp->aac_samplerate);
    if ( aacp->handle_sbr != NULL ) {
      // GST_INFO_OBJECT (aacp, "DESTROY - SBR");
      EnvClose(aacp->handle_sbr);
      aacp->handle_sbr = NULL;
    }
    aacp->handle = NULL;

    g_free(aacp->anc_data);
    aacp->anc_data = NULL;

    g_free(aacp->input_buffer);
    aacp->input_buffer = NULL;

    goto set_failed;
  }

  aacp->needed_samples = AACENC_BLOCKSIZE*aacp->channels*2;

  /////////////////////////////

  // now create a caps for it all
  // TODO(cpopescu): is this right ??
  srccaps = gst_caps_new_simple("audio/mpeg",
                                "mpegversion", G_TYPE_INT, mpegversion,
                                "channels", G_TYPE_INT, aacp->channels,
                                "rate", G_TYPE_INT,
                                2*aacp->aac_samplerate, NULL);

  {
    GstBuffer*codec_data;
    guint8*data;

    // basic codec data
    codec_data = gst_buffer_new_and_alloc(2);
    data = GST_BUFFER_DATA(codec_data);

    data[0] =
        // AAC object type
        (aac_object_type << 3) |
        // AAC sample rate index, highest 3 bits (out of 4)
        ((aac_sample_rate_index & 0x0E) >> 1);
    data[1] =
        // AAC sample rate index, lowest 1 bit (out of 4)
        ((aac_sample_rate_index & 0x01) << 7) |
        // AAC num channels
        (aac_num_channels << 3);

    // add to caps
    char codec_data_name[] = "codec_data";
    gst_caps_set_simple(srccaps,
                        codec_data_name,
                        GST_TYPE_BUFFER, codec_data, NULL);

    gst_buffer_unref(codec_data);
  }

  GST_INFO_OBJECT(aacp, "object_type: %d, sample_rate_index %d, "
    "num_channels %d, src pad caps: %" GST_PTR_FORMAT,
    (int)aac_object_type,
    (int)aac_sample_rate_index,
    (int)aac_num_channels,
    srccaps);

  ret = gst_pad_set_caps(aacp->srcpad, srccaps);
  gst_caps_unref(srccaps);

  return ret;

  // //////  ERROR PATH
 empty_caps:
  {
    gst_caps_unref(allowed_caps);
    return FALSE;
  }
 set_failed:
  {
    GST_WARNING_OBJECT(aacp, "aacp doesn't support the current configuration");
    return FALSE;
  }
}

static gboolean gst_aacp_sink_event(GstPad* pad, GstEvent* event) {
  GstAacp* aacp;
  gboolean ret;

  aacp = GST_AACP(gst_pad_get_parent(pad));

  switch ( GST_EVENT_TYPE(event) ) {
  case GST_EVENT_EOS: {
    // TODO(mihai): Flush..
    //       GstBuffer* outbuf;
    //       ret = TRUE;
    //       do {
    //         if (gst_pad_alloc_buffer_and_set_caps (aacp->srcpad,
    //                 GST_BUFFER_OFFSET_NONE, aacp->bytes,
    //                 GST_PAD_CAPS (aacp->srcpad), &outbuf) == GST_FLOW_OK) {
    //           gint ret_size;

    //           if ((ret_size = aacpEncEncode (aacp->handle, NULL, 0,
    //                       GST_BUFFER_DATA (outbuf), aacp->bytes)) > 0) {
    //             GST_BUFFER_SIZE (outbuf) = ret_size;
    //             GST_BUFFER_TIMESTAMP (outbuf) = GST_CLOCK_TIME_NONE;
    //             GST_BUFFER_DURATION (outbuf) = GST_CLOCK_TIME_NONE;
    //             gst_pad_push (aacp->srcpad, outbuf);
    //           } else {
    //             gst_buffer_unref (outbuf);
    //             ret = FALSE;
    //           }
    //         }
    //       } while (ret);

    ret = gst_pad_event_default(pad, event);
    break;
  }
  case GST_EVENT_NEWSEGMENT:
    ret = gst_pad_push_event(aacp->srcpad, event);
    break;
  case GST_EVENT_TAG:
    ret = gst_pad_event_default(pad, event);
    break;
  default:
    ret = gst_pad_event_default(pad, event);
    break;
  }
  gst_object_unref(aacp);
  return ret;
}

static GstFlowReturn gst_aacp_chain(GstPad* pad, GstBuffer* inbuf) {
  GstFlowReturn result = GST_FLOW_OK;
  GstBuffer* subbuf;
  GstAacp* aacp;
  guint size, available_size, frame_size;

  aacp = GST_AACP(gst_pad_get_parent(pad));

  if ( !GST_PAD_CAPS(aacp->srcpad) ) {
    if ( !gst_aacp_configure_source_pad(aacp) )
      goto nego_failed;
  }

  if ( aacp->handle == NULL )
    goto no_handle;

  size = GST_BUFFER_SIZE(inbuf);
  available_size = size;
  if ( aacp->cache )
    available_size += GST_BUFFER_SIZE(aacp->cache);
  frame_size = aacp->needed_samples * aacp->bps;

  // GST_INFO_OBJECT(aacp, "NEW BUFFER (size %d, accumulated %d, bps %d)",
  //                 size, available_size, aacp->bps);
  while ( true ) {
    // GST_INFO_OBJECT(aacp, "ITERATING (size %d, accumulated %d, bps %d)",
    //                 size, available_size, aacp->bps);
    if ( available_size / aacp->bps < aacp->needed_samples ) {
      if ( available_size > size ) {
        GstBuffer* merge;

        // this is panic! we got a buffer, but still don't have enough
        // data. Merge them and retry in the next cycle...
        merge = gst_buffer_merge(aacp->cache, inbuf);
        gst_buffer_unref(aacp->cache);
        gst_buffer_unref(inbuf);
        aacp->cache = merge;
      } else if ( available_size == size ) {
        // this shouldn't happen, but still...
        aacp->cache = inbuf;
      } else if ( available_size > 0 ) {
        aacp->cache = gst_buffer_create_sub(inbuf, size - available_size,
                                            available_size);
        GST_BUFFER_DURATION(aacp->cache) =
            GST_BUFFER_DURATION(inbuf) * GST_BUFFER_SIZE(aacp->cache) / size;
        GST_BUFFER_TIMESTAMP(aacp->cache) =
            GST_BUFFER_TIMESTAMP(inbuf) + (GST_BUFFER_DURATION(inbuf) *
            (size - available_size) / size);
        gst_buffer_unref(inbuf);
      } else {
        gst_buffer_unref(inbuf);
      }

      // GST_INFO_OBJECT(aacp, "SKIPPING (%d < %d)", available_size/aacp->bps,
      //                  aacp->needed_samples);
      goto done;
    }

    // create the frame
    if ( available_size > size ) {
      GstBuffer* merge;

      // merge
      subbuf = gst_buffer_create_sub(inbuf, 0,
                                     frame_size - (available_size - size));
      GST_BUFFER_DURATION(subbuf) =
        GST_BUFFER_DURATION(inbuf) * GST_BUFFER_SIZE(subbuf) / size;
      merge = gst_buffer_merge(aacp->cache, subbuf);
      gst_buffer_unref(aacp->cache);
      gst_buffer_unref(subbuf);
      subbuf = merge;
      aacp->cache = NULL;
    } else {
      subbuf = gst_buffer_create_sub(inbuf, size - available_size, frame_size);
      GST_BUFFER_DURATION(subbuf) =
          GST_BUFFER_DURATION(inbuf) * GST_BUFFER_SIZE(subbuf) / size;
      GST_BUFFER_TIMESTAMP(subbuf) =
          GST_BUFFER_TIMESTAMP(inbuf) + (GST_BUFFER_DURATION(inbuf) *
          (size - available_size) / size);
    }

    gint output_buffer_size;
    guint8* output_buffer;
    if ( aacp->outputformat != GST_OUTPUTFORMAT_ADTS ) {
      output_buffer_size = (6144/8)*MAX_CHANNELS;
      output_buffer = (guint8*)g_malloc(output_buffer_size);
    } else {
      output_buffer_size = ADTS_HEADER_SIZE+(6144/8)*MAX_CHANNELS;
      output_buffer = (guint8*)g_malloc(output_buffer_size);

      // create the ADTS header
      adts_hdr((char*)output_buffer, &aacp->aac_config);
    }

    gint samples_read = aacp->needed_samples;

    // copy from short to float input buffer */
    // GST_INFO_OBJECT (aacp, "reading into input buffer (%d), %d samples",
    //                  aacp->offset_write, samples_read);
    gint16* input_data = (gint16*)GST_BUFFER_DATA(subbuf);
    for ( gint sample = 0; sample < samples_read; sample++ ) {
      aacp->input_buffer[aacp->offset_write+sample] = (float)input_data[sample];
    }

    // encode one SBR frame
    // GST_INFO_OBJECT (aacp, "EnvEncodeFrame (%d, %d)",
    //                  aacp->offset_env_read, aacp->offset_core_write);
    EnvEncodeFrame(aacp->handle_sbr,
                   aacp->input_buffer + aacp->offset_env_read,
                   aacp->input_buffer + aacp->offset_core_write,
                   MAX_CHANNELS,
                   (unsigned int*)&aacp->anc_count,
                   (unsigned char*)aacp->anc_data);

    // 2:1 downsampling for AAC core
    if ( aacp->downsample_iir2 ) {
      for ( gint channel = 0; channel < aacp->aac_channels; channel++ ) {
        int out_samples;
        // GST_INFO_OBJECT(aacp, "IIR21_Downsample (%d)", aacp->offset_write);
        IIR21_Downsample(&(aacp->IIR21_reSampler[channel]),
                          aacp->input_buffer+aacp->offset_write+channel,
                          samples_read/aacp->channels,
                          MAX_CHANNELS,
                          aacp->input_buffer+channel,
                          &out_samples,
                          MAX_CHANNELS);
        // GST_INFO_OBJECT(aacp, "IIR21_Downsample returned (%d)", out_samples);
      }
    }

    // SBR side info data is passed as ancillary data
    // JUNK - in the original code ancDataLength is ALWAYS 0
    // if (aacp->anc_count == 0) {
    //   aacp->anc_count = ancDataLength;
    // }

    // encode one AAC frame
    gint output_count;
    if ( aacp->handle_sbr != NULL && aacp->use_ps ) {
      // GST_INFO_OBJECT (aacp, "AacEncEncode, stride=1");
      AacEncEncode(aacp->handle,
                   aacp->input_buffer,
                   1,   // stride
                   (const unsigned char*)aacp->anc_data,
                   (unsigned int*)&aacp->anc_count,
                   (unsigned int*)(output_buffer +
                                   ((aacp->outputformat !=
                                     GST_OUTPUTFORMAT_ADTS) ?
                                    0 : ADTS_HEADER_SIZE)),
                   (int*)&output_count);

      memcpy(aacp->input_buffer, aacp->input_buffer + AACENC_BLOCKSIZE,
             CORE_INPUT_OFFSET_PS * sizeof(float));
    } else {
      // GST_INFO_OBJECT(aacp, "AacEncEncode, stride=MAX_CHANNELS");
      AacEncEncode(aacp->handle,
                   aacp->input_buffer+aacp->offset_core_read,
                   MAX_CHANNELS,
                   (const unsigned char*)aacp->anc_data,
                   (unsigned int*)&aacp->anc_count,
                   (unsigned int*)(output_buffer +
                                   ((aacp->outputformat !=
                                     GST_OUTPUTFORMAT_ADTS) ?
                                    0 : ADTS_HEADER_SIZE)),
                   (int*)&output_count);

      if ( aacp->handle_sbr != NULL ) {
        memmove(aacp->input_buffer,
                aacp->input_buffer + AACENC_BLOCKSIZE * 2 * MAX_CHANNELS,
                aacp->offset_write * sizeof(float));
      }
    }

    // Write one frame of encoded audio to file
    // GST_INFO_OBJECT(aacp, "encoded %d bytes (%d ancilliary data)",
    //                  output_count, aacp->anc_count);
    if ( output_count > 0 ) {
      if ( aacp->outputformat == GST_OUTPUTFORMAT_ADTS ) {
        adts_hdr_up((char*)output_buffer, output_count);
      }

      GstBuffer* outbuf;
      outbuf = gst_buffer_new();
      GST_BUFFER_DATA(outbuf) = output_buffer;
      GST_BUFFER_MALLOCDATA(outbuf) = output_buffer;
      GST_BUFFER_SIZE(outbuf) = output_count +
        ((aacp->outputformat != GST_OUTPUTFORMAT_ADTS) ? 0 : ADTS_HEADER_SIZE);

      if ( aacp->cache_time != GST_CLOCK_TIME_NONE ) {
        GST_BUFFER_TIMESTAMP(outbuf) = aacp->cache_time;
        aacp->cache_time = GST_CLOCK_TIME_NONE;
      } else {
        GST_BUFFER_TIMESTAMP(outbuf) = GST_BUFFER_TIMESTAMP(subbuf);
      }
      GST_BUFFER_DURATION(outbuf) = GST_BUFFER_DURATION(subbuf);
      if ( aacp->cache_duration ) {
        GST_BUFFER_DURATION(outbuf) += aacp->cache_duration;
        aacp->cache_duration = 0;
      }
      gst_buffer_set_caps(outbuf, GST_PAD_CAPS(aacp->srcpad));

      GST_LOG_OBJECT(aacp,
                     "pushing %d bytes (%d ancilliary data)",
                     output_count, aacp->anc_count);
      result = gst_pad_push(aacp->srcpad, outbuf);
      if ( result != GST_FLOW_OK ) {
        GST_DEBUG_OBJECT(aacp, "flow return: %s", gst_flow_get_name(result));
      }
    } else {
      g_free(output_buffer);
      GST_LOG_OBJECT(aacp, "not enough data, delaying");
      // FIXME: what I'm doing here isn't fully correct, but there
      // really isn't a better way yet.
      // Problem is that libfaac caches buffers (for encoding
      // purposes), so the timestamp of the outgoing buffer isn't
      // the same as the timestamp of the data that I pushed in.
      // However, I don't know the delay between those two so I
      // cannot really say aything about it. This is a bad guess.
      if ( aacp->cache_time != GST_CLOCK_TIME_NONE ) {
        aacp->cache_time = GST_BUFFER_TIMESTAMP(subbuf);
      }
      aacp->cache_duration += GST_BUFFER_DURATION(subbuf);
      result = GST_FLOW_OK;
    }

    // GST_INFO_OBJECT (aacp, "BUFFER DONE");
    available_size -= frame_size;
    gst_buffer_unref(subbuf);
  }

 done:
  // GST_INFO_OBJECT (aacp, "EXITING");
  gst_object_unref(aacp);
  return result;

  // /// ERRORS
 no_handle:
  {
    GST_ELEMENT_ERROR(aacp, CORE, NEGOTIATION, (NULL),
                      ("format wasn't negotiated before chain function"));
    gst_buffer_unref(inbuf);
    result = GST_FLOW_ERROR;
    goto done;
  }
 nego_failed:
  {
    GST_ELEMENT_ERROR(
        aacp, CORE, NEGOTIATION, (NULL),
        ("failed to negotiate MPEG/AAC format with next element"));
    gst_buffer_unref(inbuf);
    result = GST_FLOW_ERROR;
    goto done;
  }
}

static void gst_aacp_set_property(GObject* object,
                                  guint prop_id,
                                  const GValue* value,
                                  GParamSpec* pspec) {
  GstAacp* aacp = GST_AACP(object);

  GST_OBJECT_LOCK(aacp);

  switch ( prop_id ) {
  case ARG_BITRATE:
    aacp->bitrate = g_value_get_int(value);
    break;
  case ARG_OUTPUTFORMAT:
    aacp->outputformat = g_value_get_enum(value);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
    break;
  }

  GST_OBJECT_UNLOCK(aacp);
}

static void gst_aacp_get_property(GObject* object,
                                  guint prop_id,
                                  GValue* value,
                                  GParamSpec* pspec) {
  GstAacp* aacp = GST_AACP(object);

  GST_OBJECT_LOCK(aacp);

  switch ( prop_id ) {
  case ARG_BITRATE:
    g_value_set_int(value, aacp->bitrate);
    break;
  case ARG_OUTPUTFORMAT:
    g_value_set_enum(value, aacp->outputformat);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
    break;
  }

  GST_OBJECT_UNLOCK(aacp);
}

static GstStateChangeReturn gst_aacp_change_state(GstElement* element,
                                                  GstStateChange transition) {
  GstAacp* aacp = GST_AACP(element);

  GstStateChangeReturn ret =
      GST_ELEMENT_CLASS(parent_class)->change_state(element, transition);

  // downwards state changes
  switch ( transition ) {
  case GST_STATE_CHANGE_PAUSED_TO_READY: {
    GST_OBJECT_LOCK(aacp);
    if ( aacp->handle != NULL ) {
      // GST_INFO_OBJECT(aacp, "DESTROY - AAC");
      AacEncClose(aacp->handle);
      aacp->handle = NULL;
    }
    if ( aacp->handle_sbr !=  NULL ) {
      // GST_INFO_OBJECT(aacp, "DESTROY - SBR");
      EnvClose(aacp->handle_sbr);
      aacp->handle_sbr = NULL;
    }
    g_free(aacp->anc_data);
    aacp->anc_data = NULL;
    g_free(aacp->input_buffer);
    aacp->input_buffer = NULL;

    if (aacp->cache) {
      gst_buffer_unref(aacp->cache);
      aacp->cache = NULL;
    }
    aacp->cache_time = GST_CLOCK_TIME_NONE;
    aacp->cache_duration = 0;
    aacp->samplerate = -1;
    aacp->channels = -1;
    // GST_INFO_OBJECT(aacp, "DESTROY - DONE");
    GST_OBJECT_UNLOCK(aacp);
    break;
  }
  default:
    break;
  }

  return ret;
}

static gboolean plugin_init(GstPlugin* plugin) {
  return gst_element_register(plugin, "aacp", GST_RANK_NONE, GST_TYPE_AACP);
}

// these are coming from the plugin makefile,
// we will need to move it there once we move
// out plugins into their own modules...
#define VERSION "0.10.1"
#define PACKAGE "gst-plugins-whispercast"
#define GST_PACKAGE_NAME "GStreamer Whispercast Plug-ins"
#define GST_PACKAGE_ORIGIN "Whispersoft s.r.l."

// GST_PLUGIN_DEFINE(GST_VERSION_MAJOR,
//     GST_VERSION_MINOR,
//     "aacp",
//     "Free AAC Encoder(AACP)",
//     plugin_init, VERSION, "-", GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)

void register_aacp() {
  static GstPluginDesc plugin_desc_ = {
    GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    (gchar*)("aacp"),
    (gchar*)("AAC+ Encoder (HE-AAC)"),
    plugin_init,
    (gchar*)(VERSION),
    (gchar*)("LGPL"),
    (gchar*)(PACKAGE),
    (gchar*)(GST_PACKAGE_NAME),
    (gchar*)(GST_PACKAGE_ORIGIN),
    GST_PADDING_INIT
  };
  _gst_plugin_register_static(&plugin_desc_);
}
