/*
 *  GStreamer pulseaudio plugin
 *
 *  Copyright (c) 2004-2008 Lennart Poettering
 *
 *  gst-pulse is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU Lesser General Public License as
 *  published by the Free Software Foundation; either version 2.1 of the
 *  License, or (at your option) any later version.
 *
 *  gst-pulse is distributed in the hope that it will be useful, but
 *  WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with gst-pulse; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
 *  USA.
 */

/**
 * SECTION:element-pulsesink
 * @short_description: Output audio to a PulseAudio sound server
 * @see_also: pulsesrc, pulsemixer
 *
 * <refsect2>
 * <para>
 * This element outputs audio to a PulseAudio sound server.
 * </para>
 * <title>Example pipelines</title>
 * <para>
 * <programlisting>
 * gst-launch -v filesrc location=sine.ogg ! oggdemux ! vorbisdec ! audioconvert ! audioresample ! pulsesink
 * </programlisting>
 * Play an Ogg/Vorbis file.
 * </para>
 * <para>
 * <programlisting>
 * gst-launch -v audiotestsrc ! audioconvert ! volume volume=0.4 ! pulsesink
 * </programlisting>
 * Play a 440Hz sine wave.
 * </para>
 * </refsect2>
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>
#include <stdio.h>

#include <gst/base/gstbasesink.h>
#include <gst/gsttaglist.h>

#include "pulsesink.h"
#include "pulseutil.h"

GST_DEBUG_CATEGORY_EXTERN (pulse_debug);
#define GST_CAT_DEFAULT pulse_debug

enum
{
  PROP_SERVER = 1,
  PROP_DEVICE,
};

static GstAudioSinkClass *parent_class = NULL;

static void gst_pulsesink_destroy_stream (GstPulseSink * pulsesink);

static void gst_pulsesink_destroy_context (GstPulseSink * pulsesink);

static void gst_pulsesink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_pulsesink_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static void gst_pulsesink_finalize (GObject * object);

static void gst_pulsesink_dispose (GObject * object);

static gboolean gst_pulsesink_open (GstAudioSink * asink);

static gboolean gst_pulsesink_close (GstAudioSink * asink);

static gboolean gst_pulsesink_prepare (GstAudioSink * asink,
    GstRingBufferSpec * spec);
static gboolean gst_pulsesink_unprepare (GstAudioSink * asink);

static guint gst_pulsesink_write (GstAudioSink * asink, gpointer data,
    guint length);
static guint gst_pulsesink_delay (GstAudioSink * asink);

static void gst_pulsesink_reset (GstAudioSink * asink);

static gboolean gst_pulsesink_event (GstBaseSink * sink, GstEvent * event);

#if (G_BYTE_ORDER == G_LITTLE_ENDIAN)
# define ENDIANNESS   "LITTLE_ENDIAN, BIG_ENDIAN"
#else
# define ENDIANNESS   "BIG_ENDIAN, LITTLE_ENDIAN"
#endif

static void
gst_pulsesink_base_init (gpointer g_class)
{

  static GstStaticPadTemplate pad_template = GST_STATIC_PAD_TEMPLATE ("sink",
      GST_PAD_SINK,
      GST_PAD_ALWAYS,
      GST_STATIC_CAPS ("audio/x-raw-int, "
          "endianness = (int) { " ENDIANNESS " }, "
          "signed = (boolean) TRUE, "
          "width = (int) 16, "
          "depth = (int) 16, "
          "rate = (int) [ 1, MAX ], "
          "channels = (int) [ 1, 16 ];"
          "audio/x-raw-float, "
          "endianness = (int) { " ENDIANNESS " }, "
          "width = (int) 32, "
          "rate = (int) [ 1, MAX ], "
          "channels = (int) [ 1, 16 ];"
          "audio/x-raw-int, "
          "endianness = (int) { " ENDIANNESS " }, "
          "signed = (boolean) TRUE, "
          "width = (int) 32, "
          "depth = (int) 32, "
          "rate = (int) [ 1, MAX ], "
          "channels = (int) [ 1, 16 ];"
          "audio/x-raw-int, "
          "signed = (boolean) FALSE, "
          "width = (int) 8, "
          "depth = (int) 8, "
          "rate = (int) [ 1, MAX ], "
          "channels = (int) [ 1, 16 ];"
          "audio/x-alaw, "
          "rate = (int) [ 1, MAX], "
          "channels = (int) [ 1, 16 ];"
          "audio/x-mulaw, "
          "rate = (int) [ 1, MAX], " "channels = (int) [ 1, 16 ]")
      );

  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_set_details_simple (element_class,
      "PulseAudio Audio Sink",
      "Sink/Audio", "Plays audio to a PulseAudio server", "Lennart Poettering");
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&pad_template));
}

static void
gst_pulsesink_class_init (gpointer g_class, gpointer class_data)
{

  GObjectClass *gobject_class = G_OBJECT_CLASS (g_class);

  GstBaseSinkClass *gstbasesink_class = GST_BASE_SINK_CLASS (g_class);

  GstAudioSinkClass *gstaudiosink_class = GST_AUDIO_SINK_CLASS (g_class);

  parent_class = g_type_class_peek_parent (g_class);

  gobject_class->dispose = GST_DEBUG_FUNCPTR (gst_pulsesink_dispose);
  gobject_class->finalize = GST_DEBUG_FUNCPTR (gst_pulsesink_finalize);
  gobject_class->set_property = GST_DEBUG_FUNCPTR (gst_pulsesink_set_property);
  gobject_class->get_property = GST_DEBUG_FUNCPTR (gst_pulsesink_get_property);

  gstbasesink_class->event = GST_DEBUG_FUNCPTR (gst_pulsesink_event);

  gstaudiosink_class->open = GST_DEBUG_FUNCPTR (gst_pulsesink_open);
  gstaudiosink_class->close = GST_DEBUG_FUNCPTR (gst_pulsesink_close);
  gstaudiosink_class->prepare = GST_DEBUG_FUNCPTR (gst_pulsesink_prepare);
  gstaudiosink_class->unprepare = GST_DEBUG_FUNCPTR (gst_pulsesink_unprepare);
  gstaudiosink_class->write = GST_DEBUG_FUNCPTR (gst_pulsesink_write);
  gstaudiosink_class->delay = GST_DEBUG_FUNCPTR (gst_pulsesink_delay);
  gstaudiosink_class->reset = GST_DEBUG_FUNCPTR (gst_pulsesink_reset);

  /* Overwrite GObject fields */
  g_object_class_install_property (gobject_class,
      PROP_SERVER,
      g_param_spec_string ("server", "Server",
          "The PulseAudio server to connect to", NULL,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_DEVICE,
      g_param_spec_string ("device", "Sink",
          "The PulseAudio sink device to connect to", NULL,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
}

static void
gst_pulsesink_init (GTypeInstance * instance, gpointer g_class)
{

  GstPulseSink *pulsesink = GST_PULSESINK (instance);

  int e;

  pulsesink->server = pulsesink->device = pulsesink->stream_name = NULL;

  pulsesink->context = NULL;
  pulsesink->stream = NULL;

  pulsesink->mainloop = pa_threaded_mainloop_new ();
  g_assert (pulsesink->mainloop);

  e = pa_threaded_mainloop_start (pulsesink->mainloop);
  g_assert (e == 0);
}

static void
gst_pulsesink_destroy_stream (GstPulseSink * pulsesink)
{
  if (pulsesink->stream) {
    pa_stream_disconnect (pulsesink->stream);
    pa_stream_unref (pulsesink->stream);
    pulsesink->stream = NULL;
  }

  g_free (pulsesink->stream_name);
  pulsesink->stream_name = NULL;
}

static void
gst_pulsesink_destroy_context (GstPulseSink * pulsesink)
{

  gst_pulsesink_destroy_stream (pulsesink);

  if (pulsesink->context) {
    pa_context_disconnect (pulsesink->context);
    pa_context_unref (pulsesink->context);
    pulsesink->context = NULL;
  }
}

static void
gst_pulsesink_finalize (GObject * object)
{
  GstPulseSink *pulsesink = GST_PULSESINK (object);

  pa_threaded_mainloop_stop (pulsesink->mainloop);

  gst_pulsesink_destroy_context (pulsesink);

  g_free (pulsesink->server);
  g_free (pulsesink->device);
  g_free (pulsesink->stream_name);

  pa_threaded_mainloop_free (pulsesink->mainloop);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_pulsesink_dispose (GObject * object)
{
  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
gst_pulsesink_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec)
{
  GstPulseSink *pulsesink = GST_PULSESINK (object);

  switch (prop_id) {
    case PROP_SERVER:
      g_free (pulsesink->server);
      pulsesink->server = g_value_dup_string (value);
      break;

    case PROP_DEVICE:
      g_free (pulsesink->device);
      pulsesink->device = g_value_dup_string (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_pulsesink_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec)
{

  GstPulseSink *pulsesink = GST_PULSESINK (object);

  switch (prop_id) {
    case PROP_SERVER:
      g_value_set_string (value, pulsesink->server);
      break;

    case PROP_DEVICE:
      g_value_set_string (value, pulsesink->device);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_pulsesink_context_state_cb (pa_context * c, void *userdata)
{
  GstPulseSink *pulsesink = GST_PULSESINK (userdata);

  switch (pa_context_get_state (c)) {
    case PA_CONTEXT_READY:
    case PA_CONTEXT_TERMINATED:
    case PA_CONTEXT_FAILED:
      pa_threaded_mainloop_signal (pulsesink->mainloop, 0);
      break;

    case PA_CONTEXT_UNCONNECTED:
    case PA_CONTEXT_CONNECTING:
    case PA_CONTEXT_AUTHORIZING:
    case PA_CONTEXT_SETTING_NAME:
      break;
  }
}

static void
gst_pulsesink_stream_state_cb (pa_stream * s, void *userdata)
{
  GstPulseSink *pulsesink = GST_PULSESINK (userdata);

  switch (pa_stream_get_state (s)) {

    case PA_STREAM_READY:
    case PA_STREAM_FAILED:
    case PA_STREAM_TERMINATED:
      pa_threaded_mainloop_signal (pulsesink->mainloop, 0);
      break;

    case PA_STREAM_UNCONNECTED:
    case PA_STREAM_CREATING:
      break;
  }
}

static void
gst_pulsesink_stream_request_cb (pa_stream * s, size_t length, void *userdata)
{
  GstPulseSink *pulsesink = GST_PULSESINK (userdata);

  pa_threaded_mainloop_signal (pulsesink->mainloop, 0);
}

static void
gst_pulsesink_stream_latency_update_cb (pa_stream * s, void *userdata)
{
  GstPulseSink *pulsesink = GST_PULSESINK (userdata);

  pa_threaded_mainloop_signal (pulsesink->mainloop, 0);
}

static gboolean
gst_pulsesink_open (GstAudioSink * asink)
{
  GstPulseSink *pulsesink = GST_PULSESINK (asink);

  gchar *name = gst_pulse_client_name ();

  pa_threaded_mainloop_lock (pulsesink->mainloop);

  if (!(pulsesink->context =
          pa_context_new (pa_threaded_mainloop_get_api (pulsesink->mainloop),
              name))) {
    GST_ELEMENT_ERROR (pulsesink, RESOURCE, FAILED,
        ("Failed to create context"), (NULL));
    goto unlock_and_fail;
  }

  pa_context_set_state_callback (pulsesink->context,
      gst_pulsesink_context_state_cb, pulsesink);

  if (pa_context_connect (pulsesink->context, pulsesink->server, 0, NULL) < 0) {
    GST_ELEMENT_ERROR (pulsesink, RESOURCE, FAILED, ("Failed to connect: %s",
            pa_strerror (pa_context_errno (pulsesink->context))), (NULL));
    goto unlock_and_fail;
  }

  /* Wait until the context is ready */
  pa_threaded_mainloop_wait (pulsesink->mainloop);

  if (pa_context_get_state (pulsesink->context) != PA_CONTEXT_READY) {
    GST_ELEMENT_ERROR (pulsesink, RESOURCE, FAILED, ("Failed to connect: %s",
            pa_strerror (pa_context_errno (pulsesink->context))), (NULL));
    goto unlock_and_fail;
  }

  pa_threaded_mainloop_unlock (pulsesink->mainloop);
  g_free (name);
  return TRUE;

unlock_and_fail:

  pa_threaded_mainloop_unlock (pulsesink->mainloop);
  g_free (name);
  return FALSE;
}

static gboolean
gst_pulsesink_close (GstAudioSink * asink)
{
  GstPulseSink *pulsesink = GST_PULSESINK (asink);

  pa_threaded_mainloop_lock (pulsesink->mainloop);
  gst_pulsesink_destroy_context (pulsesink);
  pa_threaded_mainloop_unlock (pulsesink->mainloop);

  return TRUE;
}

static gboolean
gst_pulsesink_prepare (GstAudioSink * asink, GstRingBufferSpec * spec)
{
  pa_buffer_attr buf_attr;

  pa_channel_map channel_map;

  GstPulseSink *pulsesink = GST_PULSESINK (asink);

  if (!gst_pulse_fill_sample_spec (spec, &pulsesink->sample_spec)) {
    GST_ELEMENT_ERROR (pulsesink, RESOURCE, SETTINGS,
        ("Invalid sample specification."), (NULL));
    goto unlock_and_fail;
  }

  pa_threaded_mainloop_lock (pulsesink->mainloop);

  if (!pulsesink->context
      || pa_context_get_state (pulsesink->context) != PA_CONTEXT_READY) {
    GST_ELEMENT_ERROR (pulsesink, RESOURCE, FAILED, ("Bad context state: %s",
            pulsesink->
            context ? pa_strerror (pa_context_errno (pulsesink->context)) :
            NULL), (NULL));
    goto unlock_and_fail;
  }

  if (!(pulsesink->stream = pa_stream_new (pulsesink->context,
              pulsesink->
              stream_name ? pulsesink->stream_name : "Playback Stream",
              &pulsesink->sample_spec,
              gst_pulse_gst_to_channel_map (&channel_map, spec)))) {
    GST_ELEMENT_ERROR (pulsesink, RESOURCE, FAILED,
        ("Failed to create stream: %s",
            pa_strerror (pa_context_errno (pulsesink->context))), (NULL));
    goto unlock_and_fail;
  }

  pa_stream_set_state_callback (pulsesink->stream,
      gst_pulsesink_stream_state_cb, pulsesink);
  pa_stream_set_write_callback (pulsesink->stream,
      gst_pulsesink_stream_request_cb, pulsesink);
  pa_stream_set_latency_update_callback (pulsesink->stream,
      gst_pulsesink_stream_latency_update_cb, pulsesink);

  memset (&buf_attr, 0, sizeof (buf_attr));
  buf_attr.tlength = spec->segtotal * spec->segsize;
  buf_attr.maxlength = buf_attr.tlength * 2;
  buf_attr.prebuf = buf_attr.tlength - spec->segsize;
  buf_attr.minreq = spec->segsize;

  if (pa_stream_connect_playback (pulsesink->stream, pulsesink->device,
          &buf_attr,
          PA_STREAM_INTERPOLATE_TIMING | PA_STREAM_AUTO_TIMING_UPDATE |
          PA_STREAM_NOT_MONOTONOUS, NULL, NULL) < 0) {
    GST_ELEMENT_ERROR (pulsesink, RESOURCE, FAILED,
        ("Failed to connect stream: %s",
            pa_strerror (pa_context_errno (pulsesink->context))), (NULL));
    goto unlock_and_fail;
  }

  /* Wait until the stream is ready */
  pa_threaded_mainloop_wait (pulsesink->mainloop);

  if (pa_stream_get_state (pulsesink->stream) != PA_STREAM_READY) {
    GST_ELEMENT_ERROR (pulsesink, RESOURCE, FAILED,
        ("Failed to connect stream: %s",
            pa_strerror (pa_context_errno (pulsesink->context))), (NULL));
    goto unlock_and_fail;
  }

  pa_threaded_mainloop_unlock (pulsesink->mainloop);

  spec->bytes_per_sample = pa_frame_size (&pulsesink->sample_spec);
  memset (spec->silence_sample, 0, spec->bytes_per_sample);

  return TRUE;

unlock_and_fail:

  pa_threaded_mainloop_unlock (pulsesink->mainloop);
  return FALSE;
}

static gboolean
gst_pulsesink_unprepare (GstAudioSink * asink)
{
  GstPulseSink *pulsesink = GST_PULSESINK (asink);

  pa_threaded_mainloop_lock (pulsesink->mainloop);
  gst_pulsesink_destroy_stream (pulsesink);
  pa_threaded_mainloop_unlock (pulsesink->mainloop);

  return TRUE;
}

#define CHECK_DEAD_GOTO(pulsesink, label) \
if (!(pulsesink)->context || pa_context_get_state((pulsesink)->context) != PA_CONTEXT_READY || \
    !(pulsesink)->stream || pa_stream_get_state((pulsesink)->stream) != PA_STREAM_READY) { \
    GST_ELEMENT_ERROR((pulsesink), RESOURCE, FAILED, ("Disconnected: %s", (pulsesink)->context ? pa_strerror(pa_context_errno((pulsesink)->context)) : NULL), (NULL)); \
    goto label; \
}

static guint
gst_pulsesink_write (GstAudioSink * asink, gpointer data, guint length)
{
  GstPulseSink *pulsesink = GST_PULSESINK (asink);

  size_t sum = 0;

  pa_threaded_mainloop_lock (pulsesink->mainloop);

  while (length > 0) {
    size_t l;

    for (;;) {
      CHECK_DEAD_GOTO (pulsesink, unlock_and_fail);

      if ((l = pa_stream_writable_size (pulsesink->stream)) == (size_t) - 1) {
        GST_ELEMENT_ERROR (pulsesink, RESOURCE, FAILED,
            ("pa_stream_writable_size() failed: %s",
                pa_strerror (pa_context_errno (pulsesink->context))), (NULL));
        goto unlock_and_fail;
      }

      if (l > 0)
        break;

      pa_threaded_mainloop_wait (pulsesink->mainloop);
    }

    if (l > length)
      l = length;

    if (pa_stream_write (pulsesink->stream, data, l, NULL, 0,
            PA_SEEK_RELATIVE) < 0) {
      GST_ELEMENT_ERROR (pulsesink, RESOURCE, FAILED,
          ("pa_stream_write() failed: %s",
              pa_strerror (pa_context_errno (pulsesink->context))), (NULL));
      goto unlock_and_fail;
    }

    data = (guint8 *) data + l;
    length -= l;

    sum += l;
  }

  pa_threaded_mainloop_unlock (pulsesink->mainloop);

  return sum;

unlock_and_fail:

  pa_threaded_mainloop_unlock (pulsesink->mainloop);
  return 0;
}

static guint
gst_pulsesink_delay (GstAudioSink * asink)
{
  GstPulseSink *pulsesink = GST_PULSESINK (asink);

  pa_usec_t t;

  pa_threaded_mainloop_lock (pulsesink->mainloop);

  for (;;) {
    CHECK_DEAD_GOTO (pulsesink, unlock_and_fail);

    if (pa_stream_get_latency (pulsesink->stream, &t, NULL) >= 0)
      break;

    if (pa_context_errno (pulsesink->context) != PA_ERR_NODATA) {
      GST_ELEMENT_ERROR (pulsesink, RESOURCE, FAILED,
          ("pa_stream_get_latency() failed: %s",
              pa_strerror (pa_context_errno (pulsesink->context))), (NULL));
      goto unlock_and_fail;
    }

    pa_threaded_mainloop_wait (pulsesink->mainloop);
  }

  pa_threaded_mainloop_unlock (pulsesink->mainloop);

  return gst_util_uint64_scale_int (t, pulsesink->sample_spec.rate, 1000000LL);

unlock_and_fail:

  pa_threaded_mainloop_unlock (pulsesink->mainloop);
  return 0;
}

static void
gst_pulsesink_success_cb (pa_stream * s, int success, void *userdata)
{
  GstPulseSink *pulsesink = GST_PULSESINK (userdata);

  pulsesink->operation_success = success;
  pa_threaded_mainloop_signal (pulsesink->mainloop, 0);
}

static void
gst_pulsesink_reset (GstAudioSink * asink)
{
  GstPulseSink *pulsesink = GST_PULSESINK (asink);

  pa_operation *o = NULL;

  pa_threaded_mainloop_lock (pulsesink->mainloop);

  CHECK_DEAD_GOTO (pulsesink, unlock_and_fail);

  if (!(o =
          pa_stream_flush (pulsesink->stream, gst_pulsesink_success_cb,
              pulsesink))) {
    GST_ELEMENT_ERROR (pulsesink, RESOURCE, FAILED,
        ("pa_stream_flush() failed: %s",
            pa_strerror (pa_context_errno (pulsesink->context))), (NULL));
    goto unlock_and_fail;
  }

  pulsesink->operation_success = 0;
  while (pa_operation_get_state (o) != PA_OPERATION_DONE) {
    CHECK_DEAD_GOTO (pulsesink, unlock_and_fail);

    pa_threaded_mainloop_wait (pulsesink->mainloop);
  }

  if (!pulsesink->operation_success) {
    GST_ELEMENT_ERROR (pulsesink, RESOURCE, FAILED, ("Flush failed: %s",
            pa_strerror (pa_context_errno (pulsesink->context))), (NULL));
    goto unlock_and_fail;
  }

unlock_and_fail:

  if (o) {
    pa_operation_cancel (o);
    pa_operation_unref (o);
  }

  pa_threaded_mainloop_unlock (pulsesink->mainloop);
}

static void
gst_pulsesink_change_title (GstPulseSink * pulsesink, const gchar * t)
{
  pa_operation *o = NULL;

  pa_threaded_mainloop_lock (pulsesink->mainloop);

  g_free (pulsesink->stream_name);
  pulsesink->stream_name = g_strdup (t);

  if (!(pulsesink)->context
      || pa_context_get_state ((pulsesink)->context) != PA_CONTEXT_READY
      || !(pulsesink)->stream
      || pa_stream_get_state ((pulsesink)->stream) != PA_STREAM_READY) {
    goto unlock_and_fail;
  }

  if (!(o =
          pa_stream_set_name (pulsesink->stream, pulsesink->stream_name, NULL,
              pulsesink))) {
    GST_ELEMENT_ERROR (pulsesink, RESOURCE, FAILED,
        ("pa_stream_set_name() failed: %s",
            pa_strerror (pa_context_errno (pulsesink->context))), (NULL));
    goto unlock_and_fail;
  }

  /* We're not interested if this operation failed or not */

unlock_and_fail:

  if (o)
    pa_operation_unref (o);

  pa_threaded_mainloop_unlock (pulsesink->mainloop);
}

static gboolean
gst_pulsesink_event (GstBaseSink * sink, GstEvent * event)
{
  GstPulseSink *pulsesink = GST_PULSESINK (sink);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_TAG:{
      gchar *title = NULL, *artist = NULL, *location = NULL, *description =
          NULL, *t = NULL, *buf = NULL;
      GstTagList *l;

      gst_event_parse_tag (event, &l);

      gst_tag_list_get_string (l, GST_TAG_TITLE, &title);
      gst_tag_list_get_string (l, GST_TAG_ARTIST, &artist);
      gst_tag_list_get_string (l, GST_TAG_LOCATION, &location);
      gst_tag_list_get_string (l, GST_TAG_DESCRIPTION, &description);

      if (title && artist)
        t = buf =
            g_strdup_printf ("'%s' by '%s'", g_strstrip (title),
            g_strstrip (artist));
      else if (title)
        t = g_strstrip (title);
      else if (description)
        t = g_strstrip (description);
      else if (location)
        t = g_strstrip (location);

      if (t)
        gst_pulsesink_change_title (pulsesink, t);

      g_free (title);
      g_free (artist);
      g_free (location);
      g_free (description);
      g_free (buf);

      break;
    }
    default:
      ;
  }

  return GST_BASE_SINK_CLASS (parent_class)->event (sink, event);
}

GType
gst_pulsesink_get_type (void)
{
  static GType pulsesink_type = 0;

  if (!pulsesink_type) {

    static const GTypeInfo pulsesink_info = {
      sizeof (GstPulseSinkClass),
      gst_pulsesink_base_init,
      NULL,
      gst_pulsesink_class_init,
      NULL,
      NULL,
      sizeof (GstPulseSink),
      0,
      gst_pulsesink_init,
    };

    pulsesink_type = g_type_register_static (GST_TYPE_AUDIO_SINK,
        "GstPulseSink", &pulsesink_info, 0);
  }

  return pulsesink_type;
}
