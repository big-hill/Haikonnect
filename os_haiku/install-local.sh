#!/bin/sh
# This Source Code Form is subject to the terms of the Mozilla Public License,
# v. 2.0. If a copy of the MPL was not distributed with this file, You can
# obtain one at http://mozilla.org/MPL/2.0/.
set -e

APP_DIR="/boot/home/config/non-packaged/apps/DWService"
ADDON_DIR="/boot/home/config/non-packaged/add-ons/input_server/devices"
LAUNCH_DIR="/boot/home/config/settings/launch"
SETTINGS_DIR="/boot/home/config/settings/DWService"
DESKBAR_DIR="/boot/home/config/non-packaged/data/deskbar/menu/Applications"
LOG_DIR="/boot/home/config/cache/DWService"
CONTROL_APP="$APP_DIR/BeRDAgent"
CONTROL_TEMP="$APP_DIR/.BeRDAgent.new.$$"

cleanup()
{
	rm -f "$CONTROL_TEMP"
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

mkdir -p "$ADDON_DIR" "$LAUNCH_DIR" "$SETTINGS_DIR" "$DESKBAR_DIR" "$LOG_DIR"
chmod 700 "$SETTINGS_DIR" "$LOG_DIR"
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
