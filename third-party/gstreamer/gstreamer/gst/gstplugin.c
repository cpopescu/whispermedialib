/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wtay@chello.be>
 *
 * gstplugin.c: Plugin subsystem for loading elements, types, and libs
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

/**
 * SECTION:gstplugin
 * @short_description: Container for features loaded from a shared object module
 * @see_also: #GstPluginFeature, #GstElementFactory
 *
 * GStreamer is extensible, so #GstElement instances can be loaded at runtime.
 * A plugin system can provide one or more of the basic
 * <application>GStreamer</application> #GstPluginFeature subclasses.
 *
 * A plugin should export a symbol <symbol>gst_plugin_desc</symbol> that is a
 * struct of type #GstPluginDesc.
 * the plugin loader will check the version of the core library the plugin was
 * linked against and will create a new #GstPlugin. It will then call the
 * #GstPluginInitFunc function that was provided in the
 * <symbol>gst_plugin_desc</symbol>.
 *
 * Once you have a handle to a #GstPlugin (e.g. from the #GstRegistryPool), you
 * can add any object that subclasses #GstPluginFeature.
 *
 * Use gst_plugin_find_feature() and gst_plugin_get_feature_list() to find
 * features in a plugin.
 *
 * Usually plugins are always automaticlly loaded so you don't need to call
 * gst_plugin_load() explicitly to bring it into memory. There are options to
 * statically link plugins to an app or even use GStreamer without a plugin
 * repository in which case gst_plugin_load() can be needed to bring the plugin
 * into memory.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <glib/gstdio.h>
#include <sys/types.h>
#ifdef HAVE_DIRENT_H
#include <dirent.h>
#endif
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#include <signal.h>
#include <errno.h>

#include "gst_private.h"
#include "glib-compat-private.h"

#include <gst/gst.h>

#define GST_CAT_DEFAULT GST_CAT_PLUGIN_LOADING

static guint _num_static_plugins;       /* 0    */
static GstPluginDesc *_static_plugins;  /* NULL */
static gboolean _gst_plugin_inited;

/* static variables for segfault handling of plugin loading */
static char *_gst_plugin_fault_handler_filename = NULL;

#ifndef HAVE_WIN32
static gboolean _gst_plugin_fault_handler_is_setup = FALSE;
#endif

/* list of valid licenses.
 * One of these must be specified or the plugin won't be loaded
 * Contact gstreamer-devel@lists.sourceforge.net if your license should be
 * added.
 *
 * GPL: http://www.gnu.org/copyleft/gpl.html
 * LGPL: http://www.gnu.org/copyleft/lesser.html
 * QPL: http://www.trolltech.com/licenses/qpl.html
 * MPL: http://www.opensource.org/licenses/mozilla1.1.php
 * MIT/X11: http://www.opensource.org/licenses/mit-license.php
 * 3-clause BSD: http://www.opensource.org/licenses/bsd-license.php
 */
static const gchar *valid_licenses[] = {
  "LGPL",                       /* GNU Lesser General Public License */
  "GPL",                        /* GNU General Public License */
  "QPL",                        /* Trolltech Qt Public License */
  "GPL/QPL",                    /* Combi-license of GPL + QPL */
  "MPL",                        /* MPL 1.1 license */
  "BSD",                        /* 3-clause BSD license */
  "MIT/X11",                    /* MIT/X11 license */
  "Proprietary",                /* Proprietary license */
  GST_LICENSE_UNKNOWN,          /* some other license */
  NULL
};

static GstPlugin *gst_plugin_register_func (GstPlugin * plugin,
    const GstPluginDesc * desc);
static void gst_plugin_desc_copy (GstPluginDesc * dest,
    const GstPluginDesc * src);
static void gst_plugin_desc_free (GstPluginDesc * desc);


G_DEFINE_TYPE (GstPlugin, gst_plugin, GST_TYPE_OBJECT);

