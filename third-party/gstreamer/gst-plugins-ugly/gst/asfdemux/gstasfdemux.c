/* GStreamer ASF/WMV/WMA demuxer
 * Copyright (C) 1999 Erik Walthinsen <omega@cse.ogi.edu>
 * Copyright (C) 2006-2007 Tim-Philipp Müller <tim centricular net>
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

/* TODO:
 *
 * - _loop():
 *   stop if at end of segment if != end of file, ie. demux->segment.stop
 *
 * - _chain(): fix newsegment events for live streams where timestamps don't
 *   start at zero (need sample files/streams for this)
 *
 * - fix packet parsing:
 *   there's something wrong with timestamps for packets with keyframes,
 *   and durations too.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gstutils.h>
#include <gst/riff/riff-media.h>
#include <gst/gst-i18n-plugin.h>
#include <stdlib.h>
#include <string.h>

#include "gstasfdemux.h"
#include "asfheaders.h"
#include "asfpacket.h"

static GstStaticPadTemplate gst_asf_demux_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-ms-asf")
    );

static GstStaticPadTemplate audio_src_template =
GST_STATIC_PAD_TEMPLATE ("audio_%02d",
    GST_PAD_SRC,
    GST_PAD_SOMETIMES,
    GST_STATIC_CAPS_ANY);

static GstStaticPadTemplate video_src_template =
GST_STATIC_PAD_TEMPLATE ("video_%02d",
    GST_PAD_SRC,
    GST_PAD_SOMETIMES,
    GST_STATIC_CAPS_ANY);

/* size of an ASF object header, ie. GUID (16 bytes) + object size (8 bytes) */
#define ASF_OBJECT_HEADER_SIZE  (16+8)

/* FIXME: get rid of this */
/* abuse this GstFlowReturn enum for internal usage */
#define ASF_FLOW_NEED_MORE_DATA  99

#define gst_asf_get_flow_name(flow)    \
  (flow == ASF_FLOW_NEED_MORE_DATA) ?  \
  "need-more-data" : gst_flow_get_name (flow)

GST_DEBUG_CATEGORY (asfdemux_dbg);

static GstStateChangeReturn gst_asf_demux_change_state (GstElement * element,
    GstStateChange transition);
static gboolean gst_asf_demux_element_send_event (GstElement * element,
    GstEvent * event);
static gboolean gst_asf_demux_send_event_unlocked (GstASFDemux * demux,
    GstEvent * event);
static gboolean gst_asf_demux_handle_src_query (GstPad * pad, GstQuery * query);
static const GstQueryType *gst_asf_demux_get_src_query_types (GstPad * pad);
static GstFlowReturn gst_asf_demux_chain (GstPad * pad, GstBuffer * buf);
static gboolean gst_asf_demux_sink_event (GstPad * pad, GstEvent * event);
static GstFlowReturn gst_asf_demux_process_object (GstASFDemux * demux,
    guint8 ** p_data, guint64 * p_size);
static gboolean gst_asf_demux_activate (GstPad * sinkpad);
static gboolean gst_asf_demux_activate_push (GstPad * sinkpad, gboolean active);
static gboolean gst_asf_demux_activate_pull (GstPad * sinkpad, gboolean active);
static void gst_asf_demux_loop (GstASFDemux * demux);
static void
gst_asf_demux_process_queued_extended_stream_objects (GstASFDemux * demux);
static void gst_asf_demux_activate_ext_props_streams (GstASFDemux * demux);
static gboolean gst_asf_demux_pull_headers (GstASFDemux * demux);
static void gst_asf_demux_pull_indices (GstASFDemux * demux);
static GstFlowReturn gst_asf_demux_handle_data (GstASFDemux * demux,
    guint8 ** p_data, guint64 * p_size);
static void gst_asf_demux_reset_stream_state_after_discont (GstASFDemux * asf);
static gboolean
gst_asf_demux_parse_data_object_start (GstASFDemux * demux, guint8 * data);
static void gst_asf_demux_descramble_buffer (GstASFDemux * demux,
    AsfStream * stream, GstBuffer ** p_buffer);
static void gst_asf_demux_activate_stream (GstASFDemux * demux,
    AsfStream * stream);
static GstStructure *gst_asf_demux_get_metadata_for_stream (GstASFDemux * d,
    guint stream_num);

GST_BOILERPLATE (GstASFDemux, gst_asf_demux, GstElement, GST_TYPE_ELEMENT);

static void
gst_asf_demux_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);
  static GstElementDetails gst_asf_demux_details = {
    "ASF Demuxer",
    "Codec/Demuxer",
    "Demultiplexes ASF Streams",
    "Owen Fraser-Green <owen@discobabe.net>"
  };

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&audio_src_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&video_src_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_asf_demux_sink_template));

  gst_element_class_set_details (element_class, &gst_asf_demux_details);
}

static void
gst_asf_demux_class_init (GstASFDemuxClass * klass)
{
  GstElementClass *gstelement_class;

  gstelement_class = (GstElementClass *) klass;

  gstelement_class->change_state =
      GST_DEBUG_FUNCPTR (gst_asf_demux_change_state);
  gstelement_class->send_event =
      GST_DEBUG_FUNCPTR (gst_asf_demux_element_send_event);
}

static void
gst_asf_demux_free_stream (GstASFDemux * demux, AsfStream * stream)
{
  gst_buffer_replace (&stream->cache, NULL);
  gst_buffer_replace (&stream->payload, NULL);
  gst_caps_replace (&stream->caps, NULL);
  if (stream->pending_tags) {
    gst_tag_list_free (stream->pending_tags);
    stream->pending_tags = NULL;
  }
  if (stream->pad) {
    if (stream->active)
      gst_element_remove_pad (GST_ELEMENT_CAST (demux), stream->pad);
    else
      gst_object_unref (stream->pad);
    stream->pad = NULL;
  }
  if (stream->payloads) {
    g_array_free (stream->payloads, TRUE);
    stream->payloads = NULL;
  }
  if (stream->ext_props.valid) {
    g_free (stream->ext_props.payload_extensions);
    stream->ext_props.payload_extensions = NULL;
  }
}

static void
gst_asf_demux_reset (GstASFDemux * demux)
{
  GST_LOG_OBJECT (demux, "resetting");

  gst_segment_init (&demux->segment, GST_FORMAT_UNDEFINED);
  demux->segment_running = FALSE;
  if (demux->adapter) {
    gst_adapter_clear (demux->adapter);
    g_object_unref (demux->adapter);
    demux->adapter = NULL;
  }
  if (demux->taglist) {
    gst_tag_list_free (demux->taglist);
    demux->taglist = NULL;
  }
  if (demux->metadata) {
    gst_caps_unref (demux->metadata);
    demux->metadata = NULL;
  }
  demux->state = GST_ASF_DEMUX_STATE_HEADER;
  g_free (demux->objpath);
  demux->objpath = NULL;
  g_strfreev (demux->languages);
  demux->languages = NULL;
  demux->num_languages = 0;
  g_slist_foreach (demux->ext_stream_props, (GFunc) gst_mini_object_unref,
      NULL);
  g_slist_free (demux->ext_stream_props);
  demux->ext_stream_props = NULL;
  while (demux->num_streams > 0) {
    gst_asf_demux_free_stream (demux, &demux->stream[demux->num_streams - 1]);
    --demux->num_streams;
  }
  memset (demux->stream, 0, sizeof (demux->stream));
  demux->num_audio_streams = 0;
  demux->num_video_streams = 0;
  demux->num_streams = 0;
  demux->activated_streams = FALSE;
  demux->first_ts = GST_CLOCK_TIME_NONE;
  demux->state = GST_ASF_DEMUX_STATE_HEADER;
  demux->seekable = FALSE;
  demux->broadcast = FALSE;
  demux->sidx_interval = 0;
  demux->sidx_num_entries = 0;
  g_free (demux->sidx_entries);
  demux->sidx_entries = NULL;
}

static void
gst_asf_demux_init (GstASFDemux * demux, GstASFDemuxClass * klass)
{
  demux->sinkpad =
      gst_pad_new_from_static_template (&gst_asf_demux_sink_template, "sink");
  gst_pad_set_chain_function (demux->sinkpad,
      GST_DEBUG_FUNCPTR (gst_asf_demux_chain));
  gst_pad_set_event_function (demux->sinkpad,
      GST_DEBUG_FUNCPTR (gst_asf_demux_sink_event));
  gst_pad_set_activate_function (demux->sinkpad,
      GST_DEBUG_FUNCPTR (gst_asf_demux_activate));
  gst_pad_set_activatepull_function (demux->sinkpad,
      GST_DEBUG_FUNCPTR (gst_asf_demux_activate_pull));
  gst_pad_set_activatepush_function (demux->sinkpad,
      GST_DEBUG_FUNCPTR (gst_asf_demux_activate_push));
  gst_element_add_pad (GST_ELEMENT (demux), demux->sinkpad);

  /* We should zero everything to be on the safe side */
  demux->num_audio_streams = 0;
  demux->num_video_streams = 0;
  demux->num_streams = 0;

  demux->taglist = NULL;
  demux->first_ts = GST_CLOCK_TIME_NONE;
  demux->state = GST_ASF_DEMUX_STATE_HEADER;
}

static gboolean
gst_asf_demux_activate (GstPad * sinkpad)
{
  if (gst_pad_check_pull_range (sinkpad)) {
    return gst_pad_activate_pull (sinkpad, TRUE);
  } else {
    return gst_pad_activate_push (sinkpad, TRUE);
  }
}

static gboolean
gst_asf_demux_activate_push (GstPad * sinkpad, gboolean active)
{
  GstASFDemux *demux;

  demux = GST_ASF_DEMUX (GST_OBJECT_PARENT (sinkpad));

  demux->state = GST_ASF_DEMUX_STATE_HEADER;
  demux->streaming = TRUE;

  return TRUE;
}

static gboolean
gst_asf_demux_activate_pull (GstPad * pad, gboolean active)
{
  GstASFDemux *demux;

  demux = GST_ASF_DEMUX (GST_OBJECT_PARENT (pad));

  if (active) {
    demux->state = GST_ASF_DEMUX_STATE_HEADER;
    demux->streaming = FALSE;

    return gst_pad_start_task (pad, (GstTaskFunction) gst_asf_demux_loop,
        demux);
  } else {
    return gst_pad_stop_task (pad);
  }
}


static gboolean
gst_asf_demux_sink_event (GstPad * pad, GstEvent * event)
{
  GstASFDemux *demux;
  gboolean ret = TRUE;

  demux = GST_ASF_DEMUX (gst_pad_get_parent (pad));

  GST_LOG_OBJECT (demux, "handling %s event", GST_EVENT_TYPE_NAME (event));
  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_NEWSEGMENT:{
      GstFormat newsegment_format;
      gint64 newsegment_start;

      gst_event_parse_new_segment (event, NULL, NULL, &newsegment_format,
          &newsegment_start, NULL, NULL);

      if (newsegment_format != GST_FORMAT_BYTES) {
        GST_WARNING_OBJECT (demux, "newsegment format not BYTES, ignoring");
        gst_event_unref (event);
        break;
      }

      GST_OBJECT_LOCK (demux);
      gst_asf_demux_reset_stream_state_after_discont (demux);
      GST_OBJECT_UNLOCK (demux);
      break;
    }
    case GST_EVENT_EOS:{
      if (demux->state == GST_ASF_DEMUX_STATE_HEADER) {
        GST_ELEMENT_ERROR (demux, STREAM, DEMUX,
            (_("This stream contains no data.")),
            ("got eos and didn't receive a complete header object"));
        break;
      }
      GST_OBJECT_LOCK (demux);
      gst_adapter_clear (demux->adapter);
      GST_OBJECT_UNLOCK (demux);
      gst_asf_demux_send_event_unlocked (demux, event);
      break;
    }

    default:
      ret = gst_pad_event_default (pad, event);
      break;
  }

  gst_object_unref (demux);
  return ret;
}

static gboolean
gst_asf_demux_seek_index_lookup (GstASFDemux * demux, guint * packet,
    GstClockTime seek_time, GstClockTime * p_idx_time)
{
  GstClockTime idx_time;
  guint idx;

  if (demux->sidx_num_entries == 0 || demux->sidx_interval == 0)
    return FALSE;

  idx = (guint) (seek_time / demux->sidx_interval);

  /* FIXME: seek beyond end of file should result in immediate EOS from
   * streaming thread instead of a failed seek */
  if (idx >= demux->sidx_num_entries)
    return FALSE;

  *packet = demux->sidx_entries[idx];

  /* so we get closer to the actual time of the packet ... actually, let's not
   * do this, since we throw away superfluous payloads before the seek position
   * anyway; this way, our key unit seek 'snap resolution' is a bit better
   * (ie. same as index resolution) */
  /*
     while (idx > 0 && demux->sidx_entries[idx-1] == demux->sidx_entries[idx])
     --idx;
   */

  idx_time = demux->sidx_interval * idx;

  GST_DEBUG_OBJECT (demux, "%" GST_TIME_FORMAT " => packet %u at %"
      GST_TIME_FORMAT, GST_TIME_ARGS (seek_time), *packet,
      GST_TIME_ARGS (idx_time));

  if (p_idx_time)
    *p_idx_time = idx_time;

  return TRUE;
}

static void
gst_asf_demux_reset_stream_state_after_discont (GstASFDemux * demux)
{
  guint n;

  demux->pts = 0;               //why?
  gst_adapter_clear (demux->adapter);

  for (n = 0; n < demux->num_streams; n++) {
    gst_buffer_replace (&demux->stream[n].payload, NULL);
    gst_buffer_replace (&demux->stream[n].cache, NULL);
    demux->stream[n].frag_offset = 0;
    demux->stream[n].last_pts = GST_CLOCK_TIME_NONE;
    demux->stream[n].last_buffer_timestamp = GST_CLOCK_TIME_NONE;
    demux->stream[n].sequence = 0;
    demux->stream[n].discont = TRUE;
    demux->stream[n].last_flow = GST_FLOW_OK;

    while (demux->stream[n].payloads->len > 0) {
      AsfPayload *payload;
      guint last;

      last = demux->stream[n].payloads->len - 1;
      payload = &g_array_index (demux->stream[n].payloads, AsfPayload, last);
      gst_buffer_replace (&payload->buf, NULL);
      g_array_remove_index (demux->stream[n].payloads, last);
    }
  }
}

static gboolean
gst_asf_demux_handle_seek_event (GstASFDemux * demux, GstEvent * event)
{
  GstClockTime idx_time;
  GstSegment segment;
  GstSeekFlags flags;
  GstSeekType cur_type, stop_type;
  GstFormat format;
  gboolean only_need_update;
  gboolean keyunit_sync;
  gboolean accurate;
  gboolean flush;
  gdouble rate;
  gint64 cur, stop;
  gint64 seek_time;
  guint packet;

  if (demux->seekable == FALSE || demux->packet_size == 0 ||
      demux->num_packets == 0 || demux->play_time == 0) {
    GST_LOG_OBJECT (demux, "stream is not seekable");
    return FALSE;
  }

  gst_event_parse_seek (event, &rate, &format, &flags, &cur_type, &cur,
      &stop_type, &stop);

  if (format != GST_FORMAT_TIME) {
    GST_LOG_OBJECT (demux, "seeking is only supported in TIME format");
    return FALSE;
  }

  if (rate <= 0.0) {
    GST_LOG_OBJECT (demux, "backward playback is not supported yet");
    return FALSE;
  }

  flush = ((flags & GST_SEEK_FLAG_FLUSH) == GST_SEEK_FLAG_FLUSH);
  accurate = ((flags & GST_SEEK_FLAG_ACCURATE) == GST_SEEK_FLAG_ACCURATE);
  keyunit_sync = ((flags & GST_SEEK_FLAG_KEY_UNIT) == GST_SEEK_FLAG_KEY_UNIT);

  /* unlock the streaming thread */
  if (flush) {
    gst_pad_push_event (demux->sinkpad, gst_event_new_flush_start ());
    gst_asf_demux_send_event_unlocked (demux, gst_event_new_flush_start ());
  } else {
    gst_pad_pause_task (demux->sinkpad);
  }

  /* grab the stream lock so that streaming cannot continue, for
   * non flushing seeks when the element is in PAUSED this could block
   * forever */
  GST_PAD_STREAM_LOCK (demux->sinkpad);

  /* we now can stop flushing, since we have the stream lock now */
  gst_pad_push_event (demux->sinkpad, gst_event_new_flush_stop ());

  if (flush)
    gst_asf_demux_send_event_unlocked (demux, gst_event_new_flush_stop ());

  /* operating on copy of segment until we know the seek worked */
  segment = demux->segment;

  if (demux->segment_running && !flush) {
    GstEvent *newseg;

    /* create the segment event to close the current segment */
    newseg = gst_event_new_new_segment (TRUE, segment.rate,
        GST_FORMAT_TIME, segment.start, segment.last_stop, segment.time);

    gst_asf_demux_send_event_unlocked (demux, newseg);
  }

  gst_segment_set_seek (&segment, rate, format, flags, cur_type,
      cur, stop_type, stop, &only_need_update);

  GST_DEBUG_OBJECT (demux, "seeking to time %" GST_TIME_FORMAT ", segment: "
      "%" GST_SEGMENT_FORMAT, GST_TIME_ARGS (segment.start), &segment);

  seek_time = segment.start;

  /* FIXME: should check the KEY_UNIT flag; need to adjust last_stop to
   * real start of data and segment_start to indexed time for key unit seek*/
  if (!gst_asf_demux_seek_index_lookup (demux, &packet, seek_time, &idx_time)) {
    /* First try to query our source to see if it can convert for us. This is
       the case when our source is an mms stream, notice that in this case
       gstmms will do a time based seek to get the byte offset, this is not a
       problem as the seek to this offset needs to happen anway. */
    gint64 offset;
    GstFormat dest_format = GST_FORMAT_BYTES;

    if (gst_pad_query_peer_convert (demux->sinkpad, GST_FORMAT_TIME, seek_time,
            &dest_format, &offset) && dest_format == GST_FORMAT_BYTES) {
      packet = (offset - demux->data_offset) / demux->packet_size;
      GST_LOG_OBJECT (demux, "convert %" GST_TIME_FORMAT
          " to bytes query result: %lld, data_ofset: %llu, packet_size: %u,"
          " resulting packet: %u\n", GST_TIME_ARGS (seek_time), offset,
          demux->data_offset, demux->packet_size, packet);
    } else {
      /* Hackety hack, this sucks. We just seek to an earlier position
       *  and let the sinks throw away the stuff before the segment start */
      if (flush && (accurate || keyunit_sync)) {
        seek_time -= 5 * GST_SECOND;
        if (seek_time < 0)
          seek_time = 0;
      }

      packet = (guint) gst_util_uint64_scale (demux->num_packets,
          seek_time, demux->play_time);

      if (packet > demux->num_packets)
        packet = demux->num_packets;
    }
  } else {
    if (keyunit_sync) {
      GST_DEBUG_OBJECT (demux, "key unit seek, adjust seek_time = %"
          GST_TIME_FORMAT " to index_time = %" GST_TIME_FORMAT,
          GST_TIME_ARGS (seek_time), GST_TIME_ARGS (idx_time));
      segment.start = idx_time;
      segment.last_stop = idx_time;
      segment.time = idx_time;
    }
  }

  GST_DEBUG_OBJECT (demux, "seeking to packet %u", packet);

  GST_OBJECT_LOCK (demux);
  demux->segment = segment;
  demux->packet = packet;
  demux->need_newsegment = TRUE;
  gst_asf_demux_reset_stream_state_after_discont (demux);
  GST_OBJECT_UNLOCK (demux);

  /* restart our task since it might have been stopped when we did the flush */
  gst_pad_start_task (demux->sinkpad, (GstTaskFunction) gst_asf_demux_loop,
      demux);

  /* streaming can continue now */
  GST_PAD_STREAM_UNLOCK (demux->sinkpad);

  return TRUE;
}

