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

#include <src/udisksdaemon.h>
#include <src/udisksdaemonutil.h>
#include <src/udiskslogging.h>

#include "udiskslinuxmanagerzfs.h"
#include "udiskslinuxmodulezfs.h"

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

static gboolean
handle_pool_create (UDisksManagerZFS      *manager,
                    GDBusMethodInvocation *invocation,
                    const gchar           *arg_name,
                    const gchar *const    *arg_blocks,
                    const gchar           *arg_vdev_type,
                    GVariant              *arg_options)
{
  g_dbus_method_invocation_return_error (invocation,
                                         G_DBUS_ERROR,
                                         G_DBUS_ERROR_NOT_SUPPORTED,
                                         "Method not yet implemented");
  return TRUE;
}

static gboolean
handle_pool_import (UDisksManagerZFS      *manager,
                    GDBusMethodInvocation *invocation,
                    const gchar           *arg_name_or_guid,
                    GVariant              *arg_options)
{
  g_dbus_method_invocation_return_error (invocation,
                                         G_DBUS_ERROR,
                                         G_DBUS_ERROR_NOT_SUPPORTED,
                                         "Method not yet implemented");
  return TRUE;
}

static gboolean
handle_pool_import_all (UDisksManagerZFS      *manager,
                        GDBusMethodInvocation *invocation,
                        GVariant              *arg_options)
{
  g_dbus_method_invocation_return_error (invocation,
                                         G_DBUS_ERROR,
                                         G_DBUS_ERROR_NOT_SUPPORTED,
                                         "Method not yet implemented");
  return TRUE;
}

static void
udisks_linux_manager_zfs_iface_init (UDisksManagerZFSIface *iface)
{
  iface->handle_pool_create = handle_pool_create;
  iface->handle_pool_import = handle_pool_import;
  iface->handle_pool_import_all = handle_pool_import_all;
}
