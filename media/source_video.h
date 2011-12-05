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

#ifndef __STREAMING_SOURCE_VIDEO_H__
#define __STREAMING_SOURCE_VIDEO_H__

#include <string>
#include <whispermedialib/media/component.h>

// our namespace
namespace media {

// A video source component.
class VideoSource : public Component {
 public:
  // The type of the source.
  enum Type {
    Blank,
    V4L,
    V4L2,
    DV1394,

    DefaultType = V4L
  };

 public:
  VideoSource(const char* name,
              Type type,
              const char* device,
              unsigned int width,
              unsigned int height,
              unsigned int framerate_n,
              unsigned int framerate_d);

 public:
  // Serializes this component into the given ostream.
  virtual void Serialize(std::ostream& o) const;

 protected:
  // The source type.
  Type m_type;

  // The device name.
  string m_device;

  // The video width.
  unsigned int m_width;
  // The video height.
  unsigned int m_height;
  // The video framerate numerator.
  unsigned int m_framerate_n;
  // The video framerate denominator.
  unsigned int m_framerate_d;
};

}   // namespace media

#endif   // __STREAMING_SOURCE_VIDEO_H__o
