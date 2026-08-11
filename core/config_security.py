"""Private, atomic file helpers for agent credentials.

This Source Code Form is subject to the terms of the Mozilla
Public License, v. 2.0. If a copy of the MPL was not distributed
with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
"""

import os
import stat
import tempfile


PRIVATE_FILE_MODE = stat.S_IRUSR | stat.S_IWUSR


def file_mode(path):
    """Return a file's permission bits, or None when it does not exist."""
    try:
        return stat.S_IMODE(os.lstat(path).st_mode)
    except OSError:
        return None


def mode_text(mode):
    if mode is None:
        return "missing"
    return format(mode, "04o")


def is_private_file(path):
    """Require a regular, non-symlink file with exactly owner read/write."""
    try:
        info = os.lstat(path)
    except OSError:
        return False
    return stat.S_ISREG(info.st_mode) \
        and stat.S_IMODE(info.st_mode) == PRIVATE_FILE_MODE


def enforce_private_file(path):
    """Set an existing regular file to 0600 and reject symbolic links."""
    info = os.lstat(path)
    if not stat.S_ISREG(info.st_mode):
        raise ValueError("Credential path is not a regular file: " + path)
    os.chmod(path, PRIVATE_FILE_MODE)
    if not is_private_file(path):
        raise OSError("Could not set credential file mode to 0600: " + path)


def atomic_write_private(path, data):
    """Atomically replace path with bytes stored in a mode-0600 file."""
    if isinstance(data, str):
        data = data.encode("utf-8")
    elif not isinstance(data, (bytes, bytearray)):
        raise TypeError("Private file contents must be text or bytes")

    destination = os.path.abspath(path)
    directory = os.path.dirname(destination)
    if not os.path.isdir(directory):
        raise OSError("Credential directory does not exist: " + directory)

    descriptor = None
    temporary = None
    try:
        descriptor, temporary = tempfile.mkstemp(
            prefix="." + os.path.basename(destination) + ".",
            suffix=".tmp",
            dir=directory,
        )
        if hasattr(os, "fchmod"):
            os.fchmod(descriptor, PRIVATE_FILE_MODE)
        else:
            os.chmod(temporary, PRIVATE_FILE_MODE)

        output = os.fdopen(descriptor, "wb")
        descriptor = None
        try:
            output.write(bytes(data))
            output.flush()
            os.fsync(output.fileno())
        finally:
            output.close()

        os.chmod(temporary, PRIVATE_FILE_MODE)
        if hasattr(os, "replace"):
            os.replace(temporary, destination)
        else:
            if os.path.exists(destination):
                os.remove(destination)
            os.rename(temporary, destination)
        temporary = None
        enforce_private_file(destination)
    finally:
        if descriptor is not None:
            os.close(descriptor)
        if temporary is not None and os.path.exists(temporary):
            os.remove(temporary)