static void
gst_plugin_init (GstPlugin * plugin)
{
  /* do nothing, needed because of G_DEFINE_TYPE */
}

static void
gst_plugin_finalize (GObject * object)
{
  GstPlugin *plugin = GST_PLUGIN_CAST (object);
  GstRegistry *registry = gst_registry_get_default ();
  GList *g;

  GST_DEBUG ("finalizing plugin %p", plugin);
  for (g = registry->plugins; g; g = g->next) {
    if (g->data == (gpointer) plugin) {
      g_warning ("removing plugin that is still in registry");
    }
  }
  g_free (plugin->filename);
  g_free (plugin->basename);
  gst_plugin_desc_free (&plugin->desc);

  G_OBJECT_CLASS (gst_plugin_parent_class)->finalize (object);
}

static void
gst_plugin_class_init (GstPluginClass * klass)
{
  G_OBJECT_CLASS (klass)->finalize = GST_DEBUG_FUNCPTR (gst_plugin_finalize);
}

GQuark
gst_plugin_error_quark (void)
{
  static GQuark quark = 0;

  if (!quark)
    quark = g_quark_from_static_string ("gst_plugin_error");
  return quark;
}

#ifndef GST_REMOVE_DEPRECATED
/* this function can be called in the GCC constructor extension, before
 * the _gst_plugin_initialize() was called. In that case, we store the
 * plugin description in a list to initialize it when we open the main
 * module later on.
 * When the main module is known, we can register the plugin right away.
 */
void
_gst_plugin_register_static (GstPluginDesc * desc)
{
  g_return_if_fail (desc != NULL);

  if (!_gst_plugin_inited) {
    /* We can't use any GLib functions here, since g_thread_init hasn't been
     * called yet, and we can't call it here either, or programs that don't
     * guard their g_thread_init calls in main() will just abort */
    ++_num_static_plugins;
    _static_plugins =
        realloc (_static_plugins, _num_static_plugins * sizeof (GstPluginDesc));
    /* assume strings in the GstPluginDesc are static const or live forever */
    _static_plugins[_num_static_plugins - 1] = *desc;
  } else {
    gst_plugin_register_static (desc->major_version, desc->minor_version,
        desc->name, desc->description, desc->plugin_init, desc->version,
        desc->license, desc->source, desc->package, desc->origin);
  }
}
#endif

/**
 * gst_plugin_register_static:
 * @major_version: the major version number of the GStreamer core that the
 *     plugin was compiled for, you can just use GST_VERSION_MAJOR here
 * @minor_version: the minor version number of the GStreamer core that the
 *     plugin was compiled for, you can just use GST_VERSION_MINOR here
 * @name: a unique name of the plugin (ideally prefixed with an application- or
 *     library-specific namespace prefix in order to avoid name conflicts in
 *     case a similar plugin with the same name ever gets added to GStreamer)
 * @description: description of the plugin
 * @init_func: pointer to the init function of this plugin.
 * @version: version string of the plugin
 * @license: effective license of plugin. Must be one of the approved licenses
 *     (see #GstPluginDesc above) or the plugin will not be registered.
 * @source: source module plugin belongs to
 * @package: shipped package plugin belongs to
 * @origin: URL to provider of plugin
 *
 * Registers a static plugin, ie. a plugin which is private to an application
 * or library and contained within the application or library (as opposed to
 * being shipped as a separate module file).
 *
 * You must make sure that GStreamer has been initialised (with gst_init() or
 * via gst_init_get_option_group()) before calling this function.
 *
 * Returns: TRUE if the plugin was registered correctly, otherwise FALSE.
 *
 * Since: 0.10.16
 */
