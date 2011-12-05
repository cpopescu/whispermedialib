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

#include <gst/gst.h>
#include <whisperlib/common/io/buffer/memory_stream.h>
#include <whisperlib/common/io/num_streaming.h>

#include "media/stream_processor.h"



// Private GStreamer stuff, from dp-private.h
#define GST_DP_HEADER_PAYLOAD_TYPE(x)   GST_READ_UINT16_BE(x + 4)
#define GST_DP_HEADER_PAYLOAD_LENGTH(x) GST_READ_UINT32_BE(x + 6)
#define GST_DP_HEADER_TIMESTAMP(x)      GST_READ_UINT64_BE(x + 10)
#define GST_DP_HEADER_DURATION(x)       GST_READ_UINT64_BE(x + 18)
#define GST_DP_HEADER_OFFSET(x)         GST_READ_UINT64_BE(x + 26)
#define GST_DP_HEADER_OFFSET_END(x)     GST_READ_UINT64_BE(x + 34)
#define GST_DP_HEADER_BUFFER_FLAGS(x)   GST_READ_UINT16_BE(x + 42)
#define GST_DP_HEADER_CRC_HEADER(x)     GST_READ_UINT16_BE(x + 58)
#define GST_DP_HEADER_CRC_PAYLOAD(x)    GST_READ_UINT16_BE(x + 60)

