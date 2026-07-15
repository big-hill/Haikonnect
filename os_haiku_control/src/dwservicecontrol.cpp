/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * Native Deskbar replicant for BeRD Agent on Haiku.
 *
 * The Python remote-control agent remains a normal, inspectable user service.
 * This add-on supplies its visible local identity, status and lifecycle menu.
 */

#include <Alert.h>
#include <Application.h>
#include <Deskbar.h>
#include <Entry.h>
#include <Font.h>
#include <MenuItem.h>
#include <Message.h>
#include <MessageRunner.h>
#include <Messenger.h>
#include <PopUpMenu.h>
#include <Roster.h>
#include <String.h>
#include <View.h>

#include <OS.h>

#include <algorithm>
#include <new>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/wait.h>
#include <unistd.h>


static const char* kSignature = "application/x-vnd.big-hill-BeRDAgent";
static const char* kDeskbarSignature = "application/x-vnd.Be-TSKB";
static const char* kDeskbarItemName = "BeRDAgent";
static const char* kConfig =
	"/boot/home/config/non-packaged/apps/DWService/core/config.json";
static const char* kStatusFile =
	"/boot/home/config/cache/DWService/agent.status";
static const char* kLog =
	"/boot/home/config/cache/DWService/service.log";

static const char* kStartCommand =
	"/boot/home/config/non-packaged/apps/DWService/os_haiku/"
	"berd-agent-control start >/dev/null 2>&1";
static const char* kStopCommand =
	"/boot/home/config/non-packaged/apps/DWService/os_haiku/"
	"berd-agent-control stop >/dev/null 2>&1";
static const char* kDashboardCommand =
	"open https://www.dwservice.net/ >/dev/null 2>&1";
static const char* kLogCommand =
	"open /boot/home/config/cache/DWService/service.log >/dev/null 2>&1";

enum {
	kMsgRefresh = 'bdrf',
	kMsgStart = 'bdst',
	kMsgStop = 'bdsp',
	kMsgDashboard = 'bddb',
	kMsgLog = 'bdlg',
	kMsgAbout = 'bdab',
	kMsgActionDone = 'bdad'
};

enum AgentState {
	kAgentRunning,
	kAgentUnconfigured,
	kAgentError
};


struct ActionRequest {
	BString command;
	BString failureMessage;
	BMessenger target;
};


static bool
is_agent_team(team_id team)
{
	team_info info;
	if (team <= 1 || get_team_info(team, &info) != B_OK)
		return false;

	return strstr(info.args, "agent.py") != NULL
		|| (strstr(info.name, "python") != NULL
			&& strstr(info.args, "-filelog") != NULL);
}


static bool
read_runtime_status(BString& detail)
{
	FILE* file = fopen(kStatusFile, "r");
	if (file == NULL) {
		detail = "Stopped";
		return false;
	}

	char line[128];
	if (fgets(line, sizeof(line), file) == NULL) {
		fclose(file);
		detail = "Stopped";
		return false;
	}
	fclose(file);

	if (strncmp(line, "running ", 8) == 0) {
		char* end = NULL;
		long value = strtol(line + 8, &end, 10);
		if (end != line + 8 && value > 1 && is_agent_team((team_id)value)) {
			detail = "Running";
			return true;
		}
		detail = "Stopped (stale status)";
		return false;
	}

	if (strncmp(line, "starting", 8) == 0) {
		detail = "Starting";
		return false;
	}

	if (strncmp(line, "error ", 6) == 0) {
		char* end = NULL;
		long value = strtol(line + 6, &end, 10);
		detail = "Error";
		if (end != line + 6) {
			detail << " (exit " << (int64)value << ")";
		}
		return false;
	}

	detail = "Stopped";
	return false;
}


static int32
run_action(void* cookie)
{
	ActionRequest* request = static_cast<ActionRequest*>(cookie);
	int result = system(request->command.String());
	bool succeeded = result != -1 && WIFEXITED(result)
		&& WEXITSTATUS(result) == 0;

	BMessage done(kMsgActionDone);
	done.AddBool("succeeded", succeeded);
	done.AddString("failure", request->failureMessage);
	request->target.SendMessage(&done);
	delete request;
	return succeeded ? B_OK : B_ERROR;
}


