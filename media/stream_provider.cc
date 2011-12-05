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

#include <whisperlib/common/base/errno.h>
#include <whisperlib/common/base/gflags.h>

#include "media/stream_provider.h"
#include "media/adapter_audio.h"
#include "media/adapter_video.h"
#include "media/sink_multifd.h"

//////////////////////////////////////////////////////////////////////

DEFINE_int32(source_fireup_timeout,
             5000,
             "The default source fireup timeout");
DEFINE_int32(encoder_fireup_timeout,
             5000,
             "The default encoder fireup timeout");
DEFINE_int32(source_idle_timeout,
             10000,
             "The default source idle timeout");
DEFINE_int32(encoder_idle_timeout,
             0,
             "The default encoder idle timeout");
DEFINE_int32(provider_link_soft_limit,
             2000,
             "The lag, in miliseconds, after a multifdsink client in "
             "the stream provider will be resynced");
DEFINE_int32(provider_link_hard_limit,
             5000,
             "The lag, in miliseconds, after a multifdsink client in "
             "the stream provider will be disconnected");

//////////////////////////////////////////////////////////////////////

// this flags is set on muxer pipelines
#define kIsMuxer  0x0001

// our namespace
namespace media {

StreamProvider::StreamProvider(
    const char* name,
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
    io::StateKeeper* state_keeper)
    : PipelineRunner(name),
      ServiceInvokerController(ServiceInvokerController::GetClassName()),
      selector_(NULL),
      source_pipeline_description_(source_pipeline_description),
      audio_ports_(audio_ports),
      video_ports_(video_ports),
      source_mime_type_(source_mime_type),
      source_fireup_timeout_(source_fireup_timeout),
      encoder_fireup_timeout_(encoder_fireup_timeout),
      source_idle_timeout_(source_idle_timeout),
      encoder_idle_timeout_(encoder_idle_timeout),
      max_lag_(max_lag),
      source_(NULL) {
  // create the pipeline message callback
  message_callback_ = NewPermanentCallback(
      this, &StreamProvider::PipelineMessageCallback);

  // initialize the controllers and controllers' defaults
  if (controlled_elements != NULL) {
    for (vector<string>::const_iterator controller =
             controlled_elements->begin();
         controller != controlled_elements->end(); ++controller) {
      controllers_[*controller] = NULL;
    }
  }
  // create the state keepr, if it's the case
  if (state_keeper) {
    controllers_state_keeper_ =
        new io::StateKeepUser(state_keeper,
                              string("controller/") + name + "/", 0);
  } else {
    controllers_state_keeper_ = NULL;
  }
}
StreamProvider::~StreamProvider() {
  delete message_callback_;
}

StreamProvider::Result StreamProvider::CreateSource() {
  if (source_ != NULL) {
    return NoError;
  }

  // IMPORTANT: the position of << operators is extremely important !
  //            Don't change unless you know what you do !
  PipelineDescription source_description;
  source_description << source_pipeline_description_.c_str();
  for (int audio_port = 0; audio_port < audio_ports_; ++audio_port) {
      source_description
          << PipelineDescription::end
          << PipelineDescription::ref(
              strutil::StringPrintf("source_audio%d", audio_port).c_str())
          << MultiFDSink(
              strutil::StringPrintf("source_audio_mfds%d", audio_port).c_str(),
              true,
              FLAGS_provider_link_hard_limit,
              FLAGS_provider_link_soft_limit,
              -1,
              GDP, MultiFDSink::Latest);
  }
  for (int video_port = 0; video_port < video_ports_; ++video_port) {
      source_description
          << PipelineDescription::end
          << PipelineDescription::ref(
              strutil::StringPrintf("source_video%d", video_port).c_str())
          << MultiFDSink(
              strutil::StringPrintf("source_video_mfds%d", video_port).c_str(),
              true,
              FLAGS_provider_link_hard_limit,
              FLAGS_provider_link_soft_limit,
              -1,
              GDP, MultiFDSink::Latest);
  }

  LOG_INFO << "Creating the source based on:\n"
           << source_description.description();

  source_ = new Pipeline((m_name + " - Source").c_str());
  if (!source_->Create(source_description)) {
    delete source_;
    source_ = NULL;

    LOG_ERROR << "Couldn't create the source pipeline based on:\n"
              << source_pipeline_description_;
    return PipelineCreationError;
  }
  AddPipeline(source_,
              PipelineParams(
                  0,
                  (source_fireup_timeout_ == Controller::kInvalidValue)
                  ? FLAGS_source_fireup_timeout : source_fireup_timeout_,
                  message_callback_));

  // create the controllers
  for ( ControllerMap::iterator controller = controllers_.begin();
        controller != controllers_.end(); ++controller ) {
    Controller* const element_controller =
        source_->CreateController(controller->first.c_str());
    if (element_controller != NULL) {
      controller->second = element_controller;
      CHECK(source_->AddElementHandler(element_controller,
                                       controller->first.c_str()));
    }
  }

  SyncPipeline(source_);
  return NoError;
}

StreamProvider::Result StreamProvider::CreateEncoder(
    Stream::Type type,
    const Encoder::Audio::Params* const _audio_params,
    const Encoder::Video::Params* const _video_params,
    int audio_port, int video_port,
    bool wrapped, int& fd, const char*& mime_type) {
  fd = INVALID_FD_VALUE;
  if (source_ == NULL) {
    return SourceUnavailableError;
  }

  Result result;

  Encoder::Audio::Params audio_params;
  if (_audio_params) {
    audio_params = *_audio_params;
  }
  Encoder::Video::Params video_params;
  if (_video_params) {
    video_params = *_video_params;
  }

  // for MP3/AAC requests, the server is the actual encoder
  if (type == Stream::MP3 || type == Stream::AAC) {
    const Encoder::Audio::Type audio_type =
        (type == Stream::MP3) ? Encoder::Audio::MP3 : Encoder::Audio::AAC;
    audio_params.type_ = audio_type;

    Pipeline* encoder = NULL;
    bool new_encoder = false;
    if ((result = CreateAudioEncoder(audio_params,
                                     audio_port,
                                     new_encoder,
                                     mime_type,
                                     encoder)) != NoError) {
      return result;
    }

    if ( (result = RegisterDataPipeline(
              encoder,
              wrapped ? "encoder_audio_sink_gdp" : "encoder_audio_sink",
              &fd)) != NoError ) {
      if (new_encoder) {
        RemoveEncoderFromCache(encoder);
        RemovePipeline(encoder);

        encoder->Destroy();
        delete encoder;
      }
      return result;
    }

    if (new_encoder) {
      SyncPipeline(encoder);
    }
    return NoError;
  }

  // for FLV requests, the server is a muxer that multiplexes the output of
  // distinct audio/video encoders
  if (type == Stream::FLV) {
    Pipeline* audio_encoder = NULL;
    bool new_audio_encoder = false;
    const char* audio_mime_type;

    Pipeline* video_encoder = NULL;
    bool new_video_encoder = false;
    const char* video_mime_type;

    Pipeline* muxer = NULL;
    PipelineDescription muxer_description;

    // FLV needs raw AAC...
    audio_params.raw_ = true;
    if ( (result = CreateAudioEncoder(audio_params,
                                      audio_port,
                                      new_audio_encoder,
                                      audio_mime_type,
                                      audio_encoder)) != NoError ) {
      goto FLV_Failed;
    }
    if ( (result = CreateVideoEncoder(video_params,
                                      video_port,
                                      new_video_encoder,
                                      video_mime_type,
                                      video_encoder)) != NoError ) {
      goto FLV_Failed;
    }

    ////////////////////////////////////////
    //
    // IMPORTANT: the position of << operators is extremely important !
    //            Don't change unless you know what you do !
    //
    muxer_description <<
        "ffmux_flv name=muxer ! video/x-flv";

    if (wrapped)
      muxer_description <<
          MultiFDSink(
              "muxer_sink",
              false,
              FLAGS_provider_link_hard_limit,
              FLAGS_provider_link_soft_limit,
              -1,
              GDP, MultiFDSink::Latest);
    else
      muxer_description <<
          MultiFDSink(
              "muxer_sink",
              false,
              FLAGS_provider_link_hard_limit,
              FLAGS_provider_link_soft_limit,
              -1,
              NoProtocol, MultiFDSink::Latest);

    muxer_description << PipelineDescription::end;

    if (audio_encoder != NULL) {
      muxer_description << "fdsrc name=muxer_audio_source ! gdpdepay"
                        << PipelineDescription::ref("muxer", "audio_%d")
                        << PipelineDescription::end;
    }
    if (video_encoder != NULL) {
      muxer_description << "fdsrc name=muxer_video_source ! gdpdepay"
                        << PipelineDescription::ref("muxer", "video_%d")
                        << PipelineDescription::end;
    }

    muxer = new Pipeline((m_name + " - Muxer").c_str(), kIsMuxer);
    LOG_INFO << "Creating a muxer based on:\n"
             << muxer_description.description();
    if (!muxer->Create(muxer_description)) {
      delete muxer;
      muxer = NULL;

      result = PipelineCreationError;
      goto FLV_Failed;
    }
    AddPipeline(muxer,
                PipelineParams(0,
                               (encoder_fireup_timeout_ == INT_MIN)
                               ? FLAGS_encoder_fireup_timeout
                               : encoder_fireup_timeout_,
                               message_callback_));
    if ( audio_encoder != NULL ) {
      if ( !muxer->Link("muxer_audio_source",
                        audio_encoder,
                        "encoder_audio_sink_gdp") ) {
        result = PipelineLinkingError;
        goto FLV_Failed;
      }
    }
    if ( video_encoder != NULL ) {
      if ( !muxer->Link("muxer_video_source",
                        video_encoder,
                        "encoder_video_sink_gdp") ) {
        result = PipelineLinkingError;
        goto FLV_Failed;
      }
    }

    if ((result = RegisterDataPipeline(muxer, "muxer_sink", &fd)) != NoError) {
      goto FLV_Failed;
    }

    // return the mime type
    mime_type = "video/x-flv",

                source_->AddSlavePipeline(muxer);

    // insert into encoder<->muxer maps
    if (audio_encoder != NULL) {
      encoder_to_muxer_.insert(make_pair(audio_encoder, muxer));
      muxer_to_encoder_.insert(make_pair(muxer, audio_encoder));
    }
    if (video_encoder != NULL) {
      encoder_to_muxer_.insert(make_pair(video_encoder, muxer));
      muxer_to_encoder_.insert(make_pair(muxer, video_encoder));
    }

    // these should never fail
    if (new_audio_encoder && (audio_encoder != NULL)) {
      SyncPipeline(audio_encoder);
    }
    if (new_video_encoder && (video_encoder != NULL)) {
      SyncPipeline(video_encoder);
    }
    SyncPipeline(muxer);

    return NoError;

 FLV_Failed:
    if (audio_encoder != NULL) {
      if (new_audio_encoder) {
        RemoveEncoderFromCache(audio_encoder);
        RemovePipeline(audio_encoder);

        audio_encoder->Destroy();
        delete audio_encoder;
      }
    }
    if (video_encoder != NULL) {
      if (new_video_encoder) {
        RemoveEncoderFromCache(video_encoder);
        RemovePipeline(video_encoder);

        video_encoder->Destroy();
        delete video_encoder;
      }
    }
    if (muxer != NULL) {
      RemovePipeline(muxer);

      muxer->Destroy();
      delete muxer;
    }
    return result;
  }

  CHECK(false);
  return UnknownError;
}

StreamProvider::Result StreamProvider::CreateRelay(bool wrapped, int& fd) {
  fd = INVALID_FD_VALUE;
  if (source_ == NULL) {
    return SourceUnavailableError;
  }

  Result result = NoError;

  Pipeline* relay = NULL;
  PipelineDescription relay_description;

  ////////////////////////////////////////
  //
  // IMPORTANT: the position of << operators is extremely important !
  //            Don't change unless you know what you do !
  //

  relay_description <<
      "fdsrc name=relay_source ! gdpdepay";

  if (wrapped)
    relay_description <<
        MultiFDSink(
            "relay_sink",
            false,
            FLAGS_provider_link_hard_limit,
            FLAGS_provider_link_soft_limit,
            -1,
            GDP, MultiFDSink::LatestKeyframe);
  else
    relay_description <<
        MultiFDSink(
            "relay_sink",
            false,
            FLAGS_provider_link_hard_limit,
            FLAGS_provider_link_soft_limit,
            -1,
            NoProtocol, MultiFDSink::LatestKeyframe);

  relay = new Pipeline((m_name + " - Relay").c_str());
  if (!relay->Create(relay_description)) {
    delete relay;
    relay = NULL;

    result = PipelineCreationError;
  } else {
    AddPipeline(relay,
                PipelineParams(0,
                               (encoder_fireup_timeout_ == INT_MIN)
                               ? FLAGS_encoder_fireup_timeout
                               : encoder_fireup_timeout_,
                               message_callback_));

    if (!relay->Link("relay_source", source_, "source")) {
      LOG_ERROR << "Couldn't link the relay pipeline to the source pipeline.";
      result = PipelineLinkingError;
      goto Relay_Failed;
    }

    if ((result = RegisterDataPipeline(relay, "relay_sink", &fd)) != NoError) {
      goto Relay_Failed;
    }

    source_->AddSlavePipeline(relay);

    SyncPipeline(relay);
    return NoError;
  }

 Relay_Failed:
  if (relay != NULL) {
    RemovePipeline(relay);

    relay->Destroy();
    delete relay;
  }
  return result;
}

static
bool __SaveStateCallback(
    io::StateKeepUser* state_keeper,
    std::string name,
    Controller* controller,
    Controller::ControlID control_id,
    const Controller::ControlChannel* control_channel) {
  if (control_channel) {
    if (control_channel->m_type == Controller::Range) {
      if (!control_channel->m_read_only &&
          !control_channel->m_values.m_relative) {
        int value;
        controller->GetValue(control_id, &value);
        const string key(strutil::IntToString(control_id));
        state_keeper->SetValue(name + "/" + key,
            strutil::StringPrintf("%d", value));
        LOG_DEBUG << "\t" << name + "/" + key << " : " << value;
      }
    } else
    if (control_channel->m_type == Controller::List) {
      if (!control_channel->m_read_only) {
        std::string value;
        controller->GetValue(control_id, &value);
        const string key(strutil::IntToString(control_id));
        state_keeper->SetValue(name + "/" + key, value);
        LOG_DEBUG << "\t" << name + "/" + key << " : " << value;
      }
    }
  }
  return true;
}

void StreamProvider::SaveState() {
  if (controllers_state_keeper_) {
    controllers_state_keeper_->BeginTransaction();

    // get all the controllers values, if it's the case
    for ( ControllerMap::iterator c = controllers_.begin();
          c != controllers_.end(); ++c ) {
      if (c->second == NULL) {
        LOG_WARNING << "The controller for '" << c->first
                    << "' controlled element was not yet created "
                    << "when saving the controllers' state.";
        continue;
      }

      Controller::EnumerateCallback callback =
          NewPermanentCallback(__SaveStateCallback,
                               controllers_state_keeper_, c->first, c->second);

      LOG_DEBUG << "Saving controller state for '" << c->first << "'.";
      c->second->EnumerateControls(callback);
      delete callback;
    }

    controllers_state_keeper_->CommitTransaction();
  }
}
void StreamProvider::DeleteState() {
  if (controllers_state_keeper_) {
    controllers_state_keeper_->DeletePrefix("");
  }
}

static
bool __EnumerateControlsCallback(
    map< int, ControllableParameter >* result,
    Controller::ControlID control_id,
    const Controller::ControlChannel* control_channel) {
  if (control_channel != NULL) {
    ControllableParameter p;
    p.name_ = control_channel->m_name;
    if (control_channel->m_type == Controller::Range) {
      Range r;
      r.min_ = control_channel->m_values.m_min;
      r.max_ = control_channel->m_values.m_max;
      r.step_ = control_channel->m_values.m_step;
      p.range_ = r;
    } else
      if (control_channel->m_type == Controller::Range) {
        List l;
        for (std::list<std::string>::const_iterator
                 it = control_channel->m_values.m_list.begin();
             it != control_channel->m_values.m_list.end();
             ++it) {
          l.list_.ref().push_back(*it);
        }
      }
    result->insert(make_pair(control_id, p));
  }
  return true;
}

void StreamProvider::GetControllableParameters(
    rpc::CallContext< map< int32, ControllableParameter > >* call,
    const string& element) {
  ControllerMap::iterator controller = controllers_.find(element);

  map< int32, ControllableParameter > result;
  if (controller != controllers_.end()) {
    if (controller->second != NULL) {
      Controller::EnumerateCallback callback =
          NewPermanentCallback(__EnumerateControlsCallback, &result);

      controller->second->EnumerateControls(callback);

      delete callback;
    }
  }
  call->Complete(result);
}
void StreamProvider::GetRangeValue(rpc::CallContext< int32 >* call,
                                   const string& element, int32 id) {
  ControllerMap::iterator controller = controllers_.find(element);
  int value = Controller::kInvalidValue;
  if (controller != controllers_.end()) {
    if (controller->second != NULL) {
      controller->second->GetValue(
          static_cast<Controller::ControlID>(id), &value);
    }
  }
  call->Complete(int32(value));
}
void StreamProvider::GetRangeValues(
    rpc::CallContext< map< int32, int32 > >* call,
    const string& element,
    const vector< int32 >& ids) {
  ControllerMap::iterator controller = controllers_.find(element);

  map< int32, int32 > result;
  if (controller != controllers_.end()) {
    if (controller->second != NULL) {
      uint32 ids_size = ids.size();
      for (uint32 idx = 0; idx < ids_size; idx++) {
        Controller::ControlID id =
            static_cast<Controller::ControlID>(ids[idx]);
        int value;
        if (controller->second->GetValue(id, &value)) {
          result.insert(make_pair(id, value));
        }
      }
    }
  }
  call->Complete(result);
}
void StreamProvider::SetRangeValue(rpc::CallContext< bool >* call,
                                   const string& element,
                                   int32 id, int32 value) {
  ControllerMap::iterator controller = controllers_.find(element);

  if (controller != controllers_.end()) {
    if (controller->second != NULL) {
      call->Complete(controller->second->SetValue(
                         static_cast<Controller::ControlID>(id),  value));
      // TODO(mihai): Do not do this here..
      SaveState();
      return;
    }
  }
  call->Complete(false);
}
void StreamProvider::SetRangeValues(
    rpc::CallContext< map< int32, bool > >* call,
    const string& element, const map< int32, int32 >& values) {
  ControllerMap::iterator controller = controllers_.find(element);

  map< int32, bool > result;
  if (controller != controllers_.end()) {
    if (controller->second != NULL) {
      for (map< int32, int32 >::const_iterator it =
               values.begin(); it != values.end(); ++it) {
        Controller::ControlID id =
            static_cast<Controller::ControlID>(it->first);
        if (controller->second->SetValue(id, it->second)) {
          result.insert(make_pair(id, true));
        } else {
          result.insert(make_pair(id, false));
        }
      }
    }
  }
  call->Complete(result);
  // TODO(mihai): Do not do this here..
  SaveState();
}

void StreamProvider::GetListValue(
  rpc::CallContext< string >* call,
  const string& element, int32 id) {
  ControllerMap::iterator controller = controllers_.find(element);
  std::string value;
  if (controller != controllers_.end()) {
    if (controller->second != NULL) {
      controller->second->GetValue(
          static_cast<Controller::ControlID>(id), &value);
    }
  }
  call->Complete(value);
}

void StreamProvider::GetListValues(
  rpc::CallContext< map<int32, string> >* call,
  const string& element, const vector<int32>& ids) {
  ControllerMap::iterator controller = controllers_.find(element);

  map< int32, string > result;
  if (controller != controllers_.end()) {
    if (controller->second != NULL) {
      uint32 ids_size = ids.size();
      for (uint32 idx = 0; idx < ids_size; idx++) {
        Controller::ControlID id =
            static_cast<Controller::ControlID>(ids[idx]);
        std::string value;
        if (controller->second->GetValue(id, &value)) {
          result.insert(make_pair(id, value));
        }
      }
    }
  }
  call->Complete(result);
}

void StreamProvider::SetListValue(
  rpc::CallContext<bool>* call,
  const string& element, int32 id, const string& value) {
  ControllerMap::iterator controller = controllers_.find(element);

  if (controller != controllers_.end()) {
    if (controller->second != NULL) {
      call->Complete(controller->second->SetValue(
                         static_cast<Controller::ControlID>(id),
                         value.c_str()));
      // TODO(mihai): Do not do this here..
      SaveState();
      return;
    }
  }
  call->Complete(false);
}
void StreamProvider::SetListValues(
  rpc::CallContext< map<int32, bool> >* call,
  const string& element,
  const map< int32, string >& values) {
  ControllerMap::iterator controller = controllers_.find(element);

  map< int32, bool > result;
  if (controller != controllers_.end()) {
    if (controller->second != NULL) {
      for (map< int32, string >::const_iterator it =
               values.begin(); it != values.end(); ++it) {
        Controller::ControlID id =
            static_cast<Controller::ControlID>(it->first);
        if (controller->second->SetValue(id, it->second.c_str())) {
          result.insert(make_pair(id, true));
        } else {
          result.insert(make_pair(id, false));
        }
      }
    }
  }
  call->Complete(result);
  // TODO(mihai): Do not do this here..
  SaveState();
}

StreamProvider::Result StreamProvider::RegisterDataPipeline(
    media::Pipeline* pipeline,
    const char* multifdsink_name, int* fd) {
  CHECK_NOT_NULL(pipeline);

  *fd = INVALID_FD_VALUE;

  int fds[2];
  if (::pipe(fds) != 0) {
    DLOG_ERROR << "Couldn't create a new pipe, check your system limits.";
    return PipeCreationError;
  }

  if (!pipeline->AddSink(fds[1], multifdsink_name, NULL, NULL)) {
    ::close(fds[1]);
    ::close(fds[0]);
    return PipelineLinkingError;
  }

  *fd = fds[0];
  return NoError;
}

void StreamProvider::RegisterPipelineAlarm(
    media::Pipeline* pipeline, int timeout) {
  CHECK_NOT_NULL(selector_);
  CHECK(timeout_closures_.find(pipeline) == timeout_closures_.end());

  if (pipeline->hasFlags(kIsMuxer)) {
    LOG_INFO << "Pipeline " << reinterpret_cast<void*>(pipeline) << " - '"
             << pipeline->name()
             << "' is a muxer, will be killed.";
    pipeline->Stop();
    return;
  }

  if (timeout < 0) {
    LOG_INFO << "Pipeline " << reinterpret_cast<void*>(pipeline) << " - '"
             << pipeline->name()
             << "' will not be killed, although it has no sinks.";
    return;
  }
  if (timeout == 0) {
    LOG_INFO << "Pipeline " << reinterpret_cast<void*>(pipeline) << " - '"
             << pipeline->name()
             << "' is being killed.";

    RemoveEncoderFromCache(pipeline);
    pipeline->Stop();
    return;
  }

  Closure* const timeout_closure =
      NewCallback(this, &StreamProvider::PipelineTimeout, pipeline);
  timeout_closures_[pipeline] = timeout_closure;
  selector_->RegisterAlarm(timeout_closure, timeout);
  LOG_INFO << "Pipeline " << reinterpret_cast<void*>(pipeline) << " - '"
           << pipeline->name()
           << "' will be killed in " << timeout << " miliseconds.";
}

void StreamProvider::UnregisterPipelineAlarm(media::Pipeline* pipeline,
                                             bool delete_closure) {
  CHECK_NOT_NULL(selector_);

  const ClosureMap::iterator timeout_closure = timeout_closures_.find(pipeline);
  if ( timeout_closure != timeout_closures_.end() ) {
    selector_->UnregisterAlarm(timeout_closure->second);
    if (delete_closure) {
      delete timeout_closure->second;
    }
    timeout_closures_.erase(timeout_closure);
  }
}

void StreamProvider::PipelineTimeout(media::Pipeline* pipeline) {
  LOG_WARNING << "Pipeline " << reinterpret_cast<void*>(pipeline)
              << " - '" << pipeline->name()
              << "' is being stopped as it timed out...";
  UnregisterPipelineAlarm(pipeline, false);

  RemoveEncoderFromCache(pipeline);
  pipeline->Stop();
}

void StreamProvider::PipelineStop(media::Pipeline* pipeline) {
  RemoveEncoderFromCache(pipeline);
  UnregisterPipelineAlarm(pipeline, true);

  if (pipeline == source_) {
    // destroy the controllers
    for ( ControllerMap::iterator controller = controllers_.begin();
          controller != controllers_.end(); ++controller ) {
      source_->RemoveElementHandler(controller->second);
      delete controller->second;
      controller->second = NULL;
    }
    source_ = NULL;
  } else {
    if (source_ != NULL) {
      source_->RemoveSlavePipeline(pipeline);
    }

    // check if it is a muxer
    std::pair<PipelineMultimap::iterator, PipelineMultimap::iterator>
    muxer_range = muxer_to_encoder_.equal_range(pipeline);
    for ( PipelineMultimap::const_iterator encoder_it = muxer_range.first;
          encoder_it != muxer_range.second; ++encoder_it) {
      encoder_to_muxer_.erase(encoder_it->second);
    }
    muxer_to_encoder_.erase(muxer_range.first, muxer_range.second);

    // check if it is an encoder
    std::pair<PipelineMultimap::iterator, PipelineMultimap::iterator>
    encoder_range = encoder_to_muxer_.equal_range(pipeline);
    for ( PipelineMultimap::const_iterator encoder_it = encoder_range.first;
          encoder_it != encoder_range.second; ++encoder_it) {
      encoder_it->second->Stop();
    }
    encoder_to_muxer_.erase(encoder_range.first, encoder_range.second);
  }
  LOG_INFO << "Pipeline " << reinterpret_cast<void*>(pipeline) << " - '"
           << pipeline->name() << "' has been removed from all the maps...";
}

void StreamProvider::PipelineMessageCallback(
    Pipeline* pipeline,
    Pipeline::Message::Kind message) {
  switch (message) {
    case Pipeline::Message::HasSinks:
      DLOG_DEBUG << "Pipeline " << reinterpret_cast<void*>(pipeline) << " - '"
                << pipeline->name() << "' has sinks...";
      UnregisterPipelineAlarm(pipeline, true);
      break;
    case Pipeline::Message::NoMoreSinks:
      if (pipeline == source_) {
        DLOG_DEBUG << "Source pipeline " << reinterpret_cast<void*>(pipeline)
                  << " - '" << pipeline->name() << "' has no sinks left...";
        RegisterPipelineAlarm(
            pipeline,
            (source_idle_timeout_ == INT_MIN)
            ? FLAGS_source_idle_timeout : source_idle_timeout_);
      } else {
        DLOG_DEBUG << "Encoder pipeline " << reinterpret_cast<void*>(pipeline)
                  << " - '" << pipeline->name() << "' has no sinks left...";
        RegisterPipelineAlarm(
            pipeline,
            (encoder_idle_timeout_ == INT_MIN)
            ? FLAGS_encoder_idle_timeout : encoder_idle_timeout_);
      }
      break;
    case Pipeline::Message::Playing:
      if ( pipeline == source_ ) {
        if ( controllers_state_keeper_ ) {
          // set the default controller values, if it's the case
          for ( ControllerMap::iterator c = controllers_.begin();
                c != controllers_.end(); ++c ) {
            if ( c->second == NULL ) {
              LOG_WARNING << "The controller for '" << c->first
                          << "' controlled element was not yet created "
                          << "when setting up default values.";
              continue;
            }

            DLOG_DEBUG << "Loading controller state for '" << c->first << "'.";
            string prefix = c->first+"/";
            size_t prefix_size =
                controllers_state_keeper_->prefix().length() + prefix.length();

            // get the bounds for this controller's values
            map<string, string>::const_iterator begin;
            map<string, string>::const_iterator end;
            controllers_state_keeper_->GetBounds(prefix, &begin, &end);

            // process each value
            map<Controller::ControlID, std::string> values;
            for ( map<string, string>::const_iterator v = begin;
                  v != end; v++ ) {
              // the keys are int
              errno = 0;
              const int32 key = static_cast<int32>(
                  strtol(v->first.substr(prefix_size).c_str(), NULL, 10));
              if (errno != 0) {
                LOG_WARNING
                    << "The key '" << v->first << "' is not valid "
                    << "while restoring values for controller '"
                    << c->first << "'.";
                continue;
              }
              values[static_cast<Controller::ControlID>(key)] = v->second;
            }
            // do set the values
            for (map<Controller::ControlID, string>::iterator
                    s = values.begin();
                  s != values.end(); ++s) {
              DLOG_DEBUG << "\t" << s->first << " : " << s->second;

              const Controller::ControlChannel *channel =
                  c->second->GetControlChannel(s->first);

              if (channel && !channel->m_read_only) {
                if (channel->m_type == Controller::Range) {
                  if (!channel->m_values.m_relative) {
                    if ( s->second.size() != sizeof(int32) ) {
                      LOG_WARNING << "The value for key '"
                      << s->first
                      << "' is not valid while restoring values for '"
                      << c->first << "'.";
                      continue;
                    }
                    const int32 value =
                        *reinterpret_cast<const int32*>(s->second.data());
                    c->second->SetValue(s->first, value);
                  }
                } else
                if (channel->m_type == Controller::List) {
                  c->second->SetValue(s->first, s->second.c_str());
                }
              }
            }
          }
        }
      }
      break;
    case Pipeline::Message::Terminated:
      PipelineStop(pipeline);

      RemovePipeline(pipeline);

      pipeline->Destroy();
      delete pipeline;
      break;
    default:
      break;
  }
}

StreamProvider::Result StreamProvider::CreateAudioEncoder(
    const Encoder::Audio::Params& params,
    int audio_port,
    bool& new_encoder,
    const char*& mime_type,
    media::Pipeline*& pipeline) {
  CHECK_NOT_NULL(source_);

  AudioEncodersMap::iterator existing;
  if ((existing = audio_encoders_.find(make_pair(audio_port, params))) == audio_encoders_.end()) {
    new_encoder = true;

    if (params.bitrate_ == -1) {
      pipeline = NULL;
      mime_type = "";
      return NoError;
    }

    ////////////////////////////////////////
    //
    // IMPORTANT: the position of << operators is extremely important !
    //            Don't change unless you know what you do !
    //

    PipelineDescription description;
    description
        << "fdsrc name=encoder_audio_source ! gdpdepay name=encoder_audio_gdp"
        << AudioAdapter() << AudioEncoder(NULL,
                                          params.type_,
                                          params.samplerate_,
                                          params.samplewidth_,
                                          params.bitrate_,
                                          params.raw_,
                                          NULL)
        << "tee name=t"
        << "queue max-size-buffers=0 max-size-bytes=0 max-size-time=5000000000"
        << MultiFDSink("encoder_audio_sink_gdp",
                       false,
                       FLAGS_provider_link_hard_limit,
                       FLAGS_provider_link_soft_limit,
                       -1,
                       GDP, MultiFDSink::LatestKeyframe)
        << PipelineDescription::end
        << PipelineDescription::ref("t")
        << "queue max-size-buffers=0 max-size-bytes=0 max-size-time=5000000000"
        << MultiFDSink("encoder_audio_sink",
                       false,
                       FLAGS_provider_link_hard_limit,
                       FLAGS_provider_link_soft_limit,
                       -1,
                       NoProtocol, MultiFDSink::LatestKeyframe);

    std::string name = (params.bitrate_ != 0)
                       ? strutil::StringPrintf(
                           "%s - %s (%d kbps)",
                           m_name.c_str(),
                           Encoder::Audio::GetName(params.type_),
                           params.bitrate_/1024)
                       : strutil::StringPrintf(
                           "%s - %s (default)",
                           m_name.c_str(),
                           Encoder::Audio::GetName(params.type_));
    LOG_INFO << "Creating an audio encoder based on:\n"
             << description.description();

    pipeline = new Pipeline(name.c_str());
    if ( !pipeline->Create(description) ) {
      delete pipeline;
      pipeline = NULL;
      LOG_ERROR << "Couldn't create an audio encoder based on:\n"
                << description.description();
      return PipelineParametersError;
    }
    AddPipeline(pipeline,
                PipelineParams(0,
                               ((encoder_fireup_timeout_ == INT_MIN)
                                ? FLAGS_encoder_fireup_timeout
                                : encoder_fireup_timeout_),
                               message_callback_));
    const string mfds(strutil::StringPrintf(
                          "source_audio_mfds%d", audio_port));
    if (!pipeline->Link("encoder_audio_source",
                        source_, mfds.c_str())) {
      RemovePipeline(pipeline);
      pipeline->Destroy();
      delete pipeline;
      pipeline = NULL;
      LOG_ERROR << "Couldn't link the audio encoder pipeline to the "
                << "source pipeline.";
      return PipelineLinkingError;
    }
    audio_encoders_[make_pair(audio_port, params)] = pipeline;
    audio_encoders_reversed_[pipeline] =
    audio_encoders_.find(make_pair(audio_port, params));

    source_->AddSlavePipeline(pipeline);
  } else {
    new_encoder = false;
    pipeline = existing->second;
  }

  mime_type = Encoder::Audio::GetMimeType(params.type_);
  return NoError;
}

StreamProvider::Result StreamProvider::CreateVideoEncoder(
    const Encoder::Video::Params& params,
    int video_port,
    bool& new_encoder,
    const char*& mime_type,
    media::Pipeline*& pipeline) {
  CHECK_NOT_NULL(source_);

  VideoEncodersMap::iterator existing;
  if ((existing = video_encoders_.find(make_pair(video_port, params))) == video_encoders_.end()) {
    new_encoder = true;

    if (params.bitrate_ == -1) {
      pipeline = NULL;
      mime_type = "";
      return NoError;
    }

    ////////////////////////////////////////
    //
    // IMPORTANT: the position of << operators is extremely important !
    //            Don't change unless you know what you do !
    //

    PipelineDescription description;
    description
        << "fdsrc name=encoder_video_source ! gdpdepay name=encoder_video_gdp"
        << VideoAdapter()
        << VideoEncoder(NULL,
                        params.type_,
                        params.width_,
                        params.height_,
                        params.framerate_n_,
                        params.framerate_d_,
                        params.bitrate_,
                        params.gop_size_,
                        params.strict_rc_,
                        0,
                        NULL,
                        NULL)
        << "tee name=t"
        << "queue max-size-buffers=0 max-size-bytes=0 max-size-time=5000000000"
        << MultiFDSink("encoder_video_sink_gdp",
                       false,
                       FLAGS_provider_link_hard_limit,
                       FLAGS_provider_link_soft_limit,
                       -1,
                       GDP, MultiFDSink::LatestKeyframe)
        << PipelineDescription::end
        << PipelineDescription::ref("t")
        << "queue max-size-buffers=0 max-size-bytes=0 max-size-time=5000000000"
        << MultiFDSink("encoder_video_sink",
                       false,
                       FLAGS_provider_link_hard_limit,
                       FLAGS_provider_link_soft_limit,
                       -1,
                       NoProtocol, MultiFDSink::LatestKeyframe);

    std::string name =
        (params.bitrate_ != 0) ?
        strutil::StringPrintf("%s - %s (%d kbps)",
                              m_name.c_str(),
                              Encoder::Video::GetName(params.type_),
                              params.bitrate_/1024) :
        strutil::StringPrintf("%s - %s (default)",
                              m_name.c_str(),
                              Encoder::Video::GetName(params.type_));
    LOG_INFO << "Creating a video encoder based on:\n"
             << description.description();
    pipeline = new Pipeline(name.c_str());
    if (!pipeline->Create(description)) {
      delete pipeline;
      pipeline = NULL;

      LOG_ERROR << "Couldn't create a video encoder based on:\n"
                << description.description();
      return PipelineParametersError;
    }
    AddPipeline(pipeline,
                PipelineParams(0,
                               (encoder_fireup_timeout_ == INT_MIN)
                               ? FLAGS_encoder_fireup_timeout
                               : encoder_fireup_timeout_,
                               message_callback_));
    const string mfds(strutil::StringPrintf(
                          "source_video_mfds%d", video_port));
    if (!pipeline->Link("encoder_video_source",
                        source_, mfds.c_str())) {
      RemovePipeline(pipeline);
      pipeline->Destroy();

      delete pipeline;
      pipeline = NULL;

      LOG_ERROR << "Couldn't link the video encoder pipeline to the "
                << "source pipeline.";
      return PipelineLinkingError;
    }
    video_encoders_[make_pair(video_port, params)] = pipeline;
    video_encoders_reversed_[pipeline] =
    video_encoders_.find(make_pair(video_port, params));

    source_->AddSlavePipeline(pipeline);
  } else {
    new_encoder = false;
    pipeline = existing->second;
  }

  mime_type = Encoder::Video::GetMimeType(params.type_);
  return NoError;
}

void StreamProvider::RemoveEncoderFromCache(media::Pipeline* pipeline) {
  AudioEncodersMapReversed::iterator audio;
  if ((audio = audio_encoders_reversed_.find(pipeline)) !=
      audio_encoders_reversed_.end()) {
    audio_encoders_.erase(audio->second);
    audio_encoders_reversed_.erase(audio);
    return;
  }
  VideoEncodersMapReversed::iterator video;
  if ((video = video_encoders_reversed_.find(pipeline)) !=
      video_encoders_reversed_.end()) {
    video_encoders_.erase(video->second);
    video_encoders_reversed_.erase(video);
  }
}
}
