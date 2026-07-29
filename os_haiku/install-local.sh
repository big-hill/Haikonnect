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
CONTROL_APP="$APP_DIR/Haikonnect"
CONTROL_TEMP="$APP_DIR/.Haikonnect.new.$$"
RUNTIME_PYTHON="$BIN_DIR/haikonnect-python3"
RUNTIME_TEMP="$BIN_DIR/.haikonnect-python3.new.$$"
INPUT_ADDON="$ADDON_DIR/dwservice_remote_input"
INPUT_TEMP="$LOG_DIR/.dwservice_remote_input.new.$$"
INPUT_PREVIOUS="$LOG_DIR/.dwservice_remote_input.previous.$$"
APP_FLAGS_FILE="$LOG_DIR/.background-app-flags.$$"
LEGACY_CONTROL_APP="$APP_DIR/BeRDAgent"
LEGACY_RUNTIME_PYTHON="$BIN_DIR/berd-python3"
LEGACY_DESKBAR_LINK="$DESKBAR_DIR/BeRD Agent"
SERVICE="x-vnd.DWService-Agent"
SERVICE_WAS_RUNNING=false

cleanup()
{
	# If installation was interrupted while the live add-on was outside the
	# monitored directory, put it back so input_server can load it again.
	if [ -e "$INPUT_PREVIOUS" ] && [ ! -e "$INPUT_ADDON" ]; then
		mv "$INPUT_PREVIOUS" "$INPUT_ADDON"
	fi
	rm -f "$CONTROL_TEMP" "$RUNTIME_TEMP" "$INPUT_TEMP" \
		"$INPUT_PREVIOUS" "$APP_FLAGS_FILE"
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
if [ ! -x make/native/Haikonnect ]; then
	echo "Missing make/native/Haikonnect; run: cd make && python3 compile_haiku.py"
	exit 1
fi

mkdir -p "$ADDON_DIR" "$BIN_DIR" "$LAUNCH_DIR" "$SETTINGS_DIR" \
	"$DESKBAR_DIR" "$LOG_DIR"
chmod 700 "$SETTINGS_DIR" "$LOG_DIR"
chmod 755 os_haiku/haikonnect-agent-control

# Stop an active agent before replacing its private Python launcher. Preserve
# the user's previous running/stopped choice across an in-place upgrade.
if launch_roster info "$SERVICE" 2>/dev/null \
		| grep -q "running = bool(true)"; then
	SERVICE_WAS_RUNNING=true
	if ! os_haiku/haikonnect-agent-control stop; then
		echo "Could not stop Haikonnect safely before the upgrade."
		exit 1
	fi
fi

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

# Stage outside input_server's monitored add-on directory. A temporary file
# inside that directory can be loaded under its temporary name and then
# unloaded by the final rename, leaving remote input unavailable until reboot.
# Moving an existing image out first gives Haiku time to unregister it; moving
# the final image back in then triggers a clean, live reload.
cp make/native/dwservice_remote_input "$INPUT_TEMP"
chmod 755 "$INPUT_TEMP"
if [ -e "$INPUT_ADDON" ]; then
	mv "$INPUT_ADDON" "$INPUT_PREVIOUS"
	sleep 2
fi
mv "$INPUT_TEMP" "$INPUT_ADDON"
sleep 2
rm -f "$INPUT_PREVIOUS"
cp os_haiku/dwservice_agent.launch "$LAUNCH_DIR/dwservice_agent"
chmod 644 "$LAUNCH_DIR/dwservice_agent"
chmod 755 os_haiku/dwagent-haiku-service

# Remove only our own current and former replicants before atomically replacing
# the controller image. The former binary is used first when it is available.
if [ -x "$LEGACY_CONTROL_APP" ]; then
	"$LEGACY_CONTROL_APP" --remove >/dev/null 2>&1 || true
fi
if [ -x "$CONTROL_APP" ]; then
	"$CONTROL_APP" --remove >/dev/null 2>&1 || true
fi
cp make/native/Haikonnect "$CONTROL_TEMP"
chmod 755 "$CONTROL_TEMP"
addattr -t string BEOS:TYPE application/x-vnd.Be-elfexecutable "$CONTROL_TEMP"
addattr -t string BEOS:APP_SIG application/x-vnd.big-hill-Haikonnect "$CONTROL_TEMP"
mv -f "$CONTROL_TEMP" "$CONTROL_APP"
if command -v mimeset >/dev/null 2>&1; then
	mimeset "$CONTROL_APP" >/dev/null 2>&1 || true
fi
# The short-lived native controller is a single-launch background application.
printf '\004\000\000\000' > "$APP_FLAGS_FILE"
addattr -f "$APP_FLAGS_FILE" -c APPF BEOS:APP_FLAGS "$CONTROL_APP"

# Remove the former BeRD identity without touching unrelated Deskbar items.
"$CONTROL_APP" --remove-legacy >/dev/null 2>&1 || true
rm -f "$APP_DIR/DWService" "$DESKBAR_DIR/DWService" \
	"$LEGACY_CONTROL_APP" "$LEGACY_RUNTIME_PYTHON" "$LEGACY_DESKBAR_LINK"
ln -sf "$CONTROL_APP" "$DESKBAR_DIR/Haikonnect"

if [ ! -s "$SETTINGS_DIR/input.token" ]; then
	python3 -c 'import secrets; print(secrets.token_hex(32))' > "$SETTINGS_DIR/input.token"
	chmod 600 "$SETTINGS_DIR/input.token"
fi

if ! "$CONTROL_APP" --install; then
	echo "Could not add Haikonnect to Deskbar. Log in graphically and run this installer again."
	exit 1
fi

if [ "$SERVICE_WAS_RUNNING" = true ]; then
	if ! os_haiku/haikonnect-agent-control start; then
		echo "Haikonnect was installed, but its previously running service could not be restarted."
		exit 1
	fi
fi

echo "Installed the Haikonnect Deskbar icon, input add-on, and user launch service."
echo "The input add-on was exposed under its final name for a clean live reload."
echo "Deskbar will reload the icon automatically at login."
echo "A reboot/login may still be required before a newly installed launch service is discovered."
