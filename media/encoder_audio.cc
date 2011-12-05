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
#include "media/encoder_audio.h"

// our namespace
namespace media {

AudioEncoder::AudioEncoder(const char* name,
                           Encoder::Audio::Type type,
                           int32 sample_rate,
                           int32 sample_width,
                           int32 bit_rate,
                           bool raw,
                           const char* parameters)
    : Component(name),
      m_type(type),
      m_sample_rate(sample_rate), m_sample_width(sample_width),
      m_bit_rate(bit_rate),
      m_raw(raw),
      m_parameters((parameters == NULL) ? "" : parameters) {
}

// Serializes this component into the given ostream.
void AudioEncoder::Serialize(std::ostream& o) const {
  list<const char*> mime_types;

  switch (m_type) {
    case Encoder::Audio::MP3:
      mime_types.push_back("audio/x-raw-int");
      break;
    case Encoder::Audio::AAC:
      mime_types.push_back("audio/x-raw-int");
      break;
    default:
      LOG_FATAL << "AudioEncoder: Invalid audio encoder type.";
  }

  if (m_sample_rate != 0 || m_sample_width != 0) {
    o << "capsfilter caps=\"";
    for (list<const char*>::iterator mime_type = mime_types.begin();
         mime_type != mime_types.end(); mime_type++) {
      if (mime_type != mime_types.begin())
        o << "; ";

      o << *mime_type;
      if (m_sample_rate != 0) {
        switch (m_type) {
          case Encoder::Audio::MP3:
            switch (m_sample_rate) {
              case 8000:
              case 11025:
              case 12000:
              case 16000:
              case 22050:
              case 24000:
              case 32000:
              case 44100:
              case 48000:
                break;
              default:
                LOG_ERROR << "AudioEncoder: Invalid MP3 sample rate ("
                          << m_sample_rate << ").";
                o.setstate(ios::failbit);
                return;
            }
            break;
          case Encoder::Audio::AAC:
            if (m_sample_rate < kAACMinSampleRate ||
                m_sample_rate > kAACMaxSampleRate) {
              LOG_ERROR << "AudioEncoder: Invalid AAC sample rate ("
                        << m_sample_rate << ").";
              o.setstate(ios::failbit);
              return;
            }
            break;
          default:
            LOG_FATAL << "AudioEncoder: Invalid audio encoder type.";
        }
        o << ",rate=" << m_sample_rate;
      }
      if (m_sample_width != 0) {
        switch (m_type) {
          case Encoder::Audio::MP3:
            if (m_sample_rate < kMP3MinSampleWidth ||
                m_sample_rate > kMP3MaxSampleWidth) {
              LOG_ERROR << "AudioEncoder: Invalid MP3 sample width ("
                        << m_sample_width << ").";
              o.setstate(ios::failbit);
              return;
            }
            break;
          case Encoder::Audio::AAC:
            if (m_sample_rate < kAACMinSampleWidth ||
                m_sample_rate > kAACMaxSampleWidth) {
              LOG_ERROR << "AudioEncoder: Invalid AAC sample width ("
                        << m_sample_width << ").";
              o.setstate(ios::failbit);
              return;
            }
            break;
          default:
            LOG_FATAL << "AudioEncoder: Invalid audio encoder type.";
        }
        o << ",width=" << m_sample_width;
      }
    }
    o << "\" ! ";
  }

  switch (m_type) {
    case Encoder::Audio::MP3:
      o << "lame";
      if (!m_name.empty()) {
        o << " name=\"" << m_name << "\"";
      }
      if (m_bit_rate != 0) {
        switch (m_bit_rate) {
          case   8*1024:
          case  16*1024:
          case  24*1024:
          case  32*1024:
          case  40*1024:
          case  48*1024:
          case  56*1024:
          case  64*1024:
          case  80*1024:
          case  96*1024:
          case 112*1024:
          case 128*1024:
          case 160*1024:
          case 192*1024:
          case 224*1024:
          case 256*1024:
          case 320*1024:
            break;
          default:
            LOG_ERROR << "AudioEncoder: Invalid MP3 bit rate ("
                      << m_bit_rate << ").";
            o.setstate(ios::failbit);
            return;
        }
        o << " vbr=none bitrate=" << m_bit_rate/1024;
      }
      o << " strict-iso=true disable-reservoir=true" << m_parameters;
      break;
    case Encoder::Audio::AAC:
      o << "aacp";
      if (!m_name.empty()) {
        o << " name=\"" << m_name << "\"";
      }
      if (m_raw) {
        o << " outputformat=raw";
      }

      if (m_bit_rate != 0) {
        if (m_bit_rate < kAACMinBitRate || m_bit_rate > kAACMaxBitRate) {
          LOG_ERROR << "AudioEncoder: Invalid AAC bit rate ("
                    << m_bit_rate << ").";
          o.setstate(ios::failbit);
          return;
        }
        // 1 kbps is actually 1000 bps...
        o << " bitrate=" << (1000*m_bit_rate)/1024;
      } else {
        // default
        o << " bitrate=" << 32000;
      }
      //  o << " outputformat=OUTPUTFORMAT_ADTS" << m_parameters;
      o << m_parameters;
      break;
    default:
      LOG_FATAL << "AudioEncoder: Invalid audio encoder type.";
  }
}
}
