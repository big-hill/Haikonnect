# Contributing to BeRD

Thank you for helping improve the Haiku port.

## Ground rules

- Base changes on the `haiku-port` branch unless a maintainer requests another
  target.
- Keep upstream-compatible behavior outside Haiku-specific branches.
- Never commit `core/config.json`, installation codes, agent keys, access
  tokens, logs, native build output or personal host details.
- New source files should use the MPL-2.0 notice unless they derive from code
  under another license.
- Do not add DWService or Haiku logos without documented permission and the
  appropriate trademark review.

## Checks

Run the portable test suite on any Python 3 host:

```sh
python3 -m unittest discover -s tests -v
python3 -m compileall -q core app_desktop app_filesystem make os_haiku
```

On Haiku, also run:

```sh
cd make && python3 compile_haiku.py && cd ..
python3 os_haiku/doctor.py
python3 os_haiku/doctor.py --require-input
```

For runtime changes, test a clean login/reboot and verify Screen, Filesystem,
pointer, keyboard and clipboard from the web dashboard with SSH disconnected.
Describe the tested Haiku revision, architecture and Python version in the pull
request.
