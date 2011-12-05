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

#ifndef __STREAMING_ELEMENT_BIN_H__
#define __STREAMING_ELEMENT_BIN_H__

#include <sstream>
#include <string>

#include <whisperlib/common/base/types.h>
#include WHISPER_HASH_SET_HEADER
#include WHISPER_HASH_MAP_HEADER

#include <whispermedialib/media/component.h>

// helpers
#define LOG_PIPELINE_INFO(pipeline)                                     \
  LOG_INFO << "PIPELINE "<< reinterpret_cast<void*>(pipeline)           \
                            << " - '" << (pipeline)->name() << "': "
#define LOG_PIPELINE_WARNING(pipeline)                                  \
  LOG_WARNING << "PIPELINE "<< reinterpret_cast<void*>(pipeline)        \
                               << " - '" << (pipeline)->name() << "': "
#define LOG_PIPELINE_ERROR(pipeline)                                    \
  LOG_ERROR << "PIPELINE "<< reinterpret_cast<void*>(pipeline)          \
                             << " - '" << (pipeline)->name() << "': "
#define LOG_PIPELINE_DEBUG(pipeline)                                    \
  LOG_DEBUG << "PIPELINE "<< reinterpret_cast<void*>(pipeline)          \
                             << " - '" << (pipeline)->name() << "': "
#define LOG_PIPELINE_FATAL(pipeline)                                    \
  LOG_FATAL << "PIPELINE "<< reinterpret_cast<void*>(pipeline)          \
                             << " - '" << (pipeline)->name() << "': "
#define LOG_PIPELINE_FATAL_IF(pipeline, cond)                           \
  LOG_FATAL_IF(cond) << "PIPELINE "<< reinterpret_cast<void*>(pipeline) \
                                      << " - '" << (pipeline)->name()   \
                                                   << "': "

// helpers
#define DLOG_PIPELINE_INFO(pipeline)                                    \
  DLOG_INFO << "PIPELINE "<< reinterpret_cast<void*>(pipeline)          \
                             << " - '" << (pipeline)->name() << "': "
#define DLOG_PIPELINE_WARNING(pipeline)                                 \
  DLOG_WARNING << "PIPELINE "<< reinterpret_cast<void*>(pipeline)       \
                                << " - '" << (pipeline)->name() << "': "
#define DLOG_PIPELINE_ERROR(pipeline)                                   \
  DLOG_ERROR << "PIPELINE "<< reinterpret_cast<void*>(pipeline)         \
                              << " - '" << (pipeline)->name() << "': "
#define DLOG_PIPELINE_DEBUG(pipeline)                                   \
  DLOG_DEBUG << "PIPELINE "<< reinterpret_cast<void*>(pipeline)         \
                              << " - '" << (pipeline)->name() << "': "
#define DLOG_PIPELINE_FATAL(pipeline)                                   \
  DLOG_FATAL << "PIPELINE "<< reinterpret_cast<void*>(pipeline)         \
                              << " - '" << (pipeline)->name() << "': "
#define DLOG_PIPELINE_FATAL_IF(pipeline, cond)                          \
  DLOG_FATAL_IF(cond) << "PIPELINE "<< reinterpret_cast<void*>(pipeline) \
  << " - '" << (pipeline)->name() << "': "

namespace media {
// Forward declaration
class Controller;

// The textual pipeline description that is used to
// create the actual GStreamer pipeline.

// This is built up using a std::ostream kind of process,
// serializing elements either as pure text or using the
// helper classes that are part of the streaming library.
class PipelineDescription : protected  std::stringstream {
 public:
  PipelineDescription() :
    m_state(Begin) {
  }

  // Returns the description.
  std::string description() const {
    return std::stringstream::str();
  }
  // Returns true if the description is valid.
  bool IsValid() const {
    return std::stringstream::good();
  }

  // Specializations.
  PipelineDescription& operator << (const char* s) {
    PreInsert();
    std::operator << (*this, s);
    return PostInsert(false);
  }
  PipelineDescription& operator << (const Component& c) {
    PreInsert();
    c.Serialize(*this);
    return PostInsert(false);
  }

  // Operator needed for endl style of manipulators.
  PipelineDescription&
  operator<<(PipelineDescription& (*pf)(PipelineDescription&)) {
    return pf(*this);
  }

  // Stream manipulator that inserts an element/pad reference
  // into the pipeline textual description.
  class ref {
   private:
    const char* m_element;
    const char* m_pad;
   public:
    ref(const char* element, const char* pad = NULL) :
        m_element(element), m_pad(pad) {
    }
    PipelineDescription& operator()(PipelineDescription& o) const {
      o.PreInsert();
      if (m_element != NULL)
        std::operator << (o, m_element) << ".";
      if (m_pad != NULL)
        std::operator << (o, m_pad);
      return o.PostInsert(false);
    }
  };
  // Stream manipulator that ends the current segment
  // in the pipeline textual description.
  static
  PipelineDescription& end(PipelineDescription& o) {
    std::operator << (o, " ");
    return o.PostInsert(true);
  }

