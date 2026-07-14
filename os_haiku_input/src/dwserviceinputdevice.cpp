/*
This Source Code Form is subject to the terms of the Mozilla
Public License, v. 2.0. If a copy of the MPL was not distributed
with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
*/
#if defined OS_HAIKU_INPUT

#include <InputServerDevice.h>

#include <FindDirectory.h>
#include <InterfaceDefs.h>
#include <Message.h>
#include <Path.h>
#include <Point.h>

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include <fstream>
#include <new>
#include <string>

#include "dwserviceinput.h"


class DWServiceInputDevice : public BInputServerDevice {
public:
	DWServiceInputDevice()
		:
		fButtons(0),
		fMousePosition(0, 0)
	{
		uint32 buttons = 0;
		get_mouse(&fMousePosition, &buttons);
		fButtons = buttons;
	}

	virtual status_t InitCheck()
	{
		static input_device_ref keyboard = {
			(char*)kDWServiceKeyboardName, B_KEYBOARD_DEVICE, this
		};
		static input_device_ref pointer = {
			(char*)kDWServicePointerName, B_POINTING_DEVICE, this
		};
		static input_device_ref* devices[] = {&keyboard, &pointer, NULL};
		return RegisterDevices(devices);
	}

	virtual status_t Start(const char*, void*)
	{
		return B_OK;
	}

	virtual status_t Stop(const char*, void*)
	{
		return B_OK;
	}

	virtual status_t Control(const char*, void*, uint32 command,
		BMessage* message)
	{
		if (command != kDWServiceInputCommand || message == NULL)
			return B_BAD_VALUE;
		if (message->GetInt32("version", 0) != kDWServiceInputVersion)
			return B_BAD_VALUE;
		const char* suppliedToken = message->GetString("token", "");
		std::string expectedToken = _InputToken();
		if (expectedToken.size() != 64 || expectedToken != suppliedToken)
			return B_NOT_ALLOWED;
		switch (message->GetInt32("kind", 0)) {
			case kDWServiceMouse:
				return _HandleMouse(*message);
			case kDWServiceKeyboard:
				return _HandleKeyboard(*message);
			default:
				return B_BAD_VALUE;
		}
	}

private:
	std::string _InputToken() const
	{
		BPath path;
		if (find_directory(B_USER_SETTINGS_DIRECTORY, &path) != B_OK
			|| path.Append("DWService/input.token") != B_OK)
			return std::string();
		std::ifstream stream(path.Path());
		std::string token;
		std::getline(stream, token);
		return token;
	}

	uint32 _Modifiers(const BMessage& request) const
	{
		uint32 result = 0;
		if (request.GetBool("ctrl", false))
			result |= B_CONTROL_KEY | B_LEFT_CONTROL_KEY;
		if (request.GetBool("alt", false))
			result |= B_OPTION_KEY | B_LEFT_OPTION_KEY;
		if (request.GetBool("shift", false))
			result |= B_SHIFT_KEY | B_LEFT_SHIFT_KEY;
		if (request.GetBool("command", false))
			result |= B_COMMAND_KEY | B_LEFT_COMMAND_KEY;
		return result;
	}

	status_t _EnqueueMouse(uint32 what, int32 clicks, uint32 modifiers)
	{
		BMessage* event = new(std::nothrow) BMessage(what);
		if (event == NULL)
			return B_NO_MEMORY;
		event->AddInt64("when", system_time());
		event->AddPoint("where", fMousePosition);
		event->AddInt32("buttons", fButtons);
		event->AddInt32("modifiers", modifiers);
		if (clicks > 0)
			event->AddInt32("clicks", clicks);
		status_t status = EnqueueMessage(event);
		if (status != B_OK)
			delete event;
		return status;
	}

