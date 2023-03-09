#include <QAction>
#include <QGuiApplication>
#include <QMouseEvent>
#include <QMenu>
#include <QScreen>
#include <QWindow>
#include "projector.hpp"
#include <obs-module.h>
#include <obs-frontend-api.h>
#include <util/config-file.h>
#include <util/platform.h>
#ifdef _WIN32
#include <windows.h>
#endif
static bool mouseSwitching, transitionOnDoubleClick;

static inline void GetScaleAndCenterPos(int baseCX, int baseCY, int windowCX,
					int windowCY, int &x, int &y,
					float &scale)
{
	double windowAspect, baseAspect;
	int newCX, newCY;

	windowAspect = double(windowCX) / double(windowCY);
	baseAspect = double(baseCX) / double(baseCY);

	if (windowAspect > baseAspect) {
		scale = float(windowCY) / float(baseCY);
		newCX = int(double(windowCY) * baseAspect);
		newCY = windowCY;
	} else {
		scale = float(windowCX) / float(baseCX);
		newCX = windowCX;
		newCY = int(float(windowCX) / baseAspect);
	}

	x = windowCX / 2 - newCX / 2;
	y = windowCY / 2 - newCY / 2;
}

static inline void startRegion(int vX, int vY, int vCX, int vCY, float oL,
			       float oR, float oT, float oB)
{
	gs_projection_push();
	gs_viewport_push();
	gs_set_viewport(vX, vY, vCX, vCY);
	gs_ortho(oL, oR, oT, oB, -100.0f, 100.0f);
}

static inline void endRegion()
{
	gs_viewport_pop();
	gs_projection_pop();
}

OBSProjector::OBSProjector(CanvasDock *canvas_, int monitor)
	: OBSQTDisplay(nullptr, Qt::Window), canvas(canvas_)
{
	isAlwaysOnTop = config_get_bool(obs_frontend_get_global_config(),
					"BasicWindow", "ProjectorAlwaysOnTop");

	if (isAlwaysOnTop)
		setWindowFlags(Qt::WindowStaysOnTopHint);

	// Mark the window as a projector so SetDisplayAffinity
	// can skip it
	windowHandle()->setProperty("isOBSProjectorWindow", true);

#if defined(__linux__) || defined(__FreeBSD__) || defined(__DragonFly__)
	// Prevents resizing of projector windows
	setAttribute(Qt::WA_PaintOnScreen, false);
#endif

#ifdef __APPLE__
	setWindowIcon(
		QIcon::fromTheme("obs", QIcon(":/res/images/obs_256x256.png")));
#else
	setWindowIcon(QIcon::fromTheme("obs", QIcon(":/res/images/obs.png")));
#endif

	if (monitor == -1)
		resize(480, 270);
	else
		SetMonitor(monitor);

	UpdateProjectorTitle();

	QAction *action = new QAction(this);
	action->setShortcut(Qt::Key_Escape);
	addAction(action);
	connect(action, SIGNAL(triggered()), this, SLOT(EscapeTriggered()));

	setAttribute(Qt::WA_DeleteOnClose, true);

	//disable application quit when last window closed
	setAttribute(Qt::WA_QuitOnClose, false);

	//installEventFilter(CreateShortcutFilter());

	auto addDrawCallback = [this]() {
		obs_display_add_draw_callback(GetDisplay(), OBSRender, this);
		obs_display_set_background_color(GetDisplay(), 0x000000);
	};

	connect(this, &OBSQTDisplay::DisplayCreated, addDrawCallback);
	//connect(App(), &QGuiApplication::screenRemoved, this,	&OBSProjector::ScreenRemoved);

	//App()->IncrementSleepInhibition();

	ready = true;

	show();

	// We need it here to allow keyboard input in X11 to listen to Escape
	activateWindow();
}

OBSProjector::~OBSProjector()
{
	obs_display_remove_draw_callback(GetDisplay(), OBSRender, this);

	//App()->DecrementSleepInhibition();

	screen = nullptr;
}

void OBSProjector::SetMonitor(int monitor)
{
	savedMonitor = monitor;
	screen = QGuiApplication::screens()[monitor];
	setGeometry(screen->geometry());
	showFullScreen();
	SetHideCursor();
}

void OBSProjector::SetHideCursor()
{
	if (savedMonitor == -1)
		return;

	bool hideCursor = config_get_bool(obs_frontend_get_global_config(),
					  "BasicWindow", "HideProjectorCursor");

	if (hideCursor)
		setCursor(Qt::BlankCursor);
	else
		setCursor(Qt::ArrowCursor);
}

