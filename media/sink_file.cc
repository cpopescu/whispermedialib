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

#include "media/sink_file.h"

// our namespace
namespace media {

FileSink::FileSink(const char*name, bool sync, const char* location)
    : Component(name),
      m_sync(sync),
      m_location(location != NULL ? location : "") {
}

// Serializes this component into the given ostream.
void FileSink::Serialize(std::ostream& o) const {
  o << "filesink";
  if (!m_name.empty()) {
    o << " name=\"" << m_name << "\"";
  }
  if (!m_location.empty()) {
    o << " location=\"" << m_location << "\"";
  }

  o << " sync=" << (m_sync ? "true" : "false");
}
}
