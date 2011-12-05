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

#ifndef __STREAMING_COMPONENT_H__
#define __STREAMING_COMPONENT_H__

#include <string>
#include <whispermedialib/media/streaming.h>

// our namespace
namespace media {

class Source;
class Sink;

// This is the base for all the higher level components
// of a pipeline, capable of serializing themselves into
// the pipeline textual description.
class Component {
 public:
  explicit Component(const char* name);
  virtual ~Component();

  // Returns the name of the component.
  const char* name() const { return m_name.c_str(); }

  // Serializes this component into the given ostream.
  virtual void Serialize(std::ostream& o) const = 0;

 protected:
  // The component's name.
  const string m_name;

 private:
  friend std::ostream& operator<<(std::ostream&
                                  o, const Component& component);
};

std::ostream& operator<< (std::ostream& o, const Component& component);

}   // namespace media

#endif  // STREAMING_COMPONENT_H_
