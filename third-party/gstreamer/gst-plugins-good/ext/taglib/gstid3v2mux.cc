/* GStreamer taglib-based ID3v2 muxer
 * Copyright (C) 2006 Christophe Fergeau <teuf@gnome.org>
 * Copyright (C) 2006 Tim-Philipp Müller <tim centricular net>
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
 * SECTION:element-id3v2mux
 * @see_also: #GstID3Demux, #GstTagSetter
 *
 * <refsect2>
 * <para>
 * This element adds ID3v2 tags to the beginning of a stream using the taglib
 * library. More precisely, the tags written are ID3 version 2.4.0 tags (which
 * means in practice that some hardware players or outdated programs might not
 * be able to read them properly).
 * </para>
 * <para>
 * Applications can set the tags to write using the #GstTagSetter interface.
 * Tags sent by upstream elements will be picked up automatically (and merged
 * according to the merge mode set via the tag setter interface).
 * </para>
 * <para>
 * Here is a simple pipeline that transcodes a file from Ogg/Vorbis to mp3
 * format with an ID3v2 that contains the same as the the Ogg/Vorbis file:
 * <programlisting>
 * gst-launch -v filesrc location=foo.ogg ! decodebin ! audioconvert ! lame ! id3v2mux ! filesink location=foo.mp3
 * </programlisting>
 * Make sure the Ogg/Vorbis file actually has comments to preserve.
 * You can verify the tags were written using:
 * <programlisting>
 * gst-launch -m filesrc location=foo.mp3 ! id3demux ! fakesink silent=TRUE 2&gt; /dev/null | grep taglist
 * </programlisting>
 * </para>
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "gstid3v2mux.h"

#include <string.h>

#include <textidentificationframe.h>
#include <uniquefileidentifierframe.h>
#include <attachedpictureframe.h>
#include <commentsframe.h>
#include <unknownframe.h>
#include <id3v2synchdata.h>
#include <id3v2tag.h>
#include <gst/tag/tag.h>

using namespace TagLib;

GST_DEBUG_CATEGORY_STATIC (gst_id3v2_mux_debug);
#define GST_CAT_DEFAULT gst_id3v2_mux_debug

static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-id3"));


GST_BOILERPLATE (GstId3v2Mux, gst_id3v2_mux, GstTagLibMux,
    GST_TYPE_TAG_LIB_MUX);

static GstBuffer *gst_id3v2_mux_render_tag (GstTagLibMux * mux,
    GstTagList * taglist);

static void
gst_id3v2_mux_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&src_template));

  gst_element_class_set_details_simple (element_class,
      "TagLib-based ID3v2 Muxer", "Formatter/Metadata",
      "Adds an ID3v2 header to the beginning of MP3 files using taglib",
      "Christophe Fergeau <teuf@gnome.org>");

  GST_DEBUG_CATEGORY_INIT (gst_id3v2_mux_debug, "id3v2mux", 0,
      "taglib-based ID3v2 tag muxer");
}

static void
gst_id3v2_mux_class_init (GstId3v2MuxClass * klass)
{
  GST_TAG_LIB_MUX_CLASS (klass)->render_tag =
      GST_DEBUG_FUNCPTR (gst_id3v2_mux_render_tag);
}

static void
gst_id3v2_mux_init (GstId3v2Mux * id3v2mux, GstId3v2MuxClass * id3v2mux_class)
{
  /* nothing to do */
}

#if 0
static void
add_one_txxx_tag (ID3v2::Tag * id3v2tag, const gchar * key, const gchar * val)
{
  ID3v2::UserTextIdentificationFrame * frame;

  if (val == NULL)
    return;

  GST_DEBUG ("Setting %s to %s", key, val);
  frame = new ID3v2::UserTextIdentificationFrame (String::UTF8);
  id3v2tag->addFrame (frame);
  frame->setDescription (key);
  frame->setText (val);
}
#endif

typedef void (*GstId3v2MuxAddTagFunc) (ID3v2::Tag * id3v2tag,
    const GstTagList * list,
    const gchar * tag, guint num_tags, const gchar * data);

