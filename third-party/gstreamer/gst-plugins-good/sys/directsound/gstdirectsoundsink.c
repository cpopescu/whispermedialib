/* GStreamer
* Copyright (C) 2005 Sebastien Moutte <sebastien@moutte.net>
* Copyright (C) 2007 Pioneers of the Inevitable <songbird@songbirdnest.com>
*
* gstdirectsoundsink.c:
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
*
*
* The development of this code was made possible due to the involvement
* of Pioneers of the Inevitable, the creators of the Songbird Music player
*
*/

/**
 * SECTION:element-directsoundsink
 * @short_description: output sound using Directsound API
 *
 * <refsect2>
 * <para>
 * This element lets you output sound using the DirectSound API.
 * </para>
 * <para>
 * Note that you should almost always use generic audio conversion elements
 * like audioconvert and audioresample in front of an audiosink to make sure
 * your pipeline works under all circumstances (those conversion elements will
 * act in passthrough-mode if no conversion is necessary).
 * </para>
 * <title>Example pipelines</title>
 * <para>
 * <programlisting>
 * gst-launch-0.10 -v audiotestsrc ! audioconvert ! volume volume=0.1 ! directsoundsink
 * </programlisting>
 * will output a sine wave (continuous beep sound) to your sound card (with
 * a very low volume as precaution).
 * </para>
 * <para>
 * <programlisting>
 * gst-launch-0.10 -v filesrc location=music.ogg ! decodebin ! audioconvert ! audioresample ! directsoundsink
 * </programlisting>
 * will play an Ogg/Vorbis audio file and output it.
 * </para>
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstdirectsoundsink.h"

GST_DEBUG_CATEGORY_STATIC (directsoundsink_debug);

/* elementfactory information */
static const GstElementDetails gst_directsound_sink_details =
GST_ELEMENT_DETAILS ("Direct Sound Audio Sink",
    "Sink/Audio",
    "Output to a sound card via Direct Sound",
    "Sebastien Moutte <sebastien@moutte.net>");

static void gst_directsound_sink_base_init (gpointer g_class);
static void gst_directsound_sink_class_init (GstDirectSoundSinkClass * klass);
static void gst_directsound_sink_init (GstDirectSoundSink * dsoundsink,
    GstDirectSoundSinkClass * g_class);
static void gst_directsound_sink_finalise (GObject * object);

static GstCaps *gst_directsound_sink_getcaps (GstBaseSink * bsink);
static gboolean gst_directsound_sink_prepare (GstAudioSink * asink,
    GstRingBufferSpec * spec);
static gboolean gst_directsound_sink_unprepare (GstAudioSink * asink);

static gboolean gst_directsound_sink_open (GstAudioSink * asink);
static gboolean gst_directsound_sink_close (GstAudioSink * asink);
static guint gst_directsound_sink_write (GstAudioSink * asink, gpointer data,
    guint length);
static guint gst_directsound_sink_delay (GstAudioSink * asink);
static void gst_directsound_sink_reset (GstAudioSink * asink);

/* interfaces */
static void gst_directsound_sink_interfaces_init (GType type);
static void
gst_directsound_sink_implements_interface_init (GstImplementsInterfaceClass *
    iface);
static void gst_directsound_sink_mixer_interface_init (GstMixerClass * iface);

static GstStaticPadTemplate directsoundsink_sink_factory =
    GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-raw-int, "
        "signed = (boolean) { TRUE, FALSE }, "
        "width = (int) 16, "
        "depth = (int) 16, "
        "rate = (int) [ 1, MAX ], " "channels = (int) [ 1, 2 ]; "
        "audio/x-raw-int, "
        "signed = (boolean) { TRUE, FALSE }, "
        "width = (int) 8, "
        "depth = (int) 8, "
        "rate = (int) [ 1, MAX ], " "channels = (int) [ 1, 2 ]"));

GST_BOILERPLATE_FULL (GstDirectSoundSink, gst_directsound_sink, GstAudioSink,
    GST_TYPE_AUDIO_SINK, gst_directsound_sink_interfaces_init);

/* interfaces stuff */
static void
gst_directsound_sink_interfaces_init (GType type)
{
  static const GInterfaceInfo implements_interface_info = {
    (GInterfaceInitFunc) gst_directsound_sink_implements_interface_init,
    NULL,
    NULL,
  };

  static const GInterfaceInfo mixer_interface_info = {
    (GInterfaceInitFunc) gst_directsound_sink_mixer_interface_init,
    NULL,
    NULL,
  };

  g_type_add_interface_static (type,
      GST_TYPE_IMPLEMENTS_INTERFACE, &implements_interface_info);
  g_type_add_interface_static (type, GST_TYPE_MIXER, &mixer_interface_info);
}