gboolean
gst_plugin_register_static (gint major_version, gint minor_version,
    const gchar * name, gchar * description, GstPluginInitFunc init_func,
    const gchar * version, const gchar * license, const gchar * source,
    const gchar * package, const gchar * origin)
{
  GstPluginDesc desc = { major_version, minor_version, name, description,
    init_func, version, license, source, package, origin,
  };
  GstPlugin *plugin;
  gboolean res = FALSE;

  g_return_val_if_fail (name != NULL, FALSE);
  g_return_val_if_fail (description != NULL, FALSE);
  g_return_val_if_fail (init_func != NULL, FALSE);
  g_return_val_if_fail (version != NULL, FALSE);
  g_return_val_if_fail (license != NULL, FALSE);
  g_return_val_if_fail (source != NULL, FALSE);
  g_return_val_if_fail (package != NULL, FALSE);
  g_return_val_if_fail (origin != NULL, FALSE);

  /* make sure gst_init() has been called */
  g_return_val_if_fail (_gst_plugin_inited != FALSE, FALSE);

  GST_LOG ("attempting to load static plugin \"%s\" now...", name);
  plugin = g_object_new (GST_TYPE_PLUGIN, NULL);
  if (gst_plugin_register_func (plugin, &desc) != NULL) {
    GST_INFO ("registered static plugin \"%s\"", name);
    res = gst_default_registry_add_plugin (plugin);
    GST_INFO ("added static plugin \"%s\", result: %d", name, res);
  }
  return res;
}

void
_gst_plugin_initialize (void)
{
  guint i;

  _gst_plugin_inited = TRUE;

  /* now register all static plugins */
  GST_INFO ("registering %u static plugins", _num_static_plugins);
  for (i = 0; i < _num_static_plugins; ++i) {
    gst_plugin_register_static (_static_plugins[i].major_version,
        _static_plugins[i].minor_version, _static_plugins[i].name,
        _static_plugins[i].description, _static_plugins[i].plugin_init,
        _static_plugins[i].version, _static_plugins[i].license,
        _static_plugins[i].source, _static_plugins[i].package,
        _static_plugins[i].origin);
  }

  if (_static_plugins) {
    free (_static_plugins);
    _static_plugins = NULL;
    _num_static_plugins = 0;
  }
}

/* this function could be extended to check if the plugin license matches the
 * applications license (would require the app to register its license somehow).
 * We'll wait for someone who's interested in it to code it :)
 */
static gboolean
gst_plugin_check_license (const gchar * license)
{
  const gchar **check_license = valid_licenses;

  g_assert (check_license);

  while (*check_license) {
    if (strcmp (license, *check_license) == 0)
      return TRUE;
    check_license++;
  }
  return FALSE;
}

static gboolean
gst_plugin_check_version (gint major, gint minor)
{
  /* return NULL if the major and minor version numbers are not compatible */
  /* with ours. */
  if (major != GST_VERSION_MAJOR || minor != GST_VERSION_MINOR)
    return FALSE;

  return TRUE;
}

static GstPlugin *
gst_plugin_register_func (GstPlugin * plugin, const GstPluginDesc * desc)
{
  if (!gst_plugin_check_version (desc->major_version, desc->minor_version)) {
    if (GST_CAT_DEFAULT)
      GST_WARNING ("plugin \"%s\" has incompatible version, not loading",
          plugin->filename);
    return NULL;
  }

  if (!desc->license || !desc->description || !desc->source ||
      !desc->package || !desc->origin) {
    if (GST_CAT_DEFAULT)
      GST_WARNING ("plugin \"%s\" has incorrect GstPluginDesc, not loading",
          plugin->filename);
    return NULL;
  }

  if (!gst_plugin_check_license (desc->license)) {
    if (GST_CAT_DEFAULT)
      GST_WARNING ("plugin \"%s\" has invalid license \"%s\", not loading",
          plugin->filename, desc->license);
    return NULL;
  }

  if (GST_CAT_DEFAULT)
    GST_LOG ("plugin \"%s\" looks good", GST_STR_NULL (plugin->filename));

  gst_plugin_desc_copy (&plugin->desc, desc);

  if (!((desc->plugin_init) (plugin))) {
    if (GST_CAT_DEFAULT)
      GST_WARNING ("plugin \"%s\" failed to initialise", plugin->filename);
    plugin->module = NULL;
    return NULL;
  }

  if (GST_CAT_DEFAULT)
    GST_LOG ("plugin \"%s\" initialised", GST_STR_NULL (plugin->filename));

  return plugin;
}

