/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 *               <2006> Edward Hervey <bilboed@bilboed.com>
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <string.h>
#include <errno.h>
#ifdef HAVE_FFMPEG_UNINSTALLED
#include <avformat.h>
#else
#include <ffmpeg/avformat.h>
#endif

#include <gst/gst.h>

#include "gstffmpeg.h"

typedef struct _GstProtocolInfo GstProtocolInfo;

struct _GstProtocolInfo
{
  GstPad *pad;

  guint64 offset;
  gboolean eos;
  gint set_streamheader;
};

static int
gst_ffmpegdata_open (URLContext * h, const char *filename, int flags)
{
  GstProtocolInfo *info;
  GstPad *pad;

  GST_LOG ("Opening %s", filename);

  info = g_new0 (GstProtocolInfo, 1);

  info->set_streamheader = flags & GST_FFMPEG_URL_STREAMHEADER;
  flags &= ~GST_FFMPEG_URL_STREAMHEADER;
  h->flags &= ~GST_FFMPEG_URL_STREAMHEADER;

  /* we don't support R/W together */
  if (flags != URL_RDONLY && flags != URL_WRONLY) {
    GST_WARNING ("Only read-only or write-only are supported");
    return -EINVAL;
  }

  if (sscanf (&filename[12], "%p", &pad) != 1) {
    GST_WARNING ("could not decode pad from %s", filename);
    return -EIO;
  }

  /* make sure we're a pad and that we're of the right type */
  g_return_val_if_fail (GST_IS_PAD (pad), -EINVAL);

  switch (flags) {
    case URL_RDONLY:
      g_return_val_if_fail (GST_PAD_IS_SINK (pad), -EINVAL);
      break;
    case URL_WRONLY:
      g_return_val_if_fail (GST_PAD_IS_SRC (pad), -EINVAL);
      break;
  }

  info->eos = FALSE;
  info->pad = pad;
  info->offset = 0;

  h->priv_data = (void *) info;
  h->is_streamed = FALSE;
  h->max_packet_size = 0;

  return 0;
}

static int
gst_ffmpegdata_peek (URLContext * h, unsigned char *buf, int size)
{
  GstProtocolInfo *info;
  GstBuffer *inbuf = NULL;
  GstFlowReturn ret;
  int total = 0;

  g_return_val_if_fail (h->flags == URL_RDONLY, AVERROR_IO);
  info = (GstProtocolInfo *) h->priv_data;

  GST_DEBUG ("Pulling %d bytes at position %lld", size, info->offset);

  ret = gst_pad_pull_range (info->pad, info->offset, (guint) size, &inbuf);

  switch (ret) {
    case GST_FLOW_OK:
      total = (gint) GST_BUFFER_SIZE (inbuf);
      memcpy (buf, GST_BUFFER_DATA (inbuf), total);
      gst_buffer_unref (inbuf);
      break;
    case GST_FLOW_UNEXPECTED:
      total = 0;
      break;
    case GST_FLOW_WRONG_STATE:
      total = -1;
      break;
    default:
    case GST_FLOW_ERROR:
      total = -2;
      break;
  }

  GST_DEBUG ("Got %d (%s) return result %d", ret, gst_flow_get_name (ret),
      total);

  return total;
}

static int
gst_ffmpegdata_read (URLContext * h, unsigned char *buf, int size)
{
  gint res;
  GstProtocolInfo *info;

  info = (GstProtocolInfo *) h->priv_data;

  GST_DEBUG ("Reading %d bytes of data at position %lld", size, info->offset);

  res = gst_ffmpegdata_peek (h, buf, size);
  if (res >= 0)
    info->offset += res;

  GST_DEBUG ("Returning %d bytes", res);

  return res;
}

