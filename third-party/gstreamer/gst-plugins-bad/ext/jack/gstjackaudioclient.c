/* GStreamer
 * Copyright (C) 2006 Wim Taymans <wim@fluendo.com>
 *
 * gstjackaudioclient.c: jack audio client implementation
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

#include <string.h>

#include "gstjackaudioclient.h"

GST_DEBUG_CATEGORY_STATIC (gst_jack_audio_client_debug);
#define GST_CAT_DEFAULT gst_jack_audio_client_debug

void
gst_jack_audio_client_init (void)
{
  GST_DEBUG_CATEGORY_INIT (gst_jack_audio_client_debug, "jackclient", 0,
      "jackclient helpers");
}

/* a list of global connections indexed by id and server. */
G_LOCK_DEFINE_STATIC (connections_lock);
static GList *connections;

/* the connection to a server  */
typedef struct
{
  gint refcount;
  GMutex *lock;

  /* id/server pair and the connection */
  gchar *id;
  gchar *server;
  jack_client_t *client;

  /* lists of GstJackAudioClients */
  gint n_clients;
  GList *src_clients;
  GList *sink_clients;
} GstJackAudioConnection;

/* an object sharing a jack_client_t connection. */
struct _GstJackAudioClient
{
  GstJackAudioConnection *conn;

  GstJackClientType type;
  gboolean active;

  void (*shutdown) (void *arg);
  JackProcessCallback process;
  JackBufferSizeCallback buffer_size;
  JackSampleRateCallback sample_rate;
  gpointer user_data;
};

typedef jack_default_audio_sample_t sample_t;

typedef struct
{
  jack_nframes_t nframes;
  gpointer user_data;
} JackCB;

static int
jack_process_cb (jack_nframes_t nframes, void *arg)
{
  GstJackAudioConnection *conn = (GstJackAudioConnection *) arg;
  GList *walk;
  int res = 0;

  g_mutex_lock (conn->lock);
  /* call sources first, then sinks. Sources will either push data into the
   * ringbuffer of the sinks, which will then pull the data out of it, or
   * sinks will pull the data from the sources. */
  for (walk = conn->src_clients; walk; walk = g_list_next (walk)) {
    GstJackAudioClient *client = (GstJackAudioClient *) walk->data;

    /* only call active clients */
    if (client->active && client->process)
      res = client->process (nframes, client->user_data);
  }
  for (walk = conn->sink_clients; walk; walk = g_list_next (walk)) {
    GstJackAudioClient *client = (GstJackAudioClient *) walk->data;

    /* only call active clients */
    if (client->active && client->process)
      res = client->process (nframes, client->user_data);
  }
  g_mutex_unlock (conn->lock);

  return res;
}

/* we error out */
static int
jack_sample_rate_cb (jack_nframes_t nframes, void *arg)
{
  return 0;
}

/* we error out */
static int
jack_buffer_size_cb (jack_nframes_t nframes, void *arg)
{
  return 0;
}

static void
jack_shutdown_cb (void *arg)
{
  GstJackAudioConnection *conn = (GstJackAudioConnection *) arg;
  GList *walk;

  g_mutex_lock (conn->lock);
  for (walk = conn->src_clients; walk; walk = g_list_next (walk)) {
    GstJackAudioClient *client = (GstJackAudioClient *) walk->data;

    if (client->shutdown)
      client->shutdown (client->user_data);
  }
  for (walk = conn->sink_clients; walk; walk = g_list_next (walk)) {
    GstJackAudioClient *client = (GstJackAudioClient *) walk->data;

    if (client->shutdown)
      client->shutdown (client->user_data);
  }
  g_mutex_unlock (conn->lock);
}

typedef struct
{
  const gchar *id;
  const gchar *server;
} FindData;

static gint
connection_find (GstJackAudioConnection * conn, FindData * data)
{
  /* id's must match */
  if (strcmp (conn->id, data->id))
    return 1;

  /* both the same or NULL */
  if (conn->server == data->server)
    return 0;

  /* we cannot compare NULL */
  if (conn->server == NULL || data->server == NULL)
    return 1;

  if (strcmp (conn->server, data->server))
    return 1;

  return 0;
}

/* make a connection with @id and @server. Returns NULL on failure with the
 * status set. */
