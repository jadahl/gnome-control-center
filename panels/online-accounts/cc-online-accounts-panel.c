/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */
/*
 * Copyright (C) 2011, 2012 Red Hat, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General
 * Public License along with this library; if not, see <http://www.gnu.org/licenses/>.
 *
 * Author: David Zeuthen <davidz@redhat.com>
 */

#include "config.h"

#include <gio/gio.h>
#include <string.h>
#include <glib/gi18n-lib.h>

#define GOA_API_IS_SUBJECT_TO_CHANGE
#include <goa/goa.h>
#define GOA_BACKEND_API_IS_SUBJECT_TO_CHANGE
#include <goabackend/goabackend.h>

#include "cc-online-accounts-panel.h"

#include "cc-online-accounts-add-account-dialog.h"
#include "cc-online-accounts-resources.h"

struct _CcGoaPanel
{
  CcPanel parent_instance;

  GoaClient *client;

  GtkWidget *accounts_listbox;
  GtkWidget *accounts_notebook;
  GtkWidget *accounts_tree_box;
  GtkWidget *accounts_tree_label;
  GtkWidget *add_account_button;
  GtkWidget *edit_account_dialog;
  GtkWidget *edit_account_headerbar;
  GtkWidget *toolbar;
  GtkWidget *toolbar_add_button;
  GtkWidget *toolbar_remove_button;
  GtkWidget *accounts_vbox;
};

static void on_listbox_selection_changed (GtkListBox    *listbox,
                                          GtkListBoxRow *selected_row,
                                          CcGoaPanel    *self);

static void on_toolbar_add_button_clicked (GtkToolButton *button,
                                           gpointer       user_data);
static void on_toolbar_remove_button_clicked (GtkToolButton *button,
                                              gpointer       user_data);

static void on_add_button_clicked (GtkButton *button,
                                   gpointer   user_data);

static void fill_accounts_listbox (CcGoaPanel *self);

static void on_account_added (GoaClient  *client,
                              GoaObject  *object,
                              gpointer    user_data);

static void on_account_changed (GoaClient  *client,
                                GoaObject  *object,
                                gpointer    user_data);

static void on_account_removed (GoaClient  *client,
                                GoaObject  *object,
                                gpointer    user_data);

static void select_account_by_id (CcGoaPanel    *panel,
                                  const gchar   *account_id);
static void     add_account          (CcGoaPanel *panel,
                                      GoaProvider *provider,
                                      GVariant *preseed);

CC_PANEL_REGISTER (CcGoaPanel, cc_goa_panel);

enum {
  PROP_0,
  PROP_PARAMETERS
};

static gint
sort_func (GtkListBoxRow *a,
           GtkListBoxRow *b,
           gpointer       user_data)
{
  GoaObject *a_obj, *b_obj;
  GoaAccount *a_account, *b_account;

  a_obj = g_object_get_data (G_OBJECT (a), "goa-object");
  a_account = goa_object_peek_account (a_obj);

  b_obj = g_object_get_data (G_OBJECT (b), "goa-object");
  b_account = goa_object_peek_account (b_obj);

  return g_strcmp0 (goa_account_get_id (a_account), goa_account_get_id (b_account));
}

static void
command_add (CcGoaPanel *panel,
             GVariant   *parameters)
{
  GVariant *v, *preseed = NULL;
  GoaProvider *provider = NULL;
  const gchar *provider_name = NULL;

  g_assert (panel != NULL);
  g_assert (parameters != NULL);

  switch (g_variant_n_children (parameters))
    {
      case 4:
        g_variant_get_child (parameters, 3, "v", &preseed);
      case 3:
        g_variant_get_child (parameters, 2, "v", &v);
        if (g_variant_is_of_type (v, G_VARIANT_TYPE_STRING))
          provider_name = g_variant_get_string (v, NULL);
        else
          g_warning ("Wrong type for the second argument (provider name) GVariant, expected 's' but got '%s'",
                     (gchar *)g_variant_get_type (v));
        g_variant_unref (v);
      case 2:
        /* Nothing to see here, move along */
      case 1:
        /* No flag to handle here */
        break;
      default:
        g_warning ("Unexpected parameters found, ignore request");
        goto out;
    }

  if (provider_name != NULL)
    {
      provider = goa_provider_get_for_provider_type (provider_name);
      if (provider == NULL)
        {
          g_warning ("Unable to get a provider for type '%s'", provider_name);
          goto out;
        }
    }

  add_account (panel, provider, preseed);

out:
  g_clear_object (&provider);
  g_clear_pointer (&preseed, g_variant_unref);
}