// our namespace
namespace media {

bool StreamCopyProcessor::Process(
    io::MemoryStream* output,
    io::MemoryStream* input,
    int32 output_capacity,
    bool& send_frame) {
  int32 available_in;
  available_in = input->Size();

  int32 to_copy = output_capacity;
  if (available_in < to_copy) {
    to_copy = available_in;
  }

  output->AppendStreamNonDestructive(input, to_copy);
  input->Skip(to_copy);

  send_frame = (to_copy > 0);
  return true;
}

StreamFrameProcessor::StreamFrameProcessor()
    : flags_(0),
      payload_type_(GST_DP_PAYLOAD_NONE),
      payload_length_(0),
      state_(kGDPStateHeader) {
}

static
int32 WriteFrameHeader(io::MemoryStream* output,
                       streaming::ProtocolFrameFlags flags,
                       streaming::ProtocolFrameLength length,
                       streaming::ProtocolFrameTimeStamp timestamp,
                       const char* mime_type) {
  int32 written = 0;
  // FLAGS, LENGTH
  written += io::NumStreamer::WriteNumber(
      output, flags, streaming::kProtocolByteOrder);
  written += io::NumStreamer::WriteNumber(
      output, length, streaming::kProtocolByteOrder);

  if ((flags & streaming::HAS_TIMESTAMP) != 0) {
    // TIMESTAMP
    written += io::NumStreamer::WriteNumber(
        output, timestamp, streaming::kProtocolByteOrder);
  }
  if ((flags & streaming::HAS_MIME_TYPE) != 0) {
    // MIME-TYPE, with the size of the string encoded on one byte
    int32 length = strlen(mime_type);
    written += io::NumStreamer::WriteNumber(
        output, (uint8)length, streaming::kProtocolByteOrder);
    written += output->Write(mime_type, length);
  }

  DLOG(10) << strutil::StringPrintf(
      "FRAME PROCESSED {%c%c%c%c%c%c%c%c : %08d, %08d, %s}",
      flags & streaming::HEADER ? 'H' : '-',
      flags & streaming::AUDIO ? 'A' : '-',
      flags & streaming::VIDEO ? 'V' : '-',
      flags & streaming::METADATA ? 'M' : '-',
      flags & streaming::DELTA ? 'D' : '-',
      flags & streaming::DISCONTINUITY ? 'X' : '-',
      flags & streaming::HAS_TIMESTAMP ? 'T' : '-',
      flags & streaming::HAS_MIME_TYPE ? 'M' : '-',
      length,
      timestamp,
      mime_type);

  return written;
}

bool StreamFrameProcessor::Process(
    io::MemoryStream* output,
    io::MemoryStream* input,
    int32 output_capacity,
    bool& send_frame) {
  send_frame = false;
  while (output_capacity > 0) {
    switch (state_) {
      case kGDPStateHeader: {
        // if a complete header is not yet available, wait for more
        if (input->Size() < GST_DP_HEADER_LENGTH) {
          return true;
        }

        // get the header
        CHECK(input->Read(header_, GST_DP_HEADER_LENGTH) ==
              GST_DP_HEADER_LENGTH);
        if (!gst_dp_validate_header(GST_DP_HEADER_LENGTH,
                                    (const guint8*)header_)) {
          // failed the header validation
          return false;
        }

        // store the header and the payload info
        payload_length_ = gst_dp_header_payload_length((const guint8*)header_);
        payload_type_ = gst_dp_header_payload_type((const guint8*)header_);

        // move on
        state_ = kGDPStatePayload;
      }
        // fall through
      case kGDPStatePayload: {
        // move on, based on payload type
        if (payload_type_ == GST_DP_PAYLOAD_BUFFER) {
          int16 dp_flags = GST_DP_HEADER_BUFFER_FLAGS((const guint8*)header_);
          GstClockTime dp_timestamp = GST_DP_HEADER_TIMESTAMP(
              (const guint8*)header_);

          // we start with the current flags, as we can deduce the type of
          // the frame from the caps, in some cases
          streaming::ProtocolFrameFlags flags = flags_;

          flags |=
              (((dp_flags & GST_BUFFER_FLAG_DISCONT) != 0)
               ? streaming::DISCONTINUITY : 0) |
              (((dp_flags & GST_BUFFER_FLAG_DELTA_UNIT) != 0)
               ? streaming::DELTA : 0) |
              (((dp_flags & GST_BUFFER_FLAG_IN_CAPS) != 0)
               ? streaming::HEADER : 0);

          if (dp_timestamp != GST_CLOCK_TIME_NONE) {
            // we will write out the frame header, so we need enough space
            if (output_capacity <
                sizeof(streaming::ProtocolFrameHeader) +
                ((flags & streaming::HAS_MIME_TYPE) != 0) ? mime_type_.size() : 0) {
              return true;
            }

            // we have timestamps
            flags |= streaming::HAS_TIMESTAMP;
          } else {
            // we will write out the frame header, so we need enough space
            if (output_capacity <
                (sizeof(streaming::ProtocolFrameHeader) +
                 ((flags & streaming::HAS_MIME_TYPE) != 0) ?
                 mime_type_.size()
                 : 0 - sizeof(streaming::ProtocolFrameTimeStamp))) {
              return true;
            }
          }

          WriteFrameHeader(
              output, flags, payload_length_,
              (streaming::ProtocolFrameTimeStamp)(dp_timestamp/GST_MSECOND),
              mime_type_.c_str());
          flags_ &= ~streaming::HAS_MIME_TYPE;

          state_ = kGDPStateBuffer;
        } else if (payload_type_ == GST_DP_PAYLOAD_CAPS) {
          state_ = kGDPStateCaps;
        } else if (payload_type_ >= GST_DP_PAYLOAD_EVENT_NONE) {
            state_ = kGDPStateEvent;
          } else {
            // invalid payload type
            return false;
          }
      }
        break;
      case kGDPStateBuffer:
        // if the complete payload is not yet available, wait for more
        if (input->Size() < payload_length_) {
          return true;
        }

        // copy the payload to the output
        if (payload_length_ > 0) {
          int32 to_copy = payload_length_;
          if (output_capacity < to_copy)
            to_copy = output_capacity;

          output->AppendStreamNonDestructive(input, to_copy);
          input->Skip(to_copy);

          output_capacity -= to_copy;
          payload_length_ -= to_copy;
        }
        state_ = kGDPStateHeader;

        send_frame = true;
        return true;

      case kGDPStateCaps:
        {
          // if the complete payload is not yet available, wait for more
          if (input->Size() < payload_length_) {
            return true;
          }

          // read the actual payload
          char* payload = new char[payload_length_];
          CHECK(input->Read(payload, payload_length_) == payload_length_);
          // get the actual caps, as we might need them to deduce the type
          // of the stream
          if (gst_dp_validate_packet(GST_DP_HEADER_LENGTH,
                                     (const guint8*) header_,
                                     (const guint8*) payload)) {
            GstCaps* caps = gst_dp_caps_from_packet(
                GST_DP_HEADER_LENGTH,
                reinterpret_cast<const guint8*>(header_),
                reinterpret_cast<const guint8*>(payload));
            if (caps != NULL) {
              GstStructure* structure = gst_caps_get_structure(caps, 0);
              if ( structure != NULL ) {
                mime_type_ = gst_structure_get_name(structure);
                // force the writing of a mime type on the next frame
                flags_ |= streaming::HAS_MIME_TYPE;

                // check for audio
                if (strutil::StrCasePrefix(mime_type_.c_str(), "audio/")) {
                  // audio/mpeg can be either AAC or MP3
                  if (mime_type_ == "audio/mpeg") {
                    gint mpegversion;
                    if (gst_structure_get_int(
                            structure, "mpegversion", &mpegversion)) {
                      switch (mpegversion) {
                        case 4:
                        case 2:
                          mime_type_ = "audio/aac";
                          break;
                        case 1:
                          mime_type_ = "audio/mpeg";
                          break;
                        default:
                          break;   // leave it as-is
                      }
                    }
                  }
                  flags_ = (flags_ & ~(streaming::AUDIO |
                                       streaming::VIDEO |
                                       streaming::METADATA)) | streaming::AUDIO;
                } else {
                  // check for video...
                  if (strutil::StrCasePrefix(mime_type_.c_str(), "video/")) {
                    // ..but don't classify video/flv as video
                    if (strutil::StrIEql(mime_type_.c_str(), "video/x-flv")) {
                      flags_ = flags_ & ~(streaming::AUDIO |
                                          streaming::VIDEO |
                                          streaming::METADATA);
                    } else {
                      flags_ = (flags_ & ~(streaming::AUDIO |
                                           streaming::VIDEO |
                                           streaming::METADATA)) | streaming::VIDEO;
                    }
                  } else {
                    flags_ = flags_ & ~(streaming::AUDIO |
                                        streaming::VIDEO |
                                        streaming::METADATA);
                  }
                }
              }
              gst_caps_unref(caps);
            }
          }
          delete [] payload;
        }
        state_ = kGDPStateHeader;
        break;
      case kGDPStateEvent:
        // we're just skipping the payload
        input->Skip(payload_length_);
        state_ = kGDPStateHeader;
        break;
    }
  }
  return true;
}
}
