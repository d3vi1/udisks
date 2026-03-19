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

#include <src/udisksdaemon.h>
#include <src/udisksdaemonutil.h>
#include <src/udiskslinuxblockobject.h>
#include <src/udiskslinuxdevice.h>
#include <src/udiskslogging.h>
#include <src/udisksmodule.h>
#include <src/udisksmoduleobject.h>

#include "udiskszfstypes.h"
#include "udiskslinuxmodulezfs.h"
#include "udiskslinuxblockzfs.h"
#include "udiskslinuxpoolobjectzfs.h"

/**
 * SECTION:udiskslinuxblockzfs
 * @title: UDisksLinuxBlockZFS
 * @short_description: Linux implementation of #UDisksBlockZFS
 *
 * This type provides an implementation of #UDisksBlockZFS interface
 * on Linux.
 */

/**
 * UDisksLinuxBlockZFS:
 *
 * The #UDisksLinuxBlockZFS structure contains only private data and
 * should be only accessed using provided API.
 */
struct _UDisksLinuxBlockZFS {
  UDisksBlockZFSSkeleton parent_instance;

  UDisksLinuxModuleZFS *module;
  UDisksLinuxBlockObject *block_object;
};

struct _UDisksLinuxBlockZFSClass {
  UDisksBlockZFSSkeletonClass parent_class;
};

enum
{
  PROP_0,
  PROP_MODULE,
  PROP_BLOCK_OBJECT,
  N_PROPERTIES
};

static void udisks_linux_block_zfs_module_object_iface_init (UDisksModuleObjectIface *iface);

G_DEFINE_TYPE_WITH_CODE (UDisksLinuxBlockZFS, udisks_linux_block_zfs, UDISKS_TYPE_BLOCK_ZFS_SKELETON,
                         G_IMPLEMENT_INTERFACE (UDISKS_TYPE_BLOCK_ZFS, NULL)
                         G_IMPLEMENT_INTERFACE (UDISKS_TYPE_MODULE_OBJECT, udisks_linux_block_zfs_module_object_iface_init));

static void
udisks_linux_block_zfs_get_property (GObject    *object,
                                     guint       property_id,
                                     GValue     *value,
                                     GParamSpec *pspec)
{
  UDisksLinuxBlockZFS *block_zfs = UDISKS_LINUX_BLOCK_ZFS (object);

  switch (property_id)
    {
    case PROP_MODULE:
      g_value_set_object (value, UDISKS_MODULE (udisks_linux_block_zfs_get_module (block_zfs)));
      break;

    case PROP_BLOCK_OBJECT:
      g_value_set_object (value, block_zfs->block_object);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
    }
}

