// Copyright (c) 2009, Whispersoft s.r.l.
//
// This library is free software; you can redistribute it and/or
// modify it under the terms of the GNU Lesser General Public
// License as published by the Free Software Foundation; either
// version 2 of the License, or (at your option) any later version.
//
// This library is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
// Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public
// License along with this library; if not, write to the Free Software
// Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307
//
// Author: Mihai Ianculescu

#include <list>
#include "media/source_video.h"

// our namespace
namespace media {

VideoSource::VideoSource(
    const char* name,
    Type type,
    const char* device,
    unsigned int width,
    unsigned int height,
    unsigned int framerate_n,
    unsigned int framerate_d)
    : Component(name),
      m_type(type),
      m_device(device == NULL ? "" : device),
      m_width(width),
      m_height(height),
      m_framerate_n(framerate_n),
      m_framerate_d(framerate_d) {
}

// Serializes this component into the given ostream.
void VideoSource::Serialize(std::ostream& o) const {
  list<const char*> mime_types;

  // consider the source type
  switch ( m_type ) {
    case VideoSource::Blank:
      mime_types.push_back("video/x-raw-yuv");
      o << "videotestsrc";
      if (!m_name.empty()) {
        o << " name=\"" << m_name << "\"";
      }

      if (!m_device.empty()) {
        // for Blank the device is formatted as "live:pattern", where
        // live in [0..1] (int) and maps to FALSE, TRUE
        // pattern in [0..11] (int), see GStreamer videotestsrc
        gchar** parts;
        parts = g_strsplit((const gchar*)m_device.c_str(), ":", 3);

        gchar* end;

        gchar** part = parts;
        if (*part != NULL) {
          gint is_live = (gint)strtol(*part, &end, 10);
          if (end != NULL && *end == '\0') {
            o << " is-live=" << (is_live != 0 ? "TRUE" : "FALSE");
          } else {
            LOG_WARNING << "VideoSource: Couldn't parse the blank video "
                        << "live flag number from the device description.";
          }
          part++;
        }
        if (*part != NULL) {
          gint pattern = (gint)strtol(*part, &end, 10);
          if (end != NULL && *end == '\0') {
            o << " pattern=" << pattern;
          } else {
            LOG_WARNING << "VideoSource: Couldn't parse the blank video "
                        << "pattern from the device description.";
          }
        }

        g_strfreev(parts);
      }
      break;
    case VideoSource::V4L:
      mime_types.push_back("video/x-raw-yuv");
      mime_types.push_back("video/x-raw-rgb");
      mime_types.push_back("image/jpeg");

      o << "v4lsrc";
      if (!m_name.empty()) {
        o << " name=\"" << m_name << "\"";
      }

      if (!m_device.empty()) {
        o << " device=\"" << m_device <<"\"";
      }
      break;
    case VideoSource::V4L2:
      mime_types.push_back("video/x-raw-yuv");
      mime_types.push_back("video/x-raw-rgb");
      mime_types.push_back("image/jpeg");

      o << "v4l2src";
      if (!m_name.empty()) {
        o << " name=\"" << m_name << "\"";
      }

      if (!m_device.empty()) {
        o << " device=\"" << m_device <<"\"";
      }
      break;
    case VideoSource::DV1394:
      o << "dv1394src";
      if (!m_name.empty()) {
        o << " name=\"" << m_name << "\"";
      }

      if (!m_device.empty()) {
        // for DV1394 the device is formatted as "port:channel:guid"
        gchar** parts;
        parts = g_strsplit((const gchar*)m_device.c_str(), ":", 3);

        gchar* end;

        gchar** part = parts;
        if (*part != NULL) {
          gint port = (gint)strtol(*part, &end, 10);
          if (end != NULL && *end == '\0') {
            o << " port=" << port;
          } else {
            LOG_WARNING << "VideoSource: Couldn't parse the DV1394 port "
                        << "number from the device description.";
          }
          part++;
        }
        if (*part != NULL) {
          gint channel = (gint)strtol(*part, &end, 10);
          if (end != NULL && *end == '\0') {
            o << " channel=" << channel;
          } else {
            LOG_WARNING << "VideoSource: Couldn't parse the DV1394 channel "
                        << "number from the device description.";
          }
          part++;
        }
        if (*part != NULL) {
          o << " guid=\"" << *part << "\"";
        }

        g_strfreev(parts);
      }
      break;
    default:
      LOG_FATAL << "Invalid video source type.";
  }

  if (m_width != 0
      || m_height != 0
      || (m_framerate_n != 0 && m_framerate_d != 0)) {
    o << " ! capsfilter caps=\"";
    for (list<const char*>::const_iterator mime_type = mime_types.begin();
         mime_type != mime_types.end(); mime_type++) {
      if (mime_type != mime_types.begin())
        o << "; ";

      o << *mime_type;
      if (m_width != 0)
        o << ",width=" << m_width;
      if (m_height != 0)
        o << ",height=" << m_height;
      if (m_framerate_n != 0 && m_framerate_d != 0)
        o << ",framerate=" << m_framerate_n << "/" << m_framerate_d;
    }
    o << "\"";

    if (!m_name.empty()) {
      o << " name=\"" << m_name << "_capsfilter\"";
    }
  }

  // consider the source type
  switch (m_type) {
    case VideoSource::Blank:
      break;
    case VideoSource::V4L:
      o << " ! decodebin";
      if (!m_name.empty()) {
        o << " name=\"" << m_name << "_decoder\"";
      }
      break;
    case VideoSource::V4L2:
      o << " ! decodebin";
      if (!m_name.empty()) {
        o << " name=\"" << m_name << "_decoder\"";
      }
      break;
    case VideoSource::DV1394:
      o << " ! decodebin";
      if (!m_name.empty()) {
        o << " name=\"" << m_name << "_decoder\"";
      }
      break;
    default:
      LOG_FATAL << "Invalid video source type.";
  }
}
}
