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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>

#include "pulsemixerctrl.h"
#include "pulsemixertrack.h"
#include "pulseutil.h"

GST_DEBUG_CATEGORY_EXTERN (pulse_debug);
#define GST_CAT_DEFAULT pulse_debug

static void
gst_pulsemixer_ctrl_context_state_cb (pa_context * context, void *userdata)
{
  GstPulseMixerCtrl *c = GST_PULSEMIXER_CTRL (userdata);

  /* Called from the background thread! */

  switch (pa_context_get_state (context)) {
    case PA_CONTEXT_READY:
    case PA_CONTEXT_TERMINATED:
    case PA_CONTEXT_FAILED:
      pa_threaded_mainloop_signal (c->mainloop, 0);
      break;

    case PA_CONTEXT_UNCONNECTED:
    case PA_CONTEXT_CONNECTING:
    case PA_CONTEXT_AUTHORIZING:
    case PA_CONTEXT_SETTING_NAME:
      break;
  }
}

static void
gst_pulsemixer_ctrl_sink_info_cb (pa_context * context, const pa_sink_info * i,
    int eol, void *userdata)
{
  GstPulseMixerCtrl *c = userdata;

  /* Called from the background thread! */

  if (c->outstandig_queries > 0)
    c->outstandig_queries--;

  if (c->ignore_queries > 0 || c->time_event) {

    if (c->ignore_queries > 0)
      c->ignore_queries--;

    return;
  }

  if (!i && eol < 0) {
    c->operation_success = 0;
    pa_threaded_mainloop_signal (c->mainloop, 0);
    return;
  }

  if (eol)
    return;

  g_free (c->name);
  g_free (c->description);
  c->name = g_strdup (i->name);
  c->description = g_strdup (i->description);
  c->index = i->index;
  c->channel_map = i->channel_map;
  c->volume = i->volume;
  c->muted = i->mute;
  c->type = GST_PULSEMIXER_SINK;

  if (c->track) {
    int i = g_atomic_int_get (&c->track->flags);

    i = (i & ~GST_MIXER_TRACK_MUTE) | (c->muted ? GST_MIXER_TRACK_MUTE : 0);
    g_atomic_int_set (&c->track->flags, i);
  }

  c->operation_success = 1;
  pa_threaded_mainloop_signal (c->mainloop, 0);
}

static void
gst_pulsemixer_ctrl_source_info_cb (pa_context * context,
    const pa_source_info * i, int eol, void *userdata)
{
  GstPulseMixerCtrl *c = userdata;

  /* Called from the background thread! */

  if (c->outstandig_queries > 0)
    c->outstandig_queries--;

  if (c->ignore_queries > 0 || c->time_event) {

    if (c->ignore_queries > 0)
      c->ignore_queries--;

    return;
  }

  if (!i && eol < 0) {
    c->operation_success = 0;
    pa_threaded_mainloop_signal (c->mainloop, 0);
    return;
  }

  if (eol)
    return;

  g_free (c->name);
  g_free (c->description);
  c->name = g_strdup (i->name);
  c->description = g_strdup (i->description);
  c->index = i->index;
  c->channel_map = i->channel_map;
  c->volume = i->volume;
  c->muted = i->mute;
  c->type = GST_PULSEMIXER_SOURCE;

  if (c->track) {
    int i = g_atomic_int_get (&c->track->flags);

    i = (i & ~GST_MIXER_TRACK_MUTE) | (c->muted ? GST_MIXER_TRACK_MUTE : 0);
    g_atomic_int_set (&c->track->flags, i);
  }

  c->operation_success = 1;
  pa_threaded_mainloop_signal (c->mainloop, 0);
}

