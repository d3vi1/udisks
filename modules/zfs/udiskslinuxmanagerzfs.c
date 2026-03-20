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
 */

#include "config.h"

#include <glib/gi18n-lib.h>
#include <string.h>

#include <blockdev/zfs.h>

#include <src/udisksdaemon.h>
#include <src/udisksdaemonutil.h>
#include <src/udiskslogging.h>

#include "udiskszfstypes.h"
#include "udiskszfsdaemonutil.h"
#include "udiskslinuxmanagerzfs.h"
#include "udiskslinuxmodulezfs.h"
#include "udiskslinuxpoolobjectzfs.h"

/**
 * SECTION:udiskslinuxmanagerzfs
 * @title: UDisksLinuxManagerZFS
 * @short_description: Linux implementation of #UDisksLinuxManagerZFS
 *
 * This type provides an implementation of the #UDisksLinuxManagerZFS
 * interface on Linux.
 */

/**
 * UDisksLinuxManagerZFS:
 *
 * The #UDisksLinuxManagerZFS structure contains only private data and
 * should only be accessed using the provided API.
 */
struct _UDisksLinuxManagerZFS{
  UDisksManagerZFSSkeleton parent_instance;

  UDisksLinuxModuleZFS *module;
};

struct _UDisksLinuxManagerZFSClass {
  UDisksManagerZFSSkeletonClass parent_class;
};

static void udisks_linux_manager_zfs_iface_init (UDisksManagerZFSIface *iface);

G_DEFINE_TYPE_WITH_CODE (UDisksLinuxManagerZFS, udisks_linux_manager_zfs, UDISKS_TYPE_MANAGER_ZFS_SKELETON,
                         G_IMPLEMENT_INTERFACE (UDISKS_TYPE_MANAGER_ZFS, udisks_linux_manager_zfs_iface_init));

enum
{
  PROP_0,
  PROP_MODULE,
  N_PROPERTIES
};

static void
udisks_linux_manager_zfs_get_property (GObject *object, guint property_id,
                                       GValue *value, GParamSpec *pspec)
{
  UDisksLinuxManagerZFS *manager = UDISKS_LINUX_MANAGER_ZFS (object);

  switch (property_id)
    {
    case PROP_MODULE:
      g_value_set_object (value, udisks_linux_manager_zfs_get_module (manager));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
    }
}