#ifdef HAVE_SIGACTION
static struct sigaction oldaction;

/*
 * _gst_plugin_fault_handler_restore:
 * segfault handler restorer
 */
static void
_gst_plugin_fault_handler_restore (void)
{
  if (!_gst_plugin_fault_handler_is_setup)
    return;

  _gst_plugin_fault_handler_is_setup = FALSE;

  sigaction (SIGSEGV, &oldaction, NULL);
}

/*
 * _gst_plugin_fault_handler_sighandler:
 * segfault handler implementation
 */
static void
_gst_plugin_fault_handler_sighandler (int signum)
{
  /* We need to restore the fault handler or we'll keep getting it */
  _gst_plugin_fault_handler_restore ();

  switch (signum) {
    case SIGSEGV:
      g_print ("\nERROR: ");
      g_print ("Caught a segmentation fault while loading plugin file:\n");
      g_print ("%s\n\n", _gst_plugin_fault_handler_filename);
      g_print ("Please either:\n");
      g_print ("- remove it and restart.\n");
      g_print ("- run with --gst-disable-segtrap and debug.\n");
      exit (-1);
      break;
    default:
      g_print ("Caught unhandled signal on plugin loading\n");
      break;
  }
}

/*
 * _gst_plugin_fault_handler_setup:
 * sets up the segfault handler
 */
static void
_gst_plugin_fault_handler_setup (void)
{
  struct sigaction action;

  /* if asked to leave segfaults alone, just return */
  if (!gst_segtrap_is_enabled ())
    return;

  if (_gst_plugin_fault_handler_is_setup)
    return;

  _gst_plugin_fault_handler_is_setup = TRUE;

  memset (&action, 0, sizeof (action));
  action.sa_handler = _gst_plugin_fault_handler_sighandler;

  sigaction (SIGSEGV, &action, &oldaction);
}
#else
static void
_gst_plugin_fault_handler_restore (void)
{
}

static void
_gst_plugin_fault_handler_setup (void)
{
}
#endif

static void _gst_plugin_fault_handler_setup ();

static GStaticMutex gst_plugin_loading_mutex = G_STATIC_MUTEX_INIT;

/**
 * gst_plugin_load_file:
 * @filename: the plugin filename to load
 * @error: pointer to a NULL-valued GError
 *
 * Loads the given plugin and refs it.  Caller needs to unref after use.
 *
 * Returns: a reference to the existing loaded GstPlugin, a reference to the
 * newly-loaded GstPlugin, or NULL if an error occurred.
 */