static gboolean
gst_asf_demux_handle_src_event (GstPad * pad, GstEvent * event)
{
  GstASFDemux *demux;
  gboolean ret;

  demux = GST_ASF_DEMUX (gst_pad_get_parent (pad));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_SEEK:
      GST_LOG_OBJECT (pad, "seek event");
      ret = gst_asf_demux_handle_seek_event (demux, event);
      gst_event_unref (event);
      break;
    case GST_EVENT_QOS:
    case GST_EVENT_NAVIGATION:
      /* just drop these two silently */
      gst_event_unref (event);
      ret = FALSE;
      break;
    default:
      GST_LOG_OBJECT (pad, "%s event", GST_EVENT_TYPE_NAME (event));
      ret = gst_pad_event_default (pad, event);
      break;
  }

  gst_object_unref (demux);
  return ret;
}

static inline guint32
gst_asf_demux_identify_guid (const ASFGuidHash * guids, ASFGuid * guid)
{
  guint32 ret;

  ret = gst_asf_identify_guid (guids, guid);

  GST_LOG ("%s  0x%08x-0x%08x-0x%08x-0x%08x",
      gst_asf_get_guid_nick (guids, ret),
      guid->v1, guid->v2, guid->v3, guid->v4);

  return ret;
}

typedef struct
{
  AsfObjectID id;
  guint64 size;
} AsfObject;

static gboolean
asf_demux_peek_object (GstASFDemux * demux, const guint8 * data,
    guint data_len, AsfObject * object)
{
  ASFGuid guid;

  if (data_len < ASF_OBJECT_HEADER_SIZE)
    return FALSE;

  guid.v1 = GST_READ_UINT32_LE (data + 0);
  guid.v2 = GST_READ_UINT32_LE (data + 4);
  guid.v3 = GST_READ_UINT32_LE (data + 8);
  guid.v4 = GST_READ_UINT32_LE (data + 12);

  object->size = GST_READ_UINT64_LE (data + 16);

  /* FIXME: make asf_demux_identify_object_guid() */
  object->id = gst_asf_demux_identify_guid (asf_object_guids, &guid);
  if (object->id == ASF_OBJ_UNDEFINED) {
    GST_WARNING_OBJECT (demux, "Unknown object %08x-%08x-%08x-%08x",
        guid.v1, guid.v2, guid.v3, guid.v4);
  }

  return TRUE;
}

static GstFlowReturn
gst_asf_demux_chain_headers (GstASFDemux * demux)
{
  GstFlowReturn flow;
  AsfObject obj;
  guint8 *header_data, *data = NULL;
  guint64 header_size;

  data = (guint8 *) gst_adapter_peek (demux->adapter, ASF_OBJECT_HEADER_SIZE);
  if (data == NULL)
    goto need_more_data;

  asf_demux_peek_object (demux, data, ASF_OBJECT_HEADER_SIZE, &obj);
  if (obj.id != ASF_OBJ_HEADER)
    goto wrong_type;

  GST_LOG_OBJECT (demux, "header size = %u", (guint) obj.size);

  /* + 50 for non-packet data at beginning of ASF_OBJ_DATA */
  if (gst_adapter_available (demux->adapter) < obj.size + 50)
    goto need_more_data;

  data = gst_adapter_take (demux->adapter, obj.size + 50);

  header_data = data;
  header_size = obj.size;
  flow = gst_asf_demux_process_object (demux, &header_data, &header_size);
  if (flow != GST_FLOW_OK)
    goto parse_failed;

  /* calculate where the packet data starts */
  demux->data_offset = obj.size + 50;

  /* now parse the beginning of the ASF_OBJ_DATA object */
  if (!gst_asf_demux_parse_data_object_start (demux, data + obj.size))
    goto wrong_type;

  if (demux->num_streams == 0)
    goto no_streams;

  /* FIXME: remove when we activate streams after internal preroll in
   * streaming mode as well */
  GST_LOG_OBJECT (demux, "signalling no more pads");
  gst_element_no_more_pads (GST_ELEMENT_CAST (demux));

  g_free (data);
  return GST_FLOW_OK;

/* NON-FATAL */
need_more_data:
  {
    GST_LOG_OBJECT (demux, "not enough data in adapter yet");
    return GST_FLOW_OK;
  }

/* ERRORS */
wrong_type:
  {
    GST_ELEMENT_ERROR (demux, STREAM, WRONG_TYPE, (NULL),
        ("This doesn't seem to be an ASF file"));
    g_free (data);
    return GST_FLOW_ERROR;
  }
no_streams:
parse_failed:
  {
    GST_ELEMENT_ERROR (demux, STREAM, DEMUX, (NULL),
        ("header parsing failed, or no streams found, flow = %s",
            gst_flow_get_name (flow)));
    g_free (data);
    return GST_FLOW_ERROR;
  }
}

static GstFlowReturn
gst_asf_demux_aggregate_flow_return (GstASFDemux * demux)
{
  int i;
  GST_DEBUG_OBJECT (demux, "Aggregating");

  for (i = 0; i < demux->num_streams; i++) {
    if (demux->stream[i].active) {
      GstFlowReturn flowret = demux->stream[i].last_flow;
      GST_DEBUG_OBJECT (demux, "Aggregating: flow %i return %s", i,
          gst_flow_get_name (flowret));
      if (flowret != GST_FLOW_NOT_LINKED)
        return flowret;
    }
  }

  /* If we got here, then all our active streams are not linked */
  return GST_FLOW_NOT_LINKED;
}

static GstFlowReturn
gst_asf_demux_chain (GstPad * pad, GstBuffer * buf)
{
  GstFlowReturn ret = GST_FLOW_OK;
  GstASFDemux *demux;

  demux = GST_ASF_DEMUX (GST_PAD_PARENT (pad));

  GST_LOG_OBJECT (demux, "buffer: size=%u, offset=%" G_GINT64_FORMAT,
      GST_BUFFER_SIZE (buf), GST_BUFFER_OFFSET (buf));

  if (GST_BUFFER_IS_DISCONT (buf))
    gst_asf_demux_reset_stream_state_after_discont (demux);

  gst_adapter_push (demux->adapter, buf);

  switch (demux->state) {
    case GST_ASF_DEMUX_STATE_HEADER:{
      ret = gst_asf_demux_chain_headers (demux);
      if (demux->state != GST_ASF_DEMUX_STATE_DATA)
        break;
      /* otherwise fall through */
    }
    case GST_ASF_DEMUX_STATE_DATA:{

      if (demux->need_newsegment) {
        GST_DEBUG_OBJECT (demux, "sending new-segment event from %"
            GST_SEGMENT_FORMAT, &demux->segment);

        /* FIXME: check last parameter, streams may have non-zero start */
        gst_asf_demux_send_event_unlocked (demux,
            gst_event_new_new_segment (FALSE, demux->segment.rate,
                GST_FORMAT_TIME, demux->segment.start, demux->segment.stop,
                demux->segment.start));

        demux->need_newsegment = FALSE;
        demux->segment_running = TRUE;
      }

      while (gst_adapter_available (demux->adapter) >= demux->packet_size) {
        guint64 data_size;
        guint8 *data;

        data_size = demux->packet_size;
        data = (guint8 *) gst_adapter_peek (demux->adapter, data_size);
        g_assert (data != NULL);

        ret = gst_asf_demux_handle_data (demux, &data, &data_size);

        /* FIXME: check if this can happen, shouldn't really, should it? */
        if (ret == ASF_FLOW_NEED_MORE_DATA) {
          GST_DEBUG_OBJECT (demux, "not flushing, flow was need-more-data");

          /* FIXME: we always know the packet size, this code is weird */
          /* if we know the packet size and still do a
           * short read, then something is fishy */
/*
          GST_ELEMENT_ERROR (demux, STREAM, DEMUX,
              ("Error parsing packet"),
              ("Unexpected short read in packet at offset %" G_GINT64_FORMAT,
                  gst_asf_demux_get_current_offset (demux, NULL)));
          
          ret = GST_FLOW_ERROR;
*/
          gst_adapter_flush (demux->adapter, demux->packet_size);
          ret = GST_FLOW_OK;
          break;                /* bail out */
        } else {
          GST_DEBUG ("flushing packet (%u bytes)", demux->packet_size);
          gst_adapter_flush (demux->adapter, demux->packet_size);
        }
      }
      break;
    }
    default:
      g_assert_not_reached ();
  }

  /* FIXME: eradicate ASF_FLOW_NEED_MORE_DATA */

  if (ret != GST_FLOW_OK)
    GST_DEBUG_OBJECT (demux, "flow: %s", gst_flow_get_name (ret));

  return ret;
}

static gboolean
gst_asf_demux_pull_data (GstASFDemux * demux, guint64 offset, guint size,
    GstBuffer ** p_buf, GstFlowReturn * p_flow)
{
  GstFlowReturn flow;

  GST_LOG_OBJECT (demux, "pulling buffer at %" G_GUINT64_FORMAT "+%u",
      offset, size);

  flow = gst_pad_pull_range (demux->sinkpad, offset, size, p_buf);

  if (p_flow)
    *p_flow = flow;

  if (flow != GST_FLOW_OK) {
    GST_DEBUG_OBJECT (demux, "flow %s pulling buffer at %" G_GUINT64_FORMAT
        "+%u", gst_flow_get_name (flow), offset, size);
    *p_buf = NULL;
    return FALSE;
  }

  g_assert (*p_buf != NULL);

  if (GST_BUFFER_SIZE (*p_buf) < size) {
    GST_DEBUG_OBJECT (demux, "short read pulling buffer at %" G_GUINT64_FORMAT
        "+%u (got only %u bytes)", offset, size, GST_BUFFER_SIZE (*p_buf));
    gst_buffer_unref (*p_buf);
    if (p_flow)
      *p_flow = GST_FLOW_UNEXPECTED;
    *p_buf = NULL;
    return FALSE;
  }

  return TRUE;
}

static void
gst_asf_demux_pull_indices (GstASFDemux * demux)
{
  GstBuffer *buf = NULL;
  guint64 offset;
  guint num_read = 0;

  offset = demux->index_offset;

  if (offset == 0) {
    GST_DEBUG_OBJECT (demux, "can't read indices, don't know index offset");
    return;
  }

  while (gst_asf_demux_pull_data (demux, offset, 16 + 8, &buf, NULL)) {
    GstFlowReturn flow;
    AsfObject obj;

    asf_demux_peek_object (demux, GST_BUFFER_DATA (buf), 16 + 8, &obj);
    gst_buffer_replace (&buf, NULL);

    /* check for sanity */
    if (obj.size > (5 * 1024 * 1024)) {
      GST_DEBUG_OBJECT (demux, "implausible index object size, bailing out");
      break;
    }

    if (!gst_asf_demux_pull_data (demux, offset, obj.size, &buf, NULL))
      break;

    GST_LOG_OBJECT (demux, "index object at offset 0x%" G_GINT64_MODIFIER "X"
        ", size %u", offset, (guint) obj.size);

    offset += obj.size;         /* increase before _process_object changes it */

    flow = gst_asf_demux_process_object (demux, &buf->data, &obj.size);
    gst_buffer_replace (&buf, NULL);

    if (flow != GST_FLOW_OK)
      break;

    ++num_read;
  }
  GST_DEBUG_OBJECT (demux, "read %u index objects", num_read);
}

static gboolean
gst_asf_demux_parse_data_object_start (GstASFDemux * demux, guint8 * data)
{
  AsfObject obj;

  asf_demux_peek_object (demux, data, 50, &obj);
  if (obj.id != ASF_OBJ_DATA) {
    GST_WARNING_OBJECT (demux, "headers not followed by a DATA object");
    return FALSE;
  }

  demux->state = GST_ASF_DEMUX_STATE_DATA;

  if (!demux->broadcast && obj.size > 50) {
    demux->data_size = obj.size - 50;
    /* CHECKME: for at least one file this is off by +158 bytes?! */
    demux->index_offset = demux->data_offset + demux->data_size;
  } else {
    demux->data_size = 0;
    demux->index_offset = 0;
  }

  demux->packet = 0;

  if (!demux->broadcast) {
    /* skip object header (24 bytes) and file GUID (16 bytes) */
    demux->num_packets = GST_READ_UINT64_LE (data + (16 + 8) + 16);
  } else {
    demux->num_packets = 0;
  }

  if (demux->num_packets == 0)
    demux->seekable = FALSE;

  /* fallback in the unlikely case that headers are inconsistent, can't hurt */
  if (demux->data_size == 0 && demux->num_packets > 0) {
    demux->data_size = demux->num_packets * demux->packet_size;
    demux->index_offset = demux->data_offset + demux->data_size;
  }

  /* process pending stream objects and create pads for those */
  gst_asf_demux_process_queued_extended_stream_objects (demux);
  gst_asf_demux_activate_ext_props_streams (demux);

  GST_INFO_OBJECT (demux, "Stream has %" G_GUINT64_FORMAT " packets, "
      "data_offset=%" G_GINT64_FORMAT ", data_size=%" G_GINT64_FORMAT
      ", index_offset=%" G_GUINT64_FORMAT, demux->num_packets,
      demux->data_offset, demux->data_size, demux->index_offset);

  return TRUE;
}

static gboolean
gst_asf_demux_pull_headers (GstASFDemux * demux)
{
  GstFlowReturn flow;
  AsfObject obj;
  GstBuffer *buf = NULL;
  guint64 size;

  GST_LOG_OBJECT (demux, "reading headers");

  /* pull HEADER object header, so we know its size */
  if (!gst_asf_demux_pull_data (demux, 0, 16 + 8, &buf, NULL))
    goto read_failed;

  asf_demux_peek_object (demux, GST_BUFFER_DATA (buf), 16 + 8, &obj);
  gst_buffer_replace (&buf, NULL);

  if (obj.id != ASF_OBJ_HEADER)
    goto wrong_type;

  GST_LOG_OBJECT (demux, "header size = %u", (guint) obj.size);

  /* pull HEADER object */
  if (!gst_asf_demux_pull_data (demux, 0, obj.size, &buf, NULL))
    goto read_failed;

  size = obj.size;              /* don't want obj.size changed */
  flow = gst_asf_demux_process_object (demux, &buf->data, &size);
  gst_buffer_replace (&buf, NULL);

  if (flow != GST_FLOW_OK) {
    GST_WARNING_OBJECT (demux, "process_object: %s", gst_flow_get_name (flow));
    goto parse_failed;
  }

  /* calculate where the packet data starts */
  demux->data_offset = obj.size + 50;

  /* now pull beginning of DATA object before packet data */
  if (!gst_asf_demux_pull_data (demux, obj.size, 50, &buf, NULL))
    goto read_failed;

  if (!gst_asf_demux_parse_data_object_start (demux, GST_BUFFER_DATA (buf)))
    goto wrong_type;

  if (demux->num_streams == 0)
    goto no_streams;

  gst_buffer_replace (&buf, NULL);
  return TRUE;

/* ERRORS */
wrong_type:
  {
    gst_buffer_replace (&buf, NULL);
    GST_ELEMENT_ERROR (demux, STREAM, WRONG_TYPE, (NULL),
        ("This doesn't seem to be an ASF file"));
    return FALSE;
  }
no_streams:
read_failed:
parse_failed:
  {
    GST_ELEMENT_ERROR (demux, STREAM, DEMUX, (NULL), (NULL));
    return FALSE;
  }
}