 private:
  // The current description state.
  enum State {
    Begin,
    Running
  } m_state;

  // Called before an insertion.
  PipelineDescription& PreInsert() {
    if (m_state == Running) {
      std::operator << (*this, " ! ");
    }
    return *this;
  }
  // Called after an insertion.
  PipelineDescription& PostInsert(bool end) {
    switch (m_state) {
      case Begin:
        if (!end) {
          m_state = Running;
        }
        break;
      case Running:
        if (end) {
          m_state = Begin;
        }
        break;
      default:
        break;
    }
    return *this;
  }
};

// Inserts a modifier into the pipeline description.
inline PipelineDescription& operator<<(
    PipelineDescription& o,
    const PipelineDescription::ref& m) {
  return m(o);
}

// The pipeline is the basic component
// that can perform streaming, and is instantiated
// based on a textual GStreamer pipeline representation.
//
// Links between pipelines and between pipelines and
// the external world are performed through fds,
// the data being transported over the fds either
// as a pure bitstream or packetized using one of the
// available transport protocols (@see Protocol).
//
// All the sink fds that are attached to multfdsinks in
// the pipeline are managed by the pipeline itself.
class Pipeline {
 public:
  // The possible pipeline states, the Starting_XXX states
  // being based on the interesting GStreamer state transitions,
  // enforced by the pipeline run logic.
  //
  // Valid state transitions are:
  //
  // Initial->Created,
  //
  // Created->Starting,
  // Created->Initial,
  //
  // Starting->Terminated,
  // Starting->Starting_NULL_TO_READY,
  //
  // Starting_NULL_TO_READY->Starting_READY_TO_PLAYING,
  // Starting_NULL_TO_READY->Terminating_READY_TO_NULL,
  //
  // Starting_READY_TO_PLAYING->Playing,
  // Starting_READY_TO_PLAYING->Terminating_PLAYING_TO_READY,
  //
  // Playing->Terminating_PLAYING_TO_READY,
  //
  // Terminating_PLAYING_TO_READY->Terminating_READY_TO_NULL,
  //
  // Terminating_READY_TO_NULL->Terminated,
  //
  // Terminated->Created;
  enum State {
    Initial,
    Created,
    Starting,
    Starting_NULL_TO_READY,
    Starting_READY_TO_PLAYING,
    Playing,
    Terminating_PLAYING_TO_READY,
    Terminating_READY_TO_NULL,
    Terminated
  };

  // The pipeline message.
  struct Message : media::Message {
    enum Kind {
      First = media::Message::Last+1,

      // the pipeline has started
      Started = First,

      // the pipeline is playing
      Playing,
      // the pipeline has stopped
      Stopped,

      // the pipeline was terminated
      Terminated,

      // the pipeline has at least a link to an fd
      HasSinks,
      // the pipeline is not linked anymore to fds
      NoMoreSinks,

      Last = NoMoreSinks
    };
    Pipeline* m_pipeline;

    Message(int32 kind, Pipeline* pipeline) :
      media::Message(kind), m_pipeline(pipeline) {
    }
  };

  // The base class for element handlers, objects
  // that are attached to a specific element in the pipeline
  // and are meant perform actions related to that element
  // on the pipeline state transitions.
  //
  // The OnPipelineStateChanged() member is called in the
  // pipeline thread, usually different than the thread that
  // created the element handler in the first place.
  //
  // The most useful state transitions are:
  //
  // Starting_NULL_TO_READY->Starting_READY_TO_PLAYING:
  // The element got into the READY state, so all the associated
  // resources are acquired but the element is not yet playing.
  //
  // Playing->Terminating_PLAYING_TO_READY:
  // The element got into the READY state after playing.
  class ElementHandler {
  public:
    ElementHandler() :
      m_element(NULL) {
    }
    virtual ~ElementHandler() {
      if (m_element != NULL) {
        gst_object_unref(m_element);
      }
    }

  protected:
    // The pipeline is allowed to change the state of the element handler.
    friend class Pipeline;

    // The element this element handler is attached to.
    GstElement* m_element;

    // Called by the pipeline when the element handler is attached
    // to the associated element.
    virtual void Attach(GstElement* element) {
      g_assert(m_element == NULL);
      if (element != NULL) {
        m_element = GST_ELEMENT(gst_object_ref(element));
      }
    }
    // Called by the pipeline when the element handler is detached
    // from the associated element - after this the element handler is not
    // referenced anymore by the pipeline.
    virtual void Detach() {
      delete this;
    }

    // Called by the pipeline on all the pipeline state changes.
    virtual bool OnPipelineStateChanged(
        Pipeline* pipeline,
        Pipeline::State old_state,
        Pipeline::State new_state) = 0;
  };

  // The pipeline is communicating with the outside world through multifdsink
  // and fdsrc elements, usually using pipes.
  //
  // When a fd is attached to a multifdsink in the pipeline, the caller
  // can specify a callback that will be called when the fd is detached
  // from the multifdsink. If no callback is specified then the fd
  // is simply closed.
  //
  // The fd sink removed callback.
  typedef void (*SinkRemovedCallback)(int fd, void* data);

