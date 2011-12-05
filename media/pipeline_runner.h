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

#ifndef  __STREAMING_PIPELINE_RUNNER_H__
#define __STREAMING_PIPELINE_RUNNER_H__

#include <string>
#include <map>

#include <whisperlib/common/base/callback.h>
#include <whispermedialib/media/pipeline.h>

// helpers
#define LOG_RUNNER_INFO(runner)                         \
  LOG_INFO << "RUNNER '"<< (runner)->name() << "': "
#define LOG_RUNNER_WARNING(runner)                      \
  LOG_WARNING << "RUNNER '"<< (runner)->name() << "': "
#define LOG_RUNNER_ERROR(runner)                        \
  LOG_ERROR << "RUNNER '"<< (runner)->name() << "': "
#define LOG_RUNNER_DEBUG(runner)                        \
  LOG_DEBUG << "RUNNER '"<< (runner)->name() << "': "
#define LOG_RUNNER_FATAL(runner)                        \
  LOG_FATAL << "RUNNER '"<< (runner)->name() << "': "
#define LOG_RUNNER_FATAL_IF(runner, cond)                       \
  LOG_FATAL_IF(cond) << "RUNNER '"<< (runner)->name() << "': "

// helpers
#define DLOG_RUNNER_INFO(runner)                        \
  DLOG_INFO << "RUNNER '"<< (runner)->name() << "': "
#define DLOG_RUNNER_WARNING(runner)                             \
  DLOG_WARNING << "RUNNER '"<< (runner)->name() << "': "
#define DLOG_RUNNER_ERROR(runner)                       \
  DLOG_ERROR << "RUNNER '"<< (runner)->name() << "': "
#define DLOG_RUNNER_DEBUG(runner)                       \
  DLOG_DEBUG << "RUNNER '"<< (runner)->name() << "': "
#define DLOG_RUNNER_FATAL(runner)                       \
  DLOG_FATAL << "RUNNER '"<< (runner)->name() << "': "
#define DLOG_RUNNER_FATAL_IF(runner, cond)                      \
  DLOG_FATAL_IF(cond) << "RUNNER '"<< (runner)->name() << "': "

namespace media {
// The pipeline runner is meant to be a container for
// several pipelines, being responsible with running
// the contained pipelines.
class PipelineRunner {
 public:
  // The possible pipeline runner states.
  //
  // Valid state transitions are:
  //
  // Idle->Running,
  // Running->Stopping,
  // Stopping->Idle
  enum State {
    Idle,
    Running,
    Stopping,
  };

 public:
  // The options that can be set on a pipeline added to the pipeline runner.

  // The pipeline is required, so the pipeline runner
  // will stop when this pipeline has stopped.
  static const int32 Required = 1 << 0;

  // The pipeline state change callback.
  typedef Callback2<Pipeline*, Pipeline::Message::Kind>* PipelineStateCallback;

  // The pipeline options.
  struct PipelineParams {
    // the options
    int32 m_options;
    // the fireup timeout, in miliseconds
    int32 m_fireup_timeout;
    // the pipeline message callback
    PipelineStateCallback m_message_callback;

    PipelineParams(int32 options,
                   int32 fireup_timeout,
                   PipelineStateCallback message_callback)
        : m_options(options),
          m_fireup_timeout(fireup_timeout),
          m_message_callback(message_callback) {
      CHECK(m_message_callback == NULL || m_message_callback->is_permanent());
    }
  };

 public:
  // The pipeline runner message.
  struct Message : media::Message {
    enum Kind {
      First = Pipeline::Message::Last+1,

      // the pipeline runner has started
      Started = First,
      // the pipeline was terminated
      Terminated,

      Last = Terminated
    };
    PipelineRunner* m_pipeline_runner;

    Message(int32 kind, PipelineRunner* pipeline_runner) :
      media::Message(kind), m_pipeline_runner(pipeline_runner) {
    }
  };

 protected:
  // The name of the pipeline runner.
  const string m_name;

  // The mutex used to serialize the access to the pipeline internals.
  mutable GStaticRecMutex m_mutex;
  // The current pipeline runner state.
  int32 m_state;

  // The message queue associated to the pipeline runner
  // (shared by all the pipelines).
  MessageQueue* m_message_queue;

  // The managed pipelines.
  map<Pipeline*, PipelineParams*> m_pipelines;
  // The count of running/pending run pipelines.
  int m_running_pipelines;
  int m_pending_pipelines;

 public:
  explicit PipelineRunner(const char* name);
  virtual ~PipelineRunner();

  // Returns the name of the pipeline runner.
  const char* name() const {
    return m_name.c_str();
  }
  // Returns the state of the pipeline runner.
  State state() const {
    return (State)m_state;
  }

  // Adds an existing pipeline to the pipeline runner.
  void AddPipeline(Pipeline* pipeline, PipelineParams params);
  // Removes a pipeline from the pipeline runner.
  void RemovePipeline(Pipeline* pipeline);

  // Syncs the pipeline's state with the pipeline runner's state.
  void SyncPipeline(Pipeline* pipeline) {
    map<Pipeline*, PipelineParams*>::iterator
        existing = m_pipelines.find(pipeline);
    CHECK(existing != m_pipelines.end());

    if ( m_state == Running ) {
      m_pending_pipelines++;
      if ( !existing->first->Start(existing->second->m_fireup_timeout) ) {
        if ( (existing->second->m_options & Required) != 0 ) {
          Stop();
        }
      }
    }
  }

  // Starts the pipeline runner.
  bool Start();
  // Stops the pipeline runner.
  void Stop();

  // Runs a pipeline iteration.
  bool Iterate(
      int64 timeout = 0,
      Callback1<const media::Message*>* pre_process_callback = NULL,
      Callback1<const media::Message*>* post_process_callback = NULL);

 protected:
  // Processes a message from the pipeline runner's message queue.
  virtual
  bool ProcessMessage(media::Message* message);

  // Adds a new pipeline to this pipeline runner, internal implementation.
  void AddPipeline_Internal(Pipeline* pipeline, PipelineParams params);
  // Removes an existing pipeline from this pipeline runner,
  // internal implementation.
  void RemovePipeline_Internal(
      map<Pipeline*, PipelineParams*>::iterator pipeline);
};

}   // namespace media

#endif   // __STREAMING_PIPELINE_RUNNER_H__
