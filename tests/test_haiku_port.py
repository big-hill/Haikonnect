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
import applications
from app_filesystem.filesystem import FileSystem, Linux
from app_shell.shell import HAIKU_DEFAULT_PATH, LinuxMac


class HaikuPortTests(unittest.TestCase):

    @staticmethod
    def _read(*parts):
        with open(os.path.join(ROOT, *parts), encoding="utf-8") as source:
            return source.read()

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

    def test_shell_is_advertised_on_haiku(self):
        with mock.patch.object(utils, "is_windows", return_value=False), \
                mock.patch.object(utils, "is_linux", return_value=False), \
                mock.patch.object(utils, "is_mac", return_value=False), \
                mock.patch.object(utils, "is_haiku", return_value=True):
            self.assertIn("shell", applications.get_supported(object()))

    def test_haiku_memmap_uses_file_backend(self):
        with tempfile.TemporaryDirectory() as directory, \
                mock.patch.object(ipc, "IPC_PATH", directory), \
                mock.patch.object(utils, "is_haiku", return_value=True):
            mapping = ipc.MemMapIPC(4096)
            try:
                self.assertEqual("F", mapping.ftype)
                mapping.seek(0)
                mapping.write(b"Haikonnect")
                mapping.seek(0)
                self.assertEqual(b"Haikonnect", mapping.read(10))
            finally:
                mapping._destroy()

    def test_haiku_ipc_uses_valid_python_executable(self):
        process = ipc.Process("package", "Class")
        fake_process = mock.Mock(pid=123)
        with mock.patch.object(ipc.sys, "executable", ""), \
                mock.patch.object(utils, "is_windows", return_value=False), \
                mock.patch.object(utils, "is_linux", return_value=False), \
                mock.patch.object(utils, "is_mac", return_value=False), \
                mock.patch.object(utils, "is_haiku", return_value=True), \
                mock.patch.object(utils, "path_isfile", side_effect=lambda path: path == "/boot/system/bin/python3"), \
                mock.patch.object(ipc.subprocess, "Popen", return_value=fake_process) as popen:
            executable = process._get_python_executable()
            process._create_process([executable, "agent.py", "app=ipc", "test-key"])

        self.assertEqual("/boot/system/bin/python3", executable)
        popen.assert_called_once_with(
            [executable, "agent.py", "app=ipc", "test-key"],
            env=mock.ANY,
        )

    def test_haiku_shell_reads_directly_from_pty(self):
        manager = mock.Mock()
        shell = LinuxMac(manager, "test", 80, 24)
        shell._pio = 7
        shell._reader = mock.Mock()
        with mock.patch.object(utils, "is_haiku", return_value=True), \
                mock.patch.object(
                    os, "read", return_value=b"Haikonnect") as read:
            self.assertEqual("Haikonnect", shell.read_update())

        read.assert_called_once_with(7, 80*24*16)
        shell._reader.read.assert_not_called()

    def test_haiku_shell_fallback_path_contains_system_bin(self):
        self.assertIn("/boot/system/bin", HAIKU_DEFAULT_PATH.split(":"))
        self.assertIn("/bin", HAIKU_DEFAULT_PATH.split(":"))

    def test_deskbar_controller_is_a_persistent_replicant(self):
        source = self._read(
            "os_haiku_control", "src", "dwservicecontrol.cpp")

        self.assertIn("instantiate_deskbar_item", source)
        self.assertIn("HaikonnectView::Instantiate(BMessage* archive)", source)
        self.assertIn('archive->AddString("add_on", kSignature)', source)
        self.assertIn('archive->AddString("class", "HaikonnectView")', source)
        self.assertIn("BMessageRunner", source)
        self.assertIn("deskbar.AddItem(&info.ref)", source)
        self.assertIn("deskbar.RemoveItem(kDeskbarItemName)", source)
        self.assertIn('const char* letter = "H"', source)
        self.assertNotIn('popen("ps"', source)

    def test_deskbar_menu_exposes_required_actions(self):
        source = self._read(
            "os_haiku_control", "src", "dwservicecontrol.cpp")

        for label in ("Dashboard", "Start Agent", "Stop Agent", "Log",
                      "About Haikonnect"):
            self.assertIn('"{}"'.format(label), source)
        for status in ("Running and configured", "Not configured", "Error"):
            self.assertIn('"{}"'.format(status), source)
        self.assertIn("Remote access for Haiku", source)

    def test_supervisor_publishes_pid_status_without_a_window(self):
        supervisor = self._read("os_haiku", "dwagent-haiku-service")

        self.assertIn('STATUS_FILE="$LOG_DIR/agent.status"', supervisor)
        self.assertIn('SUPERVISOR_FILE="$LOG_DIR/supervisor.pid"', supervisor)
        self.assertIn('write_supervisor_pid', supervisor)
        self.assertIn('write_status "running $child_pid"', supervisor)
        self.assertIn('write_status "error $status"', supervisor)
        self.assertIn("</dev/null", supervisor)
        self.assertIn(
            'PYTHON="/boot/home/config/non-packaged/bin/haikonnect-python3"',
            supervisor)
        self.assertIn(
            '"$PYTHON" agent.py -filelog -noctrlfile', supervisor)

    def test_installer_uses_haikonnect_identity_and_scoped_migration(self):
        installer = self._read("os_haiku", "install-local.sh")
        compiler = self._read("make", "compile_os_haiku_control.py")

        self.assertIn('CONTROL_APP="$APP_DIR/Haikonnect"', installer)
        self.assertIn(
            'RUNTIME_PYTHON="$BIN_DIR/haikonnect-python3"', installer)
        self.assertIn(
            "B_MULTIPLE_LAUNCH | B_BACKGROUND_APP is 0x5", installer)
        self.assertIn("printf '\\005\\000\\000\\000'", installer)
        self.assertIn(
            'addattr -f "$APP_FLAGS_FILE" -c APPF BEOS:APP_FLAGS',
            installer)
        self.assertIn('"$CONTROL_APP" --remove', installer)
        self.assertIn('"$CONTROL_APP" --remove-legacy', installer)
        self.assertIn('"$CONTROL_APP" --install', installer)
        self.assertIn('"$DESKBAR_DIR/Haikonnect"', installer)
        self.assertIn('LEGACY_CONTROL_APP="$APP_DIR/BeRDAgent"', installer)
        self.assertIn(
            'LEGACY_DESKBAR_LINK="$DESKBAR_DIR/BeRD Agent"', installer)
        self.assertIn(
            'INPUT_TEMP="$LOG_DIR/.dwservice_remote_input.new.$$"',
            installer)
        self.assertIn('mv "$INPUT_ADDON" "$INPUT_PREVIOUS"', installer)
        self.assertIn('mv "$INPUT_TEMP" "$INPUT_ADDON"', installer)
        self.assertNotIn(
            'cp make/native/dwservice_remote_input "$ADDON_DIR/',
            installer)
        self.assertIn('"Haikonnect"', compiler)

    def test_control_helper_targets_only_published_process_ids(self):
        helper = self._read("os_haiku", "haikonnect-agent-control")

        self.assertIn('launch_roster stop "$SERVICE"', helper)
        self.assertIn('kill -TERM "$supervisor_pid"', helper)
        self.assertIn('kill -TERM "$agent_pid"', helper)
        self.assertIn('SUPERVISOR_FILE="$LOG_DIR/supervisor.pid"', helper)
        self.assertNotIn("ps |", helper)

    def test_input_device_uses_native_modifier_and_click_sequences(self):
        source = self._read(
            "os_haiku_input", "src", "dwserviceinputdevice.cpp")

        for token in (
                "B_MODIFIERS_CHANGED",
                '"be:old_modifiers"',
                "map->control_map",
                "B_LEFT_COMMAND_KEY",
                "_WaitUntil(fButtonDownAt[index], kClickHold)",
                "snooze(kDoubleClickGap)",
                "_NextPrimaryClickCount()",
                "Release old buttons before pressing new ones",
                "what == B_MOUSE_DOWN && clicks > 0",
                'AddData("bytes", B_STRING_TYPE',
                "snooze(kKeyHold)"):
            self.assertIn(token, source)
        self.assertNotIn(
            'event->AddInt32("be:key_repeat", 1)', source)


if __name__ == "__main__":
    unittest.main()
