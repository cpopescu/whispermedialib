/* GStreamer RealMedia demuxer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 * Copyright (C) <2003> David A. Schleef <ds@schleef.org>
 * Copyright (C) <2004> Stephane Loeuillet <gstreamer@leroutier.net>
 * Copyright (C) <2005> Owen Fraser-Green <owen@discobabe.net>
 * Copyright (C) <2005> Michael Smith <fluendo.com>
 * Copyright (C) <2006> Wim Taymans <wim@fluendo.com>
 * Copyright (C) <2006> Tim-Philipp Müller <tim centricular net>
 * Copyright (C) <2007> Wim Taymans <wim.taymans@gmail.com>
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

#include "rmdemux.h"
#include "rmutils.h"

#include <string.h>
#include <ctype.h>

#define RMDEMUX_GUINT32_GET(a)  GST_READ_UINT32_BE(a)
#define RMDEMUX_GUINT16_GET(a)  GST_READ_UINT16_BE(a)
#define RMDEMUX_FOURCC_GET(a)   GST_READ_UINT32_LE(a)
#define HEADER_SIZE 10
#define DATA_SIZE 8

#define MAX_FRAGS 256

static const guint8 sipr_subpk_size[4] = { 29, 19, 37, 20 };

typedef struct _GstRMDemuxIndex GstRMDemuxIndex;

struct _GstRMDemuxStream
{
  guint32 subtype;
  guint32 fourcc;
  guint32 subformat;
  guint32 format;

  int id;
  GstPad *pad;
  GstFlowReturn last_flow;
  gboolean discont;
  int timescale;

  int sample_index;
  GstRMDemuxIndex *index;
  int index_length;
  gint framerate_numerator;
  gint framerate_denominator;
  guint32 seek_offset;

  guint16 width;
  guint16 height;
  guint16 flavor;
  guint16 rate;                 /* samplerate         */
  guint16 n_channels;           /* channels           */
  guint16 sample_width;         /* bits_per_sample    */
  guint16 leaf_size;            /* subpacket_size     */
  guint32 packet_size;          /* coded_frame_size   */
  guint16 version;
  guint32 extra_data_size;      /* codec_data_length  */
  guint8 *extra_data;           /* extras             */

  gboolean needs_descrambling;
  guint subpackets_needed;      /* subpackets needed for descrambling    */
  GPtrArray *subpackets;        /* array containing subpacket GstBuffers */

  /* Variables needed for fixing timestamps. */
  GstClockTime next_ts, last_ts;
  guint16 next_seq, last_seq;

  gint frag_seqnum;
  gint frag_subseq;
  guint frag_length;
  guint frag_current;
  guint frag_count;
  guint frag_offset[MAX_FRAGS];
  GstAdapter *adapter;

  GstTagList *pending_tags;
};

struct _GstRMDemuxIndex
{
  guint32 offset;
  GstClockTime timestamp;
};

static GstStaticPadTemplate gst_rmdemux_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/vnd.rn-realmedia")
    );

static GstStaticPadTemplate gst_rmdemux_videosrc_template =
GST_STATIC_PAD_TEMPLATE ("video_%02d",
    GST_PAD_SRC,
    GST_PAD_SOMETIMES,
    GST_STATIC_CAPS_ANY);

static GstStaticPadTemplate gst_rmdemux_audiosrc_template =
GST_STATIC_PAD_TEMPLATE ("audio_%02d",
    GST_PAD_SRC,
    GST_PAD_SOMETIMES,
    GST_STATIC_CAPS_ANY);

GST_DEBUG_CATEGORY_STATIC (rmdemux_debug);
#define GST_CAT_DEFAULT rmdemux_debug

static GstElementClass *parent_class = NULL;

static void gst_rmdemux_class_init (GstRMDemuxClass * klass);
static void gst_rmdemux_base_init (GstRMDemuxClass * klass);
static void gst_rmdemux_init (GstRMDemux * rmdemux);
static void gst_rmdemux_finalize (GObject * object);
static GstStateChangeReturn gst_rmdemux_change_state (GstElement * element,
    GstStateChange transition);
static GstFlowReturn gst_rmdemux_chain (GstPad * pad, GstBuffer * buffer);
static void gst_rmdemux_loop (GstPad * pad);
static gboolean gst_rmdemux_sink_activate (GstPad * sinkpad);
static gboolean gst_rmdemux_sink_activate_push (GstPad * sinkpad,
    gboolean active);
static gboolean gst_rmdemux_sink_activate_pull (GstPad * sinkpad,
    gboolean active);
static gboolean gst_rmdemux_sink_event (GstPad * pad, GstEvent * event);
static gboolean gst_rmdemux_src_event (GstPad * pad, GstEvent * event);
static void gst_rmdemux_send_event (GstRMDemux * rmdemux, GstEvent * event);
static const GstQueryType *gst_rmdemux_src_query_types (GstPad * pad);
static gboolean gst_rmdemux_src_query (GstPad * pad, GstQuery * query);
static gboolean gst_rmdemux_perform_seek (GstRMDemux * rmdemux,
    GstEvent * event);

static void gst_rmdemux_parse__rmf (GstRMDemux * rmdemux, const guint8 * data,
    int length);
static void gst_rmdemux_parse_prop (GstRMDemux * rmdemux, const guint8 * data,
    int length);
static void gst_rmdemux_parse_mdpr (GstRMDemux * rmdemux,
    const guint8 * data, int length);
static guint gst_rmdemux_parse_indx (GstRMDemux * rmdemux, const guint8 * data,
    int length);
static void gst_rmdemux_parse_data (GstRMDemux * rmdemux, const guint8 * data,
    int length);
static void gst_rmdemux_parse_cont (GstRMDemux * rmdemux, const guint8 * data,
    int length);
static GstFlowReturn gst_rmdemux_parse_packet (GstRMDemux * rmdemux,
    GstBuffer * in, guint16 version);
static void gst_rmdemux_parse_indx_data (GstRMDemux * rmdemux,
    const guint8 * data, int length);
static void gst_rmdemux_stream_clear_cached_subpackets (GstRMDemux * rmdemux,
    GstRMDemuxStream * stream);
static GstRMDemuxStream *gst_rmdemux_get_stream_by_id (GstRMDemux * rmdemux,
    int id);

static GType
gst_rmdemux_get_type (void)
{
  static GType rmdemux_type = 0;

  if (!rmdemux_type) {
    static const GTypeInfo rmdemux_info = {
      sizeof (GstRMDemuxClass),
      (GBaseInitFunc) gst_rmdemux_base_init, NULL,
      (GClassInitFunc) gst_rmdemux_class_init,
      NULL, NULL, sizeof (GstRMDemux), 0,
      (GInstanceInitFunc) gst_rmdemux_init,
    };

    rmdemux_type =
        g_type_register_static (GST_TYPE_ELEMENT, "GstRMDemux", &rmdemux_info,
        0);
  }
  return rmdemux_type;
}

static void
gst_rmdemux_base_init (GstRMDemuxClass * klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_rmdemux_sink_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_rmdemux_videosrc_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_rmdemux_audiosrc_template));
  gst_element_class_set_details_simple (element_class, "RealMedia Demuxer",
      "Codec/Demuxer",
      "Demultiplex a RealMedia file into audio and video streams",
      "David Schleef <ds@schleef.org>");
}

static void
gst_rmdemux_class_init (GstRMDemuxClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  parent_class = g_type_class_peek_parent (klass);

  gstelement_class->change_state = GST_DEBUG_FUNCPTR (gst_rmdemux_change_state);

  GST_DEBUG_CATEGORY_INIT (rmdemux_debug, "rmdemux",
      0, "Demuxer for Realmedia streams");

  gobject_class->finalize = gst_rmdemux_finalize;
}

static void
gst_rmdemux_finalize (GObject * object)
{
  GstRMDemux *rmdemux = GST_RMDEMUX (object);

  if (rmdemux->adapter) {
    g_object_unref (rmdemux->adapter);
    rmdemux->adapter = NULL;
  }

  GST_CALL_PARENT (G_OBJECT_CLASS, finalize, (object));
}

static void
gst_rmdemux_init (GstRMDemux * rmdemux)
{
  rmdemux->sinkpad =
      gst_pad_new_from_static_template (&gst_rmdemux_sink_template, "sink");
  gst_pad_set_event_function (rmdemux->sinkpad,
      GST_DEBUG_FUNCPTR (gst_rmdemux_sink_event));
  gst_pad_set_chain_function (rmdemux->sinkpad,
      GST_DEBUG_FUNCPTR (gst_rmdemux_chain));
  gst_pad_set_activate_function (rmdemux->sinkpad,
      GST_DEBUG_FUNCPTR (gst_rmdemux_sink_activate));
  gst_pad_set_activatepull_function (rmdemux->sinkpad,
      GST_DEBUG_FUNCPTR (gst_rmdemux_sink_activate_pull));
  gst_pad_set_activatepush_function (rmdemux->sinkpad,
      GST_DEBUG_FUNCPTR (gst_rmdemux_sink_activate_push));

  gst_element_add_pad (GST_ELEMENT (rmdemux), rmdemux->sinkpad);

  rmdemux->adapter = gst_adapter_new ();
  rmdemux->first_ts = GST_CLOCK_TIME_NONE;
  rmdemux->base_ts = GST_CLOCK_TIME_NONE;
  rmdemux->need_newsegment = TRUE;

  gst_rm_utils_run_tests ();
}

static gboolean
gst_rmdemux_sink_event (GstPad * pad, GstEvent * event)
{
  GstRMDemux *rmdemux;
  gboolean ret;

  rmdemux = GST_RMDEMUX (gst_pad_get_parent (pad));

  GST_LOG_OBJECT (pad, "%s event", GST_EVENT_TYPE_NAME (event));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_NEWSEGMENT:
      gst_event_unref (event);
      ret = TRUE;
      break;
    default:
      ret = gst_pad_event_default (pad, event);
      break;
  }

  gst_object_unref (rmdemux);
  return ret;
}

static gboolean
gst_rmdemux_src_event (GstPad * pad, GstEvent * event)
{
  gboolean ret = TRUE;

  GstRMDemux *rmdemux = GST_RMDEMUX (GST_PAD_PARENT (pad));

  GST_LOG_OBJECT (rmdemux, "handling src event");

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_SEEK:
    {
      gboolean running;

      GST_LOG_OBJECT (rmdemux, "Event on src: SEEK");
      /* can't seek if we are not seekable, FIXME could pass the
       * seek query upstream after converting it to bytes using
       * the average bitrate of the stream. */
      if (!rmdemux->seekable) {
        ret = FALSE;
        GST_DEBUG ("seek on non seekable stream");
        goto done_unref;
      }

      GST_OBJECT_LOCK (rmdemux);
      /* check if we can do the seek now */
      running = rmdemux->running;
      GST_OBJECT_UNLOCK (rmdemux);

      /* now do the seek */
      if (running) {
        ret = gst_rmdemux_perform_seek (rmdemux, event);
      } else
        ret = TRUE;

      gst_event_unref (event);
      break;
    }
    default:
      GST_LOG_OBJECT (rmdemux, "Event on src: type=%d", GST_EVENT_TYPE (event));
      ret = gst_pad_event_default (pad, event);
      break;
  }

  return ret;

done_unref:
  GST_DEBUG ("error handling event");
  gst_event_unref (event);
  return ret;
}

