/* -*- Mode: C; tab-width: 2; indent-tabs-mode: t; c-basic-offset: 2 -*- */
/* GStreamer ID3 tag demuxer
 * Copyright (C) 2005 Jan Schmidt <thaytan@mad.scientist.com>
 * Copyright (C) 2003-2004 Benjamin Otte <otte@gnome.org>
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
 * SECTION:element-id3demux
 * @short_description: reads tag information from ID3v1 and ID3v2 (<= 2.4.0) data blocks and outputs them as GStreamer tag messages and events.
 *
 * <refsect2>
 * <para>
 * id3demux accepts data streams with either (or both) ID3v2 regions at the start, or ID3v1 at the end. The mime type of the data between the tag blocks is detected using typefind functions, and the appropriate output mime type set on outgoing buffers. 
 * </para><para>
 * The element is only able to read ID3v1 tags from a seekable stream, because they are at the end of the stream. That is, when get_range mode is supported by the upstream elements. If get_range operation is available, id3demux makes it available downstream. This means that elements which require get_range mode, such as wavparse, can operate on files containing ID3 tag information.
 * </para>
 * <title>Example launch line</title>
 * <para>
 * <programlisting>
 * gst-launch filesrc location=file.mp3 ! id3demux ! fakesink -t
 * </programlisting>
 * This pipeline should read any available ID3 tag information and output it. The contents of the file inside the ID3 tag regions should be detected, and the appropriate mime type set on buffers produced from id3demux.
 * </para><para>
 * This id3demux element replaced an older element with the same name which relied on libid3tag from the MAD project.
 * </para>
 * </refsect2>
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <gst/gst.h>
#include <gst/gst-i18n-plugin.h>
#include <gst/tag/tag.h>
#include <string.h>

#include "gstid3demux.h"
#include "id3tags.h"

static const GstElementDetails gst_id3demux_details =
GST_ELEMENT_DETAILS ("ID3 tag demuxer",
    "Codec/Demuxer/Metadata",
    "Read and output ID3v1 and ID3v2 tags while demuxing the contents",
    "Jan Schmidt <thaytan@mad.scientist.com>");

enum
{
  ARG_0,
  ARG_PREFER_V1
};

#define DEFAULT_PREFER_V1  FALSE

GST_DEBUG_CATEGORY (id3demux_debug);
#define GST_CAT_DEFAULT (id3demux_debug)

static GstStaticPadTemplate sink_factory = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-id3")
    );

static gboolean gst_id3demux_identify_tag (GstTagDemux * demux,
    GstBuffer * buffer, gboolean start_tag, guint * tag_size);
static GstTagDemuxResult gst_id3demux_parse_tag (GstTagDemux * demux,
    GstBuffer * buffer, gboolean start_tag, guint * tag_size,
    GstTagList ** tags);
static GstTagList *gst_id3demux_merge_tags (GstTagDemux * tagdemux,
    const GstTagList * start_tags, const GstTagList * end_tags);

static void gst_id3demux_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_id3demux_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

GST_BOILERPLATE (GstID3Demux, gst_id3demux, GstTagDemux, GST_TYPE_TAG_DEMUX);

static void
gst_id3demux_base_init (gpointer klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sink_factory));

  gst_element_class_set_details (element_class, &gst_id3demux_details);
}

static void
gst_id3demux_class_init (GstID3DemuxClass * klass)
{
  GstTagDemuxClass *tagdemux_class = (GstTagDemuxClass *) klass;
  GObjectClass *gobject_class = (GObjectClass *) klass;

  gobject_class->set_property = gst_id3demux_set_property;
  gobject_class->get_property = gst_id3demux_get_property;

  g_object_class_install_property (gobject_class, ARG_PREFER_V1,
      g_param_spec_boolean ("prefer-v1", "Prefer version 1 tag",
          "Prefer tags from ID3v1 tag at end of file when both ID3v1 "
          "and ID3v2 tags are present", DEFAULT_PREFER_V1,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT));

  tagdemux_class->identify_tag = GST_DEBUG_FUNCPTR (gst_id3demux_identify_tag);
  tagdemux_class->parse_tag = GST_DEBUG_FUNCPTR (gst_id3demux_parse_tag);
  tagdemux_class->merge_tags = GST_DEBUG_FUNCPTR (gst_id3demux_merge_tags);

  tagdemux_class->min_start_size = ID3V2_HDR_SIZE;
  tagdemux_class->min_end_size = ID3V1_TAG_SIZE;
}

static void
gst_id3demux_init (GstID3Demux * id3demux, GstID3DemuxClass * klass)
{
  id3demux->prefer_v1 = DEFAULT_PREFER_V1;
}

static gboolean
gst_id3demux_identify_tag (GstTagDemux * demux, GstBuffer * buf,
    gboolean start_tag, guint * tag_size)
{
  const guint8 *data = GST_BUFFER_DATA (buf);

  if (start_tag) {
    if (data[0] != 'I' || data[1] != 'D' || data[2] != '3')
      goto no_marker;

    *tag_size = id3demux_calc_id3v2_tag_size (buf);
  } else {
    if (data[0] != 'T' || data[1] != 'A' || data[2] != 'G')
      goto no_marker;

    *tag_size = ID3V1_TAG_SIZE;
  }

  GST_INFO_OBJECT (demux, "Found ID3v%u marker, tag_size = %u",
      (start_tag) ? 2 : 1, *tag_size);

  return TRUE;

no_marker:
  {
    GST_DEBUG_OBJECT (demux, "No ID3v%u marker found", (start_tag) ? 2 : 1);
    return FALSE;
  }
}

static GstTagDemuxResult
gst_id3demux_parse_tag (GstTagDemux * demux, GstBuffer * buffer,
    gboolean start_tag, guint * tag_size, GstTagList ** tags)
{
  if (start_tag) {
    ID3TagsResult res;          /* FIXME: make id3tags.c return tagmuxresult values */

    res = id3demux_read_id3v2_tag (buffer, tag_size, tags);

    if (G_LIKELY (res == ID3TAGS_READ_TAG))
      return GST_TAG_DEMUX_RESULT_OK;
    else
      return GST_TAG_DEMUX_RESULT_BROKEN_TAG;
  } else {
    *tags = gst_tag_list_new_from_id3v1 (GST_BUFFER_DATA (buffer));

    if (G_UNLIKELY (*tags == NULL))
      return GST_TAG_DEMUX_RESULT_BROKEN_TAG;

    *tag_size = ID3V1_TAG_SIZE;
    return GST_TAG_DEMUX_RESULT_OK;
  }
}

