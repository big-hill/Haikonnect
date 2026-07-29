# -*- coding: utf-8 -*-
'''Build the native Haiku Deskbar replicant and installer application.

This Source Code Form is subject to the terms of the Mozilla
Public License, v. 2.0. If a copy of the MPL was not distributed
with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
'''
import os
import utils


class Compile:

    def get_name(self):
        return "os_haiku_control"

    def run(self):
        utils.info("BEGIN " + self.get_name())
        source = os.path.abspath(os.path.join(
            "..", "os_haiku_control", "src", "dwservicecontrol.cpp"))
        output = os.path.abspath(os.path.join(utils.PATHNATIVE, "Haikonnect"))
        command = (
            'g++ -std=c++17 -O2 -Wall -Wextra -o "{output}" '
            '"{source}" -lbe'
        ).format(output=output, source=source)
        if not utils.system_exec(command, "."):
            utils.info("ERROR " + self.get_name() + ": Compiler error.")
            return False
        utils.info("END " + self.get_name())
        return True


if __name__ == "__main__":
    raise SystemExit(0 if Compile().run() else 1)
