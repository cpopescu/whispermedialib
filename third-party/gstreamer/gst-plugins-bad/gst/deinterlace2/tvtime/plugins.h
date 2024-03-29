/*
 *
 * GStreamer
 * Copyright (C) 2004 Billy Biggs <vektor@dumbterm.net>
 * Copyright (C) 2008 Sebastian Dröge <slomo@collabora.co.uk>
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

/*
 * Relicensed for GStreamer from GPL to LGPL with permit from Billy Biggs.
 * See: http://bugzilla.gnome.org/show_bug.cgi?id=163578
 */

#ifndef TVTIME_PLUGINS_H_INCLUDED
#define TVTIME_PLUGINS_H_INCLUDED

#define GST_TYPE_DEINTERLACE_TOMSMOCOMP (gst_deinterlace_method_tomsmocomp_get_type ())
#define GST_TYPE_DEINTERLACE_GREEDY_H (gst_deinterlace_method_greedy_h_get_type ())
#define GST_TYPE_DEINTERLACE_GREEDY_L (gst_deinterlace_method_greedy_l_get_type ())
#define GST_TYPE_DEINTERLACE_VFIR (gst_deinterlace_method_vfir_get_type ())

GType gst_deinterlace_method_tomsmocomp_get_type (void);
GType gst_deinterlace_method_greedy_h_get_type (void);
GType gst_deinterlace_method_greedy_l_get_type (void);
GType gst_deinterlace_method_vfir_get_type (void);

//void linear_plugin_init( void );
//void scalerbob_plugin_init( void );
//void linearblend_plugin_init( void );
//void weave_plugin_init( void );
//void weavetff_plugin_init( void );
//void weavebff_plugin_init( void );

#endif /* TVTIME_PLUGINS_H_INCLUDED */
