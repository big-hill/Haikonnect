"""Portable regression tests for the Haiku integration points.

This Source Code Form is subject to the terms of the Mozilla
Public License, v. 2.0. If a copy of the MPL was not distributed
with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
"""

import os
import sys
import tempfile
import unittest
from unittest import mock


ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
CORE = os.path.join(ROOT, "core")
sys.path.insert(0, ROOT)
sys.path.insert(0, CORE)

import detectinfo
import ipc
import utils
from app_filesystem.filesystem import FileSystem, Linux


class HaikuPortTests(unittest.TestCase):

    def test_native_suffix_for_haiku_x86_64(self):
        with mock.patch.object(detectinfo.platform, "system", return_value="Haiku"), \
                mock.patch.object(detectinfo.platform, "machine", return_value="x86_64"):
            self.assertEqual("haiku_x86_64", detectinfo.get_native_suffix())

    def test_filesystem_uses_posix_backend(self):
        with mock.patch.object(utils, "is_windows", return_value=False), \
                mock.patch.object(utils, "is_linux", return_value=False), \
                mock.patch.object(utils, "is_mac", return_value=False), \
                mock.patch.object(utils, "is_haiku", return_value=True):
            filesystem = FileSystem(object())
            self.assertIsInstance(filesystem.get_osnative(), Linux)

    def test_haiku_memmap_uses_file_backend(self):
        with tempfile.TemporaryDirectory() as directory, \
                mock.patch.object(ipc, "IPC_PATH", directory), \
                mock.patch.object(utils, "is_haiku", return_value=True):
            mapping = ipc.MemMapIPC(4096)
            try:
                self.assertEqual("F", mapping.ftype)
                mapping.seek(0)
                mapping.write(b"BeRD")
                mapping.seek(0)
                self.assertEqual(b"BeRD", mapping.read(4))
            finally:
                mapping._destroy()


if __name__ == "__main__":
    unittest.main()
