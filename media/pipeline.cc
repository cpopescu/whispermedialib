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
#include <whisperlib/common/base/system.h>

#include "media/pipeline.h"
#include "media/controller_v4l.h"
#include "media/controller_v4l2.h"

namespace {
using namespace media;

// The element handler used to process fdsrc elements.
//
// We need this class because we need to set/reset the fd
// property of the fdsrc element only when the element
// is in the READY state.
//
// When the fdsrc element is transitioning to the READY
// state on termination, the element handler is removed
// and the associated fd is closed.
class FdSrcElementHandler : public Pipeline::ElementHandler {
 public:
  explicit FdSrcElementHandler(int fd)
      : m_fd(fd) {
  }
  virtual ~FdSrcElementHandler() {
    ::close(m_fd);
  }

 protected:
  // Called when the pipeline's state changes.
  virtual bool OnPipelineStateChanged(Pipeline* pipeline,
                                      Pipeline::State old_state,
                                      Pipeline::State new_state) {
    switch ( new_state ) {
      case Pipeline::Starting_READY_TO_PLAYING:
        DLOG_PIPELINE_DEBUG(pipeline)
          << "Added fd source " << m_fd << " to element '"
          << GST_OBJECT_PATH(m_element) << "'.";

        g_object_set(m_element, "fd", m_fd, NULL);
        break;
      case Pipeline::Terminating_READY_TO_NULL:
        DLOG_PIPELINE_DEBUG(pipeline)
          << "Removing fd source " << m_fd << " from element '"
          << GST_OBJECT_PATH(m_element) << "'.";

        g_object_set(m_element, "fd", 1, NULL);
        return false;
    default:
      break;
    }
    return true;
  }
 protected:
  int m_fd;
};

// The element handler used to process fd sinks.
//
// We need this class because we can add a new fd
// to a multifdsink element only in the READY state.
//
// When a fd is removed from the multifdsink, a message
// is posted to the pipeline's message bus and the fd
// is removed when the message gets processed, in the
// pipeline thread.
class FdSinkElementHandler : public Pipeline::ElementHandler {
 public:
  explicit FdSinkElementHandler(int fd)
      : m_fd(fd) {
  }

 protected:
  // Called when the pipeline's state changes.
  virtual bool OnPipelineStateChanged(
    Pipeline* pipeline,
    Pipeline::State old_state,
    Pipeline::State new_state) {
    switch (new_state) {
    case Pipeline::Starting_READY_TO_PLAYING:
      DLOG_PIPELINE_DEBUG(pipeline)
        << "Added fd sink "<< m_fd << " to element '"
        << GST_OBJECT_PATH(m_element) << "'.";
        g_signal_emit_by_name(m_element, "add", m_fd);
        break;
    case Pipeline::Terminating_READY_TO_NULL:
      DLOG_PIPELINE_DEBUG(pipeline)
        << "Removing fd sink " << m_fd << " from element '"
        << GST_OBJECT_PATH(m_element) << "'.";
        g_signal_emit_by_name(m_element, "remove", m_fd);
        return false;
    default:
      break;
    }
    return true;
  }
 protected:
  // The fd.
  int m_fd;
};
}

