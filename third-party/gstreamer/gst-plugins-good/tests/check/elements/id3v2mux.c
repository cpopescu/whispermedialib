/* GStreamer
 *
 * unit test for the taglib-based id3v2mux element
 *
 * Copyright (C) 2006 Tim-Philipp Müller  <tim centricular net>
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

#include <gst/check/gstcheck.h>

#include <gst/gst.h>
#include <string.h>

#define TEST_ARTIST           "Ar T\303\255st"
#define TEST_TITLE            "M\303\274llermilch!"
#define TEST_ALBUM            "Boom"
#define TEST_DATE             g_date_new_dmy(1,1,2006)
#define TEST_TRACK_NUMBER     7
#define TEST_TRACK_COUNT      19
#define TEST_VOLUME_NUMBER    2
#define TEST_VOLUME_COUNT     3

/* #define TEST_TRACK_GAIN      1.45  (not implemented yet) */
/* #define TEST_ALBUM_GAIN      0.78  (not implemented yet) */

/* for dummy mp3 frame sized MP3_FRAME_SIZE bytes,
 * start: ff fb b0 44 00 00 08 00  00 4b 00 00 00 00 00 00 */
static const guint8 mp3_dummyhdr[] = { 0xff, 0xfb, 0xb0, 0x44, 0x00, 0x00,
  0x08, 0x00, 0x4b, 0x00, 0x00, 0x00, 0x00, 0x20, 0x00, 0x00
};

#define MP3_FRAME_SIZE 626

static GstTagList *
test_taglib_id3mux_create_tags (guint32 mask)
{
  GstTagList *tags;

  tags = gst_tag_list_new ();

  if (mask & (1 << 0)) {
    gst_tag_list_add (tags, GST_TAG_MERGE_KEEP,
        GST_TAG_ARTIST, TEST_ARTIST, NULL);
  }
  if (mask & (1 << 1)) {
    gst_tag_list_add (tags, GST_TAG_MERGE_KEEP,
        GST_TAG_TITLE, TEST_TITLE, NULL);
  }
  if (mask & (1 << 2)) {
    gst_tag_list_add (tags, GST_TAG_MERGE_KEEP,
        GST_TAG_ALBUM, TEST_ALBUM, NULL);
  }
  if (mask & (1 << 3)) {
    GDate *date;

    date = TEST_DATE;
    gst_tag_list_add (tags, GST_TAG_MERGE_KEEP, GST_TAG_DATE, date, NULL);
    g_date_free (date);
  }
  if (mask & (1 << 4)) {
    gst_tag_list_add (tags, GST_TAG_MERGE_KEEP,
        GST_TAG_TRACK_NUMBER, TEST_TRACK_NUMBER, NULL);
  }
  if (mask & (1 << 5)) {
    gst_tag_list_add (tags, GST_TAG_MERGE_KEEP,
        GST_TAG_TRACK_COUNT, TEST_TRACK_COUNT, NULL);
  }
  if (mask & (1 << 6)) {
    gst_tag_list_add (tags, GST_TAG_MERGE_KEEP,
        GST_TAG_ALBUM_VOLUME_NUMBER, TEST_VOLUME_NUMBER, NULL);
  }
  if (mask & (1 << 7)) {
    gst_tag_list_add (tags, GST_TAG_MERGE_KEEP,
        GST_TAG_ALBUM_VOLUME_COUNT, TEST_VOLUME_COUNT, NULL);
  }
  if (mask & (1 << 8)) {
  }
  if (mask & (1 << 9)) {
  }
  if (mask & (1 << 10)) {
  }
  if (mask & (1 << 11)) {
  }
  if (mask & (1 << 12)) {
  }
  if (mask & (1 << 13)) {
  }
  return tags;
}

static gboolean
utf8_string_in_buf (GstBuffer * buf, const gchar * s)
{
  gint i, len;

  len = strlen (s);
  for (i = 0; i < (GST_BUFFER_SIZE (buf) - len); ++i) {
    if (memcmp (GST_BUFFER_DATA (buf) + i, s, len) == 0) {
      return TRUE;
    }
  }

  return FALSE;
}

static void
test_taglib_id3mux_check_tag_buffer (GstBuffer * buf, guint32 mask)
{
  /* make sure our UTF-8 string hasn't been put into the tag as ISO-8859-1 */
  if (mask & (1 << 0)) {
    fail_unless (utf8_string_in_buf (buf, TEST_ARTIST));
  }
  /* make sure our UTF-8 string hasn't been put into the tag as ISO-8859-1 */
  if (mask & (1 << 1)) {
    fail_unless (utf8_string_in_buf (buf, TEST_TITLE));
  }
  /* make sure our UTF-8 string hasn't been put into the tag as ISO-8859-1 */
  if (mask & (1 << 2)) {
    fail_unless (utf8_string_in_buf (buf, TEST_ALBUM));
  }
}

