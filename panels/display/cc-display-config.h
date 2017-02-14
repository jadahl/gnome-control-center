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

#ifndef _CC_DISPLAY_CONFIG_H
#define _CC_DISPLAY_CONFIG_H

#include <cairo.h>
#include <glib.h>
#include <stdbool.h>

#include "cc-dbus-display-config.h"

typedef struct _CcDisplayState CcDisplayState;
typedef struct _CcDisplayMonitor CcDisplayMonitor;
typedef struct _CcDisplayLogicalMonitor CcDisplayLogicalMonitor;

typedef struct _CcDisplayMode CcDisplayMode;

CcDisplayState *cc_display_state_new_current (CcDbusDisplayConfig *proxy,
                                              GError **error);
void cc_display_state_free (CcDisplayState *state);

unsigned int cc_display_state_get_serial (CcDisplayState *state);
GList *cc_display_state_get_monitors (CcDisplayState *state);
GList *cc_display_state_get_logical_monitors (CcDisplayState *state);
gboolean cc_display_state_get_max_screen_size (CcDisplayState *state,
                                               int *max_width,
                                               int *max_height);
void cc_display_state_get_supported_scales (CcDisplayState *state,
                                            double **supported_scales,
                                            int *n_supported_scales);

GList * cc_display_logical_monitor_get_monitors (CcDisplayLogicalMonitor *logical_monitor);
bool cc_display_logical_monitor_is_primary (CcDisplayLogicalMonitor *logical_monitor);
void cc_display_logical_monitor_calculate_layout (CcDisplayLogicalMonitor *logical_monitor,
                                                  cairo_rectangle_int_t *layout);
int cc_display_logical_monitor_get_scale (CcDisplayLogicalMonitor *logical_monitor);

bool cc_display_monitor_is_active (CcDisplayMonitor *monitor);
const char * cc_display_monitor_get_connector (CcDisplayMonitor *monitor);
bool cc_display_monitor_is_builtin_display (CcDisplayMonitor *monitor);

bool cc_display_monitor_supports_underscanning (CcDisplayMonitor *monitor);
bool cc_display_monitor_is_underscanning (CcDisplayMonitor *monitor);

GList * cc_display_monitor_get_modes (CcDisplayMonitor *monitor);
CcDisplayMode * cc_display_monitor_get_current_mode (CcDisplayMonitor *monitor);
CcDisplayMode * cc_display_monitor_get_preferred_mode (CcDisplayMonitor *monitor);

void cc_display_mode_get_resolution (CcDisplayMode *mode,
				     int *width,
				     int *height);
double cc_display_mode_get_refresh_rate (CcDisplayMode *mode);
double cc_display_mode_get_preferred_scale (CcDisplayMode *mode);

#endif /* _CC_DISPLAY_CONFIG_H */
