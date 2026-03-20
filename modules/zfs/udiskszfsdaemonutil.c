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

#include <string.h>

#include <udisks/udiskserror.h>

#include "udiskszfsdaemonutil.h"

/**
 * SECTION:udiskszfsdaemonutil
 * @title: ZFS Daemon Utilities
 * @short_description: Shared utilities for the ZFS module
 *
 * Utility functions shared across ZFS module source files.  This
 * includes:
 *
 *  - **Block-object-path resolvers** that convert D-Bus object paths
 *    into device paths (e.g. `/dev/sda`).
 *  - **Property allowlist enforcement** for ZFS pool and
 * dataset property modifications.  Properties are divided into two tiers:
 *
 *  - **Safe properties** require only `manage-zfs` polkit authorization.
 *  - **Security-sensitive properties** require the stronger
 *    `manage-zfs-destroy` authorization because they can alter mount
 *    behaviour, enable setuid execution, or change sharing/ACL semantics.
 *
 * User properties (those containing a colon, e.g. `user:backup-tag`) are
 * always treated as safe.
 */

/* Properties that are safe to set with manage-zfs authorization. */
static const gchar * const safe_properties[] =
{
  "compression",
  "atime",
  "relatime",
  "recordsize",
  "quota",
  "reservation",
  "refquota",
  "refreservation",
  "copies",
  "logbias",
  "primarycache",
  "secondarycache",
  "snapdir",
  "sync",
  "dedup",
  "checksum",
  "redundant_metadata",
  "dnodesize",
  "special_small_blocks",
  /* Pool-level properties */
  "autoexpand",
  "autoreplace",
  "comment",
  "delegation",
  "failmode",
  "listsnapshots",
  "multihost",
  NULL
};

/* Properties that are security-sensitive and require manage-zfs-destroy. */
static const gchar * const sensitive_properties[] =
{
  "mountpoint",
  "exec",
  "setuid",
  "devices",
  "sharenfs",
  "sharesmb",
  "canmount",
  "overlay",
  "acltype",
  "xattr",
  "readonly",
  NULL
};

/* Properties that only apply to pools, not datasets.  These overlap
 * with safe_properties[] above (they are allowed on pool objects), but
 * must be rejected when a caller tries to set them via
 * SetDatasetProperty, since ZFS would return a confusing error or
 * silently ignore them. */
static const gchar * const pool_only_properties[] =
{
  "autoexpand",
  "autoreplace",
  "comment",
  "delegation",
  "failmode",
  "listsnapshots",
  "multihost",
  NULL
};

/**
 * udisks_zfs_property_is_pool_only:
 * @property: A ZFS property name.
 *
 * Checks whether @property is a pool-only property that should not be
 * set on datasets.
 *
 * Returns: %TRUE if @property is pool-only.
 */
gboolean
udisks_zfs_property_is_pool_only (const gchar *property)
{
  guint i;

  g_return_val_if_fail (property != NULL, FALSE);

  for (i = 0; pool_only_properties[i] != NULL; i++)
    {
      if (g_strcmp0 (property, pool_only_properties[i]) == 0)
        return TRUE;
    }

  return FALSE;
}

/**
 * is_user_property:
 * @property: A ZFS property name.
 *
 * User properties always contain a colon (e.g. "user:tag", "com.example:key").
 *
 * Returns: %TRUE if @property is a user property.
 */
static gboolean
is_user_property (const gchar *property)
{
  return (strchr (property, ':') != NULL);
}

/**
 * udisks_zfs_property_is_safe:
 * @property: A ZFS property name.
 * @error: (nullable): Return location for a #GError or %NULL.
 *
 * Checks whether @property is in the "safe" tier of the allowlist.
 * Safe properties only require `manage-zfs` polkit authorization.
 *
 * User properties (containing a colon) are always considered safe.
 *
 * Returns: %TRUE if @property is a safe property, %FALSE if it is
 *   security-sensitive or not in any allowlist.  When the property is
 *   security-sensitive (but still allowed), %FALSE is returned and
 *   @error is not set.  When the property is not in any allowlist,
 *   %FALSE is returned and @error is set.
 */