/* Validate that this looks like a reasonable point to seek to */
static gboolean
gst_rmdemux_validate_offset (GstRMDemux * rmdemux)
{
  GstBuffer *buffer;
  GstFlowReturn flowret;
  guint16 version, length;
  gboolean ret = TRUE;

  flowret = gst_pad_pull_range (rmdemux->sinkpad, rmdemux->offset, 4, &buffer);

  if (flowret != GST_FLOW_OK) {
    GST_DEBUG_OBJECT (rmdemux, "Failed to pull data at offset %d",
        rmdemux->offset);
    return FALSE;
  }
  /* TODO: Can we also be seeking to a 'DATA' chunk header? Check this.
   * Also, for the case we currently handle, can we check any more? It's pretty
   * sucky to not be validating a little more heavily than this... */
  /* This should now be the start of a data packet header. That begins with
   * a 2-byte 'version' field, which has to be 0 or 1, then a length. I'm not
   * certain what values are valid for length, but it must always be at least
   * 4 bytes, and we can check that it won't take us past our known total size
   */

  version = RMDEMUX_GUINT16_GET (GST_BUFFER_DATA (buffer));
  if (version != 0 && version != 1) {
    GST_DEBUG_OBJECT (rmdemux, "Expected version 0 or 1, got %d",
        (int) version);
    ret = FALSE;
  }

  length = RMDEMUX_GUINT16_GET (GST_BUFFER_DATA (buffer) + 2);
  /* TODO: Also check against total stream length */
  if (length < 4) {
    GST_DEBUG_OBJECT (rmdemux, "Expected length >= 4, got %d", (int) length);
    ret = FALSE;
  }

  if (ret) {
    rmdemux->offset += 4;
    gst_adapter_clear (rmdemux->adapter);
    gst_adapter_push (rmdemux->adapter, buffer);
  } else {
    GST_WARNING_OBJECT (rmdemux, "Failed to validate seek offset at %d",
        rmdemux->offset);
  }

  return ret;
}

static gboolean
find_seek_offset_bytes (GstRMDemux * rmdemux, guint target)
{
  int i;
  GSList *cur;
  gboolean ret = FALSE;

  if (target < 0)
    return FALSE;

  for (cur = rmdemux->streams; cur; cur = cur->next) {
    GstRMDemuxStream *stream = cur->data;

    /* Search backwards through this stream's index until we find the first
     * timestamp before our target time */
    for (i = stream->index_length - 1; i >= 0; i--) {
      if (stream->index[i].offset <= target) {
        /* Set the seek_offset for the stream so we don't bother parsing it
         * until we've passed that point */
        stream->seek_offset = stream->index[i].offset;
        rmdemux->offset = stream->index[i].offset;
        ret = TRUE;
        break;
      }
    }
  }
  return ret;
}

static gboolean
find_seek_offset_time (GstRMDemux * rmdemux, GstClockTime time)
{
  int i, n_stream;
  gboolean ret = FALSE;
  GSList *cur;
  GstClockTime earliest = GST_CLOCK_TIME_NONE;

  n_stream = 0;
  for (cur = rmdemux->streams; cur; cur = cur->next, n_stream++) {
    GstRMDemuxStream *stream = cur->data;

    /* Search backwards through this stream's index until we find the first
     * timestamp before our target time */
    for (i = stream->index_length - 1; i >= 0; i--) {
      if (stream->index[i].timestamp <= time) {
        /* Set the seek_offset for the stream so we don't bother parsing it
         * until we've passed that point */
        stream->seek_offset = stream->index[i].offset;

        /* If it's also the earliest timestamp we've seen of all streams, then
         * that's our target!
         */
        if (earliest == GST_CLOCK_TIME_NONE ||
            stream->index[i].timestamp < earliest) {
          earliest = stream->index[i].timestamp;
          rmdemux->offset = stream->index[i].offset;
          GST_DEBUG_OBJECT (rmdemux,
              "We're looking for %" GST_TIME_FORMAT
              " and we found that stream %d has the latest index at %"
              GST_TIME_FORMAT, GST_TIME_ARGS (rmdemux->segment.start), n_stream,
              GST_TIME_ARGS (earliest));
        }

        ret = TRUE;

        break;
      }
    }
    stream->discont = TRUE;
  }
  return ret;
}

static gboolean
gst_rmdemux_perform_seek (GstRMDemux * rmdemux, GstEvent * event)
{
  gboolean validated;
  gboolean ret = TRUE;
  gboolean flush;
  GstFormat format;
  gdouble rate;
  GstSeekFlags flags;
  GstSeekType cur_type, stop_type;
  gint64 cur, stop;
  gboolean update;

  if (event) {
    GST_DEBUG_OBJECT (rmdemux, "seek with event");

    gst_event_parse_seek (event, &rate, &format, &flags,
        &cur_type, &cur, &stop_type, &stop);

    /* we can only seek on time */
    if (format != GST_FORMAT_TIME) {
      GST_DEBUG_OBJECT (rmdemux, "can only seek on TIME");
      goto error;
    }
    /* cannot yet do backwards playback */
    if (rate <= 0.0) {
      GST_DEBUG_OBJECT (rmdemux, "can only seek with positive rate, not %lf",
          rate);
      goto error;
    }
  } else {
    GST_DEBUG_OBJECT (rmdemux, "seek without event");

    flags = 0;
    rate = 1.0;
  }

  GST_DEBUG_OBJECT (rmdemux, "seek, rate %g", rate);

  flush = flags & GST_SEEK_FLAG_FLUSH;

  /* first step is to unlock the streaming thread if it is
   * blocked in a chain call, we do this by starting the flush. */
  if (flush) {
    gst_pad_push_event (rmdemux->sinkpad, gst_event_new_flush_start ());
    gst_rmdemux_send_event (rmdemux, gst_event_new_flush_start ());
  } else {
    gst_pad_pause_task (rmdemux->sinkpad);
  }

  GST_LOG_OBJECT (rmdemux, "Done starting flushes");

  /* now grab the stream lock so that streaming cannot continue, for
   * non flushing seeks when the element is in PAUSED this could block
   * forever. */
  GST_PAD_STREAM_LOCK (rmdemux->sinkpad);

  GST_LOG_OBJECT (rmdemux, "Took streamlock");

  /* close current segment first */
  if (rmdemux->segment_running && !flush) {
    GstEvent *newseg;

    newseg = gst_event_new_new_segment (TRUE, rmdemux->segment.rate,
        GST_FORMAT_TIME, rmdemux->segment.start,
        rmdemux->segment.last_stop, rmdemux->segment.time);

    gst_rmdemux_send_event (rmdemux, newseg);
  }

  if (event) {
    gst_segment_set_seek (&rmdemux->segment, rate, format, flags,
        cur_type, cur, stop_type, stop, &update);
  }

  GST_DEBUG_OBJECT (rmdemux, "segment positions set to %" GST_TIME_FORMAT "-%"
      GST_TIME_FORMAT, GST_TIME_ARGS (rmdemux->segment.start),
      GST_TIME_ARGS (rmdemux->segment.stop));

  /* we need to stop flushing on the sinkpad as we're going to use it
   * next. We can do this as we have the STREAM lock now. */
  gst_pad_push_event (rmdemux->sinkpad, gst_event_new_flush_stop ());

  GST_LOG_OBJECT (rmdemux, "Pushed FLUSH_STOP event");

  /* For each stream, find the first index offset equal to or before our seek 
   * target. Of these, find the smallest offset. That's where we seek to.
   *
   * Then we pull 4 bytes from that offset, and validate that we've seeked to a
   * what looks like a plausible packet.
   * If that fails, restart, with the seek target set to one less than the
   * offset we just tried. If we run out of places to try, treat that as a fatal
   * error.
   */
  if (!find_seek_offset_time (rmdemux, rmdemux->segment.last_stop)) {
    GST_LOG_OBJECT (rmdemux, "Failed to find seek offset by time");
    ret = FALSE;
    goto done;
  }

  GST_LOG_OBJECT (rmdemux, "Validating offset %u", rmdemux->offset);
  validated = gst_rmdemux_validate_offset (rmdemux);
  while (!validated) {
    GST_INFO_OBJECT (rmdemux, "Failed to validate offset at %u",
        rmdemux->offset);
    if (!find_seek_offset_bytes (rmdemux, rmdemux->offset - 1)) {
      ret = FALSE;
      goto done;
    }
    validated = gst_rmdemux_validate_offset (rmdemux);
  }

  GST_LOG_OBJECT (rmdemux, "Found final offset. Excellent!");

  /* now we have a new position, prepare for streaming again */
  {
    /* Reset the demuxer state */
    rmdemux->state = RMDEMUX_STATE_DATA_PACKET;

    if (flush)
      gst_rmdemux_send_event (rmdemux, gst_event_new_flush_stop ());

    /* must send newsegment event from streaming thread, so just set flag */
    rmdemux->need_newsegment = TRUE;

    /* notify start of new segment */
    if (rmdemux->segment.flags & GST_SEEK_FLAG_SEGMENT) {
      gst_element_post_message (GST_ELEMENT_CAST (rmdemux),
          gst_message_new_segment_start (GST_OBJECT_CAST (rmdemux),
              GST_FORMAT_TIME, rmdemux->segment.last_stop));
    }

    /* restart our task since it might have been stopped when we did the 
     * flush. */
    gst_pad_start_task (rmdemux->sinkpad, (GstTaskFunction) gst_rmdemux_loop,
        rmdemux->sinkpad);
  }

done:
  /* streaming can continue now */
  GST_PAD_STREAM_UNLOCK (rmdemux->sinkpad);

  return ret;

error:
  {
    GST_DEBUG_OBJECT (rmdemux, "seek failed");
    return FALSE;
  }
}


static gboolean
gst_rmdemux_src_query (GstPad * pad, GstQuery * query)
{
  gboolean res = FALSE;
  GstRMDemux *rmdemux;

  rmdemux = GST_RMDEMUX (gst_pad_get_parent (pad));

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_POSITION:
      GST_DEBUG_OBJECT (rmdemux, "Position query: no idea from demuxer!");
      break;
    case GST_QUERY_DURATION:{
      GstFormat fmt;

      gst_query_parse_duration (query, &fmt, NULL);
      if (fmt == GST_FORMAT_TIME) {
        GST_OBJECT_LOCK (rmdemux);
        if (G_LIKELY (rmdemux->running)) {
          gst_query_set_duration (query, GST_FORMAT_TIME, rmdemux->duration);
          GST_DEBUG_OBJECT (rmdemux, "duration set to %" GST_TIME_FORMAT,
              GST_TIME_ARGS (rmdemux->duration));
          res = TRUE;
        }
        GST_OBJECT_UNLOCK (rmdemux);
      }
      break;
    }
    case GST_QUERY_SEEKING:{
      GstFormat fmt;

      gst_query_parse_seeking (query, &fmt, NULL, NULL, NULL);
      if (fmt == GST_FORMAT_TIME) {
        GST_OBJECT_LOCK (rmdemux);
        if (G_LIKELY (rmdemux->running)) {
          gst_query_set_seeking (query, GST_FORMAT_TIME, rmdemux->seekable,
              0, rmdemux->duration);
          res = TRUE;
        }
        GST_OBJECT_UNLOCK (rmdemux);
      }
      break;
    }
    default:
      res = gst_pad_query_default (pad, query);
      break;
  }

  gst_object_unref (rmdemux);
  return res;
}

static const GstQueryType *
gst_rmdemux_src_query_types (GstPad * pad)
{
  static const GstQueryType query_types[] = {
    GST_QUERY_POSITION,
    GST_QUERY_DURATION,
    GST_QUERY_SEEKING,
    0
  };

  return query_types;
}

static void
gst_rmdemux_reset (GstRMDemux * rmdemux)
{
  GSList *cur;

  GST_OBJECT_LOCK (rmdemux);
  rmdemux->running = FALSE;
  GST_OBJECT_UNLOCK (rmdemux);

  for (cur = rmdemux->streams; cur; cur = cur->next) {
    GstRMDemuxStream *stream = cur->data;

    g_object_unref (stream->adapter);
    gst_rmdemux_stream_clear_cached_subpackets (rmdemux, stream);
    gst_element_remove_pad (GST_ELEMENT (rmdemux), stream->pad);
    if (stream->pending_tags)
      gst_tag_list_free (stream->pending_tags);
    if (stream->subpackets)
      g_ptr_array_free (stream->subpackets, TRUE);
    g_free (stream->index);
    g_free (stream);
  }
  g_slist_free (rmdemux->streams);
  rmdemux->streams = NULL;
  rmdemux->n_audio_streams = 0;
  rmdemux->n_video_streams = 0;

  gst_adapter_clear (rmdemux->adapter);
  rmdemux->state = RMDEMUX_STATE_HEADER;
  rmdemux->have_pads = FALSE;

  gst_segment_init (&rmdemux->segment, GST_FORMAT_UNDEFINED);
  rmdemux->first_ts = GST_CLOCK_TIME_NONE;
  rmdemux->base_ts = GST_CLOCK_TIME_NONE;
  rmdemux->need_newsegment = TRUE;
}

