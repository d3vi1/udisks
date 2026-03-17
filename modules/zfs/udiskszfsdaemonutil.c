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
 * @short_description: Property allowlist enforcement for ZFS operations
 *
 * Utility functions that enforce a property allowlist for ZFS pool and
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
  NULL
};

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
