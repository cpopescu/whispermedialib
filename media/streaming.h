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
#ifndef __STREAMING_STREAMING_H__
#define __STREAMING_STREAMING_H__

#include <glib.h>
#include <gst/gst.h>
#include <gst/interfaces/colorbalance.h>
#include <gst/interfaces/tuner.h>
#include <gst/dataprotocol/dataprotocol.h>

#include <iostream>
#include <string>
#include <vector>
#include <utility>

#include <whisperlib/common/base/types.h>
#include <whisperlib/common/base/log.h>
#include <whisperlib/common/base/callback.h>
#include <whisperlib/common/base/parse.h>

namespace media {

// Initializes the streaming library.
void Init(int argc, char* argv[]);
// Shuts down the streaming library.
void Shutdown();

// Helper class that keeps and frees a gchar* string.
class string_holder {
 private:
  gchar* m_string;
 public:
  explicit string_holder(gchar* string) :
      m_string(string) {
  }
  ~string_holder() {
    g_free(m_string);
  }
 public:
  operator const gchar*() const {
    return m_string;
  }
};

// Helper macro that retrieves the path of a GStreamer
// in the associated object hierarchy.
#define GST_OBJECT_PATH(o) \
  string_holder(gst_object_get_path_string(GST_OBJECT(o)))

// The base of all the messages that are posted
// across pipelines and pipeline runners.
struct Message {
  enum Kind {
    Last = 0
  };
  int32 m_kind;
  explicit Message(int32 kind)
      : m_kind(kind) {
  }

  // The messages can also transport parameters,
  // by the means of this union.
  union Param {
    void* m_pointer;
    long m_long;
    unsigned long m_ulong;
    long long m_longlong;
    unsigned long long m_ulonglong;

    Param() : m_ulonglong(0) {}

    Param(void* _pointer) : m_pointer(_pointer) {}
    Param(long _long) : m_long(_long) {}
    Param(unsigned long _ulong) : m_ulong(_ulong) {}
    Param(long long _longlong) : m_longlong(_longlong) {}
    Param(unsigned long long _ulonglong) : m_ulonglong(_ulonglong) {}
  };
};

// The message queue that is used to transport
// the messages across pipelines/pipeline runners.
//
// The messages are sorted, so specific messages
// can be prioritized over others.
//
// Currently all the messages except Terminate
// are queued in the same order as posted, Terminate
// messages being queued ahead of any other messages.
class MessageQueue {
 public:
  MessageQueue();
  ~MessageQueue();

  // Posts a new message to the message queue.
  void PostMessage(Message* message);
  // Blocks the calling thread until a message is available,
  // or the timeput elapses.
  Message* PopMessage(int64 timeout_ms = -1);

  // Sets the callback to be called when posting a new message.
  void SetPostMessageCallback(Callback1<Message*>* post_message_callback);

 private:
  GAsyncQueue* m_message_queue;
  Callback1<Message*>* m_post_message_callback;
};

// The protocols that are currently supported
// in bitstream connections provided by pipelines.
enum Protocol {
  NoProtocol = 0,
  GDP,

  DefaultProtocol = NoProtocol
};

namespace Stream {
// The type of a stream.
enum Type {
  Unknown = 0,

  MP3,
  AAC,

  FLV
};
// Returns the name of a specific encoder type.
const char* GetName(Type type);
// Returns the mime type associated with a specific encoder type.
const char* GetMimeType(Type type);
}

namespace Encoder {

namespace Audio {
// The type of an audio encoder.
enum  Type {
  RAW,

  MP3,
  AAC,

  Default = MP3
};
// Returns the name of a specific audio encoder type.
const char* GetName(Type type);
// Returns the mime type associated with a specific audio encoder type.
const char* GetMimeType(Type type);

// The parameters of an audio encoder.
struct Params {
  Params() {
    type_ = Default;
    bitrate_ =
    samplerate_ =
    samplewidth_ = 0;
    raw_ = false;
  }
  Type type_;
  int bitrate_;       // 0 means default, ie. not set, -1 means no audio
  int samplerate_;    // 0 means default, ie. not set
  int samplewidth_;   // 0 means default, ie. not set
  bool raw_;          // true means raw, unformatted