static gboolean
gst_directsound_sink_interface_supported (GstImplementsInterface * iface,
    GType iface_type)
{
  g_return_val_if_fail (iface_type == GST_TYPE_MIXER, FALSE);

  /* for the sake of this example, we'll always support it. However, normally,
   * you would check whether the device you've opened supports mixers. */
  return TRUE;
}

static void
gst_directsound_sink_implements_interface_init (GstImplementsInterfaceClass *
    iface)
{
  iface->supported = gst_directsound_sink_interface_supported;
}

/*
 * This function returns the list of support tracks (inputs, outputs)
 * on this element instance. Elements usually build this list during
 * _init () or when going from NULL to READY.
 */

static const GList *
gst_directsound_sink_mixer_list_tracks (GstMixer * mixer)
{
  GstDirectSoundSink *dsoundsink = GST_DIRECTSOUND_SINK (mixer);

  return dsoundsink->tracks;
}

/*
 * Set volume. volumes is an array of size track->num_channels, and
 * each value in the array gives the wanted volume for one channel
 * on the track.
 */

static void
gst_directsound_sink_mixer_set_volume (GstMixer * mixer,
    GstMixerTrack * track, gint * volumes)
{
  GstDirectSoundSink *dsoundsink = GST_DIRECTSOUND_SINK (mixer);

  if (volumes[0] != dsoundsink->volume) {
    dsoundsink->volume = volumes[0];

    if (dsoundsink->pDSBSecondary) {
      /* DirectSound is using attenuation in the following range 
       * (DSBVOLUME_MIN=-10000, DSBVOLUME_MAX=0) */
      glong ds_attenuation = DSBVOLUME_MIN + (dsoundsink->volume * 100);

      IDirectSoundBuffer_SetVolume (dsoundsink->pDSBSecondary, ds_attenuation);
    }
  }
}

static void
gst_directsound_sink_mixer_get_volume (GstMixer * mixer,
    GstMixerTrack * track, gint * volumes)
{
  GstDirectSoundSink *dsoundsink = GST_DIRECTSOUND_SINK (mixer);

  volumes[0] = dsoundsink->volume;
}

static void
gst_directsound_sink_mixer_interface_init (GstMixerClass * iface)
{
  /* the mixer interface requires a definition of the mixer type:
   * hardware or software? */
  GST_MIXER_TYPE (iface) = GST_MIXER_SOFTWARE;

  /* virtual function pointers */
  iface->list_tracks = gst_directsound_sink_mixer_list_tracks;
  iface->set_volume = gst_directsound_sink_mixer_set_volume;
  iface->get_volume = gst_directsound_sink_mixer_get_volume;
}

static void
gst_directsound_sink_finalise (GObject * object)
{
  GstDirectSoundSink *dsoundsink = GST_DIRECTSOUND_SINK (object);

  g_mutex_free (dsoundsink->dsound_lock);

  if (dsoundsink->tracks) {
    g_list_foreach (dsoundsink->tracks, (GFunc) g_object_unref, NULL);
    g_list_free (dsoundsink->tracks);
    dsoundsink->tracks = NULL;
  }

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_directsound_sink_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_set_details (element_class, &gst_directsound_sink_details);
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&directsoundsink_sink_factory));
}

static void
gst_directsound_sink_class_init (GstDirectSoundSinkClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstBaseSinkClass *gstbasesink_class;
  GstBaseAudioSinkClass *gstbaseaudiosink_class;
  GstAudioSinkClass *gstaudiosink_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;
  gstbasesink_class = (GstBaseSinkClass *) klass;
  gstbaseaudiosink_class = (GstBaseAudioSinkClass *) klass;
  gstaudiosink_class = (GstAudioSinkClass *) klass;

  GST_DEBUG_CATEGORY_INIT (directsoundsink_debug, "directsoundsink", 0,
      "DirectSound sink");

  parent_class = g_type_class_peek_parent (klass);

  gobject_class->finalize = GST_DEBUG_FUNCPTR (gst_directsound_sink_finalise);

  gstbasesink_class->get_caps =
      GST_DEBUG_FUNCPTR (gst_directsound_sink_getcaps);

  gstaudiosink_class->prepare =
      GST_DEBUG_FUNCPTR (gst_directsound_sink_prepare);
  gstaudiosink_class->unprepare =
      GST_DEBUG_FUNCPTR (gst_directsound_sink_unprepare);
  gstaudiosink_class->open = GST_DEBUG_FUNCPTR (gst_directsound_sink_open);
  gstaudiosink_class->close = GST_DEBUG_FUNCPTR (gst_directsound_sink_close);
  gstaudiosink_class->write = GST_DEBUG_FUNCPTR (gst_directsound_sink_write);
  gstaudiosink_class->delay = GST_DEBUG_FUNCPTR (gst_directsound_sink_delay);
  gstaudiosink_class->reset = GST_DEBUG_FUNCPTR (gst_directsound_sink_reset);
}