	status_t _SetButtons(uint32 buttons, int32 clicks, uint32 modifiers)
	{
		for (uint32 mask = 1; mask <= 4; mask <<= 1) {
			bool wasDown = (fButtons & mask) != 0;
			bool isDown = (buttons & mask) != 0;
			if (wasDown == isDown)
				continue;
			if (isDown)
				fButtons |= mask;
			else
				fButtons &= ~mask;
			status_t status = _EnqueueMouse(isDown ? B_MOUSE_DOWN : B_MOUSE_UP,
				clicks, modifiers);
			if (status != B_OK)
				return status;
		}
		return B_OK;
	}

	status_t _HandleMouse(const BMessage& request)
	{
		int32 x = request.GetInt32("x", -1);
		int32 y = request.GetInt32("y", -1);
		int32 button = request.GetInt32("button", -1);
		int32 wheel = request.GetInt32("wheel", 0);
		uint32 modifiers = _Modifiers(request);
		if (x >= 0 && y >= 0) {
			fMousePosition.Set(x, y);
			status_t status = _EnqueueMouse(B_MOUSE_MOVED, 0, modifiers);
			if (status != B_OK)
				return status;
		}
		if (button == 64) {
			status_t status = _SetButtons(fButtons | 1, 1, modifiers);
			if (status == B_OK)
				status = _SetButtons(fButtons & ~1, 1, modifiers);
			if (status != B_OK)
				return status;
		} else if (button == 128) {
			for (int i = 0; i < 2; i++) {
				status_t status = _SetButtons(fButtons | 1, i + 1, modifiers);
				if (status == B_OK)
					status = _SetButtons(fButtons & ~1, i + 1, modifiers);
				if (status != B_OK)
					return status;
			}
		} else if (button >= 0) {
			status_t status = _SetButtons((uint32)button & 7, 1, modifiers);
			if (status != B_OK)
				return status;
		}
		if (wheel != 0) {
			BMessage* event = new(std::nothrow) BMessage(B_MOUSE_WHEEL_CHANGED);
			if (event == NULL)
				return B_NO_MEMORY;
			event->AddInt64("when", system_time());
			event->AddFloat("be:wheel_delta_x", 0);
			event->AddFloat("be:wheel_delta_y", wheel > 0 ? -1.0f : 1.0f);
			event->AddInt32("modifiers", modifiers);
			status_t status = EnqueueMessage(event);
			if (status != B_OK)
				delete event;
			return status;
		}
		return B_OK;
	}

	static std::string _CodePointToUTF8(uint32 code)
	{
		std::string result;
		if (code <= 0x7f)
			result += (char)code;
		else if (code <= 0x7ff) {
			result += (char)(0xc0 | (code >> 6));
			result += (char)(0x80 | (code & 0x3f));
		} else if (code <= 0xffff) {
			result += (char)(0xe0 | (code >> 12));
			result += (char)(0x80 | ((code >> 6) & 0x3f));
			result += (char)(0x80 | (code & 0x3f));
		} else if (code <= 0x10ffff) {
			result += (char)(0xf0 | (code >> 18));
			result += (char)(0x80 | ((code >> 12) & 0x3f));
			result += (char)(0x80 | ((code >> 6) & 0x3f));
			result += (char)(0x80 | (code & 0x3f));
		}
		return result;
	}

	static std::string _MapBytes(const int32* table, int32 key,
		const char* buffer)
	{
		if (table == NULL || buffer == NULL || key < 0 || key >= 128)
			return std::string();
		int32 offset = table[key];
		uint8 length = (uint8)buffer[offset];
		return std::string(buffer + offset + 1, length);
	}

