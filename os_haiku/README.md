# BeRD runtime for Haiku

This directory contains the local-service part of BeRD's DWService Agent port. The agent
runs in the logged-in Haiku user's session so that it can use `app_server` for
screen capture, clipboard access, and input delivery. It does not require an
SSH session after installation.

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

Install the compiler and development packages supplied by Haiku, plus
`libjpeg_turbo_devel` and `zlib_devel`. Then run:

```sh
cd /boot/home/config/non-packaged/apps/DWService/make
python3 compile_haiku.py
python3 create_config.py
```

`create_config.py` asks for the one-time installation code generated in the
DWService dashboard. Haiku disables upstream binary self-updates because
DWService does not publish Haiku native archives.

## Runtime gates

Before installing the input add-on, run the non-invasive capture gate from the
source root:

```sh
python3 os_haiku/doctor.py
```

Success requires all of the following:

- platform detection reports `haiku_x86_64`;
- `dwaglib.so`, the generic encoder, and the Haiku capture backend load;
- at least one monitor is returned;
- one complete frame is captured and hashed;
- cursor metadata can be read.

Install the input add-on and launch file:

```sh
./os_haiku/install-local.sh
```

The installer also adds a native **DWService** control application to the
Deskbar's Applications menu. It can start or stop the launch service, refresh
local status, open the DWService web dashboard, and open the local service log.

A reboot/login is required for `input_server` and the user launch daemon to
discover the new files. After reboot, verify that the agent is online and that
screen, pointer, keyboard, and clipboard control work from the DWService web
dashboard with SSH disconnected.

The post-reboot native gate also verifies that both registered input devices
accept the shared authentication token without generating any input:

```sh
python3 os_haiku/doctor.py --require-input
```

## Logs

The supervisor log is written to:

```text
/boot/home/config/cache/DWService/service.log
```

The agent's own log is written in `core/dwagent.log`.
