/* GStreamer
 * Copyright (C) 2005 Stefan Kost <ensonic@users.sf.net>
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

#ifndef __GST_AUDIO_TEST_SRC_H__
#define __GST_AUDIO_TEST_SRC_H__


#include <gst/gst.h>
#include <gst/base/gstbasesrc.h>

G_BEGIN_DECLS


#define GST_TYPE_AUDIO_TEST_SRC \
  (gst_audio_test_src_get_type())
#define GST_AUDIO_TEST_SRC(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_AUDIO_TEST_SRC,GstAudioTestSrc))
#define GST_AUDIO_TEST_SRC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_AUDIO_TEST_SRC,GstAudioTestSrcClass))
#define GST_IS_AUDIO_TEST_SRC(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_AUDIO_TEST_SRC))
#define GST_IS_AUDIO_TEST_SRC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_AUDIO_TEST_SRC))

/**
 * GstAudioTestSrcWave:
 * @GST_AUDIO_TEST_SRC_WAVE_SINE: a sine wave
 * @GST_AUDIO_TEST_SRC_WAVE_SQUARE: a square wave
 * @GST_AUDIO_TEST_SRC_WAVE_SAW: a saw wave
 * @GST_AUDIO_TEST_SRC_WAVE_TRIANGLE: a tringle wave
 * @GST_AUDIO_TEST_SRC_WAVE_SILENCE: silence
 * @GST_AUDIO_TEST_SRC_WAVE_WHITE_NOISE: white noise
 * @GST_AUDIO_TEST_SRC_WAVE_PINK_NOISE: pink noise
 * @GST_AUDIO_TEST_SRC_WAVE_SINE_TAB: sine wave using a table
 *
 * Different types of supported sound waves.
 */
typedef enum {
  GST_AUDIO_TEST_SRC_WAVE_SINE,
  GST_AUDIO_TEST_SRC_WAVE_SQUARE,
  GST_AUDIO_TEST_SRC_WAVE_SAW,
  GST_AUDIO_TEST_SRC_WAVE_TRIANGLE,
  GST_AUDIO_TEST_SRC_WAVE_SILENCE,
  GST_AUDIO_TEST_SRC_WAVE_WHITE_NOISE,
  GST_AUDIO_TEST_SRC_WAVE_PINK_NOISE,
  GST_AUDIO_TEST_SRC_WAVE_SINE_TAB
} GstAudioTestSrcWave; 

#define PINK_MAX_RANDOM_ROWS   (30)
#define PINK_RANDOM_BITS       (16)
#define PINK_RANDOM_SHIFT      ((sizeof(long)*8)-PINK_RANDOM_BITS)

typedef struct {
  glong      rows[PINK_MAX_RANDOM_ROWS];
  glong      running_sum;   /* Used to optimize summing of generators. */
  gint       index;         /* Incremented each sample. */
  gint       index_mask;    /* Index wrapped by ANDing with this mask. */
  gdouble    scalar;        /* Used to scale within range of -1.0 to +1.0 */
} GstPinkNoise;

typedef enum {
  GST_AUDIO_TEST_SRC_FORMAT_NONE = -1,
  GST_AUDIO_TEST_SRC_FORMAT_S16 = 0,
  GST_AUDIO_TEST_SRC_FORMAT_S32,
  GST_AUDIO_TEST_SRC_FORMAT_F32,
  GST_AUDIO_TEST_SRC_FORMAT_F64
} GstAudioTestSrcFormat;

typedef struct _GstAudioTestSrc GstAudioTestSrc;
typedef struct _GstAudioTestSrcClass GstAudioTestSrcClass;

typedef void (*ProcessFunc) (GstAudioTestSrc*, guint8 *);

/**
 * GstAudioTestSrc:
 *
 * audiotestsrc object structure.
 */
struct _GstAudioTestSrc {
  GstBaseSrc parent;

  ProcessFunc process;

  /* parameters */
  GstAudioTestSrcWave wave;
  gdouble volume;
  gdouble freq;
    
  /* audio parameters */
  gint samplerate;
  gint samples_per_buffer;
  GstAudioTestSrcFormat format;
  
  /*< private >*/
  gboolean tags_pushed;			/* send tags just once ? */
  GstClockTimeDiff timestamp_offset;    /* base offset */
  GstClockTime running_time;            /* total running time */
  gint64 n_samples;                     /* total samples sent */
  gint64 n_samples_stop;
  gboolean check_seek_stop;
  gboolean eos_reached;
  gint generate_samples_per_buffer;	/* used to generate a partial buffer */
  
  /* waveform specific context data */
  gdouble accumulator;			/* phase angle */
  GstPinkNoise pink;
  gdouble wave_table[1024];
};

struct _GstAudioTestSrcClass {
  GstBaseSrcClass parent_class;
};

GType gst_audio_test_src_get_type (void);

G_END_DECLS

#endif /* __GST_AUDIO_TEST_SRC_H__ */
