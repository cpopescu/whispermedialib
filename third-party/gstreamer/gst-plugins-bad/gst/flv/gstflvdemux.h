/* GStreamer
 * Copyright (C) <2007> Julien Moutte <julien@moutte.net>
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

#ifndef __FLV_DEMUX_H__
#define __FLV_DEMUX_H__

#include <gst/gst.h>
#include <gst/base/gstadapter.h>

G_BEGIN_DECLS
#define GST_TYPE_FLV_DEMUX \
  (gst_flv_demux_get_type())
#define GST_FLV_DEMUX(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_FLV_DEMUX,GstFLVDemux))
#define GST_FLV_DEMUX_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_FLV_DEMUX,GstFLVDemuxClass))
#define GST_IS_FLV_DEMUX(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_FLV_DEMUX))
#define GST_IS_FLV_DEMUX_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_FLV_DEMUX))
typedef struct _GstFLVDemux GstFLVDemux;
typedef struct _GstFLVDemuxClass GstFLVDemuxClass;

typedef enum
{
  FLV_STATE_HEADER,
  FLV_STATE_TAG_TYPE,
  FLV_STATE_TAG_VIDEO,
  FLV_STATE_TAG_AUDIO,
  FLV_STATE_TAG_SCRIPT,
  FLV_STATE_DONE,
  FLV_STATE_NONE
} GstFLVDemuxState;

struct _GstFLVDemux
{
  GstElement element;

  GstPad *sinkpad;

  GstPad *audio_pad;
  GstPad *video_pad;
  
  GstIndex *index;
  gint index_id;
  
  GArray * times;
  GArray * filepositions;

  GstAdapter *adapter;

  GstSegment *segment;

  GstEvent *new_seg_event;

  GstTagList *taglist;

  GstFLVDemuxState state;

  guint64 offset;
  guint64 cur_tag_offset;
  GstClockTime duration;
  guint64 tag_size;
  guint64 tag_data_size;

  /* Audio infos */
  guint16 rate;
  guint16 channels;
  guint16 width;
  guint16 audio_codec_tag;
  guint64 audio_offset;
  gboolean audio_need_discont;
  gboolean audio_need_segment;
  gboolean audio_linked;
  GstBuffer * audio_codec_data;

  /* Video infos */
  guint32 w;
  guint32 h;
  guint32 par_x;
  guint32 par_y;
  guint16 video_codec_tag;
  guint64 video_offset;
  gboolean video_need_discont;
  gboolean video_need_segment;
  gboolean video_linked;
  gboolean got_par;
  GstBuffer * video_codec_data;

  gboolean random_access;
  gboolean need_header;
  gboolean has_audio;
  gboolean has_video;
  gboolean push_tags;
  gboolean strict;
  gboolean flushing;
};

struct _GstFLVDemuxClass
{
  GstElementClass parent_class;
};

GType gst_flv_demux_get_type (void);

gboolean gst_flv_demux_query (GstPad * pad, GstQuery * query);
gboolean gst_flv_demux_src_event (GstPad * pad, GstEvent * event);

G_END_DECLS
#endif /* __FLV_DEMUX_H__ */
