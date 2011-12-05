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

#include "media/source_fd.h"

// our namespace
namespace media {

FDSource::FDSource(const char* name, Protocol protocol)
    : Component(name),
      m_protocol(protocol) {
}

// Serializes this component into the given ostream.
void FDSource::Serialize(std::ostream& o) const {
  o << "fdsrc";
  if (!m_name.empty()) {
    o << " name=\"" << m_name << "\"";
  }

  switch (m_protocol) {
    case NoProtocol:
    case GDP:
      o << " ! gdpdepay";
    break;
    default:
      LOG_FATAL << "FDSource: Cannot handle the given protocol ("
                << m_protocol << ").";
  }
}
}
