/* GStreamer
 *
 * Copyright (C) 2007 Sebastian Dröge <slomo@circular-chaos.org>
 *
 * audiowsincband.c: Unit test for the audiowsincband element
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation; either version 2.1 of
 * the License, or (at your option) any later version.
 * 
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 * 
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301 USA
 */

#include <gst/gst.h>
#include <gst/base/gstbasetransform.h>
#include <gst/check/gstcheck.h>

#include <math.h>

/* For ease of programming we use globals to keep refs for our floating
 * src and sink pads we create; otherwise we always have to do get_pad,
 * get_peer, and then remove references in every test function */
GstPad *mysrcpad, *mysinkpad;

#define AUDIO_WSINC_BAND_CAPS_STRING_32          \
    "audio/x-raw-float, "               \
    "channels = (int) 1, "              \
    "rate = (int) 44100, "              \
    "endianness = (int) BYTE_ORDER, "   \
    "width = (int) 32"                  \

#define AUDIO_WSINC_BAND_CAPS_STRING_64          \
    "audio/x-raw-float, "               \
    "channels = (int) 1, "              \
    "rate = (int) 44100, "              \
    "endianness = (int) BYTE_ORDER, "   \
    "width = (int) 64"                  \

static GstStaticPadTemplate sinktemplate = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-raw-float, "
        "channels = (int) 1, "
        "rate = (int) 44100, "
        "endianness = (int) BYTE_ORDER, " "width = (int) { 32, 64 } ")
    );
static GstStaticPadTemplate srctemplate = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-raw-float, "
        "channels = (int) 1, "
        "rate = (int) 44100, "
        "endianness = (int) BYTE_ORDER, " "width = (int) { 32, 64 } ")
    );

GstElement *
setup_audiowsincband ()
{
  GstElement *audiowsincband;

  GST_DEBUG ("setup_audiowsincband");
  audiowsincband = gst_check_setup_element ("audiowsincband");
  mysrcpad = gst_check_setup_src_pad (audiowsincband, &srctemplate, NULL);
  mysinkpad = gst_check_setup_sink_pad (audiowsincband, &sinktemplate, NULL);
  gst_pad_set_active (mysrcpad, TRUE);
  gst_pad_set_active (mysinkpad, TRUE);

  return audiowsincband;
}

void
cleanup_audiowsincband (GstElement * audiowsincband)
{
  GST_DEBUG ("cleanup_audiowsincband");

  g_list_foreach (buffers, (GFunc) gst_mini_object_unref, NULL);
  g_list_free (buffers);
  buffers = NULL;

  gst_pad_set_active (mysrcpad, FALSE);
  gst_pad_set_active (mysinkpad, FALSE);
  gst_check_teardown_src_pad (audiowsincband);
  gst_check_teardown_sink_pad (audiowsincband);
  gst_check_teardown_element (audiowsincband);
}

/* Test if data containing only one frequency component
 * at rate/2 is erased with bandpass mode and a 
 * 2000Hz frequency band around rate/4 */
