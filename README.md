# BeRD

BeRD is an experimental, unofficial Haiku port of the
[DWService Agent](https://github.com/dwservice/agent). It provides native
remote screen viewing and control from the DWService web dashboard while the
agent runs locally on Haiku. No SSH session or companion server is required
after installation.

> BeRD is not affiliated with or endorsed by DWSNET s.r.l. DWService remains
> the upstream project and hosted service.

## Current status

The port has been compiled and runtime-tested on real x86_64 Haiku hardware:

- Haiku nightly `hrev57937+129`;
- Python 3.10.20;
- native screen capture and cursor metadata;
- remote pointer and keyboard input through a Haiku input-server add-on;
- clipboard integration and filesystem access;
- a per-user launch service and automatically restored native Deskbar icon;
- clean restart and reconnect after reboot.

It is still an early source-based port. Only x86_64 has been tested, no HPKG is
published yet, and upstream binary self-updates are deliberately disabled.

## Architecture

BeRD keeps the DWService protocol and Python agent intact and adds Haiku-native
adapters around it:

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
user-only permissions. Agent credentials and local tokens are runtime state;
they are ignored by Git and must never be committed.

## Requirements

Install the Haiku packages that provide Git, Python 3 (`python3`), GCC/G++ with
C++17 support, `libjpeg_turbo_devel` and `zlib_devel`.

## Build and install

The current scripts expect this exact source location:

```sh
mkdir -p /boot/home/config/non-packaged/apps
git clone https://github.com/big-hill/BeRD.git \
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

Run the non-invasive checks and install the user service, input add-on and
Deskbar icon:

```sh
python3 os_haiku/doctor.py
./os_haiku/install-local.sh
```

The installer adds **BeRD Agent** as a real Deskbar replicant. Deskbar restores
it automatically on subsequent logins. Its indicator is green when the local
agent is running and configured, yellow when configuration is missing, and red
when the agent is stopped or reports an error. Click it for Dashboard,
Start/Stop, Log and About actions.

Reboot or log out and in so `input_server` and `launch_daemon` discover the new
components. Then verify both authenticated input devices without generating
input:

```sh
python3 os_haiku/doctor.py --require-input
```

Open the DWService dashboard and test Screen and Filesystem. Screen, pointer,
keyboard and clipboard control should work with SSH disconnected.

## Runtime files

| Purpose | Path |
| --- | --- |
| Source and agent | `/boot/home/config/non-packaged/apps/DWService` |
| Agent configuration | `core/config.json` (Git-ignored) |
| Input token | `/boot/home/config/settings/DWService/input.token` |
| Launch descriptor | `/boot/home/config/settings/launch/dwservice_agent` |
| Input add-on | `/boot/home/config/non-packaged/add-ons/input_server/devices/dwservice_remote_input` |
| Supervisor log | `/boot/home/config/cache/DWService/service.log` |
| Tray status | `/boot/home/config/cache/DWService/agent.status` |
| Agent log | `core/dwagent.log` |

## Known limitations

- The dashboard may show a generic or missing platform icon. The agent reports
  `Haiku`, but icon selection is server-side and the upstream protocol defines
  numeric codes only for Linux, Windows and macOS. BeRD does not masquerade as
  Linux.
- Audio capture, privacy-screen mode and multi-monitor Haiku setups are not yet
  validated.
- Installation is per-user and currently requires the fixed source path above.
- Updating is manual because upstream does not publish Haiku native archives.
- The Python agent runs without its own window or Deskbar application entry.
  It remains visible in ProcessController/Team Monitor by design; BeRD does not
  conceal remote-control processes from the operating system or local user.

## Development

Run the portable checks before submitting changes:

```sh
python3 -m unittest discover -s tests -v
python3 -m compileall -q core app_desktop app_filesystem make os_haiku
```

Native compilation and final doctor checks must run on Haiku. See
[CONTRIBUTING.md](CONTRIBUTING.md) and [os_haiku/README.md](os_haiku/README.md).

## Provenance, licensing and marks

This fork is based on DWService Agent commit
`0e4f659e7b7e4150504251d49e2acbc09f98db5f`. The repository is multi-licensed:
the agent and BeRD additions are primarily MPL-2.0, while bundled third-party
components retain their own licenses. Read [LICENSE.md](LICENSE.md) and
[NOTICE.md](NOTICE.md) before redistribution.

DWService and Haiku names and logos belong to their respective owners. No
upstream logo is bundled or claimed by this project.

BeRD was “vibed” through an iterative `/goal` workflow with GPT-5.6 Sol, then
compiled and runtime-verified on real Haiku hardware. AI assistance does not
replace review, testing or license compliance.
