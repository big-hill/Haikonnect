/*
This Source Code Form is subject to the terms of the Mozilla
Public License, v. 2.0. If a copy of the MPL was not distributed
with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
*/
#if defined OS_HAIKU_CAPTURE
#ifndef SCREENCAPTURENATIVEHAIKU_H
#define SCREENCAPTURENATIVEHAIKU_H

#include <Application.h>
#include <Bitmap.h>
#include <Clipboard.h>
#include <FindDirectory.h>
#include <Input.h>
#include <InterfaceDefs.h>
#include <Locker.h>
#include <Message.h>
#include <Path.h>
#include <Screen.h>

#include <stdint.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>

#include <codecvt>
#include <fstream>
#include <locale>
#include <string>

#include "../common/extern_v2.h"
#include "dwserviceinput.h"

struct HaikuCaptureSession {
	RGB_IMAGE* image;
	BRect frame;
	bool firstFrame;
};

#endif
#endif
