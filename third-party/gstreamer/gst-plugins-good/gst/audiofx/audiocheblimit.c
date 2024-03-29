/* 
 * GStreamer
 * Copyright (C) 2007 Sebastian Dröge <slomo@circular-chaos.org>
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
 * Chebyshev type 1 filter design based on
 * "The Scientist and Engineer's Guide to DSP", Chapter 20.
 * http://www.dspguide.com/
 *
 * For type 2 and Chebyshev filters in general read
 * http://en.wikipedia.org/wiki/Chebyshev_filter
 *
 */

/**
 * SECTION:element-audiocheblimit
 * @short_description: Chebyshev low pass and high pass filter
 *
 * <refsect2>
 * <para>
 * Attenuates all frequencies above the cutoff frequency (low-pass) or all frequencies below the
 * cutoff frequency (high-pass). The number of poles and the ripple parameter control the rolloff.
 * </para>
 * <para>
 * This element has the advantage over the windowed sinc lowpass and highpass filter that it is
 * much faster and produces almost as good results. It's only disadvantages are the highly
 * non-linear phase and the slower rolloff compared to a windowed sinc filter with a large kernel.
 * </para>
 * <para>
 * For type 1 the ripple parameter specifies how much ripple in dB is allowed in the passband, i.e.
 * some frequencies in the passband will be amplified by that value. A higher ripple value will allow
 * a faster rolloff.
 * </para>
 * <para>
 * For type 2 the ripple parameter specifies the stopband attenuation. In the stopband the gain will
 * be at most this value. A lower ripple value will allow a faster rolloff.
 * </para>
 * <para>
 * As a special case, a Chebyshev type 1 filter with no ripple is a Butterworth filter.
 * </para>
 * <para><note>
 * Be warned that a too large number of poles can produce noise. The most poles are possible with
 * a cutoff frequency at a quarter of the sampling rate.
 * </note></para>
 * <title>Example launch line</title>
 * <para>
 * <programlisting>
 * gst-launch audiotestsrc freq=1500 ! audioconvert ! audiocheblimit mode=low-pass cutoff=1000 poles=4 ! audioconvert ! alsasink
 * gst-launch filesrc location="melo1.ogg" ! oggdemux ! vorbisdec ! audioconvert ! audiocheblimit mode=high-pass cutoff=400 ripple=0.2 ! audioconvert ! alsasink
 * gst-launch audiotestsrc wave=white-noise ! audioconvert ! audiocheblimit mode=low-pass cutoff=800 type=2 ! audioconvert ! alsasink
 * </programlisting>
 * </para>
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include <gst/base/gstbasetransform.h>
#include <gst/audio/audio.h>
#include <gst/audio/gstaudiofilter.h>
#include <gst/controller/gstcontroller.h>

#include <math.h>

#include "math_compat.h"

#include "audiocheblimit.h"

#define GST_CAT_DEFAULT gst_audio_cheb_limit_debug
GST_DEBUG_CATEGORY_STATIC (GST_CAT_DEFAULT);

static const GstElementDetails element_details =
GST_ELEMENT_DETAILS ("Low pass & high pass filter",
    "Filter/Effect/Audio",
    "Chebyshev low pass and high pass filter",
    "Sebastian Dröge <slomo@circular-chaos.org>");

/* Filter signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  PROP_0,
  PROP_MODE,
  PROP_TYPE,
  PROP_CUTOFF,
  PROP_RIPPLE,
  PROP_POLES
};

#define ALLOWED_CAPS \
    "audio/x-raw-float,"                                              \
    " width = (int) { 32, 64 }, "                                     \
    " endianness = (int) BYTE_ORDER,"                                 \
    " rate = (int) [ 1, MAX ],"                                       \
    " channels = (int) [ 1, MAX ]"

#define DEBUG_INIT(bla) \
  GST_DEBUG_CATEGORY_INIT (gst_audio_cheb_limit_debug, "audiocheblimit", 0, "audiocheblimit element");

GST_BOILERPLATE_FULL (GstAudioChebLimit,
    gst_audio_cheb_limit, GstAudioFilter, GST_TYPE_AUDIO_FILTER, DEBUG_INIT);

static void gst_audio_cheb_limit_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_audio_cheb_limit_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);

static gboolean gst_audio_cheb_limit_setup (GstAudioFilter * filter,
    GstRingBufferSpec * format);
static GstFlowReturn
gst_audio_cheb_limit_transform_ip (GstBaseTransform * base, GstBuffer * buf);
static gboolean gst_audio_cheb_limit_start (GstBaseTransform * base);

static void process_64 (GstAudioChebLimit * filter,
    gdouble * data, guint num_samples);
static void process_32 (GstAudioChebLimit * filter,
    gfloat * data, guint num_samples);

enum
{
  MODE_LOW_PASS = 0,
  MODE_HIGH_PASS
};

#define GST_TYPE_AUDIO_CHEBYSHEV_FREQ_LIMIT_MODE (gst_audio_cheb_limit_mode_get_type ())
static GType
gst_audio_cheb_limit_mode_get_type (void)
{
  static GType gtype = 0;

  if (gtype == 0) {
    static const GEnumValue values[] = {
      {MODE_LOW_PASS, "Low pass (default)",
          "low-pass"},
      {MODE_HIGH_PASS, "High pass",
          "high-pass"},
      {0, NULL, NULL}
    };

    gtype = g_enum_register_static ("GstAudioChebLimitMode", values);
  }
  return gtype;
}

/* GObject vmethod implementations */

