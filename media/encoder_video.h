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

#ifndef __STREAMING_ENCODER_VIDEO_H__
#define __STREAMING_ENCODER_VIDEO_H__

#include <string>
#include <whispermedialib/media/component.h>

// our namespace
namespace media {

// A ideo encoder component.
class VideoEncoder : public Component {
 public:
  // The parameter limits for FLV.
  static const int32 kFLVMinWidth = 16;
  static const int32 kFLVMaxWidth = 4096;
  static const int32 kFLVMinHeight = 16;
  static const int32 kFLVMaxHeight = 4096;
  static const int32 kFLVMinPass = 0;
  static const int32 kFLVMaxPass = 2;
  // The parameter limits for H264.
  static const int32 kH264MinWidth = 16;
  static const int32 kH264MinHeight = 16;
  static const int32 kH264MinPass = 0;
  static const int32 kH264MaxPass = 3;

 public:
  VideoEncoder(const char* name,
               Encoder::Video::Type type,
               int32 width,
               int32 height,
               int32 framerate_n,
               int32 framerate_d,
               uint32 bit_rate,
               int32 gop_size,
               bool strict_rc,
               int32 pass,
               const char *statsfile,
               const char* parameters);

 public:
  // Serializes this component into the given ostream.
  virtual void Serialize(std::ostream& o) const;

 protected:
  // The encoder type.
  Encoder::Video::Type m_type;

  // The output width.
  int32 m_width;
  // The output height.
  int32 m_height;

  // The framerate numerator.
  int32 m_framerate_n;
  // The framerate denominator.
  int32 m_framerate_d;

  // The bitrate.
  uint32 m_bit_rate;
  // The size of the GOP.
  int32 m_gop_size;

  // If true, strict rate control will be applied.
  bool m_strict_rc;

  // The current encoding pass.
  int32 m_pass;
  // The stats file to be used in multipass encodings.
  string m_statsfile;
  
  // Additional parameters.
  string m_parameters;
};
}    // namespace media

#endif  // __STREAMING_ENCODER_VIDEO_H__
