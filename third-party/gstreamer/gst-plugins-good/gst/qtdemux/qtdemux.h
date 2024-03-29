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


#ifndef __GST_QTDEMUX_H__
#define __GST_QTDEMUX_H__

#include <gst/gst.h>
#include <gst/base/gstadapter.h>

G_BEGIN_DECLS

GST_DEBUG_CATEGORY_EXTERN (qtdemux_debug);
#define GST_CAT_DEFAULT qtdemux_debug

#define GST_TYPE_QTDEMUX \
  (gst_qtdemux_get_type())
#define GST_QTDEMUX(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_QTDEMUX,GstQTDemux))
#define GST_QTDEMUX_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_QTDEMUX,GstQTDemuxClass))
#define GST_IS_QTDEMUX(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_QTDEMUX))
#define GST_IS_QTDEMUX_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_QTDEMUX))

#define GST_QTDEMUX_CAST(obj) ((GstQTDemux *)(obj))

#define GST_QTDEMUX_MAX_STREAMS         8

typedef struct _GstQTDemux GstQTDemux;
typedef struct _GstQTDemuxClass GstQTDemuxClass;
typedef struct _QtDemuxStream QtDemuxStream;

struct _GstQTDemux {
  GstElement element;

  /* pads */
  GstPad *sinkpad;

  QtDemuxStream *streams[GST_QTDEMUX_MAX_STREAMS];
  gint     n_streams;
  gint     n_video_streams;
  gint     n_audio_streams;

  GNode *moov_node;
  GNode *moov_node_compressed;

  guint32 timescale;
  guint32 duration;

  gint state;

  gboolean pullbased;

  /* push based variables */
  guint neededbytes;
  guint todrop;
  GstAdapter *adapter;
  GstBuffer *mdatbuffer;

  /* offset of the media data (i.e.: Size of header) */
  guint64 offset;
  /* offset of the mdat atom */
  guint64 mdatoffset;

  GstTagList *tag_list;

  /* track stuff */
  guint64 last_ts;

  /* configured playback region */
  GstSegment segment;
  gboolean segment_running;
};

struct _GstQTDemuxClass {
  GstElementClass parent_class;
};

GType gst_qtdemux_get_type (void);

G_END_DECLS

#endif /* __GST_QTDEMUX_H__ */