static GstJackAudioConnection *
gst_jack_audio_make_connection (const gchar * id, const gchar * server,
    jack_status_t * status)
{
  GstJackAudioConnection *conn;
  jack_options_t options;
  jack_client_t *jclient;
  gint res;

  *status = 0;

  GST_DEBUG ("new client %s, connecting to server %s", id,
      GST_STR_NULL (server));

  /* never start a server */
  options = JackNoStartServer;
  /* if we have a servername, use it */
  if (server != NULL)
    options |= JackServerName;
  /* open the client */
  jclient = jack_client_open (id, options, status, server);
  if (jclient == NULL)
    goto could_not_open;

  /* now create object */
  conn = g_new (GstJackAudioConnection, 1);
  conn->refcount = 1;
  conn->lock = g_mutex_new ();
  conn->id = g_strdup (id);
  conn->server = g_strdup (server);
  conn->client = jclient;
  conn->n_clients = 0;
  conn->src_clients = NULL;
  conn->sink_clients = NULL;

  /* set our callbacks  */
  jack_set_process_callback (jclient, jack_process_cb, conn);
  /* these callbacks cause us to error */
  jack_set_buffer_size_callback (jclient, jack_buffer_size_cb, conn);
  jack_set_sample_rate_callback (jclient, jack_sample_rate_cb, conn);
  jack_on_shutdown (jclient, jack_shutdown_cb, conn);

  /* all callbacks are set, activate the client */
  if ((res = jack_activate (jclient)))
    goto could_not_activate;

  GST_DEBUG ("opened connection %p", conn);

  return conn;

  /* ERRORS */
could_not_open:
  {
    GST_DEBUG ("failed to open jack client, %d", *status);
    return NULL;
  }
could_not_activate:
  {
    GST_ERROR ("Could not activate client (%d)", res);
    *status = JackFailure;
    g_mutex_free (conn->lock);
    g_free (conn->id);
    g_free (conn->server);
    g_free (conn);
    return NULL;
  }
}

static GstJackAudioConnection *
gst_jack_audio_get_connection (const gchar * id, const gchar * server,
    jack_status_t * status)
{
  GstJackAudioConnection *conn;
  GList *found;
  FindData data;

  GST_DEBUG ("getting connection for id %s, server %s", id,
      GST_STR_NULL (server));

  data.id = id;
  data.server = server;

  G_LOCK (connections_lock);
  found =
      g_list_find_custom (connections, &data, (GCompareFunc) connection_find);
  if (found != NULL) {
    /* we found it, increase refcount and return it */
    conn = (GstJackAudioConnection *) found->data;
    conn->refcount++;

    GST_DEBUG ("found connection %p", conn);
  } else {
    /* make new connection */
    conn = gst_jack_audio_make_connection (id, server, status);
    if (conn != NULL) {
      GST_DEBUG ("created connection %p", conn);
      /* add to list on success */
      connections = g_list_prepend (connections, conn);
    } else {
      GST_WARNING ("could not create connection");
    }
  }
  G_UNLOCK (connections_lock);

  return conn;
}

static void
gst_jack_audio_unref_connection (GstJackAudioConnection * conn)
{
  gint res;
  gboolean zero;

  GST_DEBUG ("unref connection %p refcnt %d", conn, conn->refcount);

  G_LOCK (connections_lock);
  conn->refcount--;
  if ((zero = (conn->refcount == 0))) {
    GST_DEBUG ("closing connection %p", conn);
    /* remove from list, we can release the mutex after removing the connection
     * from the list because after that, nobody can access the connection anymore. */
    connections = g_list_remove (connections, conn);
  }
  G_UNLOCK (connections_lock);

  /* if we are zero, close and cleanup the connection */
  if (zero) {
    /* don't use conn->lock here. two reasons:
     *
     *  1) its not necessary: jack_deactivate() will not return until the JACK thread
     *      associated with this connection is cleaned up by a thread join, hence 
     *      no more callbacks can occur or be in progress.
     *
     * 2) it would deadlock anyway, because jack_deactivate() will sleep
     *      waiting for the JACK thread, and can thus cause deadlock in 
     *      jack_process_cb()
     */
    if ((res = jack_deactivate (conn->client))) {
      /* we only warn, this means the server is probably shut down and the client
       * is gone anyway. */
      GST_WARNING ("Could not deactivate Jack client (%d)", res);
    }
    /* close connection */
    if ((res = jack_client_close (conn->client))) {
      /* we assume the client is gone. */
      GST_WARNING ("close failed (%d)", res);
    }

    /* free resources */
    g_mutex_free (conn->lock);
    g_free (conn->id);
    g_free (conn->server);
    g_free (conn);
  }
}

