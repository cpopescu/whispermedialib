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

#ifndef __STREAMING_CONTROLLER_H__
#define __STREAMING_CONTROLLER_H__

#include <string>
#include <whisperlib/common/base/types.h>
#include WHISPER_HASH_SET_HEADER
#include WHISPER_HASH_MAP_HEADER

#include <list>
#include <whispermedialib/media/pipeline.h>

// our namespace
namespace media {

// The common base for all the controllers,
// objects meant to control various features of pipeline elements.
class Controller : public Pipeline::ElementHandler {
 public:
  // The known control types.
  enum ControlType {
    Range = 1,
    List
  };
  // The known control ids.
  enum ControlID {
    None = 0,
    First,

    Brightness = First,
    Contrast,
    Saturation,

    Gamma,

    AutoHue,
    Hue,

    AutoExposure,
    Exposure,

    AutoWhiteBalance,
    WhiteBalance,

    AutoGain,
    Gain,

    BacklightCompensation,
    PowerLineFrequency,

    Sharpness,

    AutoFocus,
    FocusRelative,
    FocusAbsolute,

    PanRelative,
    TiltRelative,
    PanTiltReset,
    
    Norm,
    Channel,
    
    Last = Channel 
  };
  static ControlID const Controls[Last-First+1];

  // The invalid control value.
  static const int kInvalidValue = INT_MIN;

  // Returns a humane readable name for a control ID
  static const char* ControlName(ControlID id);

  // This abstracts a single control channel,
  // allowing the control of a single feature.
  struct ControlChannel {
    // The associated channel, implementation dependent.
    void* m_channel;
    // The read-only flag for this channel.
    const bool m_read_only;

    // The controller channel's name.
    const string m_name;
    
    // The controller type
    ControlType m_type;
    // The controller values
    struct ControlValues {
      // Range
      
      // The values are relative.
      bool m_relative;
      
      // The minimum value of the control.
      int m_min;
      // The maximum value of the control.
      int m_max;
      // The control value step.
      int m_step;
      
      // List
      std::list<std::string> m_list;
    }  m_values;

    ControlChannel(void* channel, bool read_only, const char* name,
                   const bool relative,
                   const int min, const int max, const int step) :
      m_channel(channel),
      m_read_only(read_only),
      m_name((name == NULL) ? "" : name),
      m_type(Range) {
      m_values.m_relative = relative;
      m_values.m_min = min;
      m_values.m_max = max;
      m_values.m_step = step;
    }
    ControlChannel(void* channel, bool read_only, const char* name,
                   const std::list<std::string>& list) :
      m_channel(channel),
      m_read_only(read_only),
      m_name((name == NULL) ? "" : name),
      m_type(List) {
      m_values.m_list = list;
    }
  };

  typedef
  ResultCallback2<bool, ControlID, const ControlChannel*>* EnumerateCallback;

 protected:
  Controller();
 public:
  virtual ~Controller();

 public:
  // Enumerates all the aggregated controls by the means of the given
  // enumeration callback.
  void EnumerateControls(EnumerateCallback callback);

  // Returns the control channel at the given id
  const ControlChannel* GetControlChannel(ControlID id) const;

  // Returns the control parameter limits at the given id.
  virtual
  bool GetLimits(ControlID id, int* min, int* max, int* step);

  // Gets the value of the control at the given id.
  virtual
  bool GetValue(ControlID id, int* value) const;
  // Sets the value of the control at the given id.
  virtual
  bool SetValue(ControlID id, int value);

  // Gets the list of values of the control at the given id.
  virtual
  bool GetList(ControlID id, std::list<std::string>& list) const;
  
  // Gets the value of the control at the given id.
  virtual
  bool GetValue(ControlID id, std::string* value) const;
  // Sets the value of the control at the given id.
  virtual
  bool SetValue(ControlID id, const char* value);
  
 protected:
  // Gets the value of the control at the given id.
  virtual
  bool DoGetValue(ControlID id, ControlChannel* channel, int* value) const = 0;
  // Sets the value of the control at the given id.
  virtual
  bool DoSetValue(ControlID id, ControlChannel* channel, int value) = 0;

  // Gets the list of values of the control at the given id.
  virtual
  bool DoGetList(ControlID id, ControlChannel* channel,
      std::list<std::string>& list) const = 0;
  
  // Gets the value of the control at the given id.
  virtual
  bool DoGetValue(ControlID id, ControlChannel* channel,
      std::string* value) const = 0;
  // Sets the value of the control at the given id.
  virtual
  bool DoSetValue(ControlID id, ControlChannel* channel,
      const char* value) = 0;
  
  // The controller mutex.
  mutable GStaticRecMutex m_mutex;

  // The aggregated control channels.
  typedef hash_map<int, ControlChannel*> ChannelsMap;
  ChannelsMap m_channels;

  // Called by the pipeline when the element handler is attached
  // to the associated element.
  virtual
  void Attach(GstElement* element);
  // Called by the pipeline when the element handler is detached
  // from the associated element - after this the element handler is not
  // referenced anymore by the pipeline.
  virtual
  void Detach();

  // Called by the pipeline on all the pipeline state changes.
  virtual
  bool OnPipelineStateChanged(Pipeline* pipeline,
                              Pipeline::State old_state,
                              Pipeline::State new_state);

  // Creates the control channels for this controller.
  // The mutex must be acquired.
  virtual
  void CreateControls(Pipeline* pipeline) = 0;
  // Destroys the control channels for this controller.
  // The mutex must be acquired.
  virtual
  void DestroyControls();
};

}   // namespace media

#endif    // __STREAMING_CONTROLLER_H__
