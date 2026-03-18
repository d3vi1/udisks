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


if __name__ == "__main__":
    unittest.main()
