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

#ifndef __STREAMING_SOURCE_TCP_H__
#define __STREAMING_SOURCE_TCP_H__

#include <string>
#include <whispermedialib/media/component.h>

// our namespace
namespace media {

// A TCP source component.
class TCPSource : public Component {
 public:
  // The TCP source types.
  enum Type {
    Server = 0,
    Client
  };

 public:
  TCPSource(const char* name,
            Type tcp_type,
            const char* host,
            unsigned int port,
            Protocol protocol);

 public:
  // Serializes this component into the given ostream.
  virtual void Serialize(std::ostream& o) const;

 protected:
  // The type of the source.
  Type m_tcp_type;

  string m_host;        // TODO(cpopescu): how is this specified (IP or name)
  unsigned int m_port;
  Protocol m_protocol;
};

}   // namespace media

#endif   // __STREAMING_SOURCE_TCP_H__