static GstStateChangeReturn
gst_rmdemux_change_state (GstElement * element, GstStateChange transition)
{
  GstRMDemux *rmdemux = GST_RMDEMUX (element);
  GstStateChangeReturn res;

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      rmdemux->state = RMDEMUX_STATE_HEADER;
      rmdemux->have_pads = FALSE;
      gst_segment_init (&rmdemux->segment, GST_FORMAT_TIME);
      rmdemux->running = FALSE;
      break;
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
      break;
    default:
      break;
  }

  res = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:{
      gst_rmdemux_reset (rmdemux);
      break;
    }
    case GST_STATE_CHANGE_READY_TO_NULL:
      break;
    default:
      break;
  }

  return res;
}

/* this function is called when the pad is activated and should start
 * processing data.
 *
 * We check if we can do random access to decide if we work push or
 * pull based.
 */
static gboolean
gst_rmdemux_sink_activate (GstPad * sinkpad)
{
  if (gst_pad_check_pull_range (sinkpad)) {
    return gst_pad_activate_pull (sinkpad, TRUE);
  } else {
    return gst_pad_activate_push (sinkpad, TRUE);
  }
}

/* this function gets called when we activate ourselves in push mode.
 * We cannot seek (ourselves) in the stream */
static gboolean
gst_rmdemux_sink_activate_push (GstPad * pad, gboolean active)
{
  GstRMDemux *rmdemux;

  rmdemux = GST_RMDEMUX (GST_PAD_PARENT (pad));

  GST_DEBUG_OBJECT (rmdemux, "activate_push");

  rmdemux->seekable = FALSE;

  return TRUE;
}

/* this function gets called when we activate ourselves in pull mode.
 * We can perform  random access to the resource and we start a task
 * to start reading */
static gboolean
gst_rmdemux_sink_activate_pull (GstPad * pad, gboolean active)
{
  GstRMDemux *rmdemux;

  rmdemux = GST_RMDEMUX (GST_PAD_PARENT (pad));

  GST_DEBUG_OBJECT (rmdemux, "activate_pull");

  if (active) {
    rmdemux->seekable = TRUE;
    rmdemux->offset = 0;
    rmdemux->loop_state = RMDEMUX_LOOP_STATE_HEADER;
    rmdemux->data_offset = G_MAXUINT;

    return gst_pad_start_task (pad, (GstTaskFunction) gst_rmdemux_loop, pad);
  } else {
    return gst_pad_stop_task (pad);
  }
}

/* random access mode - just pass over to our chain function */
static void
gst_rmdemux_loop (GstPad * pad)
{
  GstRMDemux *rmdemux;
  GstBuffer *buffer;
  GstFlowReturn ret = GST_FLOW_OK;
  guint size;

  rmdemux = GST_RMDEMUX (GST_PAD_PARENT (pad));

  GST_LOG_OBJECT (rmdemux, "loop with state=%d and offset=0x%x",
      rmdemux->loop_state, rmdemux->offset);

  switch (rmdemux->state) {
    case RMDEMUX_STATE_HEADER:
      size = HEADER_SIZE;
      break;
    case RMDEMUX_STATE_HEADER_DATA:
      size = DATA_SIZE;
      break;
    case RMDEMUX_STATE_DATA_PACKET:
      size = rmdemux->avg_packet_size;
      break;
    case RMDEMUX_STATE_EOS:
      GST_LOG_OBJECT (rmdemux, "At EOS, pausing task");
      ret = GST_FLOW_UNEXPECTED;
      goto need_pause;
    default:
      GST_LOG_OBJECT (rmdemux, "Default: requires %d bytes (state is %d)",
          (int) rmdemux->size, rmdemux->state);
      size = rmdemux->size;
  }

  ret = gst_pad_pull_range (pad, rmdemux->offset, size, &buffer);
  if (ret != GST_FLOW_OK) {
    if (rmdemux->offset == rmdemux->index_offset) {
      /* The index isn't available so forget about it */
      rmdemux->loop_state = RMDEMUX_LOOP_STATE_DATA;
      rmdemux->offset = rmdemux->data_offset;
      GST_OBJECT_LOCK (rmdemux);
      rmdemux->running = TRUE;
      rmdemux->seekable = FALSE;
      GST_OBJECT_UNLOCK (rmdemux);
      return;
    } else {
      GST_DEBUG_OBJECT (rmdemux, "Unable to pull %d bytes at offset 0x%08x "
          "(pull_range returned flow %s, state is %d)", (gint) size,
          rmdemux->offset, gst_flow_get_name (ret), GST_STATE (rmdemux));
      goto need_pause;
    }
  }

  size = GST_BUFFER_SIZE (buffer);

  /* Defer to the chain function */
  ret = gst_rmdemux_chain (pad, buffer);
  if (ret != GST_FLOW_OK) {
    GST_DEBUG_OBJECT (rmdemux, "Chain flow failed at offset 0x%08x",
        rmdemux->offset);
    goto need_pause;
  }

  rmdemux->offset += size;

  switch (rmdemux->loop_state) {
    case RMDEMUX_LOOP_STATE_HEADER:
      if (rmdemux->offset >= rmdemux->data_offset) {
        /* It's the end of the header */
        rmdemux->loop_state = RMDEMUX_LOOP_STATE_INDEX;
        rmdemux->offset = rmdemux->index_offset;
      }
      break;
    case RMDEMUX_LOOP_STATE_INDEX:
      if (rmdemux->state == RMDEMUX_STATE_HEADER) {
        if (rmdemux->index_offset == 0) {
          /* We've read the last index */
          rmdemux->loop_state = RMDEMUX_LOOP_STATE_DATA;
          rmdemux->offset = rmdemux->data_offset;
          GST_OBJECT_LOCK (rmdemux);
          rmdemux->running = TRUE;
          GST_OBJECT_UNLOCK (rmdemux);
        } else {
          /* Get the next index */
          rmdemux->offset = rmdemux->index_offset;
        }
      }
      break;
    case RMDEMUX_LOOP_STATE_DATA:
      break;
  }

  return;

  /* ERRORS */
need_pause:
  {
    const gchar *reason = gst_flow_get_name (ret);

    GST_LOG_OBJECT (rmdemux, "pausing task, reason %s", reason);
    rmdemux->segment_running = FALSE;
    gst_pad_pause_task (rmdemux->sinkpad);

    if (GST_FLOW_IS_FATAL (ret) || ret == GST_FLOW_NOT_LINKED) {
      if (ret == GST_FLOW_UNEXPECTED) {
        /* perform EOS logic */
        if (rmdemux->segment.flags & GST_SEEK_FLAG_SEGMENT) {
          gint64 stop;

          /* for segment playback we need to post when (in stream time)
           * we stopped, this is either stop (when set) or the duration. */
          if ((stop = rmdemux->segment.stop) == -1)
            stop = rmdemux->segment.duration;

          GST_LOG_OBJECT (rmdemux, "Sending segment done, at end of segment");
          gst_element_post_message (GST_ELEMENT (rmdemux),
              gst_message_new_segment_done (GST_OBJECT (rmdemux),
                  GST_FORMAT_TIME, stop));
        } else {
          /* normal playback, send EOS to all linked pads */
          GST_LOG_OBJECT (rmdemux, "Sending EOS, at end of stream");
          gst_rmdemux_send_event (rmdemux, gst_event_new_eos ());
        }
      } else {
        GST_ELEMENT_ERROR (rmdemux, STREAM, FAILED,
            (NULL), ("stream stopped, reason %s", reason));
        gst_rmdemux_send_event (rmdemux, gst_event_new_eos ());
      }
    }
    return;
  }
}

static gboolean
gst_rmdemux_fourcc_isplausible (guint32 fourcc)
{
  int i;

  for (i = 0; i < 4; i++) {
    if (!isprint ((int) ((unsigned char *) (&fourcc))[i])) {
      return FALSE;
    }
  }
  return TRUE;
}