GST_START_TEST (test_32_bp_0hz)
{
  GstElement *audiowsincband;
  GstBuffer *inbuffer, *outbuffer;
  GstCaps *caps;
  gfloat *in, *res, rms;
  gint i;
  GList *node;

  audiowsincband = setup_audiowsincband ();
  /* Set to bandpass */
  g_object_set (G_OBJECT (audiowsincband), "mode", 0, NULL);
  g_object_set (G_OBJECT (audiowsincband), "length", 31, NULL);

  fail_unless (gst_element_set_state (audiowsincband,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_SUCCESS,
      "could not set to playing");

  g_object_set (G_OBJECT (audiowsincband), "lower-frequency",
      44100 / 4.0 - 1000, NULL);
  g_object_set (G_OBJECT (audiowsincband), "upper-frequency",
      44100 / 4.0 + 1000, NULL);
  inbuffer = gst_buffer_new_and_alloc (1024 * sizeof (gfloat));
  in = (gfloat *) GST_BUFFER_DATA (inbuffer);
  for (i = 0; i < 1024; i++)
    in[i] = 1.0;

  caps = gst_caps_from_string (AUDIO_WSINC_BAND_CAPS_STRING_32);
  gst_buffer_set_caps (inbuffer, caps);
  gst_caps_unref (caps);
  ASSERT_BUFFER_REFCOUNT (inbuffer, "inbuffer", 1);

  /* pushing gives away my reference ... */
  fail_unless (gst_pad_push (mysrcpad, inbuffer) == GST_FLOW_OK);
  fail_unless (gst_pad_push_event (mysrcpad, gst_event_new_eos ()));
  /* ... and puts a new buffer on the global list */
  fail_unless (g_list_length (buffers) >= 1);

  for (node = buffers; node; node = node->next) {
    gint buffer_length;

    fail_if ((outbuffer = (GstBuffer *) node->data) == NULL);

    res = (gfloat *) GST_BUFFER_DATA (outbuffer);
    buffer_length = GST_BUFFER_SIZE (outbuffer) / sizeof (gfloat);
    rms = 0.0;
    for (i = 0; i < buffer_length; i++)
      rms += res[i] * res[i];
    rms = sqrt (rms / buffer_length);
    fail_unless (rms <= 0.1);
  }

  /* cleanup */
  cleanup_audiowsincband (audiowsincband);
}

GST_END_TEST;

/* Test if data containing only one frequency component
 * at the band center is preserved with bandreject mode
 * and a 2000Hz frequency band around rate/4 */
GST_START_TEST (test_32_bp_11025hz)
{
  GstElement *audiowsincband;
  GstBuffer *inbuffer, *outbuffer;
  GstCaps *caps;
  gfloat *in, *res, rms;
  gint i;
  GList *node;

  audiowsincband = setup_audiowsincband ();
  /* Set to bandpass */
  g_object_set (G_OBJECT (audiowsincband), "mode", 0, NULL);
  g_object_set (G_OBJECT (audiowsincband), "length", 31, NULL);

  fail_unless (gst_element_set_state (audiowsincband,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_SUCCESS,
      "could not set to playing");

  g_object_set (G_OBJECT (audiowsincband), "lower-frequency",
      44100 / 4.0 - 1000, NULL);
  g_object_set (G_OBJECT (audiowsincband), "upper-frequency",
      44100 / 4.0 + 1000, NULL);
  inbuffer = gst_buffer_new_and_alloc (1024 * sizeof (gfloat));
  in = (gfloat *) GST_BUFFER_DATA (inbuffer);
  for (i = 0; i < 1024; i += 4) {
    in[i] = 0.0;
    in[i + 1] = 1.0;
    in[i + 2] = 0.0;
    in[i + 3] = -1.0;
  }

  caps = gst_caps_from_string (AUDIO_WSINC_BAND_CAPS_STRING_32);
  gst_buffer_set_caps (inbuffer, caps);
  gst_caps_unref (caps);
  ASSERT_BUFFER_REFCOUNT (inbuffer, "inbuffer", 1);

  /* pushing gives away my reference ... */
  fail_unless (gst_pad_push (mysrcpad, inbuffer) == GST_FLOW_OK);
  fail_unless (gst_pad_push_event (mysrcpad, gst_event_new_eos ()));
  /* ... and puts a new buffer on the global list */
  fail_unless (g_list_length (buffers) >= 1);

  for (node = buffers; node; node = node->next) {
    gint buffer_length;

    fail_if ((outbuffer = (GstBuffer *) node->data) == NULL);

    res = (gfloat *) GST_BUFFER_DATA (outbuffer);
    buffer_length = GST_BUFFER_SIZE (outbuffer) / sizeof (gfloat);
    rms = 0.0;
    for (i = 0; i < buffer_length; i++)
      rms += res[i] * res[i];
    rms = sqrt (rms / buffer_length);
    fail_unless (rms >= 0.4);
  }

  /* cleanup */
  cleanup_audiowsincband (audiowsincband);
}

GST_END_TEST;


/* Test if data containing only one frequency component
 * at rate/2 is erased with bandreject mode and a 
 * 2000Hz frequency band around rate/4 */
GST_START_TEST (test_32_bp_22050hz)
{
  GstElement *audiowsincband;
  GstBuffer *inbuffer, *outbuffer;
  GstCaps *caps;
  gfloat *in, *res, rms;
  gint i;
  GList *node;

  audiowsincband = setup_audiowsincband ();
  /* Set to bandpass */
  g_object_set (G_OBJECT (audiowsincband), "mode", 0, NULL);
  g_object_set (G_OBJECT (audiowsincband), "length", 31, NULL);

  fail_unless (gst_element_set_state (audiowsincband,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_SUCCESS,
      "could not set to playing");

  g_object_set (G_OBJECT (audiowsincband), "lower-frequency",
      44100 / 4.0 - 1000, NULL);
  g_object_set (G_OBJECT (audiowsincband), "upper-frequency",
      44100 / 4.0 + 1000, NULL);
  inbuffer = gst_buffer_new_and_alloc (1024 * sizeof (gfloat));
  in = (gfloat *) GST_BUFFER_DATA (inbuffer);
  for (i = 0; i < 1024; i += 2) {
    in[i] = 1.0;
    in[i + 1] = -1.0;
  }

  caps = gst_caps_from_string (AUDIO_WSINC_BAND_CAPS_STRING_32);
  gst_buffer_set_caps (inbuffer, caps);
  gst_caps_unref (caps);
  ASSERT_BUFFER_REFCOUNT (inbuffer, "inbuffer", 1);

  /* pushing gives away my reference ... */
  fail_unless (gst_pad_push (mysrcpad, inbuffer) == GST_FLOW_OK);
  fail_unless (gst_pad_push_event (mysrcpad, gst_event_new_eos ()));
  /* ... and puts a new buffer on the global list */
  fail_unless (g_list_length (buffers) >= 1);

  for (node = buffers; node; node = node->next) {
    gint buffer_length;

    fail_if ((outbuffer = (GstBuffer *) node->data) == NULL);

    res = (gfloat *) GST_BUFFER_DATA (outbuffer);
    buffer_length = GST_BUFFER_SIZE (outbuffer) / sizeof (gfloat);
    rms = 0.0;
    for (i = 0; i < buffer_length; i++)
      rms += res[i] * res[i];
    rms = sqrt (rms / buffer_length);
    fail_unless (rms <= 0.3);
  }

  /* cleanup */
  cleanup_audiowsincband (audiowsincband);
}

GST_END_TEST;

/* Test if data containing only one frequency component
 * at rate/2 is preserved with bandreject mode and a 
 * 2000Hz frequency band around rate/4 */
GST_START_TEST (test_32_br_0hz)
{
  GstElement *audiowsincband;
  GstBuffer *inbuffer, *outbuffer;
  GstCaps *caps;
  gfloat *in, *res, rms;
  gint i;
  GList *node;

  audiowsincband = setup_audiowsincband ();
  /* Set to bandreject */
  g_object_set (G_OBJECT (audiowsincband), "mode", 1, NULL);
  g_object_set (G_OBJECT (audiowsincband), "length", 31, NULL);

  fail_unless (gst_element_set_state (audiowsincband,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_SUCCESS,
      "could not set to playing");

  g_object_set (G_OBJECT (audiowsincband), "lower-frequency",
      44100 / 4.0 - 1000, NULL);
  g_object_set (G_OBJECT (audiowsincband), "upper-frequency",
      44100 / 4.0 + 1000, NULL);
  inbuffer = gst_buffer_new_and_alloc (1024 * sizeof (gfloat));
  in = (gfloat *) GST_BUFFER_DATA (inbuffer);
  for (i = 0; i < 1024; i++)
    in[i] = 1.0;

  caps = gst_caps_from_string (AUDIO_WSINC_BAND_CAPS_STRING_32);
  gst_buffer_set_caps (inbuffer, caps);
  gst_caps_unref (caps);
  ASSERT_BUFFER_REFCOUNT (inbuffer, "inbuffer", 1);

  /* pushing gives away my reference ... */
  fail_unless (gst_pad_push (mysrcpad, inbuffer) == GST_FLOW_OK);
  fail_unless (gst_pad_push_event (mysrcpad, gst_event_new_eos ()));
  /* ... and puts a new buffer on the global list */
  fail_unless (g_list_length (buffers) >= 1);

  for (node = buffers; node; node = node->next) {
    gint buffer_length;

    fail_if ((outbuffer = (GstBuffer *) node->data) == NULL);

    res = (gfloat *) GST_BUFFER_DATA (outbuffer);
    buffer_length = GST_BUFFER_SIZE (outbuffer) / sizeof (gfloat);
    rms = 0.0;
    for (i = 0; i < buffer_length; i++)
      rms += res[i] * res[i];
    rms = sqrt (rms / buffer_length);
    fail_unless (rms >= 0.9);
  }

  /* cleanup */
  cleanup_audiowsincband (audiowsincband);
}

GST_END_TEST;

/* Test if data containing only one frequency component
 * at the band center is erased with bandreject mode
 * and a 2000Hz frequency band around rate/4 */
GST_START_TEST (test_32_br_11025hz)
{
  GstElement *audiowsincband;
  GstBuffer *inbuffer, *outbuffer;
  GstCaps *caps;
  gfloat *in, *res, rms;
  gint i;
  GList *node;

  audiowsincband = setup_audiowsincband ();
  /* Set to bandreject */
  g_object_set (G_OBJECT (audiowsincband), "mode", 1, NULL);
  g_object_set (G_OBJECT (audiowsincband), "length", 31, NULL);

  fail_unless (gst_element_set_state (audiowsincband,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_SUCCESS,
      "could not set to playing");

  g_object_set (G_OBJECT (audiowsincband), "lower-frequency",
      44100 / 4.0 - 1000, NULL);
  g_object_set (G_OBJECT (audiowsincband), "upper-frequency",
      44100 / 4.0 + 1000, NULL);
  inbuffer = gst_buffer_new_and_alloc (1024 * sizeof (gfloat));
  in = (gfloat *) GST_BUFFER_DATA (inbuffer);

  for (i = 0; i < 1024; i += 4) {
    in[i] = 0.0;
    in[i + 1] = 1.0;
    in[i + 2] = 0.0;
    in[i + 3] = -1.0;
  }

  caps = gst_caps_from_string (AUDIO_WSINC_BAND_CAPS_STRING_32);
  gst_buffer_set_caps (inbuffer, caps);
  gst_caps_unref (caps);
  ASSERT_BUFFER_REFCOUNT (inbuffer, "inbuffer", 1);

  /* pushing gives away my reference ... */
  fail_unless (gst_pad_push (mysrcpad, inbuffer) == GST_FLOW_OK);
  fail_unless (gst_pad_push_event (mysrcpad, gst_event_new_eos ()));
  /* ... and puts a new buffer on the global list */
  fail_unless (g_list_length (buffers) >= 1);

  for (node = buffers; node; node = node->next) {
    gint buffer_length;

    fail_if ((outbuffer = (GstBuffer *) node->data) == NULL);

    res = (gfloat *) GST_BUFFER_DATA (outbuffer);
    buffer_length = GST_BUFFER_SIZE (outbuffer) / sizeof (gfloat);
    rms = 0.0;
    for (i = 0; i < buffer_length; i++)
      rms += res[i] * res[i];
    rms = sqrt (rms / buffer_length);
    fail_unless (rms <= 0.35);
  }

  /* cleanup */
  cleanup_audiowsincband (audiowsincband);
}

GST_END_TEST;


/* Test if data containing only one frequency component
 * at rate/2 is preserved with bandreject mode and a 
 * 2000Hz frequency band around rate/4 */
GST_START_TEST (test_32_br_22050hz)
{
  GstElement *audiowsincband;
  GstBuffer *inbuffer, *outbuffer;
  GstCaps *caps;
  gfloat *in, *res, rms;
  gint i;
  GList *node;

  audiowsincband = setup_audiowsincband ();
  /* Set to bandreject */
  g_object_set (G_OBJECT (audiowsincband), "mode", 1, NULL);
  g_object_set (G_OBJECT (audiowsincband), "length", 31, NULL);

  fail_unless (gst_element_set_state (audiowsincband,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_SUCCESS,
      "could not set to playing");

  g_object_set (G_OBJECT (audiowsincband), "lower-frequency",
      44100 / 4.0 - 1000, NULL);
  g_object_set (G_OBJECT (audiowsincband), "upper-frequency",
      44100 / 4.0 + 1000, NULL);
  inbuffer = gst_buffer_new_and_alloc (1024 * sizeof (gfloat));
  in = (gfloat *) GST_BUFFER_DATA (inbuffer);
  for (i = 0; i < 1024; i += 2) {
    in[i] = 1.0;
    in[i + 1] = -1.0;
  }

  caps = gst_caps_from_string (AUDIO_WSINC_BAND_CAPS_STRING_32);
  gst_buffer_set_caps (inbuffer, caps);
  gst_caps_unref (caps);
  ASSERT_BUFFER_REFCOUNT (inbuffer, "inbuffer", 1);

  /* pushing gives away my reference ... */
  fail_unless (gst_pad_push (mysrcpad, inbuffer) == GST_FLOW_OK);
  fail_unless (gst_pad_push_event (mysrcpad, gst_event_new_eos ()));
  /* ... and puts a new buffer on the global list */
  fail_unless (g_list_length (buffers) >= 1);

  for (node = buffers; node; node = node->next) {
    gint buffer_length;

    fail_if ((outbuffer = (GstBuffer *) node->data) == NULL);

    res = (gfloat *) GST_BUFFER_DATA (outbuffer);
    buffer_length = GST_BUFFER_SIZE (outbuffer) / sizeof (gfloat);
    rms = 0.0;
    for (i = 0; i < buffer_length; i++)
      rms += res[i] * res[i];
    rms = sqrt (rms / buffer_length);
    fail_unless (rms >= 0.9);
  }

  /* cleanup */
  cleanup_audiowsincband (audiowsincband);
}

GST_END_TEST;

/* Test if buffers smaller than the kernel size are handled
 * correctly without accessing wrong memory areas */
GST_START_TEST (test_32_small_buffer)
{
  GstElement *audiowsincband;
  GstBuffer *inbuffer;
  GstCaps *caps;
  gfloat *in;
  gint i;

  audiowsincband = setup_audiowsincband ();
  /* Set to bandpass */
  g_object_set (G_OBJECT (audiowsincband), "mode", 0, NULL);
  g_object_set (G_OBJECT (audiowsincband), "length", 101, NULL);

  fail_unless (gst_element_set_state (audiowsincband,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_SUCCESS,
      "could not set to playing");

  g_object_set (G_OBJECT (audiowsincband), "lower-frequency",
      44100 / 4.0 - 44100 / 16.0, NULL);
  g_object_set (G_OBJECT (audiowsincband), "upper-frequency",
      44100 / 4.0 + 44100 / 16.0, NULL);
  inbuffer = gst_buffer_new_and_alloc (20 * sizeof (gfloat));
  in = (gfloat *) GST_BUFFER_DATA (inbuffer);
  for (i = 0; i < 20; i++)
    in[i] = 1.0;

  caps = gst_caps_from_string (AUDIO_WSINC_BAND_CAPS_STRING_32);
  gst_buffer_set_caps (inbuffer, caps);
  gst_caps_unref (caps);
  ASSERT_BUFFER_REFCOUNT (inbuffer, "inbuffer", 1);

  /* pushing gives away my reference ... */
  fail_unless (gst_pad_push (mysrcpad, inbuffer) == GST_FLOW_OK);
  fail_unless (gst_pad_push_event (mysrcpad, gst_event_new_eos ()));
  /* ... and puts a new buffer on the global list */
  fail_unless (g_list_length (buffers) >= 1);

  /* cleanup */
  cleanup_audiowsincband (audiowsincband);
}

GST_END_TEST;









/* Test if data containing only one frequency component
 * at rate/2 is erased with bandpass mode and a 
 * 2000Hz frequency band around rate/4 */
GST_START_TEST (test_64_bp_0hz)
{
  GstElement *audiowsincband;
  GstBuffer *inbuffer, *outbuffer;
  GstCaps *caps;
  gdouble *in, *res, rms;
  gint i;
  GList *node;

  audiowsincband = setup_audiowsincband ();
  /* Set to bandpass */
  g_object_set (G_OBJECT (audiowsincband), "mode", 0, NULL);
  g_object_set (G_OBJECT (audiowsincband), "length", 31, NULL);

  fail_unless (gst_element_set_state (audiowsincband,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_SUCCESS,
      "could not set to playing");

  g_object_set (G_OBJECT (audiowsincband), "lower-frequency",
      44100 / 4.0 - 1000, NULL);
  g_object_set (G_OBJECT (audiowsincband), "upper-frequency",
      44100 / 4.0 + 1000, NULL);
  inbuffer = gst_buffer_new_and_alloc (1024 * sizeof (gdouble));
  in = (gdouble *) GST_BUFFER_DATA (inbuffer);
  for (i = 0; i < 1024; i++)
    in[i] = 1.0;

  caps = gst_caps_from_string (AUDIO_WSINC_BAND_CAPS_STRING_64);
  gst_buffer_set_caps (inbuffer, caps);
  gst_caps_unref (caps);
  ASSERT_BUFFER_REFCOUNT (inbuffer, "inbuffer", 1);

  /* pushing gives away my reference ... */
  fail_unless (gst_pad_push (mysrcpad, inbuffer) == GST_FLOW_OK);
  fail_unless (gst_pad_push_event (mysrcpad, gst_event_new_eos ()));
  /* ... and puts a new buffer on the global list */
  fail_unless (g_list_length (buffers) >= 1);

  for (node = buffers; node; node = node->next) {
    gint buffer_length;

    fail_if ((outbuffer = (GstBuffer *) node->data) == NULL);

    res = (gdouble *) GST_BUFFER_DATA (outbuffer);
    buffer_length = GST_BUFFER_SIZE (outbuffer) / sizeof (gdouble);
    rms = 0.0;
    for (i = 0; i < buffer_length; i++)
      rms += res[i] * res[i];
    rms = sqrt (rms / buffer_length);
    fail_unless (rms <= 0.1);
  }

  /* cleanup */
  cleanup_audiowsincband (audiowsincband);
}

GST_END_TEST;

/* Test if data containing only one frequency component
 * at the band center is preserved with bandreject mode
 * and a 2000Hz frequency band around rate/4 */
GST_START_TEST (test_64_bp_11025hz)
{
  GstElement *audiowsincband;
  GstBuffer *inbuffer, *outbuffer;
  GstCaps *caps;
  gdouble *in, *res, rms;
  gint i;
  GList *node;

  audiowsincband = setup_audiowsincband ();
  /* Set to bandpass */
  g_object_set (G_OBJECT (audiowsincband), "mode", 0, NULL);
  g_object_set (G_OBJECT (audiowsincband), "length", 31, NULL);

  fail_unless (gst_element_set_state (audiowsincband,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_SUCCESS,
      "could not set to playing");

  g_object_set (G_OBJECT (audiowsincband), "lower-frequency",
      44100 / 4.0 - 1000, NULL);
  g_object_set (G_OBJECT (audiowsincband), "upper-frequency",
      44100 / 4.0 + 1000, NULL);
  inbuffer = gst_buffer_new_and_alloc (1024 * sizeof (gdouble));
  in = (gdouble *) GST_BUFFER_DATA (inbuffer);
  for (i = 0; i < 1024; i += 4) {
    in[i] = 0.0;
    in[i + 1] = 1.0;
    in[i + 2] = 0.0;
    in[i + 3] = -1.0;
  }

  caps = gst_caps_from_string (AUDIO_WSINC_BAND_CAPS_STRING_64);
  gst_buffer_set_caps (inbuffer, caps);
  gst_caps_unref (caps);
  ASSERT_BUFFER_REFCOUNT (inbuffer, "inbuffer", 1);

  /* pushing gives away my reference ... */
  fail_unless (gst_pad_push (mysrcpad, inbuffer) == GST_FLOW_OK);
  fail_unless (gst_pad_push_event (mysrcpad, gst_event_new_eos ()));
  /* ... and puts a new buffer on the global list */
  fail_unless (g_list_length (buffers) >= 1);

  for (node = buffers; node; node = node->next) {
    gint buffer_length;

    fail_if ((outbuffer = (GstBuffer *) node->data) == NULL);

    res = (gdouble *) GST_BUFFER_DATA (outbuffer);
    buffer_length = GST_BUFFER_SIZE (outbuffer) / sizeof (gdouble);
    rms = 0.0;
    for (i = 0; i < buffer_length; i++)
      rms += res[i] * res[i];
    rms = sqrt (rms / buffer_length);
    fail_unless (rms >= 0.4);
  }

  /* cleanup */
  cleanup_audiowsincband (audiowsincband);
}

GST_END_TEST;


/* Test if data containing only one frequency component
 * at rate/2 is erased with bandreject mode and a 
 * 2000Hz frequency band around rate/4 */
GST_START_TEST (test_64_bp_22050hz)
{
  GstElement *audiowsincband;
  GstBuffer *inbuffer, *outbuffer;
  GstCaps *caps;
  gdouble *in, *res, rms;
  gint i;
  GList *node;

  audiowsincband = setup_audiowsincband ();
  /* Set to bandpass */
  g_object_set (G_OBJECT (audiowsincband), "mode", 0, NULL);
  g_object_set (G_OBJECT (audiowsincband), "length", 31, NULL);

  fail_unless (gst_element_set_state (audiowsincband,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_SUCCESS,
      "could not set to playing");

  g_object_set (G_OBJECT (audiowsincband), "lower-frequency",
      44100 / 4.0 - 1000, NULL);
  g_object_set (G_OBJECT (audiowsincband), "upper-frequency",
      44100 / 4.0 + 1000, NULL);
  inbuffer = gst_buffer_new_and_alloc (1024 * sizeof (gdouble));
  in = (gdouble *) GST_BUFFER_DATA (inbuffer);
  for (i = 0; i < 1024; i += 2) {
    in[i] = 1.0;
    in[i + 1] = -1.0;
  }

  caps = gst_caps_from_string (AUDIO_WSINC_BAND_CAPS_STRING_64);
  gst_buffer_set_caps (inbuffer, caps);
  gst_caps_unref (caps);
  ASSERT_BUFFER_REFCOUNT (inbuffer, "inbuffer", 1);

  /* pushing gives away my reference ... */
  fail_unless (gst_pad_push (mysrcpad, inbuffer) == GST_FLOW_OK);
  fail_unless (gst_pad_push_event (mysrcpad, gst_event_new_eos ()));
  /* ... and puts a new buffer on the global list */
  fail_unless (g_list_length (buffers) >= 1);

  for (node = buffers; node; node = node->next) {
    gint buffer_length;

    fail_if ((outbuffer = (GstBuffer *) node->data) == NULL);

    res = (gdouble *) GST_BUFFER_DATA (outbuffer);
    buffer_length = GST_BUFFER_SIZE (outbuffer) / sizeof (gdouble);
    rms = 0.0;
    for (i = 0; i < buffer_length; i++)
      rms += res[i] * res[i];
    rms = sqrt (rms / buffer_length);
    fail_unless (rms <= 0.3);
  }

  /* cleanup */
  cleanup_audiowsincband (audiowsincband);
}

GST_END_TEST;

/* Test if data containing only one frequency component
 * at rate/2 is preserved with bandreject mode and a 
 * 2000Hz frequency band around rate/4 */
GST_START_TEST (test_64_br_0hz)
{
  GstElement *audiowsincband;
  GstBuffer *inbuffer, *outbuffer;
  GstCaps *caps;
  gdouble *in, *res, rms;
  gint i;
  GList *node;

  audiowsincband = setup_audiowsincband ();
  /* Set to bandreject */
  g_object_set (G_OBJECT (audiowsincband), "mode", 1, NULL);
  g_object_set (G_OBJECT (audiowsincband), "length", 31, NULL);

  fail_unless (gst_element_set_state (audiowsincband,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_SUCCESS,
      "could not set to playing");

  g_object_set (G_OBJECT (audiowsincband), "lower-frequency",
      44100 / 4.0 - 1000, NULL);
  g_object_set (G_OBJECT (audiowsincband), "upper-frequency",
      44100 / 4.0 + 1000, NULL);
  inbuffer = gst_buffer_new_and_alloc (1024 * sizeof (gdouble));
  in = (gdouble *) GST_BUFFER_DATA (inbuffer);
  for (i = 0; i < 1024; i++)
    in[i] = 1.0;

  caps = gst_caps_from_string (AUDIO_WSINC_BAND_CAPS_STRING_64);
  gst_buffer_set_caps (inbuffer, caps);
  gst_caps_unref (caps);
  ASSERT_BUFFER_REFCOUNT (inbuffer, "inbuffer", 1);

  /* pushing gives away my reference ... */
  fail_unless (gst_pad_push (mysrcpad, inbuffer) == GST_FLOW_OK);
  fail_unless (gst_pad_push_event (mysrcpad, gst_event_new_eos ()));
  /* ... and puts a new buffer on the global list */
  fail_unless (g_list_length (buffers) >= 1);

  for (node = buffers; node; node = node->next) {
    gint buffer_length;

    fail_if ((outbuffer = (GstBuffer *) node->data) == NULL);

    res = (gdouble *) GST_BUFFER_DATA (outbuffer);
    buffer_length = GST_BUFFER_SIZE (outbuffer) / sizeof (gdouble);
    rms = 0.0;
    for (i = 0; i < buffer_length; i++)
      rms += res[i] * res[i];
    rms = sqrt (rms / buffer_length);
    fail_unless (rms >= 0.9);
  }

  /* cleanup */
  cleanup_audiowsincband (audiowsincband);
}

GST_END_TEST;

/* Test if data containing only one frequency component
 * at the band center is erased with bandreject mode
 * and a 2000Hz frequency band around rate/4 */
GST_START_TEST (test_64_br_11025hz)
{
  GstElement *audiowsincband;
  GstBuffer *inbuffer, *outbuffer;
  GstCaps *caps;
  gdouble *in, *res, rms;
  gint i;
  GList *node;

  audiowsincband = setup_audiowsincband ();
  /* Set to bandreject */
  g_object_set (G_OBJECT (audiowsincband), "mode", 1, NULL);
  g_object_set (G_OBJECT (audiowsincband), "length", 31, NULL);

  fail_unless (gst_element_set_state (audiowsincband,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_SUCCESS,
      "could not set to playing");

  g_object_set (G_OBJECT (audiowsincband), "lower-frequency",
      44100 / 4.0 - 1000, NULL);
  g_object_set (G_OBJECT (audiowsincband), "upper-frequency",
      44100 / 4.0 + 1000, NULL);
  inbuffer = gst_buffer_new_and_alloc (1024 * sizeof (gdouble));
  in = (gdouble *) GST_BUFFER_DATA (inbuffer);

  for (i = 0; i < 1024; i += 4) {
    in[i] = 0.0;
    in[i + 1] = 1.0;
    in[i + 2] = 0.0;
    in[i + 3] = -1.0;
  }

  caps = gst_caps_from_string (AUDIO_WSINC_BAND_CAPS_STRING_64);
  gst_buffer_set_caps (inbuffer, caps);
  gst_caps_unref (caps);
  ASSERT_BUFFER_REFCOUNT (inbuffer, "inbuffer", 1);

  /* pushing gives away my reference ... */
  fail_unless (gst_pad_push (mysrcpad, inbuffer) == GST_FLOW_OK);
  fail_unless (gst_pad_push_event (mysrcpad, gst_event_new_eos ()));
  /* ... and puts a new buffer on the global list */
  fail_unless (g_list_length (buffers) >= 1);

  for (node = buffers; node; node = node->next) {
    gint buffer_length;

    fail_if ((outbuffer = (GstBuffer *) node->data) == NULL);

    res = (gdouble *) GST_BUFFER_DATA (outbuffer);
    buffer_length = GST_BUFFER_SIZE (outbuffer) / sizeof (gdouble);
    rms = 0.0;
    for (i = 0; i < buffer_length; i++)
      rms += res[i] * res[i];
    rms = sqrt (rms / buffer_length);
    fail_unless (rms <= 0.35);
  }

  /* cleanup */
  cleanup_audiowsincband (audiowsincband);
}

GST_END_TEST;


/* Test if data containing only one frequency component
 * at rate/2 is preserved with bandreject mode and a 
 * 2000Hz frequency band around rate/4 */
GST_START_TEST (test_64_br_22050hz)
{
  GstElement *audiowsincband;
  GstBuffer *inbuffer, *outbuffer;
  GstCaps *caps;
  gdouble *in, *res, rms;
  gint i;
  GList *node;

  audiowsincband = setup_audiowsincband ();
  /* Set to bandreject */
  g_object_set (G_OBJECT (audiowsincband), "mode", 1, NULL);
  g_object_set (G_OBJECT (audiowsincband), "length", 31, NULL);

  fail_unless (gst_element_set_state (audiowsincband,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_SUCCESS,
      "could not set to playing");

  g_object_set (G_OBJECT (audiowsincband), "lower-frequency",
      44100 / 4.0 - 1000, NULL);
  g_object_set (G_OBJECT (audiowsincband), "upper-frequency",
      44100 / 4.0 + 1000, NULL);
  inbuffer = gst_buffer_new_and_alloc (1024 * sizeof (gdouble));
  in = (gdouble *) GST_BUFFER_DATA (inbuffer);
  for (i = 0; i < 1024; i += 2) {
    in[i] = 1.0;
    in[i + 1] = -1.0;
  }

  caps = gst_caps_from_string (AUDIO_WSINC_BAND_CAPS_STRING_64);
  gst_buffer_set_caps (inbuffer, caps);
  gst_caps_unref (caps);
  ASSERT_BUFFER_REFCOUNT (inbuffer, "inbuffer", 1);

  /* pushing gives away my reference ... */
  fail_unless (gst_pad_push (mysrcpad, inbuffer) == GST_FLOW_OK);
  fail_unless (gst_pad_push_event (mysrcpad, gst_event_new_eos ()));
  /* ... and puts a new buffer on the global list */
  fail_unless (g_list_length (buffers) >= 1);

  for (node = buffers; node; node = node->next) {
    gint buffer_length;

    fail_if ((outbuffer = (GstBuffer *) node->data) == NULL);

    res = (gdouble *) GST_BUFFER_DATA (outbuffer);
    buffer_length = GST_BUFFER_SIZE (outbuffer) / sizeof (gdouble);
    rms = 0.0;
    for (i = 0; i < buffer_length; i++)
      rms += res[i] * res[i];
    rms = sqrt (rms / buffer_length);
    fail_unless (rms >= 0.9);
  }

  /* cleanup */
  cleanup_audiowsincband (audiowsincband);
}

GST_END_TEST;

/* Test if buffers smaller than the kernel size are handled
 * correctly without accessing wrong memory areas */
GST_START_TEST (test_64_small_buffer)
{
  GstElement *audiowsincband;
  GstBuffer *inbuffer;
  GstCaps *caps;
  gdouble *in;
  gint i;

  audiowsincband = setup_audiowsincband ();
  /* Set to bandpass */
  g_object_set (G_OBJECT (audiowsincband), "mode", 0, NULL);
  g_object_set (G_OBJECT (audiowsincband), "length", 101, NULL);

  fail_unless (gst_element_set_state (audiowsincband,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_SUCCESS,
      "could not set to playing");

  g_object_set (G_OBJECT (audiowsincband), "lower-frequency",
      44100 / 4.0 - 44100 / 16.0, NULL);
  g_object_set (G_OBJECT (audiowsincband), "upper-frequency",
      44100 / 4.0 + 44100 / 16.0, NULL);
  inbuffer = gst_buffer_new_and_alloc (20 * sizeof (gdouble));
  in = (gdouble *) GST_BUFFER_DATA (inbuffer);
  for (i = 0; i < 20; i++)
    in[i] = 1.0;

  caps = gst_caps_from_string (AUDIO_WSINC_BAND_CAPS_STRING_64);
  gst_buffer_set_caps (inbuffer, caps);
  gst_caps_unref (caps);
  ASSERT_BUFFER_REFCOUNT (inbuffer, "inbuffer", 1);

  /* pushing gives away my reference ... */
  fail_unless (gst_pad_push (mysrcpad, inbuffer) == GST_FLOW_OK);
  fail_unless (gst_pad_push_event (mysrcpad, gst_event_new_eos ()));
  /* ... and puts a new buffer on the global list */
  fail_unless (g_list_length (buffers) >= 1);

  /* cleanup */
  cleanup_audiowsincband (audiowsincband);
}

GST_END_TEST;

Suite *
audiowsincband_suite (void)
{
  Suite *s = suite_create ("audiowsincband");
  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_32_bp_0hz);
  tcase_add_test (tc_chain, test_32_bp_11025hz);
  tcase_add_test (tc_chain, test_32_bp_22050hz);
  tcase_add_test (tc_chain, test_32_br_0hz);
  tcase_add_test (tc_chain, test_32_br_11025hz);
  tcase_add_test (tc_chain, test_32_br_22050hz);
  tcase_add_test (tc_chain, test_32_small_buffer);
  tcase_add_test (tc_chain, test_64_bp_0hz);
  tcase_add_test (tc_chain, test_64_bp_11025hz);
  tcase_add_test (tc_chain, test_64_bp_22050hz);
  tcase_add_test (tc_chain, test_64_br_0hz);
  tcase_add_test (tc_chain, test_64_br_11025hz);
  tcase_add_test (tc_chain, test_64_br_22050hz);
  tcase_add_test (tc_chain, test_64_small_buffer);

  return s;
}

int
main (int argc, char **argv)
{
  int nf;

  Suite *s = audiowsincband_suite ();
  SRunner *sr = srunner_create (s);

  gst_check_init (&argc, &argv);

  srunner_run_all (sr, CK_NORMAL);
  nf = srunner_ntests_failed (sr);
  srunner_free (sr);

  return nf;
}