class BeRDAgentView : public BView {
public:
	BeRDAgentView(BRect frame, int32 resizingMode, bool inDeskbar)
		:
		BView(frame, kDeskbarItemName, resizingMode,
			B_WILL_DRAW | B_TRANSPARENT_BACKGROUND | B_FRAME_EVENTS),
		fInDeskbar(inDeskbar),
		fState(kAgentError),
		fAgentRunning(false),
		fRunner(NULL),
		fActionThread(-1)
	{
		_RefreshStatus();
	}

	BeRDAgentView(BMessage* archive)
		:
		BView(archive),
		fInDeskbar(false),
		fState(kAgentError),
		fAgentRunning(false),
		fRunner(NULL),
		fActionThread(-1)
	{
		app_info info;
		if (be_app->GetAppInfo(&info) == B_OK
			&& strcasecmp(info.signature, kDeskbarSignature) == 0) {
			fInDeskbar = true;
		}
		_RefreshStatus();
	}

	virtual ~BeRDAgentView()
	{
		delete fRunner;
		if (fActionThread >= B_OK) {
			status_t result;
			wait_for_thread(fActionThread, &result);
		}
	}

	static BeRDAgentView* Instantiate(BMessage* archive)
	{
		if (!validate_instantiation(archive, "BeRDAgentView"))
			return NULL;
		return new BeRDAgentView(archive);
	}

	virtual status_t Archive(BMessage* archive, bool deep = true) const
	{
		status_t status = BView::Archive(archive, deep);
		if (status == B_OK)
			status = archive->AddString("add_on", kSignature);
		if (status == B_OK)
			status = archive->AddString("class", "BeRDAgentView");
		return status;
	}

	virtual void AttachedToWindow()
	{
		BView::AttachedToWindow();
		SetViewColor(B_TRANSPARENT_COLOR);
		_RefreshStatus();

		BMessage refresh(kMsgRefresh);
		fRunner = new(std::nothrow) BMessageRunner(
			BMessenger(this), refresh, 2000000);
		if (fRunner != NULL && fRunner->InitCheck() != B_OK) {
			delete fRunner;
			fRunner = NULL;
		}
	}

	virtual void DetachedFromWindow()
	{
		delete fRunner;
		fRunner = NULL;
		BView::DetachedFromWindow();
	}

	virtual void FrameResized(float width, float height)
	{
		BView::FrameResized(width, height);
		Invalidate();
	}

	virtual void Draw(BRect updateRect)
	{
		(void)updateRect;

		BRect bounds = Bounds();
		float size = std::min(bounds.Width() + 1, bounds.Height() + 1) - 4;
		if (size < 8)
			size = std::min(bounds.Width() + 1, bounds.Height() + 1) - 2;
		float left = bounds.left + (bounds.Width() + 1 - size) / 2;
		float top = bounds.top + (bounds.Height() + 1 - size) / 2;
		BRect circle(left, top, left + size - 1, top + size - 1);

		rgb_color color;
		switch (fState) {
			case kAgentRunning:
				color = {46, 180, 91, 255};
				break;
			case kAgentUnconfigured:
				color = {235, 175, 28, 255};
				break;
			default:
				color = {211, 62, 62, 255};
				break;
		}

		SetDrawingMode(B_OP_ALPHA);
		SetHighColor(0, 0, 0, 70);
		BRect shadow(circle);
		shadow.OffsetBy(0, 1);
		FillEllipse(shadow);
		SetHighColor(color);
		FillEllipse(circle);
		SetPenSize(1);
		SetHighColor(35, 35, 35, 160);
		StrokeEllipse(circle);

		BFont font(be_bold_font);
		font.SetSize(std::max(7.0f, size * 0.58f));
		SetFont(&font);
		font_height metrics;
		font.GetHeight(&metrics);
		const char* letter = "B";
		float x = circle.left + (circle.Width() - font.StringWidth(letter)) / 2;
		float y = circle.top
			+ (circle.Height() - (metrics.ascent + metrics.descent)) / 2
			+ metrics.ascent;
		SetHighColor(255, 255, 255, 245);
		DrawString(letter, BPoint(x, y));
	}

