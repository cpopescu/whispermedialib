/*
 * GStreamer
 * Copyright 2005,2006 Zaheer Abbas Merali  <zaheerabbas at merali dot org>
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Alternatively, the contents of this file may be used under the
 * GNU Lesser General Public License Version 2.1 (the "LGPL"), in
 * which case the following provisions apply instead of the ones
 * mentioned above:
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

/**
 * SECTION:element-plugin
 *
 * <refsect2>
 * <title>Example launch line</title>
 * <para>
 * <programlisting>
 * gst-launch -v -m osxaudiosrc ! fakesink
 * </programlisting>
 * </para>
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <gst/gst.h>
#include <CoreAudio/CoreAudio.h>
#include "gstosxaudiosrc.h"
#include "gstosxaudioelement.h"

GST_DEBUG_CATEGORY_STATIC (osx_audiosrc_debug);
#define GST_CAT_DEFAULT osx_audiosrc_debug

static GstElementDetails gst_osx_audio_src_details =
GST_ELEMENT_DETAILS ("Audio Source (OSX)",
    "Source/Audio",
    "Input from a sound card in OS X",
    "Zaheer Abbas Merali <zaheerabbas at merali dot org>");

/* Filter signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  ARG_0,
  ARG_DEVICE
};

static GstStaticPadTemplate src_factory = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-raw-float, "
        "endianness = (int) {" G_STRINGIFY (G_BYTE_ORDER) " }, "
        "signed = (boolean) { TRUE }, "
        "width = (int) 32, "
        "depth = (int) 32, " "rate = (int) 44100, " "channels = (int) 2")
    );

static void gst_osx_audio_src_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_osx_audio_src_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);


static GstRingBuffer *gst_osx_audio_src_create_ringbuffer (GstBaseAudioSrc *
    src);
/*static GstCaps* gst_osx_audio_sink_getcaps (GstBaseSink * bsrc);*/
static void gst_osx_audio_src_osxelement_init (gpointer g_iface,
    gpointer iface_data);
OSStatus gst_osx_audio_src_io_proc (AudioDeviceID inDevice,
    const AudioTimeStamp * inNow, const AudioBufferList * inInputData,
    const AudioTimeStamp * inInputTime, AudioBufferList * outOutputData,
    const AudioTimeStamp * inOutputTime, void *inClientData);

static void
gst_osx_audio_src_osxelement_do_init (GType type)
{
  static const GInterfaceInfo osxelement_info = {
    gst_osx_audio_src_osxelement_init,
    NULL,
    NULL
  };

  GST_DEBUG_CATEGORY_INIT (osx_audiosrc_debug, "osxaudiosrc", 0,
      "OSX Audio Src");
  GST_DEBUG ("Adding static interface");
  g_type_add_interface_static (type, GST_OSX_AUDIO_ELEMENT_TYPE,
      &osxelement_info);
}

GST_BOILERPLATE_FULL (GstOsxAudioSrc, gst_osx_audio_src, GstBaseAudioSrc,
    GST_TYPE_BASE_AUDIO_SRC, gst_osx_audio_src_osxelement_do_init);


static void
gst_osx_audio_src_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&src_factory));

  gst_element_class_set_details (element_class, &gst_osx_audio_src_details);
}

/* initialize the plugin's class */
static void
gst_osx_audio_src_class_init (GstOsxAudioSrcClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstBaseAudioSrcClass *gstbaseaudiosrc_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;
  gstbaseaudiosrc_class = (GstBaseAudioSrcClass *) klass;

  parent_class = g_type_class_peek_parent (klass);

  gobject_class->set_property =
      GST_DEBUG_FUNCPTR (gst_osx_audio_src_set_property);
  gobject_class->get_property =
      GST_DEBUG_FUNCPTR (gst_osx_audio_src_get_property);

  g_object_class_install_property (gobject_class, ARG_DEVICE,
      g_param_spec_int ("device", "Device ID", "Device ID of input device",
          0, G_MAXINT, 0, G_PARAM_READWRITE));

  gstbaseaudiosrc_class->create_ringbuffer =
      GST_DEBUG_FUNCPTR (gst_osx_audio_src_create_ringbuffer);

}