static void
gst_directsound_sink_init (GstDirectSoundSink * dsoundsink,
    GstDirectSoundSinkClass * g_class)
{
  GstMixerTrack *track = NULL;

  dsoundsink->tracks = NULL;
  track = g_object_new (GST_TYPE_MIXER_TRACK, NULL);
  track->label = g_strdup ("DSoundTrack");
  track->num_channels = 2;
  track->min_volume = 0;
  track->max_volume = 100;
  track->flags = GST_MIXER_TRACK_OUTPUT;
  dsoundsink->tracks = g_list_append (dsoundsink->tracks, track);

  dsoundsink->pDS = NULL;
  dsoundsink->pDSBSecondary = NULL;
  dsoundsink->current_circular_offset = 0;
  dsoundsink->buffer_size = DSBSIZE_MIN;
  dsoundsink->volume = 100;
  dsoundsink->dsound_lock = g_mutex_new ();
  dsoundsink->first_buffer_after_reset = FALSE;
}

static GstCaps *
gst_directsound_sink_getcaps (GstBaseSink * bsink)
{
  GstDirectSoundSink *dsoundsink;

  dsoundsink = GST_DIRECTSOUND_SINK (bsink);

  return
      gst_caps_copy (gst_pad_get_pad_template_caps (GST_BASE_SINK_PAD
          (dsoundsink)));
}

static gboolean
gst_directsound_sink_open (GstAudioSink * asink)
{
  GstDirectSoundSink *dsoundsink = GST_DIRECTSOUND_SINK (asink);
  HRESULT hRes;

  /* create and initialize a DirecSound object */
  if (FAILED (hRes = DirectSoundCreate (NULL, &dsoundsink->pDS, NULL))) {
    GST_ELEMENT_ERROR (dsoundsink, RESOURCE, OPEN_READ,
        ("gst_directsound_sink_open: DirectSoundCreate: %s",
            DXGetErrorString9 (hRes)), (NULL));
    return FALSE;
  }

  if (FAILED (hRes = IDirectSound_SetCooperativeLevel (dsoundsink->pDS,
              GetDesktopWindow (), DSSCL_PRIORITY))) {
    GST_ELEMENT_ERROR (dsoundsink, RESOURCE, OPEN_READ,
        ("gst_directsound_sink_open: IDirectSound_SetCooperativeLevel: %s",
            DXGetErrorString9 (hRes)), (NULL));
    return FALSE;
  }

  return TRUE;
}

static gboolean
gst_directsound_sink_prepare (GstAudioSink * asink, GstRingBufferSpec * spec)
{
  GstDirectSoundSink *dsoundsink = GST_DIRECTSOUND_SINK (asink);
  HRESULT hRes;
  DSBUFFERDESC descSecondary;
  WAVEFORMATEX wfx;

  /*save number of bytes per sample */
  dsoundsink->bytes_per_sample = spec->bytes_per_sample;

  /* fill the WAVEFORMATEX struture with spec params */
  memset (&wfx, 0, sizeof (wfx));
  wfx.cbSize = sizeof (wfx);
  wfx.wFormatTag = WAVE_FORMAT_PCM;
  wfx.nChannels = spec->channels;
  wfx.nSamplesPerSec = spec->rate;
  wfx.wBitsPerSample = (spec->bytes_per_sample * 8) / wfx.nChannels;
  wfx.nBlockAlign = spec->bytes_per_sample;
  wfx.nAvgBytesPerSec = wfx.nSamplesPerSec * wfx.nBlockAlign;

  /* directsound buffer size can handle 1/2 sec of the stream */
  dsoundsink->buffer_size = wfx.nAvgBytesPerSec / 2;

  GST_INFO_OBJECT (dsoundsink,
      "GstRingBufferSpec->channels: %d, GstRingBufferSpec->rate: %d, GstRingBufferSpec->bytes_per_sample: %d\n"
      "WAVEFORMATEX.nSamplesPerSec: %ld, WAVEFORMATEX.wBitsPerSample: %d, WAVEFORMATEX.nBlockAlign: %d, WAVEFORMATEX.nAvgBytesPerSec: %ld\n"
      "Size of dsound cirucular buffe=>%d\n", spec->channels, spec->rate,
      spec->bytes_per_sample, wfx.nSamplesPerSec, wfx.wBitsPerSample,
      wfx.nBlockAlign, wfx.nAvgBytesPerSec, dsoundsink->buffer_size);

  /* create a secondary directsound buffer */
  memset (&descSecondary, 0, sizeof (DSBUFFERDESC));
  descSecondary.dwSize = sizeof (DSBUFFERDESC);
  descSecondary.dwFlags = DSBCAPS_GETCURRENTPOSITION2 |
      DSBCAPS_GLOBALFOCUS | DSBCAPS_CTRLVOLUME;

  descSecondary.dwBufferBytes = dsoundsink->buffer_size;
  descSecondary.lpwfxFormat = (WAVEFORMATEX *) & wfx;

  hRes = IDirectSound_CreateSoundBuffer (dsoundsink->pDS, &descSecondary,
      &dsoundsink->pDSBSecondary, NULL);
  if (FAILED (hRes)) {
    GST_ELEMENT_ERROR (dsoundsink, RESOURCE, OPEN_READ,
        ("gst_directsound_sink_prepare: IDirectSound_CreateSoundBuffer: %s",
            DXGetErrorString9 (hRes)), (NULL));
    return FALSE;
  }

  return TRUE;
}

