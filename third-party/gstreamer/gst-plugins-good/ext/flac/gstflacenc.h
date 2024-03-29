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


#ifndef __GST_FLAC_ENC_H__
#define __GST_FLAC_ENC_H__

#include <gst/gst.h>

#include <FLAC/all.h>

#if !defined(FLAC_API_VERSION_CURRENT) || FLAC_API_VERSION_CURRENT < 8
#define LEGACY_FLAC
#else
#undef LEGACY_FLAC
#endif 

G_BEGIN_DECLS

#define GST_TYPE_FLAC_ENC (gst_flac_enc_get_type())
#define GST_FLAC_ENC(obj) G_TYPE_CHECK_INSTANCE_CAST(obj, GST_TYPE_FLAC_ENC, GstFlacEnc)
#define GST_FLAC_ENC_CLASS(klass) G_TYPE_CHECK_CLASS_CAST(klass, GST_TYPE_FLAC_ENC, GstFlacEncClass)
#define GST_IS_FLAC_ENC(obj) G_TYPE_CHECK_INSTANCE_TYPE(obj, GST_TYPE_FLAC_ENC)
#define GST_IS_FLAC_ENC_CLASS(klass) G_TYPE_CHECK_CLASS_TYPE(klass, GST_TYPE_FLAC_ENC)

typedef struct _GstFlacEnc GstFlacEnc;
typedef struct _GstFlacEncClass GstFlacEncClass;

struct _GstFlacEnc {
  GstElement     element;

  GstPad        *sinkpad;
  GstPad        *srcpad;

  GstFlowReturn  last_flow; /* save flow from last push so we can pass the
                             * correct flow return upstream in case the push
                             * fails for some reason */

  gboolean       first;
  GstBuffer     *first_buf;
  guint64        offset;
  guint64        samples_written;
  gboolean       eos;
  gint           channels;
  gint           depth;
  gint           sample_rate;
  gboolean       negotiated;
  gint           quality;
  gboolean       stopped;
  FLAC__int32   *data;

#if !defined(FLAC_API_VERSION_CURRENT) || FLAC_API_VERSION_CURRENT < 8
  FLAC__SeekableStreamEncoder *encoder;
#else
  FLAC__StreamEncoder *encoder;
#endif
  FLAC__StreamMetadata **meta;

  GstTagList *     tags;

  /* queue headers until we have them all so we can add streamheaders to caps */
  gboolean         got_headers;
  GList           *headers;
};

struct _GstFlacEncClass {
  GstElementClass parent_class;
};

GType gst_flac_enc_get_type(void);

G_END_DECLS

#endif /* __GST_FLAC_ENC_H__ */