static void
cc_goa_panel_set_property (GObject *object,
                        guint property_id,
                        const GValue *value,
                        GParamSpec *pspec)
{
  switch (property_id)
    {
      case PROP_PARAMETERS:
        {
          GVariant *parameters, *v;
          const gchar *first_arg = NULL;

          parameters = g_value_get_variant (value);
          if (parameters == NULL)
            return;

          if (g_variant_n_children (parameters) > 0)
            {
                g_variant_get_child (parameters, 0, "v", &v);
                if (g_variant_is_of_type (v, G_VARIANT_TYPE_STRING))
                  first_arg = g_variant_get_string (v, NULL);
                else
                  g_warning ("Wrong type for the second argument GVariant, expected 's' but got '%s'",
                             (gchar *)g_variant_get_type (v));
                g_variant_unref (v);
            }

          if (g_strcmp0 (first_arg, "add") == 0)
            command_add (CC_GOA_PANEL (object), parameters);
          else if (first_arg != NULL)
            select_account_by_id (CC_GOA_PANEL (object), first_arg);

          return;
        }
    }

  G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
cc_goa_panel_finalize (GObject *object)
{
  CcGoaPanel *panel = CC_GOA_PANEL (object);

  g_clear_object (&panel->client);

  G_OBJECT_CLASS (cc_goa_panel_parent_class)->finalize (object);
}

static void
cc_goa_panel_init (CcGoaPanel *panel)
{
  GError *error;
  GNetworkMonitor *monitor;

  g_resources_register (cc_online_accounts_get_resource ());

  gtk_widget_init_template (GTK_WIDGET (panel));

  gtk_list_box_set_sort_func (GTK_LIST_BOX (panel->accounts_listbox),
                              sort_func,
                              panel,
                              NULL);

  monitor = g_network_monitor_get_default();

  g_object_bind_property (monitor, "network-available",
                          panel->toolbar_add_button, "sensitive",
                          G_BINDING_SYNC_CREATE);
  g_object_bind_property (monitor, "network-available",
                          panel->add_account_button, "sensitive",
                          G_BINDING_SYNC_CREATE);

  /* TODO: probably want to avoid _sync() ... */
  error = NULL;
  panel->client = goa_client_new_sync (NULL /* GCancellable */, &error);
  if (panel->client == NULL)
    {
      g_warning ("Error getting a GoaClient: %s (%s, %d)",
                 error->message, g_quark_to_string (error->domain), error->code);
      gtk_widget_set_sensitive (GTK_WIDGET (panel), FALSE);
      g_error_free (error);
      return;
    }

  g_signal_connect (panel->client,
                    "account-added",
                    G_CALLBACK (on_account_added),
                    panel);

  g_signal_connect (panel->client,
                    "account-changed",
                    G_CALLBACK (on_account_changed),
                    panel);

  g_signal_connect (panel->client,
                    "account-removed",
                    G_CALLBACK (on_account_removed),
                    panel);

  fill_accounts_listbox (panel);
}

static const char *
cc_goa_panel_get_help_uri (CcPanel *panel)
{
  return "help:gnome-help/accounts";
}

static void
cc_goa_panel_constructed (GObject *object)
{
  CcGoaPanel *self = CC_GOA_PANEL (object);
  GtkWindow *parent;

  /* Setup account editor dialog */
  parent = GTK_WINDOW (cc_shell_get_toplevel (cc_panel_get_shell (CC_PANEL (self))));

  gtk_window_set_transient_for (GTK_WINDOW (self->edit_account_dialog), parent);

  G_OBJECT_CLASS (cc_goa_panel_parent_class)->constructed (object);
}

static void
cc_goa_panel_class_init (CcGoaPanelClass *klass)
{
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  CcPanelClass *panel_class = CC_PANEL_CLASS (klass);

  panel_class->get_help_uri = cc_goa_panel_get_help_uri;

  object_class->set_property = cc_goa_panel_set_property;
  object_class->finalize = cc_goa_panel_finalize;
  object_class->constructed = cc_goa_panel_constructed;

  g_object_class_override_property (object_class, PROP_PARAMETERS, "parameters");

  gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/control-center/online-accounts/online-accounts.ui");

  gtk_widget_class_bind_template_child (widget_class, CcGoaPanel, accounts_listbox);
  gtk_widget_class_bind_template_child (widget_class, CcGoaPanel, accounts_notebook);
  gtk_widget_class_bind_template_child (widget_class, CcGoaPanel, accounts_tree_box);
  gtk_widget_class_bind_template_child (widget_class, CcGoaPanel, accounts_tree_label);
  gtk_widget_class_bind_template_child (widget_class, CcGoaPanel, accounts_vbox);
  gtk_widget_class_bind_template_child (widget_class, CcGoaPanel, add_account_button);
  gtk_widget_class_bind_template_child (widget_class, CcGoaPanel, edit_account_dialog);
  gtk_widget_class_bind_template_child (widget_class, CcGoaPanel, edit_account_headerbar);
  gtk_widget_class_bind_template_child (widget_class, CcGoaPanel, toolbar);
  gtk_widget_class_bind_template_child (widget_class, CcGoaPanel, toolbar_add_button);
  gtk_widget_class_bind_template_child (widget_class, CcGoaPanel, toolbar_remove_button);

  gtk_widget_class_bind_template_callback (widget_class, on_add_button_clicked);
  gtk_widget_class_bind_template_callback (widget_class, on_listbox_selection_changed);
  gtk_widget_class_bind_template_callback (widget_class, on_toolbar_add_button_clicked);
  gtk_widget_class_bind_template_callback (widget_class, on_toolbar_remove_button_clicked);
}

/* ---------------------------------------------------------------------------------------------------- */

static void
show_page (CcGoaPanel *panel,
           gint page_num)
{
  gtk_notebook_set_current_page (GTK_NOTEBOOK (panel->accounts_notebook), page_num);
}

static void
show_page_nothing_selected (CcGoaPanel *panel)
{
  show_page (panel, 0);

  gtk_widget_set_sensitive (panel->accounts_tree_box, FALSE);
  gtk_widget_show (panel->accounts_tree_label);
}

static void
show_page_account (CcGoaPanel  *panel,
                   GoaObject *object)
{
  GList *children;
  GList *l;
  GoaProvider *provider;
  GoaAccount *account;
  const gchar *provider_type;

  provider = NULL;

  show_page (panel, 1);
  gtk_widget_set_sensitive (panel->accounts_tree_box, TRUE);
  gtk_widget_hide (panel->accounts_tree_label);

  /* Out with the old */
  children = gtk_container_get_children (GTK_CONTAINER (panel->accounts_vbox));
  for (l = children; l != NULL; l = l->next)
    gtk_container_remove (GTK_CONTAINER (panel->accounts_vbox), GTK_WIDGET (l->data));
  g_list_free (children);

  account = goa_object_peek_account (object);
  provider_type = goa_account_get_provider_type (account);
  provider = goa_provider_get_for_provider_type (provider_type);

  if (provider != NULL)
    {
      goa_provider_show_account (provider,
                                 panel->client,
                                 object,
                                 GTK_BOX (panel->accounts_vbox),
                                 NULL,
                                 NULL);
    }

  /* Setup the dialog */
  gtk_header_bar_set_title (GTK_HEADER_BAR (panel->edit_account_headerbar),
                            goa_account_get_provider_name (account));

  gtk_header_bar_set_subtitle (GTK_HEADER_BAR (panel->edit_account_headerbar),
                               goa_account_get_presentation_identity (account));

  gtk_widget_show_all (panel->accounts_vbox);
  gtk_widget_show (panel->edit_account_dialog);

  g_clear_object (&provider);
}

/* ---------------------------------------------------------------------------------------------------- */

static void
select_account_by_id (CcGoaPanel  *panel,
                      const gchar *account_id)
{
  GtkListBoxRow *account_row;
  GList *children, *l;

  account_row = NULL;
  children = gtk_container_get_children (GTK_CONTAINER (panel->accounts_listbox));

  for (l = children; l != NULL; l = l->next)
    {
      GoaAccount *account;
      GoaObject *row_object;

      row_object = g_object_get_data (l->data, "goa-object");
      account = goa_object_peek_account (row_object);

      if (g_strcmp0 (goa_account_get_id (account), account_id) == 0)
        {
          account_row = l->data;
          break;
        }
    }

  g_list_free (children);
}

static void
on_listbox_selection_changed (GtkListBox    *listbox,
                              GtkListBoxRow *selected_row,
                              CcGoaPanel    *self)
{
  if (selected_row)
    {
      GoaObject *object;
      gboolean is_locked;

      object = g_object_get_data (G_OBJECT (selected_row), "goa-object");
      is_locked = goa_account_get_is_locked (goa_object_peek_account (object));

      show_page_account (self, object);

      gtk_widget_set_sensitive (self->toolbar_remove_button, !is_locked);
    }
  else
    {
      show_page_nothing_selected (self);
    }
}

static void
fill_accounts_listbox (CcGoaPanel *self)
{
  GtkListBox *listbox;
  GList *accounts, *l;

  listbox = GTK_LIST_BOX (self->accounts_listbox);
  accounts = goa_client_get_accounts (self->client);

  for (l = accounts; l != NULL; l = l->next)
    on_account_added (self->client, l->data, self);

  g_list_free_full (accounts, g_object_unref);
}

static void
on_account_added (GoaClient *client,
                  GoaObject *object,
                  gpointer   user_data)
{
  CcGoaPanel *self = user_data;
  GtkWidget *row, *icon, *label, *box;
  GoaAccount *account;
  GError *error;
  GIcon *gicon;
  gchar* title = NULL;

  account = goa_object_peek_account (object);

  /* The main grid */
  box = g_object_new (GTK_TYPE_BOX,
                      "spacing", 6,
                      "margin", 6,
                      NULL);
  gtk_widget_show (box);

  /* The provider icon */
  error = NULL;
  gicon = g_icon_new_for_string (goa_account_get_provider_icon (account), &error);

  if (error)
    {
      g_warning ("Error creating GIcon for account: %s (%s, %d)",
                 error->message,
                 g_quark_to_string (error->domain),
                 error->code);

      g_clear_error (&error);
    }

  icon = gtk_image_new_from_gicon (gicon, GTK_ICON_SIZE_DND);
  gtk_widget_show (icon);

  gtk_container_add (GTK_CONTAINER (box), icon);

  /* The name of the provider */
  title = g_strdup_printf ("<b>%s</b>\n<small>%s</small>",
                           goa_account_get_provider_name (account),
                           goa_account_get_presentation_identity (account));

  label = g_object_new (GTK_TYPE_LABEL,
                        "label", title,
                        "xalign", 0.0,
                        "use-markup", TRUE,
                        "hexpand", TRUE,
                        NULL);
  gtk_widget_show (label);

  gtk_container_add (GTK_CONTAINER (box), label);

  /* "Needs attention" icon */
  icon = gtk_image_new_from_icon_name ("dialog-warning-symbolic", GTK_ICON_SIZE_BUTTON);
  gtk_style_context_add_class (gtk_widget_get_style_context (icon), "dim-label");

  g_object_bind_property (goa_object_peek_account (object),
                          "attention-needed",
                          icon,
                          "visible",
                          G_BINDING_DEFAULT | G_BINDING_SYNC_CREATE);

  gtk_container_add (GTK_CONTAINER (box), icon);

  /* The row */
  row = gtk_list_box_row_new ();
  gtk_widget_show (row);

  g_object_set_data (G_OBJECT (row), "goa-object", object);

  gtk_container_add (GTK_CONTAINER (row), box);

  /* Add to the listbox */
  gtk_container_add (GTK_CONTAINER (self->accounts_listbox), row);

  g_clear_pointer (&title, g_free);
  g_clear_object (&gicon);
}

static void
on_account_changed (GoaClient  *client,
                    GoaObject  *object,
                    gpointer    user_data)
{
  CcGoaPanel *panel = CC_GOA_PANEL (user_data);
  GtkListBoxRow *selected_row;
  GoaObject *selected_object;

  selected_row = gtk_list_box_get_selected_row (GTK_LIST_BOX (panel->accounts_listbox));

  if (!selected_row)
    return;

  selected_object = g_object_get_data (G_OBJECT (selected_row), "goa-object");

  if (selected_object == object)
    show_page_account (panel, selected_object);
}

static void
on_account_removed (GoaClient *client,
                    GoaObject *object,
                    gpointer   user_data)
{
  CcGoaPanel *self = user_data;
  GList *children, *l, *prev;

  children = gtk_container_get_children (GTK_CONTAINER (self->accounts_listbox));
  prev = NULL;

  for (l = children; l != NULL; l = l->next)
    {
      GoaObject *row_object;

      row_object = GOA_OBJECT (g_object_get_data (l->data, "goa-object"));

      if (row_object == object)
        {
          gtk_widget_destroy (l->data);
          break;
        }

      prev = l;
    }

  g_list_free (children);
}

/* ---------------------------------------------------------------------------------------------------- */

typedef struct
{
  CcGoaPanel *panel;
  GoaProvider *provider;
  GVariant *preseed;
} AddAccountData;

static void
get_all_providers_cb (GObject      *source,
                      GAsyncResult *res,
                      gpointer      user_data)
{
  AddAccountData *data = user_data;
  GtkWindow *parent;
  GtkWidget *dialog;
  GList *providers;
  GList *l;
  GoaObject *object;
  GError *error;

  providers = NULL;
  if (!goa_provider_get_all_finish (&providers, res, NULL))
    goto out;

  parent = GTK_WINDOW (cc_shell_get_toplevel (cc_panel_get_shell (CC_PANEL (data->panel))));

  dialog = goa_panel_add_account_dialog_new (data->panel->client);
  gtk_window_set_transient_for (GTK_WINDOW (dialog), parent);

  for (l = providers; l != NULL; l = l->next)
    {
      GoaProvider *provider;
      provider = GOA_PROVIDER (l->data);

      goa_panel_add_account_dialog_add_provider (GOA_PANEL_ADD_ACCOUNT_DIALOG (dialog), provider);
    }

  goa_panel_add_account_dialog_set_preseed_data (GOA_PANEL_ADD_ACCOUNT_DIALOG (dialog),
                                                 data->provider, data->preseed);

  gtk_widget_show_all (dialog);
  goa_panel_add_account_dialog_run (GOA_PANEL_ADD_ACCOUNT_DIALOG (dialog));

  error = NULL;
  object = goa_panel_add_account_dialog_get_account (GOA_PANEL_ADD_ACCOUNT_DIALOG (dialog), &error);
  gtk_widget_destroy (dialog);

  /* We might have an object even when error is set.
   * eg., if we failed to store the credentials in the keyring.
   */

  if (object != NULL)
    {
      GoaAccount *account = goa_object_peek_account (object);
      select_account_by_id (data->panel, goa_account_get_id (account));
    }

  if (error != NULL)
    {
      if (!(error->domain == GOA_ERROR && error->code == GOA_ERROR_DIALOG_DISMISSED))
        {
          dialog = gtk_message_dialog_new (parent,
                                           GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                           GTK_MESSAGE_ERROR,
                                           GTK_BUTTONS_CLOSE,
                                           _("Error creating account"));
          gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG (dialog),
                                                    "%s",
                                                    error->message);
          gtk_widget_show_all (dialog);
          gtk_dialog_run (GTK_DIALOG (dialog));
          gtk_widget_destroy (dialog);
        }
      g_error_free (error);
    }

  g_list_free_full (providers, g_object_unref);

out:
  g_clear_object (&data->panel);
  g_clear_object (&data->provider);
  g_clear_pointer (&data->preseed, g_variant_unref);
  g_slice_free (AddAccountData, data);
}