gboolean
udisks_zfs_property_is_safe (const gchar  *property,
                              GError      **error)
{
  guint i;

  g_return_val_if_fail (property != NULL, FALSE);

  /* User properties (containing ':') are always safe */
  if (is_user_property (property))
    return TRUE;

  for (i = 0; safe_properties[i] != NULL; i++)
    {
      if (g_strcmp0 (property, safe_properties[i]) == 0)
        return TRUE;
    }

  /* Check if it is at least in the sensitive list — if so it is allowed
   * but not safe (caller should use manage-zfs-destroy). */
  for (i = 0; sensitive_properties[i] != NULL; i++)
    {
      if (g_strcmp0 (property, sensitive_properties[i]) == 0)
        return FALSE;
    }

  /* Not in any allowlist */
  if (error != NULL)
    g_set_error (error,
                 UDISKS_ERROR,
                 UDISKS_ERROR_OPTION_NOT_PERMITTED,
                 "Property '%s' is not in the ZFS property allowlist",
                 property);

  return FALSE;
}

/**
 * udisks_zfs_property_is_allowed:
 * @property: A ZFS property name.
 * @error: (nullable): Return location for a #GError or %NULL.
 *
 * Checks whether @property is permitted at all (either safe or
 * security-sensitive).
 *
 * Returns: %TRUE if @property is in the allowlist (safe or sensitive),
 *   %FALSE if it is unknown/disallowed (with @error set).
 */
gboolean
udisks_zfs_property_is_allowed (const gchar  *property,
                                 GError      **error)
{
  guint i;

  g_return_val_if_fail (property != NULL, FALSE);

  /* User properties are always allowed */
  if (is_user_property (property))
    return TRUE;

  for (i = 0; safe_properties[i] != NULL; i++)
    {
      if (g_strcmp0 (property, safe_properties[i]) == 0)
        return TRUE;
    }

  for (i = 0; sensitive_properties[i] != NULL; i++)
    {
      if (g_strcmp0 (property, sensitive_properties[i]) == 0)
        return TRUE;
    }

  g_set_error (error,
               UDISKS_ERROR,
               UDISKS_ERROR_OPTION_NOT_PERMITTED,
               "Property '%s' is not in the ZFS property allowlist",
               property);

  return FALSE;
}

/* Properties that are query-sensitive — reading them reveals
 * encryption configuration details, so reading requires manage-zfs
 * (not just query) authorization. */
static const gchar * const query_sensitive_properties[] =
{
  "keylocation",
  "cachefile",
  "keystatus",
  "keyformat",
  "encryptionroot",
  NULL
};

/**
 * udisks_zfs_property_is_query_sensitive:
 * @property: A ZFS property name.
 *
 * Checks whether reading @property should require elevated
 * (manage-zfs) authorization rather than the default query tier.
 * Properties in this list reveal sensitive encryption metadata.
 *
 * Returns: %TRUE if @property is query-sensitive.
 */
gboolean
udisks_zfs_property_is_query_sensitive (const gchar *property)
{
  guint i;

  g_return_val_if_fail (property != NULL, FALSE);

  for (i = 0; query_sensitive_properties[i] != NULL; i++)
    {
      if (g_strcmp0 (property, query_sensitive_properties[i]) == 0)
        return TRUE;
    }

  return FALSE;
}

/**
 * udisks_zfs_validate_name_in_pool:
 * @pool_name: The pool name.
 * @name: The dataset, snapshot, or bookmark name to validate.
 * @error: (nullable): Return location for a #GError or %NULL.
 *
 * Validates that @name belongs to @pool_name.  A name belongs to a pool
 * if it is exactly the pool name or starts with the pool name followed
 * by a '/' separator (for datasets) or contains '@' or '#' after the
 * pool prefix (for snapshots/bookmarks).
 *
 * This prevents a caller from using a pool's D-Bus object to operate
 * on datasets belonging to a different pool — a privilege-escalation
 * vector.
 *
 * Returns: %TRUE if @name belongs to @pool_name, %FALSE otherwise
 *   (with @error set).
 */