static gboolean
all_streams_prerolled (GstASFDemux * demux)
{
  GstClockTime preroll_time;
  guint i, num_no_data = 0;

  preroll_time = demux->preroll * GST_MSECOND;

  /* returns TRUE as long as there isn't a stream which (a) has data queued
   * and (b) the timestamp of last piece of data queued is < demux->preroll
   * AND there is at least one other stream with data queued */
  for (i = 0; i < demux->num_streams; ++i) {
    AsfPayload *last_payload;
    AsfStream *stream;
    guint last_idx;

    stream = &demux->stream[i];
    if (stream->payloads->len == 0) {
      ++num_no_data;
      GST_LOG_OBJECT (stream->pad, "no data queued");
      continue;
    }

    last_idx = stream->payloads->len - 1;
    last_payload = &g_array_index (stream->payloads, AsfPayload, last_idx);

    GST_LOG_OBJECT (stream->pad, "checking if %" GST_TIME_FORMAT " > %"
        GST_TIME_FORMAT, GST_TIME_ARGS (last_payload->ts),
        GST_TIME_ARGS (preroll_time));
    if (last_payload->ts <= preroll_time) {
      GST_LOG_OBJECT (stream->pad, "not beyond preroll point yet");
      return FALSE;
    }
  }

  if (num_no_data == demux->num_streams)
    return FALSE;

  return TRUE;
}

#if 0
static gboolean
gst_asf_demux_have_mutually_exclusive_active_stream (GstASFDemux * demux,
    AsfStream * stream)
{
  GSList *l;

  for (l = demux->mut_ex_streams; l != NULL; l = l->next) {
    guint8 *mes;

    /* check for each mutual exclusion group whether it affects this stream */
    for (mes = (guint8 *) l->data; mes != NULL && *mes != 0xff; ++mes) {
      if (*mes == stream->id) {
        /* we are in this group; let's check if we've already activated streams
         * that are in the same group (and hence mutually exclusive to this
         * one) */
        for (mes = (guint8 *) l->data; mes != NULL && *mes != 0xff; ++mes) {
          guint i;

          for (i = 0; i < demux->num_streams; ++i) {
            if (demux->stream[i].id == *mes && demux->stream[i].active) {
              GST_LOG_OBJECT (demux, "stream with ID %d is mutually exclusive "
                  "to already active stream with ID %d", stream->id,
                  demux->stream[i].id);
              return TRUE;
            }
          }
        }
        /* we can only be in this group once, let's break out and move on to
         * the next mutual exclusion group */
        break;
      }
    }
  }

  return FALSE;
}
#endif

static gboolean
gst_asf_demux_check_activate_streams (GstASFDemux * demux, gboolean force)
{
  guint i;

  if (demux->activated_streams)
    return TRUE;

  if (!all_streams_prerolled (demux) && !force) {
    GST_DEBUG_OBJECT (demux, "not all streams with data beyond preroll yet");
    return FALSE;
  }

  for (i = 0; i < demux->num_streams; ++i) {
    AsfStream *stream = &demux->stream[i];

    if (stream->payloads->len > 0) {
      /* we don't check mutual exclusion stuff here; either we have data for
       * a stream, then we active it, or we don't, then we'll ignore it */
      GST_LOG_OBJECT (stream->pad, "is prerolled - activate!");
      gst_asf_demux_activate_stream (demux, stream);
    } else {
      GST_LOG_OBJECT (stream->pad, "no data, ignoring stream");
    }
  }

  demux->activated_streams = TRUE;
  GST_LOG_OBJECT (demux, "signalling no more pads");
  gst_element_no_more_pads (GST_ELEMENT (demux));
  return TRUE;
}

/* returns the stream that has a complete payload with the lowest timestamp
 * queued, or NULL (we push things by timestamp because during the internal
 * prerolling we might accumulate more data then the external queues can take,
 * so we'd lock up if we pushed all accumulated data for stream N in one go) */
static AsfStream *
gst_asf_demux_find_stream_with_complete_payload (GstASFDemux * demux)
{
  AsfPayload *best_payload = NULL;
  AsfStream *best_stream = NULL;
  guint i;

  for (i = 0; i < demux->num_streams; ++i) {
    AsfStream *stream;

    stream = &demux->stream[i];

    /* Don't push any data until we have at least one payload that falls within
     * the current segment. This way we can remove out-of-segment payloads that
     * don't need to be decoded after a seek, sending only data from the
     * keyframe directly before our segment start */
    if (stream->payloads->len > 0) {
      AsfPayload *payload;
      guint last_idx;

      last_idx = stream->payloads->len - 1;
      payload = &g_array_index (stream->payloads, AsfPayload, last_idx);
      if (GST_CLOCK_TIME_IS_VALID (payload->ts) &&
          payload->ts < demux->segment.start) {
        GST_DEBUG_OBJECT (stream->pad, "Last queued payload has timestamp %"
            GST_TIME_FORMAT " which is before our segment start %"
            GST_TIME_FORMAT ", not pushing yet", GST_TIME_ARGS (payload->ts),
            GST_TIME_ARGS (demux->segment.start));
        continue;
      }
    }

    /* Now see if there's a complete payload queued for this stream */
    if (stream->payloads->len > 0) {
      AsfPayload *payload;

      payload = &g_array_index (stream->payloads, AsfPayload, 0);
      if (!gst_asf_payload_is_complete (payload))
        continue;

      /* ... and whether its timestamp is lower than the current best */
      if (best_stream == NULL || best_payload->ts > payload->ts) {
        best_stream = stream;
        best_payload = payload;
      }
    }
  }

  return best_stream;
}

static GstFlowReturn
gst_asf_demux_push_complete_payloads (GstASFDemux * demux, gboolean force)
{
  AsfStream *stream;

  if (G_UNLIKELY (!demux->activated_streams)) {
    if (!gst_asf_demux_check_activate_streams (demux, force))
      return GST_FLOW_OK;
    /* streams are now activated */
  }

  /* do we need to send a newsegment event */
  if (demux->need_newsegment) {
    if (demux->segment.stop == GST_CLOCK_TIME_NONE &&
        demux->segment.duration > 0) {
      demux->segment.stop = demux->segment.duration;
    }

    GST_DEBUG_OBJECT (demux, "sending new-segment event %" GST_SEGMENT_FORMAT,
        &demux->segment);

    /* note: we fix up all timestamps to start from 0, so this should be ok */
    gst_asf_demux_send_event_unlocked (demux,
        gst_event_new_new_segment (FALSE, demux->segment.rate,
            GST_FORMAT_TIME, demux->segment.start, demux->segment.stop,
            demux->segment.start));

    demux->need_newsegment = FALSE;
    demux->segment_running = TRUE;
  }

  while ((stream = gst_asf_demux_find_stream_with_complete_payload (demux))) {
    AsfPayload *payload;

    payload = &g_array_index (stream->payloads, AsfPayload, 0);

    /* Do we have tags pending for this stream? */
    if (stream->pending_tags) {
      GST_LOG_OBJECT (stream->pad, "%" GST_PTR_FORMAT, stream->pending_tags);
      gst_element_found_tags_for_pad (GST_ELEMENT (demux), stream->pad,
          stream->pending_tags);
      stream->pending_tags = NULL;
    }

    /* We have the whole packet now so we should push the packet to
     * the src pad now. First though we should check if we need to do
     * descrambling */
    if (demux->span > 1) {
      gst_asf_demux_descramble_buffer (demux, stream, &payload->buf);
    }

    payload->buf = gst_buffer_make_metadata_writable (payload->buf);

    if (!payload->keyframe) {
      GST_BUFFER_FLAG_SET (payload->buf, GST_BUFFER_FLAG_DELTA_UNIT);
    }

    if (stream->discont) {
      GST_BUFFER_FLAG_SET (payload->buf, GST_BUFFER_FLAG_DISCONT);
      stream->discont = FALSE;
    }

    gst_buffer_set_caps (payload->buf, stream->caps);

    GST_BUFFER_TIMESTAMP (payload->buf) = payload->ts;
    GST_BUFFER_DURATION (payload->buf) = payload->duration;

    /* FIXME: we should really set durations on buffers if we can */

    GST_LOG_OBJECT (stream->pad, "pushing buffer, ts=%" GST_TIME_FORMAT
        ", dur=%" GST_TIME_FORMAT " size=%u",
        GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (payload->buf)),
        GST_TIME_ARGS (GST_BUFFER_DURATION (payload->buf)),
        GST_BUFFER_SIZE (payload->buf));

    stream->last_flow = gst_pad_push (stream->pad, payload->buf);
    payload->buf = NULL;
    g_array_remove_index (stream->payloads, 0);
  }

  return gst_asf_demux_aggregate_flow_return (demux);
}

static void
gst_asf_demux_loop (GstASFDemux * demux)
{
  GstFlowReturn flow;
  GstBuffer *buf = NULL;
  guint64 off;

  if (demux->state == GST_ASF_DEMUX_STATE_HEADER) {
    if (!gst_asf_demux_pull_headers (demux)) {
      flow = GST_FLOW_ERROR;
      goto pause;
    }

    gst_asf_demux_pull_indices (demux);
  }

  g_assert (demux->state == GST_ASF_DEMUX_STATE_DATA);

  if (demux->num_packets != 0 && demux->packet >= demux->num_packets)
    goto eos;

  GST_LOG_OBJECT (demux, "packet %u/%u", (guint) demux->packet + 1,
      (guint) demux->num_packets);

  off = demux->data_offset + (demux->packet * demux->packet_size);

  if (!gst_asf_demux_pull_data (demux, off, demux->packet_size, &buf, &flow)) {
    GST_DEBUG_OBJECT (demux, "got flow %s", gst_flow_get_name (flow));
    if (flow == GST_FLOW_UNEXPECTED)
      goto eos;
    else if (!GST_FLOW_IS_FATAL (flow)) {
      GST_DEBUG_OBJECT (demux, "Not fatal");
      goto pause;
    } else
      goto read_failed;
  }

  /* FIXME: maybe we should just skip broken packets and error out only
   * after a few broken packets in a row? */
  if (!gst_asf_demux_parse_packet (demux, buf))
    goto parse_error;

  gst_buffer_unref (buf);

  flow = gst_asf_demux_push_complete_payloads (demux, FALSE);

  ++demux->packet;

  if (demux->num_packets > 0 && demux->packet >= demux->num_packets) {
    GST_LOG_OBJECT (demux, "reached EOS");
    goto eos;
  }

  if (flow != GST_FLOW_OK) {
    GST_DEBUG_OBJECT (demux, "pushing complete payloads failed");
    goto pause;
  }

  /* check if we're at the end of the configured segment */
  /* FIXME: check if segment end reached etc. */

  return;

eos:
  {
    /* if we haven't activated our streams yet, this might be because we have
     * less data queued than required for preroll; force stream activation and
     * send any pending payloads before sending EOS */
    if (!demux->activated_streams)
      flow = gst_asf_demux_push_complete_payloads (demux, TRUE);

    if (flow != GST_FLOW_OK && flow != GST_FLOW_UNEXPECTED) {
      GST_DEBUG_OBJECT (demux, "pushing complete payloads failed");
      goto pause;
    }

    if (demux->segment.flags & GST_SEEK_FLAG_SEGMENT) {
      gint64 stop;

      /* for segment playback we need to post when (in stream time)
       * we stopped, this is either stop (when set) or the duration. */
      if ((stop = demux->segment.stop) == -1)
        stop = demux->segment.duration;

      GST_INFO_OBJECT (demux, "Posting segment-done, at end of segment");
      gst_element_post_message (GST_ELEMENT_CAST (demux),
          gst_message_new_segment_done (GST_OBJECT (demux), GST_FORMAT_TIME,
              stop));
    } else {
      /* normal playback, send EOS to all linked pads */
      GST_INFO_OBJECT (demux, "Sending EOS, at end of stream");
      gst_asf_demux_send_event_unlocked (demux, gst_event_new_eos ());
    }
    /* ... and fall through to pause */
    GST_DEBUG_OBJECT (demux, "EOSing");
  }
pause:
  {
    GST_DEBUG_OBJECT (demux, "pausing task");
    demux->segment_running = FALSE;
    gst_pad_pause_task (demux->sinkpad);

    /* For the error cases (not EOS) */
    if (flow != GST_FLOW_OK && flow != GST_FLOW_UNEXPECTED) {
      /* Post an error. Hopefully something else already has, but if not... */
      GST_ELEMENT_ERROR (demux, STREAM, FAILED,
          (_("Internal data stream error.")),
          ("streaming stopped, reason %s", gst_flow_get_name (flow)));
    }
    return;
  }

/* ERRORS */
read_failed:
  {
    GST_DEBUG_OBJECT (demux, "Read failed, doh");
    gst_asf_demux_send_event_unlocked (demux, gst_event_new_eos ());
    flow = GST_FLOW_UNEXPECTED;
    goto pause;
  }
parse_error:
  {
    gst_buffer_unref (buf);
    GST_ELEMENT_ERROR (demux, STREAM, DEMUX, (NULL),
        ("Error parsing ASF packet %u", (guint) demux->packet));
    gst_asf_demux_send_event_unlocked (demux, gst_event_new_eos ());
    flow = GST_FLOW_ERROR;
    goto pause;
  }
}

static inline gboolean
gst_asf_demux_skip_bytes (guint num_bytes, guint8 ** p_data, guint64 * p_size)
{
  if (*p_size < num_bytes)
    return FALSE;

  *p_data += num_bytes;
  *p_size -= num_bytes;
  return TRUE;
}

static inline guint8
gst_asf_demux_get_uint8 (guint8 ** p_data, guint64 * p_size)
{
  guint8 ret;

  g_assert (*p_size >= 1);
  ret = GST_READ_UINT8 (*p_data);
  *p_data += sizeof (guint8);
  *p_size -= sizeof (guint8);
  return ret;
}

static inline guint16
gst_asf_demux_get_uint16 (guint8 ** p_data, guint64 * p_size)
{
  guint16 ret;

  g_assert (*p_size >= 2);
  ret = GST_READ_UINT16_LE (*p_data);
  *p_data += sizeof (guint16);
  *p_size -= sizeof (guint16);
  return ret;
}

static inline guint32
gst_asf_demux_get_uint32 (guint8 ** p_data, guint64 * p_size)
{
  guint32 ret;

  g_assert (*p_size >= 4);
  ret = GST_READ_UINT32_LE (*p_data);
  *p_data += sizeof (guint32);
  *p_size -= sizeof (guint32);
  return ret;
}

static inline guint64
gst_asf_demux_get_uint64 (guint8 ** p_data, guint64 * p_size)
{
  guint64 ret;

  g_assert (*p_size >= 8);
  ret = GST_READ_UINT64_LE (*p_data);
  *p_data += sizeof (guint64);
  *p_size -= sizeof (guint64);
  return ret;
}

static inline guint32
gst_asf_demux_get_var_length (guint8 type, guint8 ** p_data, guint64 * p_size)
{
  switch (type) {
    case 0:
      return 0;

    case 1:
      g_assert (*p_size >= 1);
      return gst_asf_demux_get_uint8 (p_data, p_size);

    case 2:
      g_assert (*p_size >= 2);
      return gst_asf_demux_get_uint16 (p_data, p_size);

    case 3:
      g_assert (*p_size >= 4);
      return gst_asf_demux_get_uint32 (p_data, p_size);

    default:
      break;
  }

  g_assert_not_reached ();
}

static gboolean
gst_asf_demux_get_buffer (GstBuffer ** p_buf, guint num_bytes_to_read,
    guint8 ** p_data, guint64 * p_size)
{
  *p_buf = NULL;

  if (*p_size < num_bytes_to_read)
    return FALSE;

  *p_buf = gst_buffer_new_and_alloc (num_bytes_to_read);
  memcpy (GST_BUFFER_DATA (*p_buf), *p_data, num_bytes_to_read);
  *p_data += num_bytes_to_read;
  *p_size -= num_bytes_to_read;
  return TRUE;
}

static gboolean
gst_asf_demux_get_bytes (guint8 ** p_buf, guint num_bytes_to_read,
    guint8 ** p_data, guint64 * p_size)
{
  *p_buf = NULL;

  if (*p_size < num_bytes_to_read)
    return FALSE;

  *p_buf = g_memdup (*p_data, num_bytes_to_read);
  *p_data += num_bytes_to_read;
  *p_size -= num_bytes_to_read;
  return TRUE;
}

static gboolean
gst_asf_demux_get_string (gchar ** p_str, guint16 * p_strlen,
    guint8 ** p_data, guint64 * p_size)
{
  guint16 s_length;
  guint8 *s;

  *p_str = NULL;

  if (*p_size < 2)
    return FALSE;

  s_length = gst_asf_demux_get_uint16 (p_data, p_size);

  if (p_strlen)
    *p_strlen = s_length;

  if (s_length == 0) {
    GST_WARNING ("zero-length string");
    *p_str = g_strdup ("");
    return TRUE;
  }

  if (!gst_asf_demux_get_bytes (&s, s_length, p_data, p_size))
    return FALSE;

  g_assert (s != NULL);

  /* just because They don't exist doesn't
   * mean They are not out to get you ... */
  if (s[s_length - 1] != '\0') {
    s = g_realloc (s, s_length + 1);
    s[s_length] = '\0';
  }

  *p_str = (gchar *) s;
  return TRUE;
}


