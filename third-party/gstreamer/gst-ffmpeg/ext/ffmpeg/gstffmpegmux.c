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
#ifdef HAVE_FFMPEG_UNINSTALLED
#include <avformat.h>
#else
#include <ffmpeg/avformat.h>
#endif

#include <gst/gst.h>
#include <gst/base/gstcollectpads.h>

#include "gstffmpeg.h"
#include "gstffmpegcodecmap.h"

typedef struct _GstFFMpegMux GstFFMpegMux;
typedef struct _GstFFMpegMuxPad GstFFMpegMuxPad;

struct _GstFFMpegMuxPad
{
  GstCollectData collect;       /* we extend the CollectData */

  gint padnum;
};

struct _GstFFMpegMux
{
  GstElement element;

  GstCollectPads *collect;
  /* We need to keep track of our pads, so we do so here. */
  GstPad *srcpad;

  AVFormatContext *context;
  gboolean opened;

  GstTagList *tags;

  gint videopads, audiopads;

  /*< private > */
  /* event_function is the collectpads default eventfunction */
  GstPadEventFunction event_function;
  /* WHISPERCAST BEGIN:
   * We encode using the stream time instead of sample time.
  */
  GstClockTime reference;
  /* WHISPERCAST END */
};

typedef struct _GstFFMpegMuxClassParams
{
  AVOutputFormat *in_plugin;
  GstCaps *srccaps, *videosinkcaps, *audiosinkcaps;
} GstFFMpegMuxClassParams;

typedef struct _GstFFMpegMuxClass GstFFMpegMuxClass;

struct _GstFFMpegMuxClass
{
  GstElementClass parent_class;

  AVOutputFormat *in_plugin;
};

#define GST_TYPE_FFMPEGMUX \
  (gst_ffmpegdec_get_type())
#define GST_FFMPEGMUX(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_FFMPEGMUX,GstFFMpegMux))
#define GST_FFMPEGMUX_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_FFMPEGMUX,GstFFMpegMuxClass))
#define GST_IS_FFMPEGMUX(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_FFMPEGMUX))
#define GST_IS_FFMPEGMUX_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_FFMPEGMUX))

enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  ARG_0,
  /* FILL ME */
};

/* A number of function prototypes are given so we can refer to them later. */
static void gst_ffmpegmux_class_init (GstFFMpegMuxClass * klass);
static void gst_ffmpegmux_base_init (gpointer g_class);
static void gst_ffmpegmux_init (GstFFMpegMux * ffmpegmux,
    GstFFMpegMuxClass * g_class);
static void gst_ffmpegmux_finalize (GObject * object);

static gboolean gst_ffmpegmux_setcaps (GstPad * pad, GstCaps * caps);
static GstPad *gst_ffmpegmux_request_new_pad (GstElement * element,
    GstPadTemplate * templ, const gchar * name);
static GstFlowReturn gst_ffmpegmux_collected (GstCollectPads * pads,
    gpointer user_data);

static gboolean gst_ffmpegmux_sink_event (GstPad * pad, GstEvent * event);

static GstStateChangeReturn gst_ffmpegmux_change_state (GstElement * element,
    GstStateChange transition);

#define GST_FFMUX_PARAMS_QDATA g_quark_from_static_string("ffmux-params")

static GstElementClass *parent_class = NULL;

/*static guint gst_ffmpegmux_signals[LAST_SIGNAL] = { 0 }; */