GstPlugin *
gst_plugin_load_file (const gchar * filename, GError ** error)
{
  GstPlugin *plugin;
  GModule *module;
  gboolean ret;
  gpointer ptr;
  struct stat file_status;
  GstRegistry *registry;

  g_return_val_if_fail (filename != NULL, NULL);

  registry = gst_registry_get_default ();
  g_static_mutex_lock (&gst_plugin_loading_mutex);

  plugin = gst_registry_lookup (registry, filename);
  if (plugin) {
    if (plugin->module) {
      g_static_mutex_unlock (&gst_plugin_loading_mutex);
      return plugin;
    } else {
      gst_object_unref (plugin);
      plugin = NULL;
    }
  }

  GST_CAT_DEBUG (GST_CAT_PLUGIN_LOADING, "attempt to load plugin \"%s\"",
      filename);

  if (g_module_supported () == FALSE) {
    GST_CAT_DEBUG (GST_CAT_PLUGIN_LOADING, "module loading not supported");
    g_set_error (error,
        GST_PLUGIN_ERROR,
        GST_PLUGIN_ERROR_MODULE, "Dynamic loading not supported");
    goto return_error;
  }

  if (g_stat (filename, &file_status)) {
    GST_CAT_DEBUG (GST_CAT_PLUGIN_LOADING, "problem accessing file");
    g_set_error (error,
        GST_PLUGIN_ERROR,
        GST_PLUGIN_ERROR_MODULE, "Problem accessing file %s: %s", filename,
        g_strerror (errno));
    goto return_error;
  }

  module = g_module_open (filename, G_MODULE_BIND_LOCAL);
  if (module == NULL) {
    GST_CAT_WARNING (GST_CAT_PLUGIN_LOADING, "module_open failed: %s",
        g_module_error ());
    g_set_error (error,
        GST_PLUGIN_ERROR, GST_PLUGIN_ERROR_MODULE, "Opening module failed: %s",
        g_module_error ());
    /* If we failed to open the shared object, then it's probably because a
     * plugin is linked against the wrong libraries. Print out an easy-to-see
     * message in this case. */
    g_warning ("Failed to load plugin '%s': %s", filename, g_module_error ());
    goto return_error;
  }

  plugin = g_object_new (GST_TYPE_PLUGIN, NULL);

  plugin->module = module;
  plugin->filename = g_strdup (filename);
  plugin->basename = g_path_get_basename (filename);
  plugin->file_mtime = file_status.st_mtime;
  plugin->file_size = file_status.st_size;

  ret = g_module_symbol (module, "gst_plugin_desc", &ptr);
  if (!ret) {
    GST_DEBUG ("Could not find plugin entry point in \"%s\"", filename);
    g_set_error (error,
        GST_PLUGIN_ERROR,
        GST_PLUGIN_ERROR_MODULE,
        "File \"%s\" is not a GStreamer plugin", filename);
    g_module_close (module);
    goto return_error;
  }
  plugin->orig_desc = (GstPluginDesc *) ptr;

  GST_LOG ("Plugin %p for file \"%s\" prepared, calling entry function...",
      plugin, filename);

  /* this is where we load the actual .so, so let's trap SIGSEGV */
  _gst_plugin_fault_handler_setup ();
  _gst_plugin_fault_handler_filename = plugin->filename;

  GST_LOG ("Plugin %p for file \"%s\" prepared, registering...",
      plugin, filename);

  if (!gst_plugin_register_func (plugin, plugin->orig_desc)) {
    /* remove signal handler */
    _gst_plugin_fault_handler_restore ();
    GST_DEBUG ("gst_plugin_register_func failed for plugin \"%s\"", filename);
    /* plugin == NULL */
    g_set_error (error,
        GST_PLUGIN_ERROR,
        GST_PLUGIN_ERROR_MODULE,
        "File \"%s\" appears to be a GStreamer plugin, but it failed to initialize",
        filename);
    g_module_close (module);
    goto return_error;
  }

  /* remove signal handler */
  _gst_plugin_fault_handler_restore ();
  _gst_plugin_fault_handler_filename = NULL;
  GST_INFO ("plugin \"%s\" loaded", plugin->filename);

  gst_object_ref (plugin);
  gst_default_registry_add_plugin (plugin);

  g_static_mutex_unlock (&gst_plugin_loading_mutex);
  return plugin;

return_error:
  {
    if (plugin)
      gst_object_unref (plugin);
    g_static_mutex_unlock (&gst_plugin_loading_mutex);
    return NULL;
  }
}