static void
gst_asf_demux_get_guid (ASFGuid * guid, guint8 ** p_data, guint64 * p_size)
{
  g_assert (*p_size >= 4 * sizeof (guint32));

  guid->v1 = gst_asf_demux_get_uint32 (p_data, p_size);
  guid->v2 = gst_asf_demux_get_uint32 (p_data, p_size);
  guid->v3 = gst_asf_demux_get_uint32 (p_data, p_size);
  guid->v4 = gst_asf_demux_get_uint32 (p_data, p_size);
}

static gboolean
gst_asf_demux_get_obj_data_correction (asf_obj_data_correction * object,
    guint8 ** p_data, guint64 * p_size)
{
  if (*p_size < (1 + 1))
    return FALSE;

  object->type = gst_asf_demux_get_uint8 (p_data, p_size);
  object->cycle = gst_asf_demux_get_uint8 (p_data, p_size);
  return TRUE;
}

static gboolean
gst_asf_demux_get_stream_audio (asf_stream_audio * audio, guint8 ** p_data,
    guint64 * p_size)
{
  if (*p_size < (2 + 2 + 4 + 4 + 2 + 2 + 2))
    return FALSE;

  /* WAVEFORMATEX Structure */
  audio->codec_tag = gst_asf_demux_get_uint16 (p_data, p_size);
  audio->channels = gst_asf_demux_get_uint16 (p_data, p_size);
  audio->sample_rate = gst_asf_demux_get_uint32 (p_data, p_size);
  audio->byte_rate = gst_asf_demux_get_uint32 (p_data, p_size);
  audio->block_align = gst_asf_demux_get_uint16 (p_data, p_size);
  audio->word_size = gst_asf_demux_get_uint16 (p_data, p_size);
  /* Codec specific data size */
  audio->size = gst_asf_demux_get_uint16 (p_data, p_size);
  return TRUE;
}

static gboolean
gst_asf_demux_get_stream_video (asf_stream_video * video, guint8 ** p_data,
    guint64 * p_size)
{
  if (*p_size < (4 + 4 + 1 + 2))
    return FALSE;

  video->width = gst_asf_demux_get_uint32 (p_data, p_size);
  video->height = gst_asf_demux_get_uint32 (p_data, p_size);
  video->unknown = gst_asf_demux_get_uint8 (p_data, p_size);
  video->size = gst_asf_demux_get_uint16 (p_data, p_size);
  return TRUE;
}

static gboolean
gst_asf_demux_get_stream_video_format (asf_stream_video_format * fmt,
    guint8 ** p_data, guint64 * p_size)
{
  if (*p_size < (4 + 4 + 4 + 2 + 2 + 4 + 4 + 4 + 4 + 4 + 4))
    return FALSE;

  fmt->size = gst_asf_demux_get_uint32 (p_data, p_size);
  fmt->width = gst_asf_demux_get_uint32 (p_data, p_size);
  fmt->height = gst_asf_demux_get_uint32 (p_data, p_size);
  fmt->planes = gst_asf_demux_get_uint16 (p_data, p_size);
  fmt->depth = gst_asf_demux_get_uint16 (p_data, p_size);
  fmt->tag = gst_asf_demux_get_uint32 (p_data, p_size);
  fmt->image_size = gst_asf_demux_get_uint32 (p_data, p_size);
  fmt->xpels_meter = gst_asf_demux_get_uint32 (p_data, p_size);
  fmt->ypels_meter = gst_asf_demux_get_uint32 (p_data, p_size);
  fmt->num_colors = gst_asf_demux_get_uint32 (p_data, p_size);
  fmt->imp_colors = gst_asf_demux_get_uint32 (p_data, p_size);
  return TRUE;
}

AsfStream *
gst_asf_demux_get_stream (GstASFDemux * demux, guint16 id)
{
  guint i;

  for (i = 0; i < demux->num_streams; i++) {
    if (demux->stream[i].id == id)
      return &demux->stream[i];
  }

  GST_WARNING ("Segment found for undefined stream: (%d)", id);
  return NULL;
}

static void
gst_asf_demux_setup_pad (GstASFDemux * demux, GstPad * src_pad,
    GstCaps * caps, guint16 id, gboolean is_video, GstTagList * tags)
{
  AsfStream *stream;

  gst_pad_use_fixed_caps (src_pad);
  gst_pad_set_caps (src_pad, caps);

  gst_pad_set_event_function (src_pad,
      GST_DEBUG_FUNCPTR (gst_asf_demux_handle_src_event));
  gst_pad_set_query_type_function (src_pad,
      GST_DEBUG_FUNCPTR (gst_asf_demux_get_src_query_types));
  gst_pad_set_query_function (src_pad,
      GST_DEBUG_FUNCPTR (gst_asf_demux_handle_src_query));

  stream = &demux->stream[demux->num_streams];
  stream->caps = caps;
  stream->pad = src_pad;
  stream->id = id;
  stream->frag_offset = 0;
  stream->sequence = 0;
  stream->delay = 0;
  stream->first_pts = GST_CLOCK_TIME_NONE;
  stream->last_pts = GST_CLOCK_TIME_NONE;
  stream->last_buffer_timestamp = GST_CLOCK_TIME_NONE;
  stream->fps_known = !is_video;        /* bit hacky for audio */
  stream->is_video = is_video;
  stream->pending_tags = tags;
  stream->discont = TRUE;

  stream->payloads = g_array_new (FALSE, FALSE, sizeof (AsfPayload));

  GST_INFO ("Created pad %s for stream %u with caps %" GST_PTR_FORMAT,
      GST_PAD_NAME (src_pad), demux->num_streams, caps);

  ++demux->num_streams;

  stream->active = FALSE;
}

static void
gst_asf_demux_add_audio_stream (GstASFDemux * demux,
    asf_stream_audio * audio, guint16 id, guint8 ** p_data, guint64 * p_size)
{
  GstTagList *tags = NULL;
  GstBuffer *extradata = NULL;
  GstPad *src_pad;
  GstCaps *caps;
  guint16 size_left = 0;
  gchar *codec_name = NULL;
  gchar *name = NULL;

  size_left = audio->size;

  /* Create the audio pad */
  name = g_strdup_printf ("audio_%02d", demux->num_audio_streams);

  src_pad = gst_pad_new_from_static_template (&audio_src_template, name);
  g_free (name);

  /* Swallow up any left over data and set up the 
   * standard properties from the header info */
  if (size_left) {
    GST_INFO_OBJECT (demux, "Audio header contains %d bytes of "
        "codec specific data", size_left);

    g_assert (size_left <= *p_size);
    gst_asf_demux_get_buffer (&extradata, size_left, p_data, p_size);
  }

  /* asf_stream_audio is the same as gst_riff_strf_auds, but with an
   * additional two bytes indicating extradata. */
  caps = gst_riff_create_audio_caps (audio->codec_tag, NULL,
      (gst_riff_strf_auds *) audio, extradata, NULL, &codec_name);

  if (caps == NULL) {
    caps = gst_caps_new_simple ("audio/x-asf-unknown", "codec_id",
        G_TYPE_INT, (gint) audio->codec_tag, NULL);
  }

  /* Informing about that audio format we just added */
  if (codec_name) {
    tags = gst_tag_list_new ();
    gst_tag_list_add (tags, GST_TAG_MERGE_APPEND, GST_TAG_AUDIO_CODEC,
        codec_name, NULL);
    g_free (codec_name);
  }

  if (extradata)
    gst_buffer_unref (extradata);

  GST_INFO ("Adding audio stream #%u, id %u codec %u (0x%04x), tags=%"
      GST_PTR_FORMAT, demux->num_audio_streams, id, audio->codec_tag,
      audio->codec_tag, tags);

  ++demux->num_audio_streams;

  gst_asf_demux_setup_pad (demux, src_pad, caps, id, FALSE, tags);
}

static void
gst_asf_demux_add_video_stream (GstASFDemux * demux,
    asf_stream_video_format * video, guint16 id,
    guint8 ** p_data, guint64 * p_size)
{
  GstTagList *tags = NULL;
  GstBuffer *extradata = NULL;
  GstPad *src_pad;
  GstCaps *caps;
  gchar *name = NULL;
  gchar *codec_name = NULL;
  gint size_left = video->size - 40;

  /* Create the video pad */
  name = g_strdup_printf ("video_%02d", demux->num_video_streams);
  src_pad = gst_pad_new_from_static_template (&video_src_template, name);
  g_free (name);

  /* Now try some gstreamer formatted MIME types (from gst_avi_demux_strf_vids) */
  if (size_left) {
    GST_LOG ("Video header has %d bytes of codec specific data", size_left);
    g_assert (size_left <= *p_size);
    gst_asf_demux_get_buffer (&extradata, size_left, p_data, p_size);
  }

  GST_DEBUG ("video codec %" GST_FOURCC_FORMAT, GST_FOURCC_ARGS (video->tag));

  /* yes, asf_stream_video_format and gst_riff_strf_vids are the same */
  caps = gst_riff_create_video_caps (video->tag, NULL,
      (gst_riff_strf_vids *) video, extradata, NULL, &codec_name);

  if (caps == NULL) {
    caps = gst_caps_new_simple ("video/x-asf-unknown", "fourcc",
        GST_TYPE_FOURCC, video->tag, NULL);
  } else {
    GstStructure *s;
    gint ax, ay;

    s = gst_asf_demux_get_metadata_for_stream (demux, id);
    if (gst_structure_get_int (s, "AspectRatioX", &ax) &&
        gst_structure_get_int (s, "AspectRatioY", &ay)) {
      gst_caps_set_simple (caps, "pixel-aspect-ratio", GST_TYPE_FRACTION,
          ax, ay, NULL);
    }
    /* remove the framerate we will guess and add it later */
    s = gst_caps_get_structure (caps, 0);
    gst_structure_remove_field (s, "framerate");
  }

  /* add fourcc format to caps, some proprietary decoders seem to need it */
  gst_caps_set_simple (caps, "format", GST_TYPE_FOURCC, video->tag, NULL);

  if (codec_name) {
    tags = gst_tag_list_new ();
    gst_tag_list_add (tags, GST_TAG_MERGE_APPEND, GST_TAG_VIDEO_CODEC,
        codec_name, NULL);
    g_free (codec_name);
  }

  if (extradata)
    gst_buffer_unref (extradata);

  GST_INFO ("Adding video stream %u codec %" GST_FOURCC_FORMAT " (0x%08x)",
      demux->num_video_streams, GST_FOURCC_ARGS (video->tag), video->tag);

  ++demux->num_video_streams;

  gst_asf_demux_setup_pad (demux, src_pad, caps, id, TRUE, tags);
}

static void
gst_asf_demux_activate_stream (GstASFDemux * demux, AsfStream * stream)
{
  if (!stream->active) {
    GST_INFO_OBJECT (demux, "Activating stream %2u, pad %s, caps %"
        GST_PTR_FORMAT, stream->id, GST_PAD_NAME (stream->pad), stream->caps);
    gst_pad_set_active (stream->pad, TRUE);
    gst_element_add_pad (GST_ELEMENT_CAST (demux), stream->pad);
    stream->active = TRUE;
  }
}

static AsfStream *
gst_asf_demux_parse_stream_object (GstASFDemux * demux, guint8 * data,
    guint64 size)
{
  AsfCorrectionType correction_type;
  AsfStreamType stream_type;
  GstClockTime time_offset;
  gboolean is_encrypted;
  guint16 stream_id;
  guint16 flags;
  ASFGuid guid;
  guint stream_specific_size;
  guint type_specific_size;
  guint unknown;

  /* Get the rest of the header's header */
  if (size < (16 + 16 + 8 + 4 + 4 + 2 + 4))
    goto not_enough_data;

  gst_asf_demux_get_guid (&guid, &data, &size);
  stream_type = gst_asf_demux_identify_guid (asf_stream_guids, &guid);

  gst_asf_demux_get_guid (&guid, &data, &size);
  correction_type = gst_asf_demux_identify_guid (asf_correction_guids, &guid);

  time_offset = gst_asf_demux_get_uint64 (&data, &size) * 100;

  type_specific_size = gst_asf_demux_get_uint32 (&data, &size);
  stream_specific_size = gst_asf_demux_get_uint32 (&data, &size);

  flags = gst_asf_demux_get_uint16 (&data, &size);
  stream_id = flags & 0x7f;
  is_encrypted = !!((flags & 0x8000) << 15);
  unknown = gst_asf_demux_get_uint32 (&data, &size);

  GST_DEBUG_OBJECT (demux, "Found stream %u, time_offset=%" GST_TIME_FORMAT,
      stream_id, GST_TIME_ARGS (time_offset));

  switch (stream_type) {
    case ASF_STREAM_AUDIO:{
      asf_stream_audio audio_object;

      if (!gst_asf_demux_get_stream_audio (&audio_object, &data, &size))
        goto not_enough_data;

      GST_INFO ("Object is an audio stream with %u bytes of additional data",
          audio_object.size);

      gst_asf_demux_add_audio_stream (demux, &audio_object, stream_id,
          &data, &size);

      switch (correction_type) {
        case ASF_CORRECTION_ON:{
          guint span, packet_size, chunk_size, data_size, silence_data;

          GST_INFO ("Using error correction");

          if (size < (1 + 2 + 2 + 2 + 1))
            goto not_enough_data;

          span = gst_asf_demux_get_uint8 (&data, &size);
          packet_size = gst_asf_demux_get_uint16 (&data, &size);
          chunk_size = gst_asf_demux_get_uint16 (&data, &size);
          data_size = gst_asf_demux_get_uint16 (&data, &size);
          silence_data = gst_asf_demux_get_uint8 (&data, &size);

          /* FIXME: shouldn't this be per-stream? */
          demux->span = span;

          GST_DEBUG_OBJECT (demux, "Descrambling ps:%u cs:%u ds:%u s:%u sd:%u",
              packet_size, chunk_size, data_size, span, silence_data);

          if (demux->span > 1) {
            if (chunk_size == 0 || ((packet_size / chunk_size) <= 1)) {
              /* Disable descrambling */
              demux->span = 0;
            } else {
              /* FIXME: this else branch was added for
               * weird_al_yankovic - the saga begins.asf */
              demux->ds_packet_size = packet_size;
              demux->ds_chunk_size = chunk_size;
            }
          } else {
            /* Descambling is enabled */
            demux->ds_packet_size = packet_size;
            demux->ds_chunk_size = chunk_size;
          }
#if 0
          /* Now skip the rest of the silence data */
          if (data_size > 1)
            gst_bytestream_flush (demux->bs, data_size - 1);
#else
          /* FIXME: CHECKME. And why -1? */
          if (data_size > 1) {
            if (!gst_asf_demux_skip_bytes (data_size - 1, &data, &size)) {
              goto not_enough_data;
            }
          }
#endif
          break;
        }
        case ASF_CORRECTION_OFF:{
          GST_INFO ("Error correction off");
          if (!gst_asf_demux_skip_bytes (stream_specific_size, &data, &size))
            goto not_enough_data;
          break;
        }
        default:
          GST_ELEMENT_ERROR (demux, STREAM, DEMUX, (NULL),
              ("Audio stream using unknown error correction"));
          return NULL;
      }

      break;
    }

    case ASF_STREAM_VIDEO:{
      asf_stream_video_format video_format_object;
      asf_stream_video video_object;
      guint16 vsize;

      if (!gst_asf_demux_get_stream_video (&video_object, &data, &size))
        goto not_enough_data;

      vsize = video_object.size - 40;   /* Byte order gets offset by single byte */

      GST_INFO ("object is a video stream with %u bytes of "
          "additional data", vsize);

      if (!gst_asf_demux_get_stream_video_format (&video_format_object,
              &data, &size)) {
        goto not_enough_data;
      }

      gst_asf_demux_add_video_stream (demux, &video_format_object, stream_id,
          &data, &size);

      break;
    }

    default:
      GST_WARNING_OBJECT (demux, "Unknown stream type for stream %u",
          stream_id);
      break;
  }

  return gst_asf_demux_get_stream (demux, stream_id);

not_enough_data:
  {
    GST_WARNING_OBJECT (demux, "Unexpected end of data parsing stream object");
    /* we'll error out later if we found no streams */
    return NULL;
  }
}

static const gchar *
gst_asf_demux_get_gst_tag_from_tag_name (const gchar * name_utf16le,
    gsize name_len)
{
  const struct
  {
    const gchar *asf_name;
    const gchar *gst_name;
  } tags[] = {
    {
    "WM/Genre", GST_TAG_GENRE}, {
    "WM/AlbumTitle", GST_TAG_ALBUM}, {
    "WM/AlbumArtist", GST_TAG_ARTIST}, {
    "WM/Track", GST_TAG_TRACK_NUMBER}, {
    "WM/Year", GST_TAG_DATE}
    /* { "WM/Composer", GST_TAG_COMPOSER } */
  };
  gchar *name_utf8;
  gsize in, out;
  guint i;

  /* convert name to UTF-8 */
  name_utf8 = g_convert (name_utf16le, name_len, "UTF-8", "UTF-16LE", &in,
      &out, NULL);

  if (name_utf8 == NULL) {
    GST_WARNING ("Failed to convert name to UTF8, skipping");
    return NULL;
  }

  for (i = 0; i < G_N_ELEMENTS (tags); ++i) {
    if (strncmp (tags[i].asf_name, name_utf8, out) == 0) {
      GST_LOG ("map tagname '%s' -> '%s'", name_utf8, tags[i].gst_name);
      g_free (name_utf8);
      return tags[i].gst_name;
    }
  }

  GST_LOG ("unhandled tagname '%s'", name_utf8);
  g_free (name_utf8);
  return NULL;
}