static void
gst_ffmpegmux_base_init (gpointer g_class)
{
  GstFFMpegMuxClass *klass = (GstFFMpegMuxClass *) g_class;
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);
  GstElementDetails details;
  GstFFMpegMuxClassParams *params;
  GstPadTemplate *videosinktempl, *audiosinktempl, *srctempl;

  params =
      (GstFFMpegMuxClassParams *) g_type_get_qdata (G_OBJECT_CLASS_TYPE (klass),
      GST_FFMUX_PARAMS_QDATA);
  g_assert (params != NULL);

  /* construct the element details struct */
  details.longname = g_strdup_printf ("FFMPEG %s Muxer",
      params->in_plugin->name);
  details.klass = g_strdup ("Codec/Muxer");
  details.description = g_strdup_printf ("FFMPEG %s Muxer",
      params->in_plugin->name);
  details.author = "Wim Taymans <wim.taymans@chello.be>, "
      "Ronald Bultje <rbultje@ronald.bitfreak.net>";
  gst_element_class_set_details (element_class, &details);
  g_free (details.longname);
  g_free (details.klass);
  g_free (details.description);

  /* pad templates */
  srctempl = gst_pad_template_new ("src", GST_PAD_SRC,
      GST_PAD_ALWAYS, params->srccaps);
  gst_element_class_add_pad_template (element_class, srctempl);

  if (params->audiosinkcaps) {
    audiosinktempl = gst_pad_template_new ("audio_%d",
        GST_PAD_SINK, GST_PAD_REQUEST, params->audiosinkcaps);
    gst_element_class_add_pad_template (element_class, audiosinktempl);
  }

  if (params->videosinkcaps) {
    videosinktempl = gst_pad_template_new ("video_%d",
        GST_PAD_SINK, GST_PAD_REQUEST, params->videosinkcaps);
    gst_element_class_add_pad_template (element_class, videosinktempl);
  }

  klass->in_plugin = params->in_plugin;
}

static void
gst_ffmpegmux_class_init (GstFFMpegMuxClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  parent_class = g_type_class_peek_parent (klass);

  gstelement_class->request_new_pad = gst_ffmpegmux_request_new_pad;
  gstelement_class->change_state = gst_ffmpegmux_change_state;
  gobject_class->finalize = gst_ffmpegmux_finalize;
}

static void
gst_ffmpegmux_init (GstFFMpegMux * ffmpegmux, GstFFMpegMuxClass * g_class)
{
  GstElementClass *klass = GST_ELEMENT_CLASS (g_class);
  GstFFMpegMuxClass *oclass = (GstFFMpegMuxClass *) klass;
  GstPadTemplate *templ = gst_element_class_get_pad_template (klass, "src");

  ffmpegmux->srcpad = gst_pad_new_from_template (templ, "src");
  gst_element_add_pad (GST_ELEMENT (ffmpegmux), ffmpegmux->srcpad);

  ffmpegmux->collect = gst_collect_pads_new ();
  gst_collect_pads_set_function (ffmpegmux->collect,
      (GstCollectPadsFunction) gst_ffmpegmux_collected, ffmpegmux);

  ffmpegmux->context = g_new0 (AVFormatContext, 1);
  ffmpegmux->context->oformat = oclass->in_plugin;
  ffmpegmux->context->nb_streams = 0;
  g_snprintf (ffmpegmux->context->filename,
      sizeof (ffmpegmux->context->filename),
      "gstreamer://%p", ffmpegmux->srcpad);
  ffmpegmux->opened = FALSE;

  ffmpegmux->videopads = 0;
  ffmpegmux->audiopads = 0;

  ffmpegmux->tags = NULL;
}

static void
gst_ffmpegmux_finalize (GObject * object)
{
  GstFFMpegMux *ffmpegmux = (GstFFMpegMux *) object;

  g_free (ffmpegmux->context);
  gst_object_unref (ffmpegmux->collect);

  if (G_OBJECT_CLASS (parent_class)->finalize)
    G_OBJECT_CLASS (parent_class)->finalize (object);
}