static gboolean
gst_directsound_sink_unprepare (GstAudioSink * asink)
{
  GstDirectSoundSink *dsoundsink;

  dsoundsink = GST_DIRECTSOUND_SINK (asink);

  /* release secondary DirectSound buffer */
  if (dsoundsink->pDSBSecondary)
    IDirectSoundBuffer_Release (dsoundsink->pDSBSecondary);

  return TRUE;
}

static gboolean
gst_directsound_sink_close (GstAudioSink * asink)
{
  GstDirectSoundSink *dsoundsink = NULL;

  dsoundsink = GST_DIRECTSOUND_SINK (asink);

  /* release DirectSound object */
  g_return_val_if_fail (dsoundsink->pDS != NULL, FALSE);
  IDirectSound_Release (dsoundsink->pDS);

  return TRUE;
}

static guint
gst_directsound_sink_write (GstAudioSink * asink, gpointer data, guint length)
{
  GstDirectSoundSink *dsoundsink;
  DWORD dwStatus;
  HRESULT hRes;
  LPVOID pLockedBuffer1 = NULL, pLockedBuffer2 = NULL;
  DWORD dwSizeBuffer1, dwSizeBuffer2;
  DWORD dwCurrentPlayCursor;

  dsoundsink = GST_DIRECTSOUND_SINK (asink);

  GST_DSOUND_LOCK (dsoundsink);

  /* get current buffer status */
  hRes = IDirectSoundBuffer_GetStatus (dsoundsink->pDSBSecondary, &dwStatus);

  /* get current play cursor position */
  hRes = IDirectSoundBuffer_GetCurrentPosition (dsoundsink->pDSBSecondary,
      &dwCurrentPlayCursor, NULL);

  if (SUCCEEDED (hRes) && (dwStatus & DSBSTATUS_PLAYING)) {
    DWORD dwFreeBufferSize;

  calculate_freesize:
    /* calculate the free size of the circular buffer */
    if (dwCurrentPlayCursor < dsoundsink->current_circular_offset)
      dwFreeBufferSize =
          dsoundsink->buffer_size - (dsoundsink->current_circular_offset -
          dwCurrentPlayCursor);
    else
      dwFreeBufferSize =
          dwCurrentPlayCursor - dsoundsink->current_circular_offset;

    if (length >= dwFreeBufferSize) {
      Sleep (100);
      hRes = IDirectSoundBuffer_GetCurrentPosition (dsoundsink->pDSBSecondary,
          &dwCurrentPlayCursor, NULL);

      hRes =
          IDirectSoundBuffer_GetStatus (dsoundsink->pDSBSecondary, &dwStatus);
      if (SUCCEEDED (hRes) && (dwStatus & DSBSTATUS_PLAYING))
        goto calculate_freesize;
      else {
        dsoundsink->first_buffer_after_reset = FALSE;
        GST_DSOUND_UNLOCK (dsoundsink);
        return 0;
      }
    }
  }

  if (dwStatus & DSBSTATUS_BUFFERLOST) {
    hRes = IDirectSoundBuffer_Restore (dsoundsink->pDSBSecondary);      /*need a loop waiting the buffer is restored?? */

    dsoundsink->current_circular_offset = 0;
  }

  hRes = IDirectSoundBuffer_Lock (dsoundsink->pDSBSecondary,
      dsoundsink->current_circular_offset, length, &pLockedBuffer1,
      &dwSizeBuffer1, &pLockedBuffer2, &dwSizeBuffer2, 0L);

  if (SUCCEEDED (hRes)) {
    // Write to pointers without reordering.
    memcpy (pLockedBuffer1, data, dwSizeBuffer1);
    if (pLockedBuffer2 != NULL)
      memcpy (pLockedBuffer2, (LPBYTE) data + dwSizeBuffer1, dwSizeBuffer2);

    // Update where the buffer will lock (for next time)
    dsoundsink->current_circular_offset += dwSizeBuffer1 + dwSizeBuffer2;
    dsoundsink->current_circular_offset %= dsoundsink->buffer_size;     /* Circular buffer */

    hRes = IDirectSoundBuffer_Unlock (dsoundsink->pDSBSecondary, pLockedBuffer1,
        dwSizeBuffer1, pLockedBuffer2, dwSizeBuffer2);
  }

  /* if the buffer was not in playing state yet, call play on the buffer 
     except if this buffer is the fist after a reset (base class call reset and write a buffer when setting the sink to pause) */
  if (!(dwStatus & DSBSTATUS_PLAYING) &&
      dsoundsink->first_buffer_after_reset == FALSE) {
    hRes = IDirectSoundBuffer_Play (dsoundsink->pDSBSecondary, 0, 0,
        DSBPLAY_LOOPING);
  }

  dsoundsink->first_buffer_after_reset = FALSE;

  GST_DSOUND_UNLOCK (dsoundsink);

  return length;
}

