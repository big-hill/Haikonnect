# Haikonnect

**Remote access for Haiku**

Haikonnect is an experimental, unofficial Haiku port of the
[DWService Agent](https://github.com/dwservice/agent). It provides native
remote screen viewing and control from the DWService web dashboard while the
agent runs locally on Haiku. No SSH session or companion server is required
after installation.

> Haikonnect is not affiliated with or endorsed by DWSNET s.r.l. or Haiku,
> Inc. DWService remains the upstream project and hosted service.

## Current status

The functional port has been built and tested end to end on two physical
x86_64 Haiku systems:

- Haiku nightly `hrev57937+129` and R1/beta6 development `hrev59917`;
- Python 3.10.20;
- native screen capture and cursor metadata;
- remote pointer and keyboard input through a Haiku input-server add-on;
- clipboard, Filesystem and Shell access;
- a per-user launch service and automatically restored native Deskbar icon;
- clean restart and reconnect after reboot.

The independent clean-machine test used public commit `a964b84`, followed this
README from clone through install, and exercised the DWService dashboard with
SSH disconnected. Later credential and lifecycle hardening must still pass the
native Haiku gates whenever those files change.

It is still an early source-based port. Only x86_64 has been tested, no HPKG is
published yet, and upstream binary self-updates are deliberately disabled.

## Architecture

Haikonnect keeps the DWService protocol and Python agent intact and adds
Haiku-native adapters around it:

```text
DWService dashboard
        |
DWService network service
        |
Python agent on Haiku
   |                 |
BScreen capture      authenticated BInputServerDevice add-on
   |                 |
app_server           input_server
```

The local input channel uses a randomly generated 256-bit token stored with
user-only permissions. Agent configuration is written atomically with mode
`0600`; `doctor.py` rejects an existing configuration with unsafe permissions.
Agent credentials and local tokens are runtime state. They are ignored by Git
and must never be committed.

## Requirements

The following package set was verified on `hrev59917`:

```sh
pkgman install git python3.10 python3_command_compat gcc haiku_devel \
  libjpeg_turbo_devel zlib_devel
```

It provides Git, Python 3 (`python3`), GCC/G++ with C++17 support and the JPEG
and zlib development headers used by the native build.

## Build and install

The current scripts expect this exact source location:

```sh
mkdir -p /boot/home/config/non-packaged/apps
git clone https://github.com/big-hill/Haikonnect.git \
  /boot/home/config/non-packaged/apps/DWService
cd /boot/home/config/non-packaged/apps/DWService
```

Build the native components:

```sh
cd make
python3 compile_haiku.py
cd ..
```

Create a one-time installation code in the DWService dashboard, then configure
the local agent:

```sh
cd make
python3 create_config.py
cd ..
```

The check prints periodic progress and has a 120-second total timeout. A
successful configuration is atomically installed as `core/config.json` with
mode `0600`; credential values are never printed.

Run the non-invasive checks and install the user service, input add-on and
Deskbar icon:

```sh
python3 os_haiku/doctor.py
./os_haiku/install-local.sh
```

The installer adds **Haikonnect** as a real Deskbar replicant. Deskbar restores
it automatically on subsequent logins. Its indicator is green when the local
agent is running and configured, yellow when configuration is missing, and red
when the agent is stopped or reports an error. Click it for Dashboard,
Start/Stop, Log and About actions. An in-place installation also removes the
former BeRD Deskbar identity and private Python launcher.

The installer stages the input add-on outside Haiku's monitored add-on
directory, then exposes it under its final name so a running `input_server` can
load or replace it cleanly. A reboot or logout/login may still be needed for
`launch_daemon` to discover a newly installed user service. Then verify both
authenticated input devices without generating input:

```sh
python3 os_haiku/doctor.py --require-input
```

Open the DWService dashboard and test Screen, Filesystem and Shell. Screen,
pointer, keyboard and clipboard control should work with SSH disconnected.

## Update, reconfigure and remove

Updating remains source-based. Stop the agent, fast-forward the checkout,
rebuild, reinstall and start it again:

```sh
./os_haiku/haikonnect-agent-control stop
git pull --ff-only
cd make
python3 compile_haiku.py
cd ..
./os_haiku/install-local.sh
./os_haiku/haikonnect-agent-control start
```

For a new DWService identity, first create a new one-time code in the dashboard.
The following command keeps a private rollback copy outside the Git checkout and
does not replace the active configuration until server validation succeeds:

```sh
mkdir -p /boot/home/config/settings/DWService
chmod 700 /boot/home/config/settings/DWService
./os_haiku/haikonnect-agent-control stop
cd make
python3 create_config.py --replace \
  --backup /boot/home/config/settings/DWService/config.json.backup
cd ..
./os_haiku/haikonnect-agent-control start
```

Restore that backup with:

```sh
./os_haiku/haikonnect-agent-control stop
cd make
python3 create_config.py --restore \
  /boot/home/config/settings/DWService/config.json.backup
cd ..
./os_haiku/haikonnect-agent-control start
```

Remove installed runtime components while preserving credentials, token, logs
and the source checkout:

```sh
./os_haiku/uninstall-local.sh
```

`./os_haiku/uninstall-local.sh --purge` additionally deletes Haikonnect's
credentials, local input token, logs, IPC state and any backup inside the
Haikonnect settings directory. It deliberately leaves the source checkout in
place. Copy any backup that must survive a purge to another private location
first.

## Runtime files

| Purpose | Path |
| --- | --- |
| Source and agent | `/boot/home/config/non-packaged/apps/DWService` |
| Native Haikonnect app | `/boot/home/config/non-packaged/apps/DWService/Haikonnect` |
| Private Python launcher | `/boot/home/config/non-packaged/bin/haikonnect-python3` |
| Agent configuration | `core/config.json` (Git-ignored, mode `0600`) |
| Optional reconfigure backup | `/boot/home/config/settings/DWService/config.json.backup` |
| Input token | `/boot/home/config/settings/DWService/input.token` |
| Launch descriptor | `/boot/home/config/settings/launch/dwservice_agent` |
| Input add-on | `/boot/home/config/non-packaged/add-ons/input_server/devices/dwservice_remote_input` |
| Supervisor log | `/boot/home/config/cache/DWService/service.log` |
| Tray status | `/boot/home/config/cache/DWService/agent.status` |
| Agent log | `core/dwagent.log` |

## Known limitations

- Modifier shortcuts depend on the hosted browser client delivering their
  flags. In the current DWService client on macOS, Option/Alt plus a character
  is normalized before it reaches the agent, and browser-reserved Ctrl
  combinations may also be intercepted. Haikonnect's native input path handles
  the modifiers when they are delivered; use the context menu, the
  application's Edit menu or DWService's explicit keystroke sender otherwise.
- The hosted dashboard currently shows `?` instead of a Haiku OS logo beside
  the device name. The agent reports `Haiku`, but icon selection is server-side
  and the upstream protocol defines numeric codes only for Linux, Windows and
  macOS. Dashboard integration for a Haiku logo is not in place yet;
  Haikonnect does not masquerade as Linux.
- Audio capture, privacy-screen mode and multi-monitor Haiku setups are not yet
  validated.
- Installation is per-user and currently requires the fixed source path above.
- Updating is manual because upstream does not publish Haiku native archives.
- The Python agent runs without its own window or Deskbar application entry.
  It remains visible in ProcessController/Team Monitor by design; Haikonnect
  does not conceal remote-control processes from the operating system or local
  user.
  The installer achieves this with a private, background-flagged copy of the
  installed Python launcher; it does not modify or bundle the system runtime.

## Development

Run the portable checks before submitting changes:

```sh
python3 -m unittest discover -s tests -v
python3 -m compileall -q core app_desktop app_filesystem app_shell make os_haiku tests
```

Native compilation and final doctor checks must run on Haiku. See
[CONTRIBUTING.md](CONTRIBUTING.md) and [os_haiku/README.md](os_haiku/README.md).

## Provenance, licensing and marks

This fork is based on DWService Agent commit
`0e4f659e7b7e4150504251d49e2acbc09f98db5f`. The repository is multi-licensed:
the agent and Haikonnect additions are primarily MPL-2.0, while bundled
third-party components retain their own licenses. Read
[LICENSE.md](LICENSE.md) and [NOTICE.md](NOTICE.md) before redistribution.

DWService and Haiku names and logos belong to their respective owners. No
upstream logo is bundled or claimed by this project.

Haikonnect was “vibed” through an iterative `/goal` workflow with GPT-5.6 Sol,
then compiled and runtime-verified on real Haiku hardware. AI assistance does
not replace review, testing or license compliance.