static GstPad *
gst_ffmpegmux_request_new_pad (GstElement * element,
    GstPadTemplate * templ, const gchar * name)
{
  GstFFMpegMux *ffmpegmux = (GstFFMpegMux *) element;
  GstElementClass *klass = GST_ELEMENT_GET_CLASS (element);
  GstFFMpegMuxClass *oclass = (GstFFMpegMuxClass *) klass;
  GstFFMpegMuxPad *collect_pad;
  gchar *padname;
  GstPad *pad;
  AVStream *st;
  enum CodecType type;
  gint bitrate = 0, framesize = 0;

  g_return_val_if_fail (templ != NULL, NULL);
  g_return_val_if_fail (templ->direction == GST_PAD_SINK, NULL);
  g_return_val_if_fail (ffmpegmux->opened == FALSE, NULL);

  /* figure out a name that *we* like */
  if (templ == gst_element_class_get_pad_template (klass, "video_%d")) {
    padname = g_strdup_printf ("video_%d", ffmpegmux->videopads++);
    type = CODEC_TYPE_VIDEO;
    bitrate = 64 * 1024;
    framesize = 1152;
  } else if (templ == gst_element_class_get_pad_template (klass, "audio_%d")) {
    padname = g_strdup_printf ("audio_%d", ffmpegmux->audiopads++);
    type = CODEC_TYPE_AUDIO;
    bitrate = 285 * 1024;
  } else {
    g_warning ("ffmux: unknown pad template!");
    return NULL;
  }

  /* create pad */
  pad = gst_pad_new_from_template (templ, padname);
  collect_pad = (GstFFMpegMuxPad *)
      gst_collect_pads_add_pad (ffmpegmux->collect, pad,
      sizeof (GstFFMpegMuxPad));
  collect_pad->padnum = ffmpegmux->context->nb_streams;

  /* small hack to put our own event pad function and chain up to collect pad */
  ffmpegmux->event_function = GST_PAD_EVENTFUNC (pad);
  gst_pad_set_event_function (pad,
      GST_DEBUG_FUNCPTR (gst_ffmpegmux_sink_event));

  gst_pad_set_setcaps_function (pad, GST_DEBUG_FUNCPTR (gst_ffmpegmux_setcaps));
  gst_element_add_pad (element, pad);

  /* AVStream needs to be created */
  st = av_new_stream (ffmpegmux->context, collect_pad->padnum);
  st->codec->codec_type = type;
  st->codec->codec_id = CODEC_ID_NONE;  /* this is a check afterwards */
  st->stream_copy = 1;          /* we're not the actual encoder */
  st->codec->bit_rate = bitrate;
  st->codec->frame_size = framesize;
  /* we fill in codec during capsnego */

  /* we love debug output (c) (tm) (r) */
  GST_DEBUG ("Created %s pad for ffmux_%s element",
      padname, oclass->in_plugin->name);
  g_free (padname);

  return pad;
}

/**
 * gst_ffmpegmux_setcaps
 * @pad: #GstPad
 * @caps: New caps.
 *
 * Set caps to pad.
 *
 * Returns: #TRUE on success.
 */
static gboolean
gst_ffmpegmux_setcaps (GstPad * pad, GstCaps * caps)
{
  GstFFMpegMux *ffmpegmux = (GstFFMpegMux *) (gst_pad_get_parent (pad));
  GstFFMpegMuxPad *collect_pad;
  AVStream *st;

  collect_pad = (GstFFMpegMuxPad *) gst_pad_get_element_private (pad);

  st = ffmpegmux->context->streams[collect_pad->padnum];

  /* for the format-specific guesses, we'll go to
   * our famous codec mapper */
  if (gst_ffmpeg_caps_to_codecid (caps, st->codec) != CODEC_ID_NONE) {
    GST_LOG_OBJECT (pad, "accepted caps %" GST_PTR_FORMAT, caps);
    return TRUE;
  }

  GST_LOG_OBJECT (pad, "rejecting caps %" GST_PTR_FORMAT, caps);
  return FALSE;
}


static gboolean
gst_ffmpegmux_sink_event (GstPad * pad, GstEvent * event)
{
  GstFFMpegMux *ffmpegmux = (GstFFMpegMux *) gst_pad_get_parent (pad);
  gboolean res = TRUE;

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_TAG:{
      GstTagList *taglist = NULL;

      gst_event_parse_tag (event, &taglist);
      ffmpegmux->tags = gst_tag_list_merge (ffmpegmux->tags, taglist,
          GST_TAG_MERGE_PREPEND);

      break;
    }
    default:
      break;
  }

  /* chaining up to collectpads default event function */
  res = ffmpegmux->event_function (pad, event);

  gst_object_unref (ffmpegmux);
  return res;
}