static GstFlowReturn
gst_rmdemux_chain (GstPad * pad, GstBuffer * buffer)
{
  GstFlowReturn ret = GST_FLOW_OK;
  const guint8 *data;
  guint16 version;
  guint avail;

  GstRMDemux *rmdemux = GST_RMDEMUX (GST_PAD_PARENT (pad));

  if (rmdemux->base_ts == -1) {
    rmdemux->base_ts = GST_BUFFER_TIMESTAMP (buffer);
    GST_LOG_OBJECT (rmdemux, "base_ts %" GST_TIME_FORMAT,
        GST_TIME_ARGS (rmdemux->base_ts));
  }

  gst_adapter_push (rmdemux->adapter, buffer);

  GST_LOG_OBJECT (rmdemux, "Chaining buffer of size %d",
      GST_BUFFER_SIZE (buffer));

  while (TRUE) {
    avail = gst_adapter_available (rmdemux->adapter);

    GST_LOG_OBJECT (rmdemux, "looping in chain, avail %u", avail);
    switch (rmdemux->state) {
      case RMDEMUX_STATE_HEADER:
      {
        if (gst_adapter_available (rmdemux->adapter) < HEADER_SIZE)
          goto unlock;

        data = gst_adapter_peek (rmdemux->adapter, HEADER_SIZE);

        rmdemux->object_id = RMDEMUX_FOURCC_GET (data + 0);
        rmdemux->size = RMDEMUX_GUINT32_GET (data + 4) - HEADER_SIZE;
        rmdemux->object_version = RMDEMUX_GUINT16_GET (data + 8);

        /* Sanity-check. We assume that the FOURCC is printable ASCII */
        if (!gst_rmdemux_fourcc_isplausible (rmdemux->object_id)) {
          /* Failed. Remain in HEADER state, try again... We flush only 
           * the actual FOURCC, not the entire header, because we could 
           * need to resync anywhere at all... really, this should never 
           * happen. */
          GST_WARNING_OBJECT (rmdemux, "Bogus looking header, unprintable "
              "FOURCC");
          gst_adapter_flush (rmdemux->adapter, 4);

          break;
        }

        GST_LOG_OBJECT (rmdemux, "header found with object_id=%"
            GST_FOURCC_FORMAT
            " size=%08x object_version=%d",
            GST_FOURCC_ARGS (rmdemux->object_id), rmdemux->size,
            rmdemux->object_version);

        gst_adapter_flush (rmdemux->adapter, HEADER_SIZE);

        switch (rmdemux->object_id) {
          case GST_MAKE_FOURCC ('.', 'R', 'M', 'F'):
            rmdemux->state = RMDEMUX_STATE_HEADER_RMF;
            break;
          case GST_MAKE_FOURCC ('P', 'R', 'O', 'P'):
            rmdemux->state = RMDEMUX_STATE_HEADER_PROP;
            break;
          case GST_MAKE_FOURCC ('M', 'D', 'P', 'R'):
            rmdemux->state = RMDEMUX_STATE_HEADER_MDPR;
            break;
          case GST_MAKE_FOURCC ('I', 'N', 'D', 'X'):
            rmdemux->state = RMDEMUX_STATE_HEADER_INDX;
            break;
          case GST_MAKE_FOURCC ('D', 'A', 'T', 'A'):
            rmdemux->state = RMDEMUX_STATE_HEADER_DATA;
            break;
          case GST_MAKE_FOURCC ('C', 'O', 'N', 'T'):
            rmdemux->state = RMDEMUX_STATE_HEADER_CONT;
            break;
          default:
            rmdemux->state = RMDEMUX_STATE_HEADER_UNKNOWN;
            break;
        }
        break;
      }
      case RMDEMUX_STATE_HEADER_UNKNOWN:
      {
        if (gst_adapter_available (rmdemux->adapter) < rmdemux->size)
          goto unlock;

        GST_WARNING_OBJECT (rmdemux, "Unknown object_id %" GST_FOURCC_FORMAT,
            GST_FOURCC_ARGS (rmdemux->object_id));

        gst_adapter_flush (rmdemux->adapter, rmdemux->size);
        rmdemux->state = RMDEMUX_STATE_HEADER;
        break;
      }
      case RMDEMUX_STATE_HEADER_RMF:
      {
        if (gst_adapter_available (rmdemux->adapter) < rmdemux->size)
          goto unlock;

        if ((rmdemux->object_version == 0) || (rmdemux->object_version == 1)) {
          data = gst_adapter_peek (rmdemux->adapter, rmdemux->size);

          gst_rmdemux_parse__rmf (rmdemux, data, rmdemux->size);
        }

        gst_adapter_flush (rmdemux->adapter, rmdemux->size);
        rmdemux->state = RMDEMUX_STATE_HEADER;
        break;
      }
      case RMDEMUX_STATE_HEADER_PROP:
      {
        if (gst_adapter_available (rmdemux->adapter) < rmdemux->size)
          goto unlock;
        data = gst_adapter_peek (rmdemux->adapter, rmdemux->size);

        gst_rmdemux_parse_prop (rmdemux, data, rmdemux->size);

        gst_adapter_flush (rmdemux->adapter, rmdemux->size);
        rmdemux->state = RMDEMUX_STATE_HEADER;
        break;
      }
      case RMDEMUX_STATE_HEADER_MDPR:
      {
        if (gst_adapter_available (rmdemux->adapter) < rmdemux->size)
          goto unlock;
        data = gst_adapter_peek (rmdemux->adapter, rmdemux->size);

        gst_rmdemux_parse_mdpr (rmdemux, data, rmdemux->size);

        gst_adapter_flush (rmdemux->adapter, rmdemux->size);
        rmdemux->state = RMDEMUX_STATE_HEADER;
        break;
      }
      case RMDEMUX_STATE_HEADER_CONT:
      {
        if (gst_adapter_available (rmdemux->adapter) < rmdemux->size)
          goto unlock;
        data = gst_adapter_peek (rmdemux->adapter, rmdemux->size);

        gst_rmdemux_parse_cont (rmdemux, data, rmdemux->size);

        gst_adapter_flush (rmdemux->adapter, rmdemux->size);
        rmdemux->state = RMDEMUX_STATE_HEADER;
        break;
      }
      case RMDEMUX_STATE_HEADER_DATA:
      {
        /* If we haven't already done so then signal there are no more pads */
        if (!rmdemux->have_pads) {
          GST_LOG_OBJECT (rmdemux, "no more pads");
          gst_element_no_more_pads (GST_ELEMENT (rmdemux));
          rmdemux->have_pads = TRUE;
        }

        /* The actual header is only 8 bytes */
        rmdemux->size = DATA_SIZE;
        GST_LOG_OBJECT (rmdemux, "data available %d",
            gst_adapter_available (rmdemux->adapter));
        if (gst_adapter_available (rmdemux->adapter) < rmdemux->size)
          goto unlock;

        data = gst_adapter_peek (rmdemux->adapter, rmdemux->size);

        gst_rmdemux_parse_data (rmdemux, data, rmdemux->size);

        gst_adapter_flush (rmdemux->adapter, rmdemux->size);

        rmdemux->state = RMDEMUX_STATE_DATA_PACKET;
        break;
      }
      case RMDEMUX_STATE_HEADER_INDX:
      {
        if (gst_adapter_available (rmdemux->adapter) < rmdemux->size)
          goto unlock;
        data = gst_adapter_peek (rmdemux->adapter, rmdemux->size);

        rmdemux->size = gst_rmdemux_parse_indx (rmdemux, data, rmdemux->size);

        /* Only flush the header */
        gst_adapter_flush (rmdemux->adapter, HEADER_SIZE);

        rmdemux->state = RMDEMUX_STATE_INDX_DATA;
        break;
      }
      case RMDEMUX_STATE_INDX_DATA:
      {
        /* There's not always an data to get... */
        if (rmdemux->size > 0) {
          if (gst_adapter_available (rmdemux->adapter) < rmdemux->size)
            goto unlock;

          data = gst_adapter_peek (rmdemux->adapter, rmdemux->size);

          gst_rmdemux_parse_indx_data (rmdemux, data, rmdemux->size);

          gst_adapter_flush (rmdemux->adapter, rmdemux->size);
        }

        rmdemux->state = RMDEMUX_STATE_HEADER;
        break;
      }
      case RMDEMUX_STATE_DATA_PACKET:
      {
        if (gst_adapter_available (rmdemux->adapter) < 2)
          goto unlock;

        data = gst_adapter_peek (rmdemux->adapter, 2);
        version = RMDEMUX_GUINT16_GET (data);
        GST_LOG_OBJECT (rmdemux, "Data packet with version=%d", version);

        if (version == 0 || version == 1) {
          guint16 length;

          if (gst_adapter_available (rmdemux->adapter) < 4)
            goto unlock;
          data = gst_adapter_peek (rmdemux->adapter, 4);

          length = RMDEMUX_GUINT16_GET (data + 2);
          GST_LOG_OBJECT (rmdemux, "Got length %d", length);

          if (length < 4) {
            GST_LOG_OBJECT (rmdemux, "length too small, dropping");
            /* Invalid, just drop it */
            gst_adapter_flush (rmdemux->adapter, 4);
          } else {
            GstBuffer *buffer;

            avail = gst_adapter_available (rmdemux->adapter);
            if (avail < length)
              goto unlock;

            GST_LOG_OBJECT (rmdemux, "we have %u available and we needed %d",
                avail, length);

            /* flush version and length */
            gst_adapter_flush (rmdemux->adapter, 4);
            length -= 4;

            buffer = gst_adapter_take_buffer (rmdemux->adapter, length);

            ret = gst_rmdemux_parse_packet (rmdemux, buffer, version);
            rmdemux->chunk_index++;
          }

          if (rmdemux->chunk_index == rmdemux->n_chunks || length == 0)
            rmdemux->state = RMDEMUX_STATE_HEADER;
        } else {
          /* Stream done */
          gst_adapter_flush (rmdemux->adapter, 2);

          if (rmdemux->data_offset == 0) {
            GST_LOG_OBJECT (rmdemux,
                "No further data, internal demux state EOS");
            rmdemux->state = RMDEMUX_STATE_EOS;
          } else
            rmdemux->state = RMDEMUX_STATE_HEADER;
        }
        break;
      }
      case RMDEMUX_STATE_EOS:
        gst_rmdemux_send_event (rmdemux, gst_event_new_eos ());
        goto unlock;
      default:
        GST_WARNING_OBJECT (rmdemux, "Unhandled state %d", rmdemux->state);
        goto unlock;
    }
  }

unlock:
  return ret;
}

static GstRMDemuxStream *
gst_rmdemux_get_stream_by_id (GstRMDemux * rmdemux, int id)
{
  GSList *cur;

  for (cur = rmdemux->streams; cur; cur = cur->next) {
    GstRMDemuxStream *stream = cur->data;

    if (stream->id == id) {
      return stream;
    }
  }

  return NULL;
}

static void
gst_rmdemux_send_event (GstRMDemux * rmdemux, GstEvent * event)
{
  GSList *cur;

  for (cur = rmdemux->streams; cur; cur = cur->next) {
    GstRMDemuxStream *stream = cur->data;

    GST_DEBUG_OBJECT (rmdemux, "Pushing %s event on pad %s",
        GST_EVENT_TYPE_NAME (event), GST_PAD_NAME (stream->pad));

    switch (GST_EVENT_TYPE (event)) {
      case GST_EVENT_FLUSH_STOP:
        stream->last_ts = -1;
        stream->next_ts = -1;
        stream->last_seq = -1;
        stream->next_seq = -1;
        stream->last_flow = GST_FLOW_OK;
        break;
      default:
        break;
    }
    gst_event_ref (event);
    gst_pad_push_event (stream->pad, event);
  }
  gst_event_unref (event);
}

