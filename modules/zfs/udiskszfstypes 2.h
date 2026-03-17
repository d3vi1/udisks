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

#ifndef __UDISKS_ZFS_TYPES_H__
#define __UDISKS_ZFS_TYPES_H__

#define ZFS_MODULE_NAME "zfs"
#define ZFS_POLICY_ACTION_ID "org.freedesktop.udisks2.zfs.manage-zfs"
#define ZFS_POLICY_ACTION_ID_QUERY "org.freedesktop.udisks2.zfs.query"
#define ZFS_POLICY_ACTION_ID_DESTROY "org.freedesktop.udisks2.zfs.manage-zfs-destroy"

struct _UDisksLinuxModuleZFS;
typedef struct _UDisksLinuxModuleZFS UDisksLinuxModuleZFS;

typedef struct _UDisksLinuxManagerZFS        UDisksLinuxManagerZFS;
typedef struct _UDisksLinuxManagerZFSClass   UDisksLinuxManagerZFSClass;

typedef struct _UDisksLinuxPoolObjectZFS        UDisksLinuxPoolObjectZFS;
typedef struct _UDisksLinuxPoolObjectZFSClass   UDisksLinuxPoolObjectZFSClass;

#endif /* __UDISKS_ZFS_TYPES_H__ */