void OBSProjector::OBSRender(void *data, uint32_t cx, uint32_t cy)
{
	OBSProjector *window = reinterpret_cast<OBSProjector *>(data);

	if (!window->ready)
		return;

	obs_view_t *view = window->canvas->view;

	uint32_t targetCX;
	uint32_t targetCY;
	int x, y;
	int newCX, newCY;
	float scale;

	targetCX = window->canvas->canvas_width;
	targetCY = window->canvas->canvas_height;

	GetScaleAndCenterPos(targetCX, targetCY, cx, cy, x, y, scale);

	newCX = int(scale * float(targetCX));
	newCY = int(scale * float(targetCY));

	startRegion(x, y, newCX, newCY, 0.0f, float(targetCX), 0.0f,
		    float(targetCY));

	obs_view_render(view);

	endRegion();
}

void OBSProjector::mousePressEvent(QMouseEvent *event)
{
	OBSQTDisplay::mousePressEvent(event);

	if (event->button() == Qt::RightButton) {
		QMenu popup(this);

		QMenu *projectorMenu = new QMenu(QString::fromUtf8(
			obs_frontend_get_locale_string("Fullscreen")));
		canvas->AddProjectorMenuMonitors(
			projectorMenu, this, SLOT(OpenFullScreenProjector()));
		popup.addMenu(projectorMenu);

		if (GetMonitor() > -1) {
			popup.addAction(QString::fromUtf8(
						obs_frontend_get_locale_string(
							"Windowed")),
					this, SLOT(OpenWindowedProjector()));

		} else if (!this->isMaximized()) {
			popup.addAction(
				QString::fromUtf8(obs_frontend_get_locale_string(
					"ResizeProjectorWindowToContent")),
				this, SLOT(ResizeToContent()));
		}

		QAction *alwaysOnTopButton = new QAction(
			QString::fromUtf8(obs_frontend_get_locale_string(
				"Basic.MainMenu.View.AlwaysOnTop")),
			this);
		alwaysOnTopButton->setCheckable(true);
		alwaysOnTopButton->setChecked(isAlwaysOnTop);

		connect(alwaysOnTopButton, &QAction::toggled, this,
			&OBSProjector::AlwaysOnTopToggled);

		popup.addAction(alwaysOnTopButton);

		popup.addAction(
			QString::fromUtf8(
				obs_frontend_get_locale_string("Close")),
			this, SLOT(EscapeTriggered()));
		popup.exec(QCursor::pos());
	}
}

void OBSProjector::EscapeTriggered()
{
	canvas->DeleteProjector(this);
}

void OBSProjector::UpdateProjectorTitle(QString name)
{
	bool window = (GetMonitor() == -1);

	QString title = QString::fromUtf8(obs_frontend_get_locale_string(
		window ? "PreviewWindow" : "PreviewProjector"));

	setWindowTitle(title);
}

int OBSProjector::GetMonitor()
{
	return savedMonitor;
}

void OBSProjector::RenameProjector(QString oldName, QString newName)
{
	if (oldName == newName)
		return;

	UpdateProjectorTitle(newName);
}

void OBSProjector::OpenFullScreenProjector()
{
	if (!isFullScreen())
		prevGeometry = geometry();

	int monitor = sender()->property("monitor").toInt();
	SetMonitor(monitor);

	UpdateProjectorTitle();
}

void OBSProjector::OpenWindowedProjector()
{
	showFullScreen();
	showNormal();
	setCursor(Qt::ArrowCursor);

	if (!prevGeometry.isNull())
		setGeometry(prevGeometry);
	else
		resize(480, 270);

	savedMonitor = -1;

	UpdateProjectorTitle();
	screen = nullptr;
}

void OBSProjector::ResizeToContent()
{
	uint32_t targetCX = canvas->canvas_width;
	uint32_t targetCY = canvas->canvas_height;
	int x, y, newX, newY;
	float scale;

	QSize size = this->size();
	GetScaleAndCenterPos(targetCX, targetCY, size.width(), size.height(), x,
			     y, scale);

	newX = size.width() - (x * 2);
	newY = size.height() - (y * 2);
	resize(newX, newY);
}

void OBSProjector::AlwaysOnTopToggled(bool isAlwaysOnTop)
{
	SetIsAlwaysOnTop(isAlwaysOnTop, true);
}

void OBSProjector::closeEvent(QCloseEvent *event)
{
	EscapeTriggered();
	event->accept();
}

bool OBSProjector::IsAlwaysOnTop() const
{
	return isAlwaysOnTop;
}

bool OBSProjector::IsAlwaysOnTopOverridden() const
{
	return isAlwaysOnTopOverridden;
}

