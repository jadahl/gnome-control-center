/*
 * Copyright (C) 2010 Intel, Inc
 * Copyright (C) 2016 Endless, Inc
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
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 * Author: Thomas Wood <thomas.wood@intel.com>
 *         Georges Basile Stavracas Neto <gbsneto@gnome.org>
 *
 */

#include <glib/gi18n.h>

#include "cc-keyboard-item.h"
#include "cc-keyboard-manager.h"
#include "cc-keyboard-option.h"
#include "cc-keyboard-panel.h"
#include "cc-keyboard-resources.h"
#include "cc-keyboard-shortcut-editor.h"

#include "keyboard-shortcuts.h"

#include "cc-util.h"

typedef struct {
  CcKeyboardItem *item;
  gchar          *section_title;
  gchar          *section_id;
} RowData;

struct _CcKeyboardPanel
{
  CcPanel             parent;

  /* Search */
  GtkWidget          *empty_search_placeholder;
  GtkWidget          *search_bar;
  GtkWidget          *search_button;
  GtkWidget          *search_entry;
  guint               search_bar_handler_id;

  /* Shortcuts */
  GtkWidget          *listbox;
  GtkListBoxRow      *add_shortcut_row;
  GtkSizeGroup       *accelerator_sizegroup;

  /* Custom shortcut dialog */
  GtkWidget          *shortcut_editor;

  GRegex             *pictures_regex;

  CcKeyboardManager  *manager;
};

CC_PANEL_REGISTER (CcKeyboardPanel, cc_keyboard_panel)

enum {
  PROP_0,
  PROP_PARAMETERS
};

static const gchar* custom_css =
"button.reset-shortcut-button {"
"    padding: 0;"
"}";

/* RowData functions */
static RowData *
row_data_new (CcKeyboardItem *item,
              const gchar    *section_id,
              const gchar    *section_title)
{
  RowData *data;

  data = g_new0 (RowData, 1);
  data->item = g_object_ref (item);
  data->section_id = g_strdup (section_id);
  data->section_title = g_strdup (section_title);

  return data;
}

static void
row_data_free (RowData *data)
{
  g_object_unref (data->item);
  g_free (data->section_id);
  g_free (data->section_title);
  g_free (data);
}

static gboolean
transform_binding_to_accel (GBinding     *binding,
                            const GValue *from_value,
                            GValue       *to_value,
                            gpointer      user_data)
{
  CcKeyboardItem *item;
  gchar *accelerator;

  item = CC_KEYBOARD_ITEM (g_binding_get_source (binding));

  /* Embolden the label when the shortcut is modified */
  if (!cc_keyboard_item_is_value_default (item))
    {
      gchar *tmp;

      tmp = convert_keysym_state_to_string (item->keyval,
                                            item->mask,
                                            item->keycode);

      accelerator = g_strdup_printf ("<b>%s</b>", tmp);

      g_free (tmp);
    }
  else
    {
      accelerator = convert_keysym_state_to_string (item->keyval,
                                                    item->mask,
                                                    item->keycode);
    }

  g_value_take_string (to_value, accelerator);

  return TRUE;
}

static void
shortcut_modified_changed_cb (CcKeyboardItem *item,
                              GParamSpec     *pspec,
                              GtkWidget      *button)
{
  gtk_widget_set_child_visible (button, !cc_keyboard_item_is_value_default (item));
}

static void
reset_shortcut_cb (GtkWidget      *reset_button,
                   CcKeyboardItem *item)
{
  CcKeyboardPanel *self;

  self = CC_KEYBOARD_PANEL (gtk_widget_get_ancestor (reset_button, CC_TYPE_KEYBOARD_PANEL));

  cc_keyboard_manager_reset_shortcut (self->manager, item);
}

