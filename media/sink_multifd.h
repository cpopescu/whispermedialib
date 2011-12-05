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

#ifndef __STREAMING_SINK_MULTIFD_H__
#define __STREAMING_SINK_MULTIFD_H__

#include <whispermedialib/media/component.h>

// our namespace
namespace media {

// A multiple fd sink component.
class MultiFDSink : public Component {
 public:
  enum SyncType {
    Latest,
    LatestKeyframe,
    NextKeyframe,

    Default = Latest
  };
 public:
  MultiFDSink(const char* name,
              bool sync,
              int hard_limit,
              int soft_limit,
              int time_min,
              Protocol protocol,
              SyncType sync_type);
 public:
  // Serializes this component into the given ostream.
  virtual void Serialize(std::ostream& o) const;

 protected:
  // If true, the sink will sync on the buffers received.
  bool m_sync;

  // The hard limit on the lag of the sink fds, in miliseconds.
  int m_hard_limit;
  // The soft limit on the lag of the sink fds, in miliseconds.
  int m_soft_limit;

  // The minimum time to buffer, in miliseconds.
  int m_time_min;

  // The protocol used on output.
  Protocol m_protocol;

  // The sync type.
  SyncType m_sync_type;
};
}   // namespace media

#endif  // __STREAMING_SINK_MULTIFD_H__
