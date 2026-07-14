/*
This Source Code Form is subject to the terms of the Mozilla
Public License, v. 2.0. If a copy of the MPL was not distributed
with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
*/
#if defined OS_HAIKU_CAPTURE

#include "screencapturenativehaiku.h"
#include <Autolock.h>

static BApplication* sApplication = NULL;
static bool sOwnApplication = false;
static BScreen* sScreen = NULL;
static BLocker sLock("dwservice haiku capture");
static uint32 sClipboardCount = 0;
static bigtime_t sCpuWall = 0;
static uint64 sCpuTime = 0;
static BRect sKnownFrame;
static bool sHaveKnownFrame = false;


static uint64
process_cpu_time()
{
	struct rusage usage;
	if (getrusage(RUSAGE_SELF, &usage) != 0)
		return 0;
	return (uint64)usage.ru_utime.tv_sec * 1000000ULL + usage.ru_utime.tv_usec
		+ (uint64)usage.ru_stime.tv_sec * 1000000ULL + usage.ru_stime.tv_usec;
}


static std::string
input_token()
{
	BPath path;
	if (find_directory(B_USER_SETTINGS_DIRECTORY, &path) != B_OK)
		return std::string();
	if (path.Append("DWService/input.token") != B_OK)
		return std::string();
	std::ifstream stream(path.Path());
	std::string token;
	std::getline(stream, token);
	return token;
}


static status_t
send_input(const char* deviceName, BMessage& message)
{
	std::string token = input_token();
	if (token.empty())
		return B_NOT_ALLOWED;
	message.AddInt32("version", kDWServiceInputVersion);
	message.AddString("token", token.c_str());
	BInputDevice* device = find_input_device(deviceName);
	if (device == NULL)
		return B_NAME_NOT_FOUND;
	status_t status = device->Control(kDWServiceInputCommand, &message);
	delete device;
	return status;
}


static bool
pixel_at(const BBitmap* bitmap, int x, int y, uint8& red, uint8& green,
	uint8& blue, uint8& alpha)
{
	const uint8* row = (const uint8*)bitmap->Bits()
		+ (size_t)y * bitmap->BytesPerRow();
	switch (bitmap->ColorSpace()) {
		case B_RGB32:
		case B_RGBA32:
			blue = row[x * 4];
			green = row[x * 4 + 1];
			red = row[x * 4 + 2];
			alpha = bitmap->ColorSpace() == B_RGBA32 ? row[x * 4 + 3] : 255;
			return true;
		case B_RGB32_BIG:
		case B_RGBA32_BIG:
			red = row[x * 4];
			green = row[x * 4 + 1];
			blue = row[x * 4 + 2];
			alpha = bitmap->ColorSpace() == B_RGBA32_BIG ? row[x * 4 + 3] : 255;
			return true;
		case B_RGB24:
			blue = row[x * 3];
			green = row[x * 3 + 1];
			red = row[x * 3 + 2];
			alpha = 255;
			return true;
		case B_RGB24_BIG:
			red = row[x * 3];
			green = row[x * 3 + 1];
			blue = row[x * 3 + 2];
			alpha = 255;
			return true;
		default:
			return false;
	}
}


static void
send_shortcut(const char* key)
{
	BMessage message;
	message.AddInt32("kind", kDWServiceKeyboard);
	message.AddString("type", "KEY");
	message.AddString("key", key);
	message.AddBool("ctrl", false);
	message.AddBool("alt", false);
	message.AddBool("shift", false);
	message.AddBool("command", true);
	send_input(kDWServiceKeyboardName, message);
}


extern "C" int
DWAHaikuInputProbe()
{
	BMessage pointer;
	pointer.AddInt32("kind", kDWServiceMouse);
	pointer.AddInt32("x", -1);
	pointer.AddInt32("y", -1);
	pointer.AddInt32("button", -1);
	pointer.AddInt32("wheel", 0);
	status_t status = send_input(kDWServicePointerName, pointer);
	if (status != B_OK)
		return status;

	BMessage keyboard;
	keyboard.AddInt32("kind", kDWServiceKeyboard);
	keyboard.AddString("type", "CTRLALTCANC");
	keyboard.AddString("key", "");
	return send_input(kDWServiceKeyboardName, keyboard);
}