static void
gst_pulsemixer_ctrl_subscribe_cb (pa_context * context,
    pa_subscription_event_type_t t, uint32_t idx, void *userdata)
{
  GstPulseMixerCtrl *c = GST_PULSEMIXER_CTRL (userdata);

  pa_operation *o = NULL;

  /* Called from the background thread! */

  if (c->index != idx)
    return;

  if ((t & PA_SUBSCRIPTION_EVENT_TYPE_MASK) != PA_SUBSCRIPTION_EVENT_CHANGE)
    return;

  if (c->type == GST_PULSEMIXER_SINK)
    o = pa_context_get_sink_info_by_index (c->context, c->index,
        gst_pulsemixer_ctrl_sink_info_cb, c);
  else
    o = pa_context_get_source_info_by_index (c->context, c->index,
        gst_pulsemixer_ctrl_source_info_cb, c);

  if (!o) {
    GST_WARNING ("Failed to get sink info: %s",
        pa_strerror (pa_context_errno (c->context)));
    return;
  }

  pa_operation_unref (o);

  c->outstandig_queries++;
}

static void
gst_pulsemixer_ctrl_success_cb (pa_context * context, int success,
    void *userdata)
{
  GstPulseMixerCtrl *c = (GstPulseMixerCtrl *) userdata;

  c->operation_success = success;
  pa_threaded_mainloop_signal (c->mainloop, 0);
}

#define CHECK_DEAD_GOTO(c, label) do { \
if (!(c)->context || pa_context_get_state((c)->context) != PA_CONTEXT_READY) { \
    GST_WARNING("Not connected: %s", (c)->context ? pa_strerror(pa_context_errno((c)->context)) : "NULL"); \
    goto label; \
} \
} while(0);

static gboolean
gst_pulsemixer_ctrl_open (GstPulseMixerCtrl * c)
{
  int e;

  gchar *name = gst_pulse_client_name ();

  pa_operation *o = NULL;

  g_assert (c);

  c->mainloop = pa_threaded_mainloop_new ();
  g_assert (c->mainloop);

  e = pa_threaded_mainloop_start (c->mainloop);
  g_assert (e == 0);

  pa_threaded_mainloop_lock (c->mainloop);

  if (!(c->context =
          pa_context_new (pa_threaded_mainloop_get_api (c->mainloop), name))) {
    GST_WARNING ("Failed to create context");
    goto unlock_and_fail;
  }

  pa_context_set_state_callback (c->context,
      gst_pulsemixer_ctrl_context_state_cb, c);
  pa_context_set_subscribe_callback (c->context,
      gst_pulsemixer_ctrl_subscribe_cb, c);

  if (pa_context_connect (c->context, c->server, 0, NULL) < 0) {
    GST_WARNING ("Failed to connect context: %s",
        pa_strerror (pa_context_errno (c->context)));
    goto unlock_and_fail;
  }

  /* Wait until the context is ready */
  pa_threaded_mainloop_wait (c->mainloop);

  if (pa_context_get_state (c->context) != PA_CONTEXT_READY) {
    GST_WARNING ("Failed to connect context: %s",
        pa_strerror (pa_context_errno (c->context)));
    goto unlock_and_fail;
  }

  /* Subscribe to events */

  if (!(o =
          pa_context_subscribe (c->context,
              PA_SUBSCRIPTION_MASK_SINK | PA_SUBSCRIPTION_MASK_SOURCE,
              gst_pulsemixer_ctrl_success_cb, c))) {
    GST_WARNING ("Failed to subscribe to events: %s",
        pa_strerror (pa_context_errno (c->context)));
    goto unlock_and_fail;
  }

  c->operation_success = 0;
  while (pa_operation_get_state (o) != PA_OPERATION_DONE) {
    pa_threaded_mainloop_wait (c->mainloop);
    CHECK_DEAD_GOTO (c, unlock_and_fail);
  }

  if (!c->operation_success) {
    GST_WARNING ("Failed to subscribe to events: %s",
        pa_strerror (pa_context_errno (c->context)));
    goto unlock_and_fail;
  }

  /* Get sink info */

  if (c->type == GST_PULSEMIXER_UNKNOWN || c->type == GST_PULSEMIXER_SINK) {
    if (!(o =
            pa_context_get_sink_info_by_name (c->context, c->device,
                gst_pulsemixer_ctrl_sink_info_cb, c))) {
      GST_WARNING ("Failed to get sink info: %s",
          pa_strerror (pa_context_errno (c->context)));
      goto unlock_and_fail;
    }

    c->operation_success = 0;
    while (pa_operation_get_state (o) != PA_OPERATION_DONE) {
      pa_threaded_mainloop_wait (c->mainloop);
      CHECK_DEAD_GOTO (c, unlock_and_fail);
    }

    pa_operation_unref (o);
    o = NULL;

    if (!c->operation_success && (c->type == GST_PULSEMIXER_SINK
            || pa_context_errno (c->context) != PA_ERR_NOENTITY)) {
      GST_WARNING ("Failed to get sink info: %s",
          pa_strerror (pa_context_errno (c->context)));
      goto unlock_and_fail;
    }
  }

  if (c->type == GST_PULSEMIXER_UNKNOWN || c->type == GST_PULSEMIXER_SOURCE) {
    if (!(o =
            pa_context_get_source_info_by_name (c->context, c->device,
                gst_pulsemixer_ctrl_source_info_cb, c))) {
      GST_WARNING ("Failed to get source info: %s",
          pa_strerror (pa_context_errno (c->context)));
      goto unlock_and_fail;
    }

    c->operation_success = 0;
    while (pa_operation_get_state (o) != PA_OPERATION_DONE) {
      pa_threaded_mainloop_wait (c->mainloop);
      CHECK_DEAD_GOTO (c, unlock_and_fail);
    }

    pa_operation_unref (o);
    o = NULL;

    if (!c->operation_success) {
      GST_WARNING ("Failed to get source info: %s",
          pa_strerror (pa_context_errno (c->context)));
      goto unlock_and_fail;
    }
  }

  g_assert (c->type != GST_PULSEMIXER_UNKNOWN);

  c->track = gst_pulsemixer_track_new (c);
  c->tracklist = g_list_append (c->tracklist, c->track);

  pa_threaded_mainloop_unlock (c->mainloop);
  g_free (name);

  return TRUE;

unlock_and_fail:

  if (o)
    pa_operation_unref (o);

  if (c->mainloop)
    pa_threaded_mainloop_unlock (c->mainloop);

  g_free (name);

  return FALSE;
}

