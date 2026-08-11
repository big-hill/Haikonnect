#!/usr/bin/env python3
"""Small runtime gate for the Haiku native DWService components.

This Source Code Form is subject to the terms of the Mozilla
Public License, v. 2.0. If a copy of the MPL was not distributed
with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
"""
import ctypes
import argparse
import hashlib
import json
import os
import platform
import sys


ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
CORE = os.path.join(ROOT, "core")
sys.path.insert(0, ROOT)
sys.path.insert(0, CORE)
os.chdir(CORE)

from app_desktop import common
import config_security
import detectinfo
import native
import utils


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--require-input", action="store_true",
        help="fail unless both DWService input-server devices authenticate")
    args = parser.parse_args()
    report = {
        "platform": platform.platform(),
        "python": platform.python_version(),
        "is_haiku": utils.is_haiku(),
        "native_suffix": detectinfo.get_native_suffix(),
    }
    config_path = os.path.join(CORE, "config.json")
    config_present = os.path.lexists(config_path)
    config_mode = config_security.file_mode(config_path)
    report["agent_config"] = {
        "present": config_present,
        "mode": config_security.mode_text(config_mode),
        "private": not config_present
            or config_security.is_private_file(config_path),
    }
    core = native.get_instance().get_library()
    report["dwaglib_loaded"] = core is not None
    dependencies = native.load_libraries_with_deps("screencapture")
    capture = native._load_lib_obj("dwagscreencapturehaiku.so")
    report["capture_version"] = capture.DWAScreenCaptureVersion()
    if not capture.DWAScreenCaptureLoad():
        raise RuntimeError("DWAScreenCaptureLoad failed")
    try:
        input_probe = getattr(capture, "DWAHaikuInputProbe", None)
        if input_probe is None:
            report["input_probe"] = "missing"
        else:
            input_probe.restype = ctypes.c_int
            report["input_probe"] = input_probe()
        monitors = common.MONITORS_INFO()
        result = capture.DWAScreenCaptureGetMonitorsInfo(ctypes.byref(monitors))
        if result != 0 or monitors.count < 1:
            raise RuntimeError("No monitor returned")
        monitor = monitors.monitor[0]
        image = common.RGB_IMAGE()
        session = ctypes.c_void_p()
        result = capture.DWAScreenCaptureInitMonitor(
            ctypes.byref(monitor), ctypes.byref(image), ctypes.byref(session))
        if result != 0:
            raise RuntimeError("InitMonitor failed: %d" % result)
        try:
            result = capture.DWAScreenCaptureGetImage(session)
            if result != 0:
                raise RuntimeError("GetImage failed: %d" % result)
            data = ctypes.string_at(image.data, image.sizedata)
            report["monitor"] = {
                "x": monitor.x,
                "y": monitor.y,
                "width": monitor.width,
                "height": monitor.height,
            }
            report["frame_bytes"] = image.sizedata
            report["frame_sha256"] = hashlib.sha256(data).hexdigest()
            report["changed_rects"] = image.sizechangearea
        finally:
            capture.DWAScreenCaptureTermMonitor(session)
        cursor = common.CURSOR_IMAGE()
        result = capture.DWAScreenCaptureCursor(ctypes.byref(cursor))
        report["cursor"] = {
            "result": result,
            "visible": cursor.visible,
            "x": cursor.x,
            "y": cursor.y,
            "width": cursor.width,
            "height": cursor.height,
        }
        if cursor.data:
            capture.DWAScreenCaptureFreeMemory(ctypes.c_void_p(cursor.data))
    finally:
        capture.DWAScreenCaptureUnload()
        native._unload_lib_obj(capture)
        native.unload_libraries(dependencies)
    print(json.dumps(report, indent=2, sort_keys=True))
    if not report["is_haiku"] or report["native_suffix"] != "haiku_x86_64":
        return 2
    if not report["agent_config"]["private"]:
        return 4
    if args.require_input and report.get("input_probe") != 0:
        return 3
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