static void
add_item (CcKeyboardPanel *self,
          CcKeyboardItem  *item,
          const gchar     *section_id,
          const gchar     *section_title)
{
  GtkWidget *row, *box, *label, *reset_button;

  /* Horizontal box */
  box = g_object_new (GTK_TYPE_BOX,
                      "orientation", GTK_ORIENTATION_HORIZONTAL,
                      "spacing", 18,
                      "margin-start", 6,
                      "margin-end", 6,
                      "margin-bottom", 4,
                      "margin-top", 4,
                      NULL);

  /* Shortcut title */
  label = gtk_label_new (item->description);
  gtk_label_set_xalign (GTK_LABEL (label), 0.0);
  gtk_label_set_line_wrap (GTK_LABEL (label), TRUE);
  gtk_label_set_line_wrap_mode (GTK_LABEL (label), PANGO_WRAP_WORD_CHAR);
  gtk_widget_set_hexpand (label, TRUE);

  g_object_bind_property (item,
                          "description",
                          label,
                          "label",
                          G_BINDING_DEFAULT);

  gtk_container_add (GTK_CONTAINER (box), label);

  /* Shortcut accelerator */
  label = gtk_label_new ("");
  gtk_label_set_xalign (GTK_LABEL (label), 0.0);
  gtk_label_set_use_markup (GTK_LABEL (label), TRUE);

  gtk_size_group_add_widget (self->accelerator_sizegroup, label);

  g_object_bind_property_full (item,
                               "binding",
                               label,
                              "label",
                               G_BINDING_DEFAULT | G_BINDING_SYNC_CREATE,
                               transform_binding_to_accel,
                               NULL, NULL, NULL);

  gtk_container_add (GTK_CONTAINER (box), label);

  gtk_style_context_add_class (gtk_widget_get_style_context (label), "dim-label");

  /* Reset shortcut button */
  reset_button = gtk_button_new_from_icon_name ("edit-clear-symbolic", GTK_ICON_SIZE_BUTTON);
  gtk_widget_set_valign (reset_button, GTK_ALIGN_CENTER);

  gtk_button_set_relief (GTK_BUTTON (reset_button), GTK_RELIEF_NONE);
  gtk_widget_set_child_visible (reset_button, !cc_keyboard_item_is_value_default (item));

  gtk_widget_set_tooltip_text (reset_button, _("Reset the shortcut to its default value"));

  gtk_container_add (GTK_CONTAINER (box), reset_button);

  gtk_style_context_add_class (gtk_widget_get_style_context (reset_button), "flat");
  gtk_style_context_add_class (gtk_widget_get_style_context (reset_button), "circular");
  gtk_style_context_add_class (gtk_widget_get_style_context (reset_button), "reset-shortcut-button");

  g_signal_connect (item,
                    "notify::is-value-default",
                    G_CALLBACK (shortcut_modified_changed_cb),
                    reset_button);

  g_signal_connect (reset_button,
                    "clicked",
                    G_CALLBACK (reset_shortcut_cb),
                    item);

  /* The row */
  row = gtk_list_box_row_new ();
  gtk_container_add (GTK_CONTAINER (row), box);

  gtk_widget_show_all (row);

  g_object_set_data_full (G_OBJECT (row),
                          "data",
                          row_data_new (item, section_id, section_title),
                          (GDestroyNotify) row_data_free);

  gtk_container_add (GTK_CONTAINER (self->listbox), row);
}

static void
remove_item (CcKeyboardPanel *self,
             CcKeyboardItem  *item)
{
  GList *children, *l;

  children = gtk_container_get_children (GTK_CONTAINER (self->listbox));

  for (l = children; l != NULL; l = l->next)
    {
      RowData *row_data;

      row_data = g_object_get_data (l->data, "data");

      if (row_data->item == item)
        {
          gtk_container_remove (GTK_CONTAINER (self->listbox), l->data);
          break;
        }
    }

  g_list_free (children);
}

static gint
sort_function (GtkListBoxRow *a,
               GtkListBoxRow *b,
               gpointer       user_data)
{
  CcKeyboardPanel *self;
  RowData *a_data, *b_data;
  gint retval;

  self = user_data;

  if (a == self->add_shortcut_row)
    return 1;

  if (b == self->add_shortcut_row)
    return -1;

  a_data = g_object_get_data (G_OBJECT (a), "data");
  b_data = g_object_get_data (G_OBJECT (b), "data");

  /* Put custom shortcuts below everything else */
  if (a_data->item->type == CC_KEYBOARD_ITEM_TYPE_GSETTINGS_PATH)
    return 1;
  else if (b_data->item->type == CC_KEYBOARD_ITEM_TYPE_GSETTINGS_PATH)
    return -1;

  retval = g_strcmp0 (a_data->section_title, b_data->section_title);

  if (retval != 0)
    return retval;

  return g_strcmp0 (a_data->item->description, b_data->item->description);
}

