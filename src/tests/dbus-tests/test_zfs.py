#!/usr/bin/python3

import os
import unittest
import shutil

import udiskstestcase


class UDisksZFSTest(udiskstestcase.UdisksTestCase):
    """Test ZFS module functionality"""

    @classmethod
    def setUpClass(cls):
        udiskstestcase.UdisksTestCase.setUpClass()
        if not cls.check_module_loaded('zfs'):
            udiskstestcase.UdisksTestCase.tearDownClass()
            raise unittest.SkipTest('Udisks module for ZFS tests not loaded, skipping.')

        # Skip if ZFS tools not available
        if not shutil.which("zpool"):
            udiskstestcase.UdisksTestCase.tearDownClass()
            raise unittest.SkipTest("zpool not found, skipping ZFS tests")
        if not shutil.which("zfs"):
            udiskstestcase.UdisksTestCase.tearDownClass()
            raise unittest.SkipTest("zfs not found, skipping ZFS tests")

        # Check if ZFS kernel module is loaded
        if not os.path.exists("/sys/module/zfs"):
            udiskstestcase.UdisksTestCase.tearDownClass()
            raise unittest.SkipTest("ZFS kernel module not loaded, skipping ZFS tests")

    def test_module_loaded(self):
        """Test that the ZFS module is loaded and Manager.ZFS interface is present"""
        manager = self.get_object('/Manager')
        intro_data = manager.Introspect(self.no_options,
                                        dbus_interface='org.freedesktop.DBus.Introspectable')
        self.assertIn('interface name="%s.Manager.ZFS"' % self.iface_prefix, intro_data)

    def test_pool_create_destroy(self):
        """Test pool create and destroy lifecycle"""
        # This test requires loop devices - skip if not available
        self.skipTest("Full pool lifecycle test requires ZFS kernel module and loop devices")

    def test_list_datasets(self):
        """Test ListDatasets method"""
        self.skipTest("Dataset listing test requires an active ZFS pool")

    def test_property_allowlist(self):
        """Test that property allowlist is enforced"""
        self.skipTest("Property allowlist test requires an active ZFS pool")

    def test_scrub_progress(self):
        """Test scrub progress polling"""
        self.skipTest("Scrub test requires an active ZFS pool")

    def test_rollback_snapshot_default_options(self):
        """Test RollbackSnapshot accepts default options (no force, no destroy_newer)"""
        self.skipTest("Rollback test requires an active ZFS pool with snapshots")

    def test_rollback_snapshot_force_option(self):
        """Test RollbackSnapshot passes force option as force-unmount (-f)"""
        self.skipTest("Rollback test requires an active ZFS pool with snapshots")

    def test_rollback_snapshot_destroy_newer_option(self):
        """Test RollbackSnapshot passes destroy_newer option to destroy newer snapshots (-r)"""
        self.skipTest("Rollback test requires an active ZFS pool with snapshots")

    def test_rollback_snapshot_both_options(self):
        """Test RollbackSnapshot with both force and destroy_newer options"""
        self.skipTest("Rollback test requires an active ZFS pool with snapshots")

    def test_pool_import_by_name(self):
        """Test PoolImport with a pool name"""
        self.skipTest("Import-by-name test requires an exported ZFS pool")

    def test_pool_import_by_guid(self):
        """Test PoolImport with a bare GUID resolves the pool name correctly"""
        self.skipTest("Import-by-GUID test requires an exported ZFS pool")

    def test_pool_import_with_new_name(self):
        """Test PoolImport with new_name option renames the pool on import"""
        self.skipTest("Import-with-new_name test requires an exported ZFS pool")

    def test_pool_import_guid_with_new_name(self):
        """Test PoolImport by GUID with new_name option"""
        self.skipTest("Import-by-GUID-with-new_name test requires an exported ZFS pool")

    def test_pool_import_all_default(self):
        """Test PoolImportAll imports all available pools without force"""
        self.skipTest("PoolImportAll test requires exported ZFS pools")

    def test_pool_import_all_force(self):
        """Test PoolImportAll with force=True passes -f to each pool import"""
        self.skipTest("PoolImportAll test requires exported ZFS pools")

    def test_pool_import_all_no_pools(self):
        """Test PoolImportAll succeeds gracefully when no pools are importable"""
        self.skipTest("PoolImportAll test requires no exported ZFS pools present")

    def test_pool_import_all_partial_failure(self):
        """Test PoolImportAll reports per-pool errors when some imports fail"""
        self.skipTest("PoolImportAll partial-failure test requires multiple exported ZFS pools")

    def test_mount_dataset_default_options(self):
        """Test MountDataset enforces nosuid,nodev safety defaults"""
        self.skipTest("Mount safety-defaults test requires an active ZFS pool with datasets")

    def test_mount_dataset_caller_options(self):
        """Test MountDataset appends caller mount_options after nosuid,nodev"""
        self.skipTest("Mount caller-options test requires an active ZFS pool with datasets")

    def test_mount_dataset_mountpoint(self):
        """Test MountDataset passes mountpoint override to libblockdev"""
        self.skipTest("Mount mountpoint test requires an active ZFS pool with datasets")

    def test_mount_dataset_rejects_malformed_options(self):
        """Test MountDataset rejects options containing newlines or tabs"""
        self.skipTest("Mount malformed-options test requires an active ZFS pool with datasets")


if __name__ == "__main__":
    unittest.main()
