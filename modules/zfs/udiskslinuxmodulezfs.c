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

#include "udiskslinuxmodulezfs.h"
#include "udiskslinuxmanagerzfs.h"

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
}

static void
udisks_linux_module_zfs_constructed (GObject *object)
{
  if (G_OBJECT_CLASS (udisks_linux_module_zfs_parent_class)->constructed)
    G_OBJECT_CLASS (udisks_linux_module_zfs_parent_class)->constructed (object);
}

static void
udisks_linux_module_zfs_finalize (GObject *object)
{
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
udisks_linux_module_zfs_class_init (UDisksLinuxModuleZFSClass *klass)
{
  GObjectClass *gobject_class;
  UDisksModuleClass *module_class;

  gobject_class = G_OBJECT_CLASS (klass);
  gobject_class->constructed = udisks_linux_module_zfs_constructed;
  gobject_class->finalize = udisks_linux_module_zfs_finalize;

  module_class = UDISKS_MODULE_CLASS (klass);
  module_class->new_manager = udisks_linux_module_zfs_new_manager;
}
