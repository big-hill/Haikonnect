# -*- coding: utf-8 -*-

'''
This Source Code Form is subject to the terms of the Mozilla
Public License, v. 2.0. If a copy of the MPL was not distributed
with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
'''

import argparse
import importlib
import json
import os
import sys
import threading
import time
import urllib


URL = "https://www.dwservice.net/"
PROXY_TYPE = "SYSTEM"  # HTTP, SOCKS4, SOCKS4A, SOCKS5 or NONE
PROXY_HOST = ""
PROXY_PORT = 0
PROXY_USER = ""
PROXY_PASSWORD = ""
DEFAULT_TOTAL_TIMEOUT = 120
PROGRESS_INTERVAL = 10

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
CORE = os.path.join(ROOT, "core")
CONFIG_PATH = os.path.join(CORE, "config.json")
CACERTS_PATH = os.path.join(CORE, "cacerts.pem")
if CORE not in sys.path:
    sys.path.insert(0, CORE)

from config_security import atomic_write_private
from config_security import enforce_private_file
from config_security import is_private_file


def is_py2():
    return sys.version_info[0] == 2


if is_py2():
    def _py2_str_new(value):
        if isinstance(value, unicode):
            return value
        if isinstance(value, str):
            return value.decode("utf8", errors="replace")
        return str(value).decode("utf8", errors="replace")

    str_new = _py2_str_new
    url_parse_quote_plus = urllib.quote_plus
else:
    import urllib.parse
    str_new = str
    url_parse_quote_plus = urllib.parse.quote_plus


def exception_to_string(exception):
    try:
        message = getattr(exception, "message", None)
        if message:
            return str_new(message)
        return str_new(exception)
    except Exception:
        return u"Unexpected error."


def _positive_timeout(value):
    timeout = int(value)
    if timeout < 1:
        raise argparse.ArgumentTypeError("timeout must be at least one second")
    return timeout


def _network_call_with_progress(function, args, timeout):
    result = {}

    def run():
        try:
            result["value"] = function(*args)
        except Exception as exception:
            result["exception"] = exception

    worker = threading.Thread(target=run, name="Haikonnect configuration request")
    worker.daemon = True
    worker.start()
    started = time.monotonic() if hasattr(time, "monotonic") else time.time()
    deadline = started + timeout

    while worker.is_alive():
        now = time.monotonic() if hasattr(time, "monotonic") else time.time()
        remaining = deadline - now
        if remaining <= 0:
            raise RuntimeError(
                "Installation-code check timed out after {} seconds.".format(
                    timeout))
        worker.join(min(PROGRESS_INTERVAL, remaining))
        if worker.is_alive():
            now = time.monotonic() if hasattr(time, "monotonic") else time.time()
            elapsed = int(now - started)
            print("Still checking the installation code... {} seconds elapsed.".format(
                elapsed))

    if "exception" in result:
        raise result["exception"]
    return result.get("value")


def _read_private_backup(path):
    if not is_private_file(path):
        raise ValueError("Backup must be a regular mode-0600 file: " + path)
    with open(path, "rb") as source:
        data = source.read()
    parsed = json.loads(data.decode("utf-8"))
    if not isinstance(parsed, dict) or "key" not in parsed or "password" not in parsed:
        raise ValueError("Backup is not a valid Haikonnect agent configuration.")
    return data


def _backup_existing_config(path):
    if os.path.abspath(path) == os.path.abspath(CONFIG_PATH):
        raise ValueError("Backup path must differ from core/config.json.")
    enforce_private_file(CONFIG_PATH)
    with open(CONFIG_PATH, "rb") as source:
        atomic_write_private(path, source.read())