	virtual void MouseDown(BPoint point)
	{
		_RefreshStatus();

		BPopUpMenu* menu = new BPopUpMenu(B_EMPTY_STRING, false, false);
		menu->SetAsyncAutoDestruct(true);
		menu->SetFont(be_plain_font);

		BString status("BeRD Agent: ");
		status << fStatusText;
		BMenuItem* statusItem = new BMenuItem(status.String(), NULL);
		statusItem->SetEnabled(false);
		menu->AddItem(statusItem);
		menu->AddSeparatorItem();
		menu->AddItem(new BMenuItem("Dashboard",
			new BMessage(kMsgDashboard)));
		if (fAgentRunning) {
			menu->AddItem(new BMenuItem("Stop Agent",
				new BMessage(kMsgStop)));
		} else {
			menu->AddItem(new BMenuItem("Start Agent",
				new BMessage(kMsgStart)));
		}
		menu->AddItem(new BMenuItem("Log", new BMessage(kMsgLog)));
		menu->AddSeparatorItem();
		menu->AddItem(new BMenuItem("About BeRD",
			new BMessage(kMsgAbout)));
		menu->SetTargetForItems(this);

		ConvertToScreen(&point);
		menu->Go(point, true, true, true);
	}

	virtual void MessageReceived(BMessage* message)
	{
		switch (message->what) {
			case kMsgRefresh:
				_RefreshStatus();
				break;
			case kMsgStart:
				if (access(kConfig, R_OK) != 0) {
					_ShowAlert("BeRD Agent is not configured yet. Create a "
						"one-time installation code in the dashboard, then run "
						"make/create_config.py locally.");
				} else {
					_StartAction(kStartCommand,
						"Could not start the BeRD Agent service.");
				}
				break;
			case kMsgStop:
				_StartAction(kStopCommand,
					"Could not stop the BeRD Agent service.");
				break;
			case kMsgDashboard:
				_StartAction(kDashboardCommand,
					"Could not open the DWService dashboard.");
				break;
			case kMsgLog:
				if (access(kLog, R_OK) != 0)
					_ShowAlert("No BeRD Agent service log exists yet.");
				else
					_StartAction(kLogCommand,
						"Could not open the BeRD Agent service log.");
				break;
			case kMsgAbout:
				_ShowAbout();
				break;
			case kMsgActionDone:
				_ActionFinished(message);
				break;
			default:
				BView::MessageReceived(message);
		}
	}

private:
	void _RefreshStatus()
	{
		BString runtimeDetail;
		bool running = read_runtime_status(runtimeDetail);
		AgentState state;
		BString text;
		if (access(kConfig, R_OK) != 0) {
			state = kAgentUnconfigured;
			text = "Not configured";
		} else if (running) {
			state = kAgentRunning;
			text = "Running and configured";
		} else {
			state = kAgentError;
			text = runtimeDetail;
		}

		bool changed = state != fState || running != fAgentRunning
			|| text != fStatusText;
		fState = state;
		fAgentRunning = running;
		fStatusText = text;

		BString tooltip("BeRD Agent - ");
		tooltip << fStatusText;
		SetToolTip(tooltip.String());
		if (changed && Window() != NULL)
			Invalidate();
	}

	void _StartAction(const char* command, const char* failureMessage)
	{
		if (fActionThread >= B_OK) {
			_ShowAlert("Another BeRD Agent action is still in progress.");
			return;
		}

		ActionRequest* request = new(std::nothrow) ActionRequest;
		if (request == NULL) {
			_ShowAlert(failureMessage);
			return;
		}
		request->command = command;
		request->failureMessage = failureMessage;
		request->target = BMessenger(this);

		fActionThread = spawn_thread(run_action, "BeRD Agent action",
			B_LOW_PRIORITY, request);
		if (fActionThread < B_OK) {
			delete request;
			fActionThread = -1;
			_ShowAlert(failureMessage);
			return;
		}
		resume_thread(fActionThread);
	}

