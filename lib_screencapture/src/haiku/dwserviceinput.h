/*
This Source Code Form is subject to the terms of the Mozilla
Public License, v. 2.0. If a copy of the MPL was not distributed
with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
*/
#ifndef DWSERVICE_HAIKU_INPUT_H
#define DWSERVICE_HAIKU_INPUT_H

#include <SupportDefs.h>

static const uint32 kDWServiceInputCommand = 'DWRI';
static const int32 kDWServiceInputVersion = 1;
static const char* const kDWServiceKeyboardName = "DWService Remote Keyboard";
static const char* const kDWServicePointerName = "DWService Remote Pointer";

enum DWServiceInputKind {
	kDWServiceMouse = 1,
	kDWServiceKeyboard = 2
};

#endif