static void
test_taglib_id3mux_check_tags (GstTagList * tags, guint32 mask)
{
  if (mask & (1 << 0)) {
    gchar *s = NULL;

    fail_unless (gst_tag_list_get_string (tags, GST_TAG_ARTIST, &s));
    fail_unless (g_str_equal (s, TEST_ARTIST));
    g_free (s);
  }
  if (mask & (1 << 1)) {
    gchar *s = NULL;

    fail_unless (gst_tag_list_get_string (tags, GST_TAG_TITLE, &s));
    fail_unless (g_str_equal (s, TEST_TITLE));
    g_free (s);
  }
  if (mask & (1 << 2)) {
    gchar *s = NULL;

    fail_unless (gst_tag_list_get_string (tags, GST_TAG_ALBUM, &s));
    fail_unless (g_str_equal (s, TEST_ALBUM));
    g_free (s);
  }
  if (mask & (1 << 3)) {
    GDate *shouldbe, *date = NULL;

    shouldbe = TEST_DATE;
    fail_unless (gst_tag_list_get_date (tags, GST_TAG_DATE, &date));
    fail_unless (g_date_compare (shouldbe, date) == 0);
    g_date_free (shouldbe);
    g_date_free (date);
  }
  if (mask & (1 << 4)) {
    guint num;

    fail_unless (gst_tag_list_get_uint (tags, GST_TAG_TRACK_NUMBER, &num));
    fail_unless (num == TEST_TRACK_NUMBER);
  }
  if (mask & (1 << 5)) {
    guint count;

    fail_unless (gst_tag_list_get_uint (tags, GST_TAG_TRACK_COUNT, &count));
    fail_unless (count == TEST_TRACK_COUNT);
  }
  if (mask & (1 << 6)) {
    guint num;

    fail_unless (gst_tag_list_get_uint (tags, GST_TAG_ALBUM_VOLUME_NUMBER,
            &num));
    fail_unless (num == TEST_VOLUME_NUMBER);
  }
  if (mask & (1 << 7)) {
    guint count;

    fail_unless (gst_tag_list_get_uint (tags, GST_TAG_ALBUM_VOLUME_COUNT,
            &count));
    fail_unless (count == TEST_VOLUME_COUNT);
  }
#if 0
  if (mask & (1 << 8)) {
    gdouble gain;

    fail_unless (gst_tag_list_get_double (tags, GST_TAG_TRACK_GAIN, &gain));
    fail_unless (gain == TEST_TRACK_GAIN);
  }
  if (mask & (1 << 9)) {
    gdouble gain;

    fail_unless (gst_tag_list_get_double (tags, GST_TAG_ALBUM_GAIN, &gain));
    fail_unless (gain == TEST_ALBUM_GAIN);
  }
#endif
  if (mask & (1 << 10)) {
  }
  if (mask & (1 << 11)) {
  }
  if (mask & (1 << 12)) {
  }
  if (mask & (1 << 13)) {
  }
}

static void
fill_mp3_buffer (GstElement * fakesrc, GstBuffer * buf, GstPad * pad,
    guint64 * p_offset)
{
  GstCaps *caps;

  fail_unless (GST_BUFFER_SIZE (buf) == MP3_FRAME_SIZE);

  GST_LOG ("filling buffer with fake mp3 data, offset = %" G_GUINT64_FORMAT,
      *p_offset);

  memcpy (GST_BUFFER_DATA (buf), mp3_dummyhdr, sizeof (mp3_dummyhdr));
  caps = gst_caps_new_simple ("audio/mpeg", "mpegversion", G_TYPE_INT, 1,
      "layer", G_TYPE_INT, 3, NULL);
  gst_buffer_set_caps (buf, caps);
  gst_caps_unref (caps);

  GST_BUFFER_OFFSET (buf) = *p_offset;
  *p_offset += GST_BUFFER_SIZE (buf);
}

