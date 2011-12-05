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

#ifndef __STREAMING_CONTROLLER_V4L_H__
#define __STREAMING_CONTROLLER_V4L_H__

#include <whispermedialib/media/controller.h>

// our namespace
namespace media {

// The V4L source element controller.
class V4LSourceController : public Controller {
 public:
  V4LSourceController();

  // Creates the control channels for this controller.
  virtual
  void CreateControls(Pipeline* pipeline);
  // Destroys the control channels for this controller.
  virtual
  void DestroyControls();

 protected:
  // Gets the value of the control at the given id.
  virtual
  bool DoGetValue(ControlID id, ControlChannel* channel, int* value) const;
  // Sets the value of the control at the given id.
  virtual
  bool DoSetValue(ControlID id, ControlChannel* channel, int value);

  // Gets the list of values of the control at the given id.
  virtual
  bool DoGetList(ControlID id, ControlChannel* channel,
      std::list<std::string>& list) const;
  
  // Gets the value of the control at the given id.
  virtual
  bool DoGetValue(ControlID id, ControlChannel* channel,
      std::string* value) const;
  // Sets the value of the control at the given id.
  virtual bool DoSetValue(ControlID id, ControlChannel* channel,
      const char* value);
  
 private:
  // The color balance interface.
  GstColorBalance* m_color_balance;

  // Maps a generic control ID to the internal, V4L dependent control id
  const char* MapControlID(ControlID id);
};
}   // namespace media

#endif  // __STREAMING_CONTROLLER_V4L_H__