	void _ActionFinished(BMessage* message)
	{
		if (fActionThread >= B_OK) {
			status_t result;
			wait_for_thread(fActionThread, &result);
			fActionThread = -1;
		}

		bool succeeded = false;
		message->FindBool("succeeded", &succeeded);
		if (!succeeded) {
			const char* failure = NULL;
			if (message->FindString("failure", &failure) == B_OK)
				_ShowAlert(failure);
		}
		_RefreshStatus();
	}

	void _ShowAlert(const char* text)
	{
		BAlert* alert = new BAlert("BeRD Agent", text, "OK", NULL, NULL,
			B_WIDTH_AS_USUAL, B_STOP_ALERT);
		alert->SetFlags(alert->Flags() | B_CLOSE_ON_ESCAPE);
		alert->Go(NULL);
	}

	void _ShowAbout()
	{
		BAlert* alert = new BAlert("About BeRD Agent",
			"BeRD Agent\nBeRD for Haiku\n\n"
			"An experimental, unofficial Haiku port of the DWService "
			"Agent. BeRD is not affiliated with or endorsed by DWSNET "
			"s.r.l.\n\nLicensed under MPL-2.0; bundled components may "
			"retain their own licenses.",
			"OK");
		alert->SetFlags(alert->Flags() | B_CLOSE_ON_ESCAPE);
		alert->Go(NULL);
	}

	bool fInDeskbar;
	AgentState fState;
	bool fAgentRunning;
	BString fStatusText;
	BMessageRunner* fRunner;
	thread_id fActionThread;
};


extern "C" _EXPORT BView*
instantiate_deskbar_item(float maxWidth, float maxHeight)
{
	(void)maxWidth;
	return new BeRDAgentView(BRect(0, 0, maxHeight - 1, maxHeight - 1),
		B_FOLLOW_LEFT | B_FOLLOW_TOP, true);
}


class BeRDAgentApplication : public BApplication {
public:
	BeRDAgentApplication()
		:
		BApplication(kSignature),
		fRemove(false),
		fShowHelp(false),
		fResult(B_OK)
	{
	}

	virtual void ArgvReceived(int32 argc, char** argv)
	{
		for (int32 index = 1; index < argc; index++) {
			if (strcmp(argv[index], "--remove") == 0)
				fRemove = true;
			else if (strcmp(argv[index], "--install") == 0)
				fRemove = false;
			else if (strcmp(argv[index], "--help") == 0
				|| strcmp(argv[index], "-h") == 0) {
				fShowHelp = true;
			} else {
				fprintf(stderr, "Unknown option: %s\n", argv[index]);
				fShowHelp = true;
				fResult = B_BAD_VALUE;
			}
		}
	}

	virtual void ReadyToRun()
	{
		if (fShowHelp) {
			puts("BeRDAgent options:\n"
				"  --install  add the BeRD Agent replicant to Deskbar\n"
				"  --remove   remove only the BeRD Agent replicant\n"
				"  --help     show this help");
			Quit();
			return;
		}

		BDeskbar deskbar;
		if (!deskbar.IsRunning()) {
			fprintf(stderr, "BeRD Agent: Deskbar is not running.\n");
			fResult = B_ERROR;
			Quit();
			return;
		}

		if (fRemove) {
			if (deskbar.HasItem(kDeskbarItemName))
				fResult = deskbar.RemoveItem(kDeskbarItemName);
			Quit();
			return;
		}

		if (!deskbar.HasItem(kDeskbarItemName)) {
			app_info info;
			fResult = GetAppInfo(&info);
			if (fResult == B_OK)
				fResult = deskbar.AddItem(&info.ref);
		}
		Quit();
	}

	status_t Result() const
	{
		return fResult;
	}

private:
	bool fRemove;
	bool fShowHelp;
	status_t fResult;
};


int
main()
{
	BeRDAgentApplication application;
	application.Run();
	return application.Result() == B_OK ? 0 : 1;
}
