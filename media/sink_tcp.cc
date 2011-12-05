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

#include "media/sink_tcp.h"

// our namespace
namespace media {

TCPSink::TCPSink(const char* name,
                 bool sync,
                 Type tcp_type,
                 const char* host,
                 unsigned int port,
                 Protocol protocol)
    : Component(name),
      m_sync(sync),
      m_tcp_type(tcp_type),
      m_host(host != NULL ? host : ""),
      m_port(port),
      m_protocol(protocol) {
}

// Serializes this component into the given ostream.
void TCPSink::Serialize(std::ostream& o) const {
  o << ((m_tcp_type == Server) ? "tcpserversink" : "tcpclientsink");
  if (!m_name.empty()) {
    o << " name=\"" << m_name << "\"";
  }

  o << " sync=" << (m_sync ? "true" : "false");

  switch (m_protocol) {
    case NoProtocol:
    case GDP:
      o << " protocol=gdp";
      break;
    default:
      LOG_FATAL << "TCPSink: Cannot handle the given protocol (" << m_protocol
                << ").";
  }
  if (!m_host.empty())
    o << " host=\"" << m_host << "\"";
  if (m_port != 0)
    o << " port=" << m_port;
}
}
