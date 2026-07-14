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
CONTROL_APP="$APP_DIR/DWService"

if [ "$(pwd)" != "$APP_DIR" ]; then
	echo "Run this script from $APP_DIR"
	exit 1
fi
if [ ! -x make/native/dwservice_remote_input ]; then
	echo "Missing make/native/dwservice_remote_input; run: cd make && python3 compile_haiku.py"
	exit 1
fi
if [ ! -x make/native/DWService ]; then
	echo "Missing make/native/DWService; run: cd make && python3 compile_haiku.py"
	exit 1
fi

mkdir -p "$ADDON_DIR" "$LAUNCH_DIR" "$SETTINGS_DIR" "$DESKBAR_DIR"
cp make/native/dwservice_remote_input "$ADDON_DIR/dwservice_remote_input"
chmod 755 "$ADDON_DIR/dwservice_remote_input"
cp os_haiku/dwservice_agent.launch "$LAUNCH_DIR/dwservice_agent"
chmod 644 "$LAUNCH_DIR/dwservice_agent"
chmod 755 os_haiku/dwagent-haiku-service
cp make/native/DWService "$CONTROL_APP"
chmod 755 "$CONTROL_APP"
addattr -t string BEOS:TYPE application/x-vnd.Be-elfexecutable "$CONTROL_APP"
addattr -t string BEOS:APP_SIG application/x-vnd.DWService-Control "$CONTROL_APP"
ln -sf "$CONTROL_APP" "$DESKBAR_DIR/DWService"

if [ ! -s "$SETTINGS_DIR/input.token" ]; then
	python3 -c 'import secrets; print(secrets.token_hex(32))' > "$SETTINGS_DIR/input.token"
	chmod 600 "$SETTINGS_DIR/input.token"
fi

echo "Installed DWService Haiku input add-on, user launch service, and Deskbar app."
echo "A reboot/login is required before the launch service is discovered."
