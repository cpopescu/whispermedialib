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

#ifndef __GST_PULSEUTIL_H__
#define __GST_PULSEUTIL_H__

#include <gst/gst.h>
#include <pulse/pulseaudio.h>
#include <gst/audio/gstaudiosink.h>

gboolean gst_pulse_fill_sample_spec (GstRingBufferSpec * spec,
    pa_sample_spec * ss);

gchar *gst_pulse_client_name (void);

pa_channel_map *gst_pulse_gst_to_channel_map (pa_channel_map * map,
    GstRingBufferSpec * spec);

#endif
