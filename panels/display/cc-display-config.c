/*
 * Copyright (C) 2016-2017  Red Hat, Inc.
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
#include "cc-dbus-display-config.h"

#include <glib-object.h>
#include <stdint.h>

#define CC_DBUS_DISPLAY_CONFIG_MODE_FLAGS_PREFERRED (1 << 0)
#define CC_DBUS_DISPLAY_CONFIG_MODE_FLAGS_CURRENT (1 << 1)

typedef struct _CcDisplayMode
{
  int resolution_width;
  int resolution_height;
  double refresh_rate;
  int preferred_scale;
  unsigned int flags;
} CcDisplayMode;

typedef struct _CcDisplayMonitor
{
  char *connector;
  char *vendor;
  char *product;
  char *serial;

  GList *modes;
  CcDisplayMode *current_mode;
  CcDisplayMode *preferred_mode;
} CcDisplayMonitor;

typedef struct _CcDisplayLogicalMonitor
{
  cairo_rectangle_int_t layout;
  int scale;
  GList *monitors;
  bool is_primary;
} CcDisplayLogicalMonitor;

struct _CcDisplayConfig
{
  GList *monitors;
  GList *logical_monitors;
  int max_screen_width;
  int max_screen_height;
};

const char *
cc_display_monitor_get_connector (CcDisplayMonitor *monitor)
{
  return monitor->connector;
}

bool
cc_display_monitor_is_active (CcDisplayMonitor *monitor)
{
  return monitor->current_mode != NULL;
}

bool
cc_display_monitor_is_builtin_display (CcDisplayMonitor *monitor)
{
  return false;
}

CcDisplayMode *
cc_display_monitor_get_current_mode (CcDisplayMonitor *monitor)
{
  return monitor->current_mode;
}

CcDisplayMode *
cc_display_monitor_get_preferred_mode (CcDisplayMonitor *monitor)
{
  return monitor->preferred_mode;
}

GList *
cc_display_monitor_get_modes (CcDisplayMonitor *monitor)
{
  return monitor->modes;
}

void
cc_display_mode_get_resolution (CcDisplayMode *mode,
                                int *width,
                                int *height)
{
  *width = mode->resolution_width;
  *height = mode->resolution_height;
}

int
cc_display_mode_get_preferred_scale (CcDisplayMode *mode)
{
  return mode->preferred_scale;
}

GList *
cc_display_config_get_monitors (CcDisplayConfig *config)
{
  return config->monitors;
}

GList *
cc_display_config_get_logical_monitors (CcDisplayConfig *config)
{
  return config->logical_monitors;
}

#define MODE_FORMAT "(iidiu)"
#define MODES_FORMAT "a" MODE_FORMAT
#define MONITOR_SPEC_FORMAT "(ssss)"
#define MONITOR_FORMAT "(" MONITOR_SPEC_FORMAT MODES_FORMAT "a{sv})"
#define MONITORS_FORMAT "a" MONITOR_FORMAT

#define LOGICAL_MONITOR_FORMAT "(iiiia" MONITOR_SPEC_FORMAT "iba{sv})"
#define LOGICAL_MONITORS_FORMAT "a" LOGICAL_MONITOR_FORMAT

static CcDisplayMode *
cc_display_mode_new_from_variant (GVariant *mode_variant)
{
  CcDisplayMode *mode;
  uint32_t flags;
  int32_t resolution_width;
  int32_t resolution_height;
  double refresh_rate;
  int32_t preferred_scale;

  g_variant_get (mode_variant, MODE_FORMAT,
                 &resolution_width,
                 &resolution_height,
                 &refresh_rate,
                 &preferred_scale,
                 &flags);

  mode = g_new0 (CcDisplayMode, 1);
  *mode = (CcDisplayMode) {
    .flags = flags,
    .resolution_width = resolution_width,
    .resolution_height = resolution_height,
    .refresh_rate = refresh_rate,
    .preferred_scale = preferred_scale
  };

  return mode;
}

static CcDisplayMonitor *
cc_display_monitor_new_from_variant (GVariant *monitor_variant)
{
  CcDisplayMonitor *monitor;
  GVariantIter *modes_iter;
  GVariant *properties;
  GVariant *mode_variant;

  monitor = g_new0 (CcDisplayMonitor, 1);

  g_variant_get (monitor_variant, MONITOR_FORMAT,
                 &monitor->connector,
                 &monitor->vendor,
                 &monitor->product,
                 &monitor->serial,
                 &modes_iter,
                 &properties);

  while ((mode_variant = g_variant_iter_next_value (modes_iter)))
    {
      CcDisplayMode *mode;

      mode = cc_display_mode_new_from_variant (mode_variant);
      monitor->modes = g_list_append (monitor->modes, mode);

      if (mode->flags & CC_DBUS_DISPLAY_CONFIG_MODE_FLAGS_PREFERRED)
        {
          g_warn_if_fail (!monitor->preferred_mode);
          monitor->preferred_mode = mode;
        }

      if (mode->flags & CC_DBUS_DISPLAY_CONFIG_MODE_FLAGS_CURRENT)
        {
          g_warn_if_fail (!monitor->current_mode);
          monitor->current_mode = mode;
        }

      g_variant_unref (mode_variant);
    }
  g_variant_iter_free (modes_iter);

  return monitor;
}

#undef MODE_FORMAT
#undef MODES_FORMAT
#undef MONITOR_FORMAT
#undef MONITORS_FORMAT

static void
get_monitors_from_variant (CcDisplayConfig *config,
                           GVariant *monitors_variant)
{
  GVariantIter monitor_iter;
  GVariant *monitor_variant;

  g_assert (!config->monitors);

  g_variant_iter_init (&monitor_iter, monitors_variant);
  while ((monitor_variant = g_variant_iter_next_value (&monitor_iter)))
    {
      CcDisplayMonitor *monitor;

      monitor = cc_display_monitor_new_from_variant (monitor_variant);
      config->monitors = g_list_append (config->monitors, monitor);

      g_variant_unref (monitor_variant);
    }
}

static CcDisplayMonitor *
monitor_from_spec (CcDisplayConfig *config,
                   const char *connector,
                   const char *vendor,
                   const char *product,
                   const char *serial)
{
  GList *l;

  for (l = config->monitors; l; l = l->next)
    {
      CcDisplayMonitor *monitor = l->data;

      if (g_strcmp0 (monitor->connector, connector) == 0 &&
          g_strcmp0 (monitor->vendor, vendor) == 0 &&
          g_strcmp0 (monitor->product, product) == 0 &&
          g_strcmp0 (monitor->serial, serial) == 0)
        return monitor;
    }

  return NULL;
}

static CcDisplayLogicalMonitor *
cc_display_logical_monitor_new_from_variant (CcDisplayConfig *config,
                                             GVariant *logical_monitor_variant)
{
  CcDisplayLogicalMonitor *logical_monitor;
  GVariantIter *monitor_specs_iter;
  GVariant *monitor_spec_variant;
  int x, y, width, height, scale;
  gboolean is_primary;
  GVariant *properties;

  logical_monitor = g_new0 (CcDisplayLogicalMonitor, 1);

  g_variant_get (logical_monitor_variant, LOGICAL_MONITOR_FORMAT,
                 &x,
                 &y,
                 &width,
                 &height,
                 &monitor_specs_iter,
                 &scale,
                 &is_primary,
                 &properties);

  logical_monitor->layout = (cairo_rectangle_int_t) {
    .x = x,
    .y = y,
    .width = width,
    .height = height
  };
  logical_monitor->scale = scale;
  logical_monitor->is_primary = is_primary;

  while ((monitor_spec_variant = g_variant_iter_next_value (monitor_specs_iter)))
    {
      CcDisplayMonitor *monitor;
      g_autofree char *connector= NULL;
      g_autofree char *vendor = NULL;
      g_autofree char *product = NULL;
      g_autofree char *serial = NULL;

      g_variant_get (monitor_spec_variant, MONITOR_SPEC_FORMAT,
                     &connector, &vendor, &product, &serial);

      monitor = monitor_from_spec (config, connector, vendor, product, serial);
      if (!monitor)
        {
          g_warning ("Couldn't find monitor given spec: %s, %s, %s, %s\n",
                     connector, vendor, product, serial);
          continue;
        }

      logical_monitor->monitors = g_list_append (logical_monitor->monitors,
                                                 monitor);

      g_variant_unref (monitor_spec_variant);
    }
  g_variant_iter_free (monitor_specs_iter);

  return logical_monitor;
}

static void
cc_display_logical_monitor_free (CcDisplayLogicalMonitor *logical_monitor)
{
  g_list_free (logical_monitor->monitors);
  g_free (logical_monitor);
}

GList *
cc_display_logical_monitor_get_monitors (CcDisplayLogicalMonitor *logical_monitor)
{
  return logical_monitor->monitors;
}

bool
cc_display_logical_monitor_is_primary (CcDisplayLogicalMonitor *logical_monitor)
{
  return logical_monitor->is_primary;
}

void
cc_display_logical_monitor_get_layout (CcDisplayLogicalMonitor *logical_monitor,
                                       cairo_rectangle_int_t *layout)
{
  *layout = logical_monitor->layout;
}

int
cc_display_logical_monitor_get_scale (CcDisplayLogicalMonitor *logical_monitor)
{
  return logical_monitor->scale;
}

static void
get_logical_monitors_from_variant (CcDisplayConfig *config,
                                   GVariant *logical_monitors_variant)
{
  GVariantIter logical_monitor_iter;

  g_variant_iter_init (&logical_monitor_iter, logical_monitors_variant);
  while (true)
    {
      GVariant *logical_monitor_variant =
        g_variant_iter_next_value (&logical_monitor_iter);
      CcDisplayLogicalMonitor *logical_monitor;

      if (!logical_monitor_variant)
        break;

      logical_monitor =
        cc_display_logical_monitor_new_from_variant (config,
                                                     logical_monitor_variant);
      config->logical_monitors = g_list_append (config->logical_monitors,
                                                logical_monitor);
    }
}

static void
get_max_screen_size_from_variant (CcDisplayConfig *config,
                                  GVariant *logical_monitors_variant)
{
}

static bool
get_current_state (CcDisplayConfig *config,
                   CcDbusDisplayConfig *proxy,
                   GError **error)
{
  unsigned int serial;
  GVariant *monitors_variant;
  GVariant *logical_monitors_variant;
  GVariant *max_screen_size_variant;

  if (!cc_dbus_display_config_call_get_current_state_sync (proxy,
                                                           &serial,
                                                           &monitors_variant,
                                                           &logical_monitors_variant,
                                                           &max_screen_size_variant,
                                                           NULL,
                                                           error))
    return false;

  get_monitors_from_variant (config, monitors_variant);
  get_logical_monitors_from_variant (config, logical_monitors_variant);
  get_max_screen_size_from_variant (config, max_screen_size_variant);

  return true;
}

static void
cc_display_monitor_free (CcDisplayMonitor *monitor)
{
  g_list_free_full (monitor->modes, g_free);
  g_free (monitor);
}

CcDisplayConfig *
cc_display_config_new_current (CcDbusDisplayConfig *proxy,
                               GError **error)
{
  g_autofree CcDisplayConfig *config = NULL;

  config = g_new0 (CcDisplayConfig, 1);

  if (!get_current_state (config, proxy, error))
    return NULL;

  return g_steal_pointer (&config);
}

void
cc_display_config_free (CcDisplayConfig *config)
{
  g_list_free_full (config->logical_monitors,
                    (GDestroyNotify) cc_display_logical_monitor_free);
  g_list_free_full (config->monitors,
                    (GDestroyNotify) cc_display_monitor_free);
  g_free (config);
}