int
DWAScreenCaptureGetCpuUsage()
{
	BAutolock locker(&sLock);
	bigtime_t wall = system_time();
	uint64 cpu = process_cpu_time();
	if (sCpuWall == 0 || wall <= sCpuWall) {
		sCpuWall = wall;
		sCpuTime = cpu;
		return 0;
	}
	uint64 wallDelta = wall - sCpuWall;
	uint64 cpuDelta = cpu >= sCpuTime ? cpu - sCpuTime : 0;
	sCpuWall = wall;
	sCpuTime = cpu;
	int result = wallDelta == 0 ? 0 : (int)(cpuDelta * 100 / wallDelta);
	return result > 100 ? 100 : result;
}


void
DWAScreenCaptureFreeMemory(void* pointer)
{
	free(pointer);
}


int
DWAScreenCaptureIsChanged()
{
	BAutolock locker(&sLock);
	if (sScreen == NULL || !sScreen->IsValid())
		return 0;
	BRect frame = sScreen->Frame();
	if (!sHaveKnownFrame || frame != sKnownFrame) {
		sKnownFrame = frame;
		sHaveKnownFrame = true;
		return 1;
	}
	return 0;
}


int
DWAScreenCaptureGetMonitorsInfo(MONITORS_INFO* info)
{
	if (info == NULL || sScreen == NULL || !sScreen->IsValid())
		return -1;
	BAutolock locker(&sLock);
	BRect frame = sScreen->Frame();
	int width = frame.IntegerWidth() + 1;
	int height = frame.IntegerHeight() + 1;
	bool changed = info->count != 1 || info->monitor[0].x != (int)frame.left
		|| info->monitor[0].y != (int)frame.top
		|| info->monitor[0].width != width
		|| info->monitor[0].height != height;
	info->count = 1;
	info->changed = changed ? 1 : 0;
	info->monitor[0].index = 0;
	info->monitor[0].x = (int)frame.left;
	info->monitor[0].y = (int)frame.top;
	info->monitor[0].width = width;
	info->monitor[0].height = height;
	info->monitor[0].changed = changed ? 1 : 0;
	info->monitor[0].internal = NULL;
	return 0;
}


int
DWAScreenCaptureInitMonitor(MONITORS_INFO_ITEM* monitor, RGB_IMAGE* image,
	void** captureSession)
{
	if (monitor == NULL || image == NULL || captureSession == NULL
		|| monitor->width <= 0 || monitor->height <= 0)
		return -1;
	size_t dataSize = (size_t)monitor->width * monitor->height * 3;
	if (dataSize > LONG_MAX)
		return -2;
	HaikuCaptureSession* session = new(std::nothrow) HaikuCaptureSession;
	if (session == NULL)
		return -2;
	memset(image, 0, sizeof(RGB_IMAGE));
	image->data = (unsigned char*)malloc(dataSize);
	if (image->data == NULL) {
		delete session;
		return -2;
	}
	memset(image->data, 0, dataSize);
	image->width = monitor->width;
	image->height = monitor->height;
	image->sizedata = dataSize;
	session->image = image;
	session->frame = BRect(monitor->x, monitor->y,
		monitor->x + monitor->width - 1, monitor->y + monitor->height - 1);
	session->firstFrame = true;
	*captureSession = session;
	return 0;
}