static void
gst_audio_cheb_limit_base_init (gpointer klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstCaps *caps;

  gst_element_class_set_details (element_class, &element_details);

  caps = gst_caps_from_string (ALLOWED_CAPS);
  gst_audio_filter_class_add_pad_templates (GST_AUDIO_FILTER_CLASS (klass),
      caps);
  gst_caps_unref (caps);
}

static void
gst_audio_cheb_limit_dispose (GObject * object)
{
  GstAudioChebLimit *filter = GST_AUDIO_CHEB_LIMIT (object);

  if (filter->a) {
    g_free (filter->a);
    filter->a = NULL;
  }

  if (filter->b) {
    g_free (filter->b);
    filter->b = NULL;
  }

  if (filter->channels) {
    GstAudioChebLimitChannelCtx *ctx;
    gint i, channels = GST_AUDIO_FILTER (filter)->format.channels;

    for (i = 0; i < channels; i++) {
      ctx = &filter->channels[i];
      g_free (ctx->x);
      g_free (ctx->y);
    }

    g_free (filter->channels);
    filter->channels = NULL;
  }

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
gst_audio_cheb_limit_class_init (GstAudioChebLimitClass * klass)
{
  GObjectClass *gobject_class;
  GstBaseTransformClass *trans_class;
  GstAudioFilterClass *filter_class;

  gobject_class = (GObjectClass *) klass;
  trans_class = (GstBaseTransformClass *) klass;
  filter_class = (GstAudioFilterClass *) klass;

  gobject_class->set_property = gst_audio_cheb_limit_set_property;
  gobject_class->get_property = gst_audio_cheb_limit_get_property;
  gobject_class->dispose = gst_audio_cheb_limit_dispose;

  g_object_class_install_property (gobject_class, PROP_MODE,
      g_param_spec_enum ("mode", "Mode",
          "Low pass or high pass mode",
          GST_TYPE_AUDIO_CHEBYSHEV_FREQ_LIMIT_MODE, MODE_LOW_PASS,
          G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE));
  g_object_class_install_property (gobject_class, PROP_TYPE,
      g_param_spec_int ("type", "Type", "Type of the chebychev filter", 1, 2, 1,
          G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE));

  /* FIXME: Don't use the complete possible range but restrict the upper boundary
   * so automatically generated UIs can use a slider without */
  g_object_class_install_property (gobject_class, PROP_CUTOFF,
      g_param_spec_float ("cutoff", "Cutoff", "Cut off frequency (Hz)", 0.0,
          100000.0, 0.0, G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE));
  g_object_class_install_property (gobject_class, PROP_RIPPLE,
      g_param_spec_float ("ripple", "Ripple", "Amount of ripple (dB)", 0.0,
          200.0, 0.25, G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE));

  /* FIXME: What to do about this upper boundary? With a cutoff frequency of
   * rate/4 32 poles are completely possible, with a cutoff frequency very low
   * or very high 16 poles already produces only noise */
  g_object_class_install_property (gobject_class, PROP_POLES,
      g_param_spec_int ("poles", "Poles",
          "Number of poles to use, will be rounded up to the next even number",
          2, 32, 4, G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE));

  filter_class->setup = GST_DEBUG_FUNCPTR (gst_audio_cheb_limit_setup);
  trans_class->transform_ip =
      GST_DEBUG_FUNCPTR (gst_audio_cheb_limit_transform_ip);
  trans_class->start = GST_DEBUG_FUNCPTR (gst_audio_cheb_limit_start);
}

static void
gst_audio_cheb_limit_init (GstAudioChebLimit * filter,
    GstAudioChebLimitClass * klass)
{
  filter->cutoff = 0.0;
  filter->mode = MODE_LOW_PASS;
  filter->type = 1;
  filter->poles = 4;
  filter->ripple = 0.25;
  gst_base_transform_set_in_place (GST_BASE_TRANSFORM (filter), TRUE);

  filter->have_coeffs = FALSE;
  filter->num_a = 0;
  filter->num_b = 0;
  filter->channels = NULL;
}

static void
generate_biquad_coefficients (GstAudioChebLimit * filter,
    gint p, gdouble * a0, gdouble * a1, gdouble * a2,
    gdouble * b1, gdouble * b2)
{
  gint np = filter->poles;
  gdouble ripple = filter->ripple;

  /* pole location in s-plane */
  gdouble rp, ip;

  /* zero location in s-plane */
  gdouble rz = 0.0, iz = 0.0;

  /* transfer function coefficients for the z-plane */
  gdouble x0, x1, x2, y1, y2;
  gint type = filter->type;

  /* Calculate pole location for lowpass at frequency 1 */
  {
    gdouble angle = (M_PI / 2.0) * (2.0 * p - 1) / np;

    rp = -sin (angle);
    ip = cos (angle);
  }

  /* If we allow ripple, move the pole from the unit
   * circle to an ellipse and keep cutoff at frequency 1 */
  if (ripple > 0 && type == 1) {
    gdouble es, vx;

    es = sqrt (pow (10.0, ripple / 10.0) - 1.0);

    vx = (1.0 / np) * asinh (1.0 / es);
    rp = rp * sinh (vx);
    ip = ip * cosh (vx);
  } else if (type == 2) {
    gdouble es, vx;

    es = sqrt (pow (10.0, ripple / 10.0) - 1.0);
    vx = (1.0 / np) * asinh (es);
    rp = rp * sinh (vx);
    ip = ip * cosh (vx);
  }

  /* Calculate inverse of the pole location to convert from
   * type I to type II */
  if (type == 2) {
    gdouble mag2 = rp * rp + ip * ip;

    rp /= mag2;
    ip /= mag2;
  }

  /* Calculate zero location for frequency 1 on the
   * unit circle for type 2 */
  if (type == 2) {
    gdouble angle = M_PI / (np * 2.0) + ((p - 1) * M_PI) / (np);
    gdouble mag2;

    rz = 0.0;
    iz = cos (angle);
    mag2 = rz * rz + iz * iz;
    rz /= mag2;
    iz /= mag2;
  }

  /* Convert from s-domain to z-domain by
   * using the bilinear Z-transform, i.e.
   * substitute s by (2/t)*((z-1)/(z+1))
   * with t = 2 * tan(0.5).
   */
  if (type == 1) {
    gdouble t, m, d;

    t = 2.0 * tan (0.5);
    m = rp * rp + ip * ip;
    d = 4.0 - 4.0 * rp * t + m * t * t;

    x0 = (t * t) / d;
    x1 = 2.0 * x0;
    x2 = x0;
    y1 = (8.0 - 2.0 * m * t * t) / d;
    y2 = (-4.0 - 4.0 * rp * t - m * t * t) / d;
  } else {
    gdouble t, m, d;

    t = 2.0 * tan (0.5);
    m = rp * rp + ip * ip;
    d = 4.0 - 4.0 * rp * t + m * t * t;

    x0 = (t * t * iz * iz + 4.0) / d;
    x1 = (-8.0 + 2.0 * iz * iz * t * t) / d;
    x2 = x0;
    y1 = (8.0 - 2.0 * m * t * t) / d;
    y2 = (-4.0 - 4.0 * rp * t - m * t * t) / d;
  }

  /* Convert from lowpass at frequency 1 to either lowpass
   * or highpass.
   *
   * For lowpass substitute z^(-1) with:
   *  -1
   * z   - k
   * ------------
   *          -1
   * 1 - k * z
   *
   * k = sin((1-w)/2) / sin((1+w)/2)
   *
   * For highpass substitute z^(-1) with:
   *
   *   -1
   * -z   - k
   * ------------
   *          -1
   * 1 + k * z
   *
   * k = -cos((1+w)/2) / cos((1-w)/2)
   *
   */
  {
    gdouble k, d;
    gdouble omega =
        2.0 * M_PI * (filter->cutoff / GST_AUDIO_FILTER (filter)->format.rate);

    if (filter->mode == MODE_LOW_PASS)
      k = sin ((1.0 - omega) / 2.0) / sin ((1.0 + omega) / 2.0);
    else
      k = -cos ((omega + 1.0) / 2.0) / cos ((omega - 1.0) / 2.0);

    d = 1.0 + y1 * k - y2 * k * k;
    *a0 = (x0 + k * (-x1 + k * x2)) / d;
    *a1 = (x1 + k * k * x1 - 2.0 * k * (x0 + x2)) / d;
    *a2 = (x0 * k * k - x1 * k + x2) / d;
    *b1 = (2.0 * k + y1 + y1 * k * k - 2.0 * y2 * k) / d;
    *b2 = (-k * k - y1 * k + y2) / d;

    if (filter->mode == MODE_HIGH_PASS) {
      *a1 = -*a1;
      *b1 = -*b1;
    }
  }
}

/* Evaluate the transfer function that corresponds to the IIR
 * coefficients at zr + zi*I and return the magnitude */
static gdouble
calculate_gain (gdouble * a, gdouble * b, gint num_a, gint num_b, gdouble zr,
    gdouble zi)
{
  gdouble sum_ar, sum_ai;
  gdouble sum_br, sum_bi;
  gdouble gain_r, gain_i;

  gdouble sum_r_old;
  gdouble sum_i_old;

  gint i;

  sum_ar = 0.0;
  sum_ai = 0.0;
  for (i = num_a; i >= 0; i--) {
    sum_r_old = sum_ar;
    sum_i_old = sum_ai;

    sum_ar = (sum_r_old * zr - sum_i_old * zi) + a[i];
    sum_ai = (sum_r_old * zi + sum_i_old * zr) + 0.0;
  }

  sum_br = 0.0;
  sum_bi = 0.0;
  for (i = num_b; i >= 0; i--) {
    sum_r_old = sum_br;
    sum_i_old = sum_bi;

    sum_br = (sum_r_old * zr - sum_i_old * zi) - b[i];
    sum_bi = (sum_r_old * zi + sum_i_old * zr) - 0.0;
  }
  sum_br += 1.0;
  sum_bi += 0.0;

  gain_r =
      (sum_ar * sum_br + sum_ai * sum_bi) / (sum_br * sum_br + sum_bi * sum_bi);
  gain_i =
      (sum_ai * sum_br - sum_ar * sum_bi) / (sum_br * sum_br + sum_bi * sum_bi);

  return (sqrt (gain_r * gain_r + gain_i * gain_i));
}

static void
generate_coefficients (GstAudioChebLimit * filter)
{
  gint channels = GST_AUDIO_FILTER (filter)->format.channels;

  if (filter->a) {
    g_free (filter->a);
    filter->a = NULL;
  }

  if (filter->b) {
    g_free (filter->b);
    filter->b = NULL;
  }

  if (filter->channels) {
    GstAudioChebLimitChannelCtx *ctx;
    gint i;

    for (i = 0; i < channels; i++) {
      ctx = &filter->channels[i];
      g_free (ctx->x);
      g_free (ctx->y);
    }

    g_free (filter->channels);
    filter->channels = NULL;
  }

  if (GST_AUDIO_FILTER (filter)->format.rate == 0) {
    filter->num_a = 1;
    filter->a = g_new0 (gdouble, 1);
    filter->a[0] = 1.0;
    filter->num_b = 0;
    filter->channels = g_new0 (GstAudioChebLimitChannelCtx, channels);
    GST_LOG_OBJECT (filter, "rate was not set yet");
    return;
  }

  filter->have_coeffs = TRUE;

  if (filter->cutoff >= GST_AUDIO_FILTER (filter)->format.rate / 2.0) {
    filter->num_a = 1;
    filter->a = g_new0 (gdouble, 1);
    filter->a[0] = (filter->mode == MODE_LOW_PASS) ? 1.0 : 0.0;
    filter->num_b = 0;
    filter->channels = g_new0 (GstAudioChebLimitChannelCtx, channels);
    GST_LOG_OBJECT (filter, "cutoff was higher than nyquist frequency");
    return;
  } else if (filter->cutoff <= 0.0) {
    filter->num_a = 1;
    filter->a = g_new0 (gdouble, 1);
    filter->a[0] = (filter->mode == MODE_LOW_PASS) ? 0.0 : 1.0;
    filter->num_b = 0;
    filter->channels = g_new0 (GstAudioChebLimitChannelCtx, channels);
    GST_LOG_OBJECT (filter, "cutoff is lower than zero");
    return;
  }

  /* Calculate coefficients for the chebyshev filter */
  {
    gint np = filter->poles;
    gdouble *a, *b;
    gint i, p;

    filter->num_a = np + 1;
    filter->a = a = g_new0 (gdouble, np + 3);
    filter->num_b = np + 1;
    filter->b = b = g_new0 (gdouble, np + 3);

    filter->channels = g_new0 (GstAudioChebLimitChannelCtx, channels);
    for (i = 0; i < channels; i++) {
      GstAudioChebLimitChannelCtx *ctx = &filter->channels[i];

      ctx->x = g_new0 (gdouble, np + 1);
      ctx->y = g_new0 (gdouble, np + 1);
    }

    /* Calculate transfer function coefficients */
    a[2] = 1.0;
    b[2] = 1.0;

    for (p = 1; p <= np / 2; p++) {
      gdouble a0, a1, a2, b1, b2;
      gdouble *ta = g_new0 (gdouble, np + 3);
      gdouble *tb = g_new0 (gdouble, np + 3);

      generate_biquad_coefficients (filter, p, &a0, &a1, &a2, &b1, &b2);

      memcpy (ta, a, sizeof (gdouble) * (np + 3));
      memcpy (tb, b, sizeof (gdouble) * (np + 3));

      /* add the new coefficients for the new two poles
       * to the cascade by multiplication of the transfer
       * functions */
      for (i = 2; i < np + 3; i++) {
        a[i] = a0 * ta[i] + a1 * ta[i - 1] + a2 * ta[i - 2];
        b[i] = tb[i] - b1 * tb[i - 1] - b2 * tb[i - 2];
      }
      g_free (ta);
      g_free (tb);
    }

    /* Move coefficients to the beginning of the array
     * and multiply the b coefficients with -1 to move from
     * the transfer function's coefficients to the difference
     * equation's coefficients */
    b[2] = 0.0;
    for (i = 0; i <= np; i++) {
      a[i] = a[i + 2];
      b[i] = -b[i + 2];
    }

    /* Normalize to unity gain at frequency 0 for lowpass
     * and frequency 0.5 for highpass */
    {
      gdouble gain;

      if (filter->mode == MODE_LOW_PASS)
        gain = calculate_gain (a, b, np, np, 1.0, 0.0);
      else
        gain = calculate_gain (a, b, np, np, -1.0, 0.0);

      for (i = 0; i <= np; i++) {
        a[i] /= gain;
      }
    }

    GST_LOG_OBJECT (filter,
        "Generated IIR coefficients for the Chebyshev filter");
    GST_LOG_OBJECT (filter,
        "mode: %s, type: %d, poles: %d, cutoff: %.2f Hz, ripple: %.2f dB",
        (filter->mode == MODE_LOW_PASS) ? "low-pass" : "high-pass",
        filter->type, filter->poles, filter->cutoff, filter->ripple);
    GST_LOG_OBJECT (filter, "%.2f dB gain @ 0 Hz",
        20.0 * log10 (calculate_gain (a, b, np, np, 1.0, 0.0)));
    {
      gdouble wc =
          2.0 * M_PI * (filter->cutoff /
          GST_AUDIO_FILTER (filter)->format.rate);
      gdouble zr = cos (wc), zi = sin (wc);

      GST_LOG_OBJECT (filter, "%.2f dB gain @ %d Hz",
          20.0 * log10 (calculate_gain (a, b, np, np, zr, zi)),
          (int) filter->cutoff);
    }
    GST_LOG_OBJECT (filter, "%.2f dB gain @ %d Hz",
        20.0 * log10 (calculate_gain (a, b, np, np, -1.0, 0.0)),
        GST_AUDIO_FILTER (filter)->format.rate / 2);
  }
}

static void
gst_audio_cheb_limit_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstAudioChebLimit *filter = GST_AUDIO_CHEB_LIMIT (object);

  switch (prop_id) {
    case PROP_MODE:
      GST_BASE_TRANSFORM_LOCK (filter);
      filter->mode = g_value_get_enum (value);
      generate_coefficients (filter);
      GST_BASE_TRANSFORM_UNLOCK (filter);
      break;
    case PROP_TYPE:
      GST_BASE_TRANSFORM_LOCK (filter);
      filter->type = g_value_get_int (value);
      generate_coefficients (filter);
      GST_BASE_TRANSFORM_UNLOCK (filter);
      break;
    case PROP_CUTOFF:
      GST_BASE_TRANSFORM_LOCK (filter);
      filter->cutoff = g_value_get_float (value);
      generate_coefficients (filter);
      GST_BASE_TRANSFORM_UNLOCK (filter);
      break;
    case PROP_RIPPLE:
      GST_BASE_TRANSFORM_LOCK (filter);
      filter->ripple = g_value_get_float (value);
      generate_coefficients (filter);
      GST_BASE_TRANSFORM_UNLOCK (filter);
      break;
    case PROP_POLES:
      GST_BASE_TRANSFORM_LOCK (filter);
      filter->poles = GST_ROUND_UP_2 (g_value_get_int (value));
      generate_coefficients (filter);
      GST_BASE_TRANSFORM_UNLOCK (filter);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_audio_cheb_limit_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstAudioChebLimit *filter = GST_AUDIO_CHEB_LIMIT (object);

  switch (prop_id) {
    case PROP_MODE:
      g_value_set_enum (value, filter->mode);
      break;
    case PROP_TYPE:
      g_value_set_int (value, filter->type);
      break;
    case PROP_CUTOFF:
      g_value_set_float (value, filter->cutoff);
      break;
    case PROP_RIPPLE:
      g_value_set_float (value, filter->ripple);
      break;
    case PROP_POLES:
      g_value_set_int (value, filter->poles);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

/* GstAudioFilter vmethod implementations */

static gboolean
gst_audio_cheb_limit_setup (GstAudioFilter * base, GstRingBufferSpec * format)
{
  GstAudioChebLimit *filter = GST_AUDIO_CHEB_LIMIT (base);
  gboolean ret = TRUE;

  if (format->width == 32)
    filter->process = (GstAudioChebLimitProcessFunc)
        process_32;
  else if (format->width == 64)
    filter->process = (GstAudioChebLimitProcessFunc)
        process_64;
  else
    ret = FALSE;

  filter->have_coeffs = FALSE;

  return ret;
}

static inline gdouble
process (GstAudioChebLimit * filter,
    GstAudioChebLimitChannelCtx * ctx, gdouble x0)
{
  gdouble val = filter->a[0] * x0;
  gint i, j;

  for (i = 1, j = ctx->x_pos; i < filter->num_a; i++) {
    val += filter->a[i] * ctx->x[j];
    j--;
    if (j < 0)
      j = filter->num_a - 1;
  }

  for (i = 1, j = ctx->y_pos; i < filter->num_b; i++) {
    val += filter->b[i] * ctx->y[j];
    j--;
    if (j < 0)
      j = filter->num_b - 1;
  }

  if (ctx->x) {
    ctx->x_pos++;
    if (ctx->x_pos > filter->num_a - 1)
      ctx->x_pos = 0;
    ctx->x[ctx->x_pos] = x0;
  }

  if (ctx->y) {
    ctx->y_pos++;
    if (ctx->y_pos > filter->num_b - 1)
      ctx->y_pos = 0;

    ctx->y[ctx->y_pos] = val;
  }

  return val;
}

#define DEFINE_PROCESS_FUNC(width,ctype) \
static void \
process_##width (GstAudioChebLimit * filter, \
    g##ctype * data, guint num_samples) \
{ \
  gint i, j, channels = GST_AUDIO_FILTER (filter)->format.channels; \
  gdouble val; \
  \
  for (i = 0; i < num_samples / channels; i++) { \
    for (j = 0; j < channels; j++) { \
      val = process (filter, &filter->channels[j], *data); \
      *data++ = val; \
    } \
  } \
}

DEFINE_PROCESS_FUNC (32, float);
DEFINE_PROCESS_FUNC (64, double);

#undef DEFINE_PROCESS_FUNC

/* GstBaseTransform vmethod implementations */
static GstFlowReturn
gst_audio_cheb_limit_transform_ip (GstBaseTransform * base, GstBuffer * buf)
{
  GstAudioChebLimit *filter = GST_AUDIO_CHEB_LIMIT (base);
  guint num_samples =
      GST_BUFFER_SIZE (buf) / (GST_AUDIO_FILTER (filter)->format.width / 8);

  if (GST_CLOCK_TIME_IS_VALID (GST_BUFFER_TIMESTAMP (buf)))
    gst_object_sync_values (G_OBJECT (filter), GST_BUFFER_TIMESTAMP (buf));

  if (gst_base_transform_is_passthrough (base))
    return GST_FLOW_OK;

  if (!filter->have_coeffs)
    generate_coefficients (filter);

  filter->process (filter, GST_BUFFER_DATA (buf), num_samples);

  return GST_FLOW_OK;
}


static gboolean
gst_audio_cheb_limit_start (GstBaseTransform * base)
{
  GstAudioChebLimit *filter = GST_AUDIO_CHEB_LIMIT (base);
  gint channels = GST_AUDIO_FILTER (filter)->format.channels;
  GstAudioChebLimitChannelCtx *ctx;
  gint i;

  /* Reset the history of input and output values if
   * already existing */
  if (channels && filter->channels) {
    for (i = 0; i < channels; i++) {
      ctx = &filter->channels[i];
      if (ctx->x)
        memset (ctx->x, 0, (filter->poles + 1) * sizeof (gdouble));
      if (ctx->y)
        memset (ctx->y, 0, (filter->poles + 1) * sizeof (gdouble));
    }
  }
  return TRUE;
}