	static int32 _FindMappedKey(const std::string& desired, uint32& mapModifier,
		std::string& rawBytes)
	{
		key_map* map = NULL;
		char* buffer = NULL;
		get_key_map(&map, &buffer);
		if (map == NULL || buffer == NULL) {
			free(map);
			free(buffer);
			return -1;
		}
		struct Table {
			const int32* offsets;
			uint32 modifier;
		};
		Table tables[] = {
			{map->normal_map, 0},
			{map->shift_map, B_SHIFT_KEY | B_LEFT_SHIFT_KEY},
			{map->option_map, B_OPTION_KEY | B_LEFT_OPTION_KEY},
			{map->option_shift_map, B_OPTION_KEY | B_LEFT_OPTION_KEY
				| B_SHIFT_KEY | B_LEFT_SHIFT_KEY}
		};
		int32 result = -1;
		for (size_t tableIndex = 0; tableIndex < sizeof(tables) / sizeof(Table)
			&& result < 0; tableIndex++) {
			for (int32 key = 0; key < 128; key++) {
				if (_MapBytes(tables[tableIndex].offsets, key, buffer) == desired) {
					result = key;
					mapModifier = tables[tableIndex].modifier;
					rawBytes = _MapBytes(map->normal_map, key, buffer);
					break;
				}
			}
		}
		free(map);
		free(buffer);
		return result;
	}

	static bool _NamedKeyBytes(const char* key, std::string& bytes)
	{
		struct NamedKey {
			const char* name;
			uint8 first;
			uint8 second;
		};
		static const NamedKey keys[] = {
			{"TAB", B_TAB, 0}, {"ENTER", B_ENTER, 0},
			{"BACKSPACE", B_BACKSPACE, 0}, {"ESCAPE", B_ESCAPE, 0},
			{"SPACE", B_SPACE, 0}, {"DELETE", B_DELETE, 0},
			{"INSERT", B_INSERT, 0}, {"HOME", B_HOME, 0},
			{"END", B_END, 0}, {"PAGE_UP", B_PAGE_UP, 0},
			{"PAGE_DOWN", B_PAGE_DOWN, 0}, {"LEFT_ARROW", B_LEFT_ARROW, 0},
			{"RIGHT_ARROW", B_RIGHT_ARROW, 0}, {"UP_ARROW", B_UP_ARROW, 0},
			{"DOWN_ARROW", B_DOWN_ARROW, 0},
			{"F1", B_FUNCTION_KEY, B_F1_KEY}, {"F2", B_FUNCTION_KEY, B_F2_KEY},
			{"F3", B_FUNCTION_KEY, B_F3_KEY}, {"F4", B_FUNCTION_KEY, B_F4_KEY},
			{"F5", B_FUNCTION_KEY, B_F5_KEY}, {"F6", B_FUNCTION_KEY, B_F6_KEY},
			{"F7", B_FUNCTION_KEY, B_F7_KEY}, {"F8", B_FUNCTION_KEY, B_F8_KEY},
			{"F9", B_FUNCTION_KEY, B_F9_KEY}, {"F10", B_FUNCTION_KEY, B_F10_KEY},
			{"F11", B_FUNCTION_KEY, B_F11_KEY}, {"F12", B_FUNCTION_KEY, B_F12_KEY}
		};
		for (size_t i = 0; i < sizeof(keys) / sizeof(NamedKey); i++) {
			if (strcmp(key, keys[i].name) == 0) {
				bytes.assign(1, (char)keys[i].first);
				if (keys[i].second != 0)
					bytes += (char)keys[i].second;
				return true;
			}
		}
		return false;
	}