static void
header_function (GtkListBoxRow *row,
                 GtkListBoxRow *before,
                 gpointer       user_data)
{
  CcKeyboardPanel *self;
  gboolean add_header;
  RowData *data;

  self = user_data;
  add_header = FALSE;

  /* The + row always has a separator */
  if (row == self->add_shortcut_row)
    {
      GtkWidget *separator = gtk_separator_new (GTK_ORIENTATION_HORIZONTAL);
      gtk_widget_show (separator);

      gtk_list_box_row_set_header (row, separator);

      return;
    }

  data = g_object_get_data (G_OBJECT (row), "data");

  if (before)
    {
      RowData *before_data = g_object_get_data (G_OBJECT (before), "data");

      if (before_data)
        add_header = g_strcmp0 (before_data->section_id, data->section_id) != 0;
    }
  else
    {
      add_header = TRUE;
    }

  if (add_header)
    {
      GtkWidget *box, *label;
      gchar *markup;

      box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 6);
      gtk_widget_set_margin_top (box, before ? 18 : 6);

      markup = g_strdup_printf ("<b>%s</b>", _(data->section_title));
      label = g_object_new (GTK_TYPE_LABEL,
                            "label", markup,
                            "use-markup", TRUE,
                            "xalign", 0.0,
                            "margin-start", 6,
                            NULL);

      gtk_style_context_add_class (gtk_widget_get_style_context (label), "dim-label");

      gtk_container_add (GTK_CONTAINER (box), label);
      gtk_container_add (GTK_CONTAINER (box), gtk_separator_new (GTK_ORIENTATION_HORIZONTAL));

      gtk_list_box_row_set_header (row, box);

      gtk_widget_show_all (box);

      g_free (markup);
    }
  else
    {
      gtk_list_box_row_set_header (row, NULL);
    }
}

static gboolean
filter_function (GtkListBoxRow *row,
                 gpointer       user_data)
{
  CcKeyboardPanel *self = user_data;
  RowData *data;
  gboolean retval;
  gchar *search, *name;

  if (gtk_entry_get_text_length (GTK_ENTRY (self->search_entry)) == 0)
    return TRUE;

  /* When searching, the '+' row is always hidden */
  if (row == self->add_shortcut_row)
    return FALSE;

  data = g_object_get_data (G_OBJECT (row), "data");
  name = cc_util_normalize_casefold_and_unaccent (data->item->description);
  search = cc_util_normalize_casefold_and_unaccent (gtk_entry_get_text (GTK_ENTRY (self->search_entry)));

  retval = strstr (name, search) != NULL;

  g_free (search);
  g_free (name);

  return retval;
}

static void
shortcut_row_activated (GtkWidget       *button,
                        GtkListBoxRow   *row,
                        CcKeyboardPanel *self)
{
  CcKeyboardShortcutEditor *editor;

  editor = CC_KEYBOARD_SHORTCUT_EDITOR (self->shortcut_editor);

  if (row != self->add_shortcut_row)
    {
      RowData *data = g_object_get_data (G_OBJECT (row), "data");

      cc_keyboard_shortcut_editor_set_mode (editor, CC_SHORTCUT_EDITOR_EDIT);
      cc_keyboard_shortcut_editor_set_item (editor, data->item);
    }
  else
    {
      cc_keyboard_shortcut_editor_set_mode (editor, CC_SHORTCUT_EDITOR_CREATE);
      cc_keyboard_shortcut_editor_set_item (editor, NULL);
    }

  gtk_widget_show (self->shortcut_editor);
}

static void
cc_keyboard_panel_set_property (GObject      *object,
                               guint         property_id,
                               const GValue *value,
                               GParamSpec   *pspec)
{
  switch (property_id)
    {
    case PROP_PARAMETERS:
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    }
}

static const char *
cc_keyboard_panel_get_help_uri (CcPanel *panel)
{
  return "help:gnome-help/keyboard";
}

static void
cc_keyboard_panel_finalize (GObject *object)
{
  CcKeyboardPanel *self = CC_KEYBOARD_PANEL (object);
  GtkWidget *window;

  g_clear_pointer (&self->pictures_regex, g_regex_unref);
  g_clear_object (&self->accelerator_sizegroup);

  cc_keyboard_option_clear_all ();

  if (self->search_bar_handler_id != 0)
    {
      window = cc_shell_get_toplevel (cc_panel_get_shell (CC_PANEL (self)));
      g_signal_handler_disconnect (window, self->search_bar_handler_id);
    }

  G_OBJECT_CLASS (cc_keyboard_panel_parent_class)->finalize (object);
}

