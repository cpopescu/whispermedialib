/* GStreamer
 * Copyright (C) 2006 Wim Taymans <wim@fluendo.com>
 *
 * gstjacksink.h:
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

#ifndef __GST_JACK_AUDIO_SINK_H__
#define __GST_JACK_AUDIO_SINK_H__

#include <jack/jack.h>

#include <gst/gst.h>
#include <gst/audio/gstbaseaudiosink.h>

#include "gstjackaudioclient.h"

G_BEGIN_DECLS

#define GST_TYPE_JACK_AUDIO_SINK             (gst_jack_audio_sink_get_type())
#define GST_JACK_AUDIO_SINK(obj)             (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_JACK_AUDIO_SINK,GstJackAudioSink))
#define GST_JACK_AUDIO_SINK_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_JACK_AUDIO_SINK,GstJackAudioSinkClass))
#define GST_JACK_AUDIO_SINK_GET_CLASS(obj)   (G_TYPE_INSTANCE_GET_CLASS ((obj),GST_TYPE_JACK_AUDIO_SINK,GstJackAudioSinkClass))
#define GST_IS_JACK_AUDIO_SINK(obj)          (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_JACK_AUDIO_SINK))
#define GST_IS_JACK_AUDIO_SINK_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_JACK_AUDIO_SINK))

typedef struct _GstJackAudioSink GstJackAudioSink;
typedef struct _GstJackAudioSinkClass GstJackAudioSinkClass;

/**
 * GstJackConnect:
 * @GST_JACK_CONNECT_NONE: Don't automatically connect to physical ports.
 *     In this mode, the element will accept any number of input channels and will
 *     create (but not connect) an output port for each channel.
 * @GST_JACK_CONNECT_AUTO: In this mode, the element will try to connect each
 *     output port to a random physical jack input pin. The sink will
 *     expose the number of physical channels on its pad caps.
 *
 * Specify how the output ports will be connected.
 */
typedef enum {
  GST_JACK_CONNECT_NONE,
  GST_JACK_CONNECT_AUTO
} GstJackConnect;

/**
 * GstJackAudioSink:
 * 
 * Opaque #GstJackAudioSink.
 */
struct _GstJackAudioSink {
  GstBaseAudioSink element;

  /*< private >*/
  /* cached caps */
  GstCaps         *caps;

  /* properties */
  GstJackConnect   connect;
  gchar           *server;

  /* our client */
  GstJackAudioClient *client;

  /* our ports */
  jack_port_t    **ports;
  int              port_count;
};

struct _GstJackAudioSinkClass {
  GstBaseAudioSinkClass parent_class;
};

GType gst_jack_audio_sink_get_type (void);

G_END_DECLS

#endif /* __GST_JACK_AUDIO_SINK_H__ */
