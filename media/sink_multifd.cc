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

#include "media/sink_multifd.h"

// our namespace
namespace media {

MultiFDSink::MultiFDSink(const char* name,
                         bool sync,
                         int hard_limit,
                         int soft_limit,
                         int time_min,
                         Protocol protocol,
                         SyncType sync_type) :
    Component(name),
    m_sync(sync),
    m_hard_limit(hard_limit),
    m_soft_limit(soft_limit),
    m_time_min(time_min),
    m_protocol(protocol),
    m_sync_type(sync_type) {
}

// Serializes this component into the given ostream.
void MultiFDSink::Serialize(std::ostream& o) const {
  o << "multifdsink";
  if (!m_name.empty()) {
    o << " name=\"" << m_name << "\"";
  }
  switch (m_protocol) {
    case NoProtocol:
      break;
    case GDP:
      o << " protocol=gdp";
      break;
    default:
      LOG_FATAL <<
      "MultiFDSink: Cannot handle the given protocol (" << m_protocol << ").";
  }
  switch (m_sync_type) {
    case Latest:
      o << " sync-method=latest";
      break;
    case LatestKeyframe:
      o << " sync-method=latest-keyframe";
      break;
    case NextKeyframe:
      o << " sync-method=next-keyframe";
      break;
    default:
      LOG_FATAL <<
      "MultiFDSink: Cannot handle the given protocol (" << m_protocol << ").";
  }

  o << " sync=" << (m_sync ? "true" : "false");
  if (m_time_min > 0) {
    o << " time-min=" << m_time_min*GST_MSECOND;
  }
  if (m_hard_limit >= 0 || m_soft_limit >= 0) {
    o  << " unit-type=time";
    if (m_hard_limit >= 0)
      o << " units-max=" << m_hard_limit*GST_MSECOND;
    if (m_soft_limit >= 0)
      o << " recover-policy=keyframe " <<
      "units-soft-max=" << m_soft_limit*GST_MSECOND;
  }
}
}