/* gst_asf_demux_commit_taglist() takes ownership of taglist! */
static void
gst_asf_demux_commit_taglist (GstASFDemux * demux, GstTagList * taglist)
{
  GST_DEBUG ("Committing tags: %" GST_PTR_FORMAT, taglist);

  gst_element_found_tags (GST_ELEMENT (demux), gst_tag_list_copy (taglist));

  /* save internally */
  if (!demux->taglist)
    demux->taglist = taglist;
  else {
    GstTagList *t;

    t = gst_tag_list_merge (demux->taglist, taglist, GST_TAG_MERGE_APPEND);
    gst_tag_list_free (demux->taglist);
    gst_tag_list_free (taglist);
    demux->taglist = t;
  }
}

#define ASF_DEMUX_DATA_TYPE_UTF16LE_STRING  0
#define ASF_DEMUX_DATA_TYPE_DWORD           3

/* Extended Content Description Object */
static GstFlowReturn
gst_asf_demux_process_ext_content_desc (GstASFDemux * demux, guint8 * data,
    guint64 size)
{
  /* Other known (and unused) 'text/unicode' metadata available :
   *
   *   WM/Lyrics =
   *   WM/MediaPrimaryClassID = {D1607DBC-E323-4BE2-86A1-48A42A28441E}
   *   WMFSDKVersion = 9.00.00.2980
   *   WMFSDKNeeded = 0.0.0.0000
   *   WM/UniqueFileIdentifier = AMGa_id=R    15334;AMGp_id=P     5149;AMGt_id=T  2324984
   *   WM/Publisher = 4AD
   *   WM/Provider = AMG
   *   WM/ProviderRating = 8
   *   WM/ProviderStyle = Rock (similar to WM/Genre)
   *   WM/GenreID (similar to WM/Genre)
   *   WM/TrackNumber (same as WM/Track but as a string)
   *
   * Other known (and unused) 'non-text' metadata available :
   *
   *   WM/EncodingTime
   *   WM/MCDI
   *   IsVBR
   *
   * We might want to read WM/TrackNumber and use atoi() if we don't have
   * WM/Track
   */

  GstTagList *taglist;
  guint16 blockcount, i;

  GST_INFO_OBJECT (demux, "object is an extended content description");

  taglist = gst_tag_list_new ();

  /* Content Descriptor Count */
  if (size < 2)
    goto not_enough_data;

  blockcount = gst_asf_demux_get_uint16 (&data, &size);

  for (i = 1; i <= blockcount; ++i) {
    const gchar *gst_tag_name;
    guint16 datatype;
    guint16 value_len;
    guint16 name_len;
    GValue tag_value = { 0, };
    gsize in, out;
    gchar *name;
    gchar *value;

    /* Descriptor */
    if (!gst_asf_demux_get_string (&name, &name_len, &data, &size))
      goto not_enough_data;

    if (size < 2) {
      g_free (name);
      goto not_enough_data;
    }
    /* Descriptor Value Data Type */
    datatype = gst_asf_demux_get_uint16 (&data, &size);

    /* Descriptor Value (not really a string, but same thing reading-wise) */
    if (!gst_asf_demux_get_string (&value, &value_len, &data, &size)) {
      g_free (name);
      goto not_enough_data;
    }

    gst_tag_name = gst_asf_demux_get_gst_tag_from_tag_name (name, name_len);
    if (gst_tag_name != NULL) {
      switch (datatype) {
        case ASF_DEMUX_DATA_TYPE_UTF16LE_STRING:{
          gchar *value_utf8;

          value_utf8 = g_convert (value, value_len, "UTF-8", "UTF-16LE",
              &in, &out, NULL);

          /* get rid of tags with empty value */
          if (value_utf8 != NULL && *value_utf8 != '\0') {
            value_utf8[out] = '\0';

            if (strcmp (gst_tag_name, GST_TAG_DATE) == 0) {
              guint year = atoi (value_utf8);

              if (year > 0) {
                GDate *date = g_date_new_dmy (1, 1, year);

                g_value_init (&tag_value, GST_TYPE_DATE);
                gst_value_set_date (&tag_value, date);
                g_date_free (date);
              }
            } else {
              GType tag_type;

              /* convert tag from string to other type if required */
              tag_type = gst_tag_get_type (gst_tag_name);
              g_value_init (&tag_value, tag_type);
              if (!gst_value_deserialize (&tag_value, value_utf8)) {
                GValue from_val = { 0, };

                g_value_init (&from_val, G_TYPE_STRING);
                g_value_set_string (&from_val, value_utf8);
                if (!g_value_transform (&from_val, &tag_value)) {
                  GST_WARNING_OBJECT (demux,
                      "Could not transform string tag to " "%s tag type %s",
                      gst_tag_name, g_type_name (tag_type));
                  g_value_unset (&tag_value);
                }
                g_value_unset (&from_val);
              }
            }
          } else if (value_utf8 == NULL) {
            GST_WARNING ("Failed to convert string value to UTF8, skipping");
          } else {
            GST_DEBUG ("Skipping empty string value for %s", gst_tag_name);
          }
          g_free (value_utf8);
          break;
        }
        case ASF_DEMUX_DATA_TYPE_DWORD:{
          /* this is the track number */
          g_value_init (&tag_value, G_TYPE_UINT);
          g_value_set_uint (&tag_value, (guint) GST_READ_UINT32_LE (value));
          break;
        }
        default:{
          GST_DEBUG ("Skipping tag %s of type %d", gst_tag_name, datatype);
          break;
        }
      }

      if (G_IS_VALUE (&tag_value)) {
        gst_tag_list_add_values (taglist, GST_TAG_MERGE_APPEND,
            gst_tag_name, &tag_value, NULL);

        g_value_unset (&tag_value);
      }
    }

    g_free (name);
    g_free (value);
  }

  if (gst_structure_n_fields (GST_STRUCTURE (taglist)) > 0) {
    gst_asf_demux_commit_taglist (demux, taglist);
  } else {
    gst_tag_list_free (taglist);
  }

  return GST_FLOW_OK;

  /* Errors */
not_enough_data:
  {
    GST_WARNING ("Unexpected end of data parsing ext content desc object");
    gst_tag_list_free (taglist);
    return GST_FLOW_OK;         /* not really fatal */
  }
}

static GstStructure *
gst_asf_demux_get_metadata_for_stream (GstASFDemux * demux, guint stream_num)
{
  gchar sname[32];
  guint i;

  g_snprintf (sname, sizeof (sname), "stream-%u", stream_num);

  for (i = 0; i < gst_caps_get_size (demux->metadata); ++i) {
    GstStructure *s;

    s = gst_caps_get_structure (demux->metadata, i);
    if (gst_structure_has_name (s, sname))
      return s;
  }

  gst_caps_append_structure (demux->metadata, gst_structure_empty_new (sname));

  /* try lookup again; demux->metadata took ownership of the structure, so we
   * can't really make any assumptions about what happened to it, so we can't
   * just return it directly after appending it */
  return gst_asf_demux_get_metadata_for_stream (demux, stream_num);
}

static GstFlowReturn
gst_asf_demux_process_metadata (GstASFDemux * demux, guint8 * data,
    guint64 size)
{
  guint16 blockcount, i;

  GST_INFO_OBJECT (demux, "object is a metadata object");

  /* Content Descriptor Count */
  if (size < 2)
    goto not_enough_data;

  blockcount = gst_asf_demux_get_uint16 (&data, &size);

  for (i = 0; i < blockcount; ++i) {
    GstStructure *s;
    guint16 lang_idx, stream_num, name_len, data_type;
    guint32 data_len, ival;
    gchar *name_utf8;

    if (size < (2 + 2 + 2 + 2 + 4))
      goto not_enough_data;

    lang_idx = gst_asf_demux_get_uint16 (&data, &size);
    stream_num = gst_asf_demux_get_uint16 (&data, &size);
    name_len = gst_asf_demux_get_uint16 (&data, &size);
    data_type = gst_asf_demux_get_uint16 (&data, &size);
    data_len = gst_asf_demux_get_uint32 (&data, &size);

    if (size < name_len + data_len)
      goto not_enough_data;

    /* convert name to UTF-8 */
    name_utf8 = g_convert ((gchar *) data, name_len, "UTF-8", "UTF-16LE",
        NULL, NULL, NULL);
    gst_asf_demux_skip_bytes (name_len, &data, &size);

    if (name_utf8 == NULL) {
      GST_WARNING ("Failed to convert value name to UTF8, skipping");
      gst_asf_demux_skip_bytes (data_len, &data, &size);
      continue;
    }

    if (data_type != ASF_DEMUX_DATA_TYPE_DWORD) {
      gst_asf_demux_skip_bytes (data_len, &data, &size);
      continue;
    }

    /* read DWORD */
    if (size < 4)
      goto not_enough_data;

    ival = gst_asf_demux_get_uint32 (&data, &size);

    /* skip anything else there may be, just in case */
    gst_asf_demux_skip_bytes (data_len - 4, &data, &size);

    s = gst_asf_demux_get_metadata_for_stream (demux, stream_num);
    gst_structure_set (s, name_utf8, G_TYPE_INT, ival, NULL);
    g_free (name_utf8);
  }

  GST_INFO_OBJECT (demux, "metadata = %" GST_PTR_FORMAT, demux->metadata);
  return GST_FLOW_OK;

  /* Errors */
not_enough_data:
  {
    GST_WARNING ("Unexpected end of data parsing metadata object");
    return GST_FLOW_OK;         /* not really fatal */
  }
}

static GstFlowReturn
gst_asf_demux_process_header (GstASFDemux * demux, guint8 * data, guint64 size)
{
  GstFlowReturn ret = GST_FLOW_OK;
  guint32 i, num_objects;
  guint8 unknown;

  /* Get the rest of the header's header */
  if (size < (4 + 1 + 1))
    goto not_enough_data;

  num_objects = gst_asf_demux_get_uint32 (&data, &size);
  unknown = gst_asf_demux_get_uint8 (&data, &size);
  unknown = gst_asf_demux_get_uint8 (&data, &size);

  GST_INFO_OBJECT (demux, "object is a header with %u parts", num_objects);

  /* Loop through the header's objects, processing those */
  for (i = 0; i < num_objects; ++i) {
    GST_INFO_OBJECT (demux, "reading header part %u", i);
    ret = gst_asf_demux_process_object (demux, &data, &size);
    if (ret != GST_FLOW_OK) {
      GST_WARNING ("process_object returned %s", gst_asf_get_flow_name (ret));
      break;
    }
  }

  return ret;

not_enough_data:
  {
    GST_ELEMENT_ERROR (demux, STREAM, DEMUX, (NULL),
        ("short read parsing HEADER object"));
    return GST_FLOW_ERROR;
  }
}

static GstFlowReturn
gst_asf_demux_process_file (GstASFDemux * demux, guint8 * data, guint64 size)
{
  guint64 file_size, creation_time, packets_count;
  guint64 play_time, send_time, preroll;
  guint32 flags, min_pktsize, max_pktsize, min_bitrate;

  if (size < (16 + 8 + 8 + 8 + 8 + 8 + 8 + 4 + 4 + 4 + 4))
    goto not_enough_data;

  gst_asf_demux_skip_bytes (16, &data, &size);  /* skip GUID */
  file_size = gst_asf_demux_get_uint64 (&data, &size);
  creation_time = gst_asf_demux_get_uint64 (&data, &size);
  packets_count = gst_asf_demux_get_uint64 (&data, &size);
  play_time = gst_asf_demux_get_uint64 (&data, &size);
  send_time = gst_asf_demux_get_uint64 (&data, &size);
  preroll = gst_asf_demux_get_uint64 (&data, &size);
  flags = gst_asf_demux_get_uint32 (&data, &size);
  min_pktsize = gst_asf_demux_get_uint32 (&data, &size);
  max_pktsize = gst_asf_demux_get_uint32 (&data, &size);
  min_bitrate = gst_asf_demux_get_uint32 (&data, &size);

  demux->broadcast = !!(flags & 0x01);
  demux->seekable = !!(flags & 0x02);

  GST_DEBUG_OBJECT (demux, "flags::broadcast = %d", demux->broadcast);
  GST_DEBUG_OBJECT (demux, "flags::seekable  = %d", demux->seekable);

  if (demux->broadcast) {
    /* these fields are invalid if the broadcast flag is set */
    play_time = 0;
    file_size = 0;
  }

  if (min_pktsize != max_pktsize)
    goto non_fixed_packet_size;

  demux->packet_size = max_pktsize;

  /* FIXME: do we need send_time as well? what is it? */
  if ((play_time * 100) >= (preroll * GST_MSECOND))
    demux->play_time = (play_time * 100) - (preroll * GST_MSECOND);
  else
    demux->play_time = 0;

  demux->preroll = preroll;     /* FIXME: make GstClockTime */

  if (demux->play_time == 0)
    demux->seekable = FALSE;

  GST_DEBUG_OBJECT (demux, "play_time %" GST_TIME_FORMAT,
      GST_TIME_ARGS (demux->play_time));
  GST_DEBUG_OBJECT (demux, "preroll   %" GST_TIME_FORMAT,
      GST_TIME_ARGS (demux->preroll * GST_MSECOND));

  if (demux->play_time > 0) {
    gst_segment_set_duration (&demux->segment, GST_FORMAT_TIME,
        demux->play_time);
  }

  GST_INFO ("object is a file with %" G_GUINT64_FORMAT " data packets",
      packets_count);
  GST_INFO ("preroll = %" G_GUINT64_FORMAT, demux->preroll);

  return GST_FLOW_OK;

/* ERRORS */
non_fixed_packet_size:
  {
    GST_ELEMENT_ERROR (demux, STREAM, DEMUX, (NULL),
        ("packet size must be fixed"));
    return GST_FLOW_ERROR;
  }
not_enough_data:
  {
    GST_ELEMENT_ERROR (demux, STREAM, DEMUX, (NULL),
        ("short read parsing FILE object"));
    return GST_FLOW_ERROR;
  }
}

/* Content Description Object */
static GstFlowReturn
gst_asf_demux_process_comment (GstASFDemux * demux, guint8 * data, guint64 size)
{
  struct
  {
    const gchar *gst_tag;
    guint16 val_length;
    gchar *val_utf8;
  } tags[5] = {
    {
    GST_TAG_TITLE, 0, NULL}, {
    GST_TAG_ARTIST, 0, NULL}, {
    GST_TAG_COPYRIGHT, 0, NULL}, {
    GST_TAG_DESCRIPTION, 0, NULL}, {
    GST_TAG_COMMENT, 0, NULL}
  };
  GstTagList *taglist;
  GValue value = { 0 };
  gsize in, out;
  gint i = -1;

  GST_INFO_OBJECT (demux, "object is a comment");

  if (size < (2 + 2 + 2 + 2 + 2))
    goto not_enough_data;

  tags[0].val_length = gst_asf_demux_get_uint16 (&data, &size);
  tags[1].val_length = gst_asf_demux_get_uint16 (&data, &size);
  tags[2].val_length = gst_asf_demux_get_uint16 (&data, &size);
  tags[3].val_length = gst_asf_demux_get_uint16 (&data, &size);
  tags[4].val_length = gst_asf_demux_get_uint16 (&data, &size);

  GST_DEBUG_OBJECT (demux, "Comment lengths: title=%d author=%d copyright=%d "
      "description=%d rating=%d", tags[0].val_length, tags[1].val_length,
      tags[2].val_length, tags[3].val_length, tags[4].val_length);

  for (i = 0; i < G_N_ELEMENTS (tags); ++i) {
    if (size < tags[i].val_length)
      goto not_enough_data;

    /* might be just '/0', '/0'... */
    if (tags[i].val_length > 2 && tags[i].val_length % 2 == 0) {
      /* convert to UTF-8 */
      tags[i].val_utf8 = g_convert ((gchar *) data, tags[i].val_length,
          "UTF-8", "UTF-16LE", &in, &out, NULL);
    }
    gst_asf_demux_skip_bytes (tags[i].val_length, &data, &size);
  }

  /* parse metadata into taglist */
  taglist = gst_tag_list_new ();
  g_value_init (&value, G_TYPE_STRING);
  for (i = 0; i < G_N_ELEMENTS (tags); ++i) {
    if (tags[i].val_utf8 && strlen (tags[i].val_utf8) > 0 && tags[i].gst_tag) {
      g_value_set_string (&value, tags[i].val_utf8);
      gst_tag_list_add_values (taglist, GST_TAG_MERGE_APPEND,
          tags[i].gst_tag, &value, NULL);
    }
  }
  g_value_unset (&value);

  if (gst_structure_n_fields (GST_STRUCTURE (taglist)) > 0) {
    gst_asf_demux_commit_taglist (demux, taglist);
  } else {
    gst_tag_list_free (taglist);
  }

  for (i = 0; i < G_N_ELEMENTS (tags); ++i)
    g_free (tags[i].val_utf8);

  return GST_FLOW_OK;

not_enough_data:
  {
    GST_WARNING_OBJECT (demux, "unexpectedly short of data while processing "
        "comment tag section %d, skipping comment object", i);
    for (i = 0; i < G_N_ELEMENTS (tags); i++)
      g_free (tags[i].val_utf8);
    return GST_FLOW_OK;         /* not really fatal */
  }
}

