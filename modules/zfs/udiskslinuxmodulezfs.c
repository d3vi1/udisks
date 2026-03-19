/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
 *
 * Copyright (C) 2024 Razvan Cojocaru <rzvncj@gmail.com>
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
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

#include "config.h"

#include <blockdev/blockdev.h>
#include <blockdev/zfs.h>

#include <src/udisksdaemon.h>
#include <src/udiskslogging.h>
#include <src/udiskslinuxdevice.h>
#include <src/udisksmodulemanager.h>
#include <src/udisksmodule.h>
#include <src/udisksmoduleobject.h>

#include <src/udiskslinuxblockobject.h>

#include "udiskszfstypes.h"
#include "udiskslinuxmodulezfs.h"
#include "udiskslinuxmanagerzfs.h"
#include "udiskslinuxpoolobjectzfs.h"
#include "udiskslinuxblockzfs.h"
#include "udiskslinuxfilesystemzfs.h"

/**
 * SECTION:udiskslinuxmodulezfs
 * @title: UDisksLinuxModuleZFS
 * @short_description: ZFS module.
 *
 * The ZFS module.
 */

/**
 * UDisksLinuxModuleZFS:
 *
 * The #UDisksLinuxModuleZFS structure contains only private data
 * and should only be accessed using the provided API.
 */
struct _UDisksLinuxModuleZFS {
  UDisksModule parent_instance;

  /* maps from pool name to UDisksLinuxPoolObjectZFS instances */
  GHashTable *name_to_pool;

  gint delayed_update_id;
  gint periodic_poll_id;       /* 30-second poll source */
  gboolean coldplug_done;

  guint32 update_epoch;
};

typedef struct _UDisksLinuxModuleZFSClass UDisksLinuxModuleZFSClass;

struct _UDisksLinuxModuleZFSClass {
  UDisksModuleClass parent_class;
};

static void initable_iface_init (GInitableIface *initable_iface);

G_DEFINE_TYPE_WITH_CODE (UDisksLinuxModuleZFS, udisks_linux_module_zfs, UDISKS_TYPE_MODULE,
                         G_IMPLEMENT_INTERFACE (G_TYPE_INITABLE, initable_iface_init));


static void
udisks_linux_module_zfs_init (UDisksLinuxModuleZFS *module)
{
  g_return_if_fail (UDISKS_IS_LINUX_MODULE_ZFS (module));
}

static void zfs_update (UDisksLinuxModuleZFS *module);

static gboolean
periodic_poll (gpointer user_data)
{
  UDisksLinuxModuleZFS *module = UDISKS_LINUX_MODULE_ZFS (user_data);

  zfs_update (module);

  return G_SOURCE_CONTINUE;
}

static void
udisks_linux_module_zfs_constructed (GObject *object)
{
  UDisksLinuxModuleZFS *module = UDISKS_LINUX_MODULE_ZFS (object);

  module->name_to_pool = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, (GDestroyNotify) g_object_unref);
  module->coldplug_done = FALSE;
  module->update_epoch = 0;
  module->delayed_update_id = 0;

  /* Start a 30-second periodic poll to catch CLI-driven changes */
  module->periodic_poll_id = g_timeout_add_seconds (30, periodic_poll, module);

  if (G_OBJECT_CLASS (udisks_linux_module_zfs_parent_class)->constructed)
    G_OBJECT_CLASS (udisks_linux_module_zfs_parent_class)->constructed (object);
}

static void
udisks_linux_module_zfs_finalize (GObject *object)
{
  UDisksLinuxModuleZFS *module = UDISKS_LINUX_MODULE_ZFS (object);

  if (module->delayed_update_id > 0)
    g_source_remove (module->delayed_update_id);

  if (module->periodic_poll_id > 0)
    g_source_remove (module->periodic_poll_id);

  g_hash_table_unref (module->name_to_pool);

  if (G_OBJECT_CLASS (udisks_linux_module_zfs_parent_class)->finalize)
    G_OBJECT_CLASS (udisks_linux_module_zfs_parent_class)->finalize (object);
}

gchar *
udisks_module_id (void)
{
  return g_strdup (ZFS_MODULE_NAME);
}