static void
gst_plugin_desc_copy (GstPluginDesc * dest, const GstPluginDesc * src)
{
  dest->major_version = src->major_version;
  dest->minor_version = src->minor_version;
  dest->name = g_intern_string (src->name);
  /* maybe intern the description too, just for convenience? */
  dest->description = g_strdup (src->description);
  dest->plugin_init = src->plugin_init;
  dest->version = g_intern_string (src->version);
  dest->license = g_intern_string (src->license);
  dest->source = g_intern_string (src->source);
  dest->package = g_intern_string (src->package);
  dest->origin = g_intern_string (src->origin);
}

/* unused */
static void
gst_plugin_desc_free (GstPluginDesc * desc)
{
  g_free (desc->description);
  memset (desc, 0, sizeof (GstPluginDesc));
}

/**
 * gst_plugin_get_name:
 * @plugin: plugin to get the name of
 *
 * Get the short name of the plugin
 *
 * Returns: the name of the plugin
 */
const gchar *
gst_plugin_get_name (GstPlugin * plugin)
{
  g_return_val_if_fail (plugin != NULL, NULL);

  return plugin->desc.name;
}

/**
 * gst_plugin_get_description:
 * @plugin: plugin to get long name of
 *
 * Get the long descriptive name of the plugin
 *
 * Returns: the long name of the plugin
 */
G_CONST_RETURN gchar *
gst_plugin_get_description (GstPlugin * plugin)
{
  g_return_val_if_fail (plugin != NULL, NULL);

  return plugin->desc.description;
}

/**
 * gst_plugin_get_filename:
 * @plugin: plugin to get the filename of
 *
 * get the filename of the plugin
 *
 * Returns: the filename of the plugin
 */
G_CONST_RETURN gchar *
gst_plugin_get_filename (GstPlugin * plugin)
{
  g_return_val_if_fail (plugin != NULL, NULL);

  return plugin->filename;
}

/**
 * gst_plugin_get_version:
 * @plugin: plugin to get the version of
 *
 * get the version of the plugin
 *
 * Returns: the version of the plugin
 */
G_CONST_RETURN gchar *
gst_plugin_get_version (GstPlugin * plugin)
{
  g_return_val_if_fail (plugin != NULL, NULL);

  return plugin->desc.version;
}

/**
 * gst_plugin_get_license:
 * @plugin: plugin to get the license of
 *
 * get the license of the plugin
 *
 * Returns: the license of the plugin
 */
G_CONST_RETURN gchar *
gst_plugin_get_license (GstPlugin * plugin)
{
  g_return_val_if_fail (plugin != NULL, NULL);

  return plugin->desc.license;
}

/**
 * gst_plugin_get_source:
 * @plugin: plugin to get the source of
 *
 * get the source module the plugin belongs to.
 *
 * Returns: the source of the plugin
 */
G_CONST_RETURN gchar *
gst_plugin_get_source (GstPlugin * plugin)
{
  g_return_val_if_fail (plugin != NULL, NULL);

  return plugin->desc.source;
}

/**
 * gst_plugin_get_package:
 * @plugin: plugin to get the package of
 *
 * get the package the plugin belongs to.
 *
 * Returns: the package of the plugin
 */
G_CONST_RETURN gchar *
gst_plugin_get_package (GstPlugin * plugin)
{
  g_return_val_if_fail (plugin != NULL, NULL);

  return plugin->desc.package;
}

/**
 * gst_plugin_get_origin:
 * @plugin: plugin to get the origin of
 *
 * get the URL where the plugin comes from
 *
 * Returns: the origin of the plugin
 */
G_CONST_RETURN gchar *
gst_plugin_get_origin (GstPlugin * plugin)
{
  g_return_val_if_fail (plugin != NULL, NULL);

  return plugin->desc.origin;
}

/**
 * gst_plugin_get_module:
 * @plugin: plugin to query
 *
 * Gets the #GModule of the plugin. If the plugin isn't loaded yet, NULL is
 * returned.
 *
 * Returns: module belonging to the plugin or NULL if the plugin isn't
 *          loaded yet.
 */