static void
gst_pulsemixer_ctrl_close (GstPulseMixerCtrl * c)
{
  g_assert (c);

  if (c->mainloop)
    pa_threaded_mainloop_stop (c->mainloop);

  if (c->context) {
    pa_context_disconnect (c->context);
    pa_context_unref (c->context);
    c->context = NULL;
  }

  if (c->mainloop) {
    pa_threaded_mainloop_free (c->mainloop);
    c->mainloop = NULL;
    c->time_event = NULL;
  }

  if (c->tracklist) {
    g_list_free (c->tracklist);
    c->tracklist = NULL;
  }

  if (c->track) {
    GST_PULSEMIXER_TRACK (c->track)->control = NULL;
    g_object_unref (c->track);
    c->track = NULL;
  }
}

GstPulseMixerCtrl *
gst_pulsemixer_ctrl_new (const gchar * server, const gchar * device,
    GstPulseMixerType type)
{
  GstPulseMixerCtrl *c = NULL;

  c = g_new (GstPulseMixerCtrl, 1);
  c->tracklist = NULL;
  c->server = g_strdup (server);
  c->device = g_strdup (device);
  c->mainloop = NULL;
  c->context = NULL;
  c->track = NULL;
  c->ignore_queries = c->outstandig_queries = 0;

  pa_cvolume_mute (&c->volume, PA_CHANNELS_MAX);
  pa_channel_map_init (&c->channel_map);
  c->muted = 0;
  c->index = PA_INVALID_INDEX;
  c->type = type;
  c->name = NULL;
  c->description = NULL;

  c->time_event = NULL;
  c->update_volume = c->update_mute = FALSE;

  if (!(gst_pulsemixer_ctrl_open (c))) {
    gst_pulsemixer_ctrl_free (c);
    return NULL;
  }

  return c;
}

void
gst_pulsemixer_ctrl_free (GstPulseMixerCtrl * c)
{
  g_assert (c);

  gst_pulsemixer_ctrl_close (c);

  g_free (c->server);
  g_free (c->device);
  g_free (c->name);
  g_free (c->description);
  g_free (c);
}

const GList *
gst_pulsemixer_ctrl_list_tracks (GstPulseMixerCtrl * c)
{
  g_assert (c);

  return c->tracklist;
}