static void
add_encoder_tag (ID3v2::Tag * id3v2tag, const GstTagList * list,
    const gchar * tag, guint num_tags, const gchar * unused)
{
  TagLib::StringList string_list;
  guint n;

  /* ENCODER_VERSION is either handled with the ENCODER tag or not at all */
  if (strcmp (tag, GST_TAG_ENCODER_VERSION) == 0)
    return;

  for (n = 0; n < num_tags; ++n) {
    gchar *encoder = NULL;

    if (gst_tag_list_get_string_index (list, tag, n, &encoder) && encoder) {
      guint encoder_version;
      gchar *s;

      if (gst_tag_list_get_uint_index (list, GST_TAG_ENCODER_VERSION, n,
              &encoder_version) && encoder_version > 0) {
        s = g_strdup_printf ("%s %u", encoder, encoder_version);
      } else {
        s = g_strdup (encoder);
      }

      GST_LOG ("encoder[%u] = '%s'", n, s);
      string_list.append (String (s, String::UTF8));
      g_free (s);
      g_free (encoder);
    }
  }

  if (!string_list.isEmpty ()) {
    ID3v2::TextIdentificationFrame * f;

    f = new ID3v2::TextIdentificationFrame ("TSSE", String::UTF8);
    id3v2tag->addFrame (f);
    f->setText (string_list);
  } else {
    GST_WARNING ("Empty list for tag %s, skipping", tag);
  }
}

static void
add_date_tag (ID3v2::Tag * id3v2tag, const GstTagList * list,
    const gchar * tag, guint num_tags, const gchar * unused)
{
  TagLib::StringList string_list;
  guint n;

  GST_LOG ("Adding date frame");

  for (n = 0; n < num_tags; ++n) {
    GDate *date = NULL;

    if (gst_tag_list_get_date_index (list, tag, n, &date) && date != NULL) {
      GDateYear year;
      gchar *s;

      year = g_date_get_year (date);
      if (year > 500 && year < 2100) {
        s = g_strdup_printf ("%u", year);
        GST_LOG ("%s[%u] = '%s'", tag, n, s);
        string_list.append (String (s, String::UTF8));
        g_free (s);
      } else {
        GST_WARNING ("invalid year %u, skipping", year);
      }

      g_date_free (date);
    }
  }

  if (!string_list.isEmpty ()) {
    ID3v2::TextIdentificationFrame * f;

    f = new ID3v2::TextIdentificationFrame ("TDRC", String::UTF8);
    id3v2tag->addFrame (f);
    f->setText (string_list);
  } else {
    GST_WARNING ("Empty list for tag %s, skipping", tag);
  }
}

static void
add_count_or_num_tag (ID3v2::Tag * id3v2tag, const GstTagList * list,
    const gchar * tag, guint num_tags, const gchar * frame_id)
{
  static const struct
  {
    const gchar *gst_tag;
    const gchar *corr_count;    /* corresponding COUNT tag (if number) */
    const gchar *corr_num;      /* corresponding NUMBER tag (if count) */
  } corr[] = {
    {
    GST_TAG_TRACK_NUMBER, GST_TAG_TRACK_COUNT, NULL}, {
    GST_TAG_TRACK_COUNT, NULL, GST_TAG_TRACK_NUMBER}, {
    GST_TAG_ALBUM_VOLUME_NUMBER, GST_TAG_ALBUM_VOLUME_COUNT, NULL}, {
    GST_TAG_ALBUM_VOLUME_COUNT, NULL, GST_TAG_ALBUM_VOLUME_NUMBER}
  };
  guint idx;

  for (idx = 0; idx < G_N_ELEMENTS (corr); ++idx) {
    if (strcmp (corr[idx].gst_tag, tag) == 0)
      break;
  }

  g_assert (idx < G_N_ELEMENTS (corr));
  g_assert (frame_id && strlen (frame_id) == 4);

  if (corr[idx].corr_num == NULL) {
    guint number;

    /* number tag */
    if (gst_tag_list_get_uint_index (list, tag, 0, &number)) {
      ID3v2::TextIdentificationFrame * frame;
      gchar *tag_str;
      guint count;

      if (gst_tag_list_get_uint_index (list, corr[idx].corr_count, 0, &count))
        tag_str = g_strdup_printf ("%u/%u", number, count);
      else
        tag_str = g_strdup_printf ("%u", number);

      GST_DEBUG ("Setting %s to %s (frame_id = %s)", tag, tag_str, frame_id);
      frame = new ID3v2::TextIdentificationFrame (frame_id, String::UTF8);
      id3v2tag->addFrame (frame);
      frame->setText (tag_str);
      g_free (tag_str);
    }
  } else if (corr[idx].corr_count == NULL) {
    guint count;

    /* count tag */
    if (gst_tag_list_get_uint_index (list, corr[idx].corr_num, 0, &count)) {
      GST_DEBUG ("%s handled with %s, skipping", tag, corr[idx].corr_num);
    } else if (gst_tag_list_get_uint_index (list, tag, 0, &count)) {
      ID3v2::TextIdentificationFrame * frame;
      gchar *tag_str;

      tag_str = g_strdup_printf ("0/%u", count);
      GST_DEBUG ("Setting %s to %s (frame_id = %s)", tag, tag_str, frame_id);
      frame = new ID3v2::TextIdentificationFrame (frame_id, String::UTF8);
      id3v2tag->addFrame (frame);
      frame->setText (tag_str);
      g_free (tag_str);
    }
  }

  if (num_tags > 1) {
    GST_WARNING ("more than one %s, can only handle one", tag);
  }
}