static GstFlowReturn
gst_asf_demux_process_bitrate_props_object (GstASFDemux * demux, guint8 * data,
    guint64 size)
{
  guint16 num_streams, i;

  if (size < 2)
    goto not_enough_data;

  num_streams = gst_asf_demux_get_uint16 (&data, &size);

  GST_INFO ("object is a bitrate properties object with %u streams",
      num_streams);

  if (size < (num_streams * (2 + 4)))
    goto not_enough_data;

  for (i = 0; i < num_streams; ++i) {
    guint32 bitrate;
    guint16 stream_id;

    stream_id = gst_asf_demux_get_uint16 (&data, &size);
    bitrate = gst_asf_demux_get_uint32 (&data, &size);

    if (stream_id < GST_ASF_DEMUX_NUM_STREAM_IDS) {
      demux->bitrate[stream_id] = bitrate;
      GST_DEBUG ("bitrate[%u] = %u", stream_id, bitrate);
    } else {
      GST_WARNING ("stream id %u is too large", stream_id);
    }
  }

  return GST_FLOW_OK;

not_enough_data:
  {
    GST_WARNING_OBJECT (demux, "short read parsing bitrate props object!");
    return GST_FLOW_OK;         /* not really fatal */
  }
}

static GstFlowReturn
gst_asf_demux_process_header_ext (GstASFDemux * demux, guint8 * data,
    guint64 size)
{
  GstFlowReturn ret = GST_FLOW_OK;
  guint64 hdr_size;

  /* Get the rest of the header's header */
  if (size < (16 + 2 + 4))
    goto not_enough_data;

  /* skip GUID and two other bytes */
  gst_asf_demux_skip_bytes (16 + 2, &data, &size);
  hdr_size = gst_asf_demux_get_uint32 (&data, &size);

  GST_INFO ("extended header object with a size of %u bytes", (guint) size);

  /* FIXME: does data_size include the rest of the header that we have read? */
  if (hdr_size > size)
    goto not_enough_data;

  while (hdr_size > 0) {
    ret = gst_asf_demux_process_object (demux, &data, &hdr_size);
    if (ret != GST_FLOW_OK)
      break;
  }

  return ret;

not_enough_data:
  {
    GST_ELEMENT_ERROR (demux, STREAM, DEMUX, (NULL),
        ("short read parsing extended header object"));
    return GST_FLOW_ERROR;
  }
}

static GstFlowReturn
gst_asf_demux_process_language_list (GstASFDemux * demux, guint8 * data,
    guint64 size)
{
  guint i;

  if (size < 2)
    goto not_enough_data;

  if (demux->languages) {
    GST_WARNING ("More than one LANGUAGE_LIST object in stream");
    g_strfreev (demux->languages);
    demux->languages = NULL;
    demux->num_languages = 0;
  }

  demux->num_languages = gst_asf_demux_get_uint16 (&data, &size);
  GST_LOG ("%u languages:", demux->num_languages);

  demux->languages = g_new0 (gchar *, demux->num_languages + 1);
  for (i = 0; i < demux->num_languages; ++i) {
    guint8 len, *lang_data = NULL;

    if (size < 1)
      goto not_enough_data;
    len = gst_asf_demux_get_uint8 (&data, &size);
    if (gst_asf_demux_get_bytes (&lang_data, len, &data, &size)) {
      gchar *utf8;

      utf8 = g_convert ((gchar *) lang_data, len, "UTF-8", "UTF-16LE", NULL,
          NULL, NULL);

      /* truncate "en-us" etc. to just "en" */
      if (utf8 && strlen (utf8) >= 5 && (utf8[2] == '-' || utf8[2] == '_')) {
        utf8[2] = '\0';
      }
      GST_DEBUG ("[%u] %s", i, GST_STR_NULL (utf8));
      demux->languages[i] = utf8;
      g_free (lang_data);
    } else {
      goto not_enough_data;
    }
  }

  return GST_FLOW_OK;

not_enough_data:
  {
    GST_WARNING_OBJECT (demux, "short read parsing language list object!");
    g_free (demux->languages);
    demux->languages = NULL;
    return GST_FLOW_OK;         /* not fatal */
  }
}

static GstFlowReturn
gst_asf_demux_process_simple_index (GstASFDemux * demux, guint8 * data,
    guint64 size)
{
  GstClockTime interval;
  guint32 x, count, i;

  if (size < (16 + 8 + 4 + 4))
    goto not_enough_data;

  /* skip file id */
  gst_asf_demux_skip_bytes (16, &data, &size);
  interval = gst_asf_demux_get_uint64 (&data, &size) * (GstClockTime) 100;
  x = gst_asf_demux_get_uint32 (&data, &size);
  count = gst_asf_demux_get_uint32 (&data, &size);
  if (count > 0) {
    demux->sidx_interval = interval;
    demux->sidx_num_entries = count;
    g_free (demux->sidx_entries);
    demux->sidx_entries = g_new0 (guint32, count);

    for (i = 0; i < count && size > (4 + 2); ++i) {
      demux->sidx_entries[i] = gst_asf_demux_get_uint32 (&data, &size);
      x = (guint32) gst_asf_demux_get_uint16 (&data, &size);
      GST_LOG_OBJECT (demux, "%" GST_TIME_FORMAT " = packet %4u",
          GST_TIME_ARGS (i * interval), demux->sidx_entries[i]);
    }
  } else {
    GST_DEBUG_OBJECT (demux, "simple index object with 0 entries");
  }

  return GST_FLOW_OK;

not_enough_data:
  {
    GST_WARNING_OBJECT (demux, "short read parsing simple index object!");
    return GST_FLOW_OK;         /* not fatal */
  }
}

static GstFlowReturn
gst_asf_demux_process_advanced_mutual_exclusion (GstASFDemux * demux,
    guint8 * data, guint64 size)
{
  ASFGuid guid;
  guint16 num, i;
  guint8 *mes;

  if (size < 16 + 2 + (2 * 2))
    goto not_enough_data;

  gst_asf_demux_get_guid (&guid, &data, &size);
  num = gst_asf_demux_get_uint16 (&data, &size);

  if (num < 2) {
    GST_WARNING_OBJECT (demux, "nonsensical mutually exclusive streams count");
    return GST_FLOW_OK;
  }

  if (size < (num * sizeof (guint16)))
    goto not_enough_data;

  /* read mutually exclusive stream numbers */
  mes = g_new (guint8, num + 1);
  for (i = 0; i < num; ++i) {
    mes[i] = gst_asf_demux_get_uint16 (&data, &size) & 0x7f;
    GST_LOG_OBJECT (demux, "mutually exclusive: stream #%d", mes[i]);
  }

  /* add terminator so we can easily get the count or know when to stop */
  mes[i] = (guint8) - 1;

  demux->mut_ex_streams = g_slist_append (demux->mut_ex_streams, mes);

  return GST_FLOW_OK;

  /* Errors */
not_enough_data:
  {
    GST_WARNING_OBJECT (demux, "short read parsing advanced mutual exclusion");
    return GST_FLOW_OK;         /* not absolutely fatal */
  }
}

static GstFlowReturn
gst_asf_demux_process_ext_stream_props (GstASFDemux * demux, guint8 * data,
    guint64 size)
{
  AsfStreamExtProps esp;
  AsfStream *stream = NULL;
  AsfObject stream_obj;
  guint16 stream_name_count;
  guint16 num_payload_ext;
  guint64 len;
  guint8 *stream_obj_data = NULL;
  guint8 *data_start;
  guint obj_size;
  guint i, stream_num;

  data_start = data;
  obj_size = (guint) size;

  if (size < 64)
    goto not_enough_data;

  esp.valid = TRUE;
  esp.start_time = gst_asf_demux_get_uint64 (&data, &size) * GST_MSECOND;
  esp.end_time = gst_asf_demux_get_uint64 (&data, &size) * GST_MSECOND;
  esp.data_bitrate = gst_asf_demux_get_uint32 (&data, &size);
  esp.buffer_size = gst_asf_demux_get_uint32 (&data, &size);
  esp.intial_buf_fullness = gst_asf_demux_get_uint32 (&data, &size);
  esp.data_bitrate2 = gst_asf_demux_get_uint32 (&data, &size);
  esp.buffer_size2 = gst_asf_demux_get_uint32 (&data, &size);
  esp.intial_buf_fullness2 = gst_asf_demux_get_uint32 (&data, &size);
  esp.max_obj_size = gst_asf_demux_get_uint32 (&data, &size);
  esp.flags = gst_asf_demux_get_uint32 (&data, &size);
  stream_num = gst_asf_demux_get_uint16 (&data, &size);
  esp.lang_idx = gst_asf_demux_get_uint16 (&data, &size);
  esp.avg_time_per_frame = gst_asf_demux_get_uint64 (&data, &size);
  stream_name_count = gst_asf_demux_get_uint16 (&data, &size);
  num_payload_ext = gst_asf_demux_get_uint16 (&data, &size);

  GST_INFO ("start_time             = %" GST_TIME_FORMAT,
      GST_TIME_ARGS (esp.start_time));
  GST_INFO ("end_time               = %" GST_TIME_FORMAT,
      GST_TIME_ARGS (esp.end_time));
  GST_INFO ("flags                  = %08x", esp.flags);
  GST_INFO ("average time per frame = %" GST_TIME_FORMAT,
      GST_TIME_ARGS (esp.avg_time_per_frame * 100));
  GST_INFO ("stream number          = %u", stream_num);
  GST_INFO ("stream language ID idx = %u (%s)", esp.lang_idx,
      (esp.lang_idx < demux->num_languages) ?
      GST_STR_NULL (demux->languages[esp.lang_idx]) : "??");
  GST_INFO ("stream name count      = %u", stream_name_count);

  /* read stream names */
  for (i = 0; i < stream_name_count; ++i) {
    guint16 stream_lang_idx;
    gchar *stream_name = NULL;

    if (size < 2)
      goto not_enough_data;
    stream_lang_idx = gst_asf_demux_get_uint16 (&data, &size);
    if (!gst_asf_demux_get_string (&stream_name, NULL, &data, &size))
      goto not_enough_data;
    GST_INFO ("stream name %d: %s", i, GST_STR_NULL (stream_name));
    g_free (stream_name);       /* TODO: store names in struct */
  }

  /* read payload extension systems stuff */
  GST_LOG ("payload extension systems count = %u", num_payload_ext);

  if (num_payload_ext > 0)
    esp.payload_extensions = g_new0 (AsfPayloadExtension, num_payload_ext + 1);
  else
    esp.payload_extensions = NULL;

  for (i = 0; i < num_payload_ext; ++i) {
    AsfPayloadExtension ext;
    ASFGuid ext_guid;
    guint32 sys_info_len;

    if (size < 16 + 2 + 4)
      goto not_enough_data;

    gst_asf_demux_get_guid (&ext_guid, &data, &size);
    ext.id = gst_asf_demux_identify_guid (asf_payload_ext_guids, &ext_guid);
    ext.len = gst_asf_demux_get_uint16 (&data, &size);

    sys_info_len = gst_asf_demux_get_uint32 (&data, &size);
    GST_LOG ("payload systems info len = %u", sys_info_len);
    if (!gst_asf_demux_skip_bytes (sys_info_len, &data, &size))
      goto not_enough_data;

    esp.payload_extensions[i] = ext;
  }

  GST_LOG ("bytes read: %u/%u", (guint) (data - data_start), obj_size);

  /* there might be an optional STREAM_INFO object here now; if not, we
   * should have parsed the corresponding stream info object already (since
   * we are parsing the extended stream properties objects delayed) */
  if (size == 0) {
    stream = gst_asf_demux_get_stream (demux, stream_num);
    goto done;
  }

  /* get size of the stream object */
  if (!asf_demux_peek_object (demux, data, size, &stream_obj))
    goto not_enough_data;

  if (stream_obj.id != ASF_OBJ_STREAM)
    goto expected_stream_object;

  if (stream_obj.size < ASF_OBJECT_HEADER_SIZE ||
      stream_obj.size > (10 * 1024 * 1024))
    goto not_enough_data;

  gst_asf_demux_skip_bytes (ASF_OBJECT_HEADER_SIZE, &data, &size);

  /* process this stream object later after all the other 'normal' ones
   * have been processed (since the others are more important/non-hidden) */
  len = stream_obj.size - ASF_OBJECT_HEADER_SIZE;
  if (!gst_asf_demux_get_bytes (&stream_obj_data, len, &data, &size))
    goto not_enough_data;

  /* parse stream object */
  stream = gst_asf_demux_parse_stream_object (demux, stream_obj_data, len);
  g_free (stream_obj_data);

done:

  if (stream) {
    stream->ext_props = esp;

    /* try to set the framerate */
    if (stream->is_video && stream->caps) {
      GValue framerate = { 0 };
      GstStructure *s;
      gint num, denom;

      g_value_init (&framerate, GST_TYPE_FRACTION);

      num = GST_SECOND / 100;
      denom = esp.avg_time_per_frame;
      if (denom == 0) {
        /* avoid division by 0, assume 25/1 framerate */
        denom = GST_SECOND / 2500;
      }

      gst_value_set_fraction (&framerate, num, denom);

      stream->caps = gst_caps_make_writable (stream->caps);
      s = gst_caps_get_structure (stream->caps, 0);
      gst_structure_set_value (s, "framerate", &framerate);
      g_value_unset (&framerate);
      GST_DEBUG_OBJECT (demux, "setting framerate of %d/%d = %f",
          num, denom, ((gdouble) num) / denom);
    }

    /* add language info now if we have it */
    if (stream->ext_props.lang_idx < demux->num_languages) {
      if (stream->pending_tags == NULL)
        stream->pending_tags = gst_tag_list_new ();
      GST_LOG_OBJECT (demux, "stream %u has language '%s'", stream->id,
          demux->languages[stream->ext_props.lang_idx]);
      gst_tag_list_add (stream->pending_tags, GST_TAG_MERGE_APPEND,
          GST_TAG_LANGUAGE_CODE, demux->languages[stream->ext_props.lang_idx],
          NULL);
    }
  } else {
    GST_WARNING_OBJECT (demux, "Ext. stream properties for unknown stream");
  }

  return GST_FLOW_OK;

  /* Errors */
not_enough_data:
  {
    GST_WARNING_OBJECT (demux, "short read parsing ext stream props object!");
    return GST_FLOW_OK;         /* not absolutely fatal */
  }
expected_stream_object:
  {
    GST_WARNING_OBJECT (demux, "error parsing extended stream properties "
        "object: expected embedded stream object, but got %s object instead!",
        gst_asf_get_guid_nick (asf_object_guids, stream_obj.id));
    return GST_FLOW_OK;         /* not absolutely fatal */
  }
}

static const gchar *
gst_asf_demux_push_obj (GstASFDemux * demux, guint32 obj_id)
{
  const gchar *nick;

  nick = gst_asf_get_guid_nick (asf_object_guids, obj_id);
  if (g_str_has_prefix (nick, "ASF_OBJ_"))
    nick += strlen ("ASF_OBJ_");

  if (demux->objpath == NULL) {
    demux->objpath = g_strdup (nick);
  } else {
    gchar *newpath;

    newpath = g_strdup_printf ("%s/%s", demux->objpath, nick);
    g_free (demux->objpath);
    demux->objpath = newpath;
  }

  return (const gchar *) demux->objpath;
}

static void
gst_asf_demux_pop_obj (GstASFDemux * demux)
{
  gchar *s;

  if ((s = g_strrstr (demux->objpath, "/"))) {
    *s = '\0';
  } else {
    g_free (demux->objpath);
    demux->objpath = NULL;
  }
}

static void
gst_asf_demux_process_queued_extended_stream_objects (GstASFDemux * demux)
{
  GSList *l;
  guint i;

  /* Parse the queued extended stream property objects and add the info
   * to the existing streams or add the new embedded streams, but without
   * activating them yet */
  GST_LOG_OBJECT (demux, "%u queued extended stream properties objects",
      g_slist_length (demux->ext_stream_props));

  for (l = demux->ext_stream_props, i = 0; l != NULL; l = l->next, ++i) {
    GstBuffer *buf = GST_BUFFER (l->data);

    GST_LOG_OBJECT (demux, "parsing ext. stream properties object #%u", i);
    gst_asf_demux_process_ext_stream_props (demux, GST_BUFFER_DATA (buf),
        GST_BUFFER_SIZE (buf));
    gst_buffer_unref (buf);
  }
  g_slist_free (demux->ext_stream_props);
  demux->ext_stream_props = NULL;
}

