/* GStreamer
 *
 * Copyright (C) 2002 Ronald Bultje <rbultje@ronald.bitfreak.net>
 *               2006 Edgard Lima <edgard.lima@indt.org.br>
 *
 * v4l2_calls.h - generic V4L2 calls handling
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

#ifndef __V4L2_CALLS_H__
#define __V4L2_CALLS_H__

#include "gstv4l2object.h"
#include "gst/gst-i18n-plugin.h"


/* simple check whether the device is open */
#define GST_V4L2_IS_OPEN(v4l2object) \
  (v4l2object->video_fd > 0)

/* check whether the device is 'active' */
#define GST_V4L2_IS_ACTIVE(v4l2object) \
  (v4l2object->buffer != NULL)

#define GST_V4L2_IS_OVERLAY(v4l2object) \
  (v4l2object->vcap.capabilities & V4L2_CAP_VIDEO_OVERLAY)

/* checks whether the current v4lv4l2object has already been open()'ed or not */
#define GST_V4L2_CHECK_OPEN(v4l2object)				\
  if (!GST_V4L2_IS_OPEN(v4l2object))				\
  {								\
    GST_ELEMENT_ERROR (v4l2object->element, RESOURCE, SETTINGS,	\
      (_("Device is not open.")), (NULL));                      \
    return FALSE;						\
  }

/* checks whether the current v4lv4l2object is close()'ed or whether it is still open */
#define GST_V4L2_CHECK_NOT_OPEN(v4l2object)			\
  if (GST_V4L2_IS_OPEN(v4l2object))				\
  {								\
    GST_ELEMENT_ERROR (v4l2object->element, RESOURCE, SETTINGS,	\
      (_("Device is open.")), (NULL));                          \
    return FALSE;						\
  }

/* checks whether the current v4lv4l2object does video overlay */
#define GST_V4L2_CHECK_OVERLAY(v4l2object)			\
  if (!GST_V4L2_IS_OVERLAY(v4l2object))				\
  {								\
    GST_ELEMENT_ERROR (v4l2object->element, RESOURCE, SETTINGS, \
      (NULL), ("Device cannot handle overlay"));                \
    return FALSE;						\
  }

/* checks whether we're in capture mode or not */
#define GST_V4L2_CHECK_ACTIVE(v4l2object)			\
  if (!GST_V4L2_IS_ACTIVE(v4l2object))				\
  {								\
    GST_ELEMENT_ERROR (v4l2object->element, RESOURCE, SETTINGS, \
      (NULL), ("Device is not in streaming mode"));             \
    return FALSE;						\
  }

/* checks whether we're out of capture mode or not */
#define GST_V4L2_CHECK_NOT_ACTIVE(v4l2object)			\
  if (GST_V4L2_IS_ACTIVE(v4l2object))				\
  {								\
    GST_ELEMENT_ERROR (v4l2object->element, RESOURCE, SETTINGS, \
      (NULL), ("Device is in streaming mode"));                 \
    return FALSE;						\
  }


/* open/close the device */
gboolean	gst_v4l2_open			(GstV4l2Object *v4l2object);
gboolean	gst_v4l2_close			(GstV4l2Object *v4l2object);

/* norm/input/output */
gboolean	gst_v4l2_get_norm		(GstV4l2Object *v4l2object,
						 v4l2_std_id    *norm);
gboolean	gst_v4l2_set_norm		(GstV4l2Object *v4l2object,
						 v4l2_std_id     norm);
gboolean        gst_v4l2_get_input              (GstV4l2Object * v4l2object,
                                                 gint * input);
gboolean        gst_v4l2_set_input              (GstV4l2Object * v4l2object,
                                                 gint input);
#if 0 /* output not handled by now */
gboolean	gst_v4l2_get_output		(GstV4l2Object *v4l2object,
						 gint           *output);
gboolean	gst_v4l2_set_output		(GstV4l2Object *v4l2object,
						 gint            output);
#endif /* #if 0 - output not handled by now */

/* frequency control */
gboolean	gst_v4l2_get_frequency		(GstV4l2Object *v4l2object,
						 gint            tunernum,
						 gulong         *frequency);
gboolean	gst_v4l2_set_frequency		(GstV4l2Object *v4l2object,
						 gint            tunernum,
					 	 gulong          frequency);
gboolean	gst_v4l2_signal_strength	(GstV4l2Object *v4l2object,
						 gint            tunernum,
						 gulong         *signal);

/* attribute control */
gboolean	gst_v4l2_get_attribute		(GstV4l2Object *v4l2object,
						 int             attribute,
						 int            *value);
gboolean	gst_v4l2_set_attribute		(GstV4l2Object *v4l2object,
						 int             attribute,
						 const int       value);

gboolean        gst_v4l2_get_capabilities       (GstV4l2Object * v4l2object);

#endif /* __V4L2_CALLS_H__ */