static int
gst_ffmpegdata_write (URLContext * h, unsigned char *buf, int size)
{
  GstProtocolInfo *info;
  GstBuffer *outbuf;

  GST_DEBUG ("Writing %d bytes", size);
  info = (GstProtocolInfo *) h->priv_data;

  g_return_val_if_fail (h->flags != URL_RDONLY, -EIO);

  /*
   * WHISPERCAST BEGIN: set the "streamheader" value on the source pad caps,
   * so we are able to use ffmpegmux with fdsink/multifdsink/etc...
  */
  if (info->set_streamheader) {
    GstCaps *caps;
    GstStructure *structure;
  
    GValue array = { 0 };
    GstBuffer *copy;
    GValue value = { 0 };
  
    caps = gst_pad_get_caps(info->pad);
    caps = gst_caps_make_writable (caps);
  
    structure = gst_caps_get_structure (caps, 0);
  
    /* put buffers in a fixed list */
    g_value_init (&array, GST_TYPE_ARRAY);
  
    g_value_init (&value, GST_TYPE_BUFFER);
    copy = gst_buffer_new_and_alloc (size);
    memcpy (GST_BUFFER_DATA (copy), buf, size);
    gst_value_set_buffer (&value, copy);
    gst_buffer_unref (copy);
    gst_value_array_append_value (&array, &value);
    g_value_unset (&value);
  
    gst_structure_set_value (structure, "streamheader", &array);
    g_value_unset (&array);
  
    gst_pad_set_caps(info->pad, caps);
    gst_caps_unref(caps);
  }
  /* WHISPERCAST END */
  
  /* create buffer and push data further */
  if (gst_pad_alloc_buffer_and_set_caps (info->pad,
          info->offset, size, GST_PAD_CAPS (info->pad), &outbuf) != GST_FLOW_OK)
    return 0;

  /*
   * WHISPERCAST BEGIN: mark the buffer as added to the source pad's caps.
  */
  if (info->set_streamheader) {
    GST_BUFFER_FLAG_SET (outbuf, GST_BUFFER_FLAG_IN_CAPS);
    info->set_streamheader = 0;
  }
  /* WHISPERCAST END */
  
  memcpy (GST_BUFFER_DATA (outbuf), buf, size);

  if (gst_pad_push (info->pad, outbuf) != GST_FLOW_OK)
    return 0;

  info->offset += size;
  return size;
}

static offset_t
gst_ffmpegdata_seek (URLContext * h, offset_t pos, int whence)
{
  GstProtocolInfo *info;
  guint64 newpos;

  GST_DEBUG ("Seeking to %" G_GINT64_FORMAT ", whence=%d", pos, whence);

  info = (GstProtocolInfo *) h->priv_data;

  /* TODO : if we are push-based, we need to return sensible info */

  switch (h->flags) {
    case URL_RDONLY:
    {
      /* sinkpad */
      switch (whence) {
        case SEEK_SET:
          info->offset = (guint64) pos;
          break;
        case SEEK_CUR:
          info->offset += pos;
          break;
        case SEEK_END:
          /* ffmpeg wants to know the current end position in bytes ! */
        {
          GstFormat format = GST_FORMAT_BYTES;
          gint64 duration;

          if (gst_pad_is_linked (info->pad))
            if (gst_pad_query_duration (GST_PAD_PEER (info->pad), &format,
                    &duration))
              info->offset = ((guint64) duration) + pos;
        }
          break;
        default:
          break;
      }
      /* FIXME : implement case for push-based behaviour */
      newpos = info->offset;
    }
      break;
    case URL_WRONLY:
    {
      /* srcpad */
      switch (whence) {
        case SEEK_SET:
          info->offset = (guint64) pos;
          gst_pad_push_event (info->pad, gst_event_new_new_segment
              (TRUE, 1.0, GST_FORMAT_BYTES, info->offset,
                  GST_CLOCK_TIME_NONE, info->offset));
          break;
        case SEEK_CUR:
          info->offset += pos;
          gst_pad_push_event (info->pad, gst_event_new_new_segment
              (TRUE, 1.0, GST_FORMAT_BYTES, info->offset,
                  GST_CLOCK_TIME_NONE, info->offset));
          break;
        default:
          break;
      }
      newpos = info->offset;
    }
      break;
    default:
      g_assert (0);
      break;
  }

  GST_DEBUG ("Now at offset %lld", info->offset);
  return newpos;
}

static int
gst_ffmpegdata_close (URLContext * h)
{
  GstProtocolInfo *info;

  info = (GstProtocolInfo *) h->priv_data;
  if (info == NULL)
    return 0;

  GST_LOG ("Closing file");

  switch (h->flags) {
    case URL_WRONLY:
    {
      /* send EOS - that closes down the stream */
      gst_pad_push_event (info->pad, gst_event_new_eos ());
      break;
    }
    default:
      break;
  }

  /* clean up data */
  g_free (info);
  h->priv_data = NULL;

  return 0;
}


URLProtocol gstreamer_protocol = {
  /*.name = */ "gstreamer",
  /*.url_open = */ gst_ffmpegdata_open,
  /*.url_read = */ gst_ffmpegdata_read,
  /*.url_write = */ gst_ffmpegdata_write,
  /*.url_seek = */ gst_ffmpegdata_seek,
  /*.url_close = */ gst_ffmpegdata_close,
};
