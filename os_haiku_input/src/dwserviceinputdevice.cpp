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
#include <OS.h>
#include <Path.h>
#include <Point.h>
#include <View.h>

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include <fstream>
#include <new>
#include <string>

#include "dwserviceinput.h"


static const bigtime_t kClickHold = 100000;
static const bigtime_t kClickGap = 80000;
static const bigtime_t kDoubleClickGap = 150000;
static const bigtime_t kKeyHold = 10000;


struct ModifierState {
	uint32 before;
	uint32 active;
	uint8 beforeStates[16];
	uint8 activeStates[16];
	bool changed;
};


class DWServiceInputDevice : public BInputServerDevice {
public:
	DWServiceInputDevice()
		:
		fButtons(0),
		fMousePosition(0, 0),
		fLastPrimaryDown(0),
		fLastClickPosition(0, 0),
		fPrimaryClickCount(0)
	{
		uint32 buttons = 0;
		get_mouse(&fMousePosition, &buttons);
		fButtons = buttons;
		memset(fButtonDownAt, 0, sizeof(fButtonDownAt));
		memset(fButtonUpAt, 0, sizeof(fButtonUpAt));
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

	static void _SetKeyState(uint8* states, uint32 key, bool down)
	{
		if (key >= 128)
			return;
		uint8 mask = 1 << (7 - (key & 7));
		if (down)
			states[key >> 3] |= mask;
		else
			states[key >> 3] &= ~mask;
	}

	static void _InitializeModifierState(ModifierState& state)
	{
		key_info info;
		memset(&info, 0, sizeof(info));
		get_key_info(&info);
		state.before = info.modifiers;
		state.active = info.modifiers;
		memcpy(state.beforeStates, info.key_states, sizeof(state.beforeStates));
		memcpy(state.activeStates, info.key_states, sizeof(state.activeStates));
		state.changed = false;
	}

	static void _AddModifier(ModifierState& state, bool enabled,
		uint32 specificModifier, uint32 generalModifier)
	{
		if (!enabled)
			return;
		state.active |= specificModifier | generalModifier;
		uint32 key = 0;
		if (get_modifier_key(specificModifier, &key) == B_OK)
			_SetKeyState(state.activeStates, key, true);
	}

	static void _AddModifierMask(ModifierState& state, uint32 modifiers)
	{
		_AddModifier(state, (modifiers & B_CONTROL_KEY) != 0,
			B_LEFT_CONTROL_KEY, B_CONTROL_KEY);
		_AddModifier(state, (modifiers & B_OPTION_KEY) != 0,
			B_LEFT_OPTION_KEY, B_OPTION_KEY);
		_AddModifier(state, (modifiers & B_SHIFT_KEY) != 0,
			B_LEFT_SHIFT_KEY, B_SHIFT_KEY);
		_AddModifier(state, (modifiers & B_COMMAND_KEY) != 0,
			B_LEFT_COMMAND_KEY, B_COMMAND_KEY);
	}

	static void _PrepareModifiers(const BMessage& request,
		ModifierState& state)
	{
		_InitializeModifierState(state);
		_AddModifier(state, request.GetBool("ctrl", false),
			B_LEFT_CONTROL_KEY, B_CONTROL_KEY);
		_AddModifier(state, request.GetBool("shift", false),
			B_LEFT_SHIFT_KEY, B_SHIFT_KEY);

		// Haiku's Command modifier is mapped to the Alt-labelled keys on
		// standard PC keyboards. Treat remote Alt and Command as that native
		// modifier so both PC and Mac dashboard clients can use Haiku
		// shortcuts.
		_AddModifier(state, request.GetBool("alt", false)
				|| request.GetBool("command", false),
			B_LEFT_COMMAND_KEY, B_COMMAND_KEY);
	}

	status_t _EnqueueModifierEvent(uint32 before, uint32 after,
		const uint8* states)
	{
		BMessage* event = new(std::nothrow) BMessage(B_MODIFIERS_CHANGED);
		if (event == NULL)
			return B_NO_MEMORY;
		event->AddInt64("when", system_time());
		event->AddInt32("be:old_modifiers", before);
		event->AddInt32("modifiers", after);
		event->AddData("states", B_UINT8_TYPE, states, 16);
		status_t status = EnqueueMessage(event);
		if (status != B_OK)
			delete event;
		return status;
	}

	status_t _BeginModifiers(ModifierState& state)
	{
		state.changed = state.before != state.active
			|| memcmp(state.beforeStates, state.activeStates,
				sizeof(state.beforeStates)) != 0;
		if (!state.changed)
			return B_OK;
		return _EnqueueModifierEvent(state.before, state.active,
			state.activeStates);
	}

	status_t _EndModifiers(const ModifierState& state)
	{
		if (!state.changed)
			return B_OK;
		return _EnqueueModifierEvent(state.active, state.before,
			state.beforeStates);
	}

	status_t _FinishModifiers(const ModifierState& state, status_t status)
	{
		status_t releaseStatus = _EndModifiers(state);
		return status != B_OK ? status : releaseStatus;
	}

	uint32 _Modifiers(const BMessage& request) const
	{
		uint32 result = 0;
		if (request.GetBool("ctrl", false))
			result |= B_CONTROL_KEY | B_LEFT_CONTROL_KEY;
		if (request.GetBool("shift", false))
			result |= B_SHIFT_KEY | B_LEFT_SHIFT_KEY;
		if (request.GetBool("alt", false)
			|| request.GetBool("command", false))
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
		if (what == B_MOUSE_DOWN && clicks > 0)
			event->AddInt32("clicks", clicks);
		status_t status = EnqueueMessage(event);
		if (status != B_OK)
			delete event;
		return status;
	}

	static int32 _ButtonIndex(uint32 mask)
	{
		if (mask == B_PRIMARY_MOUSE_BUTTON)
			return 0;
		if (mask == B_SECONDARY_MOUSE_BUTTON)
			return 1;
		return 2;
	}

	static void _WaitUntil(bigtime_t started, bigtime_t duration)
	{
		if (started <= 0)
			return;
		bigtime_t remaining = duration - (system_time() - started);
		if (remaining > 0)
			snooze(remaining);
	}

	int32 _NextPrimaryClickCount()
	{
		bigtime_t now = system_time();
		bigtime_t clickSpeed = 500000;
		get_click_speed(&clickSpeed);
		float deltaX = fMousePosition.x - fLastClickPosition.x;
		float deltaY = fMousePosition.y - fLastClickPosition.y;
		bool sameLocation = deltaX >= -4 && deltaX <= 4
			&& deltaY >= -4 && deltaY <= 4;
		if (fLastPrimaryDown > 0 && now - fLastPrimaryDown <= clickSpeed
			&& sameLocation) {
			fPrimaryClickCount++;
		} else
			fPrimaryClickCount = 1;
		fLastPrimaryDown = now;
		fLastClickPosition = fMousePosition;
		return fPrimaryClickCount;
	}

	status_t _SetButtons(uint32 buttons, int32 clicks, uint32 modifiers)
	{
		// Release old buttons before pressing new ones. In particular, a
		// right-to-left transition must not briefly report both buttons down
		// while a popup menu is tracking the pointer.
		for (uint32 mask = 1; mask <= 4; mask <<= 1) {
			if ((fButtons & mask) == 0 || (buttons & mask) != 0)
				continue;
			int32 index = _ButtonIndex(mask);
			_WaitUntil(fButtonDownAt[index], kClickHold);
			fButtons &= ~mask;
			status_t status = _EnqueueMouse(B_MOUSE_UP, 0, modifiers);
			if (status != B_OK)
				return status;
			fButtonDownAt[index] = 0;
			fButtonUpAt[index] = system_time();
		}

		for (uint32 mask = 1; mask <= 4; mask <<= 1) {
			if ((fButtons & mask) != 0 || (buttons & mask) == 0)
				continue;
			int32 index = _ButtonIndex(mask);
			_WaitUntil(fButtonUpAt[index], kClickGap);
			fButtons |= mask;
			int32 eventClicks = clicks;
			if (mask == B_PRIMARY_MOUSE_BUTTON && eventClicks <= 0)
				eventClicks = _NextPrimaryClickCount();
			else if (mask == B_PRIMARY_MOUSE_BUTTON) {
				fPrimaryClickCount = eventClicks;
				fLastPrimaryDown = system_time();
				fLastClickPosition = fMousePosition;
			}
			status_t status = _EnqueueMouse(B_MOUSE_DOWN,
				eventClicks > 0 ? eventClicks : 1, modifiers);
			if (status != B_OK)
				return status;
			fButtonDownAt[index] = system_time();
		}
		return B_OK;
	}

	status_t _Click(int32 count, uint32 modifiers)
	{
		if (fButtons != 0) {
			status_t status = _SetButtons(0, 0, modifiers);
			if (status != B_OK)
				return status;
			snooze(kClickGap);
		}
		for (int32 index = 0; index < count; index++) {
			status_t status = _SetButtons(
				fButtons | B_PRIMARY_MOUSE_BUTTON, index + 1, modifiers);
			if (status != B_OK)
				return status;
			status = _SetButtons(
				fButtons & ~B_PRIMARY_MOUSE_BUTTON, index + 1, modifiers);
			if (status != B_OK)
				return status;
			if (index + 1 < count)
				snooze(kDoubleClickGap);
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
			status_t status = _Click(1, modifiers);
			if (status != B_OK)
				return status;
		} else if (button == 128) {
			status_t status = _Click(2, modifiers);
			if (status != B_OK)
				return status;
		} else if (button >= 0) {
			status_t status = _SetButtons((uint32)button & 7, 0, modifiers);
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

	static std::string _MappedBytesForKey(int32 key, uint32 modifiers)
	{
		key_map* map = NULL;
		char* buffer = NULL;
		get_key_map(&map, &buffer);
		if (map == NULL || buffer == NULL) {
			free(map);
			free(buffer);
			return std::string();
		}

		const int32* table = map->normal_map;
		bool control = (modifiers & B_CONTROL_KEY) != 0;
		bool command = (modifiers & B_COMMAND_KEY) != 0;
		bool option = (modifiers & B_OPTION_KEY) != 0;
		bool shift = (modifiers & B_SHIFT_KEY) != 0;
		bool caps = (modifiers & B_CAPS_LOCK) != 0;
		if (control && !command)
			table = map->control_map;
		else if (option && caps && shift)
			table = map->option_caps_shift_map;
		else if (option && caps)
			table = map->option_caps_map;
		else if (option && shift)
			table = map->option_shift_map;
		else if (option)
			table = map->option_map;
		else if (caps && shift)
			table = map->caps_shift_map;
		else if (caps)
			table = map->caps_map;
		else if (shift)
			table = map->shift_map;

		std::string result = _MapBytes(table, key, buffer);
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
		const std::string& bytes, const std::string& rawBytes, uint32 rawChar,
		const uint8* modifierStates)
	{
		uint8 states[16];
		memcpy(states, modifierStates, sizeof(states));
		if (key >= 0 && key < 128)
			_SetKeyState(states, key, what == B_KEY_DOWN);
		BMessage* event = new(std::nothrow) BMessage(what);
		if (event == NULL)
			return B_NO_MEMORY;
		event->AddInt64("when", system_time());
		event->AddInt32("key", key < 0 ? 0 : key);
		event->AddInt32("modifiers", modifiers);
		event->AddData("states", B_UINT8_TYPE, states, sizeof(states));
		if (!bytes.empty()) {
			for (size_t index = 0; index < bytes.size(); index++)
				event->AddInt8("byte", (int8)bytes[index]);
			event->AddData("bytes", B_STRING_TYPE, bytes.c_str(),
				bytes.size() + 1);
		}
		if (rawChar != 0)
			event->AddInt32("raw_char", rawChar);
		else if (!rawBytes.empty())
			event->AddInt32("raw_char", (uint8)rawBytes[0] & 0x7f);
		status_t status = EnqueueMessage(event);
		if (status != B_OK)
			delete event;
		return status;
	}

	static bool _NamedModifier(const char* keyName, uint32& specificModifier,
		uint32& generalModifier)
	{
		struct NamedModifier {
			const char* name;
			uint32 specific;
			uint32 general;
		};
		static const NamedModifier modifiers[] = {
			{"CONTROL", B_LEFT_CONTROL_KEY, B_CONTROL_KEY},
			{"LCONTROL", B_LEFT_CONTROL_KEY, B_CONTROL_KEY},
			{"RCONTROL", B_RIGHT_CONTROL_KEY, B_CONTROL_KEY},
			{"SHIFT", B_LEFT_SHIFT_KEY, B_SHIFT_KEY},
			{"LSHIFT", B_LEFT_SHIFT_KEY, B_SHIFT_KEY},
			{"RSHIFT", B_RIGHT_SHIFT_KEY, B_SHIFT_KEY},

			// The standard PC Alt keys are Haiku's Command keys.
			{"ALT", B_LEFT_COMMAND_KEY, B_COMMAND_KEY},
			{"LALT", B_LEFT_COMMAND_KEY, B_COMMAND_KEY},
			{"RALT", B_RIGHT_COMMAND_KEY, B_COMMAND_KEY},
			{"COMMAND", B_LEFT_COMMAND_KEY, B_COMMAND_KEY},
			{"LCOMMAND", B_LEFT_COMMAND_KEY, B_COMMAND_KEY},
			{"RCOMMAND", B_RIGHT_COMMAND_KEY, B_COMMAND_KEY},

			// Keep explicit Option names available for clients that distinguish
			// Haiku's Option modifier from the Alt-labelled Command key.
			{"OPTION", B_LEFT_OPTION_KEY, B_OPTION_KEY},
			{"LOPTION", B_LEFT_OPTION_KEY, B_OPTION_KEY},
			{"ROPTION", B_RIGHT_OPTION_KEY, B_OPTION_KEY}
		};
		for (size_t index = 0;
			index < sizeof(modifiers) / sizeof(NamedModifier); index++) {
			if (strcmp(keyName, modifiers[index].name) == 0) {
				specificModifier = modifiers[index].specific;
				generalModifier = modifiers[index].general;
				return true;
			}
		}
		return false;
	}

	status_t _PulseModifier(uint32 specificModifier, uint32 generalModifier)
	{
		ModifierState state;
		_InitializeModifierState(state);
		_AddModifier(state, true, specificModifier, generalModifier);
		status_t status = _BeginModifiers(state);
		if (status != B_OK)
			return status;
		snooze(kKeyHold);
		return _EndModifiers(state);
	}

	status_t _SendMappedKey(const std::string& desired, ModifierState& state)
	{
		uint32 mapModifier = 0;
		std::string rawBytes;
		int32 key = _FindMappedKey(desired, mapModifier, rawBytes);
		if (key < 0)
			return B_NAME_NOT_FOUND;

		_AddModifierMask(state, mapModifier);
		std::string eventBytes = _MappedBytesForKey(key, state.active);
		if (eventBytes.empty())
			eventBytes = desired;

		status_t status = _BeginModifiers(state);
		if (status != B_OK)
			return status;
		status = _EnqueueKeyEvent(B_KEY_DOWN, key, state.active,
			eventBytes, rawBytes, 0, state.activeStates);
		if (status == B_OK)
			snooze(kKeyHold);
		if (status == B_OK) {
			status = _EnqueueKeyEvent(B_KEY_UP, key, state.active,
				eventBytes, rawBytes, 0, state.activeStates);
		}
		return _FinishModifiers(state, status);
	}

	status_t _SendDirectChar(uint32 code, const std::string& bytes,
		ModifierState& state)
	{
		status_t status = _BeginModifiers(state);
		if (status != B_OK)
			return status;
		status = _EnqueueKeyEvent(B_KEY_DOWN, -1, state.active, bytes,
			bytes, code, state.activeStates);
		if (status == B_OK)
			snooze(kKeyHold);
		if (status == B_OK) {
			status = _EnqueueKeyEvent(B_KEY_UP, -1, state.active, bytes,
				bytes, code, state.activeStates);
		}
		return _FinishModifiers(state, status);
	}

	status_t _HandleKeyboard(const BMessage& request)
	{
		const char* type = request.GetString("type", "");
		const char* keyName = request.GetString("key", "");
		if (strcmp(type, "CHAR") == 0) {
			char* end = NULL;
			unsigned long code = strtoul(keyName, &end, 10);
			if (end == keyName || *end != '\0' || code > 0x10ffff)
				return B_BAD_VALUE;
			std::string bytes = _CodePointToUTF8((uint32)code);
			if (bytes.empty())
				return B_BAD_VALUE;

			ModifierState state;
			_PrepareModifiers(request, state);
			status_t status = _SendMappedKey(bytes, state);
			if (status != B_NAME_NOT_FOUND)
				return status;

			// Unicode characters outside the active keymap have no physical
			// key code. Haiku applications can still consume their generated
			// UTF-8 bytes.
			_PrepareModifiers(request, state);
			return _SendDirectChar((uint32)code, bytes, state);
		}
		if (strcmp(type, "KEY") != 0)
			return strcmp(type, "CTRLALTCANC") == 0 ? B_OK : B_BAD_VALUE;

		uint32 specificModifier = 0;
		uint32 generalModifier = 0;
		if (_NamedModifier(keyName, specificModifier, generalModifier))
			return _PulseModifier(specificModifier, generalModifier);

		std::string desired;
		if (!_NamedKeyBytes(keyName, desired)) {
			desired = keyName;
			if (desired.size() == 1 && desired[0] >= 'A' && desired[0] <= 'Z')
				desired[0] = tolower(desired[0]);
		}

		ModifierState state;
		_PrepareModifiers(request, state);
		return _SendMappedKey(desired, state);
	}

private:
	uint32 fButtons;
	BPoint fMousePosition;
	bigtime_t fButtonDownAt[3];
	bigtime_t fButtonUpAt[3];
	bigtime_t fLastPrimaryDown;
	BPoint fLastClickPosition;
	int32 fPrimaryClickCount;
};


extern "C" BInputServerDevice*
instantiate_input_device()
{
	return new(std::nothrow) DWServiceInputDevice();
}

#endif