 public:
  Pipeline(const char* name, const uint32 flags = 0);
  virtual ~Pipeline();

  // Returns the name of the pipeline.
  const char* name() const {
    return m_name.c_str();
  }

  // Returns the error that stopped the pipeline, if any.
  const GError* error() {
    return m_error;
  }

  // Adds a slave pipeline.
  //
  // The slave pipelines have their clock synchronized with this pipeline.
  void AddSlavePipeline(Pipeline* slave);
  // Removes the given element handler from this pipeline.
  void RemoveSlavePipeline(Pipeline* slave);

  // Adds the given element handler to the given element of the pipeline.
  bool AddElementHandler(ElementHandler* handler, const char* element_name);
  // Removes the given element handler from this pipeline.
  void RemoveElementHandler(ElementHandler* handler);

  // Create a controller of the given element, it also adds
  // it as an element handler for the element.
  Controller* CreateController(const char* element_name);

  // Adds the given fd sink for the given multifdsink element.
  bool AddSink(int fd,
               const char* multifdsink_name,
               SinkRemovedCallback removed_callback,
               void* removed_callback_data);
  // Removes the given fd sink.
  void RemoveSink(int fd);

  // Links the given fdsrc of this pipeline to the given multifdsink element
  // of the given pipeline.
  bool Link(const char* fdsrc_name,
            Pipeline* other,
            const char* other_multifdsink_name);

 protected:
  // The pipeline runner is allowed to tamper with the state of the pipeline.
  friend class PipelineRunner;

  // The name of the pipeline.
  const std::string m_name;
  // 32 bits of flags, to be used by the pipeline's owners.
  const uint32 m_flags;

  // The mutex used to serialize the access to the pipeline internals.
  mutable GStaticRecMutex m_mutex;

  // The actual GStreamer pipeline object.
  GstElement* m_pipeline;

  // The current pipeline state.
  State m_state;

  // The message queue.
  MessageQueue* m_message_queue;
  // The pipeline thread.
  mutable GThread* m_thread;

  // True if EOS was seen.
  bool m_is_eos;
  // The error that stopped the pipeline.
  GError* m_error;

  // A structure used to convey the pipeline data
  // to the pipeline thread entry point.
  struct ThreadData {
    Pipeline* pipeline;
    int32 fireup_timeout;
  };

  // The fd sink data.
  struct Sink {
    GstElement* multifdsink;

    SinkRemovedCallback removed_callback;
    void* removed_callback_data;
  };

  // The multifdsinks (multifdsink->fd-removed signal id).
  typedef hash_map<GstElement*, gulong> ElementMap;
  ElementMap m_multifdsinks;
  // The multifdsink "client-fd-removed" signal hook.
  static void onFdRemoved(GstElement* element, int fd, gpointer data);

  // The fd sinks map (fd->Sink).
  typedef hash_map<int, Sink> SinkMap;
  SinkMap m_sinks;

  // The element handlers.
  typedef hash_set<ElementHandler*> ElementHandlerSet;
  ElementHandlerSet m_element_handlers;
  // The slave pipelines.
  typedef hash_set<Pipeline*> PipelineSet;
  PipelineSet m_slaves;

 public:
  // Returns the state of the pipeline.
  State state() const {
    State state;
    g_static_rec_mutex_lock(&m_mutex);
    state = m_state;
    g_static_rec_mutex_unlock(&m_mutex);
    return state;
  }
  // Returns true if the given flag(s) is(are) set on this pipeline.
  bool hasFlags(uint32 flags) const {
    return (m_flags & flags) != 0;
  }

  // Returns the actual GStreamer pipeline object.
  GstPipeline* pipeline() const {
    GstPipeline* pipeline;
    g_static_rec_mutex_lock(&m_mutex);
    pipeline = GST_PIPELINE(const_cast<Pipeline*>(this)->m_pipeline);
    g_static_rec_mutex_unlock(&m_mutex);
    return pipeline;
  }

  // Creates the pipeline.
  bool Create(const char* description);
  bool Create(const PipelineDescription& description);
  // Destroys the pipeline.
  void Destroy();

  // Starts the pipeline.
  bool Start(int32 fireup_timeout);
  // Stops a running pipeline.
  void Stop();

 protected:
  // Joins the pipeline thread.
  void Join();

  // Changes the pipeline's state to the given one.
  void SetState(State state);
  // changes the actual GStreamer's pipeline state to the given one.
  void SetGstState(GstState state, bool locked);

  // Removes the the given fd sink, internal implementation.
  void RemoveSink_Internal(SinkMap::iterator sink);

  // The actual pipeline loop.
  static gpointer RunLoop(gpointer data);
  void RunLoop(int32 fireup_timeout);


  // Processes a message from the pipeline's message bus.
  bool ProcessMessage(GstMessage* message);
};

}      // namespace media

#endif  // __STREAMING_ELEMENT_BIN_H__
