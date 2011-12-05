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

#include <whisperlib/common/base/system.h>
#include <whisperlib/common/base/log.h>
#include <whisperlib/common/base/gflags.h>

#include "media/streaming.h"

// private, custom elements...
#include "media/gst/gstaacp.h"

// our namespace
namespace media {

// One of the GST internals... we copied it here in order
// to be able to display log entries similar with the original
// GStreamer log entries.
static gchar* gst_debug_print_object(gpointer ptr) {
  GObject* object = reinterpret_cast<GObject*>(ptr);

#ifdef unused
  // This is a cute trick to detect unmapped memory, but is unportable,
  // slow, screws around with madvise, and not actually that useful.
  do {
    int ret;

    ret = madvise(reinterpret_cast<void*>(
                      reinterpret_cast<unsigned long>(ptr) & (~0xfff)),
                  4096, 0);
    if (ret == -1 && errno == ENOMEM) {
      buffer = g_strdup_printf("%p (unmapped memory)", ptr);
    }
  } while ( false );
#endif

  // nicely printed object
  if (object == NULL) {
    return g_strdup ("(NULL)");
  }
  if (*reinterpret_cast<GType*>(ptr) == GST_TYPE_CAPS) {
    return gst_caps_to_string(reinterpret_cast<GstCaps*>(ptr));
  }
  if (*reinterpret_cast<GType*>(ptr) == GST_TYPE_STRUCTURE) {
    return gst_structure_to_string(reinterpret_cast<GstStructure*>(ptr));
  }
#ifdef USE_POISONING
  if (*reinterpret_cast<guint32*>(ptr) == 0xffffffff) {
    return g_strdup_printf("<poisoned@%p>", ptr);
  }
#endif
  if (GST_IS_PAD(object) && GST_OBJECT_NAME(object)) {
    return g_strdup_printf("<%s:%s>", GST_DEBUG_PAD_NAME (object));
  }
  if (GST_IS_OBJECT(object) && GST_OBJECT_NAME(object)) {
    return g_strdup_printf("<%s>", GST_OBJECT_NAME (object));
  }
  if (G_IS_OBJECT(object)) {
    return g_strdup_printf("<%s@%p>", G_OBJECT_TYPE_NAME(object), object);
  }
  if (GST_IS_MESSAGE(object)) {
    GstMessage* msg = GST_MESSAGE_CAST(object);
    gchar* s;
    gchar* ret;

    if (msg->structure) {
      s = gst_structure_to_string(msg->structure);
    } else {
      s = g_strdup("(NULL)");
    }

    ret = g_strdup_printf("%s message from element '%s': %s",
        GST_MESSAGE_TYPE_NAME(msg), (msg->src != NULL) ?
        GST_ELEMENT_NAME(msg->src) : "(NULL)", s);
    g_free(s);
    return ret;
  }

  return g_strdup_printf("%p", ptr);
}

// The GLib print handler - used to hide the initial GStreamer messages.
static
void __glib_print_function(const gchar* message) {
}

// The GStreamer log handler - used to route all the GStreamer logging
// through out own log system.
static
void __gst_log_function(GstDebugCategory* category,
                        GstDebugLevel level,
                        const gchar* file,
                        const gchar* function,
                        gint line,
                        GObject* object,
                        GstDebugMessage* message,
                        gpointer data) {
  if (level > gst_debug_category_get_threshold(category))
    return;

  switch (level) {
    case GST_LEVEL_ERROR:
      LOG_FILE_LINE_ERROR(file, line) <<
      gst_debug_level_get_name(level) << " " <<
      gst_debug_category_get_name(category) << " " <<
      function << ":" << (object ?
                          (const gchar*)string_holder(
                            gst_debug_print_object(object)) : "") << " " <<
      gst_debug_message_get(message);
      break;
    case GST_LEVEL_WARNING:
      LOG_FILE_LINE_WARNING(file, line) <<
      gst_debug_level_get_name(level) << " " <<
      gst_debug_category_get_name(category) << " " <<
      function << ":" << (object ?
                          (const gchar*) string_holder(
                            gst_debug_print_object(object)) : "") << " " <<
      gst_debug_message_get(message);
      break;
    case GST_LEVEL_INFO:
      LOG_FILE_LINE_INFO(file, line) <<
      gst_debug_level_get_name(level) << " " <<
      gst_debug_category_get_name(category) << " " <<
      function << ":" << (object ?
                          (const gchar*) string_holder(
                            gst_debug_print_object(object)) : "") << " " <<
      gst_debug_message_get(message);
      break;
    case GST_LEVEL_DEBUG:
      LOG_FILE_LINE_DEBUG(file, line) <<
      gst_debug_level_get_name(level) << " " <<
      gst_debug_category_get_name(category) << " " <<
      function << ":" << (object ?
                          (const gchar*) string_holder(
                            gst_debug_print_object(object)) : "") << " " <<
      gst_debug_message_get(message);
      break;
    case GST_LEVEL_LOG:
      LOG_FILE_LINE_INFO(file, line) <<
      gst_debug_level_get_name(level) << " " <<
      gst_debug_category_get_name(category) << " " <<
      function << ":" << (object ?
                          (const gchar*) string_holder(
                            gst_debug_print_object(object)) : "") << " " <<
      gst_debug_message_get(message);
      break;
    default:
      break;
  }
}

// TODO(mihai): This will be probably deprecated (it dissappeared
// from the header files.. still, it seems we need it).
extern "C" {
  void _gst_plugin_initialize(void);
}

// Initializes the streaming library.
void Init(int argc, char* argv[]) {
  google::AllowCommandLineReparsing();
  common::Init(argc, argv);

  g_set_printerr_handler(__glib_print_function);

  // register private, custom elements...
  register_aacp();

  gst_init(&argc, &argv);

  gst_debug_remove_log_function(gst_debug_log_default);
  gst_debug_add_log_function(__gst_log_function, NULL);

  _gst_plugin_initialize();
}

// Shuts down the streaming library.
void Shutdown() {
  gst_deinit();
}

MessageQueue::MessageQueue()
  : m_post_message_callback(NULL) {
  m_message_queue = g_async_queue_new();
  CHECK_NOT_NULL(m_message_queue)
      << "Couldn't create the message queue.";
}

MessageQueue::~MessageQueue() {
  delete m_post_message_callback;

  CHECK_NOT_NULL(m_message_queue);
  g_async_queue_unref(m_message_queue);
}

// Callback that sorts messages in the message queue.
//
// Currently prioritizes Terminate messages over all the other
// message kinds.
void MessageQueue::SetPostMessageCallback(
    Callback1<Message*>* post_message_callback) {
  CHECK(m_post_message_callback == NULL);

  m_post_message_callback = post_message_callback;
  if (m_post_message_callback != NULL) {
    CHECK(m_post_message_callback->is_permanent());
  }
}

void MessageQueue::PostMessage(Message* message) {
  CHECK_NOT_NULL(m_message_queue);
  //  g_async_queue_push_sorted(m_message_queue, message,
  //                            MessageCompareCallback, NULL);
  g_async_queue_push(m_message_queue, message);

  if (m_post_message_callback != NULL) {
    m_post_message_callback->Run(message);
  }
}

Message* MessageQueue::PopMessage(int64 timeout) {
  CHECK_NOT_NULL(m_message_queue);
  if (timeout == -1) {
    return static_cast<Message*>(g_async_queue_pop(m_message_queue));
  } else
  if (timeout == 0) {
    return static_cast<Message*>(g_async_queue_try_pop(m_message_queue));
  }
  
  GTimeVal time;
  g_get_current_time(&time);
  g_time_val_add(&time, (glong)(1000*timeout));
  return static_cast<Message*>(g_async_queue_timed_pop(
      m_message_queue, &time));
}

// Returns the name of a specific encoder type.
const char* Stream::GetName(Type type) {
  switch (type) {
    case Unknown:
      return "Unknown";
    case MP3:
      return "MP3";
    case AAC:
      return "AAC";
    case FLV:
      return "FLV";
    default:
      LOG_FATAL << "Stream: Invalid encoder type.";
  }
  return "";
}
// Returns the mime type associated with a specific encoder type.
const char* Stream::GetMimeType(Type type) {
  switch (type) {
    case Unknown:
      return "video/x-unknown";
    case MP3:
      return "audio/mpeg";
    case AAC:
      return "audio/aac";
    case FLV:
      return "video/flv";
    default:
      LOG_FATAL << "Stream: Invalid encoder type.";
  }
  return "";
}

// Returns the name of a specific audio encoder type.
const char* Encoder::Audio::GetName(Type type) {
  switch (type) {
    case MP3:
      return "MP3";
    case AAC:
      return "AAC";
    default:
      LOG_FATAL << "Encoder::Audio: Invalid audio encoder type.";
  }
  return "";
}
// Returns the mime type associated with a specific audio encoder type.
const char* Encoder::Audio::GetMimeType(Type type) {
  switch (type) {
    case MP3:
      return "audio/mpeg";
    case AAC:
      return "audio/aac";
    default:
      LOG_FATAL << "Encoder::Audio: Invalid audio encoder type.";
  }
  return "";
}

// Returns the name of a specific encoder type
const char* Encoder::Video::GetName(Type type) {
  switch (type) {
    case VP6:
      return "VP6";
    case H264:
      return "H.264";
    default:
      LOG_FATAL << "Encoder::Video: Invalid video encoder type.";
  }
  return "";
}
// Returns the mime type associated with a specific encoder type
const char* Encoder::Video::GetMimeType(Type type) {
  switch (type) {
    case VP6:
      return "video/flv";
    case H264:
      return "video/x-h264";
    default:
      LOG_FATAL << "Enocder::Video: Invalid video encoder type.";
  }
  return "";
}

bool ParseFromValues(
    parsable::Parsable* value,
    const char* name,
    vector<pair<string, string> >& values) {
  CHECK_NOT_NULL(name);
  CHECK_NOT_NULL(value);

  bool success = true;
  for ( size_t index = 0; index < values.size(); ) {
    if (values[index].first == name) {
      success &= value->Parse(values[index].second.c_str(), NULL);
      values.erase(values.begin()+index);
    } else {
      index++;
    }
  }
  return success;
}
bool ParseBoolFromValues(
  bool* value, const char* name,
  vector<pair<string, string> >& values, bool def_value) {
  CHECK_NOT_NULL(name);
  CHECK_NOT_NULL(value);

  parsable::Boolean parsable;
  if (ParseFromValues(&parsable, name, values)) {
    if (parsable.IsSet()) {
      *value = parsable;
    } else {
      *value = def_value;
    }
    return true;
  }
  return false;
}

bool ParseEncoderParameters(const char* values,
                            Stream::Type& encoder_type,
                            Encoder::Audio::Params& audio_params,
                            Encoder::Video::Params& video_params,
                            int &audio_port,
                            int &video_port) {
  vector<pair<string, string> > valuesv;
  strutil::SplitPairs(values, ",", "=", &valuesv);
  return ParseEncoderParameters(valuesv,
                                encoder_type,
                                audio_params,
                                video_params,
				audio_port,
				video_port);
}
bool ParseEncoderParameters(
    vector<pair<string, string> >& values,
    Stream::Type& encoder_type,
    Encoder::Audio::Params& audio_params,
    Encoder::Video::Params& video_params,
    int &audio_port,
    int &video_port) {
  encoder_type = Stream::Unknown;

  // get the encoder type
  parsable::String encoder;
  if (!ParseFromValues(&encoder, "encoder", values) || !encoder.IsSet()) {
    LOG_ERROR << "Couldn't parse the encoder type.";
    return false;
  }
  // consider the encoder type
  bool needs_audio = false;
  bool needs_video = false;
  if (encoder == "flv") {
    encoder_type = Stream::FLV;
    needs_audio =
    needs_video = true;
    // get the audio encoder type, defaults to MP3
    parsable::String audio_encoder;
    if (!ParseFromValues(&audio_encoder, "audio_encoder", values)) {
      LOG_ERROR << "Couldn't parse the audio encoder type.";
      return false;
    }
    if (!audio_encoder.IsSet() || audio_encoder == "mp3") {
      audio_params.type_ = Encoder::Audio::MP3;
    } else if (audio_encoder == "aac") {
      audio_params.type_ = Encoder::Audio::AAC;
    } else {
      LOG_ERROR << "Invalid audio encoder type.";
      return false;
    }
    // get the video encoder type, defaults to FLV(OnVPxx)
    parsable::String video_encoder;
    if (!ParseFromValues(&video_encoder, "video_encoder", values)) {
      LOG_ERROR << "Couldn't parse the video encoder type.";
      return false;
    }
    if (!video_encoder.IsSet() || video_encoder == "vp6") {
      video_params.type_ = Encoder::Video::VP6;
    } else if (video_encoder == "h264") {
      video_params.type_ = Encoder::Video::H264;
    } else {
      LOG_ERROR << "Invalid video encoder type '"
                << video_encoder.Value() << "'.";
      return false;
    }
  } else if (encoder == "mp3") {
    encoder_type = Stream::MP3;
    needs_audio = true;
    audio_params.type_ = Encoder::Audio::MP3;
  } else if (encoder == "aac") {
    encoder_type = Stream::AAC;
    needs_audio = true;
    audio_params.type_ = Encoder::Audio::AAC;
  } else {
    LOG_ERROR << "Invalid encoder type '" << encoder.Value() << "'.";
    return false;
  }

  // get the audio encoder parameters
  audio_port = 0;
  if (needs_audio)
  if (
    !ParseIntFromValues(&audio_port,
                        "audio_port", values, 0) ||
    !ParseIntFromValues(&audio_params.bitrate_,
                        "audio_bitrate", values, 0) ||
    !ParseIntFromValues(&audio_params.samplerate_,
                        "audio_samplerate", values, 0) ||
    !ParseIntFromValues(&audio_params.samplewidth_,
                        "audio_samplewidth", values, 0)
  ) {
    LOG_ERROR << "Invalid audio parameters.";
    return false;
  }
  // get the video encoder parameters
  video_port = 0;
  if (needs_video)
  if (
    !ParseIntFromValues(&video_port,
                        "video_port", values, 0) ||
    !ParseIntFromValues(&video_params.bitrate_,
                        "video_bitrate", values, 0) ||
    !ParseIntFromValues(&video_params.gop_size_,
                        "video_gop_size", values, 0) ||
    !ParseIntFromValues(&video_params.width_,
                        "video_width", values, 0) ||
    !ParseIntFromValues(&video_params.height_,
                        "video_height", values, 0) ||
    !ParseIntFromValues(&video_params.framerate_n_,
                        "video_framerate_n", values, 0) ||
    !ParseIntFromValues(&video_params.framerate_d_,
                        "video_framerate_d", values, 0) ||
    !ParseBoolFromValues(&video_params.strict_rc_,
                         "video_strict_rc", values, false)
  ) {
    LOG_ERROR << "Invalid video parameters.";
    return false;
  }
  return true;
}
}
