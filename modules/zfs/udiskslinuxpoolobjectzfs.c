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

#include <src/udiskslogging.h>
#include <src/udisksdaemon.h>
#include <src/udisksdaemonutil.h>

#include "udiskszfstypes.h"
#include "udiskszfsdaemonutil.h"
#include "udiskslinuxpoolobjectzfs.h"
#include "udiskslinuxmodulezfs.h"

/**
 * SECTION:udiskslinuxpoolobjectzfs
 * @title: UDisksLinuxPoolObjectZFS
 * @short_description: Object representing a ZFS pool
 */

typedef struct _UDisksLinuxPoolObjectZFSClass UDisksLinuxPoolObjectZFSClass;

/**
 * UDisksLinuxPoolObjectZFS:
 *
 * The #UDisksLinuxPoolObjectZFS structure contains only private data and
 * should only be accessed using the provided API.
 */
struct _UDisksLinuxPoolObjectZFS
{
  UDisksObjectSkeleton parent_instance;

  UDisksLinuxModuleZFS *module;
  gchar *name;

  /* interface */
  UDisksZFSPool *iface_zfs_pool;

  /* scrub polling */
  guint scrub_poll_id;
};

struct _UDisksLinuxPoolObjectZFSClass
{
  UDisksObjectSkeletonClass parent_class;
};

enum
{
  PROP_0,
  PROP_MODULE,
  PROP_NAME,
};

G_DEFINE_TYPE (UDisksLinuxPoolObjectZFS, udisks_linux_pool_object_zfs, UDISKS_TYPE_OBJECT_SKELETON);

static void
udisks_linux_pool_object_zfs_finalize (GObject *_object)
{
  UDisksLinuxPoolObjectZFS *object = UDISKS_LINUX_POOL_OBJECT_ZFS (_object);

  if (object->scrub_poll_id != 0)
    {
      g_source_remove (object->scrub_poll_id);
      object->scrub_poll_id = 0;
    }

  g_object_unref (object->module);

  if (object->iface_zfs_pool != NULL)
    g_object_unref (object->iface_zfs_pool);

  g_free (object->name);

  if (G_OBJECT_CLASS (udisks_linux_pool_object_zfs_parent_class)->finalize != NULL)
    G_OBJECT_CLASS (udisks_linux_pool_object_zfs_parent_class)->finalize (_object);
}