static void
gst_rmdemux_add_stream (GstRMDemux * rmdemux, GstRMDemuxStream * stream)
{
  GstCaps *stream_caps = NULL;
  const gchar *codec_tag = NULL;
  const gchar *codec_name = NULL;
  int version = 0;

  if (stream->subtype == GST_RMDEMUX_STREAM_VIDEO) {
    char *name = g_strdup_printf ("video_%02d", rmdemux->n_video_streams);

    stream->pad =
        gst_pad_new_from_static_template (&gst_rmdemux_videosrc_template, name);
    g_free (name);

    codec_tag = GST_TAG_VIDEO_CODEC;

    switch (stream->fourcc) {
      case GST_RM_VDO_RV10:
        codec_name = "Real Video 1.0";
        version = 1;
        break;
      case GST_RM_VDO_RV20:
        codec_name = "Real Video 2.0";
        version = 2;
        break;
      case GST_RM_VDO_RV30:
        codec_name = "Real Video 3.0";
        version = 3;
        break;
      case GST_RM_VDO_RV40:
        codec_name = "Real Video 4.0";
        version = 4;
        break;
      default:
        stream_caps = gst_caps_new_simple ("video/x-unknown-fourcc",
            "fourcc", GST_TYPE_FOURCC, stream->fourcc, NULL);
        GST_WARNING_OBJECT (rmdemux,
            "Unknown video FOURCC code \"%" GST_FOURCC_FORMAT "\" (%08x)",
            GST_FOURCC_ARGS (stream->fourcc), stream->fourcc);
    }

    if (version) {
      stream_caps =
          gst_caps_new_simple ("video/x-pn-realvideo", "rmversion", G_TYPE_INT,
          (int) version,
          "format", G_TYPE_INT,
          (int) stream->format,
          "subformat", G_TYPE_INT, (int) stream->subformat, NULL);
    }

    if (stream_caps) {
      gst_caps_set_simple (stream_caps,
          "width", G_TYPE_INT, stream->width,
          "height", G_TYPE_INT, stream->height,
          "framerate", GST_TYPE_FRACTION, stream->framerate_numerator,
          stream->framerate_denominator, NULL);
    }
    rmdemux->n_video_streams++;

  } else if (stream->subtype == GST_RMDEMUX_STREAM_AUDIO) {
    char *name = g_strdup_printf ("audio_%02d", rmdemux->n_audio_streams);

    stream->pad =
        gst_pad_new_from_static_template (&gst_rmdemux_audiosrc_template, name);
    GST_LOG_OBJECT (rmdemux, "Created audio pad \"%s\"", name);
    g_free (name);

    codec_tag = GST_TAG_AUDIO_CODEC;

    switch (stream->fourcc) {
        /* Older RealAudio Codecs */
      case GST_RM_AUD_14_4:
        codec_name = "Real Audio 14.4kbps";
        version = 1;
        break;

      case GST_RM_AUD_28_8:
        codec_name = "Real Audio 28.8kbps";
        version = 2;
        break;

        /* DolbyNet (Dolby AC3, low bitrate) */
      case GST_RM_AUD_DNET:
        codec_name = "AC-3 audio";
        stream_caps =
            gst_caps_new_simple ("audio/x-ac3", "rate", G_TYPE_INT,
            (int) stream->rate, NULL);
        stream->needs_descrambling = TRUE;
        stream->subpackets_needed = 1;
        stream->subpackets = NULL;
        break;

        /* MPEG-4 based */
      case GST_RM_AUD_RAAC:
      case GST_RM_AUD_RACP:
        codec_name = "MPEG4 audio";
        stream_caps =
            gst_caps_new_simple ("audio/mpeg", "mpegversion", G_TYPE_INT,
            (int) 4, "framed", G_TYPE_BOOLEAN, TRUE, NULL);
        if (stream->extra_data_size > 0) {
          /* strip off an unknown byte in the extra data */
          stream->extra_data_size--;
          stream->extra_data++;
        }
        stream->needs_descrambling = TRUE;
        stream->subpackets_needed = 1;
        stream->subpackets = NULL;
        break;

        /* Sony ATRAC3 */
      case GST_RM_AUD_ATRC:
        codec_name = "Sony ATRAC3";
        stream_caps = gst_caps_new_simple ("audio/x-vnd.sony.atrac3", NULL);
        stream->needs_descrambling = TRUE;
        stream->subpackets_needed = stream->height;
        stream->subpackets = NULL;
        break;

        /* RealAudio G2 audio */
      case GST_RM_AUD_COOK:
        codec_name = "Real Audio G2 (Cook)";
        version = 8;
        stream->needs_descrambling = TRUE;
        stream->subpackets_needed = stream->height;
        stream->subpackets = NULL;
        break;

        /* RALF is lossless */
      case GST_RM_AUD_RALF:
        codec_name = "Real Audio Lossless (RALF)";
        GST_DEBUG_OBJECT (rmdemux, "RALF");
        stream_caps = gst_caps_new_simple ("audio/x-ralf-mpeg4-generic", NULL);
        break;

      case GST_RM_AUD_SIPR:

        if (stream->flavor > 3) {
          GST_WARNING_OBJECT (rmdemux, "bad SIPR flavor %d, freeing it",
              stream->flavor);
          g_free (stream);
          goto beach;
        }

        codec_name = "Sipro/ACELP.NET Voice";
        GST_DEBUG_OBJECT (rmdemux, "SIPR");
        stream_caps = gst_caps_new_simple ("audio/x-sipro", NULL);
        stream->needs_descrambling = TRUE;
        stream->subpackets_needed = stream->height;
        stream->subpackets = NULL;
        stream->leaf_size = sipr_subpk_size[stream->flavor];

        break;

      default:
        stream_caps = gst_caps_new_simple ("video/x-unknown-fourcc",
            "fourcc", GST_TYPE_FOURCC, stream->fourcc, NULL);
        GST_WARNING_OBJECT (rmdemux,
            "Unknown audio FOURCC code \"%" GST_FOURCC_FORMAT "\" (%08x)",
            GST_FOURCC_ARGS (stream->fourcc), stream->fourcc);
        break;
    }

    if (version) {
      stream_caps =
          gst_caps_new_simple ("audio/x-pn-realaudio", "raversion", G_TYPE_INT,
          (int) version, NULL);
    }

    if (stream_caps) {
      gst_caps_set_simple (stream_caps,
          "flavor", G_TYPE_INT, (int) stream->flavor,
          "rate", G_TYPE_INT, (int) stream->rate,
          "channels", G_TYPE_INT, (int) stream->n_channels,
          "width", G_TYPE_INT, (int) stream->sample_width,
          "leaf_size", G_TYPE_INT, (int) stream->leaf_size,
          "packet_size", G_TYPE_INT, (int) stream->packet_size,
          "height", G_TYPE_INT, (int) stream->height, NULL);
    }
    rmdemux->n_audio_streams++;
  } else {
    GST_WARNING_OBJECT (rmdemux, "not adding stream of type %d, freeing it",
        stream->subtype);
    g_free (stream);
    goto beach;
  }

  GST_PAD_ELEMENT_PRIVATE (stream->pad) = stream;
  rmdemux->streams = g_slist_append (rmdemux->streams, stream);
  GST_LOG_OBJECT (rmdemux, "n_streams is now %d",
      g_slist_length (rmdemux->streams));

  GST_LOG ("stream->pad = %p, stream_caps = %" GST_PTR_FORMAT, stream->pad,
      stream_caps);

  if (stream->pad && stream_caps) {

    GST_LOG_OBJECT (rmdemux, "%d bytes of extra data for stream %s",
        stream->extra_data_size, GST_PAD_NAME (stream->pad));

    /* add codec_data if there is any */
    if (stream->extra_data_size > 0) {
      GstBuffer *buffer;

      buffer = gst_buffer_new_and_alloc (stream->extra_data_size);
      memcpy (GST_BUFFER_DATA (buffer), stream->extra_data,
          stream->extra_data_size);

      gst_caps_set_simple (stream_caps, "codec_data", GST_TYPE_BUFFER,
          buffer, NULL);

      gst_buffer_unref (buffer);
    }

    gst_pad_use_fixed_caps (stream->pad);

    gst_pad_set_caps (stream->pad, stream_caps);
    gst_pad_set_event_function (stream->pad,
        GST_DEBUG_FUNCPTR (gst_rmdemux_src_event));
    gst_pad_set_query_type_function (stream->pad,
        GST_DEBUG_FUNCPTR (gst_rmdemux_src_query_types));
    gst_pad_set_query_function (stream->pad,
        GST_DEBUG_FUNCPTR (gst_rmdemux_src_query));

    GST_DEBUG_OBJECT (rmdemux, "adding pad %s with caps %" GST_PTR_FORMAT
        ", stream_id=%d", GST_PAD_NAME (stream->pad), stream_caps, stream->id);
    gst_pad_set_active (stream->pad, TRUE);
    gst_element_add_pad (GST_ELEMENT_CAST (rmdemux), stream->pad);

    /* save for later, we must send the tags after the newsegment event */
    if (codec_name != NULL && codec_tag != NULL) {
      if (stream->pending_tags == NULL)
        stream->pending_tags = gst_tag_list_new ();
      gst_tag_list_add (stream->pending_tags, GST_TAG_MERGE_KEEP,
          codec_tag, codec_name, NULL);
    }
  }

beach:

  if (stream_caps)
    gst_caps_unref (stream_caps);
}

static int
re_skip_pascal_string (const guint8 * ptr)
{
  int length;

  length = ptr[0];

  return length + 1;
}

static void
gst_rmdemux_parse__rmf (GstRMDemux * rmdemux, const guint8 * data, int length)
{
  GST_LOG_OBJECT (rmdemux, "file_version: %d", RMDEMUX_GUINT32_GET (data));
  GST_LOG_OBJECT (rmdemux, "num_headers: %d", RMDEMUX_GUINT32_GET (data + 4));
}

static void
gst_rmdemux_parse_prop (GstRMDemux * rmdemux, const guint8 * data, int length)
{
  GST_LOG_OBJECT (rmdemux, "max bitrate: %d", RMDEMUX_GUINT32_GET (data));
  GST_LOG_OBJECT (rmdemux, "avg bitrate: %d", RMDEMUX_GUINT32_GET (data + 4));
  GST_LOG_OBJECT (rmdemux, "max packet size: %d",
      RMDEMUX_GUINT32_GET (data + 8));
  rmdemux->avg_packet_size = RMDEMUX_GUINT32_GET (data + 12);
  GST_LOG_OBJECT (rmdemux, "avg packet size: %d", rmdemux->avg_packet_size);
  rmdemux->num_packets = RMDEMUX_GUINT32_GET (data + 16);
  GST_LOG_OBJECT (rmdemux, "number of packets: %d", rmdemux->num_packets);

  GST_LOG_OBJECT (rmdemux, "duration: %d", RMDEMUX_GUINT32_GET (data + 20));
  rmdemux->duration = RMDEMUX_GUINT32_GET (data + 20) * GST_MSECOND;

  GST_LOG_OBJECT (rmdemux, "preroll: %d", RMDEMUX_GUINT32_GET (data + 24));
  rmdemux->index_offset = RMDEMUX_GUINT32_GET (data + 28);
  GST_LOG_OBJECT (rmdemux, "offset of INDX section: 0x%08x",
      rmdemux->index_offset);
  rmdemux->data_offset = RMDEMUX_GUINT32_GET (data + 32);
  GST_LOG_OBJECT (rmdemux, "offset of DATA section: 0x%08x",
      rmdemux->data_offset);
  GST_LOG_OBJECT (rmdemux, "n streams: %d", RMDEMUX_GUINT16_GET (data + 36));
  GST_LOG_OBJECT (rmdemux, "flags: 0x%04x", RMDEMUX_GUINT16_GET (data + 38));
}

