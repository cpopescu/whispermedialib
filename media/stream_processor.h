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

#ifndef __STREAMING_STREAM_PROCESSOR_H__
#define __STREAMING_STREAM_PROCESSOR_H__

#include <gst/dataprotocol/dataprotocol.h>

#include <string>

#include <whisperlib/common/io/buffer/memory_stream.h>
#include <whisperstreamlib/base/consts.h>

// our namespace
namespace media {

// The component that performs the final processing on the stream,
// before the data reaches the server.
class StreamProcessor {
 public:
  StreamProcessor() {}
  virtual ~StreamProcessor() {}

  // Processes data to be passed to the output.
  virtual
  bool Process(io::MemoryStream* output,
               io::MemoryStream* input,
               int32 output_capacity,
               bool& send_frame) = 0;
 private:
  DISALLOW_EVIL_CONSTRUCTORS(StreamProcessor);
};

// No framing, just a plain input->output copying.
class StreamCopyProcessor : public StreamProcessor {
 public:
  StreamCopyProcessor() {}
 private:
  // Processes data to be passed to the output.
  virtual
  bool Process(io::MemoryStream* output,
               io::MemoryStream* input,
               int32 output_capacity,
               bool& send_frame);
 private:
  DISALLOW_EVIL_CONSTRUCTORS(StreamCopyProcessor);
};

// Pack the output into frames, based on GStreamer data protocol on input.
class StreamFrameProcessor
    : public StreamProcessor {
 public:
  StreamFrameProcessor();

 private:
  // Processes data to be passed to the output.
  virtual  bool Process(io::MemoryStream* output,
                        io::MemoryStream* input,
                        int32 output_capacity,
                        bool& send_frame);

  // The current mime type.
  string mime_type_;

  streaming::ProtocolFrameFlags flags_;

  char header_[GST_DP_HEADER_LENGTH];

  GstDPPayloadType payload_type_;
  int32 payload_length_;

  enum GDPState {
    kGDPStateHeader,
    kGDPStatePayload,
    kGDPStateBuffer,
    kGDPStateCaps,
    kGDPStateEvent
  } state_;
 private:
  DISALLOW_EVIL_CONSTRUCTORS(StreamFrameProcessor);
};

}   // namespace media

#endif   // __STREAMING_STREAM_PROCESSOR_H__