/**
 * udisks_module_zfs_new:
 * @daemon: A #UDisksDaemon.
 * @cancellable: (nullable): A #GCancellable or %NULL
 * @error: Return location for error or %NULL.
 *
 * Creates new #UDisksLinuxModuleZFS object.
 *
 * Returns: (transfer full) (type UDisksLinuxModuleZFS): A
 *   #UDisksLinuxModuleZFS object or %NULL if @error is set. Free
 *   with g_object_unref().
 */
UDisksModule *
udisks_module_zfs_new (UDisksDaemon  *daemon,
                       GCancellable  *cancellable,
                       GError       **error)
{
  GInitable *initable;

  g_return_val_if_fail (UDISKS_IS_DAEMON (daemon), NULL);
  g_return_val_if_fail (error == NULL || *error == NULL, NULL);

  initable = g_initable_new (UDISKS_TYPE_LINUX_MODULE_ZFS,
                             cancellable,
                             error,
                             "daemon", daemon,
                             "name", ZFS_MODULE_NAME,
                             NULL);

  if (initable == NULL)
    return NULL;
  else
    return UDISKS_MODULE (initable);
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
initable_init (GInitable     *initable,
               GCancellable  *cancellable,
               GError       **error)
{
  BDPluginSpec zfs_plugin = { BD_PLUGIN_ZFS, "libbd_zfs.so.3" };
  BDPluginSpec *plugins[] = { &zfs_plugin, NULL };

  if (! bd_is_plugin_available (BD_PLUGIN_ZFS))
    {
      if (! bd_reinit (plugins, FALSE, NULL, error))
        return FALSE;
    }

  return TRUE;
}

static void
initable_iface_init (GInitableIface *initable_iface)
{
  initable_iface->init = initable_init;
}

/* ---------------------------------------------------------------------------------------------------- */

GHashTable *
udisks_linux_module_zfs_get_name_to_pool (UDisksLinuxModuleZFS *module)
{
  g_return_val_if_fail (UDISKS_IS_LINUX_MODULE_ZFS (module), NULL);

  return module->name_to_pool;
}

/*  transfer-none  */
UDisksLinuxPoolObjectZFS *
udisks_linux_module_zfs_find_pool_object (UDisksLinuxModuleZFS *module,
                                          const gchar          *name)
{
  g_return_val_if_fail (UDISKS_IS_LINUX_MODULE_ZFS (module), NULL);

  return g_hash_table_lookup (module->name_to_pool, name);
}

/* ---------------------------------------------------------------------------------------------------- */

static GDBusInterfaceSkeleton *
udisks_linux_module_zfs_new_manager (UDisksModule *module)
{
  UDisksLinuxManagerZFS *manager;

  g_return_val_if_fail (UDISKS_IS_LINUX_MODULE_ZFS (module), NULL);

  manager = udisks_linux_manager_zfs_new (UDISKS_LINUX_MODULE_ZFS (module));

  return G_DBUS_INTERFACE_SKELETON (manager);
}

/* ---------------------------------------------------------------------------------------------------- */

static void
pool_list_free (BDZFSPoolInfo **pool_list)
{
  if (pool_list == NULL)
    return;
  for (BDZFSPoolInfo **p = pool_list; *p != NULL; p++)
    bd_zfs_pool_info_free (*p);
  g_free (pool_list);
}

static void
zfs_pools_task_func (GTask        *task,
                     gpointer      source_obj,
                     gpointer      task_data,
                     GCancellable *cancellable)
{
  GError *error = NULL;
  BDZFSPoolInfo **pools;

  pools = bd_zfs_pool_list (&error);

  if (pools == NULL && error != NULL)
    {
      g_task_return_error (task, error);
      return;
    }

  /* bd_zfs_pool_list may return NULL with no error when there are no pools.
   * In that case, return an empty NULL-terminated array. */
  if (pools == NULL)
    {
      pools = g_new0 (BDZFSPoolInfo *, 1);
    }

  g_task_return_pointer (task, pools, (GDestroyNotify) pool_list_free);
}

static void
zfs_update_pools (GObject      *source_obj,
                  GAsyncResult *result,
                  gpointer      user_data)
{
  UDisksLinuxModuleZFS *module = UDISKS_LINUX_MODULE_ZFS (source_obj);
  UDisksDaemon *daemon;
  GDBusObjectManagerServer *manager;

  GTask *task = G_TASK (result);
  GError *error = NULL;
  BDZFSPoolInfo **pools = g_task_propagate_pointer (task, &error);
  BDZFSPoolInfo **pools_p;

  GHashTableIter pool_name_iter;
  gpointer key, value;
  const gchar *pool_name;

  /* Reject stale async results */
  if (GPOINTER_TO_UINT (user_data) != module->update_epoch)
    {
      pool_list_free (pools);
      return;
    }

  if (! pools)
    {
      if (error)
        {
          udisks_warning ("ZFS plugin: %s", error->message);
          g_clear_error (&error);
        }
      else
        {
          /* this should never happen */
          udisks_warning ("ZFS plugin: failure but no error when getting pools!");
        }
      return;
    }

  daemon = udisks_module_get_daemon (UDISKS_MODULE (module));
  manager = udisks_daemon_get_object_manager (daemon);

  /* Remove obsolete pools */
  g_hash_table_iter_init (&pool_name_iter, module->name_to_pool);
  while (g_hash_table_iter_next (&pool_name_iter, &key, &value))
    {
      UDisksLinuxPoolObjectZFS *pool;
      gboolean found = FALSE;

      pool_name = key;
      pool = value;

      for (pools_p = pools; !found && *pools_p; pools_p++)
        found = g_strcmp0 ((*pools_p)->name, pool_name) == 0;

      if (! found)
        {
          udisks_linux_pool_object_zfs_destroy (pool);
          g_dbus_object_manager_server_unexport (manager, g_dbus_object_get_object_path (G_DBUS_OBJECT (pool)));
          g_hash_table_iter_remove (&pool_name_iter);
        }
    }

  /* Add new pools and update existing pools */
  for (pools_p = pools; *pools_p; pools_p++)
    {
      UDisksLinuxPoolObjectZFS *pool;

      pool_name = (*pools_p)->name;
      pool = g_hash_table_lookup (module->name_to_pool, pool_name);
      if (pool == NULL)
        {
          pool = udisks_linux_pool_object_zfs_new (module, pool_name);
          g_hash_table_insert (module->name_to_pool, g_strdup (pool_name), pool);
          g_dbus_object_manager_server_export_uniquely (manager, G_DBUS_OBJECT_SKELETON (pool));
        }

      udisks_linux_pool_object_zfs_update (pool, *pools_p);
    }

  /* Re-resolve Block.ZFS and Filesystem.ZFS Pool properties that are still
   * set to "/" — this happens when block uevents fire before pool objects
   * have been created. */
  {
    GList *objects;
    GList *l;

    objects = udisks_daemon_get_objects (daemon);
    for (l = objects; l != NULL; l = l->next)
      {
        GDBusInterface *block_zfs_iface;
        GDBusInterface *fs_zfs_iface;
        gboolean needs_update = FALSE;

        if (! UDISKS_IS_LINUX_BLOCK_OBJECT (l->data))
          continue;

        block_zfs_iface = g_dbus_object_get_interface (G_DBUS_OBJECT (l->data),
                                                       "org.freedesktop.UDisks2.Block.ZFS");
        if (block_zfs_iface != NULL)
          {
            if (g_strcmp0 (udisks_block_zfs_get_pool (UDISKS_BLOCK_ZFS (block_zfs_iface)), "/") == 0)
              needs_update = TRUE;
            g_object_unref (block_zfs_iface);
          }

        fs_zfs_iface = g_dbus_object_get_interface (G_DBUS_OBJECT (l->data),
                                                     "org.freedesktop.UDisks2.Filesystem.ZFS");
        if (fs_zfs_iface != NULL)
          {
            if (g_strcmp0 (udisks_filesystem_zfs_get_pool (UDISKS_FILESYSTEM_ZFS (fs_zfs_iface)), "/") == 0)
              needs_update = TRUE;
            g_object_unref (fs_zfs_iface);
          }

        if (needs_update)
          udisks_linux_block_object_uevent (UDISKS_LINUX_BLOCK_OBJECT (l->data),
                                            UDISKS_UEVENT_ACTION_OTHER, NULL);
      }
    g_list_free_full (objects, g_object_unref);
  }

  /* Free pool info array (but contents have been consumed) */
  pool_list_free (pools);
}

static void
zfs_update (UDisksLinuxModuleZFS *module)
{
  GTask *task;

  module->update_epoch++;

  /* the callback (zfs_update_pools) is called in the default main loop (context) */
  task = g_task_new (module,
                     NULL /* cancellable */,
                     zfs_update_pools,
                     GUINT_TO_POINTER (module->update_epoch));

  /* holds a reference to 'task' until it is finished */
  g_task_run_in_thread (task, (GTaskThreadFunc) zfs_pools_task_func);
  g_object_unref (task);
}

static gboolean
delayed_zfs_update (gpointer user_data)
{
  UDisksLinuxModuleZFS *module = UDISKS_LINUX_MODULE_ZFS (user_data);

  zfs_update (module);
  module->delayed_update_id = 0;

  return FALSE;
}

static void
trigger_delayed_zfs_update (UDisksLinuxModuleZFS *module)
{
  if (module->delayed_update_id > 0)
    return;

  if (! module->coldplug_done)
    {
      /* Update immediately when doing coldplug, i.e. when zfs module has just
       * been activated. This is not 100% effective as this affects only the
       * first request but from the plugin nature we don't know whether
       * coldplugging has been finished or not. Might be subject to change in
       * the future. */
      module->coldplug_done = TRUE;
      zfs_update (module);
    }
  else
    {
      module->delayed_update_id = g_timeout_add (100, delayed_zfs_update, module);
    }
}

/**
 * udisks_linux_module_zfs_trigger_update:
 * @module: A #UDisksLinuxModuleZFS.
 *
 * Triggers a delayed ZFS pool update.  This should be called after any
 * state-changing pool operation (create, import, export, destroy, etc.).
 */
void
udisks_linux_module_zfs_trigger_update (UDisksLinuxModuleZFS *module)
{
  g_return_if_fail (UDISKS_IS_LINUX_MODULE_ZFS (module));
  trigger_delayed_zfs_update (module);
}

/**
 * udisks_linux_module_zfs_remove_pool:
 * @module: A #UDisksLinuxModuleZFS.
 * @name: The pool name to remove.
 *
 * Synchronously removes a pool D-Bus object from the object manager
 * and the internal pool table.  This must only be called from the main
 * thread (it is not thread-safe with respect to the hash table accessed
 * by zfs_update_pools).  Export and Destroy handlers dispatch to the
 * main thread via g_idle_add() after completing the D-Bus method.
 *
 * The caller should still call udisks_linux_module_zfs_trigger_update()
 * afterwards to pick up any block-device changes asynchronously.
 */
void
udisks_linux_module_zfs_remove_pool (UDisksLinuxModuleZFS *module,
                                     const gchar          *name)
{
  UDisksLinuxPoolObjectZFS *pool;
  UDisksDaemon *daemon;
  GDBusObjectManagerServer *manager;

  g_return_if_fail (UDISKS_IS_LINUX_MODULE_ZFS (module));
  g_return_if_fail (name != NULL);

  pool = g_hash_table_lookup (module->name_to_pool, name);
  if (pool == NULL)
    return;

  daemon = udisks_module_get_daemon (UDISKS_MODULE (module));
  manager = udisks_daemon_get_object_manager (daemon);

  udisks_linux_pool_object_zfs_destroy (pool);
  g_dbus_object_manager_server_unexport (manager,
                                         g_dbus_object_get_object_path (G_DBUS_OBJECT (pool)));
  g_hash_table_remove (module->name_to_pool, name);
}

static gboolean
has_zfs_member_label (UDisksLinuxDevice *device)
{
  const gchar *id_fs_type;

  id_fs_type = g_udev_device_get_property (device->udev_device, "ID_FS_TYPE");
  return g_strcmp0 (id_fs_type, "zfs_member") == 0;
}

static void
udisks_linux_module_zfs_handle_uevent (UDisksModule      *module,
                                       UDisksLinuxDevice *device)
{
  g_return_if_fail (UDISKS_IS_LINUX_MODULE_ZFS (module));

  if (has_zfs_member_label (device))
    trigger_delayed_zfs_update (UDISKS_LINUX_MODULE_ZFS (module));
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
is_zvol_device (UDisksLinuxDevice *device)
{
  const gchar *dev_file;
  const gchar *dm_uuid;
  const gchar * const *symlinks;

  dev_file = g_udev_device_get_device_file (device->udev_device);

  /* zvol block devices use /dev/zd* device nodes */
  if (dev_file != NULL && g_str_has_prefix (dev_file, "/dev/zd"))
    return TRUE;

  /* Check for ZFS DM UUID (zvols created via device-mapper) */
  dm_uuid = g_udev_device_get_property (device->udev_device, "DM_UUID");
  if (dm_uuid != NULL && g_str_has_prefix (dm_uuid, "zvol-"))
    return TRUE;

  /* Check for /dev/zvol/ symlinks */
  symlinks = g_udev_device_get_device_file_symlinks (device->udev_device);
  if (symlinks != NULL)
    {
      for (const gchar * const *s = symlinks; *s != NULL; s++)
        {
          if (g_str_has_prefix (*s, "/dev/zvol/"))
            return TRUE;
        }
    }

  return FALSE;
}

static GType *
udisks_linux_module_zfs_get_block_object_interface_types (UDisksModule *module)
{
  static GType block_object_interface_types[3];

  g_return_val_if_fail (UDISKS_IS_LINUX_MODULE_ZFS (module), NULL);

  if (g_once_init_enter (&block_object_interface_types[0]))
    {
      block_object_interface_types[1] = UDISKS_TYPE_LINUX_FILESYSTEM_ZFS;
      g_once_init_leave (&block_object_interface_types[0], UDISKS_TYPE_LINUX_BLOCK_ZFS);
    }

  return block_object_interface_types;
}

static GDBusInterfaceSkeleton *
udisks_linux_module_zfs_new_block_object_interface (UDisksModule           *module,
                                                    UDisksLinuxBlockObject *object,
                                                    GType                   interface_type)
{
  GDBusInterfaceSkeleton *interface = NULL;
  UDisksLinuxDevice *device;
  const gchar *fs_type;

  g_return_val_if_fail (UDISKS_IS_LINUX_MODULE_ZFS (module), NULL);

  if (interface_type == UDISKS_TYPE_LINUX_BLOCK_ZFS)
    {
      /* Attach Block.ZFS to devices that are ZFS pool members */
      device = udisks_linux_block_object_get_device (UDISKS_LINUX_BLOCK_OBJECT (object));
      fs_type = g_udev_device_get_property (device->udev_device, "ID_FS_TYPE");
      if (g_strcmp0 (fs_type, "zfs_member") == 0)
        {
          interface = G_DBUS_INTERFACE_SKELETON (udisks_linux_block_zfs_new (UDISKS_LINUX_MODULE_ZFS (module), object));
        }
      g_object_unref (device);
    }
  else if (interface_type == UDISKS_TYPE_LINUX_FILESYSTEM_ZFS)
    {
      /* Attach Filesystem.ZFS to zvol block devices */
      device = udisks_linux_block_object_get_device (UDISKS_LINUX_BLOCK_OBJECT (object));
      if (is_zvol_device (device))
        {
          interface = G_DBUS_INTERFACE_SKELETON (udisks_linux_filesystem_zfs_new (UDISKS_LINUX_MODULE_ZFS (module), object));
        }
      g_object_unref (device);
    }
  else
    {
      udisks_error ("ZFS: invalid block object interface type");
    }

  return interface;
}

/* ---------------------------------------------------------------------------------------------------- */

static void
udisks_linux_module_zfs_class_init (UDisksLinuxModuleZFSClass *klass)
{
  GObjectClass *gobject_class;
  UDisksModuleClass *module_class;

  gobject_class = G_OBJECT_CLASS (klass);
  gobject_class->constructed = udisks_linux_module_zfs_constructed;
  gobject_class->finalize = udisks_linux_module_zfs_finalize;

  module_class = UDISKS_MODULE_CLASS (klass);
  module_class->new_manager = udisks_linux_module_zfs_new_manager;
  module_class->handle_uevent = udisks_linux_module_zfs_handle_uevent;
  module_class->get_block_object_interface_types = udisks_linux_module_zfs_get_block_object_interface_types;
  module_class->new_block_object_interface = udisks_linux_module_zfs_new_block_object_interface;
}