static void
gst_rmdemux_parse_mdpr (GstRMDemux * rmdemux, const guint8 * data, int length)
{
  GstRMDemuxStream *stream;
  char *stream1_type_string;
  char *stream2_type_string;
  guint str_len = 0;
  int stream_type;
  int offset;
  guint32 max_bitrate;
  guint32 avg_bitrate;

  stream = g_new0 (GstRMDemuxStream, 1);

  stream->id = RMDEMUX_GUINT16_GET (data);
  stream->index = NULL;
  stream->seek_offset = 0;
  stream->last_ts = -1;
  stream->next_ts = -1;
  stream->last_flow = GST_FLOW_OK;
  stream->discont = TRUE;
  stream->adapter = gst_adapter_new ();
  GST_LOG_OBJECT (rmdemux, "stream_number=%d", stream->id);

  /* parse the bitrates */
  max_bitrate = RMDEMUX_GUINT32_GET (data + 2);
  avg_bitrate = RMDEMUX_GUINT32_GET (data + 6);
  GST_LOG_OBJECT (rmdemux, "Stream max bitrate=%u", max_bitrate);
  GST_LOG_OBJECT (rmdemux, "Stream avg bitrate=%u", avg_bitrate);
  if (max_bitrate != 0) {
    if (stream->pending_tags == NULL)
      stream->pending_tags = gst_tag_list_new ();
    gst_tag_list_add (stream->pending_tags, GST_TAG_MERGE_REPLACE,
        GST_TAG_MAXIMUM_BITRATE, max_bitrate, NULL);
  }
  if (avg_bitrate != 0) {
    if (stream->pending_tags == NULL)
      stream->pending_tags = gst_tag_list_new ();
    gst_tag_list_add (stream->pending_tags, GST_TAG_MERGE_REPLACE,
        GST_TAG_BITRATE, avg_bitrate, NULL);
  }

  offset = 30;
  stream1_type_string = gst_rm_utils_read_string8 (data + offset,
      length - offset, &str_len);
  offset += str_len;
  stream2_type_string = gst_rm_utils_read_string8 (data + offset,
      length - offset, &str_len);
  offset += str_len;

  /* stream1_type_string for audio and video stream is a "put_whatever_you_want" field :
   * observed values :
   * - "[The ]Video/Audio Stream" (File produced by an official Real encoder)
   * - "RealVideoPremierePlugIn-VIDEO/AUDIO" (File produced by Abobe Premiere)
   *
   * so, we should not rely on it to know which stream type it is
   */

  GST_LOG_OBJECT (rmdemux, "stream type: %s", stream1_type_string);
  GST_LOG_OBJECT (rmdemux, "MIME type=%s", stream2_type_string);

  if (strcmp (stream2_type_string, "video/x-pn-realvideo") == 0) {
    stream_type = GST_RMDEMUX_STREAM_VIDEO;
  } else if (strcmp (stream2_type_string,
          "video/x-pn-multirate-realvideo") == 0) {
    stream_type = GST_RMDEMUX_STREAM_VIDEO;
  } else if (strcmp (stream2_type_string, "audio/x-pn-realaudio") == 0) {
    stream_type = GST_RMDEMUX_STREAM_AUDIO;
  } else if (strcmp (stream2_type_string,
          "audio/x-pn-multirate-realaudio") == 0) {
    stream_type = GST_RMDEMUX_STREAM_AUDIO;
  } else if (strcmp (stream2_type_string,
          "audio/x-pn-multirate-realaudio-live") == 0) {
    stream_type = GST_RMDEMUX_STREAM_AUDIO;
  } else if (strcmp (stream2_type_string, "audio/x-ralf-mpeg4-generic") == 0) {
    /* Another audio type found in the real testsuite */
    stream_type = GST_RMDEMUX_STREAM_AUDIO;
  } else if (strcmp (stream1_type_string, "") == 0 &&
      strcmp (stream2_type_string, "logical-fileinfo") == 0) {
    stream_type = GST_RMDEMUX_STREAM_FILEINFO;
  } else {
    stream_type = GST_RMDEMUX_STREAM_UNKNOWN;
    GST_WARNING_OBJECT (rmdemux, "unknown stream type \"%s\",\"%s\"",
        stream1_type_string, stream2_type_string);
  }
  g_free (stream1_type_string);
  g_free (stream2_type_string);

  offset += 4;

  stream->subtype = stream_type;
  switch (stream_type) {

    case GST_RMDEMUX_STREAM_VIDEO:
      /* RV10/RV20/RV30/RV40 => video/x-pn-realvideo, version=1,2,3,4 */
      stream->fourcc = RMDEMUX_FOURCC_GET (data + offset + 8);
      stream->width = RMDEMUX_GUINT16_GET (data + offset + 12);
      stream->height = RMDEMUX_GUINT16_GET (data + offset + 14);
      stream->rate = RMDEMUX_GUINT16_GET (data + offset + 16);
      stream->subformat = RMDEMUX_GUINT32_GET (data + offset + 26);
      stream->format = RMDEMUX_GUINT32_GET (data + offset + 30);
      stream->extra_data_size = length - (offset + 26);
      stream->extra_data = (guint8 *) data + offset + 26;
      /* Natural way to represent framerates here requires unsigned 32 bit
       * numerator, which we don't have. For the nasty case, approximate...
       */
      {
        guint32 numerator = RMDEMUX_GUINT16_GET (data + offset + 22) * 65536 +
            RMDEMUX_GUINT16_GET (data + offset + 24);
        if (numerator > G_MAXINT) {
          stream->framerate_numerator = (gint) (numerator >> 1);
          stream->framerate_denominator = 32768;
        } else {
          stream->framerate_numerator = (gint) numerator;
          stream->framerate_denominator = 65536;
        }
      }

      GST_DEBUG_OBJECT (rmdemux,
          "Video stream with fourcc=%" GST_FOURCC_FORMAT
          " width=%d height=%d rate=%d framerate=%d/%d subformat=%x format=%x extra_data_size=%d",
          GST_FOURCC_ARGS (stream->fourcc), stream->width, stream->height,
          stream->rate, stream->framerate_numerator,
          stream->framerate_denominator, stream->subformat, stream->format,
          stream->extra_data_size);
      break;
    case GST_RMDEMUX_STREAM_AUDIO:{
      stream->version = RMDEMUX_GUINT16_GET (data + offset + 4);
      GST_INFO ("stream version = %u", stream->version);
      switch (stream->version) {
        case 3:
          stream->fourcc = GST_RM_AUD_14_4;
          stream->packet_size = 20;
          stream->rate = 8000;
          stream->n_channels = 1;
          stream->sample_width = 16;
          stream->flavor = 1;
          stream->leaf_size = 0;
          stream->height = 0;
          break;
        case 4:
          stream->flavor = RMDEMUX_GUINT16_GET (data + offset + 22);
          stream->packet_size = RMDEMUX_GUINT32_GET (data + offset + 24);
          /* stream->frame_size = RMDEMUX_GUINT32_GET (data + offset + 42); */
          stream->leaf_size = RMDEMUX_GUINT16_GET (data + offset + 44);
          stream->height = RMDEMUX_GUINT16_GET (data + offset + 40);
          stream->rate = RMDEMUX_GUINT16_GET (data + offset + 48);
          stream->sample_width = RMDEMUX_GUINT16_GET (data + offset + 52);
          stream->n_channels = RMDEMUX_GUINT16_GET (data + offset + 54);
          stream->fourcc = RMDEMUX_FOURCC_GET (data + offset + 62);
          stream->extra_data_size = RMDEMUX_GUINT32_GET (data + offset + 69);
          GST_DEBUG_OBJECT (rmdemux, "%u bytes of extra codec data",
              stream->extra_data_size);
          if (length - (offset + 73) >= stream->extra_data_size) {
            stream->extra_data = (guint8 *) data + offset + 73;
          } else {
            GST_WARNING_OBJECT (rmdemux, "codec data runs beyond MDPR chunk");
            stream->extra_data_size = 0;
          }
          break;
        case 5:
          stream->flavor = RMDEMUX_GUINT16_GET (data + offset + 22);
          stream->packet_size = RMDEMUX_GUINT32_GET (data + offset + 24);
          /* stream->frame_size = RMDEMUX_GUINT32_GET (data + offset + 42); */
          stream->leaf_size = RMDEMUX_GUINT16_GET (data + offset + 44);
          stream->height = RMDEMUX_GUINT16_GET (data + offset + 40);
          stream->rate = RMDEMUX_GUINT16_GET (data + offset + 54);
          stream->sample_width = RMDEMUX_GUINT16_GET (data + offset + 58);
          stream->n_channels = RMDEMUX_GUINT16_GET (data + offset + 60);
          stream->fourcc = RMDEMUX_FOURCC_GET (data + offset + 66);
          stream->extra_data_size = RMDEMUX_GUINT32_GET (data + offset + 74);
          GST_DEBUG_OBJECT (rmdemux, "%u bytes of extra codec data",
              stream->extra_data_size);
          if (length - (offset + 78) >= stream->extra_data_size) {
            stream->extra_data = (guint8 *) data + offset + 78;
          } else {
            GST_WARNING_OBJECT (rmdemux, "codec data runs beyond MDPR chunk");
            stream->extra_data_size = 0;
          }
          break;
        default:{
          GST_WARNING_OBJECT (rmdemux, "Unhandled audio stream version %d",
              stream->version);
          break;
        }
      }
      /*  14_4, 28_8, cook, dnet, sipr, raac, racp, ralf, atrc */
      GST_DEBUG_OBJECT (rmdemux,
          "Audio stream with rate=%d sample_width=%d n_channels=%d",
          stream->rate, stream->sample_width, stream->n_channels);

      break;
    }
    case GST_RMDEMUX_STREAM_FILEINFO:
    {
      int element_nb;

      /* Length of this section */
      GST_DEBUG_OBJECT (rmdemux, "length2: 0x%08x",
          RMDEMUX_GUINT32_GET (data + offset));
      offset += 4;

      /* Unknown : 00 00 00 00 */
      offset += 4;

      /* Number of variables that would follow (loop iterations) */
      element_nb = RMDEMUX_GUINT32_GET (data + offset);
      offset += 4;

      while (element_nb) {
        /* Category Id : 00 00 00 XX 00 00 */
        offset += 6;

        /* Variable Name */
        offset += re_skip_pascal_string (data + offset);

        /* Variable Value Type */
        /*   00 00 00 00 00 => integer/boolean, preceded by length */
        /*   00 00 00 02 00 => pascal string, preceded by length, no trailing \0 */
        offset += 5;

        /* Variable Value */
        offset += re_skip_pascal_string (data + offset);

        element_nb--;
      }
    }
      break;
    case GST_RMDEMUX_STREAM_UNKNOWN:
    default:
      break;
  }

  gst_rmdemux_add_stream (rmdemux, stream);
}

static guint
gst_rmdemux_parse_indx (GstRMDemux * rmdemux, const guint8 * data, int length)
{
  int n;
  int id;

  n = RMDEMUX_GUINT32_GET (data);
  id = RMDEMUX_GUINT16_GET (data + 4);
  rmdemux->index_offset = RMDEMUX_GUINT32_GET (data + 6);

  GST_DEBUG_OBJECT (rmdemux, "Number of indices=%d Stream ID=%d length=%d", n,
      id, length);

  /* Point to the next index_stream */
  rmdemux->index_stream = gst_rmdemux_get_stream_by_id (rmdemux, id);

  /* Return the length of the index */
  return 14 * n;
}

static void
gst_rmdemux_parse_indx_data (GstRMDemux * rmdemux, const guint8 * data,
    int length)
{
  int i;
  int n;
  GstRMDemuxIndex *index;

  /* The number of index records */
  n = length / 14;

  if (rmdemux->index_stream == NULL)
    return;

  /* don't parse the index a second time when operating pull-based and
   * reaching the end of the file */
  if (rmdemux->index_stream->index_length > 0) {
    GST_DEBUG_OBJECT (rmdemux, "Already have an index for this stream");
    return;
  }

  index = g_malloc (sizeof (GstRMDemuxIndex) * n);
  rmdemux->index_stream->index = index;
  rmdemux->index_stream->index_length = n;

  for (i = 0; i < n; i++) {
    index[i].timestamp = RMDEMUX_GUINT32_GET (data + 2) * GST_MSECOND;
    index[i].offset = RMDEMUX_GUINT32_GET (data + 6);

    GST_DEBUG_OBJECT (rmdemux, "Index found for timestamp=%f (at offset=%x)",
        gst_guint64_to_gdouble (index[i].timestamp) / GST_SECOND,
        index[i].offset);
    data += 14;
  }
}

static void
gst_rmdemux_parse_data (GstRMDemux * rmdemux, const guint8 * data, int length)
{
  rmdemux->n_chunks = RMDEMUX_GUINT32_GET (data);
  rmdemux->data_offset = RMDEMUX_GUINT32_GET (data + 4);
  rmdemux->chunk_index = 0;
  GST_DEBUG_OBJECT (rmdemux, "Data chunk found with %d packets "
      "(next data at 0x%08x)", rmdemux->n_chunks, rmdemux->data_offset);
}

static void
gst_rmdemux_parse_cont (GstRMDemux * rmdemux, const guint8 * data, int length)
{
  GstTagList *tags;

  tags = gst_rm_utils_read_tags (data, length, gst_rm_utils_read_string16);
  if (tags) {
    gst_element_found_tags (GST_ELEMENT (rmdemux), tags);
  }
}

static GstFlowReturn
gst_rmdemux_combine_flows (GstRMDemux * rmdemux, GstRMDemuxStream * stream,
    GstFlowReturn ret)
{
  GSList *cur;

  /* store the value */
  stream->last_flow = ret;

  /* if it's success we can return the value right away */
  if (GST_FLOW_IS_SUCCESS (ret))
    goto done;

  /* any other error that is not-linked can be returned right
   * away */
  if (ret != GST_FLOW_NOT_LINKED)
    goto done;

  for (cur = rmdemux->streams; cur; cur = cur->next) {
    GstRMDemuxStream *ostream = cur->data;

    ret = ostream->last_flow;
    /* some other return value (must be SUCCESS but we can return
     * other values as well) */
    if (ret != GST_FLOW_NOT_LINKED)
      goto done;
  }
  /* if we get here, all other pads were unlinked and we return
   * NOT_LINKED then */
done:
  return ret;
}

static void
gst_rmdemux_stream_clear_cached_subpackets (GstRMDemux * rmdemux,
    GstRMDemuxStream * stream)
{
  if (stream->subpackets == NULL || stream->subpackets->len == 0)
    return;

  GST_DEBUG_OBJECT (rmdemux, "discarding %u previously collected subpackets",
      stream->subpackets->len);
  g_ptr_array_foreach (stream->subpackets, (GFunc) gst_mini_object_unref, NULL);
  g_ptr_array_set_size (stream->subpackets, 0);
}

static GstFlowReturn
gst_rmdemux_descramble_audio (GstRMDemux * rmdemux, GstRMDemuxStream * stream)
{
  GstFlowReturn ret;
  GstBuffer *outbuf;
  guint packet_size = stream->packet_size;
  guint height = stream->subpackets->len;
  guint leaf_size = stream->leaf_size;
  guint p, x;

  g_assert (stream->height == height);

  GST_LOG ("packet_size = %u, leaf_size = %u, height= %u", packet_size,
      leaf_size, height);

  ret = gst_pad_alloc_buffer_and_set_caps (stream->pad,
      GST_BUFFER_OFFSET_NONE, height * packet_size,
      GST_PAD_CAPS (stream->pad), &outbuf);

  if (ret != GST_FLOW_OK)
    goto done;

  for (p = 0; p < height; ++p) {
    GstBuffer *b = g_ptr_array_index (stream->subpackets, p);
    guint8 *b_data = GST_BUFFER_DATA (b);

    if (p == 0)
      GST_BUFFER_TIMESTAMP (outbuf) = GST_BUFFER_TIMESTAMP (b);

    for (x = 0; x < packet_size / leaf_size; ++x) {
      guint idx;

      idx = height * x + ((height + 1) / 2) * (p % 2) + (p / 2);
      /* GST_LOG ("%3u => %3u", (height * p) + x, idx); */
      memcpy (GST_BUFFER_DATA (outbuf) + leaf_size * idx, b_data, leaf_size);
      b_data += leaf_size;
    }
  }

  /* some decoders, such as realaudiodec, need to be fed in packet units */
  for (p = 0; p < height; ++p) {
    GstBuffer *subbuf;

    subbuf = gst_buffer_create_sub (outbuf, p * packet_size, packet_size);

    GST_LOG_OBJECT (rmdemux, "pushing buffer timestamp %" GST_TIME_FORMAT,
        GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (subbuf)));

    if (stream->discont) {
      GST_BUFFER_FLAG_SET (subbuf, GST_BUFFER_FLAG_DISCONT);
      stream->discont = FALSE;
    }

    gst_buffer_set_caps (subbuf, GST_PAD_CAPS (stream->pad));
    ret = gst_pad_push (stream->pad, subbuf);
    if (ret != GST_FLOW_OK)
      break;
  }

  gst_buffer_unref (outbuf);