	status_t _EnqueueKeyEvent(uint32 what, int32 key, uint32 modifiers,
		const std::string& bytes, const std::string& rawBytes, uint32 rawChar)
	{
		key_info info;
		memset(&info, 0, sizeof(info));
		get_key_info(&info);
		if (key >= 0 && key < 128) {
			uint8 mask = 1 << (7 - (key & 7));
			if (what == B_KEY_DOWN)
				info.key_states[key >> 3] |= mask;
			else
				info.key_states[key >> 3] &= ~mask;
		}
		BMessage* event = new(std::nothrow) BMessage(what);
		if (event == NULL)
			return B_NO_MEMORY;
		event->AddInt64("when", system_time());
		event->AddInt32("key", key < 0 ? 0 : key);
		event->AddInt32("modifiers", what == B_KEY_DOWN ? modifiers : 0);
		event->AddData("states", B_UINT8_TYPE, info.key_states,
			sizeof(info.key_states));
		if (!bytes.empty())
			event->AddString("bytes", bytes.c_str());
		if (rawChar != 0)
			event->AddInt32("raw_char", rawChar);
		else if (!rawBytes.empty())
			event->AddInt32("raw_char", (uint8)rawBytes[0]);
		if (!rawBytes.empty())
			event->AddInt8("byte", rawBytes[0]);
		if (what == B_KEY_DOWN)
			event->AddInt32("be:key_repeat", 1);
		status_t status = EnqueueMessage(event);
		if (status != B_OK)
			delete event;
		return status;
	}

	status_t _HandleKeyboard(const BMessage& request)
	{
		const char* type = request.GetString("type", "");
		const char* keyName = request.GetString("key", "");
		uint32 modifiers = _Modifiers(request);
		if (strcmp(type, "CHAR") == 0) {
			char* end = NULL;
			unsigned long code = strtoul(keyName, &end, 10);
			if (end == keyName || code > 0x10ffff)
				return B_BAD_VALUE;
			std::string bytes = _CodePointToUTF8((uint32)code);
			status_t status = _EnqueueKeyEvent(B_KEY_DOWN, -1, modifiers, bytes,
				bytes, (uint32)code);
			if (status == B_OK)
				status = _EnqueueKeyEvent(B_KEY_UP, -1, 0, bytes, bytes,
					(uint32)code);
			return status;
		}
		if (strcmp(type, "KEY") != 0)
			return strcmp(type, "CTRLALTCANC") == 0 ? B_OK : B_BAD_VALUE;

		uint32 directModifier = 0;
		if (strcmp(keyName, "CONTROL") == 0 || strcmp(keyName, "LCONTROL") == 0)
			directModifier = B_CONTROL_KEY;
		else if (strcmp(keyName, "ALT") == 0 || strcmp(keyName, "LALT") == 0)
			directModifier = B_OPTION_KEY;
		else if (strcmp(keyName, "SHIFT") == 0 || strcmp(keyName, "LSHIFT") == 0)
			directModifier = B_SHIFT_KEY;
		else if (strcmp(keyName, "COMMAND") == 0)
			directModifier = B_COMMAND_KEY;
		if (directModifier != 0) {
			uint32 key = 0;
			if (get_modifier_key(directModifier, &key) != B_OK)
				return B_NAME_NOT_FOUND;
			status_t status = _EnqueueKeyEvent(B_KEY_DOWN, key,
				directModifier, std::string(), std::string(), 0);
			if (status == B_OK)
				status = _EnqueueKeyEvent(B_KEY_UP, key, 0, std::string(),
					std::string(), 0);
			return status;
		}

		std::string desired;
		if (!_NamedKeyBytes(keyName, desired)) {
			desired = keyName;
			if (desired.size() == 1 && desired[0] >= 'A' && desired[0] <= 'Z')
				desired[0] = tolower(desired[0]);
		}
		uint32 mapModifier = 0;
		std::string rawBytes;
		int32 key = _FindMappedKey(desired, mapModifier, rawBytes);
		if (key < 0)
			return B_NAME_NOT_FOUND;
		uint32 eventModifiers = modifiers | mapModifier;
		status_t status = _EnqueueKeyEvent(B_KEY_DOWN, key, eventModifiers,
			desired, rawBytes, 0);
		if (status == B_OK)
			status = _EnqueueKeyEvent(B_KEY_UP, key, 0, desired, rawBytes, 0);
		return status;
	}

private:
	uint32 fButtons;
	BPoint fMousePosition;
};


extern "C" BInputServerDevice*
instantiate_input_device()
{
	return new(std::nothrow) DWServiceInputDevice();
}

#endif