static void
udisks_linux_pool_object_zfs_get_property (GObject    *__object,
                                           guint       prop_id,
                                           GValue     *value,
                                           GParamSpec *pspec)
{
  UDisksLinuxPoolObjectZFS *object = UDISKS_LINUX_POOL_OBJECT_ZFS (__object);

  switch (prop_id)
    {
    case PROP_MODULE:
      g_value_set_object (value, udisks_linux_pool_object_zfs_get_module (object));
      break;

    case PROP_NAME:
      g_value_set_string (value, udisks_linux_pool_object_zfs_get_name (object));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
udisks_linux_pool_object_zfs_set_property (GObject      *__object,
                                           guint         prop_id,
                                           const GValue *value,
                                           GParamSpec   *pspec)
{
  UDisksLinuxPoolObjectZFS *object = UDISKS_LINUX_POOL_OBJECT_ZFS (__object);

  switch (prop_id)
    {
    case PROP_MODULE:
      g_assert (object->module == NULL);
      object->module = g_value_dup_object (value);
      break;

    case PROP_NAME:
      g_assert (object->name == NULL);
      object->name = g_value_dup_string (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
udisks_linux_pool_object_zfs_init (UDisksLinuxPoolObjectZFS *object)
{
}

static const gchar *
pool_state_to_string (BDZFSPoolState state)
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

/* ---------------------------------------------------------------------------------------------------- */
/*  Helpers for ZFSPool method handlers                                                                */
/* ---------------------------------------------------------------------------------------------------- */

/**
 * resolve_blocks_to_device_paths:
 * @daemon: A #UDisksDaemon.
 * @arg_blocks: NULL-terminated array of D-Bus object paths.
 * @invocation: The method invocation (for error reporting).
 *
 * Resolves an array of D-Bus object paths to device path strings.
 *
 * Returns: (transfer full): A NULL-terminated array of device path strings,
 *   or %NULL on error (in which case an error has been returned on @invocation).
 *   Free with g_strfreev().
 */
static gchar **
pool_resolve_blocks_to_device_paths (UDisksDaemon          *daemon,
                                     const gchar *const    *arg_blocks,
                                     GDBusMethodInvocation *invocation)
{
  guint n;
  guint count;
  GPtrArray *devices;

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
      UDisksObject *obj = NULL;
      UDisksBlock *block = NULL;

      obj = udisks_daemon_find_object (daemon, arg_blocks[n]);
      if (obj == NULL)
        {
          g_dbus_method_invocation_return_error (invocation,
                                                 UDISKS_ERROR,
                                                 UDISKS_ERROR_FAILED,
                                                 "Invalid object path %s at index %u",
                                                 arg_blocks[n], n);
          g_ptr_array_unref (devices);
          return NULL;
        }

      block = udisks_object_get_block (obj);
      if (block == NULL)
        {
          g_dbus_method_invocation_return_error (invocation,
                                                 UDISKS_ERROR,
                                                 UDISKS_ERROR_FAILED,
                                                 "Object path %s at index %u is not a block device",
                                                 arg_blocks[n], n);
          g_object_unref (obj);
          g_ptr_array_unref (devices);
          return NULL;
        }

      g_ptr_array_add (devices, udisks_block_dup_device (block));
      g_object_unref (block);
      g_object_unref (obj);
    }

  g_ptr_array_add (devices, NULL);
  return (gchar **) g_ptr_array_free (devices, FALSE);
}

/* ---------------------------------------------------------------------------------------------------- */
/*  ZFSPool D-Bus method handlers                                                                       */
/* ---------------------------------------------------------------------------------------------------- */

static gboolean
handle_poll (UDisksZFSPool         *iface,
             GDBusMethodInvocation *invocation,
             gpointer               user_data)
{
  UDisksLinuxPoolObjectZFS *object = UDISKS_LINUX_POOL_OBJECT_ZFS (user_data);
  UDisksDaemon *daemon;
  GVariant *options = NULL;

  daemon = udisks_module_get_daemon (UDISKS_MODULE (object->module));

  /* Build an empty options dict for the authorization check */
  options = g_variant_new ("a{sv}", NULL);
  g_variant_ref_sink (options);

  UDISKS_DAEMON_CHECK_AUTHORIZATION (daemon,
                                     UDISKS_OBJECT (object),
                                     ZFS_POLICY_ACTION_ID_QUERY,
                                     options,
                                     N_("Authentication is required to poll ZFS pool status"),
                                     invocation);

  udisks_linux_module_zfs_trigger_update (object->module);
  udisks_zfspool_complete_poll (iface, invocation);

 out:
  g_variant_unref (options);
  return TRUE;
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
handle_export (UDisksZFSPool         *iface,
               GDBusMethodInvocation *invocation,
               gboolean               arg_force,
               GVariant              *arg_options,
               gpointer               user_data)
{
  UDisksLinuxPoolObjectZFS *object = UDISKS_LINUX_POOL_OBJECT_ZFS (user_data);
  UDisksDaemon *daemon;
  GError *error = NULL;

  daemon = udisks_module_get_daemon (UDISKS_MODULE (object->module));

  /* Policy check */
  UDISKS_DAEMON_CHECK_AUTHORIZATION (daemon,
                                     UDISKS_OBJECT (object),
                                     ZFS_POLICY_ACTION_ID,
                                     arg_options,
                                     N_("Authentication is required to export a ZFS pool"),
                                     invocation);

  if (!bd_zfs_pool_export (object->name, arg_force, &error))
    {
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  udisks_linux_module_zfs_trigger_update (object->module);
  udisks_zfspool_complete_export (iface, invocation);

 out:
  return TRUE;
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
handle_destroy (UDisksZFSPool         *iface,
                GDBusMethodInvocation *invocation,
                gboolean               arg_force,
                GVariant              *arg_options,
                gpointer               user_data)
{
  UDisksLinuxPoolObjectZFS *object = UDISKS_LINUX_POOL_OBJECT_ZFS (user_data);
  UDisksDaemon *daemon;
  GError *error = NULL;

  daemon = udisks_module_get_daemon (UDISKS_MODULE (object->module));

  /* Destruction requires the stronger manage-zfs-destroy policy */
  UDISKS_DAEMON_CHECK_AUTHORIZATION (daemon,
                                     UDISKS_OBJECT (object),
                                     ZFS_POLICY_ACTION_ID_DESTROY,
                                     arg_options,
                                     N_("Authentication is required to destroy a ZFS pool"),
                                     invocation);

  if (!bd_zfs_pool_destroy (object->name, arg_force, &error))
    {
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  udisks_linux_module_zfs_trigger_update (object->module);
  udisks_zfspool_complete_destroy (iface, invocation);

 out:
  return TRUE;
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
handle_add_vdev (UDisksZFSPool         *iface,
                 GDBusMethodInvocation *invocation,
                 const gchar           *arg_vdev_type,
                 const gchar *const    *arg_blocks,
                 GVariant              *arg_options,
                 gpointer               user_data)
{
  UDisksLinuxPoolObjectZFS *object = UDISKS_LINUX_POOL_OBJECT_ZFS (user_data);
  UDisksDaemon *daemon;
  GError *error = NULL;
  gchar **device_paths = NULL;

  daemon = udisks_module_get_daemon (UDISKS_MODULE (object->module));

  /* Policy check */
  UDISKS_DAEMON_CHECK_AUTHORIZATION (daemon,
                                     UDISKS_OBJECT (object),
                                     ZFS_POLICY_ACTION_ID,
                                     arg_options,
                                     N_("Authentication is required to add a vdev to a ZFS pool"),
                                     invocation);

  /* Resolve block object paths to device paths */
  device_paths = pool_resolve_blocks_to_device_paths (daemon, arg_blocks, invocation);
  if (device_paths == NULL)
    goto out;

  if (!bd_zfs_pool_add_vdev (object->name,
                             (const gchar **) device_paths,
                             arg_vdev_type,
                             NULL, /* extra args */
                             &error))
    {
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  udisks_linux_module_zfs_trigger_update (object->module);
  udisks_zfspool_complete_add_vdev (iface, invocation);

 out:
  g_strfreev (device_paths);
  return TRUE;
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
handle_remove_vdev (UDisksZFSPool         *iface,
                    GDBusMethodInvocation *invocation,
                    const gchar           *arg_device,
                    GVariant              *arg_options,
                    gpointer               user_data)
{
  UDisksLinuxPoolObjectZFS *object = UDISKS_LINUX_POOL_OBJECT_ZFS (user_data);
  UDisksDaemon *daemon;
  GError *error = NULL;

  daemon = udisks_module_get_daemon (UDISKS_MODULE (object->module));

  /* Policy check */
  UDISKS_DAEMON_CHECK_AUTHORIZATION (daemon,
                                     UDISKS_OBJECT (object),
                                     ZFS_POLICY_ACTION_ID,
                                     arg_options,
                                     N_("Authentication is required to remove a vdev from a ZFS pool"),
                                     invocation);

  if (!bd_zfs_pool_remove_vdev (object->name, arg_device, &error))
    {
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  udisks_linux_module_zfs_trigger_update (object->module);
  udisks_zfspool_complete_remove_vdev (iface, invocation);

 out:
  return TRUE;
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
handle_replace_vdev (UDisksZFSPool         *iface,
                     GDBusMethodInvocation *invocation,
                     const gchar           *arg_old_device,
                     const gchar           *arg_new_device,
                     gboolean               arg_force,
                     GVariant              *arg_options,
                     gpointer               user_data)
{
  UDisksLinuxPoolObjectZFS *object = UDISKS_LINUX_POOL_OBJECT_ZFS (user_data);
  UDisksDaemon *daemon;
  GError *error = NULL;

  daemon = udisks_module_get_daemon (UDISKS_MODULE (object->module));

  /* Policy check */
  UDISKS_DAEMON_CHECK_AUTHORIZATION (daemon,
                                     UDISKS_OBJECT (object),
                                     ZFS_POLICY_ACTION_ID,
                                     arg_options,
                                     N_("Authentication is required to replace a ZFS device"),
                                     invocation);

  if (!bd_zfs_pool_replace (object->name, arg_old_device, arg_new_device, arg_force, &error))
    {
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  udisks_linux_module_zfs_trigger_update (object->module);
  udisks_zfspool_complete_replace_vdev (iface, invocation);

 out:
  return TRUE;
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
handle_attach_vdev (UDisksZFSPool         *iface,
                    GDBusMethodInvocation *invocation,
                    const gchar           *arg_existing_device,
                    const gchar           *arg_new_device,
                    GVariant              *arg_options,
                    gpointer               user_data)
{
  UDisksLinuxPoolObjectZFS *object = UDISKS_LINUX_POOL_OBJECT_ZFS (user_data);
  UDisksDaemon *daemon;
  GError *error = NULL;

  daemon = udisks_module_get_daemon (UDISKS_MODULE (object->module));

  /* Policy check */
  UDISKS_DAEMON_CHECK_AUTHORIZATION (daemon,
                                     UDISKS_OBJECT (object),
                                     ZFS_POLICY_ACTION_ID,
                                     arg_options,
                                     N_("Authentication is required to attach a ZFS device"),
                                     invocation);

  if (!bd_zfs_pool_attach (object->name, arg_existing_device, arg_new_device, &error))
    {
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  udisks_linux_module_zfs_trigger_update (object->module);
  udisks_zfspool_complete_attach_vdev (iface, invocation);

 out:
  return TRUE;
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
handle_detach_vdev (UDisksZFSPool         *iface,
                    GDBusMethodInvocation *invocation,
                    const gchar           *arg_device,
                    GVariant              *arg_options,
                    gpointer               user_data)
{
  UDisksLinuxPoolObjectZFS *object = UDISKS_LINUX_POOL_OBJECT_ZFS (user_data);
  UDisksDaemon *daemon;
  GError *error = NULL;

  daemon = udisks_module_get_daemon (UDISKS_MODULE (object->module));

  /* Policy check */
  UDISKS_DAEMON_CHECK_AUTHORIZATION (daemon,
                                     UDISKS_OBJECT (object),
                                     ZFS_POLICY_ACTION_ID,
                                     arg_options,
                                     N_("Authentication is required to detach a ZFS device"),
                                     invocation);

  if (!bd_zfs_pool_detach (object->name, arg_device, &error))
    {
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  udisks_linux_module_zfs_trigger_update (object->module);
  udisks_zfspool_complete_detach_vdev (iface, invocation);

 out:
  return TRUE;
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
handle_online_vdev (UDisksZFSPool         *iface,
                    GDBusMethodInvocation *invocation,
                    const gchar           *arg_device,
                    gboolean               arg_expand,
                    GVariant              *arg_options,
                    gpointer               user_data)
{
  UDisksLinuxPoolObjectZFS *object = UDISKS_LINUX_POOL_OBJECT_ZFS (user_data);
  UDisksDaemon *daemon;
  GError *error = NULL;

  daemon = udisks_module_get_daemon (UDISKS_MODULE (object->module));

  /* Policy check */
  UDISKS_DAEMON_CHECK_AUTHORIZATION (daemon,
                                     UDISKS_OBJECT (object),
                                     ZFS_POLICY_ACTION_ID,
                                     arg_options,
                                     N_("Authentication is required to online a ZFS device"),
                                     invocation);

  if (!bd_zfs_pool_online (object->name, arg_device, arg_expand, &error))
    {
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  udisks_linux_module_zfs_trigger_update (object->module);
  udisks_zfspool_complete_online_vdev (iface, invocation);

 out:
  return TRUE;
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
handle_offline_vdev (UDisksZFSPool         *iface,
                     GDBusMethodInvocation *invocation,
                     const gchar           *arg_device,
                     gboolean               arg_temporary,
                     GVariant              *arg_options,
                     gpointer               user_data)
{
  UDisksLinuxPoolObjectZFS *object = UDISKS_LINUX_POOL_OBJECT_ZFS (user_data);
  UDisksDaemon *daemon;
  GError *error = NULL;

  daemon = udisks_module_get_daemon (UDISKS_MODULE (object->module));

  /* Policy check */
  UDISKS_DAEMON_CHECK_AUTHORIZATION (daemon,
                                     UDISKS_OBJECT (object),
                                     ZFS_POLICY_ACTION_ID,
                                     arg_options,
                                     N_("Authentication is required to offline a ZFS device"),
                                     invocation);

  if (!bd_zfs_pool_offline (object->name, arg_device, arg_temporary, &error))
    {
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  udisks_linux_module_zfs_trigger_update (object->module);
  udisks_zfspool_complete_offline_vdev (iface, invocation);

 out:
  return TRUE;
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
handle_scrub_start (UDisksZFSPool         *iface,
                    GDBusMethodInvocation *invocation,
                    GVariant              *arg_options,
                    gpointer               user_data)
{
  UDisksLinuxPoolObjectZFS *object = UDISKS_LINUX_POOL_OBJECT_ZFS (user_data);
  UDisksDaemon *daemon;
  GError *error = NULL;

  daemon = udisks_module_get_daemon (UDISKS_MODULE (object->module));

  UDISKS_DAEMON_CHECK_AUTHORIZATION (daemon,
                                     UDISKS_OBJECT (object),
                                     ZFS_POLICY_ACTION_ID,
                                     arg_options,
                                     N_("Authentication is required to scrub a ZFS pool"),
                                     invocation);

  if (!bd_zfs_pool_scrub_start (object->name, &error))
    {
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  udisks_linux_module_zfs_trigger_update (object->module);
  udisks_zfspool_complete_scrub_start (iface, invocation);

 out:
  return TRUE;
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
handle_scrub_pause (UDisksZFSPool         *iface,
                    GDBusMethodInvocation *invocation,
                    GVariant              *arg_options,
                    gpointer               user_data)
{
  UDisksLinuxPoolObjectZFS *object = UDISKS_LINUX_POOL_OBJECT_ZFS (user_data);
  UDisksDaemon *daemon;
  GError *error = NULL;

  daemon = udisks_module_get_daemon (UDISKS_MODULE (object->module));

  UDISKS_DAEMON_CHECK_AUTHORIZATION (daemon,
                                     UDISKS_OBJECT (object),
                                     ZFS_POLICY_ACTION_ID,
                                     arg_options,
                                     N_("Authentication is required to pause a ZFS pool scrub"),
                                     invocation);

  if (!bd_zfs_pool_scrub_pause (object->name, &error))
    {
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  udisks_linux_module_zfs_trigger_update (object->module);
  udisks_zfspool_complete_scrub_pause (iface, invocation);

 out:
  return TRUE;
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
handle_scrub_stop (UDisksZFSPool         *iface,
                   GDBusMethodInvocation *invocation,
                   GVariant              *arg_options,
                   gpointer               user_data)
{
  UDisksLinuxPoolObjectZFS *object = UDISKS_LINUX_POOL_OBJECT_ZFS (user_data);
  UDisksDaemon *daemon;
  GError *error = NULL;

  daemon = udisks_module_get_daemon (UDISKS_MODULE (object->module));

  UDISKS_DAEMON_CHECK_AUTHORIZATION (daemon,
                                     UDISKS_OBJECT (object),
                                     ZFS_POLICY_ACTION_ID,
                                     arg_options,
                                     N_("Authentication is required to stop a ZFS pool scrub"),
                                     invocation);

  if (!bd_zfs_pool_scrub_stop (object->name, &error))
    {
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  udisks_linux_module_zfs_trigger_update (object->module);
  udisks_zfspool_complete_scrub_stop (iface, invocation);

 out:
  return TRUE;
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
handle_trim_start (UDisksZFSPool         *iface,
                   GDBusMethodInvocation *invocation,
                   GVariant              *arg_options,
                   gpointer               user_data)
{
  UDisksLinuxPoolObjectZFS *object = UDISKS_LINUX_POOL_OBJECT_ZFS (user_data);
  UDisksDaemon *daemon;
  GError *error = NULL;

  daemon = udisks_module_get_daemon (UDISKS_MODULE (object->module));

  UDISKS_DAEMON_CHECK_AUTHORIZATION (daemon,
                                     UDISKS_OBJECT (object),
                                     ZFS_POLICY_ACTION_ID,
                                     arg_options,
                                     N_("Authentication is required to TRIM a ZFS pool"),
                                     invocation);

  if (!bd_zfs_pool_trim_start (object->name, NULL, &error))
    {
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  udisks_linux_module_zfs_trigger_update (object->module);
  udisks_zfspool_complete_trim_start (iface, invocation);

 out:
  return TRUE;
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
handle_set_property (UDisksZFSPool         *iface,
                     GDBusMethodInvocation *invocation,
                     const gchar           *arg_name,
                     const gchar           *arg_value,
                     GVariant              *arg_options,
                     gpointer               user_data)
{
  UDisksLinuxPoolObjectZFS *object = UDISKS_LINUX_POOL_OBJECT_ZFS (user_data);
  UDisksDaemon *daemon;
  GError *error = NULL;
  GError *prop_error = NULL;
  const gchar *action_id;

  daemon = udisks_module_get_daemon (UDISKS_MODULE (object->module));

  if (arg_name == NULL || strlen (arg_name) == 0)
    {
      g_dbus_method_invocation_return_error (invocation,
                                             UDISKS_ERROR,
                                             UDISKS_ERROR_FAILED,
                                             "Property name must not be empty");
      goto out;
    }

  /* Check property against the allowlist */
  if (!udisks_zfs_property_is_allowed (arg_name, &prop_error))
    {
      g_dbus_method_invocation_take_error (invocation, prop_error);
      goto out;
    }

  /* Security-sensitive properties require elevated authorization */
  if (udisks_zfs_property_is_safe (arg_name, NULL))
    action_id = ZFS_POLICY_ACTION_ID;
  else
    action_id = ZFS_POLICY_ACTION_ID_DESTROY;

  UDISKS_DAEMON_CHECK_AUTHORIZATION (daemon,
                                     UDISKS_OBJECT (object),
                                     action_id,
                                     arg_options,
                                     N_("Authentication is required to set a ZFS pool property"),
                                     invocation);

  if (!bd_zfs_pool_set_property (object->name, arg_name, arg_value, &error))
    {
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  udisks_linux_module_zfs_trigger_update (object->module);
  udisks_zfspool_complete_set_property (iface, invocation);

 out:
  return TRUE;
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
handle_get_property (UDisksZFSPool         *iface,
                     GDBusMethodInvocation *invocation,
                     const gchar           *arg_name,
                     GVariant              *arg_options,
                     gpointer               user_data)
{
  UDisksLinuxPoolObjectZFS *object = UDISKS_LINUX_POOL_OBJECT_ZFS (user_data);
  UDisksDaemon *daemon;
  GError *error = NULL;
  BDZFSPropertyInfo *prop_info = NULL;

  daemon = udisks_module_get_daemon (UDISKS_MODULE (object->module));

  UDISKS_DAEMON_CHECK_AUTHORIZATION (daemon,
                                     UDISKS_OBJECT (object),
                                     ZFS_POLICY_ACTION_ID_QUERY,
                                     arg_options,
                                     N_("Authentication is required to query a ZFS pool property"),
                                     invocation);

  if (arg_name == NULL || strlen (arg_name) == 0)
    {
      g_dbus_method_invocation_return_error (invocation,
                                             UDISKS_ERROR,
                                             UDISKS_ERROR_FAILED,
                                             "Property name must not be empty");
      goto out;
    }

  prop_info = bd_zfs_pool_get_property (object->name, arg_name, &error);
  if (prop_info == NULL)
    {
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  udisks_zfspool_complete_get_property (iface,
                                        invocation,
                                        prop_info->value ? prop_info->value : "",
                                        prop_info->source ? prop_info->source : "");
  bd_zfs_property_info_free (prop_info);

 out:
  return TRUE;
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
handle_create_dataset (UDisksZFSPool         *iface,
                       GDBusMethodInvocation *invocation,
                       const gchar           *arg_name,
                       GVariant              *arg_options,
                       gpointer               user_data)
{
  UDisksLinuxPoolObjectZFS *object = UDISKS_LINUX_POOL_OBJECT_ZFS (user_data);
  UDisksDaemon *daemon;
  GError *error = NULL;
  gchar *full_name = NULL;

  daemon = udisks_module_get_daemon (UDISKS_MODULE (object->module));

  /* Policy check */
  UDISKS_DAEMON_CHECK_AUTHORIZATION (daemon,
                                     UDISKS_OBJECT (object),
                                     ZFS_POLICY_ACTION_ID,
                                     arg_options,
                                     N_("Authentication is required to create a ZFS dataset"),
                                     invocation);

  full_name = g_strdup_printf ("%s/%s", object->name, arg_name);

  if (!bd_zfs_dataset_create (full_name, NULL, &error))
    {
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  udisks_linux_module_zfs_trigger_update (object->module);
  udisks_zfspool_complete_create_dataset (iface, invocation, full_name);

 out:
  g_free (full_name);
  return TRUE;
}

static gboolean
handle_create_volume (UDisksZFSPool         *iface,
                      GDBusMethodInvocation *invocation,
                      const gchar           *arg_name,
                      guint64                arg_size,
                      GVariant              *arg_options,
                      gpointer               user_data)
{
  UDisksLinuxPoolObjectZFS *object = UDISKS_LINUX_POOL_OBJECT_ZFS (user_data);
  UDisksDaemon *daemon;
  GError *error = NULL;
  gchar *full_name = NULL;

  daemon = udisks_module_get_daemon (UDISKS_MODULE (object->module));

  /* Policy check */
  UDISKS_DAEMON_CHECK_AUTHORIZATION (daemon,
                                     UDISKS_OBJECT (object),
                                     ZFS_POLICY_ACTION_ID,
                                     arg_options,
                                     N_("Authentication is required to create a ZFS volume"),
                                     invocation);

  full_name = g_strdup_printf ("%s/%s", object->name, arg_name);

  if (!bd_zfs_zvol_create (full_name, arg_size, FALSE, NULL, &error))
    {
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  udisks_linux_module_zfs_trigger_update (object->module);
  udisks_zfspool_complete_create_volume (iface, invocation, full_name);

 out:
  g_free (full_name);
  return TRUE;
}

static gboolean
handle_list_datasets (UDisksZFSPool         *iface,
                      GDBusMethodInvocation *invocation,
                      GVariant              *arg_options,
                      gpointer               user_data)
{
  UDisksLinuxPoolObjectZFS *object = UDISKS_LINUX_POOL_OBJECT_ZFS (user_data);
  UDisksDaemon *daemon;
  GError *error = NULL;
  BDZFSDatasetInfo **infos = NULL;
  gboolean recursive = TRUE;  /* default to recursive — a pool dataset list is useless without it */
  const gchar *type_filter = NULL;
  gint64 offset = 0;
  gint64 limit = -1;
  GVariantBuilder datasets_builder;
  gint64 count;
  gint64 idx;

  daemon = udisks_module_get_daemon (UDISKS_MODULE (object->module));

  /* Policy check */
  UDISKS_DAEMON_CHECK_AUTHORIZATION (daemon,
                                     UDISKS_OBJECT (object),
                                     ZFS_POLICY_ACTION_ID_QUERY,
                                     arg_options,
                                     N_("Authentication is required to list ZFS datasets"),
                                     invocation);

  /* Extract options */
  g_variant_lookup (arg_options, "recursive", "b", &recursive);
  g_variant_lookup (arg_options, "type", "&s", &type_filter);
  g_variant_lookup (arg_options, "offset", "x", &offset);
  g_variant_lookup (arg_options, "limit", "x", &limit);

  infos = bd_zfs_dataset_list (object->name, recursive, &error);
  if (infos == NULL)
    {
      /* NULL with no error means empty list */
      if (error != NULL)
        {
          g_dbus_method_invocation_take_error (invocation, error);
          goto out;
        }
    }

  g_variant_builder_init (&datasets_builder, G_VARIANT_TYPE ("aa{sv}"));

  count = 0;
  idx = 0;

  if (infos != NULL)
    {
      for (BDZFSDatasetInfo **p = infos; *p != NULL; p++)
        {
          BDZFSDatasetInfo *info = *p;
          GVariantBuilder dict_builder;
          const gchar *type_str = "filesystem";

          switch (info->type)
            {
            case BD_ZFS_DATASET_TYPE_VOLUME:
              type_str = "volume";
              break;
            case BD_ZFS_DATASET_TYPE_SNAPSHOT:
              type_str = "snapshot";
              break;
            case BD_ZFS_DATASET_TYPE_BOOKMARK:
              type_str = "bookmark";
              break;
            default:
              break;
            }

          /* Apply type filter if specified ("all" or NULL means no filter) */
          if (type_filter != NULL && g_strcmp0 (type_filter, "all") != 0
              && g_strcmp0 (type_filter, type_str) != 0)
            continue;

          /* Apply offset */
          if (idx < offset)
            {
              idx++;
              continue;
            }
          idx++;

          /* Apply limit */
          if (limit >= 0 && count >= limit)
            break;

          g_variant_builder_init (&dict_builder, G_VARIANT_TYPE ("a{sv}"));

          g_variant_builder_add (&dict_builder, "{sv}", "name",
                                 g_variant_new_string (info->name));
          g_variant_builder_add (&dict_builder, "{sv}", "type",
                                 g_variant_new_string (type_str));

          if (info->mountpoint)
            g_variant_builder_add (&dict_builder, "{sv}", "mountpoint",
                                   g_variant_new_string (info->mountpoint));
          g_variant_builder_add (&dict_builder, "{sv}", "mounted",
                                 g_variant_new_boolean (info->mounted));
          g_variant_builder_add (&dict_builder, "{sv}", "used",
                                 g_variant_new_uint64 (info->used));
          g_variant_builder_add (&dict_builder, "{sv}", "available",
                                 g_variant_new_uint64 (info->available));
          g_variant_builder_add (&dict_builder, "{sv}", "referenced",
                                 g_variant_new_uint64 (info->referenced));

          if (info->compression)
            g_variant_builder_add (&dict_builder, "{sv}", "compression",
                                   g_variant_new_string (info->compression));
          if (info->encryption)
            g_variant_builder_add (&dict_builder, "{sv}", "encryption",
                                   g_variant_new_string (info->encryption));
          if (info->origin)
            g_variant_builder_add (&dict_builder, "{sv}", "origin",
                                   g_variant_new_string (info->origin));

          {
            const gchar *ks = "none";
            if (info->key_status == BD_ZFS_KEY_STATUS_AVAILABLE)
              ks = "available";
            else if (info->key_status == BD_ZFS_KEY_STATUS_UNAVAILABLE)
              ks = "unavailable";
            g_variant_builder_add (&dict_builder, "{sv}", "key-status",
                                   g_variant_new_string (ks));
          }

          g_variant_builder_add (&datasets_builder, "a{sv}", &dict_builder);
          count++;
        }
    }

  udisks_zfspool_complete_list_datasets (UDISKS_ZFSPOOL (iface), invocation,
                                         g_variant_builder_end (&datasets_builder));

  /* Free the info array */
  if (infos != NULL)
    {
      for (BDZFSDatasetInfo **p = infos; *p != NULL; p++)
        bd_zfs_dataset_info_free (*p);
      g_free (infos);
    }

 out:
  return TRUE;
}

static gboolean
handle_destroy_dataset (UDisksZFSPool         *iface,
                        GDBusMethodInvocation *invocation,
                        const gchar           *arg_name,
                        gboolean               arg_recursive,
                        GVariant              *arg_options,
                        gpointer               user_data)
{
  UDisksLinuxPoolObjectZFS *object = UDISKS_LINUX_POOL_OBJECT_ZFS (user_data);
  UDisksDaemon *daemon;
  GError *error = NULL;

  daemon = udisks_module_get_daemon (UDISKS_MODULE (object->module));

  /* Cross-pool validation */
  if (!udisks_zfs_validate_name_in_pool (object->name, arg_name, &error))
    {
      g_dbus_method_invocation_take_error (invocation, error);
      return TRUE;
    }

  /* Destruction requires the stronger manage-zfs-destroy policy */
  UDISKS_DAEMON_CHECK_AUTHORIZATION (daemon,
                                     UDISKS_OBJECT (object),
                                     ZFS_POLICY_ACTION_ID_DESTROY,
                                     arg_options,
                                     N_("Authentication is required to destroy a ZFS dataset"),
                                     invocation);

  /* Try to unmount first (ignore errors — it may already be unmounted) */
  bd_zfs_dataset_unmount (arg_name, TRUE, NULL);

  if (!bd_zfs_dataset_destroy (arg_name, arg_recursive, TRUE, &error))
    {
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  udisks_linux_module_zfs_trigger_update (object->module);
  udisks_zfspool_complete_destroy_dataset (iface, invocation);

 out:
  return TRUE;
}

static gboolean
handle_mount_dataset (UDisksZFSPool         *iface,
                      GDBusMethodInvocation *invocation,
                      const gchar           *arg_name,
                      GVariant              *arg_options,
                      gpointer               user_data)
{
  UDisksLinuxPoolObjectZFS *object = UDISKS_LINUX_POOL_OBJECT_ZFS (user_data);
  UDisksDaemon *daemon;
  GError *error = NULL;

  daemon = udisks_module_get_daemon (UDISKS_MODULE (object->module));

  /* Cross-pool validation */
  if (!udisks_zfs_validate_name_in_pool (object->name, arg_name, &error))
    {
      g_dbus_method_invocation_take_error (invocation, error);
      return TRUE;
    }

  /* Policy check */
  UDISKS_DAEMON_CHECK_AUTHORIZATION (daemon,
                                     UDISKS_OBJECT (object),
                                     ZFS_POLICY_ACTION_ID,
                                     arg_options,
                                     N_("Authentication is required to mount a ZFS dataset"),
                                     invocation);

  /* TODO: When the UDisks mount options framework (udiskslinuxmountoptions.c)
   * is integrated with the ZFS module, enforce nodev,nosuid defaults here.
   * This requires mapping ZFS mount semantics to the core options framework
   * and is deferred to a future change. */

  if (!bd_zfs_dataset_mount (arg_name, NULL, NULL, &error))
    {
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  udisks_linux_module_zfs_trigger_update (object->module);
  udisks_zfspool_complete_mount_dataset (iface, invocation);

 out:
  return TRUE;
}

static gboolean
handle_unmount_dataset (UDisksZFSPool         *iface,
                        GDBusMethodInvocation *invocation,
                        const gchar           *arg_name,
                        gboolean               arg_force,
                        GVariant              *arg_options,
                        gpointer               user_data)
{
  UDisksLinuxPoolObjectZFS *object = UDISKS_LINUX_POOL_OBJECT_ZFS (user_data);
  UDisksDaemon *daemon;
  GError *error = NULL;

  daemon = udisks_module_get_daemon (UDISKS_MODULE (object->module));

  /* Cross-pool validation */
  if (!udisks_zfs_validate_name_in_pool (object->name, arg_name, &error))
    {
      g_dbus_method_invocation_take_error (invocation, error);
      return TRUE;
    }

  /* Policy check */
  UDISKS_DAEMON_CHECK_AUTHORIZATION (daemon,
                                     UDISKS_OBJECT (object),
                                     ZFS_POLICY_ACTION_ID,
                                     arg_options,
                                     N_("Authentication is required to unmount a ZFS dataset"),
                                     invocation);

  if (!bd_zfs_dataset_unmount (arg_name, arg_force, &error))
    {
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  udisks_linux_module_zfs_trigger_update (object->module);
  udisks_zfspool_complete_unmount_dataset (iface, invocation);

 out:
  return TRUE;
}

static gboolean
handle_create_snapshot (UDisksZFSPool         *iface,
                        GDBusMethodInvocation *invocation,
                        const gchar           *arg_dataset,
                        const gchar           *arg_snap_name,
                        gboolean               arg_recursive,
                        GVariant              *arg_options,
                        gpointer               user_data)
{
  UDisksLinuxPoolObjectZFS *object = UDISKS_LINUX_POOL_OBJECT_ZFS (user_data);
  UDisksDaemon *daemon;
  GError *error = NULL;
  gchar *full_name = NULL;

  daemon = udisks_module_get_daemon (UDISKS_MODULE (object->module));

  /* Cross-pool validation on the dataset part */
  if (!udisks_zfs_validate_name_in_pool (object->name, arg_dataset, &error))
    {
      g_dbus_method_invocation_take_error (invocation, error);
      return TRUE;
    }

  /* Policy check */
  UDISKS_DAEMON_CHECK_AUTHORIZATION (daemon,
                                     UDISKS_OBJECT (object),
                                     ZFS_POLICY_ACTION_ID,
                                     arg_options,
                                     N_("Authentication is required to create a ZFS snapshot"),
                                     invocation);

  full_name = g_strdup_printf ("%s@%s", arg_dataset, arg_snap_name);

  if (!bd_zfs_snapshot_create (full_name, arg_recursive, NULL, &error))
    {
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  udisks_linux_module_zfs_trigger_update (object->module);
  udisks_zfspool_complete_create_snapshot (iface, invocation);

 out:
  g_free (full_name);
  return TRUE;
}

static gboolean
handle_rollback_snapshot (UDisksZFSPool         *iface,
                          GDBusMethodInvocation *invocation,
                          const gchar           *arg_name,
                          GVariant              *arg_options,
                          gpointer               user_data)
{
  UDisksLinuxPoolObjectZFS *object = UDISKS_LINUX_POOL_OBJECT_ZFS (user_data);
  UDisksDaemon *daemon;
  GError *error = NULL;

  daemon = udisks_module_get_daemon (UDISKS_MODULE (object->module));

  /* Cross-pool validation — snapshot names contain '@', validate
   * the full name which starts with pool_name or pool_name/ */
  if (!udisks_zfs_validate_name_in_pool (object->name, arg_name, &error))
    {
      g_dbus_method_invocation_take_error (invocation, error);
      return TRUE;
    }

  /* Rollback can destroy newer snapshots, so use the stronger destroy policy */
  UDISKS_DAEMON_CHECK_AUTHORIZATION (daemon,
                                     UDISKS_OBJECT (object),
                                     ZFS_POLICY_ACTION_ID_DESTROY,
                                     arg_options,
                                     N_("Authentication is required to rollback a ZFS snapshot"),
                                     invocation);

  if (!bd_zfs_snapshot_rollback (arg_name, FALSE, TRUE, &error))
    {
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  udisks_linux_module_zfs_trigger_update (object->module);
  udisks_zfspool_complete_rollback_snapshot (iface, invocation);

 out:
  return TRUE;
}

static gboolean
handle_clone_snapshot (UDisksZFSPool         *iface,
                       GDBusMethodInvocation *invocation,
                       const gchar           *arg_snapshot,
                       const gchar           *arg_clone_name,
                       GVariant              *arg_options,
                       gpointer               user_data)
{
  UDisksLinuxPoolObjectZFS *object = UDISKS_LINUX_POOL_OBJECT_ZFS (user_data);
  UDisksDaemon *daemon;
  GError *error = NULL;
  gchar *full_clone_name = NULL;

  daemon = udisks_module_get_daemon (UDISKS_MODULE (object->module));

  /* Cross-pool validation */
  if (!udisks_zfs_validate_name_in_pool (object->name, arg_snapshot, &error))
    {
      g_dbus_method_invocation_take_error (invocation, error);
      return TRUE;
    }

  /* If the clone name doesn't contain a '/', prepend the pool name */
  if (strchr (arg_clone_name, '/') == NULL)
    full_clone_name = g_strdup_printf ("%s/%s", object->name, arg_clone_name);
  else
    full_clone_name = g_strdup (arg_clone_name);

  /* Validate the full clone name against the pool */
  if (!udisks_zfs_validate_name_in_pool (object->name, full_clone_name, &error))
    {
      g_dbus_method_invocation_take_error (invocation, error);
      g_free (full_clone_name);
      return TRUE;
    }

  /* Policy check */
  UDISKS_DAEMON_CHECK_AUTHORIZATION (daemon,
                                     UDISKS_OBJECT (object),
                                     ZFS_POLICY_ACTION_ID,
                                     arg_options,
                                     N_("Authentication is required to clone a ZFS snapshot"),
                                     invocation);

  if (!bd_zfs_snapshot_clone (arg_snapshot, full_clone_name, NULL, &error))
    {
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  udisks_linux_module_zfs_trigger_update (object->module);
  udisks_zfspool_complete_clone_snapshot (iface, invocation, full_clone_name);

 out:
  g_free (full_clone_name);
  return TRUE;
}

static gboolean
handle_rename_dataset (UDisksZFSPool         *iface,
                       GDBusMethodInvocation *invocation,
                       const gchar           *arg_name,
                       const gchar           *arg_new_name,
                       GVariant              *arg_options,
                       gpointer               user_data)
{
  UDisksLinuxPoolObjectZFS *object = UDISKS_LINUX_POOL_OBJECT_ZFS (user_data);
  UDisksDaemon *daemon;
  GError *error = NULL;

  daemon = udisks_module_get_daemon (UDISKS_MODULE (object->module));

  /* Cross-pool validation — both source and destination must belong to this pool */
  if (!udisks_zfs_validate_name_in_pool (object->name, arg_name, &error))
    {
      g_dbus_method_invocation_take_error (invocation, error);
      return TRUE;
    }
  if (!udisks_zfs_validate_name_in_pool (object->name, arg_new_name, &error))
    {
      g_dbus_method_invocation_take_error (invocation, error);
      return TRUE;
    }

  /* Policy check */
  UDISKS_DAEMON_CHECK_AUTHORIZATION (daemon,
                                     UDISKS_OBJECT (object),
                                     ZFS_POLICY_ACTION_ID,
                                     arg_options,
                                     N_("Authentication is required to rename a ZFS dataset"),
                                     invocation);

  if (!bd_zfs_dataset_rename (arg_name, arg_new_name, FALSE, FALSE, &error))
    {
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  udisks_linux_module_zfs_trigger_update (object->module);
  udisks_zfspool_complete_rename_dataset (iface, invocation);

 out:
  return TRUE;
}

static gboolean
handle_set_dataset_property (UDisksZFSPool         *iface,
                             GDBusMethodInvocation *invocation,
                             const gchar           *arg_dataset,
                             const gchar           *arg_property,
                             const gchar           *arg_value,
                             GVariant              *arg_options,
                             gpointer               user_data)
{
  UDisksLinuxPoolObjectZFS *object = UDISKS_LINUX_POOL_OBJECT_ZFS (user_data);
  UDisksDaemon *daemon;
  GError *error = NULL;
  GError *prop_error = NULL;
  const gchar *action_id;

  daemon = udisks_module_get_daemon (UDISKS_MODULE (object->module));

  /* Cross-pool validation */
  if (!udisks_zfs_validate_name_in_pool (object->name, arg_dataset, &error))
    {
      g_dbus_method_invocation_take_error (invocation, error);
      return TRUE;
    }

  if (arg_property == NULL || strlen (arg_property) == 0)
    {
      g_dbus_method_invocation_return_error (invocation,
                                             UDISKS_ERROR,
                                             UDISKS_ERROR_FAILED,
                                             "Property name must not be empty");
      goto out;
    }

  /* Check property against the allowlist */
  if (!udisks_zfs_property_is_allowed (arg_property, &prop_error))
    {
      g_dbus_method_invocation_take_error (invocation, prop_error);
      goto out;
    }

  /* Security-sensitive properties require elevated authorization */
  if (udisks_zfs_property_is_safe (arg_property, NULL))
    action_id = ZFS_POLICY_ACTION_ID;
  else
    action_id = ZFS_POLICY_ACTION_ID_DESTROY;

  UDISKS_DAEMON_CHECK_AUTHORIZATION (daemon,
                                     UDISKS_OBJECT (object),
                                     action_id,
                                     arg_options,
                                     N_("Authentication is required to set a ZFS dataset property"),
                                     invocation);

  if (!bd_zfs_dataset_set_property (arg_dataset, arg_property, arg_value, &error))
    {
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  udisks_linux_module_zfs_trigger_update (object->module);
  udisks_zfspool_complete_set_dataset_property (iface, invocation);

 out:
  return TRUE;
}

static gboolean
handle_get_dataset_property (UDisksZFSPool         *iface,
                             GDBusMethodInvocation *invocation,
                             const gchar           *arg_dataset,
                             const gchar           *arg_property,
                             GVariant              *arg_options,
                             gpointer               user_data)
{
  UDisksLinuxPoolObjectZFS *object = UDISKS_LINUX_POOL_OBJECT_ZFS (user_data);
  UDisksDaemon *daemon;
  GError *error = NULL;
  BDZFSPropertyInfo *prop_info = NULL;

  daemon = udisks_module_get_daemon (UDISKS_MODULE (object->module));

  /* Cross-pool validation */
  if (!udisks_zfs_validate_name_in_pool (object->name, arg_dataset, &error))
    {
      g_dbus_method_invocation_take_error (invocation, error);
      return TRUE;
    }

  /* Policy check */
  UDISKS_DAEMON_CHECK_AUTHORIZATION (daemon,
                                     UDISKS_OBJECT (object),
                                     ZFS_POLICY_ACTION_ID_QUERY,
                                     arg_options,
                                     N_("Authentication is required to query a ZFS dataset property"),
                                     invocation);

  if (arg_property == NULL || strlen (arg_property) == 0)
    {
      g_dbus_method_invocation_return_error (invocation,
                                             UDISKS_ERROR,
                                             UDISKS_ERROR_FAILED,
                                             "Property name must not be empty");
      goto out;
    }

  prop_info = bd_zfs_dataset_get_property (arg_dataset, arg_property, &error);
  if (prop_info == NULL)
    {
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  udisks_zfspool_complete_get_dataset_property (iface,
                                                invocation,
                                                prop_info->value ? prop_info->value : "",
                                                prop_info->source ? prop_info->source : "");
  bd_zfs_property_info_free (prop_info);

 out:
  return TRUE;
}

static gboolean
handle_load_key (UDisksZFSPool         *iface,
                 GDBusMethodInvocation *invocation,
                 const gchar           *arg_dataset,
                 GVariant              *arg_options,
                 gpointer               user_data)
{
  UDisksLinuxPoolObjectZFS *object = UDISKS_LINUX_POOL_OBJECT_ZFS (user_data);
  UDisksDaemon *daemon;
  GError *error = NULL;
  gboolean success = FALSE;
  GVariant *passphrase_v = NULL;
  const gchar *key_location = NULL;

  daemon = udisks_module_get_daemon (UDISKS_MODULE (object->module));

  /* Cross-pool validation */
  if (!udisks_zfs_validate_name_in_pool (object->name, arg_dataset, &error))
    {
      g_dbus_method_invocation_take_error (invocation, error);
      return TRUE;
    }

  UDISKS_DAEMON_CHECK_AUTHORIZATION (daemon,
                                     UDISKS_OBJECT (object),
                                     ZFS_POLICY_ACTION_ID,
                                     arg_options,
                                     N_("Authentication is required to load a ZFS encryption key"),
                                     invocation);

  passphrase_v = g_variant_lookup_value (arg_options, "passphrase", G_VARIANT_TYPE_BYTESTRING);
  if (passphrase_v)
    {
      gsize len = 0;
      const guchar *passphrase_data = g_variant_get_fixed_array (passphrase_v, &len, sizeof (guchar));
      /* Convert to NUL-terminated string */
      gchar *passphrase = g_strndup ((const gchar *) passphrase_data, len);

      success = bd_zfs_encryption_load_key (arg_dataset, passphrase, &error);

      /* SECURITY: zero out passphrase memory before freeing;
       * explicit_bzero() is not subject to dead-store elimination. */
      explicit_bzero (passphrase, len);
      g_free (passphrase);
      g_variant_unref (passphrase_v);
    }
  else if (g_variant_lookup (arg_options, "key_location", "&s", &key_location))
    {
      success = bd_zfs_encryption_load_key (arg_dataset, key_location, &error);
    }
  else
    {
      success = bd_zfs_encryption_load_key (arg_dataset, NULL, &error);
    }

  if (!success)
    {
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  udisks_linux_module_zfs_trigger_update (object->module);
  udisks_zfspool_complete_load_key (iface, invocation);

 out:
  return TRUE;
}

static gboolean
handle_unload_key (UDisksZFSPool         *iface,
                   GDBusMethodInvocation *invocation,
                   const gchar           *arg_dataset,
                   GVariant              *arg_options,
                   gpointer               user_data)
{
  UDisksLinuxPoolObjectZFS *object = UDISKS_LINUX_POOL_OBJECT_ZFS (user_data);
  UDisksDaemon *daemon;
  GError *error = NULL;

  daemon = udisks_module_get_daemon (UDISKS_MODULE (object->module));

  /* Cross-pool validation */
  if (!udisks_zfs_validate_name_in_pool (object->name, arg_dataset, &error))
    {
      g_dbus_method_invocation_take_error (invocation, error);
      return TRUE;
    }

  UDISKS_DAEMON_CHECK_AUTHORIZATION (daemon,
                                     UDISKS_OBJECT (object),
                                     ZFS_POLICY_ACTION_ID,
                                     arg_options,
                                     N_("Authentication is required to unload a ZFS encryption key"),
                                     invocation);

  if (!bd_zfs_encryption_unload_key (arg_dataset, &error))
    {
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  udisks_linux_module_zfs_trigger_update (object->module);
  udisks_zfspool_complete_unload_key (iface, invocation);

 out:
  return TRUE;
}

static gboolean
handle_change_key (UDisksZFSPool         *iface,
                   GDBusMethodInvocation *invocation,
                   const gchar           *arg_dataset,
                   GVariant              *arg_options,
                   gpointer               user_data)
{
  UDisksLinuxPoolObjectZFS *object = UDISKS_LINUX_POOL_OBJECT_ZFS (user_data);
  UDisksDaemon *daemon;
  GError *error = NULL;
  const gchar *new_key_location = NULL;

  daemon = udisks_module_get_daemon (UDISKS_MODULE (object->module));

  /* Cross-pool validation */
  if (!udisks_zfs_validate_name_in_pool (object->name, arg_dataset, &error))
    {
      g_dbus_method_invocation_take_error (invocation, error);
      return TRUE;
    }

  /* Changing encryption is destructive-level */
  UDISKS_DAEMON_CHECK_AUTHORIZATION (daemon,
                                     UDISKS_OBJECT (object),
                                     ZFS_POLICY_ACTION_ID_DESTROY,
                                     arg_options,
                                     N_("Authentication is required to change a ZFS encryption key"),
                                     invocation);

  g_variant_lookup (arg_options, "new_key_location", "&s", &new_key_location);

  if (!bd_zfs_encryption_change_key (arg_dataset, new_key_location, NULL, &error))
    {
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  udisks_linux_module_zfs_trigger_update (object->module);
  udisks_zfspool_complete_change_key (iface, invocation);

 out:
  return TRUE;
}

/* ---------------------------------------------------------------------------------------------------- */
/*  GetVdevTopology helpers                                                                             */
/* ---------------------------------------------------------------------------------------------------- */

static const gchar *
vdev_type_to_string (BDZFSVdevType type)
{
  switch (type)
    {
    case BD_ZFS_VDEV_TYPE_DISK:
      return "disk";
    case BD_ZFS_VDEV_TYPE_FILE:
      return "file";
    case BD_ZFS_VDEV_TYPE_MIRROR:
      return "mirror";
    case BD_ZFS_VDEV_TYPE_RAIDZ1:
      return "raidz1";
    case BD_ZFS_VDEV_TYPE_RAIDZ2:
      return "raidz2";
    case BD_ZFS_VDEV_TYPE_RAIDZ3:
      return "raidz3";
    case BD_ZFS_VDEV_TYPE_DRAID:
      return "draid";
    case BD_ZFS_VDEV_TYPE_SPARE:
      return "spare";
    case BD_ZFS_VDEV_TYPE_LOG:
      return "log";
    case BD_ZFS_VDEV_TYPE_CACHE:
      return "cache";
    case BD_ZFS_VDEV_TYPE_SPECIAL:
      return "special";
    case BD_ZFS_VDEV_TYPE_DEDUP:
      return "dedup";
    case BD_ZFS_VDEV_TYPE_ROOT:
      return "root";
    case BD_ZFS_VDEV_TYPE_UNKNOWN:
    default:
      return "unknown";
    }
}

static const gchar *
vdev_state_to_string (BDZFSVdevState state)
{
  switch (state)
    {
    case BD_ZFS_VDEV_STATE_ONLINE:
      return "ONLINE";
    case BD_ZFS_VDEV_STATE_DEGRADED:
      return "DEGRADED";
    case BD_ZFS_VDEV_STATE_FAULTED:
      return "FAULTED";
    case BD_ZFS_VDEV_STATE_OFFLINE:
      return "OFFLINE";
    case BD_ZFS_VDEV_STATE_UNAVAIL:
      return "UNAVAIL";
    case BD_ZFS_VDEV_STATE_REMOVED:
      return "REMOVED";
    default:
      return "UNKNOWN";
    }
}

/**
 * vdev_info_to_variant:
 * @vdev: A #BDZFSVdevInfo.
 *
 * Recursively converts a vdev info tree into a GVariant of type "a{sv}".
 * Children are represented as a nested "aa{sv}" under the "children" key.
 *
 * Returns: (transfer full): A floating #GVariant of type "a{sv}".
 */
static GVariant *
vdev_info_to_variant (BDZFSVdevInfo *vdev)
{
  GVariantBuilder builder;

  g_variant_builder_init (&builder, G_VARIANT_TYPE ("a{sv}"));

  g_variant_builder_add (&builder, "{sv}", "path",
                         g_variant_new_string (vdev->path ? vdev->path : ""));
  g_variant_builder_add (&builder, "{sv}", "type",
                         g_variant_new_string (vdev_type_to_string (vdev->type)));
  g_variant_builder_add (&builder, "{sv}", "state",
                         g_variant_new_string (vdev_state_to_string (vdev->state)));
  g_variant_builder_add (&builder, "{sv}", "read_errors",
                         g_variant_new_uint64 (vdev->read_errors));
  g_variant_builder_add (&builder, "{sv}", "write_errors",
                         g_variant_new_uint64 (vdev->write_errors));
  g_variant_builder_add (&builder, "{sv}", "checksum_errors",
                         g_variant_new_uint64 (vdev->checksum_errors));

  if (vdev->children != NULL)
    {
      GVariantBuilder children_builder;

      g_variant_builder_init (&children_builder, G_VARIANT_TYPE ("aa{sv}"));
      for (BDZFSVdevInfo **child = vdev->children; *child != NULL; child++)
        g_variant_builder_add_value (&children_builder, vdev_info_to_variant (*child));
      g_variant_builder_add (&builder, "{sv}", "children",
                             g_variant_builder_end (&children_builder));
    }

  return g_variant_builder_end (&builder);
}

static gboolean
handle_get_vdev_topology (UDisksZFSPool         *iface,
                          GDBusMethodInvocation *invocation,
                          GVariant              *arg_options,
                          gpointer               user_data)
{
  UDisksLinuxPoolObjectZFS *object = UDISKS_LINUX_POOL_OBJECT_ZFS (user_data);
  UDisksDaemon *daemon;
  GError *error = NULL;
  BDZFSVdevInfo **vdevs = NULL;
  GVariantBuilder topology_builder;

  daemon = udisks_module_get_daemon (UDISKS_MODULE (object->module));

  /* Policy check */
  UDISKS_DAEMON_CHECK_AUTHORIZATION (daemon,
                                     UDISKS_OBJECT (object),
                                     ZFS_POLICY_ACTION_ID_QUERY,
                                     arg_options,
                                     N_("Authentication is required to query ZFS pool vdev topology"),
                                     invocation);

  vdevs = bd_zfs_pool_get_vdevs (object->name, &error);
  if (vdevs == NULL)
    {
      if (error != NULL)
        {
          g_dbus_method_invocation_take_error (invocation, error);
          goto out;
        }
    }

  g_variant_builder_init (&topology_builder, G_VARIANT_TYPE ("aa{sv}"));

  if (vdevs != NULL)
    {
      for (BDZFSVdevInfo **p = vdevs; *p != NULL; p++)
        g_variant_builder_add_value (&topology_builder, vdev_info_to_variant (*p));
    }

  udisks_zfspool_complete_get_vdev_topology (iface, invocation,
                                              g_variant_builder_end (&topology_builder));

  /* Free the vdev info array */
  if (vdevs != NULL)
    {
      for (BDZFSVdevInfo **p = vdevs; *p != NULL; p++)
        bd_zfs_vdev_info_free (*p);
      g_free (vdevs);
    }

 out:
  return TRUE;
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
handle_trim_stop (UDisksZFSPool         *iface,
                  GDBusMethodInvocation *invocation,
                  GVariant              *arg_options,
                  gpointer               user_data)
{
  UDisksLinuxPoolObjectZFS *object = UDISKS_LINUX_POOL_OBJECT_ZFS (user_data);
  UDisksDaemon *daemon;
  GError *error = NULL;

  daemon = udisks_module_get_daemon (UDISKS_MODULE (object->module));

  UDISKS_DAEMON_CHECK_AUTHORIZATION (daemon,
                                     UDISKS_OBJECT (object),
                                     ZFS_POLICY_ACTION_ID,
                                     arg_options,
                                     N_("Authentication is required to stop a ZFS pool TRIM"),
                                     invocation);

  if (!bd_zfs_pool_trim_stop (object->name, NULL, &error))
    {
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  udisks_linux_module_zfs_trigger_update (object->module);
  udisks_zfspool_complete_trim_stop (iface, invocation);

 out:
  return TRUE;
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
handle_clear_errors (UDisksZFSPool         *iface,
                     GDBusMethodInvocation *invocation,
                     const gchar           *arg_device,
                     GVariant              *arg_options,
                     gpointer               user_data)
{
  UDisksLinuxPoolObjectZFS *object = UDISKS_LINUX_POOL_OBJECT_ZFS (user_data);
  UDisksDaemon *daemon;
  GError *error = NULL;

  daemon = udisks_module_get_daemon (UDISKS_MODULE (object->module));

  UDISKS_DAEMON_CHECK_AUTHORIZATION (daemon,
                                     UDISKS_OBJECT (object),
                                     ZFS_POLICY_ACTION_ID,
                                     arg_options,
                                     N_("Authentication is required to clear ZFS pool errors"),
                                     invocation);

  if (!bd_zfs_pool_clear (object->name,
                           (arg_device && strlen (arg_device) > 0) ? arg_device : NULL,
                           &error))
    {
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  udisks_linux_module_zfs_trigger_update (object->module);
  udisks_zfspool_complete_clear_errors (iface, invocation);

 out:
  return TRUE;
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
handle_upgrade (UDisksZFSPool         *iface,
                GDBusMethodInvocation *invocation,
                GVariant              *arg_options,
                gpointer               user_data)
{
  UDisksLinuxPoolObjectZFS *object = UDISKS_LINUX_POOL_OBJECT_ZFS (user_data);
  UDisksDaemon *daemon;
  GError *error = NULL;

  daemon = udisks_module_get_daemon (UDISKS_MODULE (object->module));

  /* Upgrade is irreversible, use the destroy-level policy */
  UDISKS_DAEMON_CHECK_AUTHORIZATION (daemon,
                                     UDISKS_OBJECT (object),
                                     ZFS_POLICY_ACTION_ID_DESTROY,
                                     arg_options,
                                     N_("Authentication is required to upgrade a ZFS pool"),
                                     invocation);

  if (!bd_zfs_pool_upgrade (object->name, &error))
    {
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  udisks_linux_module_zfs_trigger_update (object->module);
  udisks_zfspool_complete_upgrade (iface, invocation);

 out:
  return TRUE;
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
handle_get_history (UDisksZFSPool         *iface,
                    GDBusMethodInvocation *invocation,
                    GVariant              *arg_options,
                    gpointer               user_data)
{
  UDisksLinuxPoolObjectZFS *object = UDISKS_LINUX_POOL_OBJECT_ZFS (user_data);
  UDisksDaemon *daemon;
  GError *error = NULL;
  gchar *output = NULL;
  const gchar *argv[] = { "zpool", "history", object->name, NULL };

  daemon = udisks_module_get_daemon (UDISKS_MODULE (object->module));

  UDISKS_DAEMON_CHECK_AUTHORIZATION (daemon,
                                     UDISKS_OBJECT (object),
                                     ZFS_POLICY_ACTION_ID_QUERY,
                                     arg_options,
                                     N_("Authentication is required to view ZFS pool history"),
                                     invocation);

  if (!bd_utils_exec_and_capture_output (argv, NULL, &output, &error))
    {
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  udisks_zfspool_complete_get_history (iface, invocation, output ? output : "");

 out:
  g_free (output);
  return TRUE;
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
handle_promote_clone (UDisksZFSPool         *iface,
                      GDBusMethodInvocation *invocation,
                      const gchar           *arg_clone_name,
                      GVariant              *arg_options,
                      gpointer               user_data)
{
  UDisksLinuxPoolObjectZFS *object = UDISKS_LINUX_POOL_OBJECT_ZFS (user_data);
  UDisksDaemon *daemon;
  GError *error = NULL;

  daemon = udisks_module_get_daemon (UDISKS_MODULE (object->module));

  /* Cross-pool validation */
  if (!udisks_zfs_validate_name_in_pool (object->name, arg_clone_name, &error))
    {
      g_dbus_method_invocation_take_error (invocation, error);
      return TRUE;
    }

  /* Promote is constructive, use manage-zfs policy */
  UDISKS_DAEMON_CHECK_AUTHORIZATION (daemon,
                                     UDISKS_OBJECT (object),
                                     ZFS_POLICY_ACTION_ID,
                                     arg_options,
                                     N_("Authentication is required to promote a ZFS clone"),
                                     invocation);

  if (!bd_zfs_snapshot_promote (arg_clone_name, &error))
    {
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  udisks_linux_module_zfs_trigger_update (object->module);
  udisks_zfspool_complete_promote_clone (iface, invocation);

 out:
  return TRUE;
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
handle_hold_snapshot (UDisksZFSPool         *iface,
                      GDBusMethodInvocation *invocation,
                      const gchar           *arg_snapshot,
                      const gchar           *arg_tag,
                      GVariant              *arg_options,
                      gpointer               user_data)
{
  UDisksLinuxPoolObjectZFS *object = UDISKS_LINUX_POOL_OBJECT_ZFS (user_data);
  UDisksDaemon *daemon;
  GError *error = NULL;

  daemon = udisks_module_get_daemon (UDISKS_MODULE (object->module));

  /* Cross-pool validation */
  if (!udisks_zfs_validate_name_in_pool (object->name, arg_snapshot, &error))
    {
      g_dbus_method_invocation_take_error (invocation, error);
      return TRUE;
    }

  if (arg_tag == NULL || strlen (arg_tag) == 0)
    {
      g_dbus_method_invocation_return_error (invocation,
                                             UDISKS_ERROR,
                                             UDISKS_ERROR_FAILED,
                                             "Hold tag must not be empty");
      goto out;
    }

  UDISKS_DAEMON_CHECK_AUTHORIZATION (daemon,
                                     UDISKS_OBJECT (object),
                                     ZFS_POLICY_ACTION_ID,
                                     arg_options,
                                     N_("Authentication is required to hold a ZFS snapshot"),
                                     invocation);

  if (!bd_zfs_snapshot_hold (arg_snapshot, arg_tag, &error))
    {
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  udisks_zfspool_complete_hold_snapshot (iface, invocation);

 out:
  return TRUE;
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
handle_release_snapshot (UDisksZFSPool         *iface,
                         GDBusMethodInvocation *invocation,
                         const gchar           *arg_snapshot,
                         const gchar           *arg_tag,
                         GVariant              *arg_options,
                         gpointer               user_data)
{
  UDisksLinuxPoolObjectZFS *object = UDISKS_LINUX_POOL_OBJECT_ZFS (user_data);
  UDisksDaemon *daemon;
  GError *error = NULL;

  daemon = udisks_module_get_daemon (UDISKS_MODULE (object->module));

  /* Cross-pool validation */
  if (!udisks_zfs_validate_name_in_pool (object->name, arg_snapshot, &error))
    {
      g_dbus_method_invocation_take_error (invocation, error);
      return TRUE;
    }

  if (arg_tag == NULL || strlen (arg_tag) == 0)
    {
      g_dbus_method_invocation_return_error (invocation,
                                             UDISKS_ERROR,
                                             UDISKS_ERROR_FAILED,
                                             "Hold tag must not be empty");
      goto out;
    }

  UDISKS_DAEMON_CHECK_AUTHORIZATION (daemon,
                                     UDISKS_OBJECT (object),
                                     ZFS_POLICY_ACTION_ID,
                                     arg_options,
                                     N_("Authentication is required to release a ZFS snapshot hold"),
                                     invocation);

  if (!bd_zfs_snapshot_release (arg_snapshot, arg_tag, &error))
    {
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  udisks_zfspool_complete_release_snapshot (iface, invocation);

 out:
  return TRUE;
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
handle_inherit_property (UDisksZFSPool         *iface,
                         GDBusMethodInvocation *invocation,
                         const gchar           *arg_dataset,
                         const gchar           *arg_property,
                         GVariant              *arg_options,
                         gpointer               user_data)
{
  UDisksLinuxPoolObjectZFS *object = UDISKS_LINUX_POOL_OBJECT_ZFS (user_data);
  UDisksDaemon *daemon;
  GError *error = NULL;

  daemon = udisks_module_get_daemon (UDISKS_MODULE (object->module));

  /* Cross-pool validation */
  if (!udisks_zfs_validate_name_in_pool (object->name, arg_dataset, &error))
    {
      g_dbus_method_invocation_take_error (invocation, error);
      return TRUE;
    }

  if (arg_property == NULL || strlen (arg_property) == 0)
    {
      g_dbus_method_invocation_return_error (invocation,
                                             UDISKS_ERROR,
                                             UDISKS_ERROR_FAILED,
                                             "Property name must not be empty");
      goto out;
    }

  UDISKS_DAEMON_CHECK_AUTHORIZATION (daemon,
                                     UDISKS_OBJECT (object),
                                     ZFS_POLICY_ACTION_ID,
                                     arg_options,
                                     N_("Authentication is required to inherit a ZFS dataset property"),
                                     invocation);

  if (!bd_zfs_dataset_inherit_property (arg_dataset, arg_property, &error))
    {
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  udisks_linux_module_zfs_trigger_update (object->module);
  udisks_zfspool_complete_inherit_property (iface, invocation);

 out:
  return TRUE;
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
handle_resize_volume (UDisksZFSPool         *iface,
                      GDBusMethodInvocation *invocation,
                      const gchar           *arg_name,
                      guint64                arg_new_size,
                      GVariant              *arg_options,
                      gpointer               user_data)
{
  UDisksLinuxPoolObjectZFS *object = UDISKS_LINUX_POOL_OBJECT_ZFS (user_data);
  UDisksDaemon *daemon;
  GError *error = NULL;
  gchar *size_str = NULL;

  daemon = udisks_module_get_daemon (UDISKS_MODULE (object->module));

  /* Cross-pool validation */
  if (!udisks_zfs_validate_name_in_pool (object->name, arg_name, &error))
    {
      g_dbus_method_invocation_take_error (invocation, error);
      return TRUE;
    }

  if (arg_new_size == 0)
    {
      g_dbus_method_invocation_return_error (invocation,
                                             UDISKS_ERROR,
                                             UDISKS_ERROR_FAILED,
                                             "New size must be greater than zero");
      goto out;
    }

  UDISKS_DAEMON_CHECK_AUTHORIZATION (daemon,
                                     UDISKS_OBJECT (object),
                                     ZFS_POLICY_ACTION_ID,
                                     arg_options,
                                     N_("Authentication is required to resize a ZFS volume"),
                                     invocation);

  size_str = g_strdup_printf ("%" G_GUINT64_FORMAT, arg_new_size);

  if (!bd_zfs_dataset_set_property (arg_name, "volsize", size_str, &error))
    {
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  udisks_linux_module_zfs_trigger_update (object->module);
  udisks_zfspool_complete_resize_volume (iface, invocation);

 out:
  g_free (size_str);
  return TRUE;
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
handle_create_bookmark (UDisksZFSPool         *iface,
                        GDBusMethodInvocation *invocation,
                        const gchar           *arg_snapshot,
                        const gchar           *arg_bookmark,
                        GVariant              *arg_options,
                        gpointer               user_data)
{
  UDisksLinuxPoolObjectZFS *object = UDISKS_LINUX_POOL_OBJECT_ZFS (user_data);
  UDisksDaemon *daemon;
  GError *error = NULL;

  daemon = udisks_module_get_daemon (UDISKS_MODULE (object->module));

  /* Cross-pool validation */
  if (!udisks_zfs_validate_name_in_pool (object->name, arg_snapshot, &error))
    {
      g_dbus_method_invocation_take_error (invocation, error);
      return TRUE;
    }

  UDISKS_DAEMON_CHECK_AUTHORIZATION (daemon,
                                     UDISKS_OBJECT (object),
                                     ZFS_POLICY_ACTION_ID,
                                     arg_options,
                                     N_("Authentication is required to create a ZFS bookmark"),
                                     invocation);

  if (!bd_zfs_bookmark_create (arg_snapshot, arg_bookmark, &error))
    {
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  udisks_linux_module_zfs_trigger_update (object->module);
  udisks_zfspool_complete_create_bookmark (iface, invocation);

 out:
  return TRUE;
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
handle_destroy_bookmark (UDisksZFSPool         *iface,
                         GDBusMethodInvocation *invocation,
                         const gchar           *arg_name,
                         GVariant              *arg_options,
                         gpointer               user_data)
{
  UDisksLinuxPoolObjectZFS *object = UDISKS_LINUX_POOL_OBJECT_ZFS (user_data);
  UDisksDaemon *daemon;
  GError *error = NULL;

  daemon = udisks_module_get_daemon (UDISKS_MODULE (object->module));

  /* Cross-pool validation */
  if (!udisks_zfs_validate_name_in_pool (object->name, arg_name, &error))
    {
      g_dbus_method_invocation_take_error (invocation, error);
      return TRUE;
    }

  /* Bookmark destruction uses destroy-level policy */
  UDISKS_DAEMON_CHECK_AUTHORIZATION (daemon,
                                     UDISKS_OBJECT (object),
                                     ZFS_POLICY_ACTION_ID_DESTROY,
                                     arg_options,
                                     N_("Authentication is required to destroy a ZFS bookmark"),
                                     invocation);

  if (!bd_zfs_bookmark_destroy (arg_name, &error))
    {
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  udisks_linux_module_zfs_trigger_update (object->module);
  udisks_zfspool_complete_destroy_bookmark (iface, invocation);

 out:
  return TRUE;
}

/* ---------------------------------------------------------------------------------------------------- */

static void
udisks_linux_pool_object_zfs_constructed (GObject *_object)
{
  UDisksLinuxPoolObjectZFS *object = UDISKS_LINUX_POOL_OBJECT_ZFS (_object);
  GString *s;

  if (G_OBJECT_CLASS (udisks_linux_pool_object_zfs_parent_class)->constructed != NULL)
    G_OBJECT_CLASS (udisks_linux_pool_object_zfs_parent_class)->constructed (_object);

  /* compute the object path */
  s = g_string_new ("/org/freedesktop/UDisks2/zfs/");
  udisks_safe_append_to_object_path (s, object->name);
  g_dbus_object_skeleton_set_object_path (G_DBUS_OBJECT_SKELETON (object), s->str);
  g_string_free (s, TRUE);

  /* create the D-Bus interface */
  object->iface_zfs_pool = udisks_zfspool_skeleton_new ();

  /* allow method invocations to be handled in a worker thread */
  g_dbus_interface_skeleton_set_flags (G_DBUS_INTERFACE_SKELETON (object->iface_zfs_pool),
                                       G_DBUS_INTERFACE_SKELETON_FLAGS_HANDLE_METHOD_INVOCATIONS_IN_THREAD);

  /* connect ZFSPool method handlers */
  g_signal_connect (object->iface_zfs_pool, "handle-poll",
                    G_CALLBACK (handle_poll), object);
  g_signal_connect (object->iface_zfs_pool, "handle-export",
                    G_CALLBACK (handle_export), object);
  g_signal_connect (object->iface_zfs_pool, "handle-destroy",
                    G_CALLBACK (handle_destroy), object);
  g_signal_connect (object->iface_zfs_pool, "handle-add-vdev",
                    G_CALLBACK (handle_add_vdev), object);
  g_signal_connect (object->iface_zfs_pool, "handle-remove-vdev",
                    G_CALLBACK (handle_remove_vdev), object);
  g_signal_connect (object->iface_zfs_pool, "handle-replace-vdev",
                    G_CALLBACK (handle_replace_vdev), object);
  g_signal_connect (object->iface_zfs_pool, "handle-attach-vdev",
                    G_CALLBACK (handle_attach_vdev), object);
  g_signal_connect (object->iface_zfs_pool, "handle-detach-vdev",
                    G_CALLBACK (handle_detach_vdev), object);
  g_signal_connect (object->iface_zfs_pool, "handle-online-vdev",
                    G_CALLBACK (handle_online_vdev), object);
  g_signal_connect (object->iface_zfs_pool, "handle-offline-vdev",
                    G_CALLBACK (handle_offline_vdev), object);
  g_signal_connect (object->iface_zfs_pool, "handle-scrub-start",
                    G_CALLBACK (handle_scrub_start), object);
  g_signal_connect (object->iface_zfs_pool, "handle-scrub-pause",
                    G_CALLBACK (handle_scrub_pause), object);
  g_signal_connect (object->iface_zfs_pool, "handle-scrub-stop",
                    G_CALLBACK (handle_scrub_stop), object);
  g_signal_connect (object->iface_zfs_pool, "handle-trim-start",
                    G_CALLBACK (handle_trim_start), object);
  g_signal_connect (object->iface_zfs_pool, "handle-trim-stop",
                    G_CALLBACK (handle_trim_stop), object);
  g_signal_connect (object->iface_zfs_pool, "handle-clear-errors",
                    G_CALLBACK (handle_clear_errors), object);
  g_signal_connect (object->iface_zfs_pool, "handle-upgrade",
                    G_CALLBACK (handle_upgrade), object);
  g_signal_connect (object->iface_zfs_pool, "handle-get-history",
                    G_CALLBACK (handle_get_history), object);
  g_signal_connect (object->iface_zfs_pool, "handle-set-property",
                    G_CALLBACK (handle_set_property), object);
  g_signal_connect (object->iface_zfs_pool, "handle-get-property",
                    G_CALLBACK (handle_get_property), object);

  /* Dataset management methods */
  g_signal_connect (object->iface_zfs_pool, "handle-create-dataset",
                    G_CALLBACK (handle_create_dataset), object);
  g_signal_connect (object->iface_zfs_pool, "handle-create-volume",
                    G_CALLBACK (handle_create_volume), object);
  g_signal_connect (object->iface_zfs_pool, "handle-list-datasets",
                    G_CALLBACK (handle_list_datasets), object);
  g_signal_connect (object->iface_zfs_pool, "handle-destroy-dataset",
                    G_CALLBACK (handle_destroy_dataset), object);
  g_signal_connect (object->iface_zfs_pool, "handle-mount-dataset",
                    G_CALLBACK (handle_mount_dataset), object);
  g_signal_connect (object->iface_zfs_pool, "handle-unmount-dataset",
                    G_CALLBACK (handle_unmount_dataset), object);
  g_signal_connect (object->iface_zfs_pool, "handle-create-snapshot",
                    G_CALLBACK (handle_create_snapshot), object);
  g_signal_connect (object->iface_zfs_pool, "handle-rollback-snapshot",
                    G_CALLBACK (handle_rollback_snapshot), object);
  g_signal_connect (object->iface_zfs_pool, "handle-clone-snapshot",
                    G_CALLBACK (handle_clone_snapshot), object);
  g_signal_connect (object->iface_zfs_pool, "handle-rename-dataset",
                    G_CALLBACK (handle_rename_dataset), object);
  g_signal_connect (object->iface_zfs_pool, "handle-set-dataset-property",
                    G_CALLBACK (handle_set_dataset_property), object);
  g_signal_connect (object->iface_zfs_pool, "handle-get-dataset-property",
                    G_CALLBACK (handle_get_dataset_property), object);
  g_signal_connect (object->iface_zfs_pool, "handle-load-key",
                    G_CALLBACK (handle_load_key), object);
  g_signal_connect (object->iface_zfs_pool, "handle-unload-key",
                    G_CALLBACK (handle_unload_key), object);
  g_signal_connect (object->iface_zfs_pool, "handle-change-key",
                    G_CALLBACK (handle_change_key), object);
  g_signal_connect (object->iface_zfs_pool, "handle-get-vdev-topology",
                    G_CALLBACK (handle_get_vdev_topology), object);

  /* New dataset/snapshot/bookmark methods */
  g_signal_connect (object->iface_zfs_pool, "handle-promote-clone",
                    G_CALLBACK (handle_promote_clone), object);
  g_signal_connect (object->iface_zfs_pool, "handle-hold-snapshot",
                    G_CALLBACK (handle_hold_snapshot), object);
  g_signal_connect (object->iface_zfs_pool, "handle-release-snapshot",
                    G_CALLBACK (handle_release_snapshot), object);
  g_signal_connect (object->iface_zfs_pool, "handle-inherit-property",
                    G_CALLBACK (handle_inherit_property), object);
  g_signal_connect (object->iface_zfs_pool, "handle-resize-volume",
                    G_CALLBACK (handle_resize_volume), object);
  g_signal_connect (object->iface_zfs_pool, "handle-create-bookmark",
                    G_CALLBACK (handle_create_bookmark), object);
  g_signal_connect (object->iface_zfs_pool, "handle-destroy-bookmark",
                    G_CALLBACK (handle_destroy_bookmark), object);

  g_dbus_object_skeleton_add_interface (G_DBUS_OBJECT_SKELETON (object),
                                        G_DBUS_INTERFACE_SKELETON (object->iface_zfs_pool));
}

static void
udisks_linux_pool_object_zfs_class_init (UDisksLinuxPoolObjectZFSClass *klass)
{
  GObjectClass *gobject_class;

  gobject_class = G_OBJECT_CLASS (klass);
  gobject_class->finalize     = udisks_linux_pool_object_zfs_finalize;
  gobject_class->constructed  = udisks_linux_pool_object_zfs_constructed;
  gobject_class->set_property = udisks_linux_pool_object_zfs_set_property;
  gobject_class->get_property = udisks_linux_pool_object_zfs_get_property;

  /**
   * UDisksLinuxPoolObjectZFS:module:
   *
   * The #UDisksLinuxModuleZFS the object is for.
   */
  g_object_class_install_property (gobject_class,
                                   PROP_MODULE,
                                   g_param_spec_object ("module",
                                                        "Module",
                                                        "The module the object is for",
                                                        UDISKS_TYPE_LINUX_MODULE_ZFS,
                                                        G_PARAM_READABLE |
                                                        G_PARAM_WRITABLE |
                                                        G_PARAM_CONSTRUCT_ONLY |
                                                        G_PARAM_STATIC_STRINGS));

  /**
   * UDisksLinuxPoolObjectZFS:name:
   *
   * The name of the ZFS pool.
   */
  g_object_class_install_property (gobject_class,
                                   PROP_NAME,
                                   g_param_spec_string ("name",
                                                        "Name",
                                                        "The name of the ZFS pool",
                                                        NULL,
                                                        G_PARAM_WRITABLE |
                                                        G_PARAM_CONSTRUCT_ONLY |
                                                        G_PARAM_STATIC_STRINGS));
}

/**
 * udisks_linux_pool_object_zfs_new:
 * @module: A #UDisksLinuxModuleZFS.
 * @name: The name of the ZFS pool.
 *
 * Create a new ZFS pool object.
 *
 * Returns: A #UDisksLinuxPoolObjectZFS object. Free with g_object_unref().
 */
UDisksLinuxPoolObjectZFS *
udisks_linux_pool_object_zfs_new (UDisksLinuxModuleZFS *module,
                                  const gchar          *name)
{
  g_return_val_if_fail (UDISKS_IS_LINUX_MODULE_ZFS (module), NULL);
  g_return_val_if_fail (name != NULL, NULL);
  return UDISKS_LINUX_POOL_OBJECT_ZFS (g_object_new (UDISKS_TYPE_LINUX_POOL_OBJECT_ZFS,
                                                      "module", module,
                                                      "name", name,
                                                      NULL));
}

/**
 * udisks_linux_pool_object_zfs_get_module:
 * @object: A #UDisksLinuxPoolObjectZFS.
 *
 * Gets the module used by @object.
 *
 * Returns: A #UDisksLinuxModuleZFS. Do not free, the object is owned by @object.
 */
UDisksLinuxModuleZFS *
udisks_linux_pool_object_zfs_get_module (UDisksLinuxPoolObjectZFS *object)
{
  g_return_val_if_fail (UDISKS_IS_LINUX_POOL_OBJECT_ZFS (object), NULL);
  return object->module;
}

/**
 * udisks_linux_pool_object_zfs_get_name:
 * @object: A #UDisksLinuxPoolObjectZFS.
 *
 * Gets the name for @object.
 *
 * Returns: (transfer none): The name for object. Do not free, the string belongs to @object.
 */
const gchar *
udisks_linux_pool_object_zfs_get_name (UDisksLinuxPoolObjectZFS *object)
{
  g_return_val_if_fail (UDISKS_IS_LINUX_POOL_OBJECT_ZFS (object), NULL);
  return object->name;
}

/* ---------------------------------------------------------------------------------------------------- */
/*  Scrub status polling                                                                                */
/* ---------------------------------------------------------------------------------------------------- */

/**
 * scrub_poll_callback:
 *
 * GSource callback fired every 5 seconds while a scrub is active.
 * Queries scrub status, updates D-Bus properties, and removes itself
 * when the scrub is no longer active.
 */
static gboolean
scrub_poll_callback (gpointer user_data)
{
  UDisksLinuxPoolObjectZFS *object = UDISKS_LINUX_POOL_OBJECT_ZFS (user_data);
  UDisksZFSPool *iface = object->iface_zfs_pool;
  BDZFSScrubInfo *scrub_info;

  scrub_info = bd_zfs_pool_scrub_status (object->name, NULL);
  if (scrub_info)
    {
      gboolean running = (scrub_info->state == BD_ZFS_SCRUB_STATE_SCANNING);
      gboolean paused  = (scrub_info->state == BD_ZFS_SCRUB_STATE_PAUSED);
      gdouble  progress = scrub_info->percent_done / 100.0;
      guint64  errors   = scrub_info->errors;

      udisks_zfspool_set_scrub_running  (iface, running);
      udisks_zfspool_set_scrub_paused   (iface, paused);
      udisks_zfspool_set_scrub_progress (iface, progress);
      udisks_zfspool_set_scrub_errors   (iface, errors);

      bd_zfs_scrub_info_free (scrub_info);

      if (running || paused)
        return G_SOURCE_CONTINUE;
    }
  else
    {
      /* No scrub info — scrub has finished or never ran */
      udisks_zfspool_set_scrub_running  (iface, FALSE);
      udisks_zfspool_set_scrub_paused   (iface, FALSE);
      udisks_zfspool_set_scrub_progress (iface, 0.0);
      udisks_zfspool_set_scrub_errors   (iface, 0);
    }

  /* Scrub is no longer active; remove the timer */
  object->scrub_poll_id = 0;
  return G_SOURCE_REMOVE;
}

/**
 * update_scrub_properties:
 * @object: A #UDisksLinuxPoolObjectZFS.
 *
 * Queries the current scrub status for the pool and updates the
 * ScrubRunning, ScrubPaused, ScrubProgress and ScrubErrors D-Bus
 * properties accordingly.  Manages the poll timer: starts a 5-second
 * periodic timer when a scrub is active, and removes it once the
 * scrub finishes.
 */
static void
update_scrub_properties (UDisksLinuxPoolObjectZFS *object)
{
  UDisksZFSPool *iface = object->iface_zfs_pool;
  BDZFSScrubInfo *scrub_info;

  scrub_info = bd_zfs_pool_scrub_status (object->name, NULL);
  if (scrub_info)
    {
      gboolean running = (scrub_info->state == BD_ZFS_SCRUB_STATE_SCANNING);
      gboolean paused  = (scrub_info->state == BD_ZFS_SCRUB_STATE_PAUSED);
      gdouble  progress = scrub_info->percent_done / 100.0;
      guint64  errors   = scrub_info->errors;

      udisks_zfspool_set_scrub_running  (iface, running);
      udisks_zfspool_set_scrub_paused   (iface, paused);
      udisks_zfspool_set_scrub_progress (iface, progress);
      udisks_zfspool_set_scrub_errors   (iface, errors);

      /* Start the poll timer if a scrub is in progress and we don't
       * already have one running. */
      if ((running || paused) && object->scrub_poll_id == 0)
        {
          object->scrub_poll_id = g_timeout_add_seconds (5, scrub_poll_callback, object);
        }
      /* Stop the poll timer if the scrub has finished. */
      else if (!running && !paused && object->scrub_poll_id != 0)
        {
          g_source_remove (object->scrub_poll_id);
          object->scrub_poll_id = 0;
        }

      bd_zfs_scrub_info_free (scrub_info);
    }
  else
    {
      /* No scrub info available — clear properties */
      udisks_zfspool_set_scrub_running  (iface, FALSE);
      udisks_zfspool_set_scrub_paused   (iface, FALSE);
      udisks_zfspool_set_scrub_progress (iface, 0.0);
      udisks_zfspool_set_scrub_errors   (iface, 0);

      if (object->scrub_poll_id != 0)
        {
          g_source_remove (object->scrub_poll_id);
          object->scrub_poll_id = 0;
        }
    }
}

/* ---------------------------------------------------------------------------------------------------- */

/**
 * udisks_linux_pool_object_zfs_update:
 * @object: A #UDisksLinuxPoolObjectZFS.
 * @info: A #BDZFSPoolInfo with the pool info.
 *
 * Updates the pool object with the latest information from ZFS.
 */
void
udisks_linux_pool_object_zfs_update (UDisksLinuxPoolObjectZFS *object,
                                     BDZFSPoolInfo            *info)
{
  UDisksZFSPool *iface;

  g_return_if_fail (UDISKS_IS_LINUX_POOL_OBJECT_ZFS (object));
  g_return_if_fail (info != NULL);

  iface = object->iface_zfs_pool;

  udisks_zfspool_set_name (iface, info->name);
  udisks_zfspool_set_guid (iface, info->guid ? info->guid : "");
  udisks_zfspool_set_state (iface, pool_state_to_string (info->state));
  udisks_zfspool_set_size (iface, info->size);
  udisks_zfspool_set_allocated (iface, info->allocated);
  udisks_zfspool_set_free (iface, info->free);
  udisks_zfspool_set_fragmentation (iface, info->fragmentation);
  udisks_zfspool_set_dedup_ratio (iface, info->dedup_ratio);
  udisks_zfspool_set_read_only (iface, info->readonly);
  udisks_zfspool_set_altroot (iface, info->altroot ? info->altroot : "");
  udisks_zfspool_set_health (iface, pool_state_to_string (info->state));

  /* Query and update scrub status (also starts/stops the poll timer) */
  update_scrub_properties (object);

  /* Feature flags require a separate query; set safe defaults here. */
  {
    const gchar *const empty_strv[] = { NULL };
    udisks_zfspool_set_feature_flags (iface, empty_strv);
  }

  g_dbus_interface_skeleton_flush (G_DBUS_INTERFACE_SKELETON (iface));
}

/**
 * udisks_linux_pool_object_zfs_destroy:
 * @object: A #UDisksLinuxPoolObjectZFS.
 *
 * Destroys the pool object by removing its D-Bus interface.
 */
void
udisks_linux_pool_object_zfs_destroy (UDisksLinuxPoolObjectZFS *object)
{
  g_return_if_fail (UDISKS_IS_LINUX_POOL_OBJECT_ZFS (object));

  /* Stop the scrub poll timer to prevent use-after-free */
  if (object->scrub_poll_id != 0)
    {
      g_source_remove (object->scrub_poll_id);
      object->scrub_poll_id = 0;
    }

  if (object->iface_zfs_pool != NULL)
    {
      g_dbus_object_skeleton_remove_interface (G_DBUS_OBJECT_SKELETON (object),
                                               G_DBUS_INTERFACE_SKELETON (object->iface_zfs_pool));
    }
}