static void
gst_jack_audio_connection_add_client (GstJackAudioConnection * conn,
    GstJackAudioClient * client)
{
  g_mutex_lock (conn->lock);
  switch (client->type) {
    case GST_JACK_CLIENT_SOURCE:
      conn->src_clients = g_list_append (conn->src_clients, client);
      conn->n_clients++;
      break;
    case GST_JACK_CLIENT_SINK:
      conn->sink_clients = g_list_append (conn->sink_clients, client);
      conn->n_clients++;
      break;
    default:
      g_warning ("trying to add unknown client type");
      break;
  }
  g_mutex_unlock (conn->lock);
}

static void
gst_jack_audio_connection_remove_client (GstJackAudioConnection * conn,
    GstJackAudioClient * client)
{
  g_mutex_lock (conn->lock);
  switch (client->type) {
    case GST_JACK_CLIENT_SOURCE:
      conn->src_clients = g_list_remove (conn->src_clients, client);
      conn->n_clients--;
      break;
    case GST_JACK_CLIENT_SINK:
      conn->sink_clients = g_list_remove (conn->sink_clients, client);
      conn->n_clients--;
      break;
    default:
      g_warning ("trying to remove unknown client type");
      break;
  }
  g_mutex_unlock (conn->lock);
}

/**
 * gst_jack_audio_client_get:
 * @id: the client id
 * @server: the server to connect to or NULL for the default server
 * @type: the client type
 * @shutdown: a callback when the jack server shuts down
 * @process: a callback when samples are available
 * @buffer_size: a callback when the buffer_size changes
 * @sample_rate: a callback when the sample_rate changes
 * @user_data: user data passed to the callbacks
 * @status: pointer to hold the jack status code in case of errors
 *
 * Get the jack client connection for @id and @server. Connections to the same
 * @id and @server will receive the same physical Jack client connection and
 * will therefore be scheduled in the same process callback.
 * 
 * Returns: a #GstJackAudioClient.
 */
GstJackAudioClient *
gst_jack_audio_client_new (const gchar * id, const gchar * server,
    GstJackClientType type, void (*shutdown) (void *arg),
    JackProcessCallback process, JackBufferSizeCallback buffer_size,
    JackSampleRateCallback sample_rate, gpointer user_data,
    jack_status_t * status)
{
  GstJackAudioClient *client;
  GstJackAudioConnection *conn;

  g_return_val_if_fail (id != NULL, NULL);
  g_return_val_if_fail (status != NULL, NULL);

  /* first get a connection for the id/server pair */
  conn = gst_jack_audio_get_connection (id, server, status);
  if (conn == NULL)
    goto no_connection;

  /* make new client using the connection */
  client = g_new (GstJackAudioClient, 1);
  client->active = FALSE;
  client->conn = conn;
  client->type = type;
  client->shutdown = shutdown;
  client->process = process;
  client->buffer_size = buffer_size;
  client->sample_rate = sample_rate;
  client->user_data = user_data;

  /* add the client to the connection */
  gst_jack_audio_connection_add_client (conn, client);

  return client;

  /* ERRORS */
no_connection:
  {
    GST_DEBUG ("Could not get server connection (%d)", *status);
    return NULL;
  }
}

/**
 * gst_jack_audio_client_free:
 * @client: a #GstJackAudioClient
 *
 * Free the resources used by @client.
 */
void
gst_jack_audio_client_free (GstJackAudioClient * client)
{
  GstJackAudioConnection *conn;

  g_return_if_fail (client != NULL);

  conn = client->conn;

  /* remove from connection first so that it's not scheduled anymore after this
   * call */
  gst_jack_audio_connection_remove_client (conn, client);
  gst_jack_audio_unref_connection (conn);

  g_free (client);
}

/**
 * gst_jack_audio_client_get_client:
 * @client: a #GstJackAudioClient
 *
 * Get the jack audio client for @client. This function is used to perform
 * operations on the jack server from this client.
 *
 * Returns: The jack audio client.
 */
jack_client_t *
gst_jack_audio_client_get_client (GstJackAudioClient * client)
{
  g_return_val_if_fail (client != NULL, NULL);

  /* no lock needed, the connection and the client does not change 
   * once the client is created. */
  return client->conn->client;
}

/**
 * gst_jack_audio_client_set_active:
 * @client: a #GstJackAudioClient
 * @active: new mode for the client
 *
 * Activate or deactive @client. When a client is activated it will receive
 * callbacks when data should be processed.
 *
 * Returns: 0 if all ok.
 */
gint
gst_jack_audio_client_set_active (GstJackAudioClient * client, gboolean active)
{
  g_return_val_if_fail (client != NULL, -1);

  /* make sure that we are not dispatching the client */
  g_mutex_lock (client->conn->lock);
  client->active = active;
  g_mutex_unlock (client->conn->lock);

  return 0;
}
