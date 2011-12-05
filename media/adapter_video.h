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

#ifndef __STREAMING_ADAPTER_VIDEO_H__
#define __STREAMING_ADAPTER_VIDEO_H__

#include <whispermedialib/media/component.h>

// our namespace
namespace media  {

// A video adapter component, meant to adapt any types of
// raw/uncompressed video between it's sink and source pads.
class VideoAdapter : public Component {
 public:
  VideoAdapter();
 public:
  virtual void Serialize(std::ostream& o) const;
};

}    // namespace media

#endif   // __STREAMING_SINK_VIDEO_H__
