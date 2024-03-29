/* GStreamer
 * Copyright (C) 2008 Pioneers of the Inevitable <songbird@songbirdnest.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#ifndef __DSHOWVIDEOSINK_H__
#define __DSHOWVIDEOSINK_H__

#include <gst/gst.h>
#include <gst/base/gstbasesink.h>

#include "dshowvideofakesrc.h"

#include <dshow.h>

#include "d3d9.h"
#include "vmr9.h"

#pragma warning( disable : 4090 4024)

G_BEGIN_DECLS
#define GST_TYPE_DSHOWVIDEOSINK              (gst_dshowvideosink_get_type())
#define GST_DSHOWVIDEOSINK(obj)              (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_DSHOWVIDEOSINK,GstDshowVideoSink))
#define GST_DSHOWVIDEOSINK_CLASS(klass)      (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_DSHOWVIDEOSINK,GstDshowVideoSinkClass))
#define GST_IS_DSHOWVIDEOSINK(obj)           (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_DSHOWVIDEOSINK))
#define GST_IS_DSHOWVIDEOSINK_CLASS(klass)   (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_DSHOWVIDEOSINK))
typedef struct _GstDshowVideoSink GstDshowVideoSink;
typedef struct _GstDshowVideoSinkClass GstDshowVideoSinkClass;

/* Renderer-specific support classes */
class RendererSupport
{
public:
  virtual const char *GetName() = 0;
  virtual IBaseFilter *GetFilter() = 0;
  virtual gboolean Configure() = 0;
  virtual gboolean SetRendererWindow(HWND window) = 0;
  virtual void PaintWindow() = 0;
  virtual void MoveWindow() = 0;
  virtual void DestroyWindow() = 0;
  virtual void DisplayModeChanged() = 0;
};

struct _GstDshowVideoSink
{
  GstBaseSink sink;

  /* Preferred renderer to use: VM9 or VMR */
  char *preferredrenderer;

   /* The filter graph (DirectShow equivalent to pipeline */
  IFilterGraph *filter_graph;

  /* Renderer wrapper (EVR, VMR9, or VMR) and support code */
  RendererSupport *renderersupport;

  /* Our fakesrc filter */
  VideoFakeSrc *fakesrc;

  /* DirectShow description of media type (equivalent of GstCaps) */
  AM_MEDIA_TYPE mediatype;

  gboolean keep_aspect_ratio;
  gboolean full_screen;

  /* If the window is closed, we set this and error out */
  gboolean window_closed;

  /* The video window set through GstXOverlay */
  HWND window_id;

  gboolean connected;

  /* If we create our own window, we run it from another thread */
  GThread *window_thread;
  HANDLE window_created_signal;

  /* If we use an app-supplied window, we need to hook its WNDPROC */
  WNDPROC prevWndProc;
};

struct _GstDshowVideoSinkClass
{
  GstBaseSinkClass parent_class;
};

GType gst_dshowvideosink_get_type (void);

G_END_DECLS
#endif /* __DSHOWVIDEOSINK_H__ */