static void
add_unique_file_id_tag (ID3v2::Tag * id3v2tag, const GstTagList * list,
    const gchar * tag, guint num_tags, const gchar * unused)
{
  const gchar *origin = "http://musicbrainz.org";
  gchar *id_str = NULL;

  if (gst_tag_list_get_string_index (list, tag, 0, &id_str) && id_str) {
    ID3v2::UniqueFileIdentifierFrame * frame;

    GST_LOG ("Adding %s (%s): %s", tag, origin, id_str);
    frame = new ID3v2::UniqueFileIdentifierFrame (origin, id_str);
    id3v2tag->addFrame (frame);
    g_free (id_str);
  }
}

static void
add_musicbrainz_tag (ID3v2::Tag * id3v2tag, const GstTagList * list,
    const gchar * tag, guint num_tags, const gchar * data)
{
  static const struct
  {
    const gchar gst_tag[28];
    const gchar spec_id[28];
    const gchar realworld_id[28];
  } mb_ids[] = {
    {
    GST_TAG_MUSICBRAINZ_ARTISTID, "MusicBrainz Artist Id",
          "musicbrainz_artistid"}, {
    GST_TAG_MUSICBRAINZ_ALBUMID, "MusicBrainz Album Id", "musicbrainz_albumid"}, {
    GST_TAG_MUSICBRAINZ_ALBUMARTISTID, "MusicBrainz Album Artist Id",
          "musicbrainz_albumartistid"}, {
    GST_TAG_MUSICBRAINZ_TRMID, "MusicBrainz TRM Id", "musicbrainz_trmid"}, {
    GST_TAG_CDDA_MUSICBRAINZ_DISCID, "MusicBrainz DiscID",
          "musicbrainz_discid"}, {
      /* the following one is more or less made up, there seems to be little
       * evidence that any popular application is actually putting this info
       * into TXXX frames; the first one comes from a musicbrainz wiki 'proposed
       * tags' page, the second one is analogue to the vorbis/ape/flac tag. */
    GST_TAG_CDDA_CDDB_DISCID, "CDDB DiscID", "discid"}
  };
  guint i, idx;

  idx = (guint8) data[0];
  g_assert (idx < G_N_ELEMENTS (mb_ids));

  for (i = 0; i < num_tags; ++i) {
    ID3v2::UserTextIdentificationFrame * frame;
    gchar *id_str;

    if (gst_tag_list_get_string_index (list, tag, 0, &id_str) && id_str) {
      GST_DEBUG ("Setting '%s' to '%s'", mb_ids[idx].spec_id, id_str);

      /* add two frames, one with the ID the musicbrainz.org spec mentions
       * and one with the ID that applications use in the real world */
      frame = new ID3v2::UserTextIdentificationFrame (String::Latin1);
      id3v2tag->addFrame (frame);
      frame->setDescription (mb_ids[idx].spec_id);
      frame->setText (id_str);

      frame = new ID3v2::UserTextIdentificationFrame (String::Latin1);
      id3v2tag->addFrame (frame);
      frame->setDescription (mb_ids[idx].realworld_id);
      frame->setText (id_str);

      g_free (id_str);
    }
  }
}

