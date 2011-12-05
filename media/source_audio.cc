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

#include "media/source_audio.h"

// our namespace
namespace media {

AudioSource::AudioSource(
    const char* name,
    Type type,
    const char* device,
    unsigned int samplerate,
    unsigned int width)
    : Component(name),
      m_type(type),
      m_device(device == NULL ? "" : device),
      m_samplerate(samplerate),
      m_width(width) {
  // add the mime types
  m_mime_types.push_back("audio/x-raw-int");
}

// Serializes this component into the given ostream.
void AudioSource::Serialize(std::ostream& o) const {
  // consider the source type
  switch ( m_type ) {
    case Blank:
      o << "audiotestsrc";
      if (!m_name.empty()) {
        o << " name=\"" << m_name << "\"";
      }

      if (!m_device.empty()) {
        // for Blank the device is formatted as "live:volume:wave", where
        // live in [0..1] (int) and maps to FALSE, TRUE
        // volume in [0..1] (double), see GStreamer audiotestsrc
        // wave in [0..7] (int), see GStreamer audiotestsrc
        gchar** parts;
        parts = g_strsplit((const gchar*)m_device.c_str(), ":", 3);

        gchar* end;

        gchar** part = parts;
        if (*part != NULL) {
          gint is_live = (gint)strtol(*part, &end, 10);
          if (end != NULL && *end == '\0') {
            o << " is-live=" << ((is_live != 0) ? "TRUE"  : "FALSE");
          } else {
            LOG_WARNING << "AudioSource: Couldn't parse the blank audio "
                        << "live flag number from the device description.";
          }
          part++;
        }
        if (*part != NULL) {
          gdouble volume = (gdouble)strtod(*part, &end);
          if (end != NULL && *end == '\0') {
            o << " volume=" << volume;
          } else {
            LOG_WARNING << "AudioSource: Couldn't parse the blank audio "
                        << "volume from the device description.";
          }
          part++;
        }
        if (*part != NULL) {
          gint wave = (gint)strtol(*part, &end, 10);
          if (end != NULL && *end == '\0') {
            o << " wave=" << wave;
          } else {
            LOG_WARNING << "AudioSource: Couldn't parse the blank audio "
                        << "waveform number from the device description.";
          }
          part++;
        }

        g_strfreev(parts);
      }
      break;
    case ALSA:
      o << "alsasrc";
      if (!m_name.empty()) {
        o << " name=\"" << m_name << "\"";
      }

      if (!m_device.empty()) {
        o << " device=\"" << m_device << "\"";
      }
      break;
    case OSS:
      o << "osssrc";
      if (!m_name.empty()) {
        o << " name=\"" << m_name << "\"";
      }

      if (!m_device.empty()) {
        o << " device=\"" << m_device << "\"";
      }
      break;
    default:
      LOG_FATAL << "AudioSource: Invalid audio source type.";
  }

  if (m_samplerate != 0 || m_width != 0) {
    o << " ! capsfilter caps=\"";
    for ( list<const char*>::const_iterator mime_type = m_mime_types.begin();
          mime_type != m_mime_types.end(); mime_type++ ) {
      if (mime_type != m_mime_types.begin())
        o << "; ";

      o << *mime_type;
      if (m_samplerate != 0) {
        o << ",rate=" << m_samplerate;
      }
      if (m_width != 0) {
        o << ",width=" << m_width;
      }
    }
    o << "\"";
    if (!m_name.empty()) {
      o << " name=\"" << m_name << "_capsfilter\"";
    }
  }
}
}