int
DWAScreenCaptureGetImage(void* captureSession)
{
	HaikuCaptureSession* session = (HaikuCaptureSession*)captureSession;
	if (session == NULL || session->image == NULL || sScreen == NULL)
		return -1;
	BAutolock locker(&sLock);
	BBitmap* bitmap = NULL;
	BRect frame = session->frame;
	status_t status = sScreen->GetBitmap(&bitmap, false, &frame);
	if (status != B_OK || bitmap == NULL)
		return -3;
	RGB_IMAGE* image = session->image;
	image->sizechangearea = 0;
	image->sizemovearea = 0;
	int minX = image->width;
	int minY = image->height;
	int maxX = -1;
	int maxY = -1;
	for (int y = 0; y < image->height; y++) {
		for (int x = 0; x < image->width; x++) {
			uint8 red, green, blue, alpha;
			if (!pixel_at(bitmap, x, y, red, green, blue, alpha)) {
				delete bitmap;
				return -4;
			}
			size_t offset = ((size_t)y * image->width + x) * 3;
			if (session->firstFrame || image->data[offset] != red
				|| image->data[offset + 1] != green
				|| image->data[offset + 2] != blue) {
				if (x < minX) minX = x;
				if (y < minY) minY = y;
				if (x > maxX) maxX = x;
				if (y > maxY) maxY = y;
			}
			image->data[offset] = red;
			image->data[offset + 1] = green;
			image->data[offset + 2] = blue;
		}
	}
	delete bitmap;
	if (maxX >= minX && maxY >= minY) {
		image->sizechangearea = 1;
		image->changearea[0].x = minX;
		image->changearea[0].y = minY;
		image->changearea[0].width = maxX - minX + 1;
		image->changearea[0].height = maxY - minY + 1;
	}
	session->firstFrame = false;
	return 0;
}


void
DWAScreenCaptureTermMonitor(void* captureSession)
{
	HaikuCaptureSession* session = (HaikuCaptureSession*)captureSession;
	if (session == NULL)
		return;
	if (session->image != NULL) {
		free(session->image->data);
		session->image->data = NULL;
		session->image->width = 0;
		session->image->height = 0;
		session->image->sizedata = 0;
	}
	delete session;
}


int
DWAScreenCaptureCursor(CURSOR_IMAGE* cursor)
{
	if (cursor == NULL)
		return -1;
	BAutolock locker(&sLock);
	BPoint position;
	uint32 buttons;
	if (get_mouse(&position, &buttons) != B_OK)
		return -1;
	cursor->visible = 1;
	cursor->x = (int)position.x;
	cursor->y = (int)position.y;
	cursor->changed = 0;
	BBitmap* bitmap = NULL;
	BPoint hotspot;
	if (get_mouse_bitmap(&bitmap, &hotspot) != B_OK || bitmap == NULL)
		return 0;
	int width = bitmap->Bounds().IntegerWidth() + 1;
	int height = bitmap->Bounds().IntegerHeight() + 1;
	size_t size = (size_t)width * height * 4;
	unsigned char* data = (unsigned char*)malloc(size);
	if (data == NULL) {
		delete bitmap;
		return -2;
	}
	for (int y = 0; y < height; y++) {
		for (int x = 0; x < width; x++) {
			uint8 red, green, blue, alpha;
			if (!pixel_at(bitmap, x, y, red, green, blue, alpha)) {
				free(data);
				delete bitmap;
				return -3;
			}
			size_t offset = ((size_t)y * width + x) * 4;
			data[offset] = red;
			data[offset + 1] = green;
			data[offset + 2] = blue;
			data[offset + 3] = alpha;
		}
	}
	delete bitmap;
	bool changed = cursor->data == NULL || cursor->width != width
		|| cursor->height != height || cursor->offx != (int)hotspot.x
		|| cursor->offy != (int)hotspot.y || cursor->sizedata != (long)size
		|| memcmp(cursor->data, data, size) != 0;
	if (changed) {
		free(cursor->data);
		cursor->data = data;
		cursor->width = width;
		cursor->height = height;
		cursor->offx = (int)hotspot.x;
		cursor->offy = (int)hotspot.y;
		cursor->sizedata = size;
		cursor->changed = 1;
	} else
		free(data);
	return 0;
}


void
DWAScreenCaptureInputKeyboard(const char* type, const char* key, bool ctrl,
	bool alt, bool shift, bool command)
{
	BMessage message;
	message.AddInt32("kind", kDWServiceKeyboard);
	message.AddString("type", type == NULL ? "" : type);
	message.AddString("key", key == NULL ? "" : key);
	message.AddBool("ctrl", ctrl);
	message.AddBool("alt", alt);
	message.AddBool("shift", shift);
	message.AddBool("command", command);
	send_input(kDWServiceKeyboardName, message);
}