static void
gst_pulsemixer_ctrl_timeout_event (pa_mainloop_api * a, pa_time_event * e,
    const struct timeval *tv, void *userdata)
{
  pa_operation *o;

  GstPulseMixerCtrl *c = GST_PULSEMIXER_CTRL (userdata);

  if (c->update_volume) {
    if (c->type == GST_PULSEMIXER_SINK)
      o = pa_context_set_sink_volume_by_index (c->context, c->index, &c->volume,
          NULL, NULL);
    else
      o = pa_context_set_source_volume_by_index (c->context, c->index,
          &c->volume, NULL, NULL);

    if (!o)
      GST_WARNING ("Failed to set device volume: %s",
          pa_strerror (pa_context_errno (c->context)));
    else
      pa_operation_unref (o);

    c->update_volume = FALSE;
  }

  if (c->update_mute) {
    if (c->type == GST_PULSEMIXER_SINK)
      o = pa_context_set_sink_mute_by_index (c->context, c->index, !!c->muted,
          NULL, NULL);
    else
      o = pa_context_set_source_mute_by_index (c->context, c->index, !!c->muted,
          NULL, NULL);

    if (!o)
      GST_WARNING ("Failed to set device mute: %s",
          pa_strerror (pa_context_errno (c->context)));
    else
      pa_operation_unref (o);

    c->update_mute = FALSE;
  }

  /* Make sure that all outstanding queries are being ignored */
  c->ignore_queries = c->outstandig_queries;

  g_assert (e == c->time_event);
  a->time_free (e);
  c->time_event = NULL;
}

#define UPDATE_DELAY 50000

static void
restart_time_event (GstPulseMixerCtrl * c)
{
  struct timeval tv;
  pa_mainloop_api *api;

  g_assert (c);

  if (c->time_event)
    return;

  /* Updating the volume too often will cause a lot of traffic
   * when accessing a networked server. Therefore we make sure
   * to update the volume only once every 50ms */

  api = pa_threaded_mainloop_get_api (c->mainloop);

  c->time_event =
      api->time_new (api, pa_timeval_add (pa_gettimeofday (&tv), UPDATE_DELAY),
      gst_pulsemixer_ctrl_timeout_event, c);
}

void
gst_pulsemixer_ctrl_set_volume (GstPulseMixerCtrl * c, GstMixerTrack * track,
    gint * volumes)
{
  pa_cvolume v;

  int i;

  g_assert (c);
  g_assert (track == c->track);

  pa_threaded_mainloop_lock (c->mainloop);

  for (i = 0; i < c->channel_map.channels; i++)
    v.values[i] = (pa_volume_t) volumes[i];

  v.channels = c->channel_map.channels;

  c->volume = v;
  c->update_volume = TRUE;

  restart_time_event (c);

  pa_threaded_mainloop_unlock (c->mainloop);
}

void
gst_pulsemixer_ctrl_get_volume (GstPulseMixerCtrl * c, GstMixerTrack * track,
    gint * volumes)
{
  int i;

  g_assert (c);
  g_assert (track == c->track);

  pa_threaded_mainloop_lock (c->mainloop);

  for (i = 0; i < c->channel_map.channels; i++)
    volumes[i] = c->volume.values[i];

  pa_threaded_mainloop_unlock (c->mainloop);
}

void
gst_pulsemixer_ctrl_set_record (GstPulseMixerCtrl * c, GstMixerTrack * track,
    gboolean record)
{
  g_assert (c);
  g_assert (track == c->track);
}

void
gst_pulsemixer_ctrl_set_mute (GstPulseMixerCtrl * c, GstMixerTrack * track,
    gboolean mute)
{
  g_assert (c);
  g_assert (track == c->track);

  pa_threaded_mainloop_lock (c->mainloop);

  c->muted = !!mute;
  c->update_mute = TRUE;

  if (c->track) {
    int i = g_atomic_int_get (&c->track->flags);

    i = (i & ~GST_MIXER_TRACK_MUTE) | (c->muted ? GST_MIXER_TRACK_MUTE : 0);
    g_atomic_int_set (&c->track->flags, i);
  }

  restart_time_event (c);

  pa_threaded_mainloop_unlock (c->mainloop);
}