static GstFlowReturn
gst_ffmpegmux_collected (GstCollectPads * pads, gpointer user_data)
{
  GstFFMpegMux *ffmpegmux = (GstFFMpegMux *) user_data;
  GSList *collected;
  GstFFMpegMuxPad *best_pad;
  GstClockTime best_time;
  const GstTagList *iface_tags;

  /* open "file" (gstreamer protocol to next element) */
  if (!ffmpegmux->opened) {
    int open_flags = URL_WRONLY;

    /* we do need all streams to have started capsnego,
     * or things will go horribly wrong */
    for (collected = ffmpegmux->collect->data; collected;
        collected = g_slist_next (collected)) {
      GstFFMpegMuxPad *collect_pad = (GstFFMpegMuxPad *) collected->data;
      AVStream *st = ffmpegmux->context->streams[collect_pad->padnum];

      /* check whether the pad has successfully completed capsnego */
      if (st->codec->codec_id == CODEC_ID_NONE) {
        GST_ELEMENT_ERROR (ffmpegmux, CORE, NEGOTIATION, (NULL),
            ("no caps set on stream %d (%s)", collect_pad->padnum,
                (st->codec->codec_type == CODEC_TYPE_VIDEO) ?
                "video" : "audio"));
        return GST_FLOW_ERROR;
      }
      /* set framerate for audio */
      if (st->codec->codec_type == CODEC_TYPE_AUDIO) {
        switch (st->codec->codec_id) {
          case CODEC_ID_PCM_S16LE:
          case CODEC_ID_PCM_S16BE:
          case CODEC_ID_PCM_U16LE:
          case CODEC_ID_PCM_U16BE:
          case CODEC_ID_PCM_S8:
          case CODEC_ID_PCM_U8:
            st->codec->frame_size = 1;
            break;
          default:
          {
            GstBuffer *buffer;

            /* FIXME : This doesn't work for RAW AUDIO...
             * in fact I'm wondering if it even works for any kind of audio... */
            buffer = gst_collect_pads_peek (ffmpegmux->collect,
                (GstCollectData *) collect_pad);
            if (buffer) {
              st->codec->frame_size =
                  st->codec->sample_rate *
                  GST_BUFFER_DURATION (buffer) / GST_SECOND;
              gst_buffer_unref (buffer);
            }
          }
        }
      }
    }

    /* tags */
    iface_tags = gst_tag_setter_get_tag_list (GST_TAG_SETTER (ffmpegmux));
    if (ffmpegmux->tags || iface_tags) {
      GstTagList *tags;
      gint i;
      gchar *s;

      tags = gst_tag_list_merge (iface_tags, ffmpegmux->tags,
          GST_TAG_MERGE_APPEND);

      /* get the interesting ones */
      if (gst_tag_list_get_string (tags, GST_TAG_TITLE, &s)) {
        strncpy (ffmpegmux->context->title, s,
            sizeof (ffmpegmux->context->title));
      }
      if (gst_tag_list_get_string (tags, GST_TAG_ARTIST, &s)) {
        strncpy (ffmpegmux->context->author, s,
            sizeof (ffmpegmux->context->author));
      }
      if (gst_tag_list_get_string (tags, GST_TAG_COPYRIGHT, &s)) {
        strncpy (ffmpegmux->context->copyright, s,
            sizeof (ffmpegmux->context->copyright));
      }
      if (gst_tag_list_get_string (tags, GST_TAG_COMMENT, &s)) {
        strncpy (ffmpegmux->context->comment, s,
            sizeof (ffmpegmux->context->comment));
      }
      if (gst_tag_list_get_string (tags, GST_TAG_ALBUM, &s)) {
        strncpy (ffmpegmux->context->album, s,
            sizeof (ffmpegmux->context->album));
      }
      if (gst_tag_list_get_string (tags, GST_TAG_GENRE, &s)) {
        strncpy (ffmpegmux->context->genre, s,
            sizeof (ffmpegmux->context->genre));
      }
      if (gst_tag_list_get_int (tags, GST_TAG_TRACK_NUMBER, &i)) {
        ffmpegmux->context->track = i;
      }
      gst_tag_list_free (tags);
    }

    /* set the streamheader flag for gstffmpegprotocol if codec supports it */
    if (!strcmp (ffmpegmux->context->oformat->name, "flv")) {
      open_flags |= GST_FFMPEG_URL_STREAMHEADER;
    }

    if (url_fopen (&ffmpegmux->context->pb,
            ffmpegmux->context->filename, open_flags) < 0) {
      GST_ELEMENT_ERROR (ffmpegmux, LIBRARY, TOO_LAZY, (NULL),
          ("Failed to open stream context in ffmux"));
      return GST_FLOW_ERROR;
    }

    if (av_set_parameters (ffmpegmux->context, NULL) < 0) {
      GST_ELEMENT_ERROR (ffmpegmux, LIBRARY, INIT, (NULL),
          ("Failed to initialize muxer"));
      return GST_FLOW_ERROR;
    }

    /* now open the mux format */
    if (av_write_header (ffmpegmux->context) < 0) {
      GST_ELEMENT_ERROR (ffmpegmux, LIBRARY, SETTINGS, (NULL),
          ("Failed to write file header - check codec settings"));
      return GST_FLOW_ERROR;
    }

    /* we're now opened */
    ffmpegmux->opened = TRUE;

    /* flush the header so it will be used as streamheader */
    put_flush_packet (ffmpegmux->context->pb);
  }

  /* take the one with earliest timestamp,
   * and push it forward */
  best_pad = NULL;
  best_time = GST_CLOCK_TIME_NONE;
  for (collected = ffmpegmux->collect->data; collected;
      collected = g_slist_next (collected)) {
    GstFFMpegMuxPad *collect_pad = (GstFFMpegMuxPad *) collected->data;
    GstBuffer *buffer = gst_collect_pads_peek (ffmpegmux->collect,
        (GstCollectData *) collect_pad);

    /* if there's no buffer, just continue */
    if (buffer == NULL) {
      continue;
    }

    /* if we have no buffer yet, just use the first one */
    if (best_pad == NULL) {
      best_pad = collect_pad;
      best_time = GST_BUFFER_TIMESTAMP (buffer);
      goto next_pad;
    }

    /* if we do have one, only use this one if it's older */
    if (GST_BUFFER_TIMESTAMP (buffer) < best_time) {
      best_time = GST_BUFFER_TIMESTAMP (buffer);
      best_pad = collect_pad;
    }

  next_pad:
    gst_buffer_unref (buffer);

    /* Mux buffers with invalid timestamp first */
    if (!GST_CLOCK_TIME_IS_VALID (best_time))
      break;
  }

  /* now handle the buffer, or signal EOS if we have
   * no buffers left */
  if (best_pad != NULL) {
    GstBuffer *buf;
    AVPacket pkt;
    gboolean need_free = FALSE;

    /* push out current buffer */
    buf = gst_collect_pads_pop (ffmpegmux->collect,
        (GstCollectData *) best_pad);

    ffmpegmux->context->streams[best_pad->padnum]->codec->frame_number++;

    /* set time */
    /* WHISPERCAST BEGIN:
     * We encode using the stream time instead of sample time.
    */
    /* WHISPERCAST ORIGINAL
    pkt.pts = gst_ffmpeg_time_gst_to_ff (GST_BUFFER_TIMESTAMP (buf),
        ffmpegmux->context->streams[best_pad->padnum]->time_base);
    */
    if (ffmpegmux->reference == GST_CLOCK_TIME_NONE) {
      ffmpegmux->reference = GST_BUFFER_TIMESTAMP (buf);
    }
    pkt.pts = gst_ffmpeg_time_gst_to_ff (GST_BUFFER_TIMESTAMP (buf) - ffmpegmux->reference,
        ffmpegmux->context->streams[best_pad->padnum]->time_base);
    /* WHISPERCAST END */
    pkt.dts = pkt.pts;

    if (strcmp (ffmpegmux->context->oformat->name, "gif") == 0) {
      AVStream *st = ffmpegmux->context->streams[best_pad->padnum];
      AVPicture src, dst;

      need_free = TRUE;
      pkt.size = st->codec->width * st->codec->height * 3;
      pkt.data = g_malloc (pkt.size);

      dst.data[0] = pkt.data;
      dst.data[1] = NULL;
      dst.data[2] = NULL;
      dst.linesize[0] = st->codec->width * 3;

      gst_ffmpeg_avpicture_fill (&src, GST_BUFFER_DATA (buf),
          PIX_FMT_RGB24, st->codec->width, st->codec->height);

      av_picture_copy (&dst, &src, PIX_FMT_RGB24,
          st->codec->width, st->codec->height);
    } else {
      pkt.data = GST_BUFFER_DATA (buf);
      pkt.size = GST_BUFFER_SIZE (buf);
    }

    pkt.stream_index = best_pad->padnum;
    pkt.flags = 0;

    if (!GST_BUFFER_FLAG_IS_SET (buf, GST_BUFFER_FLAG_DELTA_UNIT))
      pkt.flags |= PKT_FLAG_KEY;

    if (GST_BUFFER_DURATION_IS_VALID (buf))
      pkt.duration =
          gst_ffmpeg_time_gst_to_ff (GST_BUFFER_DURATION (buf),
              ffmpegmux->context->streams[best_pad->padnum]->time_base);
    else
      pkt.duration = 0;
    av_write_frame (ffmpegmux->context, &pkt);
    gst_buffer_unref (buf);
    if (need_free)
      g_free (pkt.data);
  } else {
    /* close down */
    av_write_trailer (ffmpegmux->context);
    ffmpegmux->opened = FALSE;
    put_flush_packet (ffmpegmux->context->pb);
    url_fclose (ffmpegmux->context->pb);
    gst_pad_push_event (ffmpegmux->srcpad, gst_event_new_eos ());
    return GST_FLOW_UNEXPECTED;
  }

  return GST_FLOW_OK;
}