static void
cc_keyboard_panel_constructed (GObject *object)
{
  CcKeyboardPanel *self = CC_KEYBOARD_PANEL (object);
  GtkWindow *toplevel;

  G_OBJECT_CLASS (cc_keyboard_panel_parent_class)->constructed (object);

  /* Setup the dialog's transient parent */
  toplevel = GTK_WINDOW (cc_shell_get_toplevel (cc_panel_get_shell (CC_PANEL (self))));
  gtk_window_set_transient_for (GTK_WINDOW (self->shortcut_editor), toplevel);

  cc_shell_embed_widget_in_header (cc_panel_get_shell (CC_PANEL (self)), self->search_button);

  self->search_bar_handler_id =
    g_signal_connect_swapped (toplevel,
                              "key-press-event",
                              G_CALLBACK (gtk_search_bar_handle_event),
                              self->search_bar);
}

static void
cc_keyboard_panel_class_init (CcKeyboardPanelClass *klass)
{
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  CcPanelClass *panel_class = CC_PANEL_CLASS (klass);

  panel_class->get_help_uri = cc_keyboard_panel_get_help_uri;

  object_class->set_property = cc_keyboard_panel_set_property;
  object_class->finalize = cc_keyboard_panel_finalize;
  object_class->constructed = cc_keyboard_panel_constructed;

  g_object_class_override_property (object_class, PROP_PARAMETERS, "parameters");

  gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/control-center/keyboard/gnome-keyboard-panel.ui");

  gtk_widget_class_bind_template_child (widget_class, CcKeyboardPanel, add_shortcut_row);
  gtk_widget_class_bind_template_child (widget_class, CcKeyboardPanel, empty_search_placeholder);
  gtk_widget_class_bind_template_child (widget_class, CcKeyboardPanel, listbox);
  gtk_widget_class_bind_template_child (widget_class, CcKeyboardPanel, search_bar);
  gtk_widget_class_bind_template_child (widget_class, CcKeyboardPanel, search_button);
  gtk_widget_class_bind_template_child (widget_class, CcKeyboardPanel, search_entry);

  gtk_widget_class_bind_template_callback (widget_class, shortcut_row_activated);
}

static void
cc_keyboard_panel_init (CcKeyboardPanel *self)
{
  GtkCssProvider *provider;

  g_resources_register (cc_keyboard_get_resource ());

  gtk_widget_init_template (GTK_WIDGET (self));

  /* Custom CSS */
  provider = gtk_css_provider_new ();
  gtk_css_provider_load_from_data (provider, custom_css, -1, NULL);

  gtk_style_context_add_provider_for_screen (gdk_screen_get_default (),
                                             GTK_STYLE_PROVIDER (provider),
                                             GTK_STYLE_PROVIDER_PRIORITY_APPLICATION + 1);

  g_object_unref (provider);

  /* Shortcut manager */
  self->manager = cc_keyboard_manager_new ();

  /* Use a sizegroup to make the accelerator labels the same width */
  self->accelerator_sizegroup = gtk_size_group_new (GTK_SIZE_GROUP_HORIZONTAL);

  /* Shortcut editor dialog */
  self->shortcut_editor = cc_keyboard_shortcut_editor_new (self->manager);

  g_signal_connect_swapped (self->manager,
                            "shortcut-added",
                            G_CALLBACK (add_item),
                            self);

  g_signal_connect_swapped (self->manager,
                            "shortcut-removed",
                            G_CALLBACK (remove_item),
                            self);

  cc_keyboard_manager_load_shortcuts (self->manager);

  /* Shortcut editor dialog */
  self->shortcut_editor = cc_keyboard_shortcut_editor_new (self->manager);

  /* Setup the shortcuts listbox */
  gtk_list_box_set_sort_func (GTK_LIST_BOX (self->listbox),
                              sort_function,
                              self,
                              NULL);

  gtk_list_box_set_header_func (GTK_LIST_BOX (self->listbox),
                                header_function,
                                self,
                                NULL);

  gtk_list_box_set_filter_func (GTK_LIST_BOX (self->listbox),
                                filter_function,
                                self,
                                NULL);

  gtk_list_box_set_placeholder (GTK_LIST_BOX (self->listbox), self->empty_search_placeholder);
}

