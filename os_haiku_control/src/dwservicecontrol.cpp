/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * Native Deskbar control application for the DWService Haiku agent.
 * The remote-control agent remains a launch_daemon service; this application
 * only exposes safe local lifecycle, status, dashboard, and log actions.
 */

#include <Alert.h>
#include <Application.h>
#include <Button.h>
#include <LayoutBuilder.h>
#include <StringView.h>
#include <Window.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>


static const char* kSignature = "application/x-vnd.DWService-Control";
static const char* kConfig =
	"/boot/home/config/non-packaged/apps/DWService/core/config.json";
static const char* kLog =
	"/boot/home/config/cache/DWService/service.log";

enum {
	kMsgStart = 'dwst',
	kMsgStop = 'dwsp',
	kMsgRefresh = 'dwrr',
	kMsgDashboard = 'dwdb',
	kMsgLog = 'dwlg'
};


static bool
command_ok(const char* command)
{
	int result = system(command);
	return result != -1 && WIFEXITED(result) && WEXITSTATUS(result) == 0;
}


static bool
service_registered()
{
	return command_ok(
		"launch_roster info x-vnd.DWService-Agent >/dev/null 2>&1");
}


static bool
service_running()
{
	FILE* pipe = popen("ps", "r");
	if (pipe == NULL)
		return false;
	char line[1024];
	bool found = false;
	while (fgets(line, sizeof(line), pipe) != NULL) {
		if (strstr(line, "dwagent-haiku-service") != NULL
			|| strstr(line, "python3 agent.py") != NULL) {
			found = true;
			break;
		}
	}
	pclose(pipe);
	return found;
}


static void
show_error(const char* text)
{
	(new BAlert("DWService", text, "OK", NULL, NULL,
		B_WIDTH_AS_USUAL, B_STOP_ALERT))->Go();
}


class ControlWindow : public BWindow {
public:
	ControlWindow()
		:
		BWindow(BRect(100, 100, 520, 300), "DWService for Haiku",
			B_TITLED_WINDOW, B_AUTO_UPDATE_SIZE_LIMITS),
		fStatus(new BStringView("status", "Checking status..."))
	{
		BStringView* heading = new BStringView("heading", "DWService Agent");
		heading->SetFont(be_bold_font);
		BLayoutBuilder::Group<>(this, B_VERTICAL, B_USE_DEFAULT_SPACING)
			.SetInsets(B_USE_WINDOW_INSETS)
			.Add(heading)
			.Add(fStatus)
			.AddGroup(B_HORIZONTAL, B_USE_DEFAULT_SPACING)
				.Add(new BButton("Start", new BMessage(kMsgStart)))
				.Add(new BButton("Stop", new BMessage(kMsgStop)))
				.Add(new BButton("Refresh", new BMessage(kMsgRefresh)))
			.End()
			.AddGroup(B_HORIZONTAL, B_USE_DEFAULT_SPACING)
				.Add(new BButton("Open dashboard",
					new BMessage(kMsgDashboard)))
				.Add(new BButton("Open log", new BMessage(kMsgLog)))
			.End();
		Refresh();
	}

	bool QuitRequested()
	{
		be_app->PostMessage(B_QUIT_REQUESTED);
		return true;
	}

	void MessageReceived(BMessage* message)
	{
		switch (message->what) {
			case kMsgStart:
				Start();
				break;
			case kMsgStop:
				if (!command_ok(
					"launch_roster stop x-vnd.DWService-Agent >/dev/null 2>&1"))
					show_error("Could not stop the DWService service.");
				Refresh();
				break;
			case kMsgRefresh:
				Refresh();
				break;
			case kMsgDashboard:
				command_ok("open https://www.dwservice.net/ >/dev/null 2>&1 &");
				break;
			case kMsgLog:
				if (access(kLog, R_OK) != 0)
					show_error("No service log exists yet.");
				else
					command_ok("open /boot/home/config/cache/DWService/service.log "
						">/dev/null 2>&1 &");
				break;
			default:
				BWindow::MessageReceived(message);
		}
	}

private:
	void Start()
	{
		if (access(kConfig, R_OK) != 0) {
			show_error("The agent is installed but not configured. Add it in "
				"the DWService dashboard and enter its one-time installation code.");
			return;
		}
		if (!service_registered()) {
			show_error("The service is installed but is not registered yet. "
				"Restart Haiku once, then click Start again.");
			return;
		}
		if (!command_ok(
			"launch_roster start x-vnd.DWService-Agent >/dev/null 2>&1"))
			show_error("Could not start the DWService service.");
		Refresh();
	}

	void Refresh()
	{
		if (access(kConfig, R_OK) != 0)
			fStatus->SetText("Installed - waiting for an installation code");
		else if (!service_registered())
			fStatus->SetText("Configured - restart Haiku to register the service");
		else if (service_running())
			fStatus->SetText("Running");
		else
			fStatus->SetText("Stopped");
	}

	BStringView* fStatus;
};


class ControlApplication : public BApplication {
public:
	ControlApplication()
		:
		BApplication(kSignature)
	{
	}

	void ReadyToRun()
	{
		ControlWindow* window = new ControlWindow();
		window->CenterOnScreen();
		window->Show();
	}
};


int
main()
{
	ControlApplication application;
	application.Run();
	return 0;
}
