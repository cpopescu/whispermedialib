/* GStreamer
 * Copyright (C) <2006> Wim Taymans <wim@fluendo.com>
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

#ifndef __GST_RTP_THEORA_DEPAY_H__
#define __GST_RTP_THEORA_DEPAY_H__

#include <gst/gst.h>
#include <gst/base/gstadapter.h>
#include <gst/rtp/gstbasertpdepayload.h>

G_BEGIN_DECLS

#define GST_TYPE_RTP_THEORA_DEPAY \
  (gst_rtp_theora_depay_get_type())
#define GST_RTP_THEORA_DEPAY(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_RTP_THEORA_DEPAY,GstRtpTheoraDepay))
#define GST_RTP_THEORA_DEPAY_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_RTP_THEORA_DEPAY,GstRtpTheoraDepayClass))
#define GST_IS_RTP_THEORA_DEPAY(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_RTP_THEORA_DEPAY))
#define GST_IS_RTP_THEORA_DEPAY_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_RTP_THEORA_DEPAY))

typedef struct _GstRtpTheoraDepay GstRtpTheoraDepay;
typedef struct _GstRtpTheoraDepayClass GstRtpTheoraDepayClass;

typedef struct _GstRtpTheoraConfig {
  guint32  ident;
  GList   *headers;
} GstRtpTheoraConfig;

struct _GstRtpTheoraDepay
{
  GstBaseRTPDepayload parent;

  GList              *configs;
  GstRtpTheoraConfig *config;

  GstAdapter         *adapter;
  gboolean            assembling;
};

struct _GstRtpTheoraDepayClass
{
  GstBaseRTPDepayloadClass parent_class;
};

gboolean gst_rtp_theora_depay_plugin_init (GstPlugin * plugin);

G_END_DECLS

#endif /* __GST_RTP_THEORA_DEPAY_H__ */
