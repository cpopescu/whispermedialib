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

#ifndef __STREAMING_SOURCE_AUDIO_H__
#define __STREAMING_SOURCE_AUDIO_H__

#include <string>
#include <list>
#include <whispermedialib/media/component.h>

// our namespace
namespace media {

// An audio source component.
class AudioSource : public Component {
 public:
  // The type of the source.
  enum Type {
    Blank,
    ALSA,
    OSS,

    DefaultType = ALSA
  };

 public:
  AudioSource(const char* name,
              Type type,
              const char* device,
              unsigned int samplerate,
              unsigned int width);
 public:
  // Serializes this component into the given ostream.
  virtual void Serialize(std::ostream& o) const;

 protected:
  // The source type.
  Type m_type;

  // The device name.
  string m_device;

  // The sample rate.
  unsigned int m_samplerate;

  // The sample width.
  unsigned int m_width;

  // The mime types that are offered by this video source.
  list<const char*> m_mime_types;
};
}   // namespace media

#endif /*STREAMING_SOURCE_AUDIO_H_*/
