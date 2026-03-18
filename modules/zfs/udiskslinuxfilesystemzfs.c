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

#include <glib/gi18n.h>
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
#include "udiskslinuxfilesystemzfs.h"
#include "udiskslinuxpoolobjectzfs.h"

/**
 * SECTION:udiskslinuxfilesystemzfs
 * @title: UDisksLinuxFilesystemZFS
 * @short_description: Linux implementation of #UDisksFilesystemZFS
 *
 * This type provides an implementation of #UDisksFilesystemZFS interface
 * on Linux.
 */

/**
 * UDisksLinuxFilesystemZFS:
 *
 * The #UDisksLinuxFilesystemZFS structure contains only private data and
 * should be only accessed using provided API.
 */
struct _UDisksLinuxFilesystemZFS {
  UDisksFilesystemZFSSkeleton parent_instance;

  UDisksLinuxModuleZFS *module;
  UDisksLinuxBlockObject *block_object;
};

struct _UDisksLinuxFilesystemZFSClass {
  UDisksFilesystemZFSSkeletonClass parent_class;
};

enum
{
  PROP_0,
  PROP_MODULE,
  PROP_BLOCK_OBJECT,
  N_PROPERTIES
};

static void udisks_linux_filesystem_zfs_module_object_iface_init (UDisksModuleObjectIface *iface);

G_DEFINE_TYPE_WITH_CODE (UDisksLinuxFilesystemZFS, udisks_linux_filesystem_zfs, UDISKS_TYPE_FILESYSTEM_ZFS_SKELETON,
                         G_IMPLEMENT_INTERFACE (UDISKS_TYPE_FILESYSTEM_ZFS, NULL)
                         G_IMPLEMENT_INTERFACE (UDISKS_TYPE_MODULE_OBJECT, udisks_linux_filesystem_zfs_module_object_iface_init));

static void
udisks_linux_filesystem_zfs_get_property (GObject    *object,
                                          guint       property_id,
                                          GValue     *value,
                                          GParamSpec *pspec)
{
  UDisksLinuxFilesystemZFS *fs_zfs = UDISKS_LINUX_FILESYSTEM_ZFS (object);

  switch (property_id)
    {
    case PROP_MODULE:
      g_value_set_object (value, UDISKS_MODULE (udisks_linux_filesystem_zfs_get_module (fs_zfs)));
      break;

    case PROP_BLOCK_OBJECT:
      g_value_set_object (value, fs_zfs->block_object);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
    }
}

