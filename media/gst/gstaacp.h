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
// Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
//
// Author: Mihai Ianculescu

#ifndef __STREAMING_GST_GST_AACP_H__
#define __STREAMING_GST_GST_AACP_H__

#include <gst/gst.h>

#ifdef __cplusplus
extern "C" {
#endif

#include <whispermedialib/third-party/aacplusenc/aacenc.h>
#include <whispermedialib/third-party/aacplusenc/sbr_main.h>
#include <whispermedialib/third-party/aacplusenc/resampler.h>
#include <whispermedialib/third-party/aacplusenc/cfftn.h>

#ifdef __cplusplus
}
#endif

G_BEGIN_DECLS

#define GST_TYPE_AACP                           \
  (gst_aacp_get_type())
#define GST_AACP(obj)                                           \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_AACP, GstAacp))
#define GST_AACP_CLASS(klass)                                           \
  (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_AACP, GstAacpClass))
#define GST_IS_AACP(obj)                                \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_AACP))
#define GST_IS_AACP_CLASS(klass)                        \
  (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_AACP))

typedef struct _GstAacp {
  GstElement element;

  // pads
  GstPad* srcpad;
  GstPad* sinkpad;

  // stream properties
  gint samplerate;
  gint channels;
  gint bitrate;
  gint bps;
  gint outputformat;

  ////////////////////
  //
  // AACP stuff
  //
  struct AAC_ENCODER* handle;
  AACENC_CONFIG aac_config;
  HANDLE_SBR_ENCODER handle_sbr;      //  hEnvEnc

  ////////////////////
  //
  // AACP properties
  //

  // UNUSED:
  // gboolean upsample;              // bDoUpsample
  // gboolean resample_iir32;        // bDoIIR32Resample
  //
  gint aac_channels;                 // nChannelsAAC
  gint sbr_channels;                 // nChannelsSBR
  gboolean use_ps;                   // useParametricStereo
  gint aac_samplerate;               // sampleRateAAC
  gboolean downsample_iir2;          // bDoIIR2Downsample
  gint offset_core_write;            // coreWriteOffset
  gint offset_core_read;             // coreReadOffset
  gint offset_env_write;             // envWriteOffset
  gint offset_env_read;              // envReadOffset
  gint offset_write;                 // writeOffset
  gint offset_upsample_read;         // upsampleReadOffset
  gint needed_samples;               // inSamples
  guint8* anc_data;                  // ancDataBytes
                                     // Size: [MAX_PAYLOAD_SIZE]
  gint anc_count;                    // numAncDataBytes
  gfloat* input_buffer;
  // Size: [(AACENC_BLOCKSIZE * 2 + MAX_DS_FILTER_DELAY + INPUT_DELAY) *
  //         MAX_CHANNELS];
  IIR21_RESAMPLER IIR21_reSampler[MAX_CHANNELS];

  // cache of the input
  GstBuffer* cache;
  guint64 cache_time;
  guint64 cache_duration;
} GstAacp;

typedef struct _GstAacpClass {
  GstElementClass parent_class;
} GstAacpClass;

GType gst_aacp_get_type(void);

void register_aacp();

G_END_DECLS

#endif    // __GST_AACP_H__