static void
got_buffer (GstElement * fakesink, GstBuffer * buf, GstPad * pad,
    GstBuffer ** p_buf)
{
  gint64 off;
  guint size;

  off = GST_BUFFER_OFFSET (buf);
  size = GST_BUFFER_SIZE (buf);

  GST_LOG ("got buffer, size=%u, offset=%" G_GINT64_FORMAT, size, off);

  fail_unless (GST_BUFFER_OFFSET_IS_VALID (buf));

  if (*p_buf == NULL || (off + size) > GST_BUFFER_SIZE (*p_buf)) {
    GstBuffer *newbuf;

    /* not very elegant, but who cares */
    newbuf = gst_buffer_new_and_alloc (off + size);
    if (*p_buf) {
      memcpy (GST_BUFFER_DATA (newbuf), GST_BUFFER_DATA (*p_buf),
          GST_BUFFER_SIZE (*p_buf));
    }
    memcpy (GST_BUFFER_DATA (newbuf) + off, GST_BUFFER_DATA (buf), size);
    if (*p_buf)
      gst_buffer_unref (*p_buf);
    *p_buf = newbuf;
  } else {
    memcpy (GST_BUFFER_DATA (*p_buf) + off, GST_BUFFER_DATA (buf), size);
  }
}
static void
demux_pad_added (GstElement * id3demux, GstPad * srcpad, GstBuffer ** p_outbuf)
{
  GstElement *fakesink, *pipeline;

  GST_LOG ("id3demux added source pad with caps %" GST_PTR_FORMAT,
      GST_PAD_CAPS (srcpad));

  pipeline = id3demux;
  while (GST_OBJECT_PARENT (pipeline) != NULL)
    pipeline = (GstElement *) GST_OBJECT_PARENT (pipeline);

  fakesink = gst_element_factory_make ("fakesink", "fakesink");
  g_assert (fakesink != NULL);

  /* set up sink */
  g_object_set (fakesink, "signal-handoffs", TRUE, NULL);
  g_signal_connect (fakesink, "handoff", G_CALLBACK (got_buffer), p_outbuf);

  gst_bin_add (GST_BIN (pipeline), fakesink);
  gst_element_set_state (fakesink, GST_STATE_PLAYING);

  fail_unless (gst_element_link (id3demux, fakesink));
}

static void
test_taglib_id3mux_check_output_buffer (GstBuffer * buf)
{
  guint8 *data = GST_BUFFER_DATA (buf);
  guint size = GST_BUFFER_SIZE (buf);
  guint off;

  g_assert (size % MP3_FRAME_SIZE == 0);

  for (off = 0; off < size; off += MP3_FRAME_SIZE) {
    fail_unless (memcmp (data + off, mp3_dummyhdr, sizeof (mp3_dummyhdr)) == 0);
  }
}

static void
identity_cb (GstElement * identity, GstBuffer * buf, GstBuffer ** p_tagbuf)
{
  if (*p_tagbuf == NULL) {
    *p_tagbuf = gst_buffer_ref (buf);
  }
}

