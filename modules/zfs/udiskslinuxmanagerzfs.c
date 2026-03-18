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
#include <blockdev/utils.h>

#include <src/udisksdaemon.h>
#include <src/udisksdaemonutil.h>
#include <src/udiskslogging.h>

#include "udiskszfstypes.h"
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

/**
 * resolve_blocks_to_device_paths:
 * @daemon: A #UDisksDaemon.
 * @arg_blocks: NULL-terminated array of D-Bus object paths.
 * @invocation: The method invocation (for error reporting).
 * @out_n_devices: (out): Number of resolved devices.
 *
 * Resolves an array of D-Bus object paths to device paths.
 *
 * Returns: (transfer full): A NULL-terminated array of device path strings,
 *   or %NULL on error (in which case an error has been returned on @invocation).
 *   Free with g_strfreev().
 */
static gchar **
resolve_blocks_to_device_paths (UDisksDaemon          *daemon,
                                const gchar *const    *arg_blocks,
                                GDBusMethodInvocation *invocation,
                                guint                 *out_n_devices)
{
  guint n;
  guint count;
  GPtrArray *devices;

  /* Count the blocks */
  for (count = 0; arg_blocks != NULL && arg_blocks[count] != NULL; count++)
    ;

  if (count == 0)
    {
      g_dbus_method_invocation_return_error (invocation,
                                             UDISKS_ERROR,
                                             UDISKS_ERROR_FAILED,
                                             "List of block devices is empty");
      return NULL;
    }

  devices = g_ptr_array_new_with_free_func (g_free);

  for (n = 0; n < count; n++)
    {
      UDisksObject *object = NULL;
      UDisksBlock *block = NULL;

      object = udisks_daemon_find_object (daemon, arg_blocks[n]);
      if (object == NULL)
        {
          g_dbus_method_invocation_return_error (invocation,
                                                 UDISKS_ERROR,
                                                 UDISKS_ERROR_FAILED,
                                                 "Invalid object path %s at index %u",
                                                 arg_blocks[n], n);
          g_ptr_array_unref (devices);
          return NULL;
        }

      block = udisks_object_get_block (object);
      if (block == NULL)
        {
          g_dbus_method_invocation_return_error (invocation,
                                                 UDISKS_ERROR,
                                                 UDISKS_ERROR_FAILED,
                                                 "Object path %s at index %u is not a block device",
                                                 arg_blocks[n], n);
          g_object_unref (object);
          g_ptr_array_unref (devices);
          return NULL;
        }

      g_ptr_array_add (devices, udisks_block_dup_device (block));
      g_object_unref (block);
      g_object_unref (object);
    }

  g_ptr_array_add (devices, NULL);

  if (out_n_devices != NULL)
    *out_n_devices = count;

  return (gchar **) g_ptr_array_free (devices, FALSE);
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

  /* ZFS pool names must start with a letter, and contain only
   * alphanumeric characters, hyphens, underscores, and periods.
   * Reserved prefixes (mirror, raidz, draid, spare) are disallowed. */
  {
    const gchar *p;
    gboolean valid = TRUE;

    if (!g_ascii_isalpha (arg_name[0]))
      valid = FALSE;

    for (p = arg_name; valid && *p != '\0'; p++)
      {
        if (!g_ascii_isalnum (*p) && *p != '-' && *p != '_' && *p != '.')
          valid = FALSE;
      }

    if (valid && (g_str_has_prefix (arg_name, "mirror") ||
                  g_str_has_prefix (arg_name, "raidz") ||
                  g_str_has_prefix (arg_name, "draid") ||
                  g_str_has_prefix (arg_name, "spare")))
      valid = FALSE;

    if (!valid)
      {
        g_dbus_method_invocation_return_error (invocation,
                                               UDISKS_ERROR,
                                               UDISKS_ERROR_FAILED,
                                               "Invalid pool name '%s': must start with a letter, "
                                               "contain only [a-zA-Z0-9_-.], and not use reserved "
                                               "prefixes (mirror, raidz, draid, spare)",
                                               arg_name);
        goto out;
      }
  }

  /* Resolve block object paths to device paths */
  device_paths = resolve_blocks_to_device_paths (daemon, arg_blocks, invocation, NULL);
  if (device_paths == NULL)
    goto out;

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
  UDisksObject *pool_object = NULL;
  WaitForPoolObjectData wait_data;

  daemon = udisks_module_get_daemon (UDISKS_MODULE (manager->module));

  g_variant_lookup (arg_options, "force", "b", &force);

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

  /* Import the pool */
  if (!bd_zfs_pool_import (arg_name_or_guid,
                           NULL, /* altroot */
                           NULL, /* properties */
                           force,
                           NULL, /* extra args */
                           &error))
    {
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  /* Trigger update so the imported pool object appears */
  udisks_linux_module_zfs_trigger_update (manager->module);

  /* Wait for the pool object to show up */
  wait_data.module = manager->module;
  wait_data.name = arg_name_or_guid;
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
  const gchar *argv[] = { "zpool", "import", "-a", NULL };

  daemon = udisks_module_get_daemon (UDISKS_MODULE (manager->module));

  /* Policy check */
  UDISKS_DAEMON_CHECK_AUTHORIZATION (daemon,
                                     NULL,
                                     ZFS_POLICY_ACTION_ID,
                                     arg_options,
                                     N_("Authentication is required to import all ZFS pools"),
                                     invocation);

  /* There is no single libblockdev call for "import all"; invoke zpool
   * directly.  bd_utils_exec_and_report_error runs the command
   * synchronously and populates @error on failure. */
  if (!bd_utils_exec_and_report_error (argv, NULL, &error))
    {
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  /* Trigger update so newly imported pool objects appear */
  udisks_linux_module_zfs_trigger_update (manager->module);

  udisks_manager_zfs_complete_pool_import_all (_manager, invocation);

 out:
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

  /* Policy check — query tier only, no destructive action */
  UDISKS_DAEMON_CHECK_AUTHORIZATION (daemon,
                                     NULL,
                                     ZFS_POLICY_ACTION_ID,
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