void OBSProjector::SetIsAlwaysOnTop(bool isAlwaysOnTop, bool isOverridden)
{
	this->isAlwaysOnTop = isAlwaysOnTop;
	this->isAlwaysOnTopOverridden = isOverridden;

	SetAlwaysOnTop(this, isAlwaysOnTop);
}

void OBSProjector::ScreenRemoved(QScreen *screen_)
{
	if (GetMonitor() < 0 || !screen)
		return;

	if (screen == screen_)
		EscapeTriggered();
}

#ifdef _WIN32
bool IsAlwaysOnTop(QWidget *window)
{
	DWORD exStyle = GetWindowLong((HWND)window->winId(), GWL_EXSTYLE);
	return (exStyle & WS_EX_TOPMOST) != 0;
}
#else
bool IsAlwaysOnTop(QWidget *window)
{
	return (window->windowFlags() & Qt::WindowStaysOnTopHint) != 0;
}
#endif

#ifdef _WIN32
void SetAlwaysOnTop(QWidget *window, bool enable)
{
	HWND hwnd = (HWND)window->winId();
	SetWindowPos(hwnd, enable ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0,
		     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
}
#else
void SetAlwaysOnTop(QWidget *window, bool enable)
{
	Qt::WindowFlags flags = window->windowFlags();

	if (enable) {
#ifdef __APPLE__
		/* Force the level of the window high so it sits on top of
		 * full-screen applications like Keynote */
		NSView *nsv = (__bridge NSView *)reinterpret_cast<void *>(
			window->winId());
		NSWindow *nsw = nsv.window;
		[nsw setLevel:1024];
#endif
		flags |= Qt::WindowStaysOnTopHint;
	} else {
		flags &= ~Qt::WindowStaysOnTopHint;
	}

	window->setWindowFlags(flags);
	window->show();
}
#endif

#ifdef _WIN32
#if QT_VERSION < QT_VERSION_CHECK(6, 4, 0)
#define GENERIC_MONITOR_NAME QStringLiteral("Generic PnP Monitor")

struct MonitorData {
	const wchar_t *id;
	MONITORINFOEX info;
	bool found;
};

static BOOL CALLBACK GetMonitorCallback(HMONITOR monitor, HDC, LPRECT,
					LPARAM param)
{
	MonitorData *data = (MonitorData *)param;

	if (GetMonitorInfoW(monitor, &data->info)) {
		if (wcscmp(data->info.szDevice, data->id) == 0) {
			data->found = true;
			return false;
		}
	}

	return true;
}

QString GetMonitorName(const QString &id)
{
	MonitorData data = {};
	data.id = (const wchar_t *)id.utf16();
	data.info.cbSize = sizeof(data.info);

	EnumDisplayMonitors(nullptr, nullptr, GetMonitorCallback,
			    (LPARAM)&data);
	if (!data.found) {
		return GENERIC_MONITOR_NAME;
	}

	UINT32 numPath, numMode;
	if (GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &numPath,
					&numMode) != ERROR_SUCCESS) {
		return GENERIC_MONITOR_NAME;
	}

	std::vector<DISPLAYCONFIG_PATH_INFO> paths(numPath);
	std::vector<DISPLAYCONFIG_MODE_INFO> modes(numMode);

	if (QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &numPath, paths.data(),
			       &numMode, modes.data(),
			       nullptr) != ERROR_SUCCESS) {
		return GENERIC_MONITOR_NAME;
	}

	DISPLAYCONFIG_TARGET_DEVICE_NAME target;
	bool found = false;

	paths.resize(numPath);
	for (size_t i = 0; i < numPath; ++i) {
		const DISPLAYCONFIG_PATH_INFO &path = paths[i];

		DISPLAYCONFIG_SOURCE_DEVICE_NAME s;
		s.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
		s.header.size = sizeof(s);
		s.header.adapterId = path.sourceInfo.adapterId;
		s.header.id = path.sourceInfo.id;

		if (DisplayConfigGetDeviceInfo(&s.header) == ERROR_SUCCESS &&
		    wcscmp(data.info.szDevice, s.viewGdiDeviceName) == 0) {
			target.header.type =
				DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME;
			target.header.size = sizeof(target);
			target.header.adapterId = path.sourceInfo.adapterId;
			target.header.id = path.targetInfo.id;
			found = DisplayConfigGetDeviceInfo(&target.header) ==
				ERROR_SUCCESS;
			break;
		}
	}

	if (!found) {
		return GENERIC_MONITOR_NAME;
	}

	return QString::fromWCharArray(target.monitorFriendlyDeviceName);
}
#endif
#endif
