/*
 *
 * GStreamer
 * Copyright (C) 2004 Billy Biggs <vektor@dumbterm.net>
 * Copyright (c) 2001, 2002, 2003 Fabrice Bellard.
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
 * This file contains code from ffmpeg, see http://ffmpeg.org/ (LGPL)
 * and modifications by Billy Biggs.
 *
 * Relicensed for GStreamer from GPL to LGPL with permit from Billy Biggs.
 * See: http://bugzilla.gnome.org/show_bug.cgi?id=163578
 */

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include "_stdint.h"
#include "gstdeinterlace2.h"
#include <string.h>

#define GST_TYPE_DEINTERLACE_METHOD_VFIR	(gst_deinterlace_method_vfir_get_type ())
#define GST_IS_DEINTERLACE_METHOD_VFIR(obj)		(G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_DEINTERLACE_METHOD_VFIR))
#define GST_IS_DEINTERLACE_METHOD_VFIR_CLASS(klass)	(G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_DEINTERLACE_METHOD_VFIR))
#define GST_DEINTERLACE_METHOD_VFIR_GET_CLASS(obj)	(G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_DEINTERLACE_METHOD_VFIR, GstDeinterlaceMethodVFIRClass))
#define GST_DEINTERLACE_METHOD_VFIR(obj)		(G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_DEINTERLACE_METHOD_VFIR, GstDeinterlaceMethodVFIR))
#define GST_DEINTERLACE_METHOD_VFIR_CLASS(klass)	(G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_DEINTERLACE_METHOD_VFIR, GstDeinterlaceMethodVFIRClass))
#define GST_DEINTERLACE_METHOD_VFIR_CAST(obj)	((GstDeinterlaceMethodVFIR*)(obj))

GType gst_deinterlace_method_vfir_get_type (void);

typedef GstDeinterlaceMethod GstDeinterlaceMethodVFIR;

typedef struct
{
  GstDeinterlaceMethodClass parent_class;
  void (*scanline) (uint8_t * dst, uint8_t * lum_m4,
      uint8_t * lum_m3, uint8_t * lum_m2,
      uint8_t * lum_m1, uint8_t * lum, int size);
} GstDeinterlaceMethodVFIRClass;

/*
 * The MPEG2 spec uses a slightly harsher filter, they specify
 * [-1 8 2 8 -1].  ffmpeg uses a similar filter but with more of
 * a tendancy to blur than to use the local information.  The
 * filter taps here are: [-1 4 2 4 -1].
 */

/**
  * C implementation.
  */
static inline void
deinterlace_line_c (uint8_t * dst, uint8_t * lum_m4,
    uint8_t * lum_m3, uint8_t * lum_m2,
    uint8_t * lum_m1, uint8_t * lum, int size)
{
  int sum;

  for (; size >= 0; size--) {
    sum = -lum_m4[0];
    sum += lum_m3[0] << 2;
    sum += lum_m2[0] << 1;
    sum += lum_m1[0] << 2;
    sum += -lum[0];
    dst[0] = (sum + 4) >> 3;    // This needs to be clipped at 0 and 255: cm[(sum + 4) >> 3];
    lum_m4++;
    lum_m3++;
    lum_m2++;
    lum_m1++;
    lum++;
    dst++;
  }
}

