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

#include <src/udiskslogging.h>
#include <src/udisksdaemon.h>
#include <src/udisksdaemonutil.h>

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

  /* Scrub info and feature flags require separate queries;
   * set safe defaults here. They will be populated by a full
   * status update later. */
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

  if (object->iface_zfs_pool != NULL)
    {
      g_dbus_object_skeleton_remove_interface (G_DBUS_OBJECT_SKELETON (object),
                                               G_DBUS_INTERFACE_SKELETON (object->iface_zfs_pool));
    }
}
