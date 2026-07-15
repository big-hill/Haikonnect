#!/bin/sh
# This Source Code Form is subject to the terms of the Mozilla Public License,
# v. 2.0. If a copy of the MPL was not distributed with this file, You can
# obtain one at http://mozilla.org/MPL/2.0/.
set -e

APP_DIR="/boot/home/config/non-packaged/apps/DWService"
ADDON_DIR="/boot/home/config/non-packaged/add-ons/input_server/devices"
BIN_DIR="/boot/home/config/non-packaged/bin"
LAUNCH_DIR="/boot/home/config/settings/launch"
SETTINGS_DIR="/boot/home/config/settings/DWService"
DESKBAR_DIR="/boot/home/config/non-packaged/data/deskbar/menu/Applications"
LOG_DIR="/boot/home/config/cache/DWService"
CONTROL_APP="$APP_DIR/BeRDAgent"
CONTROL_TEMP="$APP_DIR/.BeRDAgent.new.$$"
RUNTIME_PYTHON="$BIN_DIR/berd-python3"
RUNTIME_TEMP="$BIN_DIR/.berd-python3.new.$$"
APP_FLAGS_FILE="$LOG_DIR/.background-app-flags.$$"

cleanup()
{
	rm -f "$CONTROL_TEMP" "$RUNTIME_TEMP" "$APP_FLAGS_FILE"
}
trap cleanup EXIT HUP INT TERM

if [ "$(pwd)" != "$APP_DIR" ]; then
	echo "Run this script from $APP_DIR"
	exit 1
fi
if [ ! -x make/native/dwservice_remote_input ]; then
	echo "Missing make/native/dwservice_remote_input; run: cd make && python3 compile_haiku.py"
	exit 1
fi
if [ ! -x make/native/BeRDAgent ]; then
	echo "Missing make/native/BeRDAgent; run: cd make && python3 compile_haiku.py"
	exit 1
fi

mkdir -p "$ADDON_DIR" "$BIN_DIR" "$LAUNCH_DIR" "$SETTINGS_DIR" \
	"$DESKBAR_DIR" "$LOG_DIR"
chmod 700 "$SETTINGS_DIR" "$LOG_DIR"

# B_MULTIPLE_LAUNCH | B_BACKGROUND_APP is 0x5. A private copy of Haiku's tiny
# Python launcher preserves its normal multi-process behavior while preventing
# capture BApplications from becoming Deskbar entries. The process remains
# visible to process monitors and the system Python executable is not changed.
PYTHON_SOURCE=$(command -v python3 || true)
if [ -z "$PYTHON_SOURCE" ] || [ ! -x "$PYTHON_SOURCE" ]; then
	echo "Missing python3 runtime"
	exit 1
fi
printf '\005\000\000\000' > "$APP_FLAGS_FILE"
cp "$PYTHON_SOURCE" "$RUNTIME_TEMP"
chmod 755 "$RUNTIME_TEMP"
addattr -t mime BEOS:TYPE application/x-vnd.Be-elfexecutable "$RUNTIME_TEMP"
addattr -f "$APP_FLAGS_FILE" -c APPF BEOS:APP_FLAGS "$RUNTIME_TEMP"
mv -f "$RUNTIME_TEMP" "$RUNTIME_PYTHON"

cp make/native/dwservice_remote_input "$ADDON_DIR/dwservice_remote_input"
chmod 755 "$ADDON_DIR/dwservice_remote_input"
cp os_haiku/dwservice_agent.launch "$LAUNCH_DIR/dwservice_agent"
chmod 644 "$LAUNCH_DIR/dwservice_agent"
chmod 755 os_haiku/dwagent-haiku-service
chmod 755 os_haiku/berd-agent-control

# Remove only our own previous replicant before atomically replacing its image.
if [ -x "$CONTROL_APP" ]; then
	"$CONTROL_APP" --remove >/dev/null 2>&1 || true
fi
cp make/native/BeRDAgent "$CONTROL_TEMP"
chmod 755 "$CONTROL_TEMP"
addattr -t string BEOS:TYPE application/x-vnd.Be-elfexecutable "$CONTROL_TEMP"
addattr -t string BEOS:APP_SIG application/x-vnd.big-hill-BeRDAgent "$CONTROL_TEMP"
mv -f "$CONTROL_TEMP" "$CONTROL_APP"
if command -v mimeset >/dev/null 2>&1; then
	mimeset "$CONTROL_APP" >/dev/null 2>&1 || true
fi
# The short-lived native controller is a single-launch background application.
printf '\004\000\000\000' > "$APP_FLAGS_FILE"
addattr -f "$APP_FLAGS_FILE" -c APPF BEOS:APP_FLAGS "$CONTROL_APP"

# Migrate the old controller name without touching unrelated Deskbar items.
rm -f "$APP_DIR/DWService" "$DESKBAR_DIR/DWService"
ln -sf "$CONTROL_APP" "$DESKBAR_DIR/BeRD Agent"

if [ ! -s "$SETTINGS_DIR/input.token" ]; then
	python3 -c 'import secrets; print(secrets.token_hex(32))' > "$SETTINGS_DIR/input.token"
	chmod 600 "$SETTINGS_DIR/input.token"
fi

if ! "$CONTROL_APP" --install; then
	echo "Could not add BeRD Agent to Deskbar. Log in graphically and run this installer again."
	exit 1
fi

echo "Installed the BeRD Agent Deskbar icon, input add-on, and user launch service."
echo "Deskbar will reload the icon automatically at login."
echo "A reboot/login is required before a newly installed launch service is discovered."