static void
add_id3v2frame_tag (ID3v2::Tag * id3v2tag, const GstTagList * list,
    const gchar * tag, guint num_tags, const gchar * frame_id)
{
  ID3v2::FrameFactory * factory = ID3v2::FrameFactory::instance ();
  guint i;

  for (i = 0; i < num_tags; ++i) {
    ID3v2::Frame * frame;
    const GValue *val;
    GstBuffer *buf;

    val = gst_tag_list_get_value_index (list, tag, i);
    buf = (GstBuffer *) gst_value_get_mini_object (val);

    if (buf && GST_BUFFER_CAPS (buf)) {
      GstStructure *s;
      gint version = 0;

      s = gst_caps_get_structure (GST_BUFFER_CAPS (buf), 0);
      if (s && gst_structure_get_int (s, "version", &version) && version > 0) {
        ByteVector bytes ((char *) GST_BUFFER_DATA (buf),
            GST_BUFFER_SIZE (buf));

        GST_DEBUG ("Injecting ID3v2.%u frame %u/%u of length %u and type %"
            GST_PTR_FORMAT, version, i, num_tags, GST_BUFFER_SIZE (buf), s);

        frame = factory->createFrame (bytes, (TagLib::uint) version);
        if (frame)
          id3v2tag->addFrame (frame);
      }
    }
  }
}

static void
add_image_tag (ID3v2::Tag * id3v2tag, const GstTagList * list,
    const gchar * tag, guint num_tags, const gchar * unused)
{
  guint n;

  for (n = 0; n < num_tags; ++n) {
    const GValue *val;
    GstBuffer *image;

    GST_DEBUG ("image %u/%u", n + 1, num_tags);

    val = gst_tag_list_get_value_index (list, tag, n);
    image = (GstBuffer *) gst_value_get_mini_object (val);

    if (GST_IS_BUFFER (image) && GST_BUFFER_SIZE (image) > 0 &&
        GST_BUFFER_CAPS (image) != NULL &&
        !gst_caps_is_empty (GST_BUFFER_CAPS (image))) {
      const gchar *mime_type;
      GstStructure *s;

      s = gst_caps_get_structure (GST_BUFFER_CAPS (image), 0);
      mime_type = gst_structure_get_name (s);
      if (mime_type != NULL) {
        ID3v2::AttachedPictureFrame * frame;
        const gchar *desc;

        if (strcmp (mime_type, "text/uri-list") == 0)
          mime_type = "-->";

        frame = new ID3v2::AttachedPictureFrame ();

        GST_DEBUG ("Attaching picture of %u bytes and mime type %s",
            GST_BUFFER_SIZE (image), mime_type);

        id3v2tag->addFrame (frame);
        frame->setPicture (ByteVector ((const char *) GST_BUFFER_DATA (image),
                GST_BUFFER_SIZE (image)));
        frame->setTextEncoding (String::UTF8);
        frame->setMimeType (mime_type);

        desc = gst_structure_get_string (s, "image-description");
        frame->setDescription ((desc) ? desc : "");

        /* FIXME set image type properly from caps */
        if (strcmp (tag, GST_TAG_PREVIEW_IMAGE) == 0) {
          frame->setType (ID3v2::AttachedPictureFrame::FileIcon);
        } else {
          frame->setType (ID3v2::AttachedPictureFrame::Other);
        }
      }
    } else {
      GST_WARNING ("NULL image or no caps on image buffer (%p, caps=%"
          GST_PTR_FORMAT ")", image, (image) ? GST_BUFFER_CAPS (image) : NULL);
    }
  }
}

static void
add_comment_tag (ID3v2::Tag * id3v2tag, const GstTagList * list,
    const gchar * tag, guint num_tags, const gchar * unused)
{
  TagLib::StringList string_list;
  guint n;

  GST_LOG ("Adding comment frames");
  for (n = 0; n < num_tags; ++n) {
    gchar *s = NULL;

    if (gst_tag_list_get_string_index (list, tag, n, &s) && s != NULL) {
      ID3v2::CommentsFrame * f;
      gchar *desc = NULL, *val = NULL, *lang = NULL;

      f = new ID3v2::CommentsFrame (String::UTF8);

      if (strcmp (tag, GST_TAG_COMMENT) == 0 ||
          !gst_tag_parse_extended_comment (s, &desc, &lang, &val, TRUE)) {
        /* create dummy description to allow for multiple comment frames
         * (taglib will drop comment frames if descriptions are not unique) */
        desc = g_strdup_printf ("c%u", n);
        val = g_strdup (s);
      }

      GST_LOG ("%s[%u] = '%s' (%s|%s|%s)", tag, n, s, GST_STR_NULL (desc),
          GST_STR_NULL (lang), GST_STR_NULL (val));

      f->setDescription (desc);
      f->setText (val);
      if (lang) {
        f->setLanguage (lang);
      }

      g_free (lang);
      g_free (desc);
      g_free (val);

      id3v2tag->addFrame (f);
    }
    g_free (s);
  }
}