static void
add_account (CcGoaPanel *panel,
             GoaProvider *provider,
             GVariant *preseed)
{
  AddAccountData *data;

  data = g_slice_new0 (AddAccountData);
  data->panel = g_object_ref_sink (panel);
  data->provider = (provider != NULL ? g_object_ref (provider) : NULL);
  data->preseed = (preseed != NULL ? g_variant_ref (preseed) : NULL);
  goa_provider_get_all (get_all_providers_cb, data);
}

/* ---------------------------------------------------------------------------------------------------- */

static void
on_toolbar_add_button_clicked (GtkToolButton *button,
                               gpointer       user_data)
{
  CcGoaPanel *panel = CC_GOA_PANEL (user_data);
  add_account (panel, NULL, NULL);
}

static void
remove_account_cb (GoaAccount    *account,
                   GAsyncResult  *res,
                   gpointer       user_data)
{
  CcGoaPanel *panel = CC_GOA_PANEL (user_data);
  GError *error;

  error = NULL;
  if (!goa_account_call_remove_finish (account, res, &error))
    {
      GtkWidget *dialog;
      dialog = gtk_message_dialog_new (GTK_WINDOW (cc_shell_get_toplevel (cc_panel_get_shell (CC_PANEL (panel)))),
                                       GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                       GTK_MESSAGE_ERROR,
                                       GTK_BUTTONS_CLOSE,
                                       _("Error removing account"));
      gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG (dialog),
                                                "%s",
                                                error->message);
      gtk_widget_show_all (dialog);
      gtk_dialog_run (GTK_DIALOG (dialog));
      gtk_widget_destroy (dialog);
      g_error_free (error);
    }
  g_object_unref (panel);
}

