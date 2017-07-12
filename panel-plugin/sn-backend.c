/*
 *  Copyright (c) 2017 Viktor Odintsev <ninetls@xfce.org>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU Library General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */



#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#ifdef HAVE_STRING_H
#include <string.h>
#endif

#include <gio/gio.h>

#include "sn-backend.h"
#include "sn-item.h"
#include "sn-watcher.h"



static void                  sn_backend_finalize                     (GObject                 *object);

static void                  sn_backend_clear_items                  (SnBackend               *backend);

static gboolean              sn_backend_register_item                (SnWatcher               *watcher,
                                                                      GDBusMethodInvocation   *invocation,
                                                                      const gchar             *service,
                                                                      SnBackend               *backend);

static void                  sn_backend_unregister_item              (SnBackend               *backend,
                                                                      SnItem                  *item,
                                                                      gboolean                 remove_and_notify);

static gboolean              sn_backend_register_host                (SnWatcher               *watcher,
                                                                      GDBusMethodInvocation   *invocation,
                                                                      const gchar             *service);

static void                  sn_backend_update_registered_items      (SnBackend               *backend);



struct _SnBackendClass
{
  GObjectClass         __parent__;
};

struct _SnBackend
{
  GObject              __parent__;

  guint                bus_owner_id;

  SnWatcher           *watcher;
  GHashTable          *items;
};

G_DEFINE_TYPE (SnBackend, sn_backend, G_TYPE_OBJECT)



enum
{
  PROP_0,
  PROP_IO_ENTRY,
};

enum
{
  ITEM_ADDED,
  ITEM_REMOVED,
  LAST_SIGNAL
};

static guint sn_backend_signals[LAST_SIGNAL] = { 0, };



typedef struct
{
  gint                 index;
  gchar              **out;
}
CollectItemKeysContext;



static void
sn_backend_class_init (SnBackendClass *klass)
{
  GObjectClass *object_class;

  object_class = G_OBJECT_CLASS (klass);
  object_class->finalize = sn_backend_finalize;

  sn_backend_signals[ITEM_ADDED] =
    g_signal_new (g_intern_static_string ("item-added"),
                  G_TYPE_FROM_CLASS (object_class),
                  G_SIGNAL_RUN_LAST,
                  0, NULL, NULL,
                  g_cclosure_marshal_VOID__OBJECT,
                  G_TYPE_NONE, 1, XFCE_TYPE_SN_ITEM);

  sn_backend_signals[ITEM_REMOVED] =
    g_signal_new (g_intern_static_string ("item-removed"),
                  G_TYPE_FROM_CLASS (object_class),
                  G_SIGNAL_RUN_LAST,
                  0, NULL, NULL,
                  g_cclosure_marshal_VOID__OBJECT,
                  G_TYPE_NONE, 1, XFCE_TYPE_SN_ITEM);
}



static void
sn_backend_init (SnBackend *backend)
{
  backend->bus_owner_id = 0;
  backend->watcher = NULL;
  backend->items = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
}



static void
sn_backend_finalize (GObject *object)
{
  SnBackend *backend = XFCE_SN_BACKEND (object);

  sn_backend_clear_items (backend);
  g_hash_table_destroy (backend->items);

  if (backend->watcher != NULL)
    g_object_unref (backend->watcher);

  if (backend->bus_owner_id != 0)
    g_bus_unown_name (backend->bus_owner_id);

  G_OBJECT_CLASS (sn_backend_parent_class)->finalize (object);
}



static gboolean
sn_backend_clear_item (gpointer key,
                       gpointer value,
                       gpointer user_data)
{
  SnBackend *backend = user_data;
  SnItem    *item = value;

  sn_backend_unregister_item (backend, item, FALSE);

  return TRUE;
}



static void
sn_backend_clear_items (SnBackend *backend)
{
  g_hash_table_foreach_remove (backend->items, sn_backend_clear_item, backend);
}



static void
sn_backend_bus_acquired (GDBusConnection *connection,
                         const gchar     *name,
                         gpointer         user_data)
{
  SnBackend *backend = user_data;
  GError    *error = NULL;

  if (backend->watcher != NULL)
    g_object_unref (backend->watcher);

  backend->watcher = sn_watcher_skeleton_new ();

  sn_watcher_set_is_status_notifier_host_registered (backend->watcher, TRUE);
  sn_watcher_set_registered_status_notifier_items (backend->watcher, NULL);
  sn_watcher_set_protocol_version (backend->watcher, 0);

  g_signal_connect (backend->watcher, "handle-register-status-notifier-item",
                    G_CALLBACK (sn_backend_register_item), backend);
  g_signal_connect (backend->watcher, "handle-register-status-notifier-host",
                    G_CALLBACK (sn_backend_register_host), backend);

  g_dbus_interface_skeleton_export (G_DBUS_INTERFACE_SKELETON (backend->watcher),
                                    connection, "/StatusNotifierWatcher", &error);

  if (error != NULL)
    {
      g_error_free (error);

      g_object_unref (backend->watcher);
      backend->watcher = NULL;
    }
}



static void
sn_backend_name_acquired (GDBusConnection *connection,
                          const gchar     *name,
                          gpointer         user_data)
{
  SnBackend *backend = user_data;

  if (backend->watcher != NULL)
    sn_watcher_emit_status_notifier_host_registered (backend->watcher);
}