static void
add_text_tag (ID3v2::Tag * id3v2tag, const GstTagList * list,
    const gchar * tag, guint num_tags, const gchar * frame_id)
{
  ID3v2::TextIdentificationFrame * f;
  TagLib::StringList string_list;
  guint n;

  GST_LOG ("Adding '%s' frame", frame_id);
  for (n = 0; n < num_tags; ++n) {
    gchar *s = NULL;

    if (gst_tag_list_get_string_index (list, tag, n, &s) && s != NULL) {
      GST_LOG ("%s: %s[%u] = '%s'", frame_id, tag, n, s);
      string_list.append (String (s, String::UTF8));
      g_free (s);
    }
  }

  if (!string_list.isEmpty ()) {
    f = new ID3v2::TextIdentificationFrame (frame_id, String::UTF8);
    id3v2tag->addFrame (f);
    f->setText (string_list);
  } else {
    GST_WARNING ("Empty list for tag %s, skipping", tag);
  }
}

static void
add_uri_tag (ID3v2::Tag * id3v2tag, const GstTagList * list,
    const gchar * tag, guint num_tags, const gchar * frame_id)
{
  gchar *url = NULL;

  g_assert (frame_id != NULL);

  /* URI tags are limited to one of each per taglist */
  if (gst_tag_list_get_string_index (list, tag, 0, &url) && url != NULL) {
    guint url_len;

    url_len = strlen (url);
    if (url_len > 0 && gst_uri_is_valid (url)) {
      ID3v2::FrameFactory * factory = ID3v2::FrameFactory::instance ();
      ID3v2::Frame * frame;
      char *data;

      data = g_new0 (char, 10 + url_len);

      memcpy (data, frame_id, 4);
      memcpy (data + 4, ID3v2::SynchData::fromUInt (url_len).data (), 4);
      memcpy (data + 10, url, url_len);
      ByteVector bytes (data, 10 + url_len);

      g_free (data);

      frame = factory->createFrame (bytes, (TagLib::uint) 4);
      if (frame) {
        id3v2tag->addFrame (frame);

        GST_LOG ("%s: %s = '%s'", frame_id, tag, url);
      }
    } else {
      GST_WARNING ("Tag %s does not contain a valid URI (%s)", tag, url);
    }

    g_free (url);
  }
}

/* id3demux produces these for frames it cannot parse */
#define GST_ID3_DEMUX_TAG_ID3V2_FRAME "private-id3v2-frame"

static const struct
{
  const gchar *gst_tag;
  const GstId3v2MuxAddTagFunc func;
  const gchar data[5];
} add_funcs[] = {
  {
  GST_TAG_ARTIST, add_text_tag, "TPE1"}, {
  GST_TAG_TITLE, add_text_tag, "TIT2"}, {
  GST_TAG_ALBUM, add_text_tag, "TALB"}, {
  GST_TAG_COPYRIGHT, add_text_tag, "TCOP"}, {
  GST_TAG_COMPOSER, add_text_tag, "TCOM"}, {
  GST_TAG_GENRE, add_text_tag, "TCON"}, {
  GST_TAG_COMMENT, add_comment_tag, ""}, {
  GST_TAG_EXTENDED_COMMENT, add_comment_tag, ""}, {
  GST_TAG_DATE, add_date_tag, ""}, {
  GST_TAG_IMAGE, add_image_tag, ""}, {
  GST_TAG_PREVIEW_IMAGE, add_image_tag, ""}, {
  GST_ID3_DEMUX_TAG_ID3V2_FRAME, add_id3v2frame_tag, ""}, {
  GST_TAG_MUSICBRAINZ_ARTISTID, add_musicbrainz_tag, "\000"}, {
  GST_TAG_MUSICBRAINZ_ALBUMID, add_musicbrainz_tag, "\001"}, {
  GST_TAG_MUSICBRAINZ_ALBUMARTISTID, add_musicbrainz_tag, "\002"}, {
  GST_TAG_MUSICBRAINZ_TRMID, add_musicbrainz_tag, "\003"}, {
  GST_TAG_CDDA_MUSICBRAINZ_DISCID, add_musicbrainz_tag, "\004"}, {
  GST_TAG_CDDA_CDDB_DISCID, add_musicbrainz_tag, "\005"}, {
  GST_TAG_MUSICBRAINZ_TRACKID, add_unique_file_id_tag, ""}, {
  GST_TAG_ARTIST_SORTNAME, add_text_tag, "TSOP"}, {
  GST_TAG_ALBUM_SORTNAME, add_text_tag, "TSOA"}, {
  GST_TAG_TITLE_SORTNAME, add_text_tag, "TSOT"}, {
  GST_TAG_TRACK_NUMBER, add_count_or_num_tag, "TRCK"}, {
  GST_TAG_TRACK_COUNT, add_count_or_num_tag, "TRCK"}, {
  GST_TAG_ALBUM_VOLUME_NUMBER, add_count_or_num_tag, "TPOS"}, {
  GST_TAG_ALBUM_VOLUME_COUNT, add_count_or_num_tag, "TPOS"}, {
  GST_TAG_ENCODER, add_encoder_tag, ""}, {
  GST_TAG_ENCODER_VERSION, add_encoder_tag, ""}, {
  GST_TAG_COPYRIGHT_URI, add_uri_tag, "WCOP"}, {
  GST_TAG_LICENSE_URI, add_uri_tag, "WCOP"}
};


