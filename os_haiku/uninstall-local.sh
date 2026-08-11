#!/bin/sh
# This Source Code Form is subject to the terms of the Mozilla Public License,
# v. 2.0. If a copy of the MPL was not distributed with this file, You can
# obtain one at http://mozilla.org/MPL/2.0/.
set -e

APP_DIR="/boot/home/config/non-packaged/apps/DWService"
CONFIG_FILE="$APP_DIR/core/config.json"
AGENT_LOG="$APP_DIR/core/dwagent.log"
SHAREDMEM_DIR="$APP_DIR/core/sharedmem"
ADDON_DIR="/boot/home/config/non-packaged/add-ons/input_server/devices"
BIN_DIR="/boot/home/config/non-packaged/bin"
LAUNCH_DIR="/boot/home/config/settings/launch"
SETTINGS_DIR="/boot/home/config/settings/DWService"
DESKBAR_DIR="/boot/home/config/non-packaged/data/deskbar/menu/Applications"
LOG_DIR="/boot/home/config/cache/DWService"
CONTROL_APP="$APP_DIR/Haikonnect"
CONTROL_HELPER="$APP_DIR/os_haiku/haikonnect-agent-control"
RUNTIME_PYTHON="$BIN_DIR/haikonnect-python3"
INPUT_ADDON="$ADDON_DIR/dwservice_remote_input"
LAUNCH_FILE="$LAUNCH_DIR/dwservice_agent"
DESKBAR_LINK="$DESKBAR_DIR/Haikonnect"
SERVICE="x-vnd.DWService-Agent"
PURGE=false

usage()
{
	echo "Usage: $0 [--purge]"
	echo "  default  remove installed code and preserve credentials/logs"
	echo "  --purge  also delete Haikonnect credentials, token, logs and IPC state"
}

if [ "$#" -gt 1 ]; then
	usage >&2
	exit 2
fi

case "${1:-}" in
	"") ;;
	--purge) PURGE=true ;;
	--help|-h)
		usage
		exit 0
		;;
	*)
		usage >&2
		exit 2
		;;
esac

if [ "$(uname -s)" != "Haiku" ]; then
	echo "uninstall-local.sh must run on Haiku." >&2
	exit 1
fi

if launch_roster info "$SERVICE" >/dev/null 2>&1; then
	if [ -x "$CONTROL_HELPER" ]; then
		if ! "$CONTROL_HELPER" stop; then
			echo "Haikonnect did not stop cleanly; no files were removed." >&2
			exit 1
		fi
	else
		if ! launch_roster stop "$SERVICE" >/dev/null 2>&1; then
			echo "Haikonnect did not stop cleanly; no files were removed." >&2
			exit 1
		fi
	fi
fi

if [ -x "$CONTROL_APP" ]; then
	"$CONTROL_APP" --remove >/dev/null 2>&1 || true
fi

rm -f "$DESKBAR_LINK" "$LAUNCH_FILE" "$INPUT_ADDON" \
	"$RUNTIME_PYTHON" "$LOG_DIR/agent.status" "$LOG_DIR/supervisor.pid"
rm -f "$CONTROL_APP"

if [ "$PURGE" = true ]; then
	rm -f "$CONFIG_FILE" "$AGENT_LOG"
	rm -rf "$SHAREDMEM_DIR" "$SETTINGS_DIR" "$LOG_DIR"
	echo "Purged Haikonnect credentials, backups, token, logs and IPC state."
else
	echo "Preserved core/config.json, the input token and logs for reinstall/rollback."
fi

echo "Removed the Haikonnect service, input add-on, runtime launcher and Deskbar item."
echo "The source checkout remains at $APP_DIR."
echo "Log out or reboot if input_server or launch_daemon still shows removed components."