static void
sn_backend_name_lost (GDBusConnection *connection,
                      const gchar     *name,
                      gpointer         user_data)
{
  SnBackend *backend = user_data;

  sn_backend_clear_items (backend);
}



SnBackend *
sn_backend_new (void)
{
  return g_object_new (XFCE_TYPE_SN_BACKEND, NULL);
}



void
sn_backend_start (SnBackend *backend)
{
  g_return_if_fail (XFCE_IS_SN_BACKEND (backend));
  g_return_if_fail (backend->bus_owner_id == 0);

  backend->bus_owner_id = g_bus_own_name (G_BUS_TYPE_SESSION,
                                          "org.kde.StatusNotifierWatcher",
                                          G_BUS_NAME_OWNER_FLAGS_NONE,
                                          sn_backend_bus_acquired,
                                          sn_backend_name_acquired,
                                          sn_backend_name_lost,
                                          backend, NULL);
}



static void
sn_backend_item_expose (SnItem    *item,
                        SnBackend *backend)
{
  g_signal_emit (G_OBJECT (backend), sn_backend_signals[ITEM_ADDED], 0, item);
}



static void
sn_backend_item_seal (SnItem    *item,
                      SnBackend *backend)
{
  g_signal_emit (G_OBJECT (backend), sn_backend_signals[ITEM_REMOVED], 0, item);
}



static void
sn_backend_item_finish (SnItem    *item,
                        SnBackend *backend)
{
  sn_backend_unregister_item (backend, item, TRUE);
}



static gboolean
sn_backend_register_item (SnWatcher             *watcher,
                          GDBusMethodInvocation *invocation,
                          const gchar           *service,
                          SnBackend             *backend)
{
  const gchar *bus_name;
  const gchar *object_path;
  const gchar *sender;
  gchar       *key;
  SnItem      *item;

  sender = g_dbus_method_invocation_get_sender (invocation);

  if (service[0] == '/')
    {
      /* /org/ayatana/NotificationItem */
      bus_name = sender;
      object_path = service;
    }
  else
    {
      /* org.kde.StatusNotifierItem */
      bus_name = service;
      object_path = "/StatusNotifierItem";
    }

  key = g_strdup_printf ("%s%s", bus_name, object_path);
  item = g_hash_table_lookup (backend->items, key);
  if (item != NULL)
    {
      g_free (key);
      sn_item_invalidate (item);
    }
  else
    {
      item = g_object_new (XFCE_TYPE_SN_ITEM,
                           "bus-name", bus_name,
                           "object-path", object_path,
                           "service", service,
                           "key", key,
                           NULL);
      g_signal_connect (item, "expose",
                        G_CALLBACK (sn_backend_item_expose), backend);
      g_signal_connect (item, "seal",
                        G_CALLBACK (sn_backend_item_seal), backend);
      g_signal_connect (item, "finish",
                        G_CALLBACK (sn_backend_item_finish), backend);
      sn_item_start (item);
      g_hash_table_insert (backend->items, key, item);
    }

  sn_backend_update_registered_items (backend);

  sn_watcher_complete_register_status_notifier_item (watcher, invocation);

  sn_watcher_emit_status_notifier_item_registered (watcher, service);

  return TRUE;
}



static void
sn_backend_unregister_item (SnBackend *backend,
                            SnItem    *item,
                            gboolean   remove_and_notify)
{
  gchar   *service;
  gchar   *key;
  gboolean exposed;

  g_object_get (item,
                "service", &service,
                "key", &key,
                "exposed", &exposed,
                NULL);

  if (exposed)
    g_signal_emit (G_OBJECT (backend), sn_backend_signals[ITEM_REMOVED], 0, item);

  if (remove_and_notify)
    g_hash_table_remove (backend->items, key);

  g_object_unref (item);

  if (backend->watcher != NULL)
    sn_watcher_emit_status_notifier_item_unregistered (backend->watcher, service);

  g_free (service);
  g_free (key);

  if (remove_and_notify)
    sn_backend_update_registered_items (backend);
}



static gboolean
sn_backend_register_host (SnWatcher             *watcher,
                          GDBusMethodInvocation *invocation,
                          const gchar           *service)
{
  g_dbus_method_invocation_return_error_literal (invocation,
                                                 G_IO_ERROR,
                                                 G_IO_ERROR_EXISTS,
                                                 "Multiple hosts are not supported");

  return FALSE;
}



static void
sn_backend_collect_item_keys (gpointer key,
                              gpointer value,
                              gpointer user_data)
{
  CollectItemKeysContext *context = user_data;
  context->out[context->index++] = key;
}



static void
sn_backend_update_registered_items (SnBackend *backend)
{
  CollectItemKeysContext context;

  if (backend->watcher != NULL)
    {
      context.index = 0;
      context.out = g_new0 (gchar *, g_hash_table_size (backend->items) + 1);
      g_hash_table_foreach (backend->items,
                            sn_backend_collect_item_keys,
                            &context);
      sn_watcher_set_registered_status_notifier_items (backend->watcher,
                                                       (gpointer)context.out);
      g_free (context.out);
    }
}