static void
udisks_linux_filesystem_zfs_set_property (GObject      *object,
                                          guint         property_id,
                                          const GValue *value,
                                          GParamSpec   *pspec)
{
  UDisksLinuxFilesystemZFS *fs_zfs = UDISKS_LINUX_FILESYSTEM_ZFS (object);

  switch (property_id)
    {
    case PROP_MODULE:
      g_assert (fs_zfs->module == NULL);
      fs_zfs->module = UDISKS_LINUX_MODULE_ZFS (g_value_dup_object (value));
      break;

    case PROP_BLOCK_OBJECT:
      g_assert (fs_zfs->block_object == NULL);
      /* we don't take reference to block_object */
      fs_zfs->block_object = g_value_get_object (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
    }
}

static void
udisks_linux_filesystem_zfs_finalize (GObject *object)
{
  UDisksLinuxFilesystemZFS *fs_zfs = UDISKS_LINUX_FILESYSTEM_ZFS (object);

  /* we don't take reference to block_object */
  g_object_unref (fs_zfs->module);

  if (G_OBJECT_CLASS (udisks_linux_filesystem_zfs_parent_class))
    G_OBJECT_CLASS (udisks_linux_filesystem_zfs_parent_class)->finalize (object);
}

static void
udisks_linux_filesystem_zfs_class_init (UDisksLinuxFilesystemZFSClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->get_property = udisks_linux_filesystem_zfs_get_property;
  gobject_class->set_property = udisks_linux_filesystem_zfs_set_property;
  gobject_class->finalize = udisks_linux_filesystem_zfs_finalize;

  /**
   * UDisksLinuxFilesystemZFS:module:
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
   * UDisksLinuxFilesystemZFS:blockobject:
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
udisks_linux_filesystem_zfs_init (UDisksLinuxFilesystemZFS *fs_zfs)
{
  g_dbus_interface_skeleton_set_flags (G_DBUS_INTERFACE_SKELETON (fs_zfs),
                                       G_DBUS_INTERFACE_SKELETON_FLAGS_HANDLE_METHOD_INVOCATIONS_IN_THREAD);
}

/**
 * udisks_linux_filesystem_zfs_new:
 * @module: A #UDisksLinuxModuleZFS.
 * @block_object: A #UDisksLinuxBlockObject.
 *
 * Creates a new #UDisksLinuxFilesystemZFS instance.
 *
 * Returns: A new #UDisksLinuxFilesystemZFS. Free with g_object_unref().
 */
UDisksLinuxFilesystemZFS *
udisks_linux_filesystem_zfs_new (UDisksLinuxModuleZFS  *module,
                                 UDisksLinuxBlockObject *block_object)
{
  g_return_val_if_fail (UDISKS_IS_LINUX_MODULE_ZFS (module), NULL);
  g_return_val_if_fail (UDISKS_IS_LINUX_BLOCK_OBJECT (block_object), NULL);
  return UDISKS_LINUX_FILESYSTEM_ZFS (g_object_new (UDISKS_TYPE_LINUX_FILESYSTEM_ZFS,
                                                    "module", UDISKS_MODULE (module),
                                                    "blockobject", block_object,
                                                    NULL));
}

/**
 * udisks_linux_filesystem_zfs_get_module:
 * @fs_zfs: A #UDisksLinuxFilesystemZFS.
 *
 * Gets the module used by @fs_zfs.
 *
 * Returns: A #UDisksLinuxModuleZFS. Do not free, the object is owned by @fs_zfs.
 */
UDisksLinuxModuleZFS *
udisks_linux_filesystem_zfs_get_module (UDisksLinuxFilesystemZFS *fs_zfs)
{
  g_return_val_if_fail (UDISKS_IS_LINUX_FILESYSTEM_ZFS (fs_zfs), NULL);
  return fs_zfs->module;
}

/**
 * udisks_linux_filesystem_zfs_update:
 * @fs_zfs: A #UDisksLinuxFilesystemZFS.
 * @object: The enclosing #UDisksLinuxBlockObject instance.
 *
 * Updates the interface properties from the block device.
 *
 * For zvol block devices, the device path is typically /dev/zd* and the
 * DM_NAME or device symlinks reveal the pool and dataset name.
 * We also use ID_FS_LABEL and ID_FS_UUID_SUB udev properties when available.
 */
static void
udisks_linux_filesystem_zfs_update (UDisksLinuxFilesystemZFS *fs_zfs,
                                    UDisksLinuxBlockObject   *object)
{
  UDisksFilesystemZFS *iface = UDISKS_FILESYSTEM_ZFS (fs_zfs);
  UDisksLinuxDevice *device;
  const gchar *dev_file;
  const gchar *pool_name = NULL;
  const gchar *dataset_name = NULL;
  UDisksLinuxPoolObjectZFS *pool_object;

  device = udisks_linux_block_object_get_device (object);
  dev_file = g_udev_device_get_device_file (device->udev_device);

  /* Try to extract the pool and dataset name.
   *
   * For zvol block devices, udev typically sets DM_NAME to "pool/volname"
   * or the device appears under /dev/zvol/pool/volname.
   * We also check ID_FS_LABEL which on some setups contains the pool name.
   */

  /* Check DM_NAME first (e.g. "tank-zvol0" or "tank/zvol0") */
  pool_name = g_udev_device_get_property (device->udev_device, "DM_NAME");
  if (pool_name != NULL && strchr (pool_name, '/') != NULL)
    {
      /* DM_NAME has format "pool/dataset" */
      gchar **parts = g_strsplit (pool_name, "/", 2);
      udisks_filesystem_zfs_set_pool_name (iface, parts[0]);
      udisks_filesystem_zfs_set_dataset_name (iface, pool_name);
      pool_name = parts[0];

      pool_object = udisks_linux_module_zfs_find_pool_object (fs_zfs->module, pool_name);
      if (pool_object != NULL)
        {
          const gchar *pool_path;

          pool_path = g_dbus_object_get_object_path (G_DBUS_OBJECT (pool_object));
          udisks_filesystem_zfs_set_pool (iface, pool_path);
        }
      else
        {
          udisks_filesystem_zfs_set_pool (iface, "/");
        }

      g_strfreev (parts);
    }
  else
    {
      /* Try /dev/zvol symlink path: the device path encodes pool/dataset */
      const gchar *zvol_prefix = "/dev/zvol/";

      if (dev_file != NULL && g_str_has_prefix (dev_file, zvol_prefix))
        {
          const gchar *zvol_path = dev_file + strlen (zvol_prefix);
          const gchar *slash;

          slash = strchr (zvol_path, '/');
          if (slash != NULL)
            {
              gchar *pname = g_strndup (zvol_path, slash - zvol_path);
              udisks_filesystem_zfs_set_pool_name (iface, pname);
              udisks_filesystem_zfs_set_dataset_name (iface, zvol_path);

              pool_object = udisks_linux_module_zfs_find_pool_object (fs_zfs->module, pname);
              if (pool_object != NULL)
                {
                  const gchar *pool_path;

                  pool_path = g_dbus_object_get_object_path (G_DBUS_OBJECT (pool_object));
                  udisks_filesystem_zfs_set_pool (iface, pool_path);
                }
              else
                {
                  udisks_filesystem_zfs_set_pool (iface, "/");
                }

              g_free (pname);
            }
          else
            {
              /* No slash: the path is just the pool name */
              udisks_filesystem_zfs_set_pool_name (iface, zvol_path);
              udisks_filesystem_zfs_set_dataset_name (iface, zvol_path);

              pool_object = udisks_linux_module_zfs_find_pool_object (fs_zfs->module, zvol_path);
              if (pool_object != NULL)
                {
                  const gchar *pool_path;

                  pool_path = g_dbus_object_get_object_path (G_DBUS_OBJECT (pool_object));
                  udisks_filesystem_zfs_set_pool (iface, pool_path);
                }
              else
                {
                  udisks_filesystem_zfs_set_pool (iface, "/");
                }
            }
        }
      else
        {
          /* Fallback: try ID_FS_LABEL for the pool name */
          dataset_name = g_udev_device_get_property (device->udev_device, "ID_FS_LABEL");
          if (dataset_name != NULL)
            {
              const gchar *slash = strchr (dataset_name, '/');
              if (slash != NULL)
                {
                  gchar *pname = g_strndup (dataset_name, slash - dataset_name);
                  udisks_filesystem_zfs_set_pool_name (iface, pname);
                  udisks_filesystem_zfs_set_dataset_name (iface, dataset_name);

                  pool_object = udisks_linux_module_zfs_find_pool_object (fs_zfs->module, pname);
                  if (pool_object != NULL)
                    {
                      const gchar *pool_path;

                      pool_path = g_dbus_object_get_object_path (G_DBUS_OBJECT (pool_object));
                      udisks_filesystem_zfs_set_pool (iface, pool_path);
                    }
                  else
                    {
                      udisks_filesystem_zfs_set_pool (iface, "/");
                    }

                  g_free (pname);
                }
              else
                {
                  udisks_filesystem_zfs_set_pool_name (iface, dataset_name);
                  udisks_filesystem_zfs_set_dataset_name (iface, dataset_name);

                  pool_object = udisks_linux_module_zfs_find_pool_object (fs_zfs->module, dataset_name);
                  if (pool_object != NULL)
                    {
                      const gchar *pool_path;

                      pool_path = g_dbus_object_get_object_path (G_DBUS_OBJECT (pool_object));
                      udisks_filesystem_zfs_set_pool (iface, pool_path);
                    }
                  else
                    {
                      udisks_filesystem_zfs_set_pool (iface, "/");
                    }
                }
            }
          else
            {
              udisks_filesystem_zfs_set_pool (iface, "/");
              udisks_filesystem_zfs_set_pool_name (iface, "");
              udisks_filesystem_zfs_set_dataset_name (iface, "");
            }
        }
    }

  g_dbus_interface_skeleton_flush (G_DBUS_INTERFACE_SKELETON (iface));
  g_object_unref (device);
}

/* -------------------------------------------------------------------------- */

/**
 * is_zvol_device:
 * @device: A #UDisksLinuxDevice.
 *
 * Checks whether the device is a ZFS zvol block device.
 *
 * Zvol devices are identified by:
 * - Device path starting with /dev/zd (zvol device nodes)
 * - DM_NAME containing a slash (pool/dataset format)
 * - Symlinks under /dev/zvol/
 *
 * Returns: %TRUE if the device is a zvol.
 */
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

static gboolean
udisks_linux_filesystem_zfs_module_object_process_uevent (UDisksModuleObject *module_object,
                                                          UDisksUeventAction  action,
                                                          UDisksLinuxDevice  *device,
                                                          gboolean           *keep)
{
  UDisksLinuxFilesystemZFS *fs_zfs = UDISKS_LINUX_FILESYSTEM_ZFS (module_object);

  g_return_val_if_fail (UDISKS_IS_LINUX_FILESYSTEM_ZFS (module_object), FALSE);

  if (device == NULL)
    return FALSE;

  /* Keep the interface as long as the device is still a zvol */
  *keep = is_zvol_device (device);
  if (*keep)
    {
      udisks_linux_filesystem_zfs_update (fs_zfs, fs_zfs->block_object);
    }

  return TRUE;
}

static void
udisks_linux_filesystem_zfs_module_object_iface_init (UDisksModuleObjectIface *iface)
{
  iface->process_uevent = udisks_linux_filesystem_zfs_module_object_process_uevent;
}