static void
test_taglib_id3mux_with_tags (GstTagList * tags, guint32 mask)
{
  GstMessage *msg;
  GstTagList *tags_read = NULL;
  GstElement *pipeline, *id3mux, *id3demux, *fakesrc, *identity;
  GstBus *bus;
  guint64 offset;
  GstBuffer *outbuf = NULL;
  GstBuffer *tagbuf = NULL;

  pipeline = gst_pipeline_new ("pipeline");
  g_assert (pipeline != NULL);

  fakesrc = gst_element_factory_make ("fakesrc", "fakesrc");
  g_assert (fakesrc != NULL);

  id3mux = gst_element_factory_make ("id3v2mux", "id3v2mux");
  g_assert (id3mux != NULL);

  identity = gst_element_factory_make ("identity", "identity");
  g_assert (identity != NULL);

  id3demux = gst_element_factory_make ("id3demux", "id3demux");
  g_assert (id3demux != NULL);

  outbuf = NULL;
  g_signal_connect (id3demux, "pad-added",
      G_CALLBACK (demux_pad_added), &outbuf);

  gst_bin_add (GST_BIN (pipeline), fakesrc);
  gst_bin_add (GST_BIN (pipeline), id3mux);
  gst_bin_add (GST_BIN (pipeline), identity);
  gst_bin_add (GST_BIN (pipeline), id3demux);

  gst_tag_setter_merge_tags (GST_TAG_SETTER (id3mux), tags,
      GST_TAG_MERGE_APPEND);

  gst_element_link_many (fakesrc, id3mux, identity, id3demux, NULL);

  /* set up source */
  g_object_set (fakesrc, "signal-handoffs", TRUE, "can-activate-pull", FALSE,
      "filltype", 2, "sizetype", 2, "sizemax", MP3_FRAME_SIZE,
      "num-buffers", 16, NULL);

  offset = 0;
  g_signal_connect (fakesrc, "handoff", G_CALLBACK (fill_mp3_buffer), &offset);

  /* set up identity to catch tag buffer */
  g_signal_connect (identity, "handoff", G_CALLBACK (identity_cb), &tagbuf);

  gst_element_set_state (pipeline, GST_STATE_PLAYING);
  fail_unless (gst_element_get_state (pipeline, NULL, NULL,
          -1) == GST_STATE_CHANGE_SUCCESS);

  bus = gst_pipeline_get_bus (GST_PIPELINE (pipeline));

  GST_LOG ("Waiting for tag ...");
  msg =
      gst_bus_poll (bus, GST_MESSAGE_TAG | GST_MESSAGE_EOS | GST_MESSAGE_ERROR,
      -1);
  if (GST_MESSAGE_TYPE (msg) == GST_MESSAGE_ERROR) {
    GError *err;
    gchar *dbg;

    gst_message_parse_error (msg, &err, &dbg);
    g_printerr ("ERROR from element %s: %s\n%s\n",
        GST_OBJECT_NAME (msg->src), err->message, GST_STR_NULL (dbg));
    g_error_free (err);
    g_free (dbg);
  } else if (GST_MESSAGE_TYPE (msg) == GST_MESSAGE_EOS) {
    g_printerr ("EOS message, but were waiting for TAGS!\n");
  }
  fail_unless (msg->type == GST_MESSAGE_TAG);

  gst_message_parse_tag (msg, &tags_read);
  gst_message_unref (msg);

  GST_LOG ("Got tags: %" GST_PTR_FORMAT, tags_read);
  test_taglib_id3mux_check_tags (tags_read, mask);
  gst_tag_list_free (tags_read);

  fail_unless (tagbuf != NULL);
  test_taglib_id3mux_check_tag_buffer (tagbuf, mask);
  gst_buffer_unref (tagbuf);

  GST_LOG ("Waiting for EOS ...");
  msg = gst_bus_poll (bus, GST_MESSAGE_EOS | GST_MESSAGE_ERROR, -1);
  if (GST_MESSAGE_TYPE (msg) == GST_MESSAGE_ERROR) {
    GError *err;
    gchar *dbg;

    gst_message_parse_error (msg, &err, &dbg);
    g_printerr ("ERROR from element %s: %s\n%s\n",
        GST_OBJECT_NAME (msg->src), err->message, GST_STR_NULL (dbg));
    g_error_free (err);
    g_free (dbg);
  }
  fail_unless (msg->type == GST_MESSAGE_EOS);
  gst_message_unref (msg);

  gst_object_unref (bus);

  GST_LOG ("Got EOS, shutting down ...");
  gst_element_set_state (pipeline, GST_STATE_NULL);
  gst_object_unref (pipeline);

  test_taglib_id3mux_check_output_buffer (outbuf);
  gst_buffer_unref (outbuf);

  GST_LOG ("Done");
}

GST_START_TEST (test_id3v2mux)
{
  GstTagList *tags;
  gint i;

  g_random_set_seed (247166295);

  /* internal consistency check */
  tags = test_taglib_id3mux_create_tags (0xFFFFFFFF);
  test_taglib_id3mux_check_tags (tags, 0xFFFFFFFF);
  gst_tag_list_free (tags);

  /* now the real tests */
  for (i = 0; i < 50; ++i) {
    guint32 mask;

    mask = g_random_int ();
    GST_LOG ("tag mask = %08x (i=%d)", mask, i);

    if (mask == 0)
      continue;

    /* create tags */
    tags = test_taglib_id3mux_create_tags (mask);
    GST_LOG ("tags for mask %08x = %" GST_PTR_FORMAT, mask, tags);

    /* double-check for internal consistency */
    test_taglib_id3mux_check_tags (tags, mask);

    /* test with pipeline */
    test_taglib_id3mux_with_tags (tags, mask);

    /* free tags */
    gst_tag_list_free (tags);
  }
}

GST_END_TEST;

static Suite *
id3v2mux_suite (void)
{
  Suite *s = suite_create ("id3v2mux");
  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_id3v2mux);

  return s;
}

int
main (int argc, char **argv)
{
  int nf;

  Suite *s = id3v2mux_suite ();
  SRunner *sr = srunner_create (s);

  gst_check_init (&argc, &argv);

  srunner_run_all (sr, CK_NORMAL);
  nf = srunner_ntests_failed (sr);
  srunner_free (sr);

  return nf;
}
