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

// @@@ V4L2 compatibility.
// TO BE SYNCHRONIZED ON V4L2 API CHANGES.

// Private V4L2 control identifiers,
// from uvcvideo.h.
// #include <linux/types.h>
#include <linux/videodev2.h>

#include "media/controller_v4l2.h"

#ifndef V4L2_CID_BACKLIGHT_COMPENSATION
#define V4L2_CID_BACKLIGHT_COMPENSATION      (V4L2_CID_PRIVATE_BASE + 0)
#endif
#ifndef V4L2_CID_POWER_LINE_FREQUENCY
#define V4L2_CID_POWER_LINE_FREQUENCY        (V4L2_CID_PRIVATE_BASE + 1)
#endif
#ifndef V4L2_CID_SHARPNESS
#define V4L2_CID_SHARPNESS                   (V4L2_CID_PRIVATE_BASE + 2)
#endif
#ifndef V4L2_CID_HUE_AUTO
#define V4L2_CID_HUE_AUTO                    (V4L2_CID_PRIVATE_BASE + 3)
#endif
#ifndef V4L2_CID_FOCUS_AUTO
#define V4L2_CID_FOCUS_AUTO                  (V4L2_CID_PRIVATE_BASE + 4)
#endif
#ifndef V4L2_CID_FOCUS_ABSOLUTE
#define V4L2_CID_FOCUS_ABSOLUTE              (V4L2_CID_PRIVATE_BASE + 5)
#endif
#ifndef V4L2_CID_FOCUS_RELATIVE
#define V4L2_CID_FOCUS_RELATIVE              (V4L2_CID_PRIVATE_BASE + 6)
#endif
#ifndef V4L2_CID_PAN_RELATIVE
#define V4L2_CID_PAN_RELATIVE                (V4L2_CID_PRIVATE_BASE + 7)
#endif
#ifndef V4L2_CID_TILT_RELATIVE
#define V4L2_CID_TILT_RELATIVE               (V4L2_CID_PRIVATE_BASE + 8)
#endif
#ifndef V4L2_CID_PANTILT_RESET
#define V4L2_CID_PANTILT_RESET               (V4L2_CID_PRIVATE_BASE + 9)
#endif
#ifndef V4L2_CID_EXPOSURE_AUTO
#define V4L2_CID_EXPOSURE_AUTO               (V4L2_CID_PRIVATE_BASE + 10)
#endif
#ifndef V4L2_CID_EXPOSURE_ABSOLUTE
#define V4L2_CID_EXPOSURE_ABSOLUTE           (V4L2_CID_PRIVATE_BASE + 11)
#endif
#ifndef V4L2_CID_WHITE_BALANCE_TEMPERATURE_AUTO
#define V4L2_CID_WHITE_BALANCE_TEMPERATURE_AUTO  (V4L2_CID_PRIVATE_BASE + 12)
#endif
#ifndef V4L2_CID_WHITE_BALANCE_TEMPERATURE
#define V4L2_CID_WHITE_BALANCE_TEMPERATURE   (V4L2_CID_PRIVATE_BASE + 13)
#endif
#ifndef V4L2_CID_EXPOSURE_AUTO_PRIORITY
#define V4L2_CID_EXPOSURE_AUTO_PRIORITY      (V4L2_CID_PRIVATE_BASE + 14)
#endif

// @@@ GStreamer compatibility.
// TO BE SYNCHRONIZED ON GStreamer ABI CHANGES.
typedef struct _GstV4l2ColorBalanceChannel {
  GstColorBalanceChannel parent;

  guint32 id;
} GstV4l2ColorBalanceChannel;

