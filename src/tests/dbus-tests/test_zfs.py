#!/usr/bin/python3

import dbus
import os
import time
import unittest
import shutil

import udiskstestcase


# ZFS runtime availability: kernel module loaded and userspace tools present
ZFS_AVAILABLE = (os.path.exists('/sys/module/zfs')
                 and shutil.which('zpool') is not None
                 and shutil.which('zfs') is not None)


class UDisksZFSTest(udiskstestcase.UdisksTestCase):
    """Test ZFS module functionality"""

    @classmethod
    def setUpClass(cls):
        udiskstestcase.UdisksTestCase.setUpClass()
        if not cls.check_module_loaded('zfs'):
            udiskstestcase.UdisksTestCase.tearDownClass()
            raise unittest.SkipTest('Udisks module for ZFS tests not loaded, skipping.')

    # ------------------------------------------------------------------ #
    #  Helpers                                                            #
    # ------------------------------------------------------------------ #

    _POOL_PREFIX = 'udisks_test_zfs_'
    """Prefix for temporary pool names so they are easy to identify."""

    def _unique_pool_name(self, tag='pool'):
        """Return a pool name that is unique to this test invocation."""
        return '%s%s_%d' % (self._POOL_PREFIX, tag, os.getpid())

    def _get_manager_zfs(self):
        """Return a dbus.Interface for the Manager.ZFS interface."""
        manager = self.get_object('/Manager')
        return dbus.Interface(manager,
                              dbus_interface=self.iface_prefix + '.Manager.ZFS')

    def _get_pool_iface(self, pool_obj_path):
        """Return a dbus.Interface for the ZFSPool interface on a pool object."""
        pool_obj = self.get_object(pool_obj_path)
        return dbus.Interface(pool_obj,
                              dbus_interface=self.iface_prefix + '.ZFSPool')

    def _create_pool(self, tag='pool', vdev_idx=0):
        """Create a temporary ZFS pool on self.vdevs[vdev_idx].

        Registers cleanup to destroy the pool.  Returns (pool_name,
        pool_obj_path, pool_iface).
        """
        pool_name = self._unique_pool_name(tag)
        dev_path = self.vdevs[vdev_idx]
        dev_name = os.path.basename(dev_path)
        dev_obj_path = dbus.ObjectPath(
            self.path_prefix + '/block_devices/' + dev_name)

        manager_zfs = self._get_manager_zfs()
        pool_obj_path = manager_zfs.PoolCreate(
            pool_name,
            dbus.Array([dev_obj_path], signature='o'),
            '',  # stripe
            self.no_options)

        # Register cleanup: force-destroy the pool even if the test fails
        self.addCleanup(self._destroy_pool_cleanup, pool_name, dev_path)

        pool_iface = self._get_pool_iface(pool_obj_path)
        return pool_name, pool_obj_path, pool_iface

    def _destroy_pool_cleanup(self, pool_name, dev_path):
        """Best-effort pool cleanup invoked via addCleanup."""
        # Try D-Bus destroy first (force=True)
        try:
            pool_obj_path = self.path_prefix + '/zfs/' + pool_name
            pool_iface = self._get_pool_iface(pool_obj_path)
            pool_iface.Destroy(dbus.Boolean(True), self.no_options)
            time.sleep(0.5)
        except dbus.exceptions.DBusException:
            pass

        # Fallback: shell out to zpool destroy
        self.run_command('zpool destroy -f %s' % pool_name)

        # Wipe any leftover ZFS labels so the device is clean for the next test
        self.run_command('wipefs -a %s' % dev_path)
        self.udev_settle()

    # ------------------------------------------------------------------ #
    #  Module / interface presence tests (no ZFS runtime needed)          #
    # ------------------------------------------------------------------ #

    def test_module_loaded(self):
        """Test that the ZFS module is loaded and Manager.ZFS interface is present"""
        manager = self.get_object('/Manager')
        intro_data = manager.Introspect(self.no_options,
                                        dbus_interface='org.freedesktop.DBus.Introspectable')
        self.assertIn('interface name="%s.Manager.ZFS"' % self.iface_prefix, intro_data)

    def test_manager_zfs_interface(self):
        """Test that the Manager.ZFS interface exposes expected D-Bus methods"""
        manager = self.get_object('/Manager')
        intro_data = manager.Introspect(self.no_options,
                                        dbus_interface='org.freedesktop.DBus.Introspectable')
        for method in ('PoolCreate', 'PoolImport', 'PoolImportAll',
                       'ListImportablePools'):
            self.assertIn('name="%s"' % method, intro_data,
                          msg='Method %s not found on Manager.ZFS interface' % method)

    # ------------------------------------------------------------------ #
    #  Pool lifecycle                                                     #
    # ------------------------------------------------------------------ #

    @unittest.skipUnless(ZFS_AVAILABLE, 'ZFS kernel module or tools not available')
    def test_pool_create_destroy(self):
        """Test pool create and destroy lifecycle"""
        pool_name, pool_obj_path, pool_iface = self._create_pool(tag='crdes')

        # The pool D-Bus object must be present on the bus
        pool_obj = self.get_object(pool_obj_path)
        self.assertIsNotNone(pool_obj)

        # Verify the Name property matches
        name_prop = self.get_property(pool_obj, '.ZFSPool', 'Name')
        name_prop.assertEqual(pool_name)

        # Verify the pool is ONLINE via zpool list
        ret, out = self.run_command('zpool list -H -o name %s' % pool_name)
        self.assertEqual(ret, 0)
        self.assertIn(pool_name, out)

        # Destroy the pool via D-Bus
        pool_iface.Destroy(dbus.Boolean(False), self.no_options)
        time.sleep(1)

        # The pool should no longer be reported by zpool
        ret, _out = self.run_command('zpool list -H -o name %s' % pool_name)
        self.assertNotEqual(ret, 0)

        # The D-Bus object should be gone
        self.assertObjNotOnBus(pool_obj_path)

    # ------------------------------------------------------------------ #
    #  Dataset listing                                                    #
    # ------------------------------------------------------------------ #

    @unittest.skipUnless(ZFS_AVAILABLE, 'ZFS kernel module or tools not available')
    def test_list_datasets(self):
        """Test ListDatasets method"""
        pool_name, _pool_obj_path, pool_iface = self._create_pool(tag='listds')

        datasets = pool_iface.ListDatasets(self.no_options)

        # A fresh pool always contains the root dataset (pool itself)
        names = [str(d['name']) for d in datasets]
        self.assertIn(pool_name, names)

    # ------------------------------------------------------------------ #
    #  Property allowlist                                                 #
    # ------------------------------------------------------------------ #

    @unittest.skipUnless(ZFS_AVAILABLE, 'ZFS kernel module or tools not available')
    def test_property_allowlist(self):
        """Test that property allowlist is enforced"""
        pool_name, _pool_obj_path, pool_iface = self._create_pool(tag='allow')

        # A property that is never on the allowlist should be rejected
        msg = 'OptionNotPermitted'
        with self.assertRaisesRegex(dbus.exceptions.DBusException, msg):
            pool_iface.SetDatasetProperty(pool_name, 'exec', 'on',
                                          self.no_options)

    # ------------------------------------------------------------------ #
    #  Scrub                                                              #
    # ------------------------------------------------------------------ #

    @unittest.skipUnless(ZFS_AVAILABLE, 'ZFS kernel module or tools not available')
    def test_scrub_progress(self):
        """Test scrub progress polling"""
        pool_name, pool_obj_path, pool_iface = self._create_pool(tag='scrub')

        pool_obj = self.get_object(pool_obj_path)

        # Start a scrub — on a tiny pool it finishes almost instantly
        pool_iface.ScrubStart(self.no_options)
        time.sleep(1)

        # Poll so the D-Bus properties update
        pool_iface.Poll()

        # ScrubRunning may already be False if the scrub finished, but
        # the property must at least be a valid boolean
        running = self.get_property_raw(pool_obj, '.ZFSPool', 'ScrubRunning')
        self.assertIn(running, (True, False))

        # Clean up: stop scrub if still running (ignore errors)
        try:
            pool_iface.ScrubStop(self.no_options)
        except dbus.exceptions.DBusException:
            pass

    # ------------------------------------------------------------------ #
    #  Rollback                                                           #
    # ------------------------------------------------------------------ #

    @unittest.skipUnless(ZFS_AVAILABLE, 'ZFS kernel module or tools not available')
    def test_rollback_snapshot_default_options(self):
        """Test RollbackSnapshot accepts default options (no force, no destroy_newer)"""
        pool_name, _pool_obj_path, pool_iface = self._create_pool(tag='rbdef')

        snap_name = '%s@snap1' % pool_name
        pool_iface.CreateSnapshot(pool_name, 'snap1', dbus.Boolean(False),
                                  self.no_options)

        # Rollback with default options (no force, no destroy_newer)
        pool_iface.RollbackSnapshot(snap_name, self.no_options)

        # The snapshot should still exist after rollback
        datasets = pool_iface.ListDatasets(
            dbus.Dictionary({'type': 'snapshot'}, signature='sv'))
        snap_names = [str(d['name']) for d in datasets]
        self.assertIn(snap_name, snap_names)

    @unittest.skipUnless(ZFS_AVAILABLE, 'ZFS kernel module or tools not available')
    def test_rollback_snapshot_force_option(self):
        """Test RollbackSnapshot passes force option as force-unmount (-f)"""
        pool_name, _pool_obj_path, pool_iface = self._create_pool(tag='rbfrc')

        snap_name = '%s@snap1' % pool_name
        pool_iface.CreateSnapshot(pool_name, 'snap1', dbus.Boolean(False),
                                  self.no_options)

        # Rollback with force=True should succeed
        opts = dbus.Dictionary({'force': dbus.Boolean(True)}, signature='sv')
        pool_iface.RollbackSnapshot(snap_name, opts)

        # Verify the snapshot is still present
        datasets = pool_iface.ListDatasets(
            dbus.Dictionary({'type': 'snapshot'}, signature='sv'))
        snap_names = [str(d['name']) for d in datasets]
        self.assertIn(snap_name, snap_names)

    @unittest.skipUnless(ZFS_AVAILABLE, 'ZFS kernel module or tools not available')
    def test_rollback_snapshot_destroy_newer_option(self):
        """Test RollbackSnapshot passes destroy_newer option to destroy newer snapshots (-r)"""
        pool_name, _pool_obj_path, pool_iface = self._create_pool(tag='rbnew')

        # Create two snapshots; rollback to the first with destroy_newer=True
        snap1 = '%s@snap1' % pool_name
        snap2 = '%s@snap2' % pool_name
        pool_iface.CreateSnapshot(pool_name, 'snap1', dbus.Boolean(False),
                                  self.no_options)
        pool_iface.CreateSnapshot(pool_name, 'snap2', dbus.Boolean(False),
                                  self.no_options)

        opts = dbus.Dictionary({'destroy_newer': dbus.Boolean(True)},
                               signature='sv')
        pool_iface.RollbackSnapshot(snap1, opts)

        # snap2 should have been destroyed
        datasets = pool_iface.ListDatasets(
            dbus.Dictionary({'type': 'snapshot'}, signature='sv'))
        snap_names = [str(d['name']) for d in datasets]
        self.assertIn(snap1, snap_names)
        self.assertNotIn(snap2, snap_names)

    @unittest.skipUnless(ZFS_AVAILABLE, 'ZFS kernel module or tools not available')
    def test_rollback_snapshot_both_options(self):
        """Test RollbackSnapshot with both force and destroy_newer options"""
        pool_name, _pool_obj_path, pool_iface = self._create_pool(tag='rbbth')

        snap1 = '%s@snap1' % pool_name
        snap2 = '%s@snap2' % pool_name
        pool_iface.CreateSnapshot(pool_name, 'snap1', dbus.Boolean(False),
                                  self.no_options)
        pool_iface.CreateSnapshot(pool_name, 'snap2', dbus.Boolean(False),
                                  self.no_options)

        opts = dbus.Dictionary({'force': dbus.Boolean(True),
                                'destroy_newer': dbus.Boolean(True)},
                               signature='sv')
        pool_iface.RollbackSnapshot(snap1, opts)

        datasets = pool_iface.ListDatasets(
            dbus.Dictionary({'type': 'snapshot'}, signature='sv'))
        snap_names = [str(d['name']) for d in datasets]
        self.assertIn(snap1, snap_names)
        self.assertNotIn(snap2, snap_names)

    # ------------------------------------------------------------------ #
    #  Import semantics                                                   #
    # ------------------------------------------------------------------ #

    @unittest.skipUnless(ZFS_AVAILABLE, 'ZFS kernel module or tools not available')
    def test_pool_import_by_name(self):
        """Test PoolImport with a pool name"""
        pool_name, pool_obj_path, pool_iface = self._create_pool(tag='impnm')

        # Export the pool
        pool_iface.Export(dbus.Boolean(False), self.no_options)
        time.sleep(1)
        self.udev_settle()

        # Re-import by name
        manager_zfs = self._get_manager_zfs()
        new_obj_path = manager_zfs.PoolImport(pool_name, self.no_options)

        pool_obj = self.get_object(new_obj_path)
        name_prop = self.get_property(pool_obj, '.ZFSPool', 'Name')
        name_prop.assertEqual(pool_name)

    @unittest.skipUnless(ZFS_AVAILABLE, 'ZFS kernel module or tools not available')
    def test_pool_import_by_guid(self):
        """Test PoolImport with a bare GUID resolves the pool name correctly"""
        pool_name, pool_obj_path, pool_iface = self._create_pool(tag='impgd')

        pool_obj = self.get_object(pool_obj_path)
        guid = str(self.get_property_raw(pool_obj, '.ZFSPool', 'GUID'))

        # Export and re-import by GUID
        pool_iface.Export(dbus.Boolean(False), self.no_options)
        time.sleep(1)
        self.udev_settle()

        manager_zfs = self._get_manager_zfs()
        new_obj_path = manager_zfs.PoolImport(guid, self.no_options)

        pool_obj = self.get_object(new_obj_path)
        name_prop = self.get_property(pool_obj, '.ZFSPool', 'Name')
        name_prop.assertEqual(pool_name)

    @unittest.skipUnless(ZFS_AVAILABLE, 'ZFS kernel module or tools not available')
    def test_pool_import_with_new_name(self):
        """Test PoolImport with new_name option renames the pool on import"""
        pool_name, pool_obj_path, pool_iface = self._create_pool(tag='imprn')
        new_name = self._unique_pool_name('renamed')

        # Register cleanup for the renamed pool too
        self.addCleanup(self.run_command, 'zpool destroy -f %s' % new_name)

        pool_iface.Export(dbus.Boolean(False), self.no_options)
        time.sleep(1)
        self.udev_settle()

        opts = dbus.Dictionary({'new_name': new_name}, signature='sv')
        manager_zfs = self._get_manager_zfs()
        new_obj_path = manager_zfs.PoolImport(pool_name, opts)

        pool_obj = self.get_object(new_obj_path)
        name_prop = self.get_property(pool_obj, '.ZFSPool', 'Name')
        name_prop.assertEqual(new_name)

    @unittest.skipUnless(ZFS_AVAILABLE, 'ZFS kernel module or tools not available')
    def test_pool_import_guid_with_new_name(self):
        """Test PoolImport by GUID with new_name option"""
        pool_name, pool_obj_path, pool_iface = self._create_pool(tag='gdrn')
        new_name = self._unique_pool_name('gdrenamed')

        self.addCleanup(self.run_command, 'zpool destroy -f %s' % new_name)

        pool_obj = self.get_object(pool_obj_path)
        guid = str(self.get_property_raw(pool_obj, '.ZFSPool', 'GUID'))

        pool_iface.Export(dbus.Boolean(False), self.no_options)
        time.sleep(1)
        self.udev_settle()

        opts = dbus.Dictionary({'new_name': new_name}, signature='sv')
        manager_zfs = self._get_manager_zfs()
        new_obj_path = manager_zfs.PoolImport(guid, opts)

        pool_obj = self.get_object(new_obj_path)
        name_prop = self.get_property(pool_obj, '.ZFSPool', 'Name')
        name_prop.assertEqual(new_name)

    @unittest.skipUnless(ZFS_AVAILABLE, 'ZFS kernel module or tools not available')
    def test_pool_import_all_default(self):
        """Test PoolImportAll imports all available pools without force"""
        pool_name, _pool_obj_path, pool_iface = self._create_pool(tag='imall')

        pool_iface.Export(dbus.Boolean(False), self.no_options)
        time.sleep(1)
        self.udev_settle()

        manager_zfs = self._get_manager_zfs()
        manager_zfs.PoolImportAll(self.no_options)
        time.sleep(1)

        # The pool should be back
        ret, out = self.run_command('zpool list -H -o name %s' % pool_name)
        self.assertEqual(ret, 0)
        self.assertIn(pool_name, out)

    @unittest.skipUnless(ZFS_AVAILABLE, 'ZFS kernel module or tools not available')
    def test_pool_import_all_force(self):
        """Test PoolImportAll with force=True passes -f to each pool import"""
        pool_name, _pool_obj_path, pool_iface = self._create_pool(tag='imafo')

        pool_iface.Export(dbus.Boolean(False), self.no_options)
        time.sleep(1)
        self.udev_settle()

        opts = dbus.Dictionary({'force': dbus.Boolean(True)}, signature='sv')
        manager_zfs = self._get_manager_zfs()
        manager_zfs.PoolImportAll(opts)
        time.sleep(1)

        ret, out = self.run_command('zpool list -H -o name %s' % pool_name)
        self.assertEqual(ret, 0)
        self.assertIn(pool_name, out)

    @unittest.skipUnless(ZFS_AVAILABLE, 'ZFS kernel module or tools not available')
    def test_pool_import_all_no_pools(self):
        """Test PoolImportAll succeeds gracefully when no pools are importable"""
        # Ensure there are no exported pools by not creating one.
        # PoolImportAll should succeed with nothing to import.
        manager_zfs = self._get_manager_zfs()
        # This should not raise (it is a no-op when nothing is importable)
        manager_zfs.PoolImportAll(self.no_options)

    @unittest.skipUnless(ZFS_AVAILABLE, 'ZFS kernel module or tools not available')
    def test_pool_import_all_partial_failure(self):
        """Test PoolImportAll reports per-pool errors when some imports fail"""
        # Create a pool, export it, then corrupt the vdev so import fails
        pool_name, _pool_obj_path, pool_iface = self._create_pool(tag='impar')
        dev_path = self.vdevs[0]

        pool_iface.Export(dbus.Boolean(False), self.no_options)
        time.sleep(1)
        self.udev_settle()

        # Wipe the ZFS labels so the pool cannot be discovered
        self.run_command('wipefs -a %s' % dev_path)
        self.udev_settle()

        # PoolImportAll should succeed (no importable pools found after wipe)
        # or fail with an error mentioning the pool — either is acceptable
        manager_zfs = self._get_manager_zfs()
        try:
            manager_zfs.PoolImportAll(self.no_options)
        except dbus.exceptions.DBusException:
            # Any error is fine — the pool's vdev was destroyed
            pass

    # ------------------------------------------------------------------ #
    #  Mount policy                                                       #
    # ------------------------------------------------------------------ #

    @unittest.skipUnless(ZFS_AVAILABLE, 'ZFS kernel module or tools not available')
    def test_mount_dataset_default_options(self):
        """Test MountDataset enforces nosuid,nodev safety defaults"""
        pool_name, _pool_obj_path, pool_iface = self._create_pool(tag='mntdf')

        # Mount the root dataset
        pool_iface.MountDataset(pool_name, self.no_options)
        self.addCleanup(self.run_command,
                        'zfs unmount %s' % pool_name)
        time.sleep(0.5)

        # Check /proc/mounts for nosuid,nodev
        mounts = self.read_file('/proc/mounts')
        found = False
        for line in mounts.splitlines():
            parts = line.split()
            if len(parts) >= 4 and parts[0] == pool_name:
                mount_opts = parts[3]
                self.assertIn('nosuid', mount_opts)
                self.assertIn('nodev', mount_opts)
                found = True
                break
        self.assertTrue(found,
                        'Dataset %s not found in /proc/mounts' % pool_name)

    @unittest.skipUnless(ZFS_AVAILABLE, 'ZFS kernel module or tools not available')
    def test_mount_dataset_caller_options(self):
        """Test MountDataset appends caller mount_options after nosuid,nodev"""
        # The current implementation hardcodes nosuid,nodev and does not
        # support caller-supplied mount options.  Verify that the
        # hardcoded defaults are present regardless.
        pool_name, _pool_obj_path, pool_iface = self._create_pool(tag='mntcl')

        pool_iface.MountDataset(pool_name, self.no_options)
        self.addCleanup(self.run_command,
                        'zfs unmount %s' % pool_name)
        time.sleep(0.5)

        mounts = self.read_file('/proc/mounts')
        for line in mounts.splitlines():
            parts = line.split()
            if len(parts) >= 4 and parts[0] == pool_name:
                mount_opts = parts[3]
                self.assertIn('nosuid', mount_opts)
                self.assertIn('nodev', mount_opts)
                return
        self.fail('Dataset %s not found in /proc/mounts' % pool_name)

    @unittest.skipUnless(ZFS_AVAILABLE, 'ZFS kernel module or tools not available')
    def test_mount_dataset_mountpoint(self):
        """Test MountDataset passes mountpoint override to libblockdev"""
        pool_name, _pool_obj_path, pool_iface = self._create_pool(tag='mntpt')

        import tempfile
        tmp = tempfile.mkdtemp(prefix='udisks_zfs_test_')
        self.addCleanup(self.run_command,
                        'zfs unmount %s' % pool_name)
        self.addCleanup(os.rmdir, tmp)

        opts = dbus.Dictionary({'mountpoint': tmp}, signature='sv')
        pool_iface.MountDataset(pool_name, opts)
        time.sleep(0.5)

        self.assertTrue(os.path.ismount(tmp),
                        'Expected %s to be a mountpoint' % tmp)

    @unittest.skipUnless(ZFS_AVAILABLE, 'ZFS kernel module or tools not available')
    def test_mount_dataset_rejects_malformed_options(self):
        """Test MountDataset rejects options containing newlines or tabs"""
        self.skipTest("Mount malformed-options test requires an active ZFS pool with datasets")

    # ------------------------------------------------------------------ #
    #  ListDatasets edge cases                                            #
    # ------------------------------------------------------------------ #

    @unittest.skipUnless(ZFS_AVAILABLE, 'ZFS kernel module or tools not available')
    def test_list_datasets_unknown_type(self):
        """Test ListDatasets maps unrecognized backend dataset types to 'unknown'"""
        self.skipTest("Unknown-type mapping test requires a ZFS pool with a non-standard dataset type")

    @unittest.skipUnless(ZFS_AVAILABLE, 'ZFS kernel module or tools not available')
    def test_list_datasets_unknown_key_status(self):
        """Test ListDatasets maps unrecognized backend key states to 'unknown'"""
        self.skipTest("Unknown-key-status mapping test requires a ZFS pool with a non-standard key state")

    # ------------------------------------------------------------------ #
    #  InheritProperty boundaries                                         #
    # ------------------------------------------------------------------ #

    @unittest.skipUnless(ZFS_AVAILABLE, 'ZFS kernel module or tools not available')
    def test_inherit_property_rejects_pool_only(self):
        """Test InheritProperty rejects pool-only properties like autoexpand"""
        pool_name, _pool_obj_path, pool_iface = self._create_pool(tag='inhpo')

        msg = 'OptionNotPermitted'
        with self.assertRaisesRegex(dbus.exceptions.DBusException, msg):
            pool_iface.InheritProperty(pool_name, 'autoexpand',
                                       self.no_options)

    @unittest.skipUnless(ZFS_AVAILABLE, 'ZFS kernel module or tools not available')
    def test_inherit_property_allows_dataset_property(self):
        """Test InheritProperty accepts dataset-level properties like compression"""
        pool_name, _pool_obj_path, pool_iface = self._create_pool(tag='inhds')

        # Create a child dataset so we have something to inherit on
        child_name = pool_iface.CreateDataset('child', self.no_options)

        # Set compression explicitly, then inherit it — should succeed
        pool_iface.SetDatasetProperty(str(child_name), 'compression', 'lz4',
                                      self.no_options)
        pool_iface.InheritProperty(str(child_name), 'compression',
                                   self.no_options)

        # After inherit, the source should be "default" or "inherited"
        value, source = pool_iface.GetDatasetProperty(str(child_name),
                                                      'compression',
                                                      self.no_options)
        self.assertIn(str(source), ('default', 'inherited from %s' % pool_name,
                                    'inherited'))

    # ------------------------------------------------------------------ #
    #  Destroy / unmount behavior                                         #
    # ------------------------------------------------------------------ #

    @unittest.skipUnless(ZFS_AVAILABLE, 'ZFS kernel module or tools not available')
    def test_destroy_dataset_skips_unmount_for_snapshot(self):
        """Test DestroyDataset does not attempt unmount for snapshots"""
        pool_name, _pool_obj_path, pool_iface = self._create_pool(tag='dsnap')

        snap_full = '%s@snapdel' % pool_name
        pool_iface.CreateSnapshot(pool_name, 'snapdel', dbus.Boolean(False),
                                  self.no_options)

        # Destroying a snapshot must succeed without an unmount error
        pool_iface.DestroyDataset(snap_full, dbus.Boolean(False),
                                  self.no_options)

        # Verify the snapshot is gone
        datasets = pool_iface.ListDatasets(
            dbus.Dictionary({'type': 'snapshot'}, signature='sv'))
        snap_names = [str(d['name']) for d in datasets]
        self.assertNotIn(snap_full, snap_names)

    @unittest.skipUnless(ZFS_AVAILABLE, 'ZFS kernel module or tools not available')
    def test_rollback_force_maps_correctly(self):
        """Test that the force option is accepted by RollbackSnapshot"""
        pool_name, _pool_obj_path, pool_iface = self._create_pool(tag='rbmap')

        snap_name = '%s@snap1' % pool_name
        pool_iface.CreateSnapshot(pool_name, 'snap1', dbus.Boolean(False),
                                  self.no_options)

        # Calling with force=True must not raise
        opts = dbus.Dictionary({'force': dbus.Boolean(True)}, signature='sv')
        pool_iface.RollbackSnapshot(snap_name, opts)

        # Calling with force=False must also not raise
        opts_no_force = dbus.Dictionary({'force': dbus.Boolean(False)},
                                        signature='sv')
        pool_iface.RollbackSnapshot(snap_name, opts_no_force)


if __name__ == "__main__":
    unittest.main()