static void
gst_asf_demux_activate_ext_props_streams (GstASFDemux * demux)
{
  guint i, j;

  for (i = 0; i < demux->num_streams; ++i) {
    AsfStream *stream;
    gboolean is_hidden;
    GSList *x;

    stream = &demux->stream[i];

    GST_LOG_OBJECT (demux, "checking  stream %2u", stream->id);

    if (stream->active) {
      GST_LOG_OBJECT (demux, "stream %2u is already activated", stream->id);
      continue;
    }

    is_hidden = FALSE;
    for (x = demux->mut_ex_streams; x != NULL; x = x->next) {
      guint8 *mes;

      /* check for each mutual exclusion whether it affects this stream */
      for (mes = (guint8 *) x->data; mes != NULL && *mes != 0xff; ++mes) {
        if (*mes == stream->id) {
          /* if yes, check if we've already added streams that are mutually
           * exclusive with the stream we're about to add */
          for (mes = (guint8 *) x->data; mes != NULL && *mes != 0xff; ++mes) {
            for (j = 0; j < demux->num_streams; ++j) {
              /* if the broadcast flag is set, assume the hidden streams aren't
               * actually streamed and hide them (or playbin won't work right),
               * otherwise assume their data is available */
              if (demux->stream[j].id == *mes && demux->broadcast) {
                is_hidden = TRUE;
                GST_LOG_OBJECT (demux, "broadcast stream ID %d to be added is "
                    "mutually exclusive with already existing stream ID %d, "
                    "hiding stream", stream->id, demux->stream[j].id);
                goto next;
              }
            }
          }
          break;
        }
      }
    }

  next:

    /* FIXME: we should do stream activation based on preroll data in
     * streaming mode too */
    if (demux->streaming && !is_hidden)
      gst_asf_demux_activate_stream (demux, stream);
  }
}

static GstFlowReturn
gst_asf_demux_process_object (GstASFDemux * demux, guint8 ** p_data,
    guint64 * p_size)
{
  GstFlowReturn ret = GST_FLOW_OK;
  AsfObject obj;
  guint64 obj_data_size;

  if (*p_size < ASF_OBJECT_HEADER_SIZE)
    return ASF_FLOW_NEED_MORE_DATA;

  asf_demux_peek_object (demux, *p_data, ASF_OBJECT_HEADER_SIZE, &obj);
  gst_asf_demux_skip_bytes (ASF_OBJECT_HEADER_SIZE, p_data, p_size);

  obj_data_size = obj.size - ASF_OBJECT_HEADER_SIZE;

  if (*p_size < obj_data_size)
    return ASF_FLOW_NEED_MORE_DATA;

  gst_asf_demux_push_obj (demux, obj.id);

  GST_INFO ("%s: size %" G_GUINT64_FORMAT, demux->objpath, obj.size);

  switch (obj.id) {
    case ASF_OBJ_STREAM:{
      AsfStream *stream;

      stream =
          gst_asf_demux_parse_stream_object (demux, *p_data, obj_data_size);

      /* FIXME: we should do stream activation based on preroll data in
       * streaming mode too */
      if (demux->streaming && stream != NULL)
        gst_asf_demux_activate_stream (demux, stream);

      ret = GST_FLOW_OK;
      break;
    }
    case ASF_OBJ_FILE:
      ret = gst_asf_demux_process_file (demux, *p_data, obj_data_size);
      break;
    case ASF_OBJ_HEADER:
      ret = gst_asf_demux_process_header (demux, *p_data, obj_data_size);
      break;
    case ASF_OBJ_COMMENT:
      ret = gst_asf_demux_process_comment (demux, *p_data, obj_data_size);
      break;
    case ASF_OBJ_HEAD1:
      ret = gst_asf_demux_process_header_ext (demux, *p_data, obj_data_size);
      break;
    case ASF_OBJ_BITRATE_PROPS:
      ret =
          gst_asf_demux_process_bitrate_props_object (demux, *p_data,
          obj_data_size);
      break;
    case ASF_OBJ_EXT_CONTENT_DESC:
      ret =
          gst_asf_demux_process_ext_content_desc (demux, *p_data,
          obj_data_size);
      break;
    case ASF_OBJ_METADATA_OBJECT:
      ret = gst_asf_demux_process_metadata (demux, *p_data, obj_data_size);
      break;
    case ASF_OBJ_EXTENDED_STREAM_PROPS:{
      GstBuffer *buf;

      /* process these later, we might not have parsed the corresponding
       * stream object yet */
      GST_LOG ("%s: queued for later parsing", demux->objpath);
      buf = gst_buffer_new_and_alloc (obj_data_size);
      memcpy (GST_BUFFER_DATA (buf), *p_data, obj_data_size);
      demux->ext_stream_props = g_slist_append (demux->ext_stream_props, buf);
      ret = GST_FLOW_OK;
      break;
    }
    case ASF_OBJ_LANGUAGE_LIST:
      ret = gst_asf_demux_process_language_list (demux, *p_data, obj_data_size);
      break;
    case ASF_OBJ_ADVANCED_MUTUAL_EXCLUSION:
      ret = gst_asf_demux_process_advanced_mutual_exclusion (demux, *p_data,
          obj_data_size);
      break;
    case ASF_OBJ_SIMPLE_INDEX:
      ret = gst_asf_demux_process_simple_index (demux, *p_data, obj_data_size);
      break;
    case ASF_OBJ_CONTENT_ENCRYPTION:
    case ASF_OBJ_EXT_CONTENT_ENCRYPTION:
    case ASF_OBJ_DIGITAL_SIGNATURE_OBJECT:
      goto error_encrypted;
    case ASF_OBJ_CONCEAL_NONE:
    case ASF_OBJ_HEAD2:
    case ASF_OBJ_UNDEFINED:
    case ASF_OBJ_CODEC_COMMENT:
    case ASF_OBJ_INDEX:
    case ASF_OBJ_PADDING:
    case ASF_OBJ_BITRATE_MUTEX:
    case ASF_OBJ_COMPATIBILITY:
    case ASF_OBJ_INDEX_PLACEHOLDER:
    case ASF_OBJ_INDEX_PARAMETERS:
    case ASF_OBJ_STREAM_PRIORITIZATION:
    case ASF_OBJ_SCRIPT_COMMAND:
    default:
      /* Unknown/unhandled object, skip it and hope for the best */
      GST_INFO ("%s: skipping object", demux->objpath);
      ret = GST_FLOW_OK;
      break;
  }

  /* this can't fail, we checked the number of bytes available before */
  gst_asf_demux_skip_bytes (obj_data_size, p_data, p_size);

  GST_LOG ("%s: ret = %s", demux->objpath, gst_asf_get_flow_name (ret));

  gst_asf_demux_pop_obj (demux);

  return ret;

/* ERRORS */
error_encrypted:
  {
    GST_ELEMENT_ERROR (demux, STREAM, DECRYPT, (NULL), (NULL));
    return GST_FLOW_ERROR;
  }
}

static void
gst_asf_demux_descramble_buffer (GstASFDemux * demux, AsfStream * stream,
    GstBuffer ** p_buffer)
{
  GstBuffer *descrambled_buffer;
  GstBuffer *scrambled_buffer;
  GstBuffer *sub_buffer;
  guint offset;
  guint off;
  guint row;
  guint col;
  guint idx;

  /* descrambled_buffer is initialised in the first iteration */
  descrambled_buffer = NULL;
  scrambled_buffer = *p_buffer;

  if (GST_BUFFER_SIZE (scrambled_buffer) < demux->ds_packet_size * demux->span)
    return;

  for (offset = 0; offset < GST_BUFFER_SIZE (scrambled_buffer);
      offset += demux->ds_chunk_size) {
    off = offset / demux->ds_chunk_size;
    row = off / demux->span;
    col = off % demux->span;
    idx = row + col * demux->ds_packet_size / demux->ds_chunk_size;
    GST_DEBUG ("idx=%u, row=%u, col=%u, off=%u, ds_chunk_size=%u", idx, row,
        col, off, demux->ds_chunk_size);
    GST_DEBUG ("scrambled buffer size=%u, span=%u, packet_size=%u",
        GST_BUFFER_SIZE (scrambled_buffer), demux->span, demux->ds_packet_size);
    GST_DEBUG ("GST_BUFFER_SIZE (scrambled_buffer) = %u",
        GST_BUFFER_SIZE (scrambled_buffer));
    sub_buffer =
        gst_buffer_create_sub (scrambled_buffer, idx * demux->ds_chunk_size,
        demux->ds_chunk_size);
    if (!offset) {
      descrambled_buffer = sub_buffer;
    } else {
      descrambled_buffer = gst_buffer_join (descrambled_buffer, sub_buffer);
    }
  }

  gst_buffer_copy_metadata (descrambled_buffer, scrambled_buffer,
      GST_BUFFER_COPY_TIMESTAMPS);

  /* FIXME/CHECK: do we need to transfer buffer flags here too? */

  gst_buffer_unref (scrambled_buffer);
  *p_buffer = descrambled_buffer;
}

static gboolean
gst_asf_demux_element_send_event (GstElement * element, GstEvent * event)
{
  GstASFDemux *demux = GST_ASF_DEMUX (element);
  gint i;

  GST_DEBUG ("handling element event of type %s", GST_EVENT_TYPE_NAME (event));

  for (i = 0; i < demux->num_streams; ++i) {
    gst_event_ref (event);
    if (gst_asf_demux_handle_src_event (demux->stream[i].pad, event)) {
      gst_event_unref (event);
      return TRUE;
    }
  }

  gst_event_unref (event);
  return FALSE;
}

/* takes ownership of the passed event */
static gboolean
gst_asf_demux_send_event_unlocked (GstASFDemux * demux, GstEvent * event)
{
  gboolean ret = TRUE;
  gint i;

  GST_DEBUG_OBJECT (demux, "sending %s event to all source pads",
      GST_EVENT_TYPE_NAME (event));

  for (i = 0; i < demux->num_streams; ++i) {
    gst_event_ref (event);
    ret &= gst_pad_push_event (demux->stream[i].pad, event);
  }
  gst_event_unref (event);
  return ret;
}

static GstFlowReturn
gst_asf_demux_push_buffer (GstASFDemux * demux, AsfStream * stream,
    GstBuffer * buf)
{
  buf = gst_buffer_make_metadata_writable (buf);

  /* need to send tags? */
  if (stream->pending_tags) {
    GST_LOG_OBJECT (stream->pad, "tags %" GST_PTR_FORMAT, stream->pending_tags);
    gst_element_found_tags_for_pad (GST_ELEMENT (demux), stream->pad,
        stream->pending_tags);
    stream->pending_tags = NULL;
  }

  /* don't set the same time stamp on multiple consecutive outgoing
   * video buffers, set it on the first one and set NONE on the others,
   * it's the decoder's job to fill the missing bits properly */
  if (stream->is_video && GST_BUFFER_TIMESTAMP_IS_VALID (buf) &&
      GST_BUFFER_TIMESTAMP (buf) == stream->last_buffer_timestamp) {
    GST_BUFFER_TIMESTAMP (buf) = GST_CLOCK_TIME_NONE;
  }

  /* make sure segment.last_stop is continually increasing */
  if (GST_BUFFER_TIMESTAMP_IS_VALID (buf) &&
      demux->segment.last_stop < (gint64) GST_BUFFER_TIMESTAMP (buf)) {
    gst_segment_set_last_stop (&demux->segment, GST_FORMAT_TIME,
        GST_BUFFER_TIMESTAMP (buf));
  }

  if (GST_BUFFER_TIMESTAMP_IS_VALID (buf))
    stream->last_buffer_timestamp = GST_BUFFER_TIMESTAMP (buf);

  gst_buffer_set_caps (buf, stream->caps);

  GST_LOG_OBJECT (stream->pad, "pushing buffer, ts=%" GST_TIME_FORMAT " %s",
      GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (buf)),
      (stream->discont) ? "discont" : "");

  if (stream->discont) {
    GST_BUFFER_FLAG_SET (buf, GST_BUFFER_FLAG_DISCONT);
    stream->discont = FALSE;
  }

  stream->last_flow = gst_pad_push (stream->pad, buf);

  return stream->last_flow;
}

static GstFlowReturn
gst_asf_demux_process_chunk (GstASFDemux * demux,
    asf_packet_info * packet_info, asf_segment_info * segment_info,
    guint8 ** p_data, guint64 * p_size)
{
  GstFlowReturn ret = GST_FLOW_OK;
  AsfStream *stream;
  GstBuffer *buffer;

  stream = gst_asf_demux_get_stream (demux, segment_info->stream_number);
  if (stream == NULL) {
    GST_WARNING ("invalid stream number %d", segment_info->stream_number);
    if (!gst_asf_demux_skip_bytes (segment_info->chunk_size, p_data, p_size))
      ret = ASF_FLOW_NEED_MORE_DATA;
    goto done;
  }

  GST_DEBUG ("Processing %s chunk of size %u (frag_offset=%d)",
      GST_PAD_NAME (stream->pad), segment_info->chunk_size,
      stream->frag_offset);

  if (segment_info->frag_offset == 0) {
    /* new packet */
    stream->sequence = segment_info->sequence;
    if (!GST_CLOCK_TIME_IS_VALID (stream->first_pts))
      stream->first_pts = segment_info->frag_timestamp - demux->preroll;
    demux->pts =
        segment_info->frag_timestamp - demux->preroll - stream->first_pts;

    /*
       if (stream->is_video) {
       GST_DEBUG ("%s: demux->pts=%lld (frag_timestamp=%ld, preroll=%lld)",
       GST_PAD_NAME (stream->pad), demux->pts,
       segment_info->frag_timestamp, demux->preroll);
       }
     */

    if (!gst_asf_demux_get_buffer (&buffer, segment_info->chunk_size,
            p_data, p_size)) {
      return ASF_FLOW_NEED_MORE_DATA;
    }

    GST_DEBUG ("BUFFER: Copied stream to buffer %p", buffer);
    stream->payload = buffer;
  } else {
    GST_DEBUG ("segment_info->sequence=%d, stream->sequence=%d,"
        " segment_info->frag_offset=%d, stream->frag_offset=%d",
        segment_info->sequence, stream->sequence, segment_info->frag_offset,
        stream->frag_offset);

    if (segment_info->sequence == stream->sequence &&
        segment_info->frag_offset == stream->frag_offset) {
      GstBuffer *new_buffer;

      /* continuing packet */
      GST_INFO ("continuation packet");

      if (!gst_asf_demux_get_buffer (&buffer, segment_info->chunk_size,
              p_data, p_size)) {
        return ASF_FLOW_NEED_MORE_DATA;
      }

      GST_DEBUG ("copied stream to buffer %p", buffer);

      new_buffer = gst_buffer_merge (stream->payload, buffer);
      GST_DEBUG_OBJECT (demux,
          "BUFFER: Merged new_buffer (%p - %d) from stream->payload (%p - %d)"
          " and buffer (%p - %d)", new_buffer,
          GST_MINI_OBJECT_REFCOUNT_VALUE (new_buffer), stream->payload,
          GST_MINI_OBJECT_REFCOUNT_VALUE (stream->payload), buffer,
          GST_MINI_OBJECT_REFCOUNT_VALUE (buffer));
      gst_buffer_unref (stream->payload);
      gst_buffer_unref (buffer);
      stream->payload = new_buffer;
    } else {
      /* cannot continue current packet: free it */
      if (stream->frag_offset != 0) {
        /* cannot create new packet */
        GST_DEBUG ("BUFFER: Freeing stream->payload (%p)", stream->payload);
        gst_buffer_unref (stream->payload);
#if 0
        /* FIXME: is this right/needed? we already do that below, no? */
        packet_info->size_left -= segment_info->chunk_size;
#endif
        stream->frag_offset = 0;
      }
      demux->pts =
          segment_info->frag_timestamp - demux->preroll - stream->first_pts;

      /*
         if (stream->is_video) {
         GST_DEBUG ("%s: demux->pts=%lld (frag_timestamp=%ld, preroll=%lld)",
         GST_PAD_NAME (stream->pad), demux->pts,
         segment_info->frag_timestamp, demux->preroll);
         }
       */

      goto done;
#if 0
      /* FIXME: where did this come from / fit in ? */
      return TRUE;
      else {
        /* create new packet */
        stream->sequence = segment_info->sequence;
      }
#endif
    }
  }

  stream->frag_offset += segment_info->chunk_size;

  GST_DEBUG ("frag_offset = %d  segment_size = %d ", stream->frag_offset,
      segment_info->segment_size);

  if (stream->frag_offset < segment_info->segment_size) {
    /* We don't have the whole packet yet */
  } else {
    /* We have the whole packet now so we should push the packet to
       the src pad now. First though we should check if we need to do
       descrambling */
    if (demux->span > 1) {
      gst_asf_demux_descramble_buffer (demux, stream, &stream->payload);
    }

    if (stream->is_video) {
      GST_DEBUG ("%s: demux->pts=%lld=%" GST_TIME_FORMAT
          ", stream->last_pts=%lld=%" GST_TIME_FORMAT,
          GST_PAD_NAME (stream->pad), demux->pts,
          GST_TIME_ARGS (GST_MSECOND * demux->pts), stream->last_pts,
          GST_TIME_ARGS (GST_MSECOND * stream->last_pts));
    }

    /* FIXME: last_pts is not a GstClockTime and not in nanoseconds, so
     * this is not really 100% right ... */
    if (demux->pts >= stream->last_pts ||
        !GST_CLOCK_TIME_IS_VALID (stream->last_pts)) {
      stream->last_pts = demux->pts;
    }

    GST_BUFFER_TIMESTAMP (stream->payload) = GST_MSECOND * stream->last_pts;

    GST_DEBUG ("sending stream %d of size %d, ts=%" GST_TIME_FORMAT,
        stream->id, segment_info->chunk_size,
        GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (stream->payload)));

    if (!stream->fps_known) {
      if (!stream->cache) {
        stream->cache = stream->payload;
      } else {
        gdouble fps;
        gint64 diff;
        gint num, denom;

        /* why is all this needed anyway? (tpm) */
        diff = GST_BUFFER_TIMESTAMP (stream->payload) -
            GST_BUFFER_TIMESTAMP (stream->cache);

        fps = (gdouble) GST_SECOND / diff;

        /* artificial cap */
        if (fps >= 50.0) {
          num = 50;
          denom = 1;
        } else if (fps <= 5.0) {
          num = 5;
          denom = 1;
        } else {
          /* crack alert */
          num = (gint) GST_SECOND;
          while (diff > G_MAXINT) {
            num = num >> 1;
            diff = diff >> 1;
          }
          denom = (gint) diff;
        }
        stream->fps_known = TRUE;
        stream->caps = gst_caps_make_writable (stream->caps);
        gst_caps_set_simple (stream->caps,
            "framerate", GST_TYPE_FRACTION, num, denom, NULL);
        GST_DEBUG ("set up stream with fps %d/%d", num, denom);
        gst_pad_use_fixed_caps (stream->pad);
        gst_pad_set_caps (stream->pad, stream->caps);

        ret = gst_asf_demux_push_buffer (demux, stream, stream->cache);
        stream->cache = NULL;

        ret = gst_asf_demux_push_buffer (demux, stream, stream->payload);
        stream->payload = NULL;
      }
    } else {
      ret = gst_asf_demux_push_buffer (demux, stream, stream->payload);
      stream->payload = NULL;
    }

    stream->frag_offset = 0;
  }