/* initialize the new element
 * instantiate pads and add them to element
 * set functions
 * initialize structure
 */
static void
gst_osx_audio_src_init (GstOsxAudioSrc * src, GstOsxAudioSrcClass * gclass)
{
/*  GstElementClass *klass = GST_ELEMENT_GET_CLASS (sink); */
  gst_base_src_set_live (GST_BASE_SRC (src), TRUE);
  src->ringbuffer = NULL;
  GST_DEBUG ("Initialising object");
  gst_osx_audio_src_create_ringbuffer (GST_BASE_AUDIO_SRC (src));
}

static void
gst_osx_audio_src_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstOsxAudioSrc *src = GST_OSX_AUDIO_SRC (object);

  switch (prop_id) {
    case ARG_DEVICE:
      if (src->ringbuffer)
        src->ringbuffer->device_id = g_value_get_int (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_osx_audio_src_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstOsxAudioSrc *src = GST_OSX_AUDIO_SRC (object);
  int val = 0;

  switch (prop_id) {
    case ARG_DEVICE:
      if (src->ringbuffer)
        val = src->ringbuffer->device_id;

      g_value_set_int (value, val);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

/* GstElement vmethod implementations */


/* GstBaseAudioSink vmethod implementations */
static GstRingBuffer *
gst_osx_audio_src_create_ringbuffer (GstBaseAudioSrc * src)
{
  GstOsxAudioSrc *osxsrc;
  OSStatus status;
  UInt32 propertySize;

  osxsrc = GST_OSX_AUDIO_SRC (src);
  if (!osxsrc->ringbuffer) {
    GST_DEBUG ("Creating ringbuffer");
    osxsrc->ringbuffer = g_object_new (GST_TYPE_OSX_RING_BUFFER, NULL);
    /* change the device to the Default Input Device */
    propertySize = sizeof (osxsrc->ringbuffer->device_id);
    status = AudioHardwareGetProperty (kAudioHardwarePropertyDefaultInputDevice,
        &propertySize, &osxsrc->ringbuffer->device_id);
    GST_DEBUG ("osx src 0x%p element 0x%p  ioproc 0x%p", osxsrc,
        GST_OSX_AUDIO_ELEMENT_GET_INTERFACE (osxsrc),
        (void *) gst_osx_audio_src_io_proc);
    osxsrc->ringbuffer->element = GST_OSX_AUDIO_ELEMENT_GET_INTERFACE (osxsrc);
  }

  return GST_RING_BUFFER (osxsrc->ringbuffer);
}

OSStatus
gst_osx_audio_src_io_proc (AudioDeviceID inDevice, const AudioTimeStamp * inNow,
    const AudioBufferList * inInputData, const AudioTimeStamp * inInputTime,
    AudioBufferList * outOutputData, const AudioTimeStamp * inOutputTime,
    void *inClientData)
{
  GstOsxRingBuffer *buf = GST_OSX_RING_BUFFER (inClientData);

  guint8 *writeptr;
  gint writeseg;
  gint len;
  gint bytesToCopy;

  if (gst_ring_buffer_prepare_read (GST_RING_BUFFER (buf), &writeseg, &writeptr,
          &len)) {
    bytesToCopy = inInputData->mBuffers[0].mDataByteSize;
    memcpy (writeptr, (char *) inInputData->mBuffers[0].mData, bytesToCopy);

    /* clear written samples */
    /*gst_ring_buffer_clear (GST_RING_BUFFER(buf), writeseg); */

    /* we wrote one segment */
    gst_ring_buffer_advance (GST_RING_BUFFER (buf), 1);
  }
  return 0;
}

static void
gst_osx_audio_src_osxelement_init (gpointer g_iface, gpointer iface_data)
{
  GstOsxAudioElementInterface *iface = (GstOsxAudioElementInterface *) g_iface;

  iface->io_proc = gst_osx_audio_src_io_proc;
}