// our namespace
namespace media {

Pipeline::Pipeline(const char* name, const uint32 flags)
    : m_name(name),
      m_flags(flags),
      m_pipeline(NULL),
      m_state(Initial),
      m_message_queue(NULL),
      m_thread(NULL),
      m_is_eos(false),
      m_error(NULL)  {
  g_static_rec_mutex_init(&m_mutex);
}

Pipeline::~Pipeline() {
  g_static_rec_mutex_lock(&m_mutex);

  while (!m_element_handlers.empty()) {
    (*m_element_handlers.begin())->Detach();
    m_element_handlers.erase(m_element_handlers.begin());
  }

  CHECK_NULL(m_pipeline);
  CHECK_NULL(m_thread);

  if (m_error)
    g_error_free(m_error);

  g_static_rec_mutex_unlock(&m_mutex);
  g_static_rec_mutex_free(&m_mutex);
}

// Creates the pipeline.
bool Pipeline::Create(const char* description) {
  CHECK_NOT_NULL(description);

  g_static_rec_mutex_lock(&m_mutex);
  CHECK_NULL(m_pipeline);

  LOG_PIPELINE_DEBUG(this) << "Creating with:\n" << description;

  GError* error = NULL;
  m_pipeline = gst_parse_launch(description, &error);
  if (m_pipeline == NULL) {
    if (error) {
      LOG_PIPELINE_ERROR(this)
          << "Couldn't construct the pipeline, reason '"
          << GST_STR_NULL(error->message) << "'.";
      g_error_free(error);
    } else {
      LOG_PIPELINE_ERROR(this)
          << "Couldn't construct the pipeline, unknown reason.";
    }
    g_static_rec_mutex_unlock(&m_mutex);
    return false;
  } else {
    if (error != NULL) {
      LOG_PIPELINE_ERROR(this)
          << "Couldn't construct the pipeline, reason '"
          << GST_STR_NULL(error->message) << "'.";

      gst_object_unref(m_pipeline);
      m_pipeline = NULL;

      g_static_rec_mutex_unlock(&m_mutex);
      return false;
    }
  }

  g_object_set(m_pipeline, "name", m_name.c_str(), NULL);
  gst_pipeline_set_auto_flush_bus(GST_PIPELINE(m_pipeline), FALSE);

  SetState(Created);
  g_static_rec_mutex_unlock(&m_mutex);
  return true;
}
// Creates the pipeline.
bool Pipeline::Create(const PipelineDescription& description) {
  if (!description.IsValid()) {
    LOG_PIPELINE_ERROR(this)
        << "The pipeline description is marked as invalid.";
    return false;
  }
  return Create(description.description().c_str());
}

// Destroys the pipeline.
void Pipeline::Destroy() {
  g_static_rec_mutex_lock(&m_mutex);

  if (m_pipeline != NULL) {
    while (!m_multifdsinks.empty()) {
      g_signal_handler_disconnect(m_multifdsinks.begin()->first,
                                  m_multifdsinks.begin()->second);
      gst_object_unref(m_multifdsinks.begin()->first);

      m_multifdsinks.erase(m_multifdsinks.begin());
    }

    CHECK(m_multifdsinks.empty());
    CHECK(m_sinks.empty());

    gst_object_unref(m_pipeline);
    m_pipeline = NULL;

    SetState(Initial);
  }

  CHECK(m_state == Initial);
  g_static_rec_mutex_unlock(&m_mutex);
}

// Starts the pipeline.
bool Pipeline::Start(int32 fireup_timeout) {
  g_static_rec_mutex_lock(&m_mutex);
  CHECK(m_state == Created);
  CHECK_NULL(m_thread);
  CHECK_NOT_NULL(m_pipeline);

  ThreadData* thread_data = new ThreadData;
  thread_data->pipeline = this;
  thread_data->fireup_timeout = fireup_timeout;

  LOG_PIPELINE_DEBUG(this) << "Starting pipeline execution...";
  m_message_queue->PostMessage(new Message(Message::Started, this));

  SetState(Starting);
  bool result;
  if ( !(result = (m_thread = g_thread_create(
                     Pipeline::RunLoop, thread_data, TRUE, NULL)) != NULL)) {
    SetState(Created);

    LOG_PIPELINE_ERROR(this)
      << "Couldn't start the pipeline thread, the pipeline will be terminated.";
    m_message_queue->PostMessage(new Message(Message::Terminated, this));

    delete thread_data;
  }

  g_static_rec_mutex_unlock(&m_mutex);
  return result;
}

// Stops a running pipeline.
void Pipeline::Stop() {
  g_static_rec_mutex_lock(&m_mutex);
  if (m_state > Created) {
    if (m_state < Terminated) {
      CHECK_NOT_NULL(m_pipeline);
      gst_element_post_message(GST_ELEMENT(m_pipeline),
                               gst_message_new_application(
                                   GST_OBJECT(m_pipeline),
                                   gst_structure_new("Interrupt",
                                                     "message",
                                                     G_TYPE_STRING,
                                                     "Pipeline interrupted",
                                                     NULL)));
    }
  }
  g_static_rec_mutex_unlock(&m_mutex);
}

// Joins the pipeline thread.
void Pipeline::Join() {
  g_static_rec_mutex_lock(&m_mutex);
  CHECK_NOT_NULL(m_pipeline);

  if (m_thread != NULL) {
    GThread* thread = m_thread;

    g_static_rec_mutex_unlock(&m_mutex);
    g_thread_join(thread);
    g_static_rec_mutex_lock(&m_mutex);

    m_thread = NULL;

    CHECK(m_state == Terminated);

    SetState(Created);

    LOG_PIPELINE_DEBUG(this) << "The pipeline has terminated.";
    m_message_queue->PostMessage(new Message(Message::Terminated, this));
  }

  g_static_rec_mutex_unlock(&m_mutex);
}

// Adds a slave pipeline.
//
// The slave pipelines have their clock synchronized with this pipeline.
void Pipeline::AddSlavePipeline(Pipeline* slave) {
  g_static_rec_mutex_lock(&m_mutex);
  CHECK_NOT_NULL(m_pipeline);
  g_static_rec_mutex_lock(&slave->m_mutex);
  CHECK_NOT_NULL(slave->m_pipeline);

  GstClock* clock = gst_pipeline_get_clock(GST_PIPELINE(m_pipeline));
  gst_pipeline_use_clock(GST_PIPELINE(slave->m_pipeline), clock);
  gst_object_unref(clock);

  m_slaves.insert(slave);

  g_static_rec_mutex_unlock(&slave->m_mutex);
  g_static_rec_mutex_unlock(&m_mutex);
}

// Removes the given element handler from this pipeline.
void Pipeline::RemoveSlavePipeline(Pipeline* slave) {
  g_static_rec_mutex_lock(&m_mutex);
  m_slaves.erase(slave);
  g_static_rec_mutex_unlock(&m_mutex);
}

// Adds the given element handler to the pipeline's element handlers.
bool Pipeline::AddElementHandler(ElementHandler* handler,
                                 const char* element_name) {
  g_static_rec_mutex_lock(&m_mutex);
  CHECK_NOT_NULL(m_pipeline);

  LOG_PIPELINE_FATAL_IF(this, handler == NULL)
    << "Adding a NULL element handler.";

  GstElement* element = gst_bin_get_by_name(GST_BIN(m_pipeline), element_name);
  if (element == NULL) {
    LOG_PIPELINE_ERROR(this)
      << "Cannot find any element named '" << element_name
      << "' while adding element handler " << handler << ".";

    g_static_rec_mutex_unlock(&m_mutex);
    return false;
  }

  handler->Attach(element);
  gst_object_unref(element);

  // When the element handler is added we go through
  // all the intermediate states until reaching the
  // current pipeline state.
  State state_current = Initial;
  while (state_current != m_state) {
    State state_next = state_current;
    switch (state_current) {
      case Initial:
        state_next = Created;
        break;
      case Created:
        state_next = Starting;
        break;
      case Starting:
        if (m_state < Terminated) {
          state_next = Starting_NULL_TO_READY;
        } else {
          state_next = Terminated;
        }
        break;
      case Starting_NULL_TO_READY:
        if (m_state < Terminating_PLAYING_TO_READY) {
          state_next = Starting_READY_TO_PLAYING;
        } else {
          state_next = Terminating_PLAYING_TO_READY;
        }
        break;
      case Starting_READY_TO_PLAYING:
        if (m_state < Terminating_READY_TO_NULL) {
          state_next = Playing;
        } else {
          state_next = Terminating_READY_TO_NULL;
        }
        break;
      case Playing:
        state_next = Terminating_PLAYING_TO_READY;
        break;
      case Terminating_PLAYING_TO_READY:
        state_next = Terminating_READY_TO_NULL;
        break;
      case Terminating_READY_TO_NULL:
        state_next = Terminated;
        break;
      default:
        LOG_PIPELINE_FATAL(this)
          << "Invalid current state while updating the state of an "
          << " element handler.";
    }

    if (!handler->OnPipelineStateChanged(this, state_current, state_next)) {
      handler->Detach();
      handler = NULL;
      break;
    }
    state_current = state_next;
  }

  if (handler != NULL) {
    m_element_handlers.insert(handler);
  }

  g_static_rec_mutex_unlock(&m_mutex);
  return true;
}

// Removes the given element handler from this pipeline.
void Pipeline::RemoveElementHandler(ElementHandler* handler) {
  g_static_rec_mutex_lock(&m_mutex);
  CHECK_NOT_NULL(m_pipeline);

  const ElementHandlerSet::iterator handler_ = m_element_handlers.find(handler);
  if ( handler_ != m_element_handlers.end() ) {
    handler->Detach();
    m_element_handlers.erase(handler_);
  } else {
    LOG_PIPELINE_ERROR(this)
      << "Cannot find element handler " << handler << " in this pipeline.";
  }

  g_static_rec_mutex_unlock(&m_mutex);
}

// Create a controller of the given element.
Controller* Pipeline::CreateController(const char* element_name) {
  g_static_rec_mutex_lock(&m_mutex);
  CHECK_NOT_NULL(m_pipeline);

  GstElement* element = gst_bin_get_by_name(GST_BIN(m_pipeline), element_name);
  if (element == NULL) {
    LOG_PIPELINE_ERROR(this)
        << "Cannot find any element named '"
        << element_name << "' while creating a controller.";

    g_static_rec_mutex_unlock(&m_mutex);
    return NULL;
  }

  GstObject* factory = GST_OBJECT(gst_element_get_factory(element));
  CHECK_NOT_NULL(factory);

  Controller* controller = NULL;

  const char* class_name = (factory->name != NULL) ? factory->name : "";
  if (strcmp(class_name, "v4lsrc") == 0) {
    DLOG_PIPELINE_DEBUG(this) << "Created a V4L controller for element '"
                              << element_name << "'.";
    controller = new V4LSourceController();
  } else if (strcmp(class_name, "v4l2src") == 0) {
    DLOG_PIPELINE_DEBUG(this) << "Created a V4L2 controller for element '"
                              << element_name << "'.";
    controller = new V4L2SourceController();
  } else {
    LOG_PIPELINE_ERROR(this) << "Element '" << element_name
                             << "' of class '"
                             << class_name << "' is not controllable.";
  }

  gst_object_unref(element);
  g_static_rec_mutex_unlock(&m_mutex);

  return controller;
}

// Adds the given fd sink for the given multifdsink element.
bool Pipeline::AddSink(int fd,
                       const char* multifdsink_name,
                       SinkRemovedCallback removed_callback,
                       void* removed_callback_data) {
  g_static_rec_mutex_lock(&m_mutex);
  CHECK_NOT_NULL(m_pipeline);
  if ( m_state == Terminated ) {
    LOG_PIPELINE_ERROR(this)
      << "Cannot add " << fd
      << " file descriptor as a sink, as the pipeline is terminated.";

    g_static_rec_mutex_unlock(&m_mutex);
    return false;
  }

  if ( m_sinks.find(fd) != m_sinks.end() ) {
    LOG_PIPELINE_ERROR(this)
      << "The " << fd
      << " file descriptor was already added as a sink for this pipeline.";

    g_static_rec_mutex_unlock(&m_mutex);
    return false;
  }

  Sink sink;
  sink.multifdsink = gst_bin_get_by_name(GST_BIN(m_pipeline), multifdsink_name);
  if ( sink.multifdsink == NULL ) {
    LOG_PIPELINE_ERROR(this)
      << "The pipeline couldn't find any '" << multifdsink_name
      << "' element while adding file descriptor sink " << fd << ".";

    g_static_rec_mutex_unlock(&m_mutex);
    return false;
  }

  const ElementMap::iterator
      multifdsink = m_multifdsinks.find(sink.multifdsink);
  if ( multifdsink == m_multifdsinks.end() ) {
    gulong signal_id = g_signal_connect(sink.multifdsink,
                                        "client-fd-removed",
                                        G_CALLBACK(onFdRemoved),
                                        this);
    if (signal_id == 0) {
      LOG_PIPELINE_ERROR(this)
        << "Element '" << multifdsink_name
        << "' doesn't seem to be a multifdsink while adding "
        << "file descriptor sink " << fd << ".";

      gst_object_unref(sink.multifdsink);
      g_static_rec_mutex_unlock(&m_mutex);
      return false;
    }

    gst_object_ref(sink.multifdsink);
    m_multifdsinks[sink.multifdsink] = signal_id;
  }

  sink.removed_callback = removed_callback;
  sink.removed_callback_data = removed_callback_data;

  bool was_empty = m_sinks.empty();
  m_sinks[fd] = sink;

  if ( !AddElementHandler(new FdSinkElementHandler(fd), multifdsink_name) ) {
    LOG_PIPELINE_FATAL(this)
      << "Couldn't create and add the fd sink element handler for multifdsink '"
      << multifdsink_name << "'.";
  }

  if (was_empty) {
    m_message_queue->PostMessage(new Message(Message::HasSinks, this));
  }
  g_static_rec_mutex_unlock(&m_mutex);
  return true;
}

// Removes the given fd sink.
void Pipeline::RemoveSink(int fd) {
  g_static_rec_mutex_lock(&m_mutex);
  CHECK_NOT_NULL(m_pipeline);

  const SinkMap::iterator sink = m_sinks.find(fd);
  if ( sink != m_sinks.end() ) {
    g_signal_emit_by_name(sink->second.multifdsink, "remove", fd);
  }

  g_static_rec_mutex_unlock(&m_mutex);
}

// Links the given fdsrc of this pipeline to the given multifdsink element of
// the given pipeline.
bool Pipeline::Link(const char* fdsrc_name,
                    Pipeline* other,
                    const char* other_multifdsink_name) {
  g_static_rec_mutex_lock(&m_mutex);
  CHECK_NOT_NULL(m_pipeline);

  GstElement* fdsrc;
  if ((fdsrc = gst_bin_get_by_name(GST_BIN(m_pipeline), fdsrc_name)) == NULL) {
    LOG_PIPELINE_ERROR(this) <<
    "Couldn't find any '" << fdsrc_name << "' element "
    "while linking '" << name() << ":" << fdsrc_name << "' to '" <<
    other->name() << ":" << other_multifdsink_name << "'.";

    g_static_rec_mutex_unlock(&m_mutex);
    return false;
  }

  int fd;
  g_object_get(fdsrc, "fd", &fd, NULL);
  if ( fd != 0 && fd != -1 ) {  // TODO(mihai): why also 0 ???
    LOG_PIPELINE_ERROR(this)
      << "The '" << fdsrc_name << "' element was already linked "
      << "while linking '" << name() << ":" << fdsrc_name << "' to '"
      << other->name() << ":" << other_multifdsink_name << "'.";

    g_static_rec_mutex_unlock(&m_mutex);
    return false;
  }

  int fds[2];
  if (pipe(fds) == 0) {
    if (!other->AddSink(fds[1], other_multifdsink_name, NULL, NULL)) {
      ::close(fds[1]);
      ::close(fds[0]);

      gst_object_unref(fdsrc);
      g_static_rec_mutex_unlock(&m_mutex);
      return false;
    }

    if (!AddElementHandler(new FdSrcElementHandler(fds[0]), fdsrc_name)) {
      LOG_PIPELINE_FATAL(this)
        << "Couldn't create and add the fd source element handler for '"
        << fdsrc_name << "' " "while linking '" << name() << ":" << fdsrc_name
        << "' to '" << other->name() << ":" << other_multifdsink_name << "'.";
    }

    DLOG_PIPELINE_INFO(this)
      << "Linked '" << name() << ":" << fdsrc_name << "' to '"
      << other->name() << ":" << other_multifdsink_name << "'.";

    gst_object_unref(fdsrc);
    g_static_rec_mutex_unlock(&m_mutex);
    return true;
  } else {
    LOG_PIPELINE_ERROR(this)
      << "Couldn't create the pipe while linking '" << name()
      << ":" << fdsrc_name << "' to '" << other->name()
      << ":" << other_multifdsink_name << "'.";
  }

  gst_object_unref(fdsrc);
  g_static_rec_mutex_unlock(&m_mutex);
  return false;
}

// The "client-fd-removed" signal hook.
void Pipeline::onFdRemoved(GstElement* element, int fd, gpointer data) {
  Pipeline* pipeline;
  pipeline = static_cast<Pipeline*>(data);

  CHECK_NOT_NULL(pipeline);
  gst_element_post_message(GST_ELEMENT(element),
                           gst_message_new_application(
                               GST_OBJECT(element),
                               gst_structure_new("RemoveSink", "fd",
                                                 G_TYPE_INT, fd, NULL)));
}

// Changes the pipeline's state to the given one.
void Pipeline::SetState(State state) {
  if ( state == m_state )
    return;

  DLOG_PIPELINE_DEBUG(this)
    << "Changing pipeline state from " << m_state << " to " << state << ".";

  // checking if the state change is valid
  switch (m_state) {
    case Initial:
      CHECK(state == Created);
      break;
    case Created:
      CHECK(state == Starting || state == Initial);
      break;
    case Starting:
      CHECK(state == Starting_NULL_TO_READY || state == Terminated);
      break;
    case Starting_NULL_TO_READY:
      CHECK(state == Starting_READY_TO_PLAYING ||
            state == Terminating_READY_TO_NULL);
      break;
    case Starting_READY_TO_PLAYING:
      CHECK(state == Playing || state == Terminating_PLAYING_TO_READY);
      break;
    case Playing:
      CHECK(state == Terminating_PLAYING_TO_READY);
      break;
    case Terminating_PLAYING_TO_READY:
      CHECK(state == Terminating_READY_TO_NULL);
      break;
    case Terminating_READY_TO_NULL:
      CHECK(state == Terminated);
      break;
    case Terminated:
      CHECK(state == Created);
      break;
  }

  list<ElementHandler*> to_detach;
  for ( ElementHandlerSet::iterator handler = m_element_handlers.begin();
        handler != m_element_handlers.end(); ++handler ) {
    if (!(*handler)->OnPipelineStateChanged(this, m_state, state)) {
      to_detach.push_back(*handler);
    }
  }
  while ( !to_detach.empty() ) {
    (*to_detach.begin())->Detach();
    m_element_handlers.erase(*to_detach.begin());

    to_detach.erase(to_detach.begin());
  }

  m_state = state;
}

// changes the actual GStreamer's pipeline state to the given one.
void Pipeline::SetGstState(GstState state, bool locked) {
  if ( m_pipeline == NULL ) {
    return;
  }
  GstElement* const pipeline = GST_ELEMENT(gst_object_ref(m_pipeline));
  if ( locked ) {
    g_static_rec_mutex_unlock(&m_mutex);
  }
  gst_element_set_state(pipeline, state);
  if ( locked ) {
    g_static_rec_mutex_lock(&m_mutex);
  }
  gst_object_unref(pipeline);
}

// Removes the the given fd sink, internal implementation.
void Pipeline::RemoveSink_Internal(SinkMap::iterator sink) {
  int fd = sink->first;

  Sink sink_ = sink->second;
  m_sinks.erase(sink);

  if (sink_.removed_callback) {
    sink_.removed_callback(fd, sink_.removed_callback_data);
  } else {
    ::close(fd);
  }

  DLOG_PIPELINE_DEBUG(this) <<
  "Removed fd sink " << fd << " of element '" <<
  GST_OBJECT_PATH(sink->second.multifdsink) << "'.";

  gst_object_unref(sink_.multifdsink);

  if (m_sinks.empty()) {
    m_message_queue->PostMessage(new Message(Message::NoMoreSinks, this));
  }
}

// The actual pipeline loop.
gpointer Pipeline::RunLoop(gpointer data) {
  ThreadData* thread_data = static_cast<ThreadData*>(data);
  thread_data->pipeline->RunLoop(thread_data->fireup_timeout);
  delete thread_data;
  return NULL;
}
void Pipeline::RunLoop(int32 fireup_timeout) {
  g_static_rec_mutex_lock(&m_mutex);
  CHECK_NOT_NULL(m_pipeline);

  GstBus* bus = gst_pipeline_get_bus(GST_PIPELINE(m_pipeline));
  LOG_PIPELINE_FATAL_IF(this, bus == NULL) <<
  "Couldn't retrieve the pipeline's message bus.";

  GstClock* clock = gst_system_clock_obtain();
  LOG_PIPELINE_FATAL_IF(this, clock == NULL) <<
  "Couldn't retrieve the system clock.";

  GstClockTime start_position = gst_clock_get_time(clock);
  GstClockTime current_position = start_position;

  GstClockTimeDiff timeout = GST_MSECOND*fireup_timeout;

  bool run = true;
  CHECK(m_state == Starting);

  if (m_error) {
    g_error_free(m_error);
    m_error = NULL;
  }

  SetState(Starting_NULL_TO_READY);

  // move the pipeline to the READY state
  if (run) {
    SetGstState(GST_STATE_READY, true);

    GstState state;
    GstState pending;
    gst_element_get_state(m_pipeline, &state, &pending, 0);

    if ( state == GST_STATE_READY && pending == GST_STATE_VOID_PENDING ) {
      SetState(Starting_READY_TO_PLAYING);
    } else {
      while ( run && m_state == Starting_NULL_TO_READY ) {
        g_static_rec_mutex_unlock(&m_mutex);

        GstMessage* message;
        if ( (message = gst_bus_timed_pop(bus, GST_SECOND/100)) != NULL ) {
          run = ProcessMessage(message);
        }

        current_position = gst_clock_get_time(clock);
        g_static_rec_mutex_lock(&m_mutex);

        if (GST_CLOCK_DIFF(start_position, current_position) >= timeout) {
          LOG_PIPELINE_ERROR(this) <<
          "Starting up the pipeline timed out.";
          run = false;
        }
      }
    }
  }

  // move the pipeline to the PLAYING state
  if (run) {
    SetGstState(GST_STATE_PLAYING, true);

    GstState state;
    GstState pending;
    gst_element_get_state(m_pipeline, &state, &pending, 0);

    if ( state == GST_STATE_PLAYING && pending == GST_STATE_VOID_PENDING ) {
      SetState(Playing);
    } else {
      while ( run && m_state == Starting_READY_TO_PLAYING ) {
        g_static_rec_mutex_unlock(&m_mutex);

        GstMessage* message = gst_bus_timed_pop(bus, GST_SECOND/100);
        if ( message != NULL ) {
          run = ProcessMessage(message);
        }

        current_position = gst_clock_get_time(clock);
        g_static_rec_mutex_lock(&m_mutex);

        if (GST_CLOCK_DIFF(start_position, current_position) >= timeout) {
          // TODO(mihai): make this configurable
          m_error = g_error_new(g_quark_from_static_string("whispercast"),
                                1, "The pipeline startup has timed out.");

          LOG_PIPELINE_ERROR(this)  << "Starting up the pipeline timed out.";
          run = false;
        }
      }
    }
  }

  if (run) {
    LOG_PIPELINE_DEBUG(this) << "The pipeline has started playing.";

    CHECK(m_state == Playing);
    m_message_queue->PostMessage(new Message(Message::Playing, this));

    while (run) {
      g_static_rec_mutex_unlock(&m_mutex);

      GstMessage* message;
      if ((message = gst_bus_timed_pop(bus, GST_CLOCK_TIME_NONE)) != NULL) {
        run = ProcessMessage(message);
      }

      g_static_rec_mutex_lock(&m_mutex);
    }
  }

  switch (m_state) {
    case Starting_NULL_TO_READY:
      SetState(Terminating_READY_TO_NULL);
      break;
    case Starting_READY_TO_PLAYING:
      SetState(Terminating_PLAYING_TO_READY);
      break;
    case Playing:
      SetState(Terminating_PLAYING_TO_READY);
      break;
    default:
      LOG_PIPELINE_FATAL(this)
        << "Invalid pipeline state on play loop termination.";
  }

  LOG_PIPELINE_DEBUG(this) << "Shutting down the pipeline...";

  // move the pipeline to the READY state
  if (m_state == Terminating_PLAYING_TO_READY) {
    SetGstState(GST_STATE_READY, true);

    GstState state;
    GstState pending;
    gst_element_get_state(m_pipeline, &state, &pending, 0);

    if (state == GST_STATE_READY && pending == GST_STATE_VOID_PENDING) {
      SetState(Terminating_READY_TO_NULL);
    } else {
      while (m_state == Terminating_PLAYING_TO_READY) {
        g_static_rec_mutex_unlock(&m_mutex);

        GstMessage* message;
        if ( (message = gst_bus_timed_pop(bus, GST_CLOCK_TIME_NONE)) != NULL ) {
          ProcessMessage(message);
        }

        g_static_rec_mutex_lock(&m_mutex);
      }
      CHECK(m_state == Terminating_READY_TO_NULL);
    }
  }

  // move the pipeline to the NULL state
  if (m_state == Terminating_READY_TO_NULL) {
    SetGstState(GST_STATE_NULL, true);

    GstState state;
    GstState pending;
    gst_element_get_state(m_pipeline, &state, &pending, 0);

    if ( state == GST_STATE_NULL && pending == GST_STATE_VOID_PENDING ) {
      SetState(Terminated);
    } else {
      while ( m_state == Terminating_READY_TO_NULL ) {
        g_static_rec_mutex_unlock(&m_mutex);
        GstMessage* message;
        if ( (message = gst_bus_timed_pop(bus, GST_CLOCK_TIME_NONE)) != NULL ) {
          ProcessMessage(message);
        }

        g_static_rec_mutex_lock(&m_mutex);
      }
    }
    CHECK(m_state == Terminated);
  }

  CHECK(m_state == Terminated);

  while (!m_sinks.empty()) {
    RemoveSink_Internal(m_sinks.begin());
  }

  GstMessage* message;
  while ( ((message = gst_bus_timed_pop(bus, 0)) != NULL) ) {
    ProcessMessage(message);
  }

  gst_object_unref(clock);
  gst_object_unref(bus);

  LOG_PIPELINE_DEBUG(this) << "The pipeline has stopped.";
  m_message_queue->PostMessage(new Message(Message::Stopped, this));

  g_static_rec_mutex_unlock(&m_mutex);
}

// Processes a message from the pipeline's message bus.
bool Pipeline::ProcessMessage(GstMessage* message) {
  bool run = true;

  switch (GST_MESSAGE_TYPE(message)) {
  case GST_MESSAGE_ERROR:
  case GST_MESSAGE_WARNING:
  case GST_MESSAGE_INFO: {
    // error
    GError* err;
    gchar* debug;
    switch (GST_MESSAGE_TYPE(message)) {
    case GST_MESSAGE_ERROR:
      gst_message_parse_error(message, &err, &debug);
      if (debug != NULL) {
        LOG_PIPELINE_ERROR(this)
          << "ERROR (" << std::hex << std::showbase << err->code << "): "
          << err->message << "\nAdditional info:\n" << debug;
      } else {
        LOG_PIPELINE_ERROR(this)
          << "ERROR (" << std::hex << std::showbase << err->code << "): "
          << err->message;
      }
      // we terminate on any error, but we save the error that triggered this...
      g_static_rec_mutex_lock(&m_mutex);
      if (m_error)
        g_error_free(m_error);
      m_error = g_error_copy(err);
      g_static_rec_mutex_unlock(&m_mutex);
      Stop();
      break;
    case GST_MESSAGE_WARNING:
      gst_message_parse_warning(message, &err, &debug);
      if (debug != NULL) {
        LOG_PIPELINE_WARNING(this)
          << "WARNING (" << std::hex << std::showbase << err->code << "): "
          << err->message << "\nAdditional info:\n" << debug;
      } else {
        LOG_PIPELINE_WARNING(this)
          << "WARNING (" << std::hex << std::showbase << err->code << "): "
          << err->message;
      }
      g_static_rec_mutex_lock(&m_mutex);
      if ( m_state > Starting ) {
        if ( m_state < Terminating_PLAYING_TO_READY ) {
          switch (err->code) {
          case GST_CORE_ERROR_CLOCK:
            SetGstState(GST_STATE_PAUSED, true);
            SetGstState(GST_STATE_PLAYING, true);
            LOG_PIPELINE_WARNING(this)
              << "The pipeline was restarted due to a clocking warning.";
            break;
          default:
            break;
          }
        }
      }
      g_static_rec_mutex_unlock(&m_mutex);
      break;
    case GST_MESSAGE_INFO:
      gst_message_parse_info(message, &err, &debug);
      if (debug != NULL) {
        LOG_PIPELINE_DEBUG(this)
          << "INFO: " << err->message << "\nAdditional info:\n" << debug;
      } else {
        LOG_PIPELINE_DEBUG(this) << "INFO: " << err->message;
      }
      break;

    default:
      break;
    }
    g_error_free(err);
    g_free(debug);
  }
    break;
  case GST_MESSAGE_STATE_CHANGED: {
    g_static_rec_mutex_lock(&m_mutex);

    if ( GST_ELEMENT(message->src) == m_pipeline ) {
      GstState old_state;
      GstState new_state;
      GstState pending_state;
      gst_message_parse_state_changed(
        message, &old_state, &new_state, &pending_state);

      DLOG_PIPELINE_DEBUG(this)
        << "STATE CHANGE: Pipeline has changed state from "
        << gst_element_state_get_name(old_state)
        << " to " << gst_element_state_get_name(new_state)
        << ", pending " << gst_element_state_get_name(pending_state) << ".";
      if (pending_state == GST_STATE_VOID_PENDING) {
        switch (m_state) {
        case Starting_NULL_TO_READY:
          if (new_state == GST_STATE_READY)
            SetState(Starting_READY_TO_PLAYING);
          break;
        case Starting_READY_TO_PLAYING:
          if (new_state == GST_STATE_PLAYING)
            SetState(Playing);
          break;
        case Terminating_PLAYING_TO_READY:
          if (new_state == GST_STATE_READY)
            SetState(Terminating_READY_TO_NULL);
          break;
        case Terminating_READY_TO_NULL:
          if (new_state == GST_STATE_NULL)
            SetState(Terminated);
          break;
        default:
          break;
        }
      }
    }

    g_static_rec_mutex_unlock(&m_mutex);
  }
    break;
  case GST_MESSAGE_NEW_CLOCK: {
    GstClock* new_clock;
    gst_message_parse_new_clock(message, &new_clock);
    DLOG_PIPELINE_DEBUG(this)
      << "NEW CLOCK: " << GST_OBJECT_PATH(new_clock);
    g_static_rec_mutex_lock(&m_mutex);
    for ( PipelineSet::iterator slave = m_slaves.begin();
          slave != m_slaves.end(); ++slave ) {
      g_static_rec_mutex_lock(&(*slave)->m_mutex);
      gst_pipeline_use_clock(GST_PIPELINE((*slave)->m_pipeline), new_clock);
      g_static_rec_mutex_unlock(&(*slave)->m_mutex);
    }
    g_static_rec_mutex_unlock(&m_mutex);
  }
    break;
  case GST_MESSAGE_SEGMENT_START:
    DLOG_PIPELINE_DEBUG(this) << "SEGMENT START";
    g_static_rec_mutex_lock(&m_mutex);
    m_is_eos = false;
    g_static_rec_mutex_unlock(&m_mutex);
    break;
  case GST_MESSAGE_SEGMENT_DONE:
    DLOG_PIPELINE_DEBUG(this) << "SEGMENT STOP";
    break;
  case GST_MESSAGE_EOS:
    DLOG_PIPELINE_DEBUG(this) << "EOS.";
    // we terminate on EOS
    g_static_rec_mutex_lock(&m_mutex);
    m_is_eos = true;
    g_static_rec_mutex_unlock(&m_mutex);
    Stop();
    break;
  case GST_MESSAGE_BUFFERING: {
    gint percent;
    gst_message_parse_buffering(message, &percent);
    LOG_PIPELINE_DEBUG(this) << "BUFFERING: %" << percent << ".";
  }
    break;
    case GST_MESSAGE_APPLICATION: {
      const GstStructure* structure;
      structure = gst_message_get_structure(message);
      if ( gst_structure_has_name(structure, "Interrupt") ) {
        if ( !m_is_eos )
          m_error = g_error_new(
              // TODO(mihai): make this configurable
              g_quark_from_static_string ("whispercast"), 1,
              "The pipeline was interrupted.");
        run = false;
      } else {
        if (gst_structure_has_name(structure, "RemoveSink")) {
          gint fd;
          CHECK(gst_structure_get_int(structure, "fd", &fd));

          g_static_rec_mutex_lock(&m_mutex);

          const SinkMap::iterator sink = m_sinks.find(fd);
          if ( sink != m_sinks.end() ) {
            RemoveSink_Internal(sink);
          }

          g_static_rec_mutex_unlock(&m_mutex);
        }
      }
    }
    default:
      break;
  }
  gst_message_unref(message);
  return run;
}
}
