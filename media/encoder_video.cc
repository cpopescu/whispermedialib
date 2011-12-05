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

#include <list>
#include "media/encoder_video.h"

// our namespace
namespace media {

VideoEncoder::VideoEncoder(const char* name,
                           Encoder::Video::Type type,
                           int32 width,
                           int32 height,
                           int32 framerate_n,
                           int32 framerate_d,
                           uint32 bit_rate,
                           int32 gop_size,
                           bool strict_rc,
                           int32 pass,
                           const char *statsfile,
                           const char* parameters) :
  Component(name),
  m_type(type),
  m_width(width), m_height(height),
  m_framerate_n(framerate_n), m_framerate_d(framerate_d),
  m_bit_rate(bit_rate), m_gop_size(gop_size), m_strict_rc(strict_rc),
  m_pass(pass), m_statsfile((statsfile == NULL) ? "" : statsfile),
  m_parameters((parameters == NULL) ? "" : parameters) {
}

void VideoEncoder::Serialize(std::ostream& o) const {
  list<const char*> mime_types;

  switch (m_type) {
    case Encoder::Video::RGB:
      mime_types.push_back("video/x-raw-rgb,bpp=32");
      break;
    case Encoder::Video::YUV:
      mime_types.push_back("video/x-raw-yuv,format=I420");
      break;
    case Encoder::Video::JPEG:
      mime_types.push_back("video/x-raw-yuv,format=I420");
      break;
    case Encoder::Video::PNG:
      mime_types.push_back("video/x-raw-rgb,bpp=32,endianness=4321");
      mime_types.push_back("video/x-raw-rgb,bpp=24,endianness=4321");
      break;
    case Encoder::Video::VP6:
      mime_types.push_back("video/x-raw-yuv");
      mime_types.push_back("video/x-raw-rgb");
      mime_types.push_back("video/x-raw-gray");
      break;
    case Encoder::Video::H264:
      mime_types.push_back("video/x-raw-yuv,format=(fourcc)I420");
      break;
    default:
      LOG_FATAL << "VideoEncoder: Invalid video encoder type.";
  }

  o << "capsfilter caps=\"";
  for (list<const char*>::const_iterator mime_type = mime_types.begin();
       mime_type != mime_types.end(); mime_type++) {
    if (mime_type != mime_types.begin())
      o << "; ";

    o << *mime_type;
    if (m_width != 0) {
      switch (m_type) {
        case Encoder::Video::VP6:
          if (m_width < kFLVMinWidth || m_width > kFLVMaxWidth) {
            LOG_ERROR << "VideoEncoder: Invalid VP6 video width ("
                      << m_width << ").";
            o.setstate(ios::failbit);
            return;
          }
          break;
        case Encoder::Video::H264:
          if (m_width < kH264MinWidth) {
            LOG_ERROR << "VideoEncoder: Invalid H264 video width ("
                      << m_width << ").";
            o.setstate(ios::failbit);
            return;
          }
          break;

        case Encoder::Video::RGB:
        case Encoder::Video::YUV:
        case Encoder::Video::JPEG:
        case Encoder::Video::PNG:
          if (m_width < 1) {
            LOG_ERROR << "VideoEncoder: Invalid video width ("
                      << m_width << ").";
            o.setstate(ios::failbit);
            return;
          }
          break;

        default:
          LOG_FATAL << "VideoEncoder: Invalid video encoder type.";
      }
      o << ",width=" << m_width;
    }
    if (m_height != 0) {
      switch (m_type) {
        case Encoder::Video::VP6:
          if (m_height < kFLVMinHeight || m_height > kFLVMaxHeight) {
            LOG_ERROR << "VideoEncoder: Invalid VP6 video height ("
                      << m_height << ").";
            o.setstate(ios::failbit);
            return;
          }
          break;
        case Encoder::Video::H264:
          if (m_height < kH264MinHeight) {
            LOG_ERROR << "VideoEncoder: Invalid H264 video height ("
                      << m_height << ").";
            o.setstate(ios::failbit);
            return;
          }
          break;

        case Encoder::Video::RGB:
        case Encoder::Video::YUV:
        case Encoder::Video::JPEG:
        case Encoder::Video::PNG:
          if (m_height < 1) {
            LOG_ERROR << "VideoEncoder: Invalid video height ("
                      << m_height << ").";
            o.setstate(ios::failbit);
            return;
          }
          break;

        default:
          LOG_FATAL << "VideoEncoder: Invalid video encoder type.";
      }
      o << ",height=" << m_height;
    }
    if (m_framerate_n != 0 && m_framerate_d != 0) {
      if (m_framerate_n < 0 || m_framerate_d < 0) {
        LOG_ERROR << "VideoEncoder: Invalid framerate ("
                  << m_framerate_n << "/" << m_framerate_d << ").";
        o.setstate(ios::failbit);
        return;
      }
      o << ",framerate=" << m_framerate_n << "/" << m_framerate_d;
    }
  }
  o << "\" ! ";

  switch (m_type) {
    case Encoder::Video::VP6:
      o << "ffenc_flv";
      if (m_strict_rc) {
        o << "pre-me=all flags=0x03000000  ";
        if (!m_name.empty()) {
          o << " name=\"" << m_name << "\"";
        }
        if (m_bit_rate != 0) {
          o << " bitrate=" << m_bit_rate;
          o << " rc-buffer-size="
            << ((m_gop_size != 0) ? m_gop_size*m_bit_rate : 30*m_bit_rate);
          o << " rc-min-rate=" << m_bit_rate;
          o << " rc-max-rate=" << m_bit_rate;
        }
        if (m_gop_size != 0) {
          if (m_gop_size < 0) {
            LOG_ERROR << "VideoEncoder: Invalid VP6 GOP size ("
                      << m_gop_size << ").";
            o.setstate(ios::failbit);
            return;
          }
          o << " gop-size=" << m_gop_size;
        }
      } else {
        if (!m_name.empty()) {
          o << " name=\"" << m_name << "\"";
        }
        if (m_bit_rate != 0) {
          o << " bitrate=" << m_bit_rate;
        }
        if (m_gop_size != 0) {
          if (m_gop_size < 0) {
            LOG_ERROR << "VideoEncoder: Invalid VP6 GOP size ("
                      << m_gop_size << ").";
            o.setstate(ios::failbit);
            return;
          }
          o << " gop-size=" << m_gop_size;
        }
      }
      if (m_pass > 0) {
        if (m_pass < kFLVMinPass || m_pass > kFLVMaxPass) {
          LOG_ERROR << "VideoEncoder: Invalid pass ("
                    << m_pass << ").";
          o.setstate(ios::failbit);
          return;
        }
        if (m_statsfile.empty()) {
          LOG_ERROR << "VideoEncoder: A multipass encoding needs a stats file.";
          o.setstate(ios::failbit);
          return;
        }
        o << " pass=" << (512*m_pass) <<
          " statsfile=\"" << m_statsfile << "\"";
      }
      o << " " << m_parameters;
      break;
    case Encoder::Video::H264:
      o << "x264enc";
      if (!m_name.empty()) {
        o << " name=\"" << m_name << "\"";
      }
      if (m_bit_rate != 0) {
        o << " bitrate=" << m_bit_rate/1000;
      }
      if (m_gop_size != 0) {
        if (m_gop_size < 0) {
          LOG_ERROR << "VideoEncoder: Invalid H264 GOP size ("
                    << m_gop_size << ").";
          o.setstate(ios::failbit);
          return;
        }
        o << " key-int-max=" << m_gop_size;
      }
      if (m_pass > 0) {
        if (m_pass < kH264MinPass || m_pass > kH264MaxPass) {
          LOG_ERROR << "VideoEncoder: Invalid pass ("
                    << m_pass << ").";
          o.setstate(ios::failbit);
          return;
        }
        if (m_statsfile.empty()) {
          LOG_ERROR << "VideoEncoder: A multipass encoding needs a stats file.";
          o.setstate(ios::failbit);
          return;
        }
        o << " pass=" << (512*m_pass) <<
          " stats-file=\"" << m_statsfile << "\"";
      } else {
        o << " trellis=false ";
      }

      // o << " cabac=false me=umh subme=6 ";
      // 0x00000002 | 0x00000010 | 0x00000100
      // o << " cabac=false me=umh subme=8 analyse=274 ";

      o << " cabac=false me=umh subme=3 ";
      o << " " << m_parameters;
      break;

    case Encoder::Video::RGB:
    case Encoder::Video::YUV:
      o << "identity";
      if (!m_name.empty()) {
        o << " name=\"" << m_name << "\"";
      }
      break;

    case Encoder::Video::JPEG:
      o << "jpegenc";
      if (!m_name.empty()) {
        o << " name=\"" << m_name << "\"";
      }
      break;

    case Encoder::Video::PNG:
      o << "pngenc";
      if (!m_name.empty()) {
        o << " name=\"" << m_name << "\"";
      }
      break;

    default:
      LOG_FATAL << "VideoEncoder: Invalid video encoder type.";
  }
}
}