GModule *
gst_plugin_get_module (GstPlugin * plugin)
{
  g_return_val_if_fail (plugin != NULL, NULL);

  return plugin->module;
}

/**
 * gst_plugin_is_loaded:
 * @plugin: plugin to query
 *
 * queries if the plugin is loaded into memory
 *
 * Returns: TRUE is loaded, FALSE otherwise
 */
gboolean
gst_plugin_is_loaded (GstPlugin * plugin)
{
  g_return_val_if_fail (plugin != NULL, FALSE);

  return (plugin->module != NULL || plugin->filename == NULL);
}

#if 0
/**
 * gst_plugin_feature_list:
 * @plugin: plugin to query
 * @filter: the filter to use
 * @first: only return first match
 * @user_data: user data passed to the filter function
 *
 * Runs a filter against all plugin features and returns a GList with
 * the results. If the first flag is set, only the first match is
 * returned (as a list with a single object).
 *
 * Returns: a GList of features, g_list_free after use.
 */
GList *
gst_plugin_feature_filter (GstPlugin * plugin,
    GstPluginFeatureFilter filter, gboolean first, gpointer user_data)
{
  GList *list;
  GList *g;

  list = gst_filter_run (plugin->features, (GstFilterFunc) filter, first,
      user_data);
  for (g = list; g; g = g->next) {
    gst_object_ref (plugin);
  }

  return list;
}

typedef struct
{
  GstPluginFeatureFilter filter;
  gboolean first;
  gpointer user_data;
  GList *result;
}
FeatureFilterData;

static gboolean
_feature_filter (GstPlugin * plugin, gpointer user_data)
{
  GList *result;
  FeatureFilterData *data = (FeatureFilterData *) user_data;

  result = gst_plugin_feature_filter (plugin, data->filter, data->first,
      data->user_data);
  if (result) {
    data->result = g_list_concat (data->result, result);
    return TRUE;
  }
  return FALSE;
}

/**
 * gst_plugin_list_feature_filter:
 * @list: a #GList of plugins to query
 * @filter: the filter function to use
 * @first: only return first match
 * @user_data: user data passed to the filter function
 *
 * Runs a filter against all plugin features of the plugins in the given
 * list and returns a GList with the results.
 * If the first flag is set, only the first match is
 * returned (as a list with a single object).
 *
 * Returns: a GList of features, g_list_free after use.
 */
GList *
gst_plugin_list_feature_filter (GList * list,
    GstPluginFeatureFilter filter, gboolean first, gpointer user_data)
{
  FeatureFilterData data;
  GList *result;

  data.filter = filter;
  data.first = first;
  data.user_data = user_data;
  data.result = NULL;

  result = gst_filter_run (list, (GstFilterFunc) _feature_filter, first, &data);
  g_list_free (result);

  return data.result;
}
#endif

/**
 * gst_plugin_name_filter:
 * @plugin: the plugin to check
 * @name: the name of the plugin
 *
 * A standard filter that returns TRUE when the plugin is of the
 * given name.
 *
 * Returns: TRUE if the plugin is of the given name.
 */
gboolean
gst_plugin_name_filter (GstPlugin * plugin, const gchar * name)
{
  return (plugin->desc.name && !strcmp (plugin->desc.name, name));
}

#if 0
/**
 * gst_plugin_find_feature:
 * @plugin: plugin to get the feature from
 * @name: The name of the feature to find
 * @type: The type of the feature to find
 *
 * Find a feature of the given name and type in the given plugin.
 *
 * Returns: a GstPluginFeature or NULL if the feature was not found.
 */
GstPluginFeature *
gst_plugin_find_feature (GstPlugin * plugin, const gchar * name, GType type)
{
  GList *walk;
  GstPluginFeature *result = NULL;
  GstTypeNameData data;

  g_return_val_if_fail (name != NULL, NULL);

  data.type = type;
  data.name = name;

  walk = gst_filter_run (plugin->features,
      (GstFilterFunc) gst_plugin_feature_type_name_filter, TRUE, &data);

  if (walk) {
    result = GST_PLUGIN_FEATURE (walk->data);

    gst_object_ref (result);
    gst_plugin_feature_list_free (walk);
  }

  return result;
}
#endif

