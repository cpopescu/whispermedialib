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

#ifndef __STREAMING_STREAM_PROVIDER_H__
#define __STREAMING_STREAM_PROVIDER_H__

#include <map>
#include <vector>
#include <string>
#include <whisperlib/common/base/types.h>
#include WHISPER_HASH_MAP_HEADER

#include <whisperlib/common/base/parse.h>
#include <whisperlib/net/base/selector.h>

#include <whisperlib/common/io/checkpoint/state_keeper.h>
#include <whispermedialib/media/pipeline.h>
#include <whispermedialib/media/pipeline_runner.h>
#include <whispermedialib/media/controller.h>
#include <whispermedialib/media/encoder_audio.h>
#include <whispermedialib/media/encoder_video.h>

#include <whisperlib/net/rpc/lib/types/rpc_all_types.h>
#include <whispermedialib/media/auto/types.h>
#include <whispermedialib/media/auto/invokers.h>

// our namespace
namespace media {

// The pipeline runner that can handle a set of output encoders/muxers
// for each stream it serves.
class StreamProvider
    : public PipelineRunner,
      public ServiceInvokerController {
 public:
  typedef map<string, media::Controller*> ControllerMap;
  // The error codes.
  enum Result {
    NoError = 0,

    PipelineParametersError,
    PipelineCreationError,
    PipelineLinkingError,

    PipeCreationError,

    SourceUnavailableError,

    UnknownError
  };

  StreamProvider(const char* name,
                 const char* source_pipeline_description,
                 int audio_ports,
                 int video_ports,
                 const char* source_mime_type,
                 int source_fireup_timeout,
                 int encoder_fireup_timeout,
                 int source_idle_timeout,
                 int encoder_idle_timeout,
                 int max_lag,
                 const vector<string>* controlled_elements,
                 io::StateKeeper* state_keeper);
  virtual ~StreamProvider();

  // Returns the source's pipeline description.
  const string& source_pipeline_description() const {
    return source_pipeline_description_;
  }
  // Returns the source's mime-type.
  const string& source_mime_type() const {
    return source_mime_type_;
  }

  // Returns the source fireup timeout.
  int source_fireup_timeout() const {
    return source_fireup_timeout_;
  }
  // Returns the encoder fireup timeout.
  int encoder_fireup_timeout() const {
    return encoder_fireup_timeout_;
  }
  // Returns the source idle timeout.
  int source_idle_timeout() const {
    return source_idle_timeout_;
  }
  // Returns the encoder idle timeout.
  int encoder_idle_timeout() const {
    return encoder_idle_timeout_;
  }

  // Returns the maximum acceptable lag for this provider.
  int max_lag() const {
    return max_lag_;
  }

  // Returns the controlled elements map.
  const ControllerMap& controllers() const {
    return controllers_;
  }

  // Initializes the provider.
  void Initialize(net::Selector* selector) {
    CHECK(selector_ == NULL);
    selector_ = selector;
  }

  // Creates the source pipeline.
  virtual
  Result CreateSource();

  // Creates an encoder pipeline linked to the source pipeline.
  //
  // If wrapped is true, the output will be GDP encoded,
  // so it can be wrapped into our own internal media
  // encoding protocol.
  //
  // If successful, returns the fd that can be read
  // in order to get the encoded data.
  virtual
  Result CreateEncoder(Stream::Type type,
                       const Encoder::Audio::Params* const audio_params,
                       const Encoder::Video::Params* const video_params,
                       int audio_port,
                       int video_port,
                       bool wrapped,
                       int& fd,
                       const char*& mime_type);

  // Creates a relay pipeline linked to the source pipeline.
  //
  // A relay is just exposing whatever the source is providing,
  // without any reencoding and stuff.
  //
  // If wrapped is true, the output will be GDP encoded,
  // so it can be wrapped into our own internal media
  // encoding protocol.
  //
  // If successful, returns the fd that can be read
  // in order to get the encoded data.
  virtual Result CreateRelay(bool wrapped, int& fd);

  // Sets the callback that is to be called when a new message is posted
  // to the pipeline runner's message queue.
  //
  // This is needed to enable the running of the pipeline runner
  // in the context of the HTTP selector thread.
  void SetMessageQueueCallback(
    Callback1<media::Message*>* post_message_callback) {
    m_message_queue->SetPostMessageCallback(post_message_callback);
  }

  // Save the associated controllers' state.
  void SaveState();
  // Delete the associated controllers' state.
  void DeleteState();

  // RPCServiceInvokerController implementation.
  virtual void GetControllableParameters(
    rpc::CallContext< map< int32, ControllableParameter > >* call,
    const string& element);
  virtual void GetRangeValue(
    rpc::CallContext< int32 >* call,
    const string& element, int32 id);
  virtual void GetRangeValues(
    rpc::CallContext< map<int32, int32> >* call,
    const string& element, const vector<int32>& ids);
  virtual void SetRangeValue(
    rpc::CallContext<bool>* call,
    const string& element, int32 id, int32 value);
  virtual void SetRangeValues(
    rpc::CallContext< map<int32, bool> >* call,
    const string& element, const map< int32, int32 >& values);

  virtual void GetListValue(
    rpc::CallContext< string >* call,
    const string& element, int32 id);
  virtual void GetListValues(
    rpc::CallContext< map<int32, string> >* call,
    const string& element, const vector<int32>& ids);
  virtual void SetListValue(
    rpc::CallContext<bool>* call,
    const string& element, int32 id, const string& value);
  virtual void SetListValues(
    rpc::CallContext< map<int32, bool> >* call,
    const string& element, const map< int32, string >& values);

 protected:
  // The associated selector.
  net::Selector* selector_;

  // The pipeline message callback.
  Callback2<Pipeline*, Pipeline::Message::Kind>* message_callback_;

  // The source pipeline description.
  string source_pipeline_description_;
  // The number of audio ports in the source pipeline.
  int audio_ports_;
  // The number of video ports in the source pipeline.
  int video_ports_;
  // The source mime type.
  string source_mime_type_;

  // The source fireup timeout, in miliseconds.
  int source_fireup_timeout_;
  // The encoder fireup timeout, in miliseconds.
  int encoder_fireup_timeout_;

  // The source idle timeout (-1 for no timeout, 0 for instant stop).
  int source_idle_timeout_;
  // The encoder idle timeout (-1 for no timeout, 0 for instant stop).
  int encoder_idle_timeout_;

  // The maximum acceptable lag, in bytes.
  int max_lag_;

  // The source pipeline.
  media::Pipeline* source_;

  // The mapping of pipelines to timeout closures.
  typedef hash_map<media::Pipeline*, Closure*> ClosureMap;
  ClosureMap timeout_closures_;

  // The controllers map (element name->controller)
  ControllerMap controllers_;
  // The controlled elements' state
  io::StateKeepUser* controllers_state_keeper_;

  // Since the default (by design and correct) behaviour of GStreamer
  // is to run a muxer (or any GstCollectPads derived object) until ALL
  // the input pads are in EOS, we have a big problem.
  //
  // What we have here is the following generic structure used for
  // serving streams:
  //
  //           |----> video_encoder --->|
  //   source -|                        | ---> muxer ---> HTTP
  //           |----> audio_encoder --->|
  //
  // where source, audio_encoder, etc are pipelines.
  //
  // If one of the encoding pipelines (audio_encoder, video_encoder)
  // dies for any reason, the muxer will still happily run until
  // the other encoder dies too. What we actually want is that if any
  // error occurs in any of the involved pipelines, we shut down the muxer
  // and terminate this specific encoder/muxer.
  //
  // Therefore we will store a mapping of encoders to muxers,
  // and if any of the encoders dies, we terminate the muxer, too - this, in
  // turn will also trigger the termination of the other encoder.
  //
  typedef multimap<media::Pipeline*,
                   media::Pipeline*> PipelineMultimap;
  PipelineMultimap muxer_to_encoder_;
  PipelineMultimap encoder_to_muxer_;

  // Registers the given pipeline as a data source.
  //
  // On failure, it cleans up and destroys the given pipeline.
  //
  // Returns a fd that can be used to read the encoded data
  // out of the pipeline.
  Result RegisterDataPipeline(media::Pipeline* pipeline,
                              const char* multifdsink_name, int* fd);

  // Registers the pipeline timeout alarm.
  void RegisterPipelineAlarm(media::Pipeline* pipeline, int timeout);
  // Unregisters the pipeline timeout alarm.
  void UnregisterPipelineAlarm(media::Pipeline* pipeline,
                               bool delete_closure);

  // Processes a pipeline timeout.
  virtual
  void PipelineTimeout(media::Pipeline* pipeline);
  // Processes a pipeline stop.
  virtual
  void PipelineStop(media::Pipeline* pipeline);

  // Processes a message from the pipeline runner's message queue.
  void PipelineMessageCallback(Pipeline* pipeline,
                               Pipeline::Message::Kind message);

  // The currently running audio encoders.
  typedef map<pair<int, Encoder::Audio::Params>, media::Pipeline*>
  AudioEncodersMap;
  AudioEncodersMap audio_encoders_;

  typedef map<media::Pipeline*, AudioEncodersMap::iterator>
  AudioEncodersMapReversed;
  AudioEncodersMapReversed audio_encoders_reversed_;

  // The currently running video encoders.
  typedef map<pair<int, Encoder::Video::Params>, media::Pipeline*> VideoEncodersMap;
  VideoEncodersMap video_encoders_;

  typedef map<media::Pipeline*, VideoEncodersMap::iterator>
  VideoEncodersMapReversed;
  VideoEncodersMapReversed video_encoders_reversed_;

  // Creates an audio encoder based on the given parameters.
  Result CreateAudioEncoder(const Encoder::Audio::Params& params,  // in
                            int audio_port,
                            bool& new_encoder,                     // out
                            const char*& mime_type,
                            media::Pipeline*& pipeline);
  // Creates a video encoder based on the given parameters.
  Result CreateVideoEncoder(const Encoder::Video::Params& params,  // in
                            int video_port,
                            bool& new_encoder,                     // out
                            const char*& mime_type,
                            media::Pipeline*& pipeline);

  // Removes an encoder from the encoders cache.
  void RemoveEncoderFromCache(media::Pipeline* pipeline);
};
}   // namespace media

#endif  // __STREAMING_STREAM_PROVIDER_H__
