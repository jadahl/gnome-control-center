/*
 * Copyright (C) 2016  Red Hat, Inc.
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

#include "cc-display-config-manager.h"

#include "cc-display-config.h"
#include "cc-dbus-display-config.h"

struct _CcDisplayConfigManager
{
  GObject parent;
  CcDbusDisplayConfig *proxy;
};

static void
cc_display_config_manager_initable_init_iface (GInitableIface *iface);

G_DEFINE_TYPE_WITH_CODE (CcDisplayConfigManager, cc_display_config_manager,
                         G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (G_TYPE_INITABLE,
                                                cc_display_config_manager_initable_init_iface));

CcDisplayState *
cc_display_config_manager_new_current_state (CcDisplayConfigManager *manager,
                                             GError **error)
{
  return cc_display_state_new_current (manager->proxy, error);
}

CcDisplayConfigManager *
cc_display_config_manager_new (GError **error)
{
  g_autoptr(CcDisplayConfigManager) manager = NULL;

  manager = g_object_new (CC_TYPE_DISPLAY_CONFIG_MANAGER, NULL);
  if (!g_initable_init (G_INITABLE (manager), NULL, error))
    return NULL;

  return g_steal_pointer (&manager);
}

static gboolean
cc_display_config_manager_initable_init (GInitable *initable,
                                         GCancellable *cancellable,
                                         GError **error)
{
  CcDisplayConfigManager *manager = CC_DISPLAY_CONFIG_MANAGER (initable);
  CcDbusDisplayConfig *proxy;

  proxy = cc_dbus_display_config_proxy_new_for_bus_sync (G_BUS_TYPE_SESSION,
                                                         G_DBUS_PROXY_FLAGS_NONE,
                                                         "org.gnome.Mutter.DisplayConfig",
                                                         "/org/gnome/Mutter/DisplayConfig",
                                                         NULL, error);
  if (!proxy)
    return FALSE;

  manager->proxy = proxy;

  return TRUE;
}

static void
cc_display_config_manager_initable_init_iface (GInitableIface *iface)
{
  iface->init = cc_display_config_manager_initable_init;
}

static void
cc_display_config_manager_init (CcDisplayConfigManager *config_manager)
{
}

static void
cc_display_config_manager_class_init (CcDisplayConfigManagerClass *klass)
{
}
