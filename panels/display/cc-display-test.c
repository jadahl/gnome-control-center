/*
 * Copyright (C) 2016-2017 Red Hat, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 */

#include "cc-display-config.h"

#include <stdlib.h>

#include "cc-display-config-manager.h"

static gboolean arg_list = false;

static GOptionEntry entries[] =
{
  { "list", 'l', 0, G_OPTION_ARG_NONE, &arg_list, "List current configuration", NULL },
  { NULL }
};

static void
list_modes (CcDisplayMonitor *monitor)
{
  CcDisplayMode *preferred_mode, *current_mode;
  GList *modes, *l;

  preferred_mode = cc_display_monitor_get_preferred_mode (monitor);
  current_mode = cc_display_monitor_get_current_mode (monitor);
  modes = cc_display_monitor_get_modes (monitor);

  for (l = modes; l; l = l->next)
    {
      CcDisplayMode *mode = l->data;
      int resolution_width, resolution_height;
      int preferred_scale;

      cc_display_mode_get_resolution (mode,
                                      &resolution_width, &resolution_height);
      preferred_scale = cc_display_mode_get_preferred_scale (mode);

      g_print ("  %dx%d [preferred scale = %d]%s%s\n",
               resolution_width, resolution_height,
               preferred_scale,
               mode == preferred_mode ? " PREFERRED" : "",
               mode == current_mode ? " CURRENT" : "");
    }
}

static void
list_logical_monitor_monitors (CcDisplayLogicalMonitor *logical_monitor)
{
  GList *monitors;
  GList *l;

  monitors = cc_display_logical_monitor_get_monitors (logical_monitor);
  for (l = monitors; l; l = l->next)
    {
      CcDisplayMonitor *monitor = l->data;
      const char *connector = cc_display_monitor_get_connector (monitor);

      g_print ("  %s\n", connector);
    }
}

static int
list_monitors (CcDisplayConfigManager *config_manager)
{
  CcDisplayConfig *config;
  GError *error = NULL;
  GList *l;

  config = cc_display_config_manager_new_current (config_manager, &error);
  if (!config)
    {
      g_print ("Failed to get current config: %s\n", error->message);
      return EXIT_FAILURE;
    }

  for (l = cc_display_config_get_monitors (config); l; l = l->next)
    {
      CcDisplayMonitor *monitor = l->data;
      const char *connector = cc_display_monitor_get_connector (monitor);
      bool is_active = cc_display_monitor_is_active (monitor);
      bool is_builtin_display = cc_display_monitor_is_builtin_display (monitor);

      g_print ("Monitor [ %s ] %s%s\n",
               connector,
               is_active ? "ON" : "OFF",
               is_builtin_display ? " BUILTIN" : "");
      list_modes (monitor);
    }

  for (l = cc_display_config_get_logical_monitors (config); l; l = l->next)
    {
      CcDisplayLogicalMonitor *logical_monitor = l->data;
      cairo_rectangle_int_t layout;
      bool is_primary;
      int scale;

      cc_display_logical_monitor_get_layout (logical_monitor, &layout);
      is_primary = cc_display_logical_monitor_is_primary (logical_monitor);
      scale = cc_display_logical_monitor_get_scale (logical_monitor);

      g_print ("Logical monitor [ %dx%d+%d+%d ]%s, scale = %d\n",
               layout.width, layout.height, layout.x, layout.y,
               is_primary ? ", PRIMARY" : "",
               scale);
      list_logical_monitor_monitors (logical_monitor);
    }

  return EXIT_SUCCESS;
}

int
main (int argc,
      char *argv[])
{
  GError *error = NULL;
  GOptionContext *context;
  CcDisplayConfigManager *config_manager;

  context = g_option_context_new ("- display configuration test utility");
  g_option_context_add_main_entries (context, entries, NULL);
  if (!g_option_context_parse (context, &argc, &argv, &error))
    {
      g_print ("Option parsing failed: %s\n", error->message);
      return EXIT_FAILURE;
    }

  config_manager = cc_display_config_manager_new (&error);
  if (!config_manager)
    {
      g_print ("Failed to creata config manager: %s\n", error->message);
      return EXIT_SUCCESS;
    }

  if (arg_list)
    return list_monitors (config_manager);

  g_print ("%s", g_option_context_get_help (context, FALSE, NULL));
  return EXIT_FAILURE;
}