#if 0
static gboolean
gst_plugin_feature_name_filter (GstPluginFeature * feature, const gchar * name)
{
  return !strcmp (name, GST_PLUGIN_FEATURE_NAME (feature));
}
#endif

#if 0
/**
 * gst_plugin_find_feature_by_name:
 * @plugin: plugin to get the feature from
 * @name: The name of the feature to find
 *
 * Find a feature of the given name in the given plugin.
 *
 * Returns: a GstPluginFeature or NULL if the feature was not found.
 */
GstPluginFeature *
gst_plugin_find_feature_by_name (GstPlugin * plugin, const gchar * name)
{
  GList *walk;
  GstPluginFeature *result = NULL;

  g_return_val_if_fail (name != NULL, NULL);

  walk = gst_filter_run (plugin->features,
      (GstFilterFunc) gst_plugin_feature_name_filter, TRUE, (void *) name);

  if (walk) {
    result = GST_PLUGIN_FEATURE (walk->data);

    gst_object_ref (result);
    gst_plugin_feature_list_free (walk);
  }

  return result;
}
#endif

/**
 * gst_plugin_load_by_name:
 * @name: name of plugin to load
 *
 * Load the named plugin. Refs the plugin.
 *
 * Returns: A reference to a loaded plugin, or NULL on error.
 */
GstPlugin *
gst_plugin_load_by_name (const gchar * name)
{
  GstPlugin *plugin, *newplugin;
  GError *error = NULL;

  GST_DEBUG ("looking up plugin %s in default registry", name);
  plugin = gst_registry_find_plugin (gst_registry_get_default (), name);
  if (plugin) {
    GST_DEBUG ("loading plugin %s from file %s", name, plugin->filename);
    newplugin = gst_plugin_load_file (plugin->filename, &error);
    gst_object_unref (plugin);

    if (!newplugin) {
      GST_WARNING ("load_plugin error: %s", error->message);
      g_error_free (error);
      return NULL;
    }
    /* newplugin was reffed by load_file */
    return newplugin;
  }

  GST_DEBUG ("Could not find plugin %s in registry", name);
  return NULL;
}

/**
 * gst_plugin_load:
 * @plugin: plugin to load
 *
 * Loads @plugin. Note that the *return value* is the loaded plugin; @plugin is
 * untouched. The normal use pattern of this function goes like this:
 *
 * <programlisting>
 * GstPlugin *loaded_plugin;
 * loaded_plugin = gst_plugin_load (plugin);
 * // presumably, we're no longer interested in the potentially-unloaded plugin
 * gst_object_unref (plugin);
 * plugin = loaded_plugin;
 * </programlisting>
 *
 * Returns: A reference to a loaded plugin, or NULL on error.
 */
GstPlugin *
gst_plugin_load (GstPlugin * plugin)
{
  GError *error = NULL;
  GstPlugin *newplugin;

  if (gst_plugin_is_loaded (plugin)) {
    return plugin;
  }

  if (!(newplugin = gst_plugin_load_file (plugin->filename, &error)))
    goto load_error;

  return newplugin;

load_error:
  {
    GST_WARNING ("load_plugin error: %s", error->message);
    g_error_free (error);
    return NULL;
  }
}

/**
 * gst_plugin_list_free:
 * @list: list of #GstPlugin
 *
 * Unrefs each member of @list, then frees the list.
 */
void
gst_plugin_list_free (GList * list)
{
  GList *g;

  for (g = list; g; g = g->next) {
    gst_object_unref (GST_PLUGIN_CAST (g->data));
  }
  g_list_free (list);
}