static GstStateChangeReturn
gst_ffmpegmux_change_state (GstElement * element, GstStateChange transition)
{
  GstFlowReturn ret;
  GstFFMpegMux *ffmpegmux = (GstFFMpegMux *) (element);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      /* WHISPERCAST BEGIN
       * We encode using the stream time instead of sample time.
      */
      ffmpegmux->reference = GST_CLOCK_TIME_NONE;
      /* WHISPERCAST END */
      gst_collect_pads_start (ffmpegmux->collect);
      break;
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      gst_collect_pads_stop (ffmpegmux->collect);
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      if (ffmpegmux->tags) {
        gst_tag_list_free (ffmpegmux->tags);
        ffmpegmux->tags = NULL;
      }
      if (ffmpegmux->opened) {
        ffmpegmux->opened = FALSE;
        url_fclose (ffmpegmux->context->pb);
      }
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      break;
    default:
      break;
  }

  return ret;
}

GstCaps *
gst_ffmpegmux_get_id_caps (enum CodecID * id_list)
{
  GstCaps *caps, *t;
  gint i;

  caps = gst_caps_new_empty ();
  for (i = 0; id_list[i] != CODEC_ID_NONE; i++) {
    if ((t = gst_ffmpeg_codecid_to_caps (id_list[i], NULL, TRUE)))
      gst_caps_append (caps, t);
  }

  return caps;
}

