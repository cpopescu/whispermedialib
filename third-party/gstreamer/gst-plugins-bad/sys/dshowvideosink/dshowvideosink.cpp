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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "dshowvideosink.h"
#include "dshowvideofakesrc.h"

#include <gst/interfaces/xoverlay.h>

#include "windows.h"

static const GstElementDetails gst_dshowvideosink_details =
GST_ELEMENT_DETAILS ("DirectShow video sink",
    "Sink/Video",
    "Display data using a DirectShow video renderer",
    "Pioneers of the Inevitable <songbird@songbirdnest.com>");

GST_DEBUG_CATEGORY_STATIC (dshowvideosink_debug);
#define GST_CAT_DEFAULT dshowvideosink_debug

static GstCaps * gst_directshow_media_type_to_caps (AM_MEDIA_TYPE *mediatype);
static gboolean gst_caps_to_directshow_media_type (GstCaps *caps, AM_MEDIA_TYPE *mediatype);

/* TODO: Support RGB! */
static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (
        "video/x-raw-yuv,"
        "width = (int) [ 1, MAX ],"
        "height = (int) [ 1, MAX ],"
        "framerate = (fraction) [ 0, MAX ]," 
        "format = {(fourcc)YUY2, (fourcc)UYVY, (fourcc) YUVY, (fourcc)YV12 }")
    );

static void gst_dshowvideosink_init_interfaces (GType type);

GST_BOILERPLATE_FULL (GstDshowVideoSink, gst_dshowvideosink, GstBaseSink,
    GST_TYPE_BASE_SINK, gst_dshowvideosink_init_interfaces);

enum
{
  PROP_0,
  PROP_KEEP_ASPECT_RATIO,
  PROP_FULL_SCREEN,
  PROP_RENDERER
};

/* GObject methods */
static void gst_dshowvideosink_finalize (GObject * gobject);
static void gst_dshowvideosink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_dshowvideosink_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

/* GstElement methods */
static GstStateChangeReturn gst_dshowvideosink_change_state (GstElement * element, GstStateChange transition);

/* GstBaseSink methods */
static gboolean gst_dshowvideosink_start (GstBaseSink * bsink);
static gboolean gst_dshowvideosink_stop (GstBaseSink * bsink);
static gboolean gst_dshowvideosink_unlock (GstBaseSink * bsink);
static gboolean gst_dshowvideosink_unlock_stop (GstBaseSink * bsink);
static gboolean gst_dshowvideosink_set_caps (GstBaseSink * bsink, GstCaps * caps);
static GstCaps *gst_dshowvideosink_get_caps (GstBaseSink * bsink);
static GstFlowReturn gst_dshowvideosink_render (GstBaseSink *sink, GstBuffer *buffer);

/* GstXOverlay methods */
static void gst_dshowvideosink_set_window_id (GstXOverlay * overlay, ULONG window_id);

/* TODO: event, preroll, buffer_alloc? 
 * buffer_alloc won't generally be all that useful because the renderers require a 
 * different stride to GStreamer's implicit values. 
 */

static gboolean
gst_dshowvideosink_interface_supported (GstImplementsInterface * iface,
    GType type)
{
  g_assert (type == GST_TYPE_X_OVERLAY);
  return TRUE;
}

static void
gst_dshowvideosink_interface_init (GstImplementsInterfaceClass * klass)
{
  klass->supported = gst_dshowvideosink_interface_supported;
}


static void
gst_dshowvideosink_xoverlay_interface_init (GstXOverlayClass * iface)
{
  iface->set_xwindow_id = gst_dshowvideosink_set_window_id;
}

static void
gst_dshowvideosink_init_interfaces (GType type)
{
  static const GInterfaceInfo iface_info = {
    (GInterfaceInitFunc) gst_dshowvideosink_interface_init,
    NULL,
    NULL,
  };

  static const GInterfaceInfo xoverlay_info = {
    (GInterfaceInitFunc) gst_dshowvideosink_xoverlay_interface_init,
    NULL,
    NULL,
  };

  g_type_add_interface_static (type, GST_TYPE_IMPLEMENTS_INTERFACE,
      &iface_info);
  g_type_add_interface_static (type, GST_TYPE_X_OVERLAY, &xoverlay_info);

  GST_DEBUG_CATEGORY_INIT (dshowvideosink_debug, "dshowvideosink", 0, \
      "DirectShow video sink");
}
static void
gst_dshowvideosink_base_init (gpointer klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sink_template));

  gst_element_class_set_details (element_class, &gst_dshowvideosink_details);
}

static void
gst_dshowvideosink_class_init (GstDshowVideoSinkClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstBaseSinkClass *gstbasesink_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;
  gstbasesink_class = (GstBaseSinkClass *) klass;

  gobject_class->finalize = GST_DEBUG_FUNCPTR (gst_dshowvideosink_finalize);
  gobject_class->set_property =
      GST_DEBUG_FUNCPTR (gst_dshowvideosink_set_property);
  gobject_class->get_property =
      GST_DEBUG_FUNCPTR (gst_dshowvideosink_get_property);

  gstelement_class->change_state = GST_DEBUG_FUNCPTR (gst_dshowvideosink_change_state);

  gstbasesink_class->get_caps = GST_DEBUG_FUNCPTR (gst_dshowvideosink_get_caps);
  gstbasesink_class->set_caps = GST_DEBUG_FUNCPTR (gst_dshowvideosink_set_caps);
  gstbasesink_class->start = GST_DEBUG_FUNCPTR (gst_dshowvideosink_start);
  gstbasesink_class->stop = GST_DEBUG_FUNCPTR (gst_dshowvideosink_stop);
  gstbasesink_class->unlock = GST_DEBUG_FUNCPTR (gst_dshowvideosink_unlock);
  gstbasesink_class->unlock_stop =
      GST_DEBUG_FUNCPTR (gst_dshowvideosink_unlock_stop);
  gstbasesink_class->render = GST_DEBUG_FUNCPTR (gst_dshowvideosink_render);

  /* Add properties */
  g_object_class_install_property (G_OBJECT_CLASS (klass),
      PROP_KEEP_ASPECT_RATIO, g_param_spec_boolean ("force-aspect-ratio",
          "Force aspect ratio",
          "When enabled, scaling will respect original aspect ratio", FALSE,
          (GParamFlags)G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass),
      PROP_FULL_SCREEN, g_param_spec_boolean ("fullscreen",
          "Full screen mode",
          "Use full-screen mode (not available when using XOverlay)", FALSE,
          (GParamFlags)G_PARAM_READWRITE));

  g_object_class_install_property (G_OBJECT_CLASS (klass),
      PROP_RENDERER, g_param_spec_string ("renderer", "Renderer", 
      "Force usage of specific DirectShow renderer (VMR9 or VMR)",
      NULL, (GParamFlags)G_PARAM_READWRITE));
}