// our namespace
namespace media {

V4L2SourceController::V4L2SourceController()
    : m_color_balance(NULL), m_tuner(NULL) {
  for ( int c = Controller::First; c <= Controller::Last; ++c ) {
    if ( MapControlID((ControlID)c) != 0 ) {
      m_channels[c] = NULL;
    }
  }
}

void V4L2SourceController::CreateControls(Pipeline* pipeline) {
  g_static_rec_mutex_lock(&m_mutex);

  CHECK(m_color_balance == NULL);
  if (gst_element_implements_interface(m_element, GST_TYPE_COLOR_BALANCE)) {
    m_color_balance = GST_COLOR_BALANCE(m_element);
    const GList* channels =  gst_color_balance_list_channels(m_color_balance);
    if (channels != NULL) {
      for ( const GList* channel_item = channels; channel_item != NULL;
            channel_item = channel_item->next ) {
        LOG_PIPELINE_DEBUG(pipeline)
          << "V4L2 colorbalance control '"
          << GST_COLOR_BALANCE_CHANNEL(channel_item->data)->label << "' "
          << " ("
          << ((reinterpret_cast<GstV4l2ColorBalanceChannel*>(
                   channel_item->data))->id)
          << ") " <<
        "is available.";
      }
      for ( ChannelsMap::iterator channel = m_channels.begin();
            channel != m_channels.end(); ++channel ) {
        bool read_only = false;
        bool relative = false;
        const int v4l2_id = MapControlID((ControlID)channel->first,
                                         &read_only, &relative);
        // check if a colorbalance control
        if ( v4l2_id == -1 ) {
          continue;
        }
        if ( v4l2_id != 0 ) {
          for ( const GList* channel_item = channels; channel_item != NULL;
                channel_item = channel_item->next ) {
            if ( (reinterpret_cast<GstV4l2ColorBalanceChannel*>(
                      channel_item->data))->id ==
                 static_cast<guint32>(v4l2_id) ) {
              ControlChannel* control_channel = new ControlChannel(
                  GST_COLOR_BALANCE_CHANNEL(channel_item->data),
                  read_only,
                  GST_COLOR_BALANCE_CHANNEL(channel_item->data)->label,
                  relative,
                  GST_COLOR_BALANCE_CHANNEL(channel_item->data)->min_value,
                  GST_COLOR_BALANCE_CHANNEL(channel_item->data)->max_value,
                  0);
              channel->second = control_channel;

              LOG_PIPELINE_DEBUG(pipeline)
                  << "Got V4L2 colorbalance control '"
                  << control_channel->m_name.c_str() << "'"
                  << " (" << channel->first << ") "
                  << ", range [" << control_channel->m_values.m_min
                  << ", " << control_channel->m_values.m_max << "]"
                << " for element '" << GST_OBJECT_PATH(m_element) << "'.";
              break;
            }
          }
        } else {
          LOG_PIPELINE_INFO(pipeline)
            << "V4L2 control '" << ControlName((ControlID)channel->first)
            << "' is not available in element '" << GST_OBJECT_PATH(m_element)
            << "'.";
        }
      }
    }
  } else {
    LOG_PIPELINE_INFO(pipeline)
      << "The element '" << GST_OBJECT_PATH(m_element)
      << "' doesn't expose any V4L2 colorbalance controls";
  }

  CHECK(m_tuner == NULL);
  if (gst_element_implements_interface(m_element, GST_TYPE_TUNER)) {
    m_tuner = GST_TUNER(m_element);
    LOG_PIPELINE_INFO(pipeline)
      << "The element '" << GST_OBJECT_PATH(m_element)
      << "' is providing a tuner interface.";

    for ( ChannelsMap::iterator channel = m_channels.begin();
          channel != m_channels.end(); ++channel ) {
      if ( channel->first == Norm ) {
        channel->second = new ControlChannel(
            NULL,
            false,
            "Norm",
            std::list<std::string>());
      } else
      if ( channel->first == Channel ) {
        channel->second = new ControlChannel(
            NULL,
            false,
            "Channel",
            std::list<std::string>());
      }
    }
  }

  g_static_rec_mutex_unlock(&m_mutex);
}

void V4L2SourceController::DestroyControls() {
  g_static_rec_mutex_lock(&m_mutex);

  m_tuner = NULL;
  m_color_balance = NULL;
  Controller::DestroyControls();

  g_static_rec_mutex_unlock(&m_mutex);
}

bool V4L2SourceController::DoGetValue(ControlID id,
    ControlChannel* channel, int* value) const {
  // The only interface that implements Range controls is colorbalance..
  if (m_color_balance != NULL) {
    *value = gst_color_balance_get_value(
        m_color_balance,
        reinterpret_cast<GstColorBalanceChannel*>(channel->m_channel));
    return true;
  }
  *value = kInvalidValue;
  return false;
}

bool V4L2SourceController::DoSetValue(ControlID id,
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
bool V4L2SourceController::DoGetList(ControlID id, ControlChannel* channel,
    std::list<std::string>& list) const {
  // TODO: Implement this..
  return false;
}
// Gets the value of the control at the given id.
bool V4L2SourceController::DoGetValue(ControlID id, ControlChannel* channel,
    std::string* value) const {
  // The only interface that implements List controls is tuner..
  if (m_tuner != NULL) {
    switch (id) {
      case Norm:
        {
        GstTunerNorm *norm = gst_tuner_get_norm(m_tuner);
        if (norm) {
          *value = norm->label;
          g_object_unref(norm);
          DLOG_DEBUG << "Retrieved norm '" << *value
            << "' from '" << GST_OBJECT_PATH(m_element) << "'.";
          return true;
        } else {
          DLOG_DEBUG << "No current norm on element '"
            << GST_OBJECT_PATH(m_element) << "'.";
        }
        }
        break;
      case Channel:
        {
        GstTunerChannel *chan = gst_tuner_get_channel(m_tuner);
        if (chan) {
          *value = chan->label;
          g_object_unref(chan);
          DLOG_DEBUG << "Retrieved channel '" << *value
            << "' from '" << GST_OBJECT_PATH(m_element) << "'.";
          return true;
        } else {
          DLOG_DEBUG << "No current channel on element '"
            << GST_OBJECT_PATH(m_element) << "'.";
        }
        }
        break;
      default:
        ;
    }
  }
  value->clear();
  return false;
}
// Sets the value of the control at the given id.
bool V4L2SourceController::DoSetValue(ControlID id, ControlChannel* channel,
    const char* value) {
  // The only interface that implements List controls is tuner..
  if (m_tuner != NULL) {
    switch (id) {
      case Norm:
        {
        GstTunerNorm* norm = gst_tuner_find_norm_by_name(m_tuner,
            const_cast<gchar*>(value));
        if (norm) {
          DLOG_DEBUG << "Setting norm '" << value
            << "' on '" << GST_OBJECT_PATH(m_element) << "'.";
          gst_tuner_set_norm(m_tuner, norm);
          return true;
        } else {
          DLOG_DEBUG << "Norm '" << value
            << "' not found on '" << GST_OBJECT_PATH(m_element) << "'.";
        }
        }
        break;
      case Channel:
        {
        GstTunerChannel* chan = gst_tuner_find_channel_by_name(m_tuner,
            const_cast<gchar*>(value));
        if (chan) {
          DLOG_DEBUG << "Setting channel '" << value
            << "' on '" << GST_OBJECT_PATH(m_element) << "'.";
          gst_tuner_set_channel(m_tuner, chan);
          return true;
        } else {
          DLOG_DEBUG << "Channel '" << value
            << "' not found on '" << GST_OBJECT_PATH(m_element) << "'.";
        }
        }
        break;
      default:
        ;
    }
  }
  return false;
}

int V4L2SourceController::MapControlID(
  ControlID id, bool* read_only, bool* relative) {
  if (read_only != NULL) {
    *read_only = false;
  }
  if (relative != NULL) {
    *relative = false;
  }

  switch (id) {
    case None:
      return 0;

    case Brightness:
      return V4L2_CID_BRIGHTNESS;
    case Contrast:
      return V4L2_CID_CONTRAST;
    case Saturation:
      return V4L2_CID_SATURATION;

    case Hue:
      return V4L2_CID_HUE;
    case AutoHue:
      return V4L2_CID_HUE_AUTO;

    case Gamma:
      return V4L2_CID_GAMMA;

    case Exposure:
      return V4L2_CID_EXPOSURE_ABSOLUTE;
    case AutoExposure:
      return V4L2_CID_EXPOSURE_AUTO;

    case WhiteBalance:
      return V4L2_CID_WHITE_BALANCE_TEMPERATURE;
    case AutoWhiteBalance:
      return V4L2_CID_WHITE_BALANCE_TEMPERATURE_AUTO;

    case Gain:
      return V4L2_CID_GAIN;
    case AutoGain:
      return V4L2_CID_AUTOGAIN;

    case BacklightCompensation:
      return V4L2_CID_BACKLIGHT_COMPENSATION;
    case PowerLineFrequency:
      return V4L2_CID_POWER_LINE_FREQUENCY;

    case Sharpness:
      return V4L2_CID_SHARPNESS;

    case FocusRelative:
      if (relative != NULL) {
        *relative = true;
      }
      return V4L2_CID_FOCUS_RELATIVE;
    case FocusAbsolute:
      return V4L2_CID_FOCUS_ABSOLUTE;
    case AutoFocus:
      return V4L2_CID_FOCUS_AUTO;

    case PanRelative:
      if (relative != NULL) {
        *relative = true;
      }
      return V4L2_CID_PAN_RELATIVE;
    case TiltRelative:
      if (relative != NULL) {
        *relative = true;
      }
      return V4L2_CID_TILT_RELATIVE;
    case PanTiltReset:
      if (relative != NULL) {
        *relative = true;
      }
      return V4L2_CID_PANTILT_RESET;

    case Norm:
    case Channel:
      return -1;

    default:
      ;
  }
  return 0;
}
}
