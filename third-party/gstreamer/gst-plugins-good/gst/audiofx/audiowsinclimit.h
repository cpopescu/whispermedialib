/* -*- c-basic-offset: 2 -*-
 * 
 * GStreamer
 * Copyright (C) 1999-2001 Erik Walthinsen <omega@cse.ogi.edu>
 *               2006 Dreamlab Technologies Ltd. <mathis.hofer@dreamlab.net>
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
 * 
 * 
 * this windowed sinc filter is taken from the freely downloadable DSP book,
 * "The Scientist and Engineer's Guide to Digital Signal Processing",
 * chapter 16
 * available at http://www.dspguide.com/
 *
 */

#ifndef __GST_AUDIO_WSINC_LIMIT_H__
#define __GST_AUDIO_WSINC_LIMIT_H__

#include <gst/gst.h>
#include <gst/audio/gstaudiofilter.h>

G_BEGIN_DECLS

#define GST_TYPE_AUDIO_WSINC_LIMIT \
  (audio_wsinclimit_get_type())
#define GST_AUDIO_WSINC_LIMIT(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_AUDIO_WSINC_LIMIT,GstAudioWSincLimit))
#define GST_AUDIO_WSINC_LIMIT_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_AUDIO_WSINC_LIMIT,GstAudioWSincLimitClass))
#define GST_IS_AUDIO_WSINC_LIMIT(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_AUDIO_WSINC_LIMIT))
#define GST_IS_AUDIO_WSINC_LIMIT_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_AUDIO_WSINC_LIMIT))

typedef struct _GstAudioWSincLimit GstAudioWSincLimit;
typedef struct _GstAudioWSincLimitClass GstAudioWSincLimitClass;

typedef void (*GstAudioWSincLimitProcessFunc) (GstAudioWSincLimit *, guint8 *, guint8 *, guint);

/**
 * GstAudioWSincLimit:
 *
 * Opaque data structure.
 */
struct _GstAudioWSincLimit {
  GstAudioFilter element;

  /* < private > */
  GstAudioWSincLimitProcessFunc process;

  gint mode;
  gint window;
  gfloat cutoff;
  gint kernel_length;           /* length of the filter kernel */

  gdouble *residue;             /* buffer for left-over samples from previous buffer */
  gdouble *kernel;              /* filter kernel */
  gboolean have_kernel;
  gint residue_length;
  guint64 latency;
  GstClockTime next_ts;
  guint64 next_off;
};

struct _GstAudioWSincLimitClass {
  GstAudioFilterClass parent_class;
};

GType audio_wsinclimit_get_type (void);

G_END_DECLS

#endif /* __GST_AUDIO_WSINC_LIMIT_H__ */
