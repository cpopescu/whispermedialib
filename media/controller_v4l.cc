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

#include "media/controller_v4l.h"

// our namespace
namespace media {

V4LSourceController::V4LSourceController()
    : m_color_balance(NULL) {
  for ( int c = Controller::First; c <= Controller::Last; ++c ) {
    if ( MapControlID((ControlID)c) != 0 ) {
      m_channels[c] = NULL;
    }
  }
}

void V4LSourceController::CreateControls(Pipeline* pipeline) {
  g_static_rec_mutex_lock(&m_mutex);

  CHECK_NULL(m_color_balance);
  if ( gst_element_implements_interface(m_element, GST_TYPE_COLOR_BALANCE) ) {
    m_color_balance = GST_COLOR_BALANCE(m_element);
    CHECK_NOT_NULL(m_color_balance);
    const GList* channels = gst_color_balance_list_channels(m_color_balance);
    if ( channels != NULL ) {
      for ( ChannelsMap::iterator channel = m_channels.begin();
            channel != m_channels.end(); ++channel ) {
        const char* v4l_id = MapControlID((ControlID)channel->first);
        if ( v4l_id != NULL ) {
          for ( const GList* channel_item = channels;
                channel_item != NULL; channel_item = channel_item->next ) {
            const char* const label =
                reinterpret_cast<GstColorBalanceChannel*>(
                    channel_item->data)->label;
            if ( strcmp(v4l_id, label) == 0 ) {
              ControlChannel* control_channel = new ControlChannel(
                GST_COLOR_BALANCE_CHANNEL(channel_item->data),
                false,
                GST_COLOR_BALANCE_CHANNEL(channel_item->data)->label,
                false,
                GST_COLOR_BALANCE_CHANNEL(channel_item->data)->min_value,
                GST_COLOR_BALANCE_CHANNEL(channel_item->data)->max_value,
                0);
              channel->second = control_channel;

              LOG_PIPELINE_INFO(pipeline)
                << "Got V4L control '" << control_channel->m_name.c_str() << "'"
                << " (" << channel->first << ") "
                << ", range [" << control_channel->m_values.m_min
                << ", " << control_channel->m_values.m_max << "]"
                << " for element '" << GST_OBJECT_PATH(m_element) << "'";
              break;
            }
          }
        } else {
          DLOG_PIPELINE_DEBUG(pipeline)
            << "V4L2 control '" << ControlName((ControlID)channel->first)
            << "' is not available in element '" << GST_OBJECT_PATH(m_element)
            << "'";
        }
      }
    }
  } else {
    DLOG_PIPELINE_DEBUG(pipeline)
      << "The element '" << GST_OBJECT_PATH(m_element)
      << "' doesn't expose any V4L controls";
  }
  g_static_rec_mutex_unlock(&m_mutex);
}

void V4LSourceController::DestroyControls() {
  g_static_rec_mutex_lock(&m_mutex);

  m_color_balance = NULL;
  Controller::DestroyControls();

  g_static_rec_mutex_unlock(&m_mutex);
}

bool V4LSourceController::DoGetValue(ControlID id,
    ControlChannel* channel, int* value) const {
  if (m_color_balance !=  NULL) {
    *value = gst_color_balance_get_value(
        m_color_balance,
        reinterpret_cast<GstColorBalanceChannel*>(channel->m_channel));
    return true;
  }
  *value = kInvalidValue;
  return false;
}
bool V4LSourceController::DoSetValue(ControlID id,
    ControlChannel* channel, int value) {
  if (m_color_balance !=  NULL) {
    gst_color_balance_set_value(
        m_color_balance,
        reinterpret_cast<GstColorBalanceChannel*>(channel->m_channel), value);
    return true;
  }
  return false;
}

// Gets the list of values of the control at the given id.
bool V4LSourceController::DoGetList(ControlID id, ControlChannel* channel,
    std::list<std::string>& list) const {
  // TODO: Implement this..
  return false;
}
// Gets the value of the control at the given id.
bool V4LSourceController::DoGetValue(ControlID id, ControlChannel* channel,
    std::string* value) const {
  // TODO: Implement this...
  value->clear();
  return false;
}
// Sets the value of the control at the given id.
bool V4LSourceController::DoSetValue(ControlID id, ControlChannel* channel,
    const char* value) {
  // TODO: Implement this...
  return false;
}

const char* V4LSourceController::MapControlID(ControlID id) {
  switch (id) {
    case Hue:
      return "Hue";
    case Brightness:
      return "Brightness";
    case Contrast:
      return "Contrast";
    case Saturation:
      return "Saturation";
    default:
      return NULL;
  }
  return NULL;
}
}