/* set a list of integer values on the caps, e.g. for sample rates */
static void
gst_ffmpeg_mux_simple_caps_set_int_list (GstCaps * caps, const gchar * field,
    guint num, const gint * values)
{
  GValue list = { 0, };
  GValue val = { 0, };
  gint i;

  g_return_if_fail (GST_CAPS_IS_SIMPLE (caps));

  g_value_init (&list, GST_TYPE_LIST);
  g_value_init (&val, G_TYPE_INT);

  for (i = 0; i < num; ++i) {
    g_value_set_int (&val, values[i]);
    gst_value_list_append_value (&list, &val);
  }

  gst_structure_set_value (gst_caps_get_structure (caps, 0), field, &list);

  g_value_unset (&val);
  g_value_unset (&list);
}

gboolean
gst_ffmpegmux_register (GstPlugin * plugin)
{
  GTypeInfo typeinfo = {
    sizeof (GstFFMpegMuxClass),
    (GBaseInitFunc) gst_ffmpegmux_base_init,
    NULL,
    (GClassInitFunc) gst_ffmpegmux_class_init,
    NULL,
    NULL,
    sizeof (GstFFMpegMux),
    0,
    (GInstanceInitFunc) gst_ffmpegmux_init,
  };
  static const GInterfaceInfo tag_setter_info = {
    NULL, NULL, NULL
  };
  GType type;
  AVOutputFormat *in_plugin;
  GstFFMpegMuxClassParams *params;

  in_plugin = first_oformat;

  GST_LOG ("Registering muxers");

  while (in_plugin) {
    gchar *type_name;
    gchar *p;
    GstCaps *srccaps, *audiosinkcaps, *videosinkcaps;
    enum CodecID *video_ids = NULL, *audio_ids = NULL;

    /* Try to find the caps that belongs here */
    srccaps = gst_ffmpeg_formatid_to_caps (in_plugin->name);
    if (!srccaps) {
      GST_WARNING ("Couldn't get source caps for muxer %s", in_plugin->name);
      goto next;
    }
    if (!gst_ffmpeg_formatid_get_codecids (in_plugin->name,
            &video_ids, &audio_ids)) {
      gst_caps_unref (srccaps);
      GST_WARNING
          ("Couldn't get sink caps for muxer %s. Most likely because no input format mapping exists.",
          in_plugin->name);
      goto next;
    }
    videosinkcaps = video_ids ? gst_ffmpegmux_get_id_caps (video_ids) : NULL;
    audiosinkcaps = audio_ids ? gst_ffmpegmux_get_id_caps (audio_ids) : NULL;

    /* construct the type */
    type_name = g_strdup_printf ("ffmux_%s", in_plugin->name);

    p = type_name;

    while (*p) {
      if (*p == '.')
        *p = '_';
      p++;
    }

    /* if it's already registered, drop it */
    if (g_type_from_name (type_name)) {
      g_free (type_name);
      gst_caps_unref (srccaps);
      if (audiosinkcaps)
        gst_caps_unref (audiosinkcaps);
      if (videosinkcaps)
        gst_caps_unref (videosinkcaps);
      goto next;
    }

    /* fix up allowed caps for some muxers */
    if (strcmp (in_plugin->name, "flv") == 0) {
      /* WHISPERCAST ORIGINAL: Do we really need this ?
       * Even if we did, gst_ffmpeg_mux_simple_caps_set_int_list()
       * must be hacked to accomodate s set of caps...
      const gint rates[] = { 44100, 22050, 11025 };

      gst_ffmpeg_mux_simple_caps_set_int_list (audiosinkcaps, "rate", 3, rates);
      */
    } else if (strcmp (in_plugin->name, "gif") == 0) {
      if (videosinkcaps)
        gst_caps_unref (videosinkcaps);

      videosinkcaps =
          gst_caps_from_string ("video/x-raw-rgb, bpp=(int)24, depth=(int)24");
    }

    /* create a cache for these properties */
    params = g_new0 (GstFFMpegMuxClassParams, 1);
    params->in_plugin = in_plugin;
    params->srccaps = srccaps;
    params->videosinkcaps = videosinkcaps;
    params->audiosinkcaps = audiosinkcaps;

    /* create the type now */
    type = g_type_register_static (GST_TYPE_ELEMENT, type_name, &typeinfo, 0);
    g_type_set_qdata (type, GST_FFMUX_PARAMS_QDATA, (gpointer) params);
    g_type_add_interface_static (type, GST_TYPE_TAG_SETTER, &tag_setter_info);

    if (!gst_element_register (plugin, type_name, GST_RANK_NONE, type)) {
      g_free (type_name);
      gst_caps_unref (srccaps);
      if (audiosinkcaps)
        gst_caps_unref (audiosinkcaps);
      if (videosinkcaps)
        gst_caps_unref (videosinkcaps);
      return FALSE;
    }

    g_free (type_name);

  next:
    in_plugin = in_plugin->next;
  }

  GST_LOG ("Finished registering muxers");

  return TRUE;
}
