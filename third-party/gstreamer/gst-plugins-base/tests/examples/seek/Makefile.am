if HAVE_GTK
GTK_EXAMPLES=seek scrubby
endif

examples = $(GTK_EXAMPLES)

noinst_PROGRAMS = $(examples)

LIBS = $(GST_LIBS) $(GTK_LIBS)
AM_CFLAGS = $(GST_CFLAGS) $(GTK_CFLAGS)