static void
foreach_add_tag (const GstTagList * list, const gchar * tag, gpointer userdata)
{
  ID3v2::Tag * id3v2tag = (ID3v2::Tag *) userdata;
  TagLib::StringList string_list;
  guint num_tags, i;

  num_tags = gst_tag_list_get_tag_size (list, tag);

  GST_LOG ("Processing tag %s (num=%u)", tag, num_tags);

  if (num_tags > 1 && gst_tag_is_fixed (tag)) {
    GST_WARNING ("Multiple occurences of fixed tag '%s', ignoring some", tag);
    num_tags = 1;
  }

  for (i = 0; i < G_N_ELEMENTS (add_funcs); ++i) {
    if (strcmp (add_funcs[i].gst_tag, tag) == 0) {
      add_funcs[i].func (id3v2tag, list, tag, num_tags, add_funcs[i].data);
      break;
    }
  }

  if (i == G_N_ELEMENTS (add_funcs)) {
    GST_WARNING ("Unsupported tag '%s' - not written", tag);
  }
}

static GstBuffer *
gst_id3v2_mux_render_tag (GstTagLibMux * mux, GstTagList * taglist)
{
  ID3v2::Tag id3v2tag;
  ByteVector rendered_tag;
  GstBuffer *buf;
  guint tag_size;

  /* write all strings as UTF-8 by default */
  TagLib::ID3v2::FrameFactory::instance ()->
      setDefaultTextEncoding (TagLib::String::UTF8);

  /* Render the tag */
  gst_tag_list_foreach (taglist, foreach_add_tag, &id3v2tag);

#if 0
  /* Do we want to add our own signature to the tag somewhere? */
  {
    gchar *tag_producer_str;

    tag_producer_str = g_strdup_printf ("(GStreamer id3v2mux %s, using "
        "taglib %u.%u)", VERSION, TAGLIB_MAJOR_VERSION, TAGLIB_MINOR_VERSION);
    add_one_txxx_tag (id3v2tag, "tag_encoder", tag_producer_str);
    g_free (tag_producer_str);
  }
#endif

  rendered_tag = id3v2tag.render ();
  tag_size = rendered_tag.size ();

  GST_LOG_OBJECT (mux, "tag size = %d bytes", tag_size);

  /* Create buffer with tag */
  buf = gst_buffer_new_and_alloc (tag_size);
  memcpy (GST_BUFFER_DATA (buf), rendered_tag.data (), tag_size);
  gst_buffer_set_caps (buf, GST_PAD_CAPS (mux->srcpad));

  return buf;
}

gboolean
gst_id3v2_mux_plugin_init (GstPlugin * plugin)
{
  if (!gst_element_register (plugin, "id3v2mux", GST_RANK_NONE,
          GST_TYPE_ID3V2_MUX))
    return FALSE;

  gst_tag_register_musicbrainz_tags ();

  return TRUE;
}