static guint
gst_directsound_sink_delay (GstAudioSink * asink)
{
  GstDirectSoundSink *dsoundsink;
  HRESULT hRes;
  DWORD dwCurrentPlayCursor;
  DWORD dwBytesInQueue = 0;
  gint nNbSamplesInQueue = 0;
  DWORD dwStatus;

  dsoundsink = GST_DIRECTSOUND_SINK (asink);

  /* get current buffer status */
  hRes = IDirectSoundBuffer_GetStatus (dsoundsink->pDSBSecondary, &dwStatus);

  if (dwStatus & DSBSTATUS_PLAYING) {
    /*evaluate the number of samples in queue in the circular buffer */
    hRes = IDirectSoundBuffer_GetCurrentPosition (dsoundsink->pDSBSecondary,
        &dwCurrentPlayCursor, NULL);

    if (hRes == S_OK) {
      if (dwCurrentPlayCursor < dsoundsink->current_circular_offset)
        dwBytesInQueue =
            dsoundsink->current_circular_offset - dwCurrentPlayCursor;
      else
        dwBytesInQueue =
            dsoundsink->current_circular_offset + (dsoundsink->buffer_size -
            dwCurrentPlayCursor);

      nNbSamplesInQueue = dwBytesInQueue / dsoundsink->bytes_per_sample;
    }
  }

  return nNbSamplesInQueue;
}

static void
gst_directsound_sink_reset (GstAudioSink * asink)
{
  GstDirectSoundSink *dsoundsink;
  LPVOID pLockedBuffer = NULL;
  DWORD dwSizeBuffer = 0;

  dsoundsink = GST_DIRECTSOUND_SINK (asink);

  GST_DSOUND_LOCK (dsoundsink);

  if (dsoundsink->pDSBSecondary) {
    /*stop playing */
    HRESULT hRes = IDirectSoundBuffer_Stop (dsoundsink->pDSBSecondary);

    /*reset position */
    hRes = IDirectSoundBuffer_SetCurrentPosition (dsoundsink->pDSBSecondary, 0);
    dsoundsink->current_circular_offset = 0;

    /*reset the buffer */
    hRes = IDirectSoundBuffer_Lock (dsoundsink->pDSBSecondary,
        dsoundsink->current_circular_offset, dsoundsink->buffer_size,
        &pLockedBuffer, &dwSizeBuffer, NULL, NULL, 0L);

    if (SUCCEEDED (hRes)) {
      memset (pLockedBuffer, 0, dwSizeBuffer);

      hRes =
          IDirectSoundBuffer_Unlock (dsoundsink->pDSBSecondary, pLockedBuffer,
          dwSizeBuffer, NULL, 0);
    }
  }

  dsoundsink->first_buffer_after_reset = TRUE;

  GST_DSOUND_UNLOCK (dsoundsink);
}