done:

  gst_rmdemux_stream_clear_cached_subpackets (rmdemux, stream);

  return ret;
}

static GstFlowReturn
gst_rmdemux_descramble_dnet_audio (GstRMDemux * rmdemux,
    GstRMDemuxStream * stream)
{
  GstBuffer *buf;

  buf = g_ptr_array_index (stream->subpackets, 0);
  g_ptr_array_index (stream->subpackets, 0) = NULL;
  g_ptr_array_set_size (stream->subpackets, 0);

  buf = gst_rm_utils_descramble_dnet_buffer (buf);

  if (stream->discont) {
    GST_BUFFER_FLAG_SET (buf, GST_BUFFER_FLAG_DISCONT);
    stream->discont = FALSE;
  }
  return gst_pad_push (stream->pad, buf);
}

static GstFlowReturn
gst_rmdemux_descramble_mp4a_audio (GstRMDemux * rmdemux,
    GstRMDemuxStream * stream)
{
  GstFlowReturn res;
  GstBuffer *buf, *outbuf;
  guint frames, index, i;
  guint8 *data;
  GstClockTime timestamp;

  res = GST_FLOW_OK;

  buf = g_ptr_array_index (stream->subpackets, 0);
  g_ptr_array_index (stream->subpackets, 0) = NULL;
  g_ptr_array_set_size (stream->subpackets, 0);

  data = GST_BUFFER_DATA (buf);
  timestamp = GST_BUFFER_TIMESTAMP (buf);

  frames = (data[1] & 0xf0) >> 4;
  index = 2 * frames + 2;

  for (i = 0; i < frames; i++) {
    guint len = (data[i * 2 + 2] << 8) | data[i * 2 + 3];

    outbuf = gst_buffer_create_sub (buf, index, len);
    if (i == 0)
      GST_BUFFER_TIMESTAMP (outbuf) = timestamp;
    gst_buffer_set_caps (outbuf, GST_PAD_CAPS (stream->pad));

    index += len;

    if (stream->discont) {
      GST_BUFFER_FLAG_SET (buf, GST_BUFFER_FLAG_DISCONT);
      stream->discont = FALSE;
    }
    res = gst_pad_push (stream->pad, outbuf);
    if (res != GST_FLOW_OK)
      break;
  }
  gst_buffer_unref (buf);
  return res;
}

static GstFlowReturn
gst_rmdemux_descramble_sipr_audio (GstRMDemux * rmdemux,
    GstRMDemuxStream * stream)
{
  GstFlowReturn ret;
  GstBuffer *outbuf;
  guint packet_size = stream->packet_size;
  guint height = stream->subpackets->len;
  guint leaf_size = stream->leaf_size;
  guint p;

  g_assert (stream->height == height);

  GST_LOG ("packet_size = %u, leaf_size = %u, height= %u", packet_size,
      leaf_size, height);

  ret = gst_pad_alloc_buffer_and_set_caps (stream->pad,
      GST_BUFFER_OFFSET_NONE, height * packet_size,
      GST_PAD_CAPS (stream->pad), &outbuf);

  if (ret != GST_FLOW_OK)
    goto done;

  for (p = 0; p < height; ++p) {
    GstBuffer *b = g_ptr_array_index (stream->subpackets, p);
    guint8 *b_data = GST_BUFFER_DATA (b);

    if (p == 0)
      GST_BUFFER_TIMESTAMP (outbuf) = GST_BUFFER_TIMESTAMP (b);

    memcpy (GST_BUFFER_DATA (outbuf) + packet_size * p, b_data, packet_size);
  }

  GST_LOG_OBJECT (rmdemux, "pushing buffer timestamp %" GST_TIME_FORMAT,
      GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (outbuf)));

  if (stream->discont) {
    GST_BUFFER_FLAG_SET (outbuf, GST_BUFFER_FLAG_DISCONT);
    stream->discont = FALSE;
  }

  outbuf = gst_rm_utils_descramble_sipr_buffer (outbuf);

  gst_buffer_set_caps (outbuf, GST_PAD_CAPS (stream->pad));
  ret = gst_pad_push (stream->pad, outbuf);

done:

  gst_rmdemux_stream_clear_cached_subpackets (rmdemux, stream);

  return ret;
}

static GstFlowReturn
gst_rmdemux_handle_scrambled_packet (GstRMDemux * rmdemux,
    GstRMDemuxStream * stream, GstBuffer * buf, gboolean keyframe)
{
  GstFlowReturn ret;

  if (stream->subpackets == NULL)
    stream->subpackets = g_ptr_array_sized_new (stream->subpackets_needed);

  GST_LOG ("Got subpacket %u/%u, len=%u, key=%d", stream->subpackets->len + 1,
      stream->subpackets_needed, GST_BUFFER_SIZE (buf), keyframe);

  if (keyframe && stream->subpackets->len > 0) {
    gst_rmdemux_stream_clear_cached_subpackets (rmdemux, stream);
  }

  g_ptr_array_add (stream->subpackets, buf);

  if (stream->subpackets->len < stream->subpackets_needed)
    return GST_FLOW_OK;

  g_assert (stream->subpackets->len >= 1);

  switch (stream->fourcc) {
    case GST_RM_AUD_DNET:
      ret = gst_rmdemux_descramble_dnet_audio (rmdemux, stream);
      break;
    case GST_RM_AUD_COOK:
    case GST_RM_AUD_ATRC:
      ret = gst_rmdemux_descramble_audio (rmdemux, stream);
      break;
    case GST_RM_AUD_RAAC:
    case GST_RM_AUD_RACP:
      ret = gst_rmdemux_descramble_mp4a_audio (rmdemux, stream);
      break;
    case GST_RM_AUD_SIPR:
      ret = gst_rmdemux_descramble_sipr_audio (rmdemux, stream);
      break;
    default:
      g_assert_not_reached ();
  }

  return ret;
}

static GstClockTime
gst_rmdemux_fix_timestamp (GstRMDemux * rmdemux, GstRMDemuxStream * stream,
    guint8 * data, GstClockTime timestamp)
{
  guint8 frame_type;
  guint16 seq;
  GstClockTime ts = timestamp;

  if (timestamp == GST_CLOCK_TIME_NONE)
    goto done;

  /* only adjust when we have a stream with B frames */
  if (stream->format < 0x20200002)
    goto done;

  /* Fix timestamp. */
  switch (stream->fourcc) {
    case GST_RM_VDO_RV10:
      goto done;
    case GST_RM_VDO_RV20:
    {
      /*
       * Bit  1- 2: frame type
       * Bit  3- 9: ?
       * Bit 10-22: sequence number
       * Bit 23-32: ?
       */
      frame_type = (data[0] >> 6) & 0x03;
      seq = ((data[1] & 0x7f) << 6) + ((data[2] & 0xfc) >> 2);
      break;
    }
    case GST_RM_VDO_RV30:
    {
      /*
       * Bit  1- 2: ?
       * Bit     3: skip packet if 1
       * Bit  4- 5: frame type
       * Bit  6-12: ?
       * Bit 13-25: sequence number
       * Bit 26-32: ?
       */
      frame_type = (data[0] >> 3) & 0x03;
      seq = ((data[1] & 0x0f) << 9) + (data[2] << 1) + ((data[3] & 0x80) >> 7);
      break;
    }
    case GST_RM_VDO_RV40:
    {
      /*
       * Bit     1: skip packet if 1
       * Bit  2- 3: frame type
       * Bit  4-13: ?
       * Bit 14-26: sequence number
       * Bit 27-32: ?
       */
      frame_type = (data[0] >> 5) & 0x03;
      seq = ((data[1] & 0x07) << 10) + (data[2] << 2) + ((data[3] & 0xc0) >> 6);
      break;
    }
    default:
      goto unknown_version;
  }

  switch (frame_type) {
    case 0:
    case 1:
    {
      GST_LOG_OBJECT (rmdemux, "I frame %d", frame_type);
      /* I frame */
      if (stream->next_ts == -1)
        stream->next_ts = timestamp;
      else
        timestamp = stream->next_ts;
      stream->last_ts = stream->next_ts;
      stream->next_ts = ts;
      stream->last_seq = stream->next_seq;
      stream->next_seq = seq;
      break;
    }
    case 2:
    {
      GST_LOG_OBJECT (rmdemux, "P frame");
      /* P frame */
      timestamp = stream->last_ts = stream->next_ts;
      if (seq < stream->next_seq)
        stream->next_ts += (seq + 0x2000 - stream->next_seq) * GST_MSECOND;
      else
        stream->next_ts += (seq - stream->next_seq) * GST_MSECOND;
      stream->last_seq = stream->next_seq;
      stream->next_seq = seq;
      break;
    }
    case 3:
    {
      GST_LOG_OBJECT (rmdemux, "B frame");
      /* B frame */
      if (seq < stream->last_seq) {
        timestamp =
            (seq + 0x2000 - stream->last_seq) * GST_MSECOND + stream->last_ts;
      } else {
        timestamp = (seq - stream->last_seq) * GST_MSECOND + stream->last_ts;
      }
      break;
    }
    default:
      goto unknown_frame_type;
  }

done:
  GST_LOG_OBJECT (rmdemux,
      "timestamp %" GST_TIME_FORMAT " -> %" GST_TIME_FORMAT, GST_TIME_ARGS (ts),
      GST_TIME_ARGS (timestamp));

  return timestamp;

  /* Errors */
unknown_version:
  {
    GST_ELEMENT_ERROR (rmdemux, STREAM, DECODE,
        ("Unknown version: %i.", stream->version), (NULL));
    return GST_FLOW_ERROR;
  }

unknown_frame_type:
  {
    GST_ELEMENT_ERROR (rmdemux, STREAM, DECODE, ("Unknown frame type %d.",
            frame_type), (NULL));
    return GST_FLOW_ERROR;
  }
}

#define PARSE_NUMBER(data, size, number, label) \
G_STMT_START {                                  \
  if (size < 2)                                 \
    goto label;                                 \
  number = GST_READ_UINT16_BE (data);           \
  if (!(number & 0xc000)) {                     \
    if (size < 4)                               \
      goto label;                               \
    number = GST_READ_UINT32_BE (data);         \
    data += 4;                                  \
    size -= 4;                                  \
  } else {                                      \
    number &= 0x3fff;                           \
    data += 2;                                  \
    size -= 2;                                  \
  }                                             \
} G_STMT_END