static void
gst_dshowvideosink_clear (GstDshowVideoSink *sink)
{
  sink->renderersupport = NULL;
  sink->fakesrc = NULL;
  sink->filter_graph = NULL;

  sink->keep_aspect_ratio = FALSE;
  sink->full_screen = FALSE;

  sink->window_closed = FALSE;
  sink->window_id = NULL;

  sink->connected = FALSE;
}

static void
gst_dshowvideosink_init (GstDshowVideoSink * sink, GstDshowVideoSinkClass * klass)
{
  gst_dshowvideosink_clear (sink);

  CoInitializeEx (NULL, COINIT_MULTITHREADED);

  /* TODO: Copied from GstVideoSink; should we use that as base class? */
  /* 20ms is more than enough, 80-130ms is noticable */
  gst_base_sink_set_max_lateness (GST_BASE_SINK (sink), 20 * GST_MSECOND);
  gst_base_sink_set_qos_enabled (GST_BASE_SINK (sink), TRUE);
}

static void
gst_dshowvideosink_finalize (GObject * gobject)
{
  GstDshowVideoSink *sink = GST_DSHOWVIDEOSINK (gobject);

  if (sink->preferredrenderer)
    g_free (sink->preferredrenderer);

  CoUninitialize ();

  G_OBJECT_CLASS (parent_class)->finalize (gobject);
}

