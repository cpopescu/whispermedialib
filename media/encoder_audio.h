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
// Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
//
// Author: Mihai Ianculescu

#ifndef __STREAMING_ENCODER_AUDIO_H__
#define __STREAMING_ENCODER_AUDIO_H__

#include <string>
#include <whispermedialib/media/component.h>

// our namespace
namespace media {

// An audio encoder component.
class AudioEncoder : public Component {
 public:
  // The parameter limits for MP3.
  //
  // In addition to this MP3 can be encoded only
  // for the following sample rates:
  // 8000, 11025, 12000, 16000, 22050, 24000, 32000, 44100, 48000
  // and the following bit rates:
  // 8129, 16384, 24576, 32768, 40960, 49152, 57334,
  // 65536, 81920, 98034, 114688, 131072, 163840, 196608,
  // 229376, 262144, 327680
  static const int kMP3MinSampleRate = 8000;
  static const int kMP3MaxSampleRate = 48000;
  static const int kMP3MinSampleWidth = 16;
  static const int kMP3MaxSampleWidth = 16;
  static const int kMP3MinBitRate = 8*1024;
  static const int kMP3MaxBitRate = 320*1024;

  // The parameter limits for AAC.
  static const int kAACMinSampleRate = 8000;
  static const int kAACMaxSampleRate = 96000;
  static const int kAACMinSampleWidth = 16;
  static const int kAACMaxSampleWidth = 16;
  static const int kAACMinBitRate = 8*1024;
  static const int kAACMaxBitRate = 320*1024;

 public:
  AudioEncoder(const char* name,
               Encoder::Audio::Type type,
               int sample_rate,
               int sample_width,
               int bit_rate,
               bool raw,
               const char* parameters);

 public:
  // Serializes this component into the given ostream.
  virtual void Serialize(std::ostream& o) const;
 protected:
  // The type of this encoder.
  Encoder::Audio::Type m_type;

  // The sample rate.
  int m_sample_rate;
  // The sample width.
  int m_sample_width;
  // The bit rate, in bits per second.
  int m_bit_rate;

  // If true, the ouput will not be framed.
  bool m_raw;

  // The audio encoder parameters.
  string m_parameters;
};

}   // namespace media

#endif   // __STREAMING_ENCODER_AUDIO_H__