def _parse_args(argv):
    parser = argparse.ArgumentParser(
        description="Create or safely replace core/config.json")
    parser.add_argument(
        "--replace", action="store_true",
        help="replace an existing configuration only after validation succeeds")
    parser.add_argument(
        "--backup", metavar="PATH",
        help="write a private backup before --replace")
    parser.add_argument(
        "--restore", metavar="PATH",
        help="restore a private backup instead of contacting DWService")
    parser.add_argument(
        "--timeout", type=_positive_timeout, default=DEFAULT_TOTAL_TIMEOUT,
        help="total installation-code check timeout in seconds (default: 120)")
    args = parser.parse_args(argv)
    if args.backup and not args.replace:
        parser.error("--backup requires --replace")
    if args.restore and (args.replace or args.backup):
        parser.error("--restore cannot be combined with --replace or --backup")
    return args


def main(argv=None):
    args = _parse_args(argv)
    print("This script generates core/config.json")
    print("")

    if args.restore:
        try:
            atomic_write_private(CONFIG_PATH, _read_private_backup(args.restore))
            print("Restored core/config.json with mode 0600.")
            print("")
            print("END.")
            return 0
        except Exception as exception:
            print("Restore error: " + exception_to_string(exception))
            return 1

    config_exists = os.path.exists(CONFIG_PATH)
    if config_exists:
        try:
            enforce_private_file(CONFIG_PATH)
        except Exception as exception:
            print("Configuration security error: " + exception_to_string(exception))
            return 1
    if config_exists and not args.replace:
        print("Error: File core/config.json already exists.")
        print("Use --replace, preferably together with --backup PATH.")
        return 1

    communication = importlib.import_module("communication")
    set_cacerts_path = getattr(communication, "set_cacerts_path")
    get_url_prop = getattr(communication, "get_url_prop")
    ProxyInfo = getattr(communication, "ProxyInfo")
    agent = importlib.import_module("agent")
    obfuscate_password = getattr(agent, "obfuscate_password")

    print("Create a new agent in your www.dwservice.net account to get an installation code.")
    code = input("Enter the code: ") if not is_py2() else raw_input("Enter the code: ")
    url = URL + "checkInstallCode.dw?code=" + url_parse_quote_plus(code)

    try:
        set_cacerts_path(CACERTS_PATH)
        proxy = ProxyInfo()
        proxy.set_type(PROXY_TYPE)
        if PROXY_HOST:
            proxy.set_host(PROXY_HOST)
        if PROXY_PORT > 0:
            proxy.set_port(PROXY_PORT)
        if PROXY_USER:
            proxy.set_user(PROXY_USER)
        if PROXY_PASSWORD:
            proxy.set_password(PROXY_PASSWORD)

        print("Check installation code (total timeout: {} seconds)...".format(
            args.timeout))
        properties = _network_call_with_progress(
            get_url_prop, (url, proxy), args.timeout)
        if "error" in properties:
            print("Installation code error: " + properties["error"])
            return 1

        config = {
            "url_primary": URL,
            "key": properties["key"],
            "password": obfuscate_password(properties["password"]),
            "enabled": True,
            "debug_indentation_max": 0,
            "debug_mode": True,
            "develop_mode": True,
            "proxy_type": PROXY_TYPE,
            "proxy_host": PROXY_HOST,
            "proxy_port": PROXY_PORT,
            "proxy_user": PROXY_USER,
            "proxy_password": PROXY_PASSWORD,
        }
        if sys.platform.lower().startswith("haiku"):
            config["updates_auto"] = False
            config["listener_http_enable"] = False
            config["desktop"] = {"sound_enable": False}

        if config_exists:
            if args.backup:
                _backup_existing_config(os.path.abspath(args.backup))
                print("Private backup written to: " + os.path.abspath(args.backup))

        serialized = json.dumps(config, sort_keys=True, indent=1)
        atomic_write_private(CONFIG_PATH, serialized)
        print("File core/config.json generated atomically with mode 0600.")
        print("")
        print("END.")
        return 0
    except Exception as exception:
        print("Connection/configuration error: " + exception_to_string(exception))
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