static void
gst_dshowvideosink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstDshowVideoSink *sink = GST_DSHOWVIDEOSINK (object);

  switch (prop_id) {
    case PROP_RENDERER:
      if (sink->preferredrenderer)
        g_free (sink->preferredrenderer);

      sink->preferredrenderer = g_value_dup_string (value);
      break;
    case PROP_KEEP_ASPECT_RATIO:
      sink->keep_aspect_ratio = g_value_get_boolean (value);
      break;
    case PROP_FULL_SCREEN:
      sink->full_screen = g_value_get_boolean (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_dshowvideosink_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstDshowVideoSink *sink = GST_DSHOWVIDEOSINK (object);

  switch (prop_id) {
    case PROP_RENDERER:
      g_value_take_string (value, sink->preferredrenderer);
      break;
    case PROP_KEEP_ASPECT_RATIO:
      g_value_set_boolean (value, sink->keep_aspect_ratio);
      break;
    case PROP_FULL_SCREEN:
      g_value_set_boolean (value, sink->full_screen);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstCaps *
gst_dshowvideosink_get_caps (GstBaseSink * basesink)
{
  GstDshowVideoSink *sink = GST_DSHOWVIDEOSINK (basesink);

  return NULL;
}

static void dump_available_media_types (IPin *pin)
{
  /* Enumerate all media types on this pin, output info about them */
  IEnumMediaTypes *enumerator = NULL;
  AM_MEDIA_TYPE *type;
  GstCaps *caps;
  int i = 0;

  GST_INFO ("Enumerating media types on pin %p", pin);

  pin->EnumMediaTypes (&enumerator);

  while (enumerator->Next (1, &type, NULL) == S_OK) {
    i++;
    caps = gst_directshow_media_type_to_caps (type);

    if (caps) {
      gchar *str = gst_caps_to_string (caps);
      GST_INFO ("Type %d: converted to caps \"%s\"", i, str);
      g_free (str);

      gst_caps_unref (caps);
    }
    else
      GST_INFO ("Failed to convert type to GstCaps");

    DeleteMediaType (type);
  }
  GST_INFO ("Enumeration complete");

  enumerator->Release();
}

static void
dump_all_pin_media_types (IBaseFilter *filter)
{
  IEnumPins *enumpins = NULL;
  IPin *pin = NULL;
  HRESULT hres; 

  hres = filter->EnumPins (&enumpins);
  if (FAILED(hres)) {
    GST_WARNING ("Cannot enumerate pins on filter");
    return;
  }

  GST_INFO ("Enumerating pins on filter %p", filter);
  while (enumpins->Next (1, &pin, NULL) == S_OK)
  {
    IMemInputPin *meminputpin;
    PIN_DIRECTION pindir;
    hres = pin->QueryDirection (&pindir);

    GST_INFO ("Found a pin with direction: %s", (pindir == PINDIR_INPUT)? "input": "output");
    dump_available_media_types (pin);

    hres = pin->QueryInterface (
            IID_IMemInputPin, (void **) &meminputpin);
    if (hres == S_OK) {
      GST_INFO ("Pin is a MemInputPin (push mode): %p", meminputpin);
      meminputpin->Release();
    }
    else
      GST_INFO ("Pin is not a MemInputPin (pull mode?): %p", pin);

    pin->Release();
  }
  enumpins->Release();
}

gboolean 
gst_dshow_get_pin_from_filter (IBaseFilter *filter, PIN_DIRECTION pindir, IPin **pin)
{
  gboolean ret = FALSE;
  IEnumPins *enumpins = NULL;
  IPin *pintmp = NULL;
  HRESULT hres; 
  *pin = NULL;

  hres = filter->EnumPins (&enumpins);
  if (FAILED(hres)) {
    return ret;
  }

  while (enumpins->Next (1, &pintmp, NULL) == S_OK)
  {
    PIN_DIRECTION pindirtmp;
    hres = pintmp->QueryDirection (&pindirtmp);
    if (hres == S_OK && pindir == pindirtmp) {
      *pin = pintmp;
      ret = TRUE;
      break;
    }
    pintmp->Release ();
  }
  enumpins->Release ();

  return ret;
}

/* WNDPROC for application-supplied windows */
LRESULT APIENTRY WndProcHook (HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
  /* Handle certain actions specially on the window passed to us.
   * Then forward back to the original window.
   */
  GstDshowVideoSink *sink = (GstDshowVideoSink *)GetProp (hWnd, L"GstDShowVideoSink");

  switch (message) {
    case WM_PAINT:
      sink->renderersupport->PaintWindow ();
      break;
    case WM_MOVE:
    case WM_SIZE:
      sink->renderersupport->MoveWindow ();
      break;
    case WM_DISPLAYCHANGE:
      sink->renderersupport->DisplayModeChanged();
      break;
    case WM_ERASEBKGND:
      /* DirectShow docs recommend ignoring this message to avoid flicker */
      return TRUE;
    case WM_CLOSE:
      sink->window_closed = TRUE;
  }
  return CallWindowProc (sink->prevWndProc, hWnd, message, wParam, lParam);
}

/* WndProc for our default window, if the application didn't supply one */
LRESULT APIENTRY 
WndProc (HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
  GstDshowVideoSink *sink = (GstDshowVideoSink *)GetWindowLongPtr (hWnd, GWLP_USERDATA);

  if (!sink) {
    /* I think these happen before we have a chance to set our userdata pointer */
    GST_DEBUG ("No sink!");
    return DefWindowProc (hWnd, message, wParam, lParam);
  }

  GST_DEBUG_OBJECT (sink, "Got a window message for %x, %x", hWnd, message);

  switch (message) {
    case WM_PAINT:
      sink->renderersupport->PaintWindow ();
      break;
    case WM_MOVE:
    case WM_SIZE:
      sink->renderersupport->MoveWindow ();
      break;
    case WM_DISPLAYCHANGE:
      sink->renderersupport->DisplayModeChanged();
      break;
    case WM_ERASEBKGND:
      /* DirectShow docs recommend ignoring this message */
      return TRUE;
    case WM_CLOSE:
      sink->renderersupport->DestroyWindow ();
      sink->window_closed = TRUE;
      return 0;
  }

  return DefWindowProc (hWnd, message, wParam, lParam);
}

static gpointer
gst_dshowvideosink_window_thread (GstDshowVideoSink * sink)
{
  WNDCLASS WndClass;
  int width, height;
  int offx, offy;
  DWORD exstyle, style;

  memset (&WndClass, 0, sizeof (WNDCLASS));
  WndClass.style = CS_HREDRAW | CS_VREDRAW;
  WndClass.hInstance = GetModuleHandle (NULL);
  WndClass.lpszClassName = L"GST-DShowSink";
  WndClass.hbrBackground = (HBRUSH) GetStockObject (BLACK_BRUSH);
  WndClass.cbClsExtra = 0;
  WndClass.cbWndExtra = 0;
  WndClass.lpfnWndProc = WndProc;
  WndClass.hCursor = LoadCursor (NULL, IDC_ARROW);
  RegisterClass (&WndClass);

  if (sink->full_screen) {
    /* This doesn't seem to work, it returns the wrong values! But when we
     * later use ShowWindow to show it maximized, it goes to full-screen
     * anyway. TODO: Figure out why. */
    width = GetSystemMetrics (SM_CXFULLSCREEN);
    height = GetSystemMetrics (SM_CYFULLSCREEN);
    offx = 0;
    offy = 0;

    style = WS_POPUP; /* No window decorations */
    exstyle = 0;
  }
  else {
    /* By default, create a normal top-level window, the size 
     * of the video.
     */
    RECT rect;
    VIDEOINFOHEADER *vi = (VIDEOINFOHEADER *)sink->mediatype.pbFormat;

    /* rcTarget is the aspect-ratio-corrected size of the video. */
    width = vi->rcTarget.right + GetSystemMetrics (SM_CXSIZEFRAME) * 2;
    height = vi->rcTarget.bottom + GetSystemMetrics (SM_CYCAPTION) +
        (GetSystemMetrics (SM_CYSIZEFRAME) * 2);

    SystemParametersInfo (SPI_GETWORKAREA, NULL, &rect, 0);
    int screenwidth = rect.right - rect.left;
    int screenheight = rect.bottom - rect.top;
    offx = rect.left;
    offy = rect.top;

    /* Make it fit into the screen without changing the
     * aspect ratio. */
    if (width > screenwidth) {
      double ratio = (double)screenwidth/(double)width;
      width = screenwidth;
      height = (int)(height * ratio);
    }
    if (height > screenheight) {
      double ratio = (double)screenheight/(double)height;
      height = screenheight;
      width = (int)(width * ratio);
    }

    style = WS_OVERLAPPEDWINDOW; /* Normal top-level window */
    exstyle = 0;
  }

  HWND video_window = CreateWindowEx (exstyle, L"GST-DShowSink",
      L"GStreamer DirectShow sink default window",
      style, offx, offy, width, height, NULL, NULL,
      WndClass.hInstance, NULL);
  if (video_window == NULL) {
    GST_ERROR_OBJECT (sink, "Failed to create window!");
    return NULL;
  }

  SetWindowLongPtr (video_window, GWLP_USERDATA, (LONG)sink);

  /* signal application we created a window */
  gst_x_overlay_got_xwindow_id (GST_X_OVERLAY (sink),
      (gulong)video_window);

  /* Set the renderer's clipping window */
  if (!sink->renderersupport->SetRendererWindow (video_window)) {
    GST_WARNING_OBJECT (sink, "Failed to set video clipping window on filter %p", sink->renderersupport);
  }

  /* Now show the window, as appropriate */
  if (sink->full_screen) {
    ShowWindow (video_window, SW_SHOWMAXIMIZED);
    ShowCursor (FALSE);
  }
  else
    ShowWindow (video_window, SW_SHOWNORMAL);

  /* Trigger the initial paint of the window */
  UpdateWindow (video_window);

  ReleaseSemaphore (sink->window_created_signal, 1, NULL);

  /* start message loop processing our default window messages */
  while (1) {
    MSG msg;

    if (GetMessage (&msg, video_window, 0, 0) <= 0) {
      GST_LOG_OBJECT (sink, "our window received WM_QUIT or error.");
      break;
    }
    DispatchMessage (&msg);
  }

  return NULL;
}

static gboolean
gst_dshowvideosink_create_default_window (GstDshowVideoSink * sink)
{
  sink->window_created_signal = CreateSemaphore (NULL, 0, 1, NULL);
  if (sink->window_created_signal == NULL)
    goto failed;

  sink->window_thread = g_thread_create (
      (GThreadFunc) gst_dshowvideosink_window_thread, sink, TRUE, NULL);

  /* wait maximum 10 seconds for window to be created */
  if (WaitForSingleObject (sink->window_created_signal,
          10000) != WAIT_OBJECT_0)
    goto failed;

  CloseHandle (sink->window_created_signal);
  return TRUE;

failed:
  CloseHandle (sink->window_created_signal);
  GST_ELEMENT_ERROR (sink, RESOURCE, WRITE,
      ("Error creating our default window"), (NULL));

  return FALSE;
}

static void gst_dshowvideosink_set_window_id (GstXOverlay * overlay, ULONG window_id)
{
  GstDshowVideoSink *sink = GST_DSHOWVIDEOSINK (overlay);
  HWND videowindow = (HWND)window_id;

  if (videowindow == sink->window_id) {
    GST_DEBUG_OBJECT (sink, "Window already set");
    return;
  }

  /* TODO: What if we already have a window? What if we're already playing? */
  sink->window_id = videowindow;
}

static void gst_dshowvideosink_set_window_for_renderer (GstDshowVideoSink *sink)
{
  /* Application has requested a specific window ID */
  sink->prevWndProc = (WNDPROC) SetWindowLong (sink->window_id, GWL_WNDPROC, (LONG)WndProcHook);
  GST_DEBUG_OBJECT (sink, "Set wndproc to %p from %p", WndProcHook, sink->prevWndProc);
  SetProp (sink->window_id, L"GstDShowVideoSink", sink);
  /* This causes the new WNDPROC to become active */
  SetWindowPos (sink->window_id, 0, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);

  if (!sink->renderersupport->SetRendererWindow (sink->window_id)) {
    GST_WARNING_OBJECT (sink, "Failed to set HWND %x on renderer", sink->window_id);
    return;
  }

  /* This tells the renderer where the window is located, needed to 
   * start drawing in the right place.  */
  sink->renderersupport->MoveWindow();
  GST_INFO_OBJECT (sink, "Set renderer window to %x", sink->window_id);
}

static void
gst_dshowvideosink_prepare_window (GstDshowVideoSink *sink)
{
  /* Give the app a last chance to supply a window id */
  if (!sink->window_id) {
    gst_x_overlay_prepare_xwindow_id (GST_X_OVERLAY (sink));
  }

  /* If the app supplied one, use it. Otherwise, go ahead
   * and create (and use) our own window */
  if (sink->window_id) {
    gst_dshowvideosink_set_window_for_renderer (sink);
  }
  else {
    gst_dshowvideosink_create_default_window (sink);
  }
}

static gboolean
gst_dshowvideosink_connect_graph (GstDshowVideoSink *sink)
{
  HRESULT hres;
  IPin *srcpin;
  IPin *sinkpin;

  GST_INFO_OBJECT (sink, "Connecting DirectShow pins");

  srcpin = sink->fakesrc->GetOutputPin();

  gst_dshow_get_pin_from_filter (sink->renderersupport->GetFilter(), PINDIR_INPUT, 
      &sinkpin);
  if (!sinkpin) {
    GST_WARNING_OBJECT (sink, "Cannot get input pin from Renderer");
    return FALSE;
  }

  /* Be warned that this call WILL deadlock unless you call it from
   * the main thread. Thus, we call this from the state change, not from
   * setcaps (which happens in a streaming thread).
   */
  hres = sink->filter_graph->ConnectDirect (
           srcpin, sinkpin, NULL);
  if (FAILED (hres)) {
    GST_WARNING_OBJECT (sink, "Could not connect pins: %x", hres);
    sinkpin->Release();
    return FALSE;
  }
  sinkpin->Release();
  return TRUE;
}

static GstStateChangeReturn
gst_dshowvideosink_start_graph (GstDshowVideoSink *sink)
{
  IMediaControl *control = NULL;
  HRESULT hres;
  GstStateChangeReturn ret;

  GST_DEBUG_OBJECT (sink, "Connecting and starting DirectShow graph");

  if (!sink->connected) {
    /* This is fine; this just means we haven't connected yet.
     * That's normal for the first time this is called. 
     * So, create a window (or start using an application-supplied
     * one, then connect the graph */
    gst_dshowvideosink_prepare_window (sink);
    if (!gst_dshowvideosink_connect_graph (sink)) {
      ret = GST_STATE_CHANGE_FAILURE;
      goto done;
    }
    sink->connected = TRUE;
  }

  hres = sink->filter_graph->QueryInterface(
          IID_IMediaControl, (void **) &control);

  if (FAILED (hres)) {
    GST_WARNING_OBJECT (sink, "Failed to get IMediaControl interface");
    ret = GST_STATE_CHANGE_FAILURE;
    goto done;
  }

  GST_INFO_OBJECT (sink, "Running DirectShow graph");
  hres = control->Run();
  if (FAILED (hres)) {
    GST_ERROR_OBJECT (sink,
        "Failed to run the directshow graph (error=%x)", hres);
    ret = GST_STATE_CHANGE_FAILURE;
    goto done;
  }
  
  GST_DEBUG_OBJECT (sink, "DirectShow graph is now running");
  ret = GST_STATE_CHANGE_SUCCESS;

done:
  if (control)
    control->Release();

  return ret;
}
static GstStateChangeReturn
gst_dshowvideosink_pause_graph (GstDshowVideoSink *sink)
{
  IMediaControl *control = NULL;
  GstStateChangeReturn ret;
  HRESULT hres;

  hres = sink->filter_graph->QueryInterface(
          IID_IMediaControl, (void **) &control);
  if (FAILED (hres)) {
    GST_WARNING_OBJECT (sink, "Failed to get IMediaControl interface");
    ret = GST_STATE_CHANGE_FAILURE;
    goto done;
  }

  GST_INFO_OBJECT (sink, "Pausing DirectShow graph");
  hres = control->Pause();
  if (FAILED (hres)) {
    GST_WARNING_OBJECT (sink,
        "Can't pause the directshow graph (error=%x)", hres);
    ret = GST_STATE_CHANGE_FAILURE;
    goto done;
  }

  ret = GST_STATE_CHANGE_SUCCESS;

done:
  if (control)
    control->Release();

  return ret;
}

static GstStateChangeReturn
gst_dshowvideosink_stop_graph (GstDshowVideoSink *sink)
{
  IMediaControl *control = NULL;
  GstStateChangeReturn ret;
  HRESULT hres;
  IPin *sinkpin;

  hres = sink->filter_graph->QueryInterface(
          IID_IMediaControl, (void **) &control);
  if (FAILED (hres)) {
    GST_WARNING_OBJECT (sink, "Failed to get IMediaControl interface");
    ret = GST_STATE_CHANGE_FAILURE;
    goto done;
  }

  GST_INFO_OBJECT (sink, "Stopping DirectShow graph");
  hres = control->Stop();
  if (FAILED (hres)) {
    GST_WARNING_OBJECT (sink,
        "Can't stop the directshow graph (error=%x)", hres);
    ret = GST_STATE_CHANGE_FAILURE;
    goto done;
  }

  sink->filter_graph->Disconnect(sink->fakesrc->GetOutputPin());

  gst_dshow_get_pin_from_filter (sink->renderersupport->GetFilter(), PINDIR_INPUT, 
      &sinkpin);
  sink->filter_graph->Disconnect(sinkpin);
  sinkpin->Release();

  GST_DEBUG_OBJECT (sink, "DirectShow graph has stopped");

  if (sink->window_id) {
    /* Return control of application window */
    SetWindowLong (sink->window_id, GWL_WNDPROC, (LONG)sink->prevWndProc);
    RemoveProp (sink->window_id, L"GstDShowVideoSink");
    SetWindowPos (sink->window_id, 0, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
    sink->prevWndProc = NULL;
  }
  sink->connected = FALSE;

  ret = GST_STATE_CHANGE_SUCCESS;

done:
  if (control)
    control->Release();

  return ret;
}

static GstStateChangeReturn
gst_dshowvideosink_change_state (GstElement * element, GstStateChange transition)
{
  GstDshowVideoSink *sink = GST_DSHOWVIDEOSINK (element);
  GstStateChangeReturn ret;

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      break;
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
      ret = gst_dshowvideosink_start_graph (sink);
      if (ret != GST_STATE_CHANGE_SUCCESS)
        return ret;
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
      ret = gst_dshowvideosink_pause_graph (sink);
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      ret = gst_dshowvideosink_stop_graph (sink);
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      gst_dshowvideosink_clear (sink);
      break;
  }

  return ret;
}

class VMR9Support : public RendererSupport
{
private:
  GstDshowVideoSink *sink;
  IBaseFilter *filter;
  IVMRWindowlessControl9 *control;
  IVMRFilterConfig9 *config;
  HWND video_window;

public:
  VMR9Support (GstDshowVideoSink *sink) : 
      sink(sink), 
      filter(NULL),
      control(NULL),
      config(NULL)
  {
  }

  ~VMR9Support() {
    if (control)
      control->Release();
    if (config)
      config->Release();
    if (filter)
      filter->Release();
  }

  const char *GetName() {
    return "VideoMixingRenderer9";
  }

  IBaseFilter *GetFilter() {
    return filter;
  }

  gboolean Configure() {
    HRESULT hres;

    hres = CoCreateInstance (CLSID_VideoMixingRenderer9, NULL, CLSCTX_INPROC,
        IID_IBaseFilter, (LPVOID *) &filter);
    if (FAILED (hres)) {
      GST_ERROR_OBJECT (sink, 
          "Can't create an instance of renderer (error=%x)",
          hres);
      return FALSE;
    }

    hres = filter->QueryInterface (
          IID_IVMRFilterConfig9, (void **) &config);
    if (FAILED (hres)) {
      GST_WARNING_OBJECT (sink, "VMR9 filter config interface missing: %x", hres);
      return FALSE;
    }

    hres = config->SetRenderingMode (VMR9Mode_Windowless);
    if (FAILED (hres)) {
      GST_WARNING_OBJECT (sink, "VMR9 couldn't be set to windowless mode: %x", hres);
      return FALSE;
    }
    else {
      GST_DEBUG_OBJECT (sink, "Set VMR9 (%p) to windowless mode!", filter);
    }

    /* We can't QI to this until _after_ we've been set to windowless mode. 
     * Apparently this is against the rules in COM, but that's how it is... */
    hres = filter->QueryInterface (
          IID_IVMRWindowlessControl9, (void **) &control);
    if (FAILED (hres)) {
      GST_WARNING_OBJECT (sink, "VMR9 windowless control interface missing: %x", hres);
      return FALSE;
    }

    if (sink->keep_aspect_ratio) {
      control->SetAspectRatioMode(VMR9ARMode_LetterBox);
    }
    else {
      control->SetAspectRatioMode(VMR9ARMode_None);
    }
    return TRUE;
  }

  gboolean SetRendererWindow(HWND window) {
    video_window = window;
    HRESULT hres = control->SetVideoClippingWindow (video_window);
    if (FAILED (hres)) {
      GST_WARNING_OBJECT (sink, "Failed to set video clipping window on filter %p: %x", filter, hres);
      return FALSE;
    }
    return TRUE;
  }

  void PaintWindow()
  {
    HRESULT hr;
    PAINTSTRUCT ps;
    HDC         hdc;
    RECT        rcClient;

    GetClientRect(video_window, &rcClient);
    hdc = BeginPaint(video_window, &ps);

    hr = control->RepaintVideo(video_window, hdc);

    EndPaint(video_window, &ps);
  }

  void MoveWindow()
  {
    HRESULT hr;
    RECT rect;

    // Track the movement of the container window and resize as needed
    GetClientRect(video_window, &rect);
    hr = control->SetVideoPosition(NULL, &rect);
  }

  void DisplayModeChanged() {
    control->DisplayModeChanged();
  }

  void DestroyWindow() {
    ::DestroyWindow (video_window);
  }
};

class VMR7Support : public RendererSupport
{
private:
  GstDshowVideoSink *sink;
  IBaseFilter *filter;
  IVMRWindowlessControl *control;
  IVMRFilterConfig *config;
  HWND video_window;

public:
  VMR7Support (GstDshowVideoSink *sink) : 
      sink(sink), 
      filter(NULL),
      control(NULL),
      config(NULL)
  {
  }

  ~VMR7Support() {
    if (control)
      control->Release();
    if (config)
      config->Release();
    if (filter)
      filter->Release();
  }

  const char *GetName() {
    return "VideoMixingRenderer";
  }

  IBaseFilter *GetFilter() {
    return filter;
  }

  gboolean Configure() {
    HRESULT hres;

    hres = CoCreateInstance (CLSID_VideoMixingRenderer, NULL, CLSCTX_INPROC,
        IID_IBaseFilter, (LPVOID *) &filter);
    if (FAILED (hres)) {
      GST_ERROR_OBJECT (sink, 
          "Can't create an instance of renderer (error=%x)",
          hres);
      return FALSE;
    }

    hres = filter->QueryInterface (
          IID_IVMRFilterConfig, (void **) &config);
    if (FAILED (hres)) {
      GST_WARNING_OBJECT (sink, "VMR filter config interface missing: %x", hres);
      return FALSE;
    }

    hres = config->SetRenderingMode (VMRMode_Windowless);
    if (FAILED (hres)) {
      GST_WARNING_OBJECT (sink, "VMR couldn't be set to windowless mode: %x", hres);
      return FALSE;
    }
    else {
      GST_DEBUG_OBJECT (sink, "Set VMR (%p) to windowless mode!", filter);
    }

    hres = filter->QueryInterface (
          IID_IVMRWindowlessControl, (void **) &control);
    if (FAILED (hres)) {
      GST_WARNING_OBJECT (sink, "VMR windowless control interface missing: %x", hres);
      return FALSE;
    }

    if (sink->keep_aspect_ratio) {
      control->SetAspectRatioMode(VMR_ARMODE_LETTER_BOX);
    }
    else {
      control->SetAspectRatioMode(VMR_ARMODE_NONE);
    }
    return TRUE;
  }

  gboolean SetRendererWindow(HWND window) {
    video_window = window;
    HRESULT hres = control->SetVideoClippingWindow (video_window);
    if (FAILED (hres)) {
      GST_WARNING_OBJECT (sink, "Failed to set video clipping window on filter %p: %x", filter, hres);
      return FALSE;
    }
    return TRUE;
  }

  void PaintWindow()
  {
    HRESULT hr;
    PAINTSTRUCT ps;
    HDC         hdc;
    RECT        rcClient;

    GetClientRect(video_window, &rcClient);
    hdc = BeginPaint(video_window, &ps);

    hr = control->RepaintVideo(video_window, hdc);

    EndPaint(video_window, &ps);
  }

  void MoveWindow()
  {
    HRESULT hr;
    RECT rect;

    // Track the movement of the container window and resize as needed
    GetClientRect(video_window, &rect);
    hr = control->SetVideoPosition(NULL, &rect);
  }

  void DisplayModeChanged() {
    control->DisplayModeChanged();
  }

  void DestroyWindow() {
    ::DestroyWindow (video_window);
  }
};

static gboolean 
gst_dshowvideosink_create_renderer (GstDshowVideoSink *sink) 
{
  GST_DEBUG_OBJECT (sink, "Trying to create renderer '%s'", "VMR9");

  RendererSupport *support = NULL;

  if (sink->preferredrenderer) {
    if (!strcmp (sink->preferredrenderer, "VMR9")) {
      GST_INFO_OBJECT (sink, "Forcing use of VMR9");
      support = new VMR9Support (sink);
    }
    else if (!strcmp (sink->preferredrenderer, "VMR")) {
      GST_INFO_OBJECT (sink, "Forcing use of VMR");
      support = new VMR7Support (sink);
    }
    else {
      GST_ERROR_OBJECT (sink, "Unknown sink type '%s'", sink->preferredrenderer);
      return FALSE;
    }

    if (!support->Configure()) {
      GST_ERROR_OBJECT (sink, "Couldn't configure selected renderer");
      delete support;
      return FALSE;
    }
    goto done;
  }

  support = new VMR9Support (sink);
  if (!support->Configure()) {
    GST_INFO_OBJECT (sink, "Failed to configure VMR9, trying VMR7");
    delete support;
    support = new VMR7Support (sink);
    if (!support->Configure()) {
      GST_ERROR_OBJECT (sink, "Failed to configure VMR9 or VMR7");
      delete support;
      return FALSE;
    }
  }

done:
  sink->renderersupport = support;
  return TRUE;
}

static gboolean
gst_dshowvideosink_build_filtergraph (GstDshowVideoSink *sink)
{
  HRESULT hres;

  /* Build our DirectShow FilterGraph, looking like: 
   *
   *    [ fakesrc ] -> [ sink filter ]
   *
   * so we can feed data in through the fakesrc.
   *
   * The sink filter can be one of our supported filters: VMR9 (VMR7?, EMR?)
   */

  hres = CoCreateInstance (CLSID_FilterGraph, NULL, CLSCTX_INPROC,
      IID_IFilterGraph, (LPVOID *) & sink->filter_graph);
  if (FAILED (hres)) {
    GST_ERROR_OBJECT (sink, 
          "Can't create an instance of the dshow graph manager (error=%x)", hres);
    goto error;
  }

  sink->fakesrc = new VideoFakeSrc();

  IBaseFilter *filter;
  hres = sink->fakesrc->QueryInterface (
          IID_IBaseFilter, (void **) &filter);
  if (FAILED (hres)) {
    GST_ERROR_OBJECT (sink, "Could not QI fakesrc to IBaseFilter");
    goto error;
  }

  hres = sink->filter_graph->AddFilter (filter, L"fakesrc");
  if (FAILED (hres)) {
    GST_ERROR_OBJECT (sink,
        "Can't add our fakesrc filter to the graph (error=%x)", hres);
    goto error;
  }

  if (!gst_dshowvideosink_create_renderer (sink)) {
    GST_ERROR_OBJECT (sink, "Could not create a video renderer");
    goto error;
  }

  /* dump_all_pin_media_types (sink->renderer); */

  hres =
      sink->filter_graph->AddFilter (sink->renderersupport->GetFilter(),
      L"renderer");
  if (FAILED (hres)) {
    GST_ERROR_OBJECT (sink, 
          "Can't add renderer to the graph (error=%x)", hres);
    goto error;
  }

  return TRUE;

error:
  if (sink->fakesrc) {
    sink->fakesrc->Release();
    sink->fakesrc = NULL;
  }

  if (sink->filter_graph) {
    sink->filter_graph->Release();
    sink->filter_graph = NULL;
  }

  return FALSE;
}

static gboolean
gst_dshowvideosink_start (GstBaseSink * bsink)
{
  HRESULT hres = S_FALSE;
  GstDshowVideoSink *sink = GST_DSHOWVIDEOSINK (bsink);

  /* Just build the filtergraph; we don't link or otherwise configure it yet */
  return gst_dshowvideosink_build_filtergraph (sink);
}

static gboolean
gst_dshowvideosink_set_caps (GstBaseSink * bsink, GstCaps * caps)
{
  GstDshowVideoSink *sink = GST_DSHOWVIDEOSINK (bsink);

  /* TODO: What do we want to do if the caps change while we're running?
   * Find out if we can handle this or not... */

  if (!gst_caps_to_directshow_media_type (caps, &sink->mediatype)) {
    GST_WARNING_OBJECT (sink, "Cannot convert caps to AM_MEDIA_TYPE, rejecting");
    return FALSE;
  }

  /* Now we have an AM_MEDIA_TYPE describing what we're going to send.
   * We set this on our DirectShow fakesrc's output pin. 
   */
  sink->fakesrc->GetOutputPin()->SetMediaType (&sink->mediatype);

  return TRUE;
}

static gboolean
gst_dshowvideosink_stop (GstBaseSink * bsink)
{
  IPin *input_pin = NULL, *output_pin = NULL;
  HRESULT hres = S_FALSE;
  GstDshowVideoSink *sink = GST_DSHOWVIDEOSINK (bsink);

  if (!sink->filter_graph) {
    GST_WARNING_OBJECT (sink, "Cannot destroy filter graph; it doesn't exist");
    return TRUE;
  }

  /* Release the renderer */
  if (sink->renderersupport) {
    delete sink->renderersupport;
    sink->renderersupport = NULL;
  }

  /* Release our dshow fakesrc */
  if (sink->fakesrc) {
    sink->fakesrc->Release();
    sink->fakesrc = NULL;
  }

  /* Release the filter graph manager */
  if (sink->filter_graph) {
    sink->filter_graph->Release();
    sink->filter_graph = NULL;
  }

  return TRUE;
}

static GstFlowReturn 
gst_dshowvideosink_render (GstBaseSink *bsink, GstBuffer *buffer)
{
  GstDshowVideoSink *sink = GST_DSHOWVIDEOSINK (bsink);
  GstFlowReturn ret;

  if (sink->window_closed) {
    GST_WARNING_OBJECT (sink, "Window has been closed, stopping");
    return GST_FLOW_ERROR;
  }

  GST_DEBUG_OBJECT (sink, "Pushing buffer through fakesrc->renderer");
  ret = sink->fakesrc->GetOutputPin()->PushBuffer (buffer);
  GST_DEBUG_OBJECT (sink, "Done pushing buffer through fakesrc->renderer");

  return ret;
}

/* TODO: How can we implement these? Figure that out... */
static gboolean
gst_dshowvideosink_unlock (GstBaseSink * bsink)
{
  GstDshowVideoSink *sink = GST_DSHOWVIDEOSINK (bsink);

  return TRUE;
}

static gboolean
gst_dshowvideosink_unlock_stop (GstBaseSink * bsink)
{
  GstDshowVideoSink *sink = GST_DSHOWVIDEOSINK (bsink);

  return TRUE;
}

/* TODO: Move all of this into generic code? */

/* Helpers to format GUIDs the same way we find them in the source */
#define GUID_FORMAT "{%.8x, %.4x, %.4x, { %.2x, %.2x, %.2x, %.2x, %.2x, %.2x, %.2x, %.2x }}"
#define GUID_ARGS(guid) \
    guid.Data1, guid.Data2, guid.Data3, \
    guid.Data4[0], guid.Data4[1], guid.Data4[3], guid.Data4[4], \
    guid.Data4[5], guid.Data4[6], guid.Data4[7], guid.Data4[8]

static GstCaps *
audio_media_type_to_caps (AM_MEDIA_TYPE *mediatype)
{
  return NULL;
}

static GstCaps *
video_media_type_to_caps (AM_MEDIA_TYPE *mediatype)
{
  GstCaps *caps = NULL;

  /* TODO: Add  RGB types. */
  if (IsEqualGUID (mediatype->subtype, MEDIASUBTYPE_YUY2))
    caps = gst_caps_new_simple ("video/x-raw-yuv", 
            "format", GST_TYPE_FOURCC, GST_MAKE_FOURCC ('Y', 'U', 'Y', '2'), NULL);
  else if (IsEqualGUID (mediatype->subtype, MEDIASUBTYPE_UYVY))
    caps = gst_caps_new_simple ("video/x-raw-yuv", 
            "format", GST_TYPE_FOURCC, GST_MAKE_FOURCC ('U', 'Y', 'V', 'Y'), NULL);
  else if (IsEqualGUID (mediatype->subtype, MEDIASUBTYPE_YUYV))
    caps = gst_caps_new_simple ("video/x-raw-yuv", 
            "format", GST_TYPE_FOURCC, GST_MAKE_FOURCC ('Y', 'U', 'Y', 'V'), NULL);
  else if (IsEqualGUID (mediatype->subtype, MEDIASUBTYPE_YV12))
    caps = gst_caps_new_simple ("video/x-raw-yuv", 
            "format", GST_TYPE_FOURCC, GST_MAKE_FOURCC ('Y', 'V', '1', '2'), NULL);

  if (!caps) {
    GST_DEBUG ("No subtype known; cannot continue");
    return NULL;
  }

  if (IsEqualGUID (mediatype->formattype, FORMAT_VideoInfo) &&
          mediatype->cbFormat >= sizeof(VIDEOINFOHEADER))
  {
    VIDEOINFOHEADER *vh = (VIDEOINFOHEADER *)mediatype->pbFormat;

	/* TODO: Set PAR here. Based on difference between source and target RECTs? 
     *       Do we want framerate? Based on AvgTimePerFrame? */
    gst_caps_set_simple (caps, 
            "width", G_TYPE_INT, vh->bmiHeader.biWidth,
            "height", G_TYPE_INT, vh->bmiHeader.biHeight,
			NULL);
  }

  return caps;
}


/* Create a GstCaps object representing the same media type as
 * this AM_MEDIA_TYPE.
 *
 * Returns NULL if no corresponding GStreamer type is known.
 *
 * May modify mediatype.
 */
static GstCaps *
gst_directshow_media_type_to_caps (AM_MEDIA_TYPE *mediatype)
{
  GstCaps *caps = NULL;

  if (IsEqualGUID (mediatype->majortype, MEDIATYPE_Video))
    caps = video_media_type_to_caps (mediatype);
  else if (IsEqualGUID (mediatype->majortype, MEDIATYPE_Audio))
    caps = audio_media_type_to_caps (mediatype);
  else {
    GST_DEBUG ("Non audio/video media types not yet recognised, please add me: "
            GUID_FORMAT, GUID_ARGS(mediatype->majortype));
  }

  if (caps) {
    gchar *capsstring = gst_caps_to_string (caps);
    GST_DEBUG ("Converted AM_MEDIA_TYPE to \"%s\"", capsstring);
    g_free (capsstring);
  }
  else {
    GST_WARNING ("Failed to convert AM_MEDIA_TYPE to caps");
  }

  return caps;
}

/* Fill in a DirectShow AM_MEDIA_TYPE structure representing the same media
 * type as this GstCaps object.
 *
 * Returns FALSE if no corresponding type is known.
 *
 * Only operates on simple (single structure) caps.
 */
static gboolean
gst_caps_to_directshow_media_type (GstCaps *caps, AM_MEDIA_TYPE *mediatype)
{
  GstStructure *s = gst_caps_get_structure (caps, 0);
  const gchar *name = gst_structure_get_name (s);

  gchar *capsstring = gst_caps_to_string (caps);
  GST_DEBUG ("Converting caps \"%s\" to AM_MEDIA_TYPE", capsstring);
  g_free (capsstring);

  memset (mediatype, 0, sizeof (AM_MEDIA_TYPE));

  if (!strcmp (name, "video/x-raw-yuv")) {
    guint32 fourcc;
	int width, height;
    int bpp;

    if (!gst_structure_get_fourcc (s, "format", &fourcc)) {
      GST_WARNING ("Failed to convert caps, no fourcc");
      return FALSE;
    }

	if (!gst_structure_get_int (s, "width", &width)) {
      GST_WARNING ("Failed to convert caps, no width");
      return FALSE;
    }
	if (!gst_structure_get_int (s, "height", &height)) {
      GST_WARNING ("Failed to convert caps, no height");
      return FALSE;
    }

    mediatype->majortype = MEDIATYPE_Video;
    switch (fourcc) {
      case GST_MAKE_FOURCC ('Y', 'U', 'Y', '2'):
        mediatype->subtype = MEDIASUBTYPE_YUY2;
        bpp = 16;
        break;
      case GST_MAKE_FOURCC ('Y', 'U', 'Y', 'V'):
        mediatype->subtype = MEDIASUBTYPE_YUYV;
        bpp = 16;
        break;
      case GST_MAKE_FOURCC ('U', 'Y', 'V', 'Y'):
        mediatype->subtype = MEDIASUBTYPE_UYVY;
        bpp = 16;
        break;
      case GST_MAKE_FOURCC ('Y', 'V', '1', '2'):
        mediatype->subtype = MEDIASUBTYPE_YV12;
        bpp = 12;
        break;
      default:
        GST_WARNING ("Failed to convert caps, not a known fourcc");
        return FALSE;
    }

    mediatype->bFixedSizeSamples = TRUE; /* Always true for raw video */
    mediatype->bTemporalCompression = FALSE; /* Likewise, always false */

    {
      int par_n, par_d;
      VIDEOINFOHEADER *vi = (VIDEOINFOHEADER *)CoTaskMemAlloc (sizeof (VIDEOINFOHEADER));
      memset (vi, 0, sizeof (VIDEOINFOHEADER));

      mediatype->formattype = FORMAT_VideoInfo;
      mediatype->cbFormat = sizeof (VIDEOINFOHEADER);
      mediatype->pbFormat = (BYTE *)vi;

      mediatype->lSampleSize = width * height * bpp / 8;

      GST_INFO ("Set mediatype format: size %d, sample size %d", mediatype->cbFormat, mediatype->lSampleSize);

      vi->rcSource.top = 0;
      vi->rcSource.left = 0;
      vi->rcSource.bottom = height;
      vi->rcSource.right = width;

      vi->rcTarget.top = 0;
      vi->rcTarget.left = 0;
      if (gst_structure_get_fraction (s, "pixel-aspect-ratio", &par_n, &par_d)) {
        /* To handle non-square pixels, we set the target rectangle to a 
         * different size than the source rectangle.
         * There might be a better way, but this seems to work. */
        vi->rcTarget.bottom = height;
        vi->rcTarget.right = width * par_n / par_d;
        GST_DEBUG ("Got PAR: set target right to %d from width %d", vi->rcTarget.right, width);
      }
      else {
        GST_DEBUG ("No PAR found");
        vi->rcTarget.bottom = height;
        vi->rcTarget.right = width;
      }

      vi->bmiHeader.biSize = sizeof (BITMAPINFOHEADER);
      vi->bmiHeader.biWidth = width;
      vi->bmiHeader.biHeight = -height; /* Required to be negative. */
      vi->bmiHeader.biPlanes = 1; /* Required to be 1 */
      vi->bmiHeader.biBitCount = bpp;
      vi->bmiHeader.biCompression = fourcc;
      vi->bmiHeader.biSizeImage = width * height * bpp / 8;

      /* We can safely zero these; they don't matter for our uses */
      vi->bmiHeader.biXPelsPerMeter = 0;
      vi->bmiHeader.biYPelsPerMeter = 0;
      vi->bmiHeader.biClrUsed = 0;
      vi->bmiHeader.biClrImportant = 0;
    }

    GST_DEBUG ("Successfully built AM_MEDIA_TYPE from caps");
    return TRUE;
  }

  GST_WARNING ("Failed to convert caps, not a known caps type");
  /* Only YUV supported so far */

  return FALSE;
}

/* Plugin entry point */
extern "C" static gboolean
plugin_init (GstPlugin * plugin)
{
  /* PRIMARY: this is the best videosink to use on windows */
  if (!gst_element_register (plugin, "dshowvideosink",
          GST_RANK_PRIMARY, GST_TYPE_DSHOWVIDEOSINK))
    return FALSE;

  return TRUE;
}

extern "C" GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "dshowsinkwrapper",
    "DirectShow sink wrapper plugin",
    plugin_init, VERSION, "LGPL", GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