static GstTagList *
gst_id3demux_merge_tags (GstTagDemux * tagdemux, const GstTagList * start_tags,
    const GstTagList * end_tags)
{
  GstID3Demux *id3demux;
  GstTagList *merged;
  gboolean prefer_v1;

  id3demux = GST_ID3DEMUX (tagdemux);

  GST_OBJECT_LOCK (id3demux);
  prefer_v1 = id3demux->prefer_v1;
  GST_OBJECT_UNLOCK (id3demux);

  /* we merge in REPLACE mode, so put the less important tags first */
  if (prefer_v1)
    merged = gst_tag_list_merge (start_tags, end_tags, GST_TAG_MERGE_REPLACE);
  else
    merged = gst_tag_list_merge (end_tags, start_tags, GST_TAG_MERGE_REPLACE);

  GST_LOG_OBJECT (id3demux, "start  tags: %" GST_PTR_FORMAT, start_tags);
  GST_LOG_OBJECT (id3demux, "end    tags: %" GST_PTR_FORMAT, end_tags);
  GST_LOG_OBJECT (id3demux, "merged tags: %" GST_PTR_FORMAT, merged);

  return merged;
}

static void
gst_id3demux_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstID3Demux *id3demux;

  id3demux = GST_ID3DEMUX (object);

  switch (prop_id) {
    case ARG_PREFER_V1:{
      GST_OBJECT_LOCK (id3demux);
      id3demux->prefer_v1 = g_value_get_boolean (value);
      GST_OBJECT_UNLOCK (id3demux);
      break;
    }
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_id3demux_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstID3Demux *id3demux;

  id3demux = GST_ID3DEMUX (object);

  switch (prop_id) {
    case ARG_PREFER_V1:
      GST_OBJECT_LOCK (id3demux);
      g_value_set_boolean (value, id3demux->prefer_v1);
      GST_OBJECT_UNLOCK (id3demux);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  GST_DEBUG_CATEGORY_INIT (id3demux_debug, "id3demux", 0,
      "GStreamer ID3 tag demuxer");

  gst_tag_register_musicbrainz_tags ();

  /* ensure private tag is registered */
  gst_tag_register (GST_ID3_DEMUX_TAG_ID3V2_FRAME, GST_TAG_FLAG_META,
      GST_TYPE_BUFFER, "ID3v2 frame", "unparsed id3v2 tag frame",
      gst_tag_merge_use_first);

  return gst_element_register (plugin, "id3demux",
      GST_RANK_PRIMARY, GST_TYPE_ID3DEMUX);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "id3demux",
    "Demux ID3v1 and ID3v2 tags from a file",
    plugin_init, VERSION, GST_LICENSE, GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