#ifdef BUILD_X86_ASM
#include "mmx.h"
static void
deinterlace_line_mmx (uint8_t * dst, uint8_t * lum_m4,
    uint8_t * lum_m3, uint8_t * lum_m2,
    uint8_t * lum_m1, uint8_t * lum, int size)
{
  mmx_t rounder;

  rounder.uw[0] = 4;
  rounder.uw[1] = 4;
  rounder.uw[2] = 4;
  rounder.uw[3] = 4;
  pxor_r2r (mm7, mm7);
  movq_m2r (rounder, mm6);

  for (; size > 3; size -= 4) {
    movd_m2r (*lum_m4, mm0);
    movd_m2r (*lum_m3, mm1);
    movd_m2r (*lum_m2, mm2);
    movd_m2r (*lum_m1, mm3);
    movd_m2r (*lum, mm4);
    punpcklbw_r2r (mm7, mm0);
    punpcklbw_r2r (mm7, mm1);
    punpcklbw_r2r (mm7, mm2);
    punpcklbw_r2r (mm7, mm3);
    punpcklbw_r2r (mm7, mm4);
    paddw_r2r (mm3, mm1);
    psllw_i2r (1, mm2);
    paddw_r2r (mm4, mm0);
    psllw_i2r (2, mm1);         // 2
    paddw_r2r (mm6, mm2);
    paddw_r2r (mm2, mm1);
    psubusw_r2r (mm0, mm1);
    psrlw_i2r (3, mm1);         // 3
    packuswb_r2r (mm7, mm1);
    movd_r2m (mm1, *dst);
    lum_m4 += 4;
    lum_m3 += 4;
    lum_m2 += 4;
    lum_m1 += 4;
    lum += 4;
    dst += 4;
  }
  emms ();

  /* Handle odd widths */
  if (size > 0)
    deinterlace_line_c (dst, lum_m4, lum_m3, lum_m2, lum_m1, lum, size);
}
#endif

static void
deinterlace_frame_vfir (GstDeinterlaceMethod * d_method,
    GstDeinterlace2 * object)
{
  GstDeinterlaceMethodVFIRClass *klass =
      GST_DEINTERLACE_METHOD_VFIR_GET_CLASS (d_method);
  gint line = 0;
  uint8_t *cur_field, *last_field;
  uint8_t *t0, *b0, *tt1, *m1, *bb1, *out_data;

  cur_field =
      GST_BUFFER_DATA (object->field_history[object->history_count - 2].buf);
  last_field =
      GST_BUFFER_DATA (object->field_history[object->history_count - 1].buf);

  out_data = GST_BUFFER_DATA (object->out_buf);

  if (object->field_history[object->history_count - 2].flags ==
      PICTURE_INTERLACED_BOTTOM) {
    memcpy (out_data, cur_field, object->line_length);
    out_data += object->output_stride;
  }

  memcpy (out_data, cur_field, object->line_length);
  out_data += object->output_stride;
  line++;

  for (; line < object->field_height; line++) {
    t0 = cur_field;
    b0 = cur_field + object->field_stride;

    tt1 = last_field;
    m1 = last_field + object->field_stride;
    bb1 = last_field + (object->field_stride * 2);

    /* set valid data for corner cases */
    if (line == 1) {
      tt1 = bb1;
    } else if (line == object->field_height - 1) {
      bb1 = tt1;
    }

    klass->scanline (out_data, tt1, t0, m1, b0, bb1, object->line_length);
    out_data += object->output_stride;
    cur_field += object->field_stride;
    last_field += object->field_stride;

    memcpy (out_data, cur_field, object->line_length);
    out_data += object->output_stride;
  }

  if (object->field_history[object->history_count - 2].flags ==
      PICTURE_INTERLACED_TOP) {
    /* double the last scanline of the top field */
    memcpy (out_data, cur_field, object->line_length);
  }
}

G_DEFINE_TYPE (GstDeinterlaceMethodVFIR, gst_deinterlace_method_vfir,
    GST_TYPE_DEINTERLACE_METHOD);

static void
gst_deinterlace_method_vfir_class_init (GstDeinterlaceMethodVFIRClass * klass)
{
  GstDeinterlaceMethodClass *dim_class = (GstDeinterlaceMethodClass *) klass;
#ifdef BUILD_X86_ASM
  guint cpu_flags = oil_cpu_get_flags ();
#endif

  dim_class->fields_required = 2;
  dim_class->deinterlace_frame = deinterlace_frame_vfir;
  dim_class->name = "Blur Vertical";
  dim_class->nick = "vfir";
  dim_class->latency = 0;

#ifdef BUILD_X86_ASM
  if (cpu_flags & OIL_IMPL_FLAG_MMX) {
    klass->scanline = deinterlace_line_mmx;
  } else {
    klass->scanline = deinterlace_line_c;
  }
#else
  klass->scanline = deinterlace_line_c;
#endif
}

static void
gst_deinterlace_method_vfir_init (GstDeinterlaceMethodVFIR * self)
{
}
