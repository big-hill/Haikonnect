# Haikonnect runtime for Haiku

This directory contains the local-service part of Haikonnect's DWService Agent
port. The agent runs in the logged-in Haiku user's session so that it can use
`app_server` for screen capture, clipboard access, and input delivery. It does
not require an SSH session after installation.

## Runtime layout

The source tree is installed at:

```text
/boot/home/config/non-packaged/apps/DWService
```

The native build outputs remain in `make/native` because the checkout contains
`core/.srcmode`. The input-server add-on is copied to:

```text
/boot/home/config/non-packaged/add-ons/input_server/devices/dwservice_remote_input
```

The per-user launch description is installed at:

```text
/boot/home/config/settings/launch/dwservice_agent
```

## Build and configure

Install the package set verified on `hrev59917`:

```sh
pkgman install git python3.10 python3_command_compat gcc haiku_devel \
  libjpeg_turbo_devel zlib_devel
```

Then run:

```sh
cd /boot/home/config/non-packaged/apps/DWService/make
python3 compile_haiku.py
python3 create_config.py
```

`create_config.py` asks for the one-time installation code generated in the
DWService dashboard. It reports progress during the network check, enforces a
120-second total timeout and installs `core/config.json` atomically with mode
`0600`. Haiku disables upstream binary self-updates because DWService does not
publish Haiku native archives.

## Runtime gates

Before installing the input add-on, run the non-invasive capture gate from the
source root:

```sh
python3 os_haiku/doctor.py
```

Success requires all of the following:

- an existing agent configuration is either absent or a regular mode-`0600`
  file;
- platform detection reports `haiku_x86_64`;
- `dwaglib.so`, the generic encoder, and the Haiku capture backend load;
- at least one monitor is returned;
- one complete frame is captured and hashed;
- cursor metadata can be read.

Install the input add-on and launch file:

```sh
./os_haiku/install-local.sh
```

The installer adds a native **Haikonnect** replicant directly to Deskbar and an
Applications-menu shortcut that can restore it if needed. Deskbar persists and
reloads the replicant automatically at login. The indicator is green when the
agent is configured and running, yellow when it is not configured, and red
when it is stopped or has failed. Its menu opens the dashboard or log, starts
or stops the service, and shows project information.

The Python agent is launched with no terminal, window or separate Deskbar app
entry. It is intentionally still visible in Haiku's ProcessController/Team
Monitor so the remote-control process is never concealed from the local user.
The installer copies Haiku's small Python launcher to the per-user bin folder
and marks only that copy as `B_BACKGROUND_APP`; system Python is not modified.

The installer stages the input add-on outside Haiku's monitored add-on
directory and then moves it into place under its final name. This makes a
running `input_server` load or replace it cleanly. A reboot/login may still be
required for the user launch daemon to discover a newly installed service.
Afterward, verify that the agent is online and that screen, pointer, keyboard,
and clipboard control work from the DWService web dashboard with SSH
disconnected.

The post-reboot native gate also verifies that both registered input devices
accept the shared authentication token without generating any input:

```sh
python3 os_haiku/doctor.py --require-input
```

## Update, reconfigure and uninstall

For an update, stop the service, use `git pull --ff-only`, rebuild with
`compile_haiku.py`, rerun `install-local.sh`, and start the service again. The
generated root `Haikonnect` binary and `core/sharedmem/` runtime state are
ignored so normal installation does not make `git status` dirty.

To replace the DWService identity safely, stop the agent and run:

```sh
cd /boot/home/config/non-packaged/apps/DWService/make
python3 create_config.py --replace \
  --backup /boot/home/config/settings/DWService/config.json.backup
```

The current configuration remains active until the new installation code is
validated. Both the new file and backup are mode `0600`. Restore the backup
with `python3 create_config.py --restore` followed by its path.

Remove installed components but preserve credentials, token and logs with:

```sh
cd /boot/home/config/non-packaged/apps/DWService
./os_haiku/uninstall-local.sh
```

Pass `--purge` to also delete `core/config.json`, the local input token, logs,
IPC state and backups inside the Haikonnect settings directory. The source
checkout itself is never deleted. Copy any backup that must survive a purge to
another private location first. Logout or reboot may be required for
`input_server` and `launch_daemon` to forget removed components.

## Logs

The supervisor log is written to:

```text
/boot/home/config/cache/DWService/service.log
```

The supervisor atomically publishes the current child process state for the
Deskbar replicant at:

```text
/boot/home/config/cache/DWService/agent.status
```

It also publishes its own numeric team ID in the same private directory. The
`haikonnect-agent-control` helper uses only these service-owned IDs when
Start/Stop is selected; it never searches for or signals processes by a broad
name match.

The agent's own log is written in `core/dwagent.log`.