static GstFlowReturn
gst_rmdemux_parse_video_packet (GstRMDemux * rmdemux, GstRMDemuxStream * stream,
    GstBuffer * in, guint offset, guint16 version,
    GstClockTime timestamp, gboolean key)
{
  GstFlowReturn ret;
  const guint8 *data, *base;
  guint size;

  base = GST_BUFFER_DATA (in);
  data = base + offset;
  size = GST_BUFFER_SIZE (in) - offset;
  /* if size <= 2, we want this method to return the same GstFlowReturn as it
   * was previously for that given stream. */
  ret = stream->last_flow;

  while (size > 2) {
    guint8 pkg_header;
    guint pkg_offset;
    guint pkg_length;
    guint pkg_subseq = 0, pkg_seqnum = G_MAXUINT;
    guint fragment_size;
    GstBuffer *fragment;

    pkg_header = *data++;
    size--;

    /* packet header
     * bit 7: 1=last block in block chain
     * bit 6: 1=short header (only one block?)
     */
    if ((pkg_header & 0xc0) == 0x40) {
      /* skip unknown byte */
      data++;
      size--;
      pkg_offset = 0;
      pkg_length = size;
    } else {
      if ((pkg_header & 0x40) == 0) {
        pkg_subseq = (*data++) & 0x7f;
        size--;
      } else {
        pkg_subseq = 0;
      }

      /* length */
      PARSE_NUMBER (data, size, pkg_length, not_enough_data);

      /* offset */
      PARSE_NUMBER (data, size, pkg_offset, not_enough_data);

      /* seqnum */
      if (size < 1)
        goto not_enough_data;

      pkg_seqnum = *data++;
      size--;
    }

    GST_DEBUG_OBJECT (rmdemux,
        "seq %d, subseq %d, offset %d, length %d, size %d, header %02x",
        pkg_seqnum, pkg_subseq, pkg_offset, pkg_length, size, pkg_header);

    /* calc size of fragment */
    if ((pkg_header & 0xc0) == 0x80) {
      fragment_size = pkg_offset;
    } else {
      if ((pkg_header & 0xc0) == 0)
        fragment_size = size;
      else
        fragment_size = pkg_length;
    }
    GST_DEBUG_OBJECT (rmdemux, "fragment size %d", fragment_size);

    /* get the fragment */
    fragment = gst_buffer_create_sub (in, data - base, fragment_size);

    if (pkg_subseq == 1) {
      GST_DEBUG_OBJECT (rmdemux, "start new fragment");
      gst_adapter_clear (stream->adapter);
      stream->frag_current = 0;
      stream->frag_count = 0;
      stream->frag_length = pkg_length;
    } else if (pkg_subseq == 0) {
      GST_DEBUG_OBJECT (rmdemux, "non fragmented packet");
      stream->frag_current = 0;
      stream->frag_count = 0;
      stream->frag_length = fragment_size;
    }

    /* put fragment in adapter */
    gst_adapter_push (stream->adapter, fragment);
    stream->frag_offset[stream->frag_count] = stream->frag_current;
    stream->frag_current += fragment_size;
    stream->frag_count++;

    if (stream->frag_count > MAX_FRAGS)
      goto too_many_fragments;

    GST_DEBUG_OBJECT (rmdemux, "stored fragment in adapter %d/%d",
        stream->frag_current, stream->frag_length);

    /* flush fragment when complete */
    if (stream->frag_current >= stream->frag_length) {
      GstBuffer *out;
      guint8 *outdata;
      guint header_size;
      gint i, avail;

      /* calculate header size, which is:
       * 1 byte for the number of fragments - 1
       * for each fragment:
       *   4 bytes 0x00000001 little endian
       *   4 bytes fragment offset
       *
       * This is also the matroska header for realvideo, the decoder needs the
       * fragment offsets, both in ffmpeg and real .so, so we just give it that
       * in front of the data.
       */
      header_size = 1 + (8 * (stream->frag_count));

      GST_DEBUG_OBJECT (rmdemux,
          "fragmented completed. count %d, header_size %u", stream->frag_count,
          header_size);

      avail = gst_adapter_available (stream->adapter);

      out = gst_buffer_new_and_alloc (header_size + avail);
      outdata = GST_BUFFER_DATA (out);

      /* create header */
      *outdata++ = stream->frag_count - 1;
      for (i = 0; i < stream->frag_count; i++) {
        GST_WRITE_UINT32_LE (outdata, 0x00000001);
        outdata += 4;
        GST_WRITE_UINT32_LE (outdata, stream->frag_offset[i]);
        outdata += 4;
      }

      /* copy packet data after the header now */
      gst_adapter_copy (stream->adapter, outdata, 0, avail);
      gst_adapter_flush (stream->adapter, avail);

      stream->frag_current = 0;
      stream->frag_count = 0;
      stream->frag_length = 0;

      gst_buffer_set_caps (out, GST_PAD_CAPS (stream->pad));

      if (timestamp != -1) {
        if (rmdemux->first_ts != -1 && timestamp > rmdemux->first_ts)
          timestamp -= rmdemux->first_ts;
        else
          timestamp = 0;

        if (rmdemux->base_ts != -1)
          timestamp += rmdemux->base_ts;
      }
      timestamp =
          gst_rmdemux_fix_timestamp (rmdemux, stream, outdata, timestamp);

      GST_BUFFER_TIMESTAMP (out) = timestamp;

      GST_LOG_OBJECT (rmdemux, "pushing timestamp %" GST_TIME_FORMAT,
          GST_TIME_ARGS (timestamp));

      if (stream->discont) {
        GST_BUFFER_FLAG_SET (out, GST_BUFFER_FLAG_DISCONT);
        stream->discont = FALSE;
      }

      ret = gst_pad_push (stream->pad, out);
      ret = gst_rmdemux_combine_flows (rmdemux, stream, ret);
      if (ret != GST_FLOW_OK)
        break;

      timestamp = GST_CLOCK_TIME_NONE;
    }
    data += fragment_size;
    size -= fragment_size;
  }
  GST_DEBUG_OBJECT (rmdemux, "%d bytes left", size);

  gst_buffer_unref (in);

  return ret;

  /* ERRORS */
not_enough_data:
  {
    GST_ELEMENT_WARNING (rmdemux, STREAM, DECODE, ("Skipping bad packet."),
        (NULL));
    gst_buffer_unref (in);
    return GST_FLOW_OK;
  }
too_many_fragments:
  {
    GST_ELEMENT_ERROR (rmdemux, STREAM, DECODE,
        ("Got more fragments (%u) than can be handled (%u)",
            stream->frag_count, MAX_FRAGS), (NULL));
    gst_buffer_unref (in);
    return GST_FLOW_ERROR;
  }
}

static GstFlowReturn
gst_rmdemux_parse_audio_packet (GstRMDemux * rmdemux, GstRMDemuxStream * stream,
    GstBuffer * in, guint offset, guint16 version,
    GstClockTime timestamp, gboolean key)
{
  GstFlowReturn ret, cret;
  GstBuffer *buffer;
  const guint8 *data;
  guint size;

  data = GST_BUFFER_DATA (in) + offset;
  size = GST_BUFFER_SIZE (in) - offset;

  ret = gst_pad_alloc_buffer_and_set_caps (stream->pad,
      GST_BUFFER_OFFSET_NONE, size, GST_PAD_CAPS (stream->pad), &buffer);

  cret = gst_rmdemux_combine_flows (rmdemux, stream, ret);
  if (ret != GST_FLOW_OK)
    goto alloc_failed;

  memcpy (GST_BUFFER_DATA (buffer), (guint8 *) data, size);

  if (rmdemux->first_ts != -1 && timestamp > rmdemux->first_ts)
    timestamp -= rmdemux->first_ts;
  else
    timestamp = 0;

  if (rmdemux->base_ts != -1)
    timestamp += rmdemux->base_ts;

  GST_BUFFER_TIMESTAMP (buffer) = timestamp;

  if (stream->needs_descrambling) {
    GST_LOG_OBJECT (rmdemux, "descramble timestamp %" GST_TIME_FORMAT,
        GST_TIME_ARGS (timestamp));
    ret = gst_rmdemux_handle_scrambled_packet (rmdemux, stream, buffer, key);
  } else {
    GST_LOG_OBJECT (rmdemux,
        "Pushing buffer of size %d, timestamp %" GST_TIME_FORMAT "to pad %s",
        GST_BUFFER_SIZE (buffer), GST_TIME_ARGS (timestamp),
        GST_PAD_NAME (stream->pad));

    if (stream->discont) {
      GST_BUFFER_FLAG_SET (buffer, GST_BUFFER_FLAG_DISCONT);
      stream->discont = FALSE;
    }
    ret = gst_pad_push (stream->pad, buffer);
  }

  gst_buffer_unref (in);

  return ret;

  /* ERRORS */
alloc_failed:
  {
    GST_DEBUG_OBJECT (rmdemux, "pad alloc returned %d", ret);
    gst_buffer_unref (in);
    return cret;
  }
}

static GstFlowReturn
gst_rmdemux_parse_packet (GstRMDemux * rmdemux, GstBuffer * in, guint16 version)
{
  guint16 id;
  GstRMDemuxStream *stream;
  guint size;
  GstFlowReturn cret, ret;
  GstClockTime timestamp;
  gboolean key;
  guint8 *data, *base;
  guint8 flags;
  guint32 ts;

  base = data = GST_BUFFER_DATA (in);
  size = GST_BUFFER_SIZE (in);

  /* stream number */
  id = RMDEMUX_GUINT16_GET (data);

  stream = gst_rmdemux_get_stream_by_id (rmdemux, id);
  if (!stream || !stream->pad)
    goto unknown_stream;

  /* timestamp in Msec */
  ts = RMDEMUX_GUINT32_GET (data + 2);
  timestamp = ts * GST_MSECOND;

  gst_segment_set_last_stop (&rmdemux->segment, GST_FORMAT_TIME, timestamp);

  GST_LOG_OBJECT (rmdemux, "Parsing a packet for stream=%d, timestamp=%"
      GST_TIME_FORMAT ", size %u, version=%d, ts=%u", id,
      GST_TIME_ARGS (timestamp), size, version, ts);

  if (rmdemux->first_ts == GST_CLOCK_TIME_NONE) {
    GST_DEBUG_OBJECT (rmdemux, "First timestamp: %" GST_TIME_FORMAT,
        GST_TIME_ARGS (timestamp));
    rmdemux->first_ts = timestamp;
  }

  /* skip stream_id and timestamp */
  data += (2 + 4);
  size -= (2 + 4);

  /* get flags */
  flags = GST_READ_UINT8 (data + 1);

  data += 2;
  size -= 2;

  /* version 1 has an extra byte */
  if (version == 1) {
    data += 1;
    size -= 1;
  }
  key = (flags & 0x02) != 0;
  GST_DEBUG_OBJECT (rmdemux, "flags %d, Keyframe %d", flags, key);

  if (rmdemux->need_newsegment) {
    GstEvent *event;

    event = gst_event_new_new_segment (FALSE, rmdemux->segment.rate,
        rmdemux->segment.format, rmdemux->segment.start,
        rmdemux->segment.stop, rmdemux->segment.time);

    GST_DEBUG_OBJECT (rmdemux, "sending NEWSEGMENT event, segment.start= %"
        GST_TIME_FORMAT, GST_TIME_ARGS (rmdemux->segment.start));

    gst_rmdemux_send_event (rmdemux, event);
    rmdemux->need_newsegment = FALSE;
  }

  if (stream->pending_tags != NULL) {
    GST_LOG_OBJECT (stream->pad, "tags %" GST_PTR_FORMAT, stream->pending_tags);
    gst_element_found_tags_for_pad (GST_ELEMENT_CAST (rmdemux), stream->pad,
        stream->pending_tags);
    stream->pending_tags = NULL;
  }

  if ((rmdemux->offset + size) <= stream->seek_offset) {
    GST_DEBUG_OBJECT (rmdemux,
        "Stream %d is skipping: seek_offset=%d, offset=%d, size=%u",
        stream->id, stream->seek_offset, rmdemux->offset, size);
    cret = GST_FLOW_OK;
    goto beach;
  }

  /* do special headers */
  if (stream->subtype == GST_RMDEMUX_STREAM_VIDEO) {
    ret =
        gst_rmdemux_parse_video_packet (rmdemux, stream, in, data - base,
        version, timestamp, key);
  } else if (stream->subtype == GST_RMDEMUX_STREAM_AUDIO) {
    ret =
        gst_rmdemux_parse_audio_packet (rmdemux, stream, in, data - base,
        version, timestamp, key);
  } else
    ret = GST_FLOW_OK;

  cret = gst_rmdemux_combine_flows (rmdemux, stream, ret);

beach:
  return cret;

  /* ERRORS */
unknown_stream:
  {
    GST_WARNING_OBJECT (rmdemux, "No stream for stream id %d in parsing "
        "data packet", id);
    return GST_FLOW_OK;
  }
}

gboolean
gst_rmdemux_plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "rmdemux",
      GST_RANK_PRIMARY, GST_TYPE_RMDEMUX);
}
