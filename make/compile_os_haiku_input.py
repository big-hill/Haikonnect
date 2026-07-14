# -*- coding: utf-8 -*-
'''
This Source Code Form is subject to the terms of the Mozilla
Public License, v. 2.0. If a copy of the MPL was not distributed
with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
'''
import os
import compile_generic


class Compile(compile_generic.Compile):

    def __init__(self):
        compile_generic.Compile.__init__(self, "os_haiku_input")

    def get_os_config(self, osn):
        if osn == "haiku":
            return {
                "outname": "dwservice_remote_input",
                "cpp_include_paths": [os.path.join("..", "lib_screencapture", "src", "haiku")],
                "cpp_library_paths": [],
                # BInputServerDevice is implemented and exported by the
                # input_server executable. Haiku's own Jamfiles link device
                # add-ons against that target rather than an installed
                # libinput_server.so (which does not exist).
                "other_resources": ["/boot/system/servers/input_server"],
                "libraries": ["be"],
                "cpp_compiler_flags": "-DOS_HAIKU_INPUT -std=c++17",
            }
        return None


if __name__ == "__main__":
    Compile().run()