static void
udisks_linux_manager_zfs_set_property (GObject *object, guint property_id,
                                       const GValue *value, GParamSpec *pspec)
{
  UDisksLinuxManagerZFS *manager = UDISKS_LINUX_MANAGER_ZFS (object);

  switch (property_id)
    {
    case PROP_MODULE:
      g_assert (manager->module == NULL);
      manager->module = g_value_dup_object (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
    }
}

static void
udisks_linux_manager_zfs_finalize (GObject *object)
{
  UDisksLinuxManagerZFS *manager = UDISKS_LINUX_MANAGER_ZFS (object);

  g_object_unref (manager->module);

  if (G_OBJECT_CLASS (udisks_linux_manager_zfs_parent_class))
    G_OBJECT_CLASS (udisks_linux_manager_zfs_parent_class)->finalize (object);
}

static void
udisks_linux_manager_zfs_class_init (UDisksLinuxManagerZFSClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->get_property = udisks_linux_manager_zfs_get_property;
  gobject_class->set_property = udisks_linux_manager_zfs_set_property;
  gobject_class->finalize = udisks_linux_manager_zfs_finalize;

  /**
   * UDisksLinuxManagerZFS:module
   *
   * The #UDisksLinuxModuleZFS for the object.
   */
  g_object_class_install_property (gobject_class,
                                   PROP_MODULE,
                                   g_param_spec_object ("module",
                                                        "Module",
                                                        "The module for the object",
                                                        UDISKS_TYPE_LINUX_MODULE_ZFS,
                                                        G_PARAM_READABLE |
                                                        G_PARAM_WRITABLE |
                                                        G_PARAM_CONSTRUCT_ONLY |
                                                        G_PARAM_STATIC_STRINGS));
}

static void
udisks_linux_manager_zfs_init (UDisksLinuxManagerZFS *self)
{
  g_dbus_interface_skeleton_set_flags (G_DBUS_INTERFACE_SKELETON (self),
                                       G_DBUS_INTERFACE_SKELETON_FLAGS_HANDLE_METHOD_INVOCATIONS_IN_THREAD);
}

/**
 * udisks_linux_manager_zfs_new:
 * @module: A #UDisksLinuxModuleZFS.
 *
 * Creates a new #UDisksLinuxManagerZFS instance.
 *
 * Returns: A new #UDisksLinuxManagerZFS. Free with g_object_unref().
 */
UDisksLinuxManagerZFS *
udisks_linux_manager_zfs_new (UDisksLinuxModuleZFS *module)
{
  g_return_val_if_fail (UDISKS_IS_LINUX_MODULE_ZFS (module), NULL);
  return UDISKS_LINUX_MANAGER_ZFS (g_object_new (UDISKS_TYPE_LINUX_MANAGER_ZFS,
                                                  "module", module,
                                                  NULL));
}

/**
 * udisks_linux_manager_zfs_get_module:
 * @manager: A #UDisksLinuxManagerZFS.
 *
 * Gets the module used by @manager.
 *
 * Returns: A #UDisksLinuxModuleZFS. Do not free, the object is owned by @manager.
 */
UDisksLinuxModuleZFS *
udisks_linux_manager_zfs_get_module (UDisksLinuxManagerZFS *manager)
{
  g_return_val_if_fail (UDISKS_IS_LINUX_MANAGER_ZFS (manager), NULL);
  return manager->module;
}

/* ---------------------------------------------------------------------------------------------------- */

typedef struct
{
  UDisksLinuxModuleZFS *module;
  const gchar *name;
} WaitForPoolObjectData;

static UDisksObject *
wait_for_pool_object (UDisksDaemon *daemon,
                      gpointer      user_data)
{
  WaitForPoolObjectData *data = user_data;
  UDisksLinuxPoolObjectZFS *object;

  object = udisks_linux_module_zfs_find_pool_object (data->module, data->name);

  if (object == NULL)
    return NULL;

  return g_object_ref (UDISKS_OBJECT (object));
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
handle_pool_create (UDisksManagerZFS      *_manager,
                    GDBusMethodInvocation *invocation,
                    const gchar           *arg_name,
                    const gchar *const    *arg_blocks,
                    const gchar           *arg_vdev_type,
                    GVariant              *arg_options)
{
  UDisksLinuxManagerZFS *manager = UDISKS_LINUX_MANAGER_ZFS (_manager);
  UDisksDaemon *daemon;
  GError *error = NULL;
  gchar **device_paths = NULL;
  UDisksObject *pool_object = NULL;
  WaitForPoolObjectData wait_data;

  daemon = udisks_module_get_daemon (UDISKS_MODULE (manager->module));

  /* Policy check */
  UDISKS_DAEMON_CHECK_AUTHORIZATION (daemon,
                                     NULL,
                                     ZFS_POLICY_ACTION_ID,
                                     arg_options,
                                     N_("Authentication is required to create a ZFS pool"),
                                     invocation);

  /* Validate pool name format */
  if (arg_name == NULL || strlen (arg_name) == 0)
    {
      g_dbus_method_invocation_return_error (invocation,
                                             UDISKS_ERROR,
                                             UDISKS_ERROR_FAILED,
                                             "Pool name must not be empty");
      goto out;
    }

  /* Validate pool name format via libblockdev */
  if (!bd_zfs_validate_pool_name (arg_name, &error))
    {
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  /* Resolve block object paths to device paths */
  device_paths = udisks_zfs_resolve_blocks_to_device_paths (daemon, arg_blocks, invocation, NULL);
  if (device_paths == NULL)
    goto out;

  /* Normalize empty vdev_type to NULL (stripe).  libblockdev treats
   * non-NULL as a real argv token, so "" would be a bogus raid level. */
  if (arg_vdev_type != NULL && arg_vdev_type[0] == '\0')
    arg_vdev_type = NULL;

  /* Create the pool */
  if (!bd_zfs_pool_create (arg_name,
                           (const gchar **) device_paths,
                           arg_vdev_type,
                           NULL, /* properties */
                           &error))
    {
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  /* Trigger update so the new pool object appears */
  udisks_linux_module_zfs_trigger_update (manager->module);

  /* Wait for the pool object to show up */
  wait_data.module = manager->module;
  wait_data.name = arg_name;
  pool_object = udisks_daemon_wait_for_object_sync (daemon,
                                                     wait_for_pool_object,
                                                     &wait_data,
                                                     NULL,
                                                     UDISKS_DEFAULT_WAIT_TIMEOUT,
                                                     &error);
  if (pool_object == NULL)
    {
      g_prefix_error (&error,
                      "Error waiting for pool object for '%s': ",
                      arg_name);
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  udisks_manager_zfs_complete_pool_create (_manager,
                                           invocation,
                                           g_dbus_object_get_object_path (G_DBUS_OBJECT (pool_object)));

 out:
  g_strfreev (device_paths);
  g_clear_object (&pool_object);
  return TRUE;
}

/* ---------------------------------------------------------------------------------------------------- */

/**
 * resolve_pool_name_by_guid:
 * @guid: A pool GUID string.
 *
 * Scans the list of currently imported pools and returns the name of
 * the pool whose GUID matches @guid.  This is needed after importing
 * by bare GUID (without new_name) because ZFS keeps the original pool
 * name and we need it to locate the D-Bus object.
 *
 * Returns: (transfer full): The pool name, or %NULL if not found.
 *   Free with g_free().
 */
static gchar *
resolve_pool_name_by_guid (const gchar *guid)
{
  BDZFSPoolInfo **pools = NULL;
  BDZFSPoolInfo **p;
  gchar *name = NULL;

  pools = bd_zfs_pool_list (NULL);
  if (pools == NULL)
    return NULL;

  for (p = pools; *p != NULL; p++)
    {
      if (g_strcmp0 ((*p)->guid, guid) == 0)
        {
          name = g_strdup ((*p)->name);
          break;
        }
    }

  for (p = pools; *p != NULL; p++)
    bd_zfs_pool_info_free (*p);
  g_free (pools);

  return name;
}

static gboolean
handle_pool_import (UDisksManagerZFS      *_manager,
                    GDBusMethodInvocation *invocation,
                    const gchar           *arg_name_or_guid,
                    GVariant              *arg_options)
{
  UDisksLinuxManagerZFS *manager = UDISKS_LINUX_MANAGER_ZFS (_manager);
  UDisksDaemon *daemon;
  GError *error = NULL;
  gboolean force = FALSE;
  const gchar *new_name = NULL;
  gchar *resolved_name = NULL;
  const gchar *wait_name = NULL;
  UDisksObject *pool_object = NULL;
  WaitForPoolObjectData wait_data;

  daemon = udisks_module_get_daemon (UDISKS_MODULE (manager->module));

  g_variant_lookup (arg_options, "force", "b", &force);
  g_variant_lookup (arg_options, "new_name", "&s", &new_name);

  /* Treat empty new_name the same as absent */
  if (new_name != NULL && *new_name == '\0')
    new_name = NULL;

  /* Use the destroy policy for force imports, regular policy otherwise */
  UDISKS_DAEMON_CHECK_AUTHORIZATION (daemon,
                                     NULL,
                                     force ? ZFS_POLICY_ACTION_ID_DESTROY : ZFS_POLICY_ACTION_ID,
                                     arg_options,
                                     N_("Authentication is required to import a ZFS pool"),
                                     invocation);

  if (arg_name_or_guid == NULL || strlen (arg_name_or_guid) == 0)
    {
      g_dbus_method_invocation_return_error (invocation,
                                             UDISKS_ERROR,
                                             UDISKS_ERROR_FAILED,
                                             "Pool name or GUID must not be empty");
      goto out;
    }

  /* Import the pool, passing new_name so ZFS renames on import */
  if (!bd_zfs_pool_import (arg_name_or_guid,
                           new_name,       /* new_name (NULL keeps original) */
                           NULL,           /* search_dirs */
                           force,
                           NULL,           /* extra args */
                           &error))
    {
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  /* Trigger update so the imported pool object appears */
  udisks_linux_module_zfs_trigger_update (manager->module);

  /* Determine the pool name to wait for.  The hash table is keyed by
   * pool name, so we must resolve the expected name:
   *   - If new_name was given, the imported pool uses that name.
   *   - If arg_name_or_guid is a pool name, it is used directly.
   *   - If arg_name_or_guid is a GUID (no new_name), scan the
   *     imported pools to discover the original name. */
  if (new_name != NULL)
    {
      wait_name = new_name;
    }
  else
    {
      /* Check whether the caller passed a GUID (all-digit string) */
      gboolean is_guid = TRUE;
      const gchar *ch;

      for (ch = arg_name_or_guid; *ch != '\0'; ch++)
        {
          if (!g_ascii_isdigit (*ch))
            {
              is_guid = FALSE;
              break;
            }
        }

      if (is_guid)
        {
          resolved_name = resolve_pool_name_by_guid (arg_name_or_guid);
          if (resolved_name == NULL)
            {
              g_dbus_method_invocation_return_error (invocation,
                                                     UDISKS_ERROR,
                                                     UDISKS_ERROR_FAILED,
                                                     "Pool with GUID %s was imported but could not "
                                                     "be found in the pool list",
                                                     arg_name_or_guid);
              goto out;
            }
          wait_name = resolved_name;
        }
      else
        {
          wait_name = arg_name_or_guid;
        }
    }

  wait_data.module = manager->module;
  wait_data.name = wait_name;
  pool_object = udisks_daemon_wait_for_object_sync (daemon,
                                                     wait_for_pool_object,
                                                     &wait_data,
                                                     NULL,
                                                     UDISKS_DEFAULT_WAIT_TIMEOUT,
                                                     &error);
  if (pool_object == NULL)
    {
      g_prefix_error (&error,
                      "Error waiting for pool object for '%s': ",
                      arg_name_or_guid);
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  udisks_manager_zfs_complete_pool_import (_manager,
                                           invocation,
                                           g_dbus_object_get_object_path (G_DBUS_OBJECT (pool_object)));

 out:
  g_free (resolved_name);
  g_clear_object (&pool_object);
  return TRUE;
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
handle_pool_import_all (UDisksManagerZFS      *_manager,
                        GDBusMethodInvocation *invocation,
                        GVariant              *arg_options)
{
  UDisksLinuxManagerZFS *manager = UDISKS_LINUX_MANAGER_ZFS (_manager);
  UDisksDaemon *daemon;
  GError *error = NULL;
  BDZFSPoolInfo **importable = NULL;
  BDZFSPoolInfo **p;
  gboolean force = FALSE;
  GString *errors_str = NULL;
  guint n_failed = 0;

  daemon = udisks_module_get_daemon (UDISKS_MODULE (manager->module));

  g_variant_lookup (arg_options, "force", "b", &force);

  /* Bulk-importing every available pool is a higher-privilege operation
   * than importing a single named pool: it can activate pools the admin
   * did not intend to bring online.  Always require the destroy
   * (auth_admin) tier regardless of force. */
  UDISKS_DAEMON_CHECK_AUTHORIZATION (daemon,
                                     NULL,
                                     ZFS_POLICY_ACTION_ID_DESTROY,
                                     arg_options,
                                     N_("Authentication is required to import all ZFS pools"),
                                     invocation);

  /* Enumerate importable pools via libblockdev rather than shelling out
   * to "zpool import -a" directly.  This uses the same code path as
   * ListImportablePools and gives us per-pool error reporting. */
  importable = bd_zfs_pool_list_importable (&error);
  if (importable == NULL && error != NULL)
    {
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  /* Import each pool individually via bd_zfs_pool_import(), honoring
   * the force option and collecting per-pool errors. */
  if (importable != NULL)
    {
      for (p = importable; *p != NULL; p++)
        {
          BDZFSPoolInfo *info = *p;
          GError *pool_error = NULL;
          const gchar *import_id;

          /* Prefer GUID over name: GUID is unambiguous when multiple
           * exportable pools share a name (e.g. after send/receive). */
          if (info->guid != NULL && info->guid[0] != '\0')
            import_id = info->guid;
          else if (info->name != NULL && info->name[0] != '\0')
            import_id = info->name;
          else
            continue;

          if (!bd_zfs_pool_import (import_id,
                                   NULL,         /* new_name */
                                   NULL,         /* search_dirs */
                                   force,
                                   NULL,         /* extra args */
                                   &pool_error))
            {
              if (errors_str == NULL)
                errors_str = g_string_new (NULL);
              else
                g_string_append (errors_str, "; ");

              g_string_append_printf (errors_str, "%s: %s",
                                      info->name ? info->name : import_id,
                                      pool_error->message);
              g_error_free (pool_error);
              n_failed++;
            }
        }
    }

  /* If any individual imports failed, report a combined error */
  if (n_failed > 0)
    {
      g_dbus_method_invocation_return_error (invocation,
                                             UDISKS_ERROR,
                                             UDISKS_ERROR_FAILED,
                                             "Failed to import %u pool(s): %s",
                                             n_failed,
                                             errors_str->str);
      g_string_free (errors_str, TRUE);
      goto out;
    }

  /* Trigger update so newly imported pool objects appear */
  udisks_linux_module_zfs_trigger_update (manager->module);

  udisks_manager_zfs_complete_pool_import_all (_manager, invocation);

 out:
  if (importable != NULL)
    {
      for (p = importable; *p != NULL; p++)
        bd_zfs_pool_info_free (*p);
      g_free (importable);
    }
  return TRUE;
}

/* ---------------------------------------------------------------------------------------------------- */

static const gchar *
importable_pool_state_to_string (BDZFSPoolState state)
{
  switch (state)
    {
    case BD_ZFS_POOL_STATE_ONLINE:
      return "ONLINE";
    case BD_ZFS_POOL_STATE_DEGRADED:
      return "DEGRADED";
    case BD_ZFS_POOL_STATE_FAULTED:
      return "FAULTED";
    case BD_ZFS_POOL_STATE_OFFLINE:
      return "OFFLINE";
    case BD_ZFS_POOL_STATE_REMOVED:
      return "REMOVED";
    case BD_ZFS_POOL_STATE_UNAVAIL:
      return "UNAVAIL";
    case BD_ZFS_POOL_STATE_UNKNOWN:
    default:
      return "UNKNOWN";
    }
}

static gboolean
handle_list_importable_pools (UDisksManagerZFS      *_manager,
                              GDBusMethodInvocation *invocation,
                              GVariant              *arg_options)
{
  UDisksLinuxManagerZFS *manager = UDISKS_LINUX_MANAGER_ZFS (_manager);
  UDisksDaemon *daemon;
  GError *error = NULL;
  BDZFSPoolInfo **infos = NULL;
  GVariantBuilder pools_builder;

  daemon = udisks_module_get_daemon (UDISKS_MODULE (manager->module));

  /* Listing importable pools is a read-only query — use the query tier
   * so that active-session users can enumerate without admin auth. */
  UDISKS_DAEMON_CHECK_AUTHORIZATION (daemon,
                                     NULL,
                                     ZFS_POLICY_ACTION_ID_QUERY,
                                     arg_options,
                                     N_("Authentication is required to list importable ZFS pools"),
                                     invocation);

  infos = bd_zfs_pool_list_importable (&error);
  if (infos == NULL && error != NULL)
    {
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  g_variant_builder_init (&pools_builder, G_VARIANT_TYPE ("aa{sv}"));

  if (infos != NULL)
    {
      for (BDZFSPoolInfo **p = infos; *p != NULL; p++)
        {
          BDZFSPoolInfo *info = *p;
          GVariantBuilder dict_builder;

          g_variant_builder_init (&dict_builder, G_VARIANT_TYPE ("a{sv}"));
          g_variant_builder_add (&dict_builder, "{sv}", "name",
                                 g_variant_new_string (info->name ? info->name : ""));
          g_variant_builder_add (&dict_builder, "{sv}", "guid",
                                 g_variant_new_string (info->guid ? info->guid : ""));
          g_variant_builder_add (&dict_builder, "{sv}", "state",
                                 g_variant_new_string (importable_pool_state_to_string (info->state)));
          g_variant_builder_add_value (&pools_builder,
                                       g_variant_builder_end (&dict_builder));
        }
    }

  udisks_manager_zfs_complete_list_importable_pools (_manager,
                                                      invocation,
                                                      g_variant_builder_end (&pools_builder));

 out:
  if (infos != NULL)
    {
      for (BDZFSPoolInfo **p = infos; *p != NULL; p++)
        bd_zfs_pool_info_free (*p);
      g_free (infos);
    }
  return TRUE;
}

/* ---------------------------------------------------------------------------------------------------- */

static void
udisks_linux_manager_zfs_iface_init (UDisksManagerZFSIface *iface)
{
  iface->handle_pool_create = handle_pool_create;
  iface->handle_pool_import = handle_pool_import;
  iface->handle_pool_import_all = handle_pool_import_all;
  iface->handle_list_importable_pools = handle_list_importable_pools;
}