static void
on_toolbar_remove_button_clicked (GtkToolButton *button,
                                  gpointer       user_data)
{
  CcGoaPanel *panel = CC_GOA_PANEL (user_data);
  GtkListBoxRow *selected_row;
  GoaObject *object;
  GtkWidget *dialog;
  gint response;

  selected_row = gtk_list_box_get_selected_row (GTK_LIST_BOX (panel->accounts_listbox));
  object = g_object_get_data (G_OBJECT (selected_row), "goa-object");

  dialog = gtk_message_dialog_new (GTK_WINDOW (cc_shell_get_toplevel (cc_panel_get_shell (CC_PANEL (panel)))),
                                   GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                   GTK_MESSAGE_QUESTION,
                                   GTK_BUTTONS_CANCEL,
                                   _("Are you sure you want to remove the account?"));
  gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG (dialog),
                                            _("This will not remove the account on the server."));
  gtk_dialog_add_button (GTK_DIALOG (dialog), _("_Remove"), GTK_RESPONSE_OK);
  gtk_widget_show_all (dialog);
  response = gtk_dialog_run (GTK_DIALOG (dialog));
  gtk_widget_destroy (dialog);

  if (response == GTK_RESPONSE_OK)
    {
      goa_account_call_remove (goa_object_peek_account (object),
                               NULL, /* GCancellable */
                               (GAsyncReadyCallback) remove_account_cb,
                               g_object_ref (panel));
    }
}

/* ---------------------------------------------------------------------------------------------------- */

static void
on_add_button_clicked (GtkButton *button,
                       gpointer   user_data)
{
  CcGoaPanel *panel = CC_GOA_PANEL (user_data);
  add_account (panel, NULL, NULL);
}
