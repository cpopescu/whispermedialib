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

#include "media/pipeline_runner.h"

// our namespace
namespace media {

PipelineRunner::PipelineRunner(const char* name)
    : m_name((name == NULL) ? "" : name),
      m_state(Idle),
      m_running_pipelines(0),
      m_pending_pipelines(0) {
  g_static_rec_mutex_init(&m_mutex);
  m_message_queue = new MessageQueue();
}

PipelineRunner::~PipelineRunner() {
  g_static_rec_mutex_lock(&m_mutex);
  
  CHECK(m_state == Idle);
  CHECK_EQ(m_running_pipelines, 0);
  CHECK(m_pipelines.empty());
  delete m_message_queue;
  
  g_static_rec_mutex_unlock(&m_mutex);
  g_static_rec_mutex_free(&m_mutex);
}

// Adds an existing pipeline to the pipeline runner.
void PipelineRunner::AddPipeline(Pipeline* pipeline, PipelineParams params) {
  AddPipeline_Internal(pipeline, params);
}
// Removes a pipeline from the pipeline runner.
void PipelineRunner::RemovePipeline(Pipeline* pipeline) {
  RemovePipeline_Internal(m_pipelines.find(pipeline));
}

// Starts the pipeline runner.
bool PipelineRunner::Start() {
  g_static_rec_mutex_lock(&m_mutex);
  CHECK_EQ(m_state, Idle);

  m_state = Running;
  m_message_queue->PostMessage(new Message(Message::Started, this));
  
  g_static_rec_mutex_unlock(&m_mutex);
  return true;
}

// Stops the pipeline runner.
void PipelineRunner::Stop() {
  g_static_rec_mutex_lock(&m_mutex);
  m_state = Stopping;

  if (m_running_pipelines == 0 && m_pending_pipelines == 0) {
    m_message_queue->PostMessage(new Message(Message::Terminated, this));
  } else {
    for (map<Pipeline*, PipelineParams*>::iterator
             pipeline = m_pipelines.begin();
         pipeline != m_pipelines.end(); pipeline++) {
      pipeline->first->Stop();
    }
  }
  g_static_rec_mutex_unlock(&m_mutex);
}

// Runs a pipeline iteration.
bool PipelineRunner::Iterate(
    int64 timeout,
    Callback1<const media::Message*>* pre_process_callback,
    Callback1<const media::Message*>* post_process_callback) {
  bool run = true;

  media::Message* const message = m_message_queue->PopMessage(timeout);
  if ( message != NULL ) {
    if (pre_process_callback != NULL) {
      pre_process_callback->Run(message);
    }
    run = ProcessMessage(message);
    if (post_process_callback != NULL) {
      post_process_callback->Run(message);
    }
    delete message;
  }
  return run;
}

// Processes a message from the pipeline runner's message queue.
bool PipelineRunner::ProcessMessage(media::Message* message) {
  g_static_rec_mutex_lock(&m_mutex);
  
  CHECK_NOT_NULL(message);
  bool run = true;

  // get the pipeline that posted the message
  map<Pipeline*, PipelineParams*>::iterator pipeline;
  if (message->m_kind >= Pipeline::Message::First
      && message->m_kind <= Pipeline::Message::Last) {
    pipeline = m_pipelines.find(
        reinterpret_cast<Pipeline::Message*>(message)->m_pipeline);
    CHECK(pipeline != m_pipelines.end());
  } else {
    pipeline = m_pipelines.end();
  }

  // pre-process
  switch (message->m_kind) {
    case Message::Terminated:
      DLOG_RUNNER_DEBUG(this) << "Terminated...";
      m_state = Idle;
      run = false;
      break;

    case Pipeline::Message::Started:
      // increment the number of running pipelines
      m_running_pipelines++;
      m_pending_pipelines--;
      CHECK_GT(m_pending_pipelines, -1);

      DLOG_RUNNER_DEBUG(this)
          << "Pipeline " << reinterpret_cast<void*>(pipeline->first) << " - '"
          << pipeline->first->name() << "' has started.";
      break;
    case Pipeline::Message::Playing:
      DLOG_RUNNER_DEBUG(this)
          << "Pipeline " << reinterpret_cast<void*>(pipeline->first) << " - '"
          << pipeline->first->name() << "' is now running normally.";
      break;
    case Pipeline::Message::Stopped:
      DLOG_RUNNER_DEBUG(this)
          << "Pipeline " << reinterpret_cast<void*>(pipeline->first) << " - '"
          << pipeline->first->name() << "' has stopped.";
      break;
    case Pipeline::Message::Terminated:
      DLOG_RUNNER_DEBUG(this)
          << "Pipeline " << reinterpret_cast<void*>(pipeline->first) << " - '"
          << pipeline->first->name() << "' has terminated it's execution.";

      if ((pipeline->second->m_options & Required) != 0) {
        Stop();
      }

      // decrement the number of running pipelines
      m_running_pipelines--;
      CHECK_GT(m_running_pipelines, -1);
      if (m_state == Stopping) {
        if (m_running_pipelines == 0) {
          m_message_queue->PostMessage(new Message(Message::Terminated, this));
        }
      }
      break;
    default:
      break;
  }

  // run the associated state callback
  if (pipeline != m_pipelines.end()) {
    if (pipeline->second->m_message_callback != NULL) {
      pipeline->second->m_message_callback->Run(
          pipeline->first,
          static_cast<Pipeline::Message::Kind>(message->m_kind));
    }
  }

  // post-process
  switch (message->m_kind) {
    case Pipeline::Message::Stopped:
      // wait for the pipeline to actually terminate
      pipeline->first->Join();
      break;
    default:
      break;
  }

  g_static_rec_mutex_unlock(&m_mutex);
  return run;
}

// Adds a new pipeline to this pipeline runner, internal implementation
void PipelineRunner::AddPipeline_Internal(Pipeline* pipeline,
                                          PipelineParams params) {
  g_static_rec_mutex_lock(&m_mutex);
  CHECK(pipeline->state() <= Pipeline::Created);

  CHECK(pipeline->m_message_queue == NULL);
  pipeline->m_message_queue = m_message_queue;

  m_pipelines[pipeline] = new PipelineParams(params);
  g_static_rec_mutex_unlock(&m_mutex);
}

// Removes an existing pipeline from this pipeline runner, internal
// implementation.
void PipelineRunner::RemovePipeline_Internal(
    map<Pipeline*, PipelineParams*>::iterator pipeline) {
  g_static_rec_mutex_lock(&m_mutex);
  CHECK(pipeline != m_pipelines.end());
  CHECK(pipeline->first->state() <= Pipeline::Created);

  CHECK(pipeline->first->m_message_queue == m_message_queue);
  pipeline->first->m_message_queue = NULL;

  delete pipeline->second;
  m_pipelines.erase(pipeline);
  g_static_rec_mutex_unlock(&m_mutex);
}
}