static void
udisks_linux_block_zfs_set_property (GObject      *object,
                                     guint         property_id,
                                     const GValue *value,
                                     GParamSpec   *pspec)
{
  UDisksLinuxBlockZFS *block_zfs = UDISKS_LINUX_BLOCK_ZFS (object);

  switch (property_id)
    {
    case PROP_MODULE:
      g_assert (block_zfs->module == NULL);
      block_zfs->module = UDISKS_LINUX_MODULE_ZFS (g_value_dup_object (value));
      break;

    case PROP_BLOCK_OBJECT:
      g_assert (block_zfs->block_object == NULL);
      /* we don't take reference to block_object */
      block_zfs->block_object = g_value_get_object (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
    }
}

static void
udisks_linux_block_zfs_finalize (GObject *object)
{
  UDisksLinuxBlockZFS *block_zfs = UDISKS_LINUX_BLOCK_ZFS (object);

  /* we don't take reference to block_object */
  g_object_unref (block_zfs->module);

  if (G_OBJECT_CLASS (udisks_linux_block_zfs_parent_class))
    G_OBJECT_CLASS (udisks_linux_block_zfs_parent_class)->finalize (object);
}

static void
udisks_linux_block_zfs_class_init (UDisksLinuxBlockZFSClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->get_property = udisks_linux_block_zfs_get_property;
  gobject_class->set_property = udisks_linux_block_zfs_set_property;
  gobject_class->finalize = udisks_linux_block_zfs_finalize;

  /**
   * UDisksLinuxBlockZFS:module:
   *
   * The #UDisksModule for the object.
   */
  g_object_class_install_property (gobject_class,
                                   PROP_MODULE,
                                   g_param_spec_object ("module",
                                                        "Module",
                                                        "The module for the object",
                                                        UDISKS_TYPE_MODULE,
                                                        G_PARAM_READABLE |
                                                        G_PARAM_WRITABLE |
                                                        G_PARAM_CONSTRUCT_ONLY |
                                                        G_PARAM_STATIC_STRINGS));
  /**
   * UDisksLinuxBlockZFS:blockobject:
   *
   * The #UDisksLinuxBlockObject for the object.
   */
  g_object_class_install_property (gobject_class,
                                   PROP_BLOCK_OBJECT,
                                   g_param_spec_object ("blockobject",
                                                        "Block object",
                                                        "The block object for the interface",
                                                        UDISKS_TYPE_LINUX_BLOCK_OBJECT,
                                                        G_PARAM_READABLE |
                                                        G_PARAM_WRITABLE |
                                                        G_PARAM_CONSTRUCT_ONLY |
                                                        G_PARAM_STATIC_STRINGS));
}

static void
udisks_linux_block_zfs_init (UDisksLinuxBlockZFS *block_zfs)
{
  g_dbus_interface_skeleton_set_flags (G_DBUS_INTERFACE_SKELETON (block_zfs),
                                       G_DBUS_INTERFACE_SKELETON_FLAGS_HANDLE_METHOD_INVOCATIONS_IN_THREAD);
}

/**
 * udisks_linux_block_zfs_new:
 * @module: A #UDisksLinuxModuleZFS.
 * @block_object: A #UDisksLinuxBlockObject.
 *
 * Creates a new #UDisksLinuxBlockZFS instance.
 *
 * Returns: A new #UDisksLinuxBlockZFS. Free with g_object_unref().
 */
UDisksLinuxBlockZFS *
udisks_linux_block_zfs_new (UDisksLinuxModuleZFS  *module,
                            UDisksLinuxBlockObject *block_object)
{
  g_return_val_if_fail (UDISKS_IS_LINUX_MODULE_ZFS (module), NULL);
  g_return_val_if_fail (UDISKS_IS_LINUX_BLOCK_OBJECT (block_object), NULL);
  return UDISKS_LINUX_BLOCK_ZFS (g_object_new (UDISKS_TYPE_LINUX_BLOCK_ZFS,
                                               "module", UDISKS_MODULE (module),
                                               "blockobject", block_object,
                                               NULL));
}

/**
 * udisks_linux_block_zfs_get_module:
 * @block_zfs: A #UDisksLinuxBlockZFS.
 *
 * Gets the module used by @block_zfs.
 *
 * Returns: A #UDisksLinuxModuleZFS. Do not free, the object is owned by @block_zfs.
 */
UDisksLinuxModuleZFS *
udisks_linux_block_zfs_get_module (UDisksLinuxBlockZFS *block_zfs)
{
  g_return_val_if_fail (UDISKS_IS_LINUX_BLOCK_ZFS (block_zfs), NULL);
  return block_zfs->module;
}

/**
 * udisks_linux_block_zfs_update:
 * @block_zfs: A #UDisksLinuxBlockZFS.
 * @object: The enclosing #UDisksLinuxBlockObject instance.
 *
 * Updates the interface.
 */
static void
udisks_linux_block_zfs_update (UDisksLinuxBlockZFS   *block_zfs,
                               UDisksLinuxBlockObject *object)
{
  UDisksBlockZFS *iface = UDISKS_BLOCK_ZFS (block_zfs);
  UDisksLinuxDevice *device;
  const gchar *label;
  UDisksLinuxPoolObjectZFS *pool_object;

  device = udisks_linux_block_object_get_device (object);

  /* ZFS pool members have the pool name in ID_FS_LABEL */
  label = g_udev_device_get_property (device->udev_device, "ID_FS_LABEL");

  if (label != NULL)
    {
      pool_object = udisks_linux_module_zfs_find_pool_object (block_zfs->module, label);
      if (pool_object != NULL)
        {
          const gchar *pool_path;

          pool_path = g_dbus_object_get_object_path (G_DBUS_OBJECT (pool_object));
          udisks_block_zfs_set_pool (iface, pool_path);
        }
      else
        {
          udisks_block_zfs_set_pool (iface, "/");
        }
    }
  else
    {
      udisks_block_zfs_set_pool (iface, "/");
    }

  g_dbus_interface_skeleton_flush (G_DBUS_INTERFACE_SKELETON (iface));
  g_object_unref (device);
}

/* -------------------------------------------------------------------------- */

static gboolean
udisks_linux_block_zfs_module_object_process_uevent (UDisksModuleObject *module_object,
                                                     UDisksUeventAction  action,
                                                     UDisksLinuxDevice  *device,
                                                     gboolean           *keep)
{
  UDisksLinuxBlockZFS *block_zfs = UDISKS_LINUX_BLOCK_ZFS (module_object);
  const gchar *fs_type = NULL;

  g_return_val_if_fail (UDISKS_IS_LINUX_BLOCK_ZFS (module_object), FALSE);

  if (device == NULL)
    return FALSE;

  /* Check filesystem type from udev property. */
  fs_type = g_udev_device_get_property (device->udev_device, "ID_FS_TYPE");
  *keep = g_strcmp0 (fs_type, "zfs_member") == 0;
  if (*keep)
    {
      udisks_linux_block_zfs_update (block_zfs, block_zfs->block_object);
    }

  return TRUE;
}

static void
udisks_linux_block_zfs_module_object_iface_init (UDisksModuleObjectIface *iface)
{
  iface->process_uevent = udisks_linux_block_zfs_module_object_process_uevent;
}