done:

  packet_info->size_left -= segment_info->chunk_size;

  return ret;
}

static GstFlowReturn
gst_asf_demux_process_segment (GstASFDemux * demux,
    asf_packet_info * packet_info, guint8 ** p_data, guint64 * p_size)
{
  GstFlowReturn ret = GST_FLOW_OK;
  asf_segment_info segment_info;
  gboolean has_key_frame;
  guint64 start_size;
  guint32 replic_size;
  guint32 time_start;
  guint32 frag_size;
  guint32 rsize;
  guint8 time_delta;
  guint8 byte;

  start_size = *p_size;

  if (*p_size < 1)
    return ASF_FLOW_NEED_MORE_DATA;

  byte = gst_asf_demux_get_uint8 (p_data, p_size);
  segment_info.stream_number = byte & 0x7f;
  has_key_frame = ((byte & 0x80) == 0x80);      /* FIXME: use this somewhere? */

  GST_INFO ("processing segment for stream %u%s", segment_info.stream_number,
      (has_key_frame) ? " (has keyframe)" : "");

  /* FIXME: check (doesn't work) */
#if 0
  {
    AsfStream *stream;

    stream = gst_asf_demux_get_stream (demux, segment_info.stream_number);
    if (stream && stream->last_pts == GST_CLOCK_TIME_NONE &&
        stream->is_video && !has_key_frame) {
      g_print ("skipping segment, waiting for a key unit\n");
      if (!gst_asf_demux_skip_bytes (segment_info.segment_size - 1, p_data,
              p_size))
        return ASF_FLOW_NEED_MORE_DATA;
      packet_info->size_left -= segment_info.segment_size;
      return GST_FLOW_OK;
    }
  }
#endif

  {
    const guint lengths[4] = { 0, 1, 2, 4 };
    guint needed;

    needed = lengths[packet_info->seqtype]
        + lengths[packet_info->fragoffsettype]
        + lengths[packet_info->replicsizetype];

    if (*p_size < needed)
      return ASF_FLOW_NEED_MORE_DATA;
  }

  segment_info.sequence =
      gst_asf_demux_get_var_length (packet_info->seqtype, p_data, p_size);
  segment_info.frag_offset =
      gst_asf_demux_get_var_length (packet_info->fragoffsettype, p_data,
      p_size);
  replic_size =
      gst_asf_demux_get_var_length (packet_info->replicsizetype, p_data,
      p_size);

  GST_DEBUG ("sequence=%u, frag_offset=%u, replic_size=%u",
      segment_info.sequence, segment_info.frag_offset, replic_size);

  if (replic_size > 1) {
    segment_info.compressed = FALSE;

    /* It's uncompressed with replic data */
    if (*p_size < (4 + 4))
      goto short_repdata;

    segment_info.segment_size = gst_asf_demux_get_uint32 (p_data, p_size);
    segment_info.frag_timestamp = gst_asf_demux_get_uint32 (p_data, p_size);

    GST_LOG ("frag_timestamp: %" GST_TIME_FORMAT,
        GST_TIME_ARGS (segment_info.frag_timestamp * GST_MSECOND));

    if (replic_size > 8) {
      if (!gst_asf_demux_skip_bytes ((replic_size - 8), p_data, p_size))
        return ASF_FLOW_NEED_MORE_DATA;
    }
  } else if (replic_size == 1) {
    /* It's compressed */
    segment_info.compressed = TRUE;
    if (*p_size < 1)
      return ASF_FLOW_NEED_MORE_DATA;
    time_delta = gst_asf_demux_get_uint8 (p_data, p_size);
    GST_DEBUG ("time_delta = %u", time_delta);

    time_start = segment_info.frag_offset;
    segment_info.frag_offset = 0;
    segment_info.frag_timestamp = time_start;   /* was: demux->timestamp */
  } else {
    segment_info.compressed = FALSE;

    time_start = segment_info.frag_offset;
    segment_info.frag_offset = 0;
    segment_info.frag_timestamp = time_start;   /* was: demux->timestamp */
  }

  GST_DEBUG ("multiple = %u, compressed = %u",
      packet_info->multiple, segment_info.compressed);

  if (packet_info->multiple) {
    const guint lengths[4] = { 0, 1, 2, 4 };

    if (*p_size < lengths[packet_info->segsizetype])
      return ASF_FLOW_NEED_MORE_DATA;

    frag_size = gst_asf_demux_get_var_length (packet_info->segsizetype,
        p_data, p_size);
  } else {
    frag_size = packet_info->size_left - (start_size - *p_size);
  }

  rsize = start_size - *p_size;

  packet_info->size_left -= rsize;

  GST_DEBUG ("size left = %u, frag size = %u, rsize = %u",
      packet_info->size_left, frag_size, rsize);

  if (segment_info.compressed) {
    while (frag_size > 0) {
      if (*p_size < 1)
        return ASF_FLOW_NEED_MORE_DATA;
      byte = gst_asf_demux_get_uint8 (p_data, p_size);
      packet_info->size_left--;
      segment_info.chunk_size = byte;
      segment_info.segment_size = segment_info.chunk_size;

      if (segment_info.chunk_size > packet_info->size_left) {
        return ASF_FLOW_NEED_MORE_DATA;
        /* or is this an error?
         *   GST_ELEMENT_ERROR (demux, STREAM, DEMUX, (NULL),
         *        ("Payload chunk overruns packet size."));
         *    return GST_FLOW_ERROR; */
      }

      ret = gst_asf_demux_process_chunk (demux, packet_info, &segment_info,
          p_data, p_size);

      if (ret != GST_FLOW_OK)
        break;

      if (segment_info.chunk_size < frag_size)
        frag_size -= segment_info.chunk_size + 1;
      else {
/*
        GST_ELEMENT_ERROR (demux, STREAM, DEMUX,
            ("Invalid data in stream"),
            ("Invalid fragment size indicator in segment"));
        ret = GST_FLOW_ERROR;
*/
        return ASF_FLOW_NEED_MORE_DATA;
      }
    }
  } else {
    segment_info.chunk_size = frag_size;
    ret = gst_asf_demux_process_chunk (demux, packet_info, &segment_info,
        p_data, p_size);
  }

  return ret;

/* ERRORS */
short_repdata:
  {
    if (replic_size < 8) {
      GST_ELEMENT_ERROR (demux, STREAM, DEMUX, (NULL),
          ("payload has replicated data, but size is less than 8"));
    } else {
      GST_ELEMENT_ERROR (demux, STREAM, DEMUX, (NULL),
          ("payload has %u bytes of replicated data, but size is only %u",
              replic_size, (guint) * p_size));
    }
    return GST_FLOW_ERROR;
  }
}

static GstFlowReturn
gst_asf_demux_handle_data (GstASFDemux * demux, guint8 ** p_data,
    guint64 * p_size)
{
  asf_packet_info packet_info;
  gboolean correction;
  guint64 start_size;
  guint32 sequence;
  guint32 packet_length;
  guint32 rsize;
  guint16 duration;
  guint8 num_segments;
  guint8 segment;
  guint8 flags;
  guint8 property;

  start_size = *p_size;

  GST_LOG ("processing packet %" G_GUINT64_FORMAT "/%" G_GUINT64_FORMAT,
      demux->packet + 1, demux->num_packets);

  if (demux->num_packets > 0 && demux->packet >= demux->num_packets) {
    GST_LOG_OBJECT (demux, "reached EOS");
    return GST_FLOW_UNEXPECTED;
  }

  ++demux->packet;

  if (*p_size < 1) {
    GST_WARNING ("unexpected end of data");     /* unexpected, why? */
    return ASF_FLOW_NEED_MORE_DATA;
  }

  correction = ((gst_asf_demux_get_uint8 (p_data, p_size) & 0x80) == 0x80);

  /* Uses error correction? */
  if (correction) {
    asf_obj_data_correction corr_obj;

    GST_DEBUG ("data has error correction");
    if (!gst_asf_demux_get_obj_data_correction (&corr_obj, p_data, p_size)) {
      GST_WARNING ("unexpected end of data");
      return ASF_FLOW_NEED_MORE_DATA;
    }
  }

  /* Read the packet flags */
  if (*p_size < (1 + 1)) {
    GST_WARNING ("unexpected end of data");
    return ASF_FLOW_NEED_MORE_DATA;
  }
  flags = gst_asf_demux_get_uint8 (p_data, p_size);
  property = gst_asf_demux_get_uint8 (p_data, p_size);

  packet_info.multiple = ((flags & 0x01) == 0x01);

  {
    const guint lengths[4] = { 0, 1, 2, 4 };
    guint needed;

    needed = lengths[(flags >> 5) & 0x03]
        + lengths[(flags >> 3) & 0x03]
        + lengths[(flags >> 1) & 0x03];

    if (*p_size < needed)
      return ASF_FLOW_NEED_MORE_DATA;
  }

  packet_length =
      gst_asf_demux_get_var_length ((flags >> 5) & 0x03, p_data, p_size);

  sequence = gst_asf_demux_get_var_length ((flags >> 1) & 0x03, p_data, p_size);

  packet_info.padsize =
      gst_asf_demux_get_var_length ((flags >> 3) & 0x03, p_data, p_size);

  if (packet_length == 0)
    packet_length = demux->packet_size;

  GST_DEBUG ("multiple = %u, sequence = %u, padsize = %u, "
      "packet length = %u", packet_info.multiple, sequence,
      packet_info.padsize, packet_length);

  /* Read the property flags */
  packet_info.replicsizetype = property & 0x03;
  packet_info.fragoffsettype = (property >> 2) & 0x03;
  packet_info.seqtype = (property >> 4) & 0x03;

  if (*p_size < (4 + 2)) {
    GST_WARNING ("unexpected end of data");
    return ASF_FLOW_NEED_MORE_DATA;
  }

  /* FIXME: isn't this the send time, ie. not the presentation time? (tpm) */
  demux->timestamp = gst_asf_demux_get_uint32 (p_data, p_size);
  duration = gst_asf_demux_get_uint16 (p_data, p_size);

  GST_DEBUG ("timestamp = %" GST_TIME_FORMAT ", duration = %" GST_TIME_FORMAT,
      GST_TIME_ARGS ((gint64) demux->timestamp * GST_MSECOND),
      GST_TIME_ARGS ((gint64) duration * GST_MSECOND));

  /* Are there multiple payloads? */
  if (packet_info.multiple) {
    guint8 multi_flags;

    if (*p_size < 1)
      return ASF_FLOW_NEED_MORE_DATA;

    multi_flags = gst_asf_demux_get_uint8 (p_data, p_size);
    packet_info.segsizetype = (multi_flags >> 6) & 0x03;
    num_segments = multi_flags & 0x3f;
  } else {
    packet_info.segsizetype = 2;
    num_segments = 1;
  }

  rsize = start_size - *p_size;

  packet_info.size_left = packet_length - packet_info.padsize - rsize;

  GST_DEBUG ("rsize: %u, size left: %u", rsize, packet_info.size_left);

  for (segment = 0; segment < num_segments; ++segment) {
    GstFlowReturn ret;

    ret = gst_asf_demux_process_segment (demux, &packet_info, p_data, p_size);
    if (ret != GST_FLOW_OK) {
      GST_DEBUG ("process_segment %u returned %s", segment,
          gst_asf_get_flow_name (ret));
    }
  }

  /* Skip the padding */
  if (packet_info.padsize > 0) {
    if (*p_size < packet_info.padsize) {
      GST_WARNING ("unexpected end of data");
      return ASF_FLOW_NEED_MORE_DATA;
    }

    if (!gst_asf_demux_skip_bytes (packet_info.padsize, p_data, p_size))
      return ASF_FLOW_NEED_MORE_DATA;
  }

  GST_DEBUG ("remaining size left: %u", packet_info.size_left);

  /* FIXME: this doesn't really make sense, does it? if we don't have enough
   * bytes left to skip the stuff at the end and we've already sent out
   * buffers, just returning NEED_MORE_DATA isn't really right. Should we
   * just throw an error in that case (can it happen with a non-broken
   * stream?) */
  if (packet_info.size_left > 0) {
    if (!gst_asf_demux_skip_bytes (packet_info.size_left, p_data, p_size)) {
      GST_WARNING
          ("unexpected end of data, *p_size=%lld,packet_info.size_left=%u",
          *p_size, packet_info.size_left);
      return ASF_FLOW_NEED_MORE_DATA;
    }
  }

  return gst_asf_demux_aggregate_flow_return (demux);
}

static const GstQueryType *
gst_asf_demux_get_src_query_types (GstPad * pad)
{
  static const GstQueryType types[] = {
    GST_QUERY_POSITION,
    GST_QUERY_DURATION,
    GST_QUERY_SEEKING,
    0
  };

  return types;
}

static gboolean
gst_asf_demux_handle_src_query (GstPad * pad, GstQuery * query)
{
  GstASFDemux *demux;
  gboolean res = FALSE;

  demux = GST_ASF_DEMUX (gst_pad_get_parent (pad));

  GST_DEBUG ("handling %s query",
      gst_query_type_get_name (GST_QUERY_TYPE (query)));

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_DURATION:
    {
      GstFormat format;

      gst_query_parse_duration (query, &format, NULL);

      if (format != GST_FORMAT_TIME) {
        GST_LOG ("only support duration queries in TIME format");
        break;
      }

      GST_OBJECT_LOCK (demux);

      if (demux->segment.duration != GST_CLOCK_TIME_NONE) {
        GST_LOG ("returning duration: %" GST_TIME_FORMAT,
            GST_TIME_ARGS (demux->segment.duration));

        gst_query_set_duration (query, GST_FORMAT_TIME,
            demux->segment.duration);

        res = TRUE;
      } else {
        GST_LOG ("duration not known yet");
      }

      GST_OBJECT_UNLOCK (demux);
      break;
    }

    case GST_QUERY_POSITION:{
      GstFormat format;

      gst_query_parse_position (query, &format, NULL);

      if (format != GST_FORMAT_TIME) {
        GST_LOG ("only support position queries in TIME format");
        break;
      }

      GST_OBJECT_LOCK (demux);

      if (demux->segment.last_stop != GST_CLOCK_TIME_NONE) {
        GST_LOG ("returning position: %" GST_TIME_FORMAT,
            GST_TIME_ARGS (demux->segment.last_stop));

        gst_query_set_position (query, GST_FORMAT_TIME,
            demux->segment.last_stop);

        res = TRUE;
      } else {
        GST_LOG ("position not known yet");
      }

      GST_OBJECT_UNLOCK (demux);
      break;
    }

    case GST_QUERY_SEEKING:{
      GstFormat format;

      gst_query_parse_seeking (query, &format, NULL, NULL, NULL);
      if (format == GST_FORMAT_TIME) {
        gint64 duration;

        GST_OBJECT_LOCK (demux);
        duration = demux->segment.duration;
        GST_OBJECT_UNLOCK (demux);

        gst_query_set_seeking (query, GST_FORMAT_TIME, demux->seekable,
            0, duration);
        res = TRUE;
      }
      break;
    }

    default:
      res = gst_pad_query_default (pad, query);
      break;
  }

  gst_object_unref (demux);
  return res;
}

static GstStateChangeReturn
gst_asf_demux_change_state (GstElement * element, GstStateChange transition)
{
  GstASFDemux *demux = GST_ASF_DEMUX (element);
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:{
      gst_segment_init (&demux->segment, GST_FORMAT_TIME);
      demux->need_newsegment = TRUE;
      demux->segment_running = FALSE;
      demux->adapter = gst_adapter_new ();
      demux->metadata = gst_caps_new_empty ();
      demux->data_size = 0;
      demux->data_offset = 0;
      demux->index_offset = 0;
      break;
    }
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);
  if (ret == GST_STATE_CHANGE_FAILURE)
    return ret;

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
    case GST_STATE_CHANGE_READY_TO_NULL:
      gst_asf_demux_reset (demux);
      break;
    default:
      break;
  }

  return ret;
}
