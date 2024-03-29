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

#include "pulseprobe.h"
#include "pulseutil.h"

GST_DEBUG_CATEGORY_EXTERN (pulse_debug);
#define GST_CAT_DEFAULT pulse_debug

static void
gst_pulseprobe_context_state_cb (pa_context * context, void *userdata)
{
  GstPulseProbe *c = (GstPulseProbe *) userdata;

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
gst_pulseprobe_sink_info_cb (pa_context * context, const pa_sink_info * i,
    int eol, void *userdata)
{
  GstPulseProbe *c = (GstPulseProbe *) userdata;

  /* Called from the background thread! */

  if (eol || !i) {
    c->operation_success = eol > 0;
    pa_threaded_mainloop_signal (c->mainloop, 0);
  }

  if (i)
    c->devices = g_list_append (c->devices, g_strdup (i->name));

}

static void
gst_pulseprobe_source_info_cb (pa_context * context, const pa_source_info * i,
    int eol, void *userdata)
{
  GstPulseProbe *c = (GstPulseProbe *) userdata;

  /* Called from the background thread! */

  if (eol || !i) {
    c->operation_success = eol > 0;
    pa_threaded_mainloop_signal (c->mainloop, 0);
  }

  if (i)
    c->devices = g_list_append (c->devices, g_strdup (i->name));
}

static void
gst_pulseprobe_invalidate (GstPulseProbe * c)
{
  g_list_foreach (c->devices, (GFunc) g_free, NULL);
  g_list_free (c->devices);
  c->devices = NULL;
  c->devices_valid = 0;
}

static gboolean
gst_pulseprobe_open (GstPulseProbe * c)
{
  int e;

  gchar *name = gst_pulse_client_name ();

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

  pa_context_set_state_callback (c->context, gst_pulseprobe_context_state_cb,
      c);

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

  pa_threaded_mainloop_unlock (c->mainloop);
  g_free (name);

  gst_pulseprobe_invalidate (c);

  return TRUE;

unlock_and_fail:

  if (c->mainloop)
    pa_threaded_mainloop_unlock (c->mainloop);

  g_free (name);

  return FALSE;
}

#define CHECK_DEAD_GOTO(c, label) do { \
if (!(c)->context || pa_context_get_state((c)->context) != PA_CONTEXT_READY) { \
    GST_WARNING("Not connected: %s", (c)->context ? pa_strerror(pa_context_errno((c)->context)) : "NULL"); \
    goto label; \
} \
} while(0);

static gboolean
gst_pulseprobe_enumerate (GstPulseProbe * c)
{
  pa_operation *o = NULL;

  pa_threaded_mainloop_lock (c->mainloop);

  if (c->enumerate_sinks) {
    /* Get sink info */

    if (!(o =
            pa_context_get_sink_info_list (c->context,
                gst_pulseprobe_sink_info_cb, c))) {
      GST_WARNING ("Failed to get sink info: %s",
          pa_strerror (pa_context_errno (c->context)));
      goto unlock_and_fail;
    }

    c->operation_success = 0;
    while (pa_operation_get_state (o) != PA_OPERATION_DONE) {
      pa_threaded_mainloop_wait (c->mainloop);
      CHECK_DEAD_GOTO (c, unlock_and_fail);
    }

    if (!c->operation_success) {
      GST_WARNING ("Failed to get sink info: %s",
          pa_strerror (pa_context_errno (c->context)));
      goto unlock_and_fail;
    }

    pa_operation_unref (o);
    o = NULL;
  }

  if (c->enumerate_sources) {
    /* Get source info */

    if (!(o =
            pa_context_get_source_info_list (c->context,
                gst_pulseprobe_source_info_cb, c))) {
      GST_WARNING ("Failed to get source info: %s",
          pa_strerror (pa_context_errno (c->context)));
      goto unlock_and_fail;
    }

    c->operation_success = 0;
    while (pa_operation_get_state (o) != PA_OPERATION_DONE) {
      pa_threaded_mainloop_wait (c->mainloop);
      CHECK_DEAD_GOTO (c, unlock_and_fail);
    }

    if (!c->operation_success) {
      GST_WARNING ("Failed to get sink info: %s",
          pa_strerror (pa_context_errno (c->context)));
      goto unlock_and_fail;
    }

    pa_operation_unref (o);
    o = NULL;
  }

  c->devices_valid = 1;

  pa_threaded_mainloop_unlock (c->mainloop);

  return TRUE;

unlock_and_fail:

  if (o)
    pa_operation_unref (o);

  pa_threaded_mainloop_unlock (c->mainloop);

  return FALSE;
}

static void
gst_pulseprobe_close (GstPulseProbe * c)
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
  }
}

GstPulseProbe *
gst_pulseprobe_new (GObjectClass * klass, guint prop_id, const gchar * server,
    gboolean sinks, gboolean sources)
{
  GstPulseProbe *c = NULL;

  c = g_new (GstPulseProbe, 1);
  c->server = g_strdup (server);
  c->enumerate_sinks = sinks;
  c->enumerate_sources = sources;

  c->mainloop = NULL;
  c->context = NULL;

  c->prop_id = prop_id;
  c->properties =
      g_list_append (NULL, g_object_class_find_property (klass, "device"));
  c->devices = NULL;
  c->devices_valid = 0;

  return c;
}

void
gst_pulseprobe_free (GstPulseProbe * c)
{
  g_assert (c);

  gst_pulseprobe_close (c);

  g_list_free (c->properties);
  g_free (c->server);

  g_list_foreach (c->devices, (GFunc) g_free, NULL);
  g_list_free (c->devices);

  g_free (c);
}

const GList *
gst_pulseprobe_get_properties (GstPulseProbe * c)
{
  return c->properties;
}

gboolean
gst_pulseprobe_needs_probe (GstPulseProbe * c, guint prop_id,
    const GParamSpec * pspec)
{

  if (prop_id == c->prop_id)
    return !c->devices_valid;

  G_OBJECT_WARN_INVALID_PROPERTY_ID (c, prop_id, pspec);
  return FALSE;
}

void
gst_pulseprobe_probe_property (GstPulseProbe * c, guint prop_id,
    const GParamSpec * pspec)
{

  if (prop_id != c->prop_id) {
    G_OBJECT_WARN_INVALID_PROPERTY_ID (c, prop_id, pspec);
    return;
  }

  if (gst_pulseprobe_open (c)) {
    gst_pulseprobe_enumerate (c);
    gst_pulseprobe_close (c);
  }
}

GValueArray *
gst_pulseprobe_get_values (GstPulseProbe * c, guint prop_id,
    const GParamSpec * pspec)
{
  GValueArray *array;
  GValue value = { 0 };
  GList *item;

  if (prop_id != c->prop_id) {
    G_OBJECT_WARN_INVALID_PROPERTY_ID (c, prop_id, pspec);
    return NULL;
  }

  if (!c->devices_valid)
    return NULL;

  array = g_value_array_new (g_list_length (c->devices));
  g_value_init (&value, G_TYPE_STRING);
  for (item = c->devices; item != NULL; item = item->next) {
    GST_WARNING ("device found: %s", (const gchar *) item->data);
    g_value_set_string (&value, (const gchar *) item->data);
    g_value_array_append (array, &value);
  }
  g_value_unset (&value);

  return array;
}

void
gst_pulseprobe_set_server (GstPulseProbe * c, const gchar * server)
{
  g_assert (c);

  gst_pulseprobe_invalidate (c);

  g_free (c->server);
  c->server = g_strdup (server);
}
