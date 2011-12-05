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

#include <whisperlib/common/base/types.h>
#include "media/controller.h"

// our namespace
namespace media {

const char* Controller::ControlName(ControlID id) {
  switch (id) {
    CONSIDER(None)

    CONSIDER(Brightness)
    CONSIDER(Contrast)
    CONSIDER(Saturation)

    CONSIDER(Hue)
    CONSIDER(AutoHue)

    CONSIDER(Gamma)

    CONSIDER(Exposure)
    CONSIDER(AutoExposure)

    CONSIDER(WhiteBalance)
    CONSIDER(AutoWhiteBalance)

    CONSIDER(Gain)
    CONSIDER(AutoGain)

    CONSIDER(BacklightCompensation)
    CONSIDER(PowerLineFrequency)

    CONSIDER(Sharpness)

    CONSIDER(FocusRelative)
    CONSIDER(FocusAbsolute)
    CONSIDER(AutoFocus)

    CONSIDER(PanRelative)
    CONSIDER(TiltRelative)
    CONSIDER(PanTiltReset)
    
    CONSIDER(Norm)
    CONSIDER(Channel)
  }
  return "";
}

Controller::Controller() {
  g_static_rec_mutex_init(&m_mutex);
}

Controller::~Controller() {
  DestroyControls();
  g_static_rec_mutex_free(&m_mutex);
}

void Controller::DestroyControls() {
  for ( ChannelsMap::iterator channel = m_channels.begin();
        channel != m_channels.end(); ++channel) {
    delete channel->second;
    channel->second = NULL;
  }
}

void Controller::EnumerateControls(EnumerateCallback callback) {
  CHECK(callback->is_permanent());
  g_static_rec_mutex_lock(&m_mutex);
  for ( ChannelsMap::iterator channel = m_channels.begin();
        channel != m_channels.end(); ++channel) {
    if (!callback->Run((ControlID)channel->first, channel->second)) {
      break;
    }
  }
  g_static_rec_mutex_unlock(&m_mutex);
}

// Returns true if the control at the given id is relative.
const Controller::ControlChannel*
Controller::GetControlChannel(ControlID id) const {
  const ChannelsMap::const_iterator control = m_channels.find(id);
  if ( control != m_channels.end() ) {
    return control->second;
  }
  return NULL;
}

bool Controller::GetLimits(ControlID id, int* min, int* max, int* step) {
  bool result = false;
  g_static_rec_mutex_lock(&m_mutex);
  
  const ChannelsMap::iterator channel = m_channels.find(id);
  if ( channel != m_channels.end() ) {
    if ( channel->second != NULL ) {
      if ( channel->second->m_type == Range ) {
        if ( min != NULL ) {
          *min = channel->second->m_values.m_min;
        }
        if ( max != NULL ) {
          *max = channel->second->m_values.m_max;
        }
        if ( step != NULL ) {
          *step = channel->second->m_values.m_step;
        }
        result = true;
      } else {
        DLOG_DEBUG
          << "Control '" << channel->second->m_name.c_str()
          << "' of element '" << GST_OBJECT_PATH(m_element)
          << "' is not of range type, not retrieving the limits.";
      }
    }
  } else {
    DLOG_DEBUG
      << "Control ID " << id << " is not exposed by element '"
      << GST_OBJECT_PATH(m_element) << "', not retrieving the limits.";
  }
  
  g_static_rec_mutex_unlock(&m_mutex);
  return result;
}

// Gets the value of the control at the given id.
bool Controller::GetValue(ControlID id, int* value) const {
  bool result = false;
  g_static_rec_mutex_lock(&m_mutex);

  ChannelsMap::const_iterator channel = m_channels.find(id);
  if ( channel != m_channels.end() ) {
    if ( channel->second != NULL ) {
      if ( channel->second->m_type == Range ) {
        if ( value != NULL ) {
          if ( !channel->second->m_values.m_relative ) {
            result = DoGetValue(id, channel->second, value);
            DLOG_DEBUG
              << "Got " << *value
              << " (" << channel->first << ") "
              << " from control '" << channel->second->m_name.c_str() << "'"
              << " of element '" << GST_OBJECT_PATH(m_element) << "', result: "
              << result << ".";
          } else {
            DLOG_DEBUG
              << "Control '" << channel->second->m_name.c_str() << "'"
              << " (" << channel->first << ") "
              << " of element '" << GST_OBJECT_PATH(m_element)
              << "' is relative, not retrieving it.";
            *value = kInvalidValue;
          }
        }
      } else {
        DLOG_DEBUG
          << "Control '" << channel->second->m_name.c_str()
          << "' of element '" << GST_OBJECT_PATH(m_element)
          << "' is not of range type, not retrieving it.";
      }
    }
  } else {
    DLOG_DEBUG
      << "Control ID " << id << " is not exposed by element '"
      << GST_OBJECT_PATH(m_element) << "', not retrieving it.";
  }

  g_static_rec_mutex_unlock(&m_mutex);
  return result;
}
// Sets the value of the control at the given id.
bool Controller::SetValue(ControlID id, int value) {
  bool result = false;
  g_static_rec_mutex_lock(&m_mutex);

  ChannelsMap::iterator channel = m_channels.find(id);
  if ( channel != m_channels.end() ) {
    if ( channel->second != NULL ) {
      if ( value == kInvalidValue ) {
        DLOG_DEBUG
          << "Invalid value when setting control '"
          << channel->second->m_name.c_str() << "'"
          << " (" << channel->first << ") "
          << " of element '" << GST_OBJECT_PATH(m_element) << "' value.";
      } else {
        if ( channel->second->m_type == Range ) {
          if (!channel->second->m_read_only) {
            result = DoSetValue(id, channel->second, value);
            DLOG_DEBUG
              << "Set " << value << " to '"
              << channel->second->m_name.c_str() << "'"
              << " (" << channel->first << ") "
              << " of element '" << GST_OBJECT_PATH(m_element)
              << "', result: " << result << ".";
          } else {
            DLOG_DEBUG
              << "Control '" << channel->second->m_name.c_str() << "'"
              << " (" << channel->first << ") "
              << " of element '" << GST_OBJECT_PATH(m_element)
              << "' is read-only, not setting it.";
          }
        } else {
          DLOG_DEBUG
            << "Control '" << channel->second->m_name.c_str()
            << "' of element '" << GST_OBJECT_PATH(m_element)
            << "' is not of range type, not setting it.";
        }
      }
    }
  } else {
    DLOG_DEBUG
      << "Control ID " << id << " is not exposed by element '"
      << GST_OBJECT_PATH(m_element) << "', not setting it.";
  }

  g_static_rec_mutex_unlock(&m_mutex);
  return result;
}

// Gets the list of values of the control at the given id.
bool Controller::GetList(ControlID id, std::list<std::string>& list) const {
  bool result = false;
  g_static_rec_mutex_lock(&m_mutex);

  ChannelsMap::const_iterator channel = m_channels.find(id);
  if ( channel != m_channels.end() ) {
    if ( channel->second != NULL ) {
      if ( channel->second->m_type == List ) {
        result = DoGetList(id, channel->second, list);
        DLOG_DEBUG
          << "Listed the values of "
          << " (" << channel->first << ") "
          << " from control '" << channel->second->m_name.c_str() << "'"
          << " of element '" << GST_OBJECT_PATH(m_element) << "', result: "
          << result << ".";
      } else {
        DLOG_DEBUG
          << "Control '" << channel->second->m_name.c_str()
          << "' of element '" << GST_OBJECT_PATH(m_element)
          << "' is not of list type, not listing it.";
      }
    }
  } else {
    DLOG_DEBUG
      << "Control ID " << id << " is not exposed by element '"
      << GST_OBJECT_PATH(m_element) << "', not retrieving it.";
  }

  g_static_rec_mutex_unlock(&m_mutex);
  return result;
}

// Gets the value of the control at the given id.
bool Controller::GetValue(ControlID id, std::string* value) const {
  bool result = false;
  g_static_rec_mutex_lock(&m_mutex);

  ChannelsMap::const_iterator channel = m_channels.find(id);
  if ( channel != m_channels.end() ) {
    if ( channel->second != NULL ) {
      if ( channel->second->m_type == List ) {
        if ( value != NULL ) {
          result = DoGetValue(id, channel->second, value);
          DLOG_DEBUG
            << "Got " << *value
            << " (" << channel->first << ") "
            << " from control '" << channel->second->m_name.c_str() << "'"
            << " of element '" << GST_OBJECT_PATH(m_element) << "', result: "
            << result << ".";
        }
      } else {
        DLOG_DEBUG
          << "Control '" << channel->second->m_name.c_str()
          << "' of element '" << GST_OBJECT_PATH(m_element)
          << "' is not of list type, not retrieving it.";
      }
    }
  } else {
    DLOG_DEBUG
      << "Control ID " << id << " is not exposed by element '"
      << GST_OBJECT_PATH(m_element) << "', not retrieving it.";
  }

  g_static_rec_mutex_unlock(&m_mutex);
  return result;
}
// Sets the value of the control at the given id.
bool Controller::SetValue(ControlID id, const char* value) {
  bool result = false;
  g_static_rec_mutex_lock(&m_mutex);

  ChannelsMap::iterator channel = m_channels.find(id);
  if ( channel != m_channels.end() ) {
    if ( channel->second != NULL ) {
      if ( value == NULL ) {
        DLOG_DEBUG
          << "Invalid value when setting control '"
          << channel->second->m_name.c_str() << "'"
          << " (" << channel->first << ") "
          << " of element '" << GST_OBJECT_PATH(m_element) << "' value.";
      } else {
        if ( channel->second->m_type == List ) {
          if (!channel->second->m_read_only) {
            result = DoSetValue(id, channel->second, value);
            DLOG_DEBUG
              << "Set '" << value << "' to '"
              << channel->second->m_name.c_str() << "'"
              << " (" << channel->first << ") "
              << " of element '" << GST_OBJECT_PATH(m_element)
              << "', result: " << result << ".";
          } else {
            DLOG_DEBUG
              << "Control '" << channel->second->m_name.c_str() << "'"
              << " (" << channel->first << ") "
              << " of element '" << GST_OBJECT_PATH(m_element)
              << "' is read-only, not setting it.";
          }
        } else {
          DLOG_DEBUG
            << "Control '" << channel->second->m_name.c_str()
            << "' of element '" << GST_OBJECT_PATH(m_element)
            << "' is not of list type, not setting it.";
        }
      }
    }
  } else {
    DLOG_DEBUG
      << "Control ID " << id << " is not exposed by element '"
      << GST_OBJECT_PATH(m_element) << "', not setting it.";
  }

  g_static_rec_mutex_unlock(&m_mutex);
  return result;
}

void Controller::Attach(GstElement* element) {
  g_static_rec_mutex_lock(&m_mutex);
  Pipeline::ElementHandler::Attach(element);
  g_static_rec_mutex_unlock(&m_mutex);
}

void Controller::Detach() {
  g_static_rec_mutex_lock(&m_mutex);

  DestroyControls();
  if (m_element != NULL) {
    gst_object_unref(m_element);
    m_element = NULL;
  }

  g_static_rec_mutex_unlock(&m_mutex);
}

bool Controller::OnPipelineStateChanged(
  Pipeline* pipeline, Pipeline::State old_state, Pipeline::State new_state) {
  switch (new_state) {
  case Pipeline::Playing:
  {
    DLOG_PIPELINE_DEBUG(pipeline)
      << "Getting the controls of the element "
      << GST_OBJECT_PATH(m_element) << ".";
    g_static_rec_mutex_lock(&m_mutex);
    CreateControls(pipeline);
    g_static_rec_mutex_unlock(&m_mutex);
  }
  break;
  case Pipeline::Terminating_PLAYING_TO_READY:
    DLOG_PIPELINE_DEBUG(pipeline)
      << "Releasing the controls of the element "
      << GST_OBJECT_PATH(m_element) << ".";
    g_static_rec_mutex_lock(&m_mutex);
    DestroyControls();
      g_static_rec_mutex_unlock(&m_mutex);
      break;
  default:
    break;
  }
  return true;
}
}