gboolean
udisks_zfs_validate_name_in_pool (const gchar  *pool_name,
                                   const gchar  *name,
                                   GError      **error)
{
  gsize plen;

  if (name == NULL || pool_name == NULL)
    {
      g_set_error_literal (error,
                           UDISKS_ERROR,
                           UDISKS_ERROR_FAILED,
                           "Pool name and dataset name must not be NULL");
      return FALSE;
    }

  plen = strlen (pool_name);

  /* Name must be exactly the pool name, or pool_name followed by '/',
   * '@', or '#' (the latter two for snapshots and bookmarks). */
  if (g_strcmp0 (name, pool_name) == 0)
    return TRUE;

  if (strlen (name) > plen &&
      strncmp (name, pool_name, plen) == 0 &&
      (name[plen] == '/' || name[plen] == '@' || name[plen] == '#'))
    return TRUE;

  g_set_error (error,
               UDISKS_ERROR,
               UDISKS_ERROR_FAILED,
               "Dataset name '%s' does not belong to pool '%s'",
               name, pool_name);

  return FALSE;
}

/* ---------------------------------------------------------------------------------------------------- */
/*  Block-object-path resolvers                                                                          */
/* ---------------------------------------------------------------------------------------------------- */

/**
 * udisks_zfs_resolve_blocks_to_device_paths:
 * @daemon: A #UDisksDaemon.
 * @arg_blocks: NULL-terminated array of D-Bus object paths.
 * @invocation: The method invocation (for error reporting).
 * @out_n_devices: (out) (optional): Number of resolved devices, or %NULL.
 *
 * Resolves an array of D-Bus object paths to device path strings.
 *
 * Returns: (transfer full): A NULL-terminated array of device path strings,
 *   or %NULL on error (in which case an error has been returned on @invocation).
 *   Free with g_strfreev().
 */
gchar **
udisks_zfs_resolve_blocks_to_device_paths (UDisksDaemon          *daemon,
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

/**
 * udisks_zfs_resolve_block_to_device_path:
 * @daemon: A #UDisksDaemon.
 * @object_path: A single D-Bus object path.
 * @invocation: The method invocation (for error reporting).
 *
 * Resolves a single D-Bus object path to a device path string.
 * This is the single-object convenience wrapper around
 * udisks_zfs_resolve_blocks_to_device_paths().
 *
 * Returns: (transfer full): The device path string (e.g. "/dev/sda"),
 *   or %NULL on error (in which case an error has been returned on @invocation).
 *   Free with g_free().
 */
gchar *
udisks_zfs_resolve_block_to_device_path (UDisksDaemon          *daemon,
                                          const gchar           *object_path,
                                          GDBusMethodInvocation *invocation)
{
  UDisksObject *obj = NULL;
  UDisksBlock *block = NULL;
  gchar *device_path = NULL;

  obj = udisks_daemon_find_object (daemon, object_path);
  if (obj == NULL)
    {
      g_dbus_method_invocation_return_error (invocation,
                                             UDISKS_ERROR,
                                             UDISKS_ERROR_FAILED,
                                             "Invalid object path %s",
                                             object_path);
      return NULL;
    }

  block = udisks_object_get_block (obj);
  if (block == NULL)
    {
      g_dbus_method_invocation_return_error (invocation,
                                             UDISKS_ERROR,
                                             UDISKS_ERROR_FAILED,
                                             "Object path %s is not a block device",
                                             object_path);
      g_object_unref (obj);
      return NULL;
    }

  device_path = udisks_block_dup_device (block);
  g_object_unref (block);
  g_object_unref (obj);

  return device_path;
}
