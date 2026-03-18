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

#ifndef __UDISKS_LINUX_POOL_OBJECT_ZFS_H__
#define __UDISKS_LINUX_POOL_OBJECT_ZFS_H__

#include <src/udisksdaemontypes.h>
#include "udiskszfstypes.h"
#include "udiskslinuxmodulezfs.h"

#include <blockdev/zfs.h>

G_BEGIN_DECLS

#define UDISKS_TYPE_LINUX_POOL_OBJECT_ZFS  (udisks_linux_pool_object_zfs_get_type ())
#define UDISKS_LINUX_POOL_OBJECT_ZFS(o)    (G_TYPE_CHECK_INSTANCE_CAST ((o), UDISKS_TYPE_LINUX_POOL_OBJECT_ZFS, UDisksLinuxPoolObjectZFS))
#define UDISKS_IS_LINUX_POOL_OBJECT_ZFS(o) (G_TYPE_CHECK_INSTANCE_TYPE ((o), UDISKS_TYPE_LINUX_POOL_OBJECT_ZFS))

GType                        udisks_linux_pool_object_zfs_get_type   (void) G_GNUC_CONST;
UDisksLinuxPoolObjectZFS    *udisks_linux_pool_object_zfs_new        (UDisksLinuxModuleZFS *module,
                                                                      const gchar          *name);
const gchar                 *udisks_linux_pool_object_zfs_get_name   (UDisksLinuxPoolObjectZFS *object);
UDisksLinuxModuleZFS        *udisks_linux_pool_object_zfs_get_module (UDisksLinuxPoolObjectZFS *object);
void                         udisks_linux_pool_object_zfs_update     (UDisksLinuxPoolObjectZFS *object,
                                                                      BDZFSPoolInfo            *info);
void                         udisks_linux_pool_object_zfs_destroy    (UDisksLinuxPoolObjectZFS *object);

G_END_DECLS

#endif /* __UDISKS_LINUX_POOL_OBJECT_ZFS_H__ */