  bool operator ==(const Params& other) const {
    return ((type_ == other.type_) &&
            (bitrate_ == other.bitrate_) &&
            (samplerate_ == other.samplerate_) &&
            (samplewidth_ == other.samplewidth_) &&
            (raw_ == other.raw_));
  }
  bool operator <(const Params& other) const {
    if (type_ == other.type_)
    if (bitrate_ == other.bitrate_)
    if (samplerate_ == other.samplerate_)
    if (samplewidth_ == other.samplewidth_)
    if (raw_ == other.raw_)
      return false;
    else
      return raw_ < other.raw_;
    else
      return samplewidth_ < other.samplewidth_;
    else
      return samplerate_ < other.samplerate_;
    else
      return bitrate_ < other.bitrate_;
    else
      return type_ < other.type_;
  }
};
}   // namespace Audio

namespace Video {
// The type of a video encoder.
enum Type {
  RGB,
  YUV,      // actually I420

  JPEG,
  PNG,

  VP6,
  H264,

  Default = VP6
};
// Returns the name of a specific video encoder type.
const char* GetName(Type type);
// Returns the mime type associated with a specific video encoder type.
const char* GetMimeType(Type type);

// The parameters of a video encoder.
struct Params {
  Params() {
    type_ = Default;
    bitrate_ =
    gop_size_ =
    width_ =
    height_ =
    framerate_n_ =
    framerate_d_ = 0;
    strict_rc_ = false;
  }

  Type type_;
  int bitrate_;      // 0 means default, ie. not set, -1 means no video
  int gop_size_;     // 0 means default, ie. not set
  int width_;        // 0 means default, ie. not set
  int height_;       // 0 means default, ie. not set
  int framerate_n_;  // 0 means default, ie. not set
  int framerate_d_;  // 0 means default, ie. not set
  bool strict_rc_;

  bool operator ==(const Params& other) const {
    return
    (type_ == other.type_) &&
    (bitrate_ == other.bitrate_) &&
    (gop_size_ == other.gop_size_) &&
    (width_ == other.width_) &&
    (height_ == other.height_) &&
    (framerate_n_ == other.framerate_n_) &&
    (framerate_d_ == other.framerate_d_) &&
    (strict_rc_ == other.strict_rc_);
  }
  bool operator <(const Params& other) const {
    if (type_ == other.type_)
    if (bitrate_ == other.bitrate_)
    if (gop_size_ == other.gop_size_)
    if (width_ == other.width_)
    if (height_ == other.height_)
    if (framerate_n_ == other.framerate_n_)
    if (framerate_d_ == other.framerate_d_)
    if (strict_rc_ == other.strict_rc_)
      return false;
    else
      return strict_rc_ < other.strict_rc_;
    else
      return framerate_d_ < other.framerate_d_;
    else
      return framerate_n_ < other.framerate_n_;
    else
      return height_ < other.height_;
    else
      return width_ < other.width_;
    else
      return gop_size_ < other.gop_size_;
    else
      return bitrate_ < other.bitrate_;
    else
      return type_ < other.type_;
  }
};
}    // namespace Video
}    // namespace Encoder

bool ParseFromValues(parsable::Parsable* value,
                     const char* name,
                     vector<pair<string, string> >& values);

template<typename T> bool ParseIntFromValues(
  T* value,
  const char* name,
  vector<pair<string, string> >& values,
  T def_value) {
  CHECK_NOT_NULL(name);
  CHECK_NOT_NULL(value);

  parsable::TParsable<parsable::TIntegerTraits<T> > parsable;
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
bool ParseBoolFromValues(bool* value,
                         const char* name,
                         vector<pair<string, string> >& values,
                         bool def_value);

bool ParseEncoderParameters(const char* values,
                            Stream::Type& encoder_type,
                            Encoder::Audio::Params& audio_params,
                            Encoder::Video::Params& video_params,
                            int &audio_port,
                            int &video_port);

bool ParseEncoderParameters(vector<pair<string, string> >& values,
                            Stream::Type& encoder_type,
                            Encoder::Audio::Params& audio_params,
                            Encoder::Video::Params& video_params,
                            int &audio_port,
                            int &video_port);
}   // namespace media

#endif  // __STREAMING_STREAMING_H__