void
DWAScreenCaptureInputMouse(MONITORS_INFO_ITEM* monitor, int x, int y,
	int button, int wheel, bool ctrl, bool alt, bool shift, bool command)
{
	if (monitor != NULL && x >= 0 && y >= 0) {
		x += monitor->x;
		y += monitor->y;
	}
	BMessage message;
	message.AddInt32("kind", kDWServiceMouse);
	message.AddInt32("x", x);
	message.AddInt32("y", y);
	message.AddInt32("button", button);
	message.AddInt32("wheel", wheel);
	message.AddBool("ctrl", ctrl);
	message.AddBool("alt", alt);
	message.AddBool("shift", shift);
	message.AddBool("command", command);
	send_input(kDWServicePointerName, message);
}


void
DWAScreenCaptureGetClipboardChanges(CLIPBOARD_DATA* clipboard)
{
	if (clipboard == NULL)
		return;
	clipboard->type = 0;
	clipboard->sizedata = 0;
	clipboard->data = NULL;
	if (be_clipboard == NULL || be_clipboard->SystemCount() == sClipboardCount)
		return;
	sClipboardCount = be_clipboard->SystemCount();
	if (!be_clipboard->Lock())
		return;
	const void* data = NULL;
	ssize_t size = 0;
	status_t status = be_clipboard->Data()->FindData("text/plain", B_MIME_TYPE,
		&data, &size);
	if (status == B_OK && data != NULL && size > 0 && size <= 4 * 1024 * 1024) {
		try {
			std::wstring_convert<std::codecvt_utf8<wchar_t> > converter;
			std::wstring text = converter.from_bytes((const char*)data,
				(const char*)data + size);
			clipboard->sizedata = text.size() * sizeof(wchar_t);
			clipboard->data = (unsigned char*)malloc(clipboard->sizedata);
			if (clipboard->data != NULL) {
				memcpy(clipboard->data, text.data(), clipboard->sizedata);
				clipboard->type = 1;
			} else
				clipboard->sizedata = 0;
		} catch (...) {
		}
	}
	be_clipboard->Unlock();
}


void
DWAScreenCaptureSetClipboard(CLIPBOARD_DATA* clipboard)
{
	if (clipboard == NULL || clipboard->type != 1 || clipboard->data == NULL
		|| clipboard->sizedata < 0 || clipboard->sizedata > 4 * 1024 * 1024
		|| be_clipboard == NULL)
		return;
	try {
		size_t count = clipboard->sizedata / sizeof(wchar_t);
		const wchar_t* input = (const wchar_t*)clipboard->data;
		while (count > 0 && input[count - 1] == 0)
			count--;
		std::wstring text(input, count);
		std::wstring_convert<std::codecvt_utf8<wchar_t> > converter;
		std::string utf8 = converter.to_bytes(text);
		if (be_clipboard->Lock()) {
			be_clipboard->Clear();
			be_clipboard->Data()->AddData("text/plain", B_MIME_TYPE,
				utf8.data(), utf8.size());
			be_clipboard->Commit();
			sClipboardCount = be_clipboard->SystemCount();
			be_clipboard->Unlock();
		}
	} catch (...) {
	}
}


void
DWAScreenCaptureCopy()
{
	send_shortcut("C");
}


void
DWAScreenCapturePaste()
{
	send_shortcut("V");
}


bool
DWAScreenCaptureLoad()
{
	BAutolock locker(&sLock);
	if (be_app == NULL) {
		sApplication = new(std::nothrow) BApplication(
			"application/x-vnd.DWService-Capture");
		sOwnApplication = sApplication != NULL;
	}
	sScreen = new(std::nothrow) BScreen(B_MAIN_SCREEN_ID);
	if (sScreen == NULL || !sScreen->IsValid()) {
		delete sScreen;
		sScreen = NULL;
		if (sOwnApplication) {
			delete sApplication;
			sApplication = NULL;
			sOwnApplication = false;
		}
		return false;
	}
	sClipboardCount = be_clipboard == NULL ? 0 : be_clipboard->SystemCount();
	sCpuWall = system_time();
	sCpuTime = process_cpu_time();
	sKnownFrame = sScreen->Frame();
	sHaveKnownFrame = true;
	return true;
}


void
DWAScreenCaptureUnload()
{
	BAutolock locker(&sLock);
	delete sScreen;
	sScreen = NULL;
	sHaveKnownFrame = false;
	if (sOwnApplication) {
		delete sApplication;
		sApplication = NULL;
		sOwnApplication = false;
	}
}

#endif
