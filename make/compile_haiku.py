# -*- coding: utf-8 -*-
'''
This Source Code Form is subject to the terms of the Mozilla
Public License, v. 2.0. If a copy of the MPL was not distributed
with this file, You can obtain one at http://mozilla.org/MPL/2.0/.

Build only the components required by the Haiku DWService desktop port.
This intentionally uses Haiku system packages instead of DWService binary
dependency archives, which do not contain a haiku_x86_64 target.
'''
import compile_lib_core
import compile_lib_screencapture
import compile_lib_screencapture_haiku
import compile_os_haiku_control
import compile_os_haiku_input
import utils


def main():
    if not utils.is_haiku():
        raise SystemExit("compile_haiku.py must run on Haiku")
    utils.init_path(utils.PATHNATIVE)
    for module in (compile_lib_core, compile_lib_screencapture,
                   compile_lib_screencapture_haiku, compile_os_haiku_input,
                   compile_os_haiku_control):
        compiler = module.Compile()
        if not compiler.run():
            raise SystemExit("Haiku build failed in %s" % compiler.get_name())
    print("Haiku desktop components compiled in make/native")


if __name__ == "__main__":
    main()
