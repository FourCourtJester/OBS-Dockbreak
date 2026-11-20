/*
Dockbreak
Copyright (C) 2026 FourCourtJester <shaun@mse.gg>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program. If not, see <https://www.gnu.org/licenses/>
*/

#include "DockBreak.hpp"

#include <obs-module.h>
#include <obs-frontend-api.h>
#include <plugin-support.h>

#include <QChildEvent>
#include <QDockWidget>
#include <QEvent>
#include <QGuiApplication>
#include <QMainWindow>
#include <QScreen>
#include <QWindow>

#include <utility>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <dwmapi.h>
#endif

/* Window moves and resizes arrive in bursts while the operator drags. The file
 * is written once things settle, and again on exit. */
static constexpr int SAVE_DELAY_MS = 1000;

static QByteArray dock_name(const QDockWidget *dock)
{
	return dock->objectName().toUtf8();
}

#ifdef _WIN32
/*
 * The attribute Qt itself uses for dark title bars
 * (QWindowsWindow::setDarkBorderToWindow): DWMWA_USE_IMMERSIVE_DARK_MODE is 20
 * from Windows 10 20H1 on, and was 19 before it. Both are tried, as Qt does.
 */
static bool set_dark_title_bar(HWND hwnd, bool dark)
{
	const BOOL value = dark ? TRUE : FALSE;
	return SUCCEEDED(DwmSetWindowAttribute(hwnd, 20, &value, sizeof(value))) ||
	       SUCCEEDED(DwmSetWindowAttribute(hwnd, 19, &value, sizeof(value)));
}
#endif

/*
 * TEMPORARY diagnostics for the first round of Windows testing. Each line
 * records what Qt believes about a dock next to what Windows actually has,
 * so a log from one test run shows where the two part company. Remove
 * before release.
 */
void DockBreak::diag(QDockWidget *dock, const char *what) const
{
	const QRect g = dock->geometry();
	QWindow *window = dock->windowHandle();
	const void *tp = window ? static_cast<const void *>(window->transientParent()) : nullptr;
	const int area = main ? int(main->dockWidgetArea(dock)) : -1;

#ifdef _WIN32
	HWND hwnd = reinterpret_cast<HWND>(dock->internalWinId());
	HWND owner = hwnd ? GetWindow(hwnd, GW_OWNER) : nullptr;
	HWND parent = hwnd ? GetParent(hwnd) : nullptr;
	const LONG_PTR ex = hwnd ? GetWindowLongPtr(hwnd, GWL_EXSTYLE) : 0;
	const LONG_PTR st = hwnd ? GetWindowLongPtr(hwnd, GWL_STYLE) : 0;
	RECT r = {};
	if (hwnd)
		GetWindowRect(hwnd, &r);

	obs_log(LOG_INFO,
		"diag %-22s '%s' qt[vis=%d hidden=%d float=%d native=%d area=%d tp=%p geom=%d,%d %dx%d] "
		"win[hwnd=%p owner=%p parent=%p vis=%d iconic=%d child=%d app=%d tool=%d rect=%ld,%ld %ldx%ld]",
		what, dock_name(dock).constData(), dock->isVisible(), dock->isHidden(), dock->isFloating(),
		dock->testAttribute(Qt::WA_NativeWindow), area, tp, g.x(), g.y(), g.width(), g.height(),
		static_cast<void *>(hwnd), static_cast<void *>(owner), static_cast<void *>(parent),
		hwnd ? int(IsWindowVisible(hwnd)) : -1, hwnd ? int(IsIconic(hwnd)) : -1, int((st & WS_CHILD) != 0),
		int((ex & WS_EX_APPWINDOW) != 0), int((ex & WS_EX_TOOLWINDOW) != 0), long(r.left), long(r.top),
		long(r.right - r.left), long(r.bottom - r.top));
#else
	obs_log(LOG_INFO, "diag %-22s '%s' qt[vis=%d hidden=%d float=%d native=%d area=%d tp=%p geom=%d,%d %dx%d]",
		what, dock_name(dock).constData(), dock->isVisible(), dock->isHidden(), dock->isFloating(),
		dock->testAttribute(Qt::WA_NativeWindow), area, tp, g.x(), g.y(), g.width(), g.height());
#endif
}

void DockBreak::diagAll(const char *what) const
{
	for (QDockWidget *dock : std::as_const(tracked)) {
		if (dock->isFloating() || dock->windowHandle())
			diag(dock, what);
	}
}

DockBreak::DockBreak(QMainWindow *main_, const QString &storePath) : main(main_), store(storePath)
{
	store.load();

	saveTimer.setSingleShot(true);
	saveTimer.setInterval(SAVE_DELAY_MS);
	connect(&saveTimer, &QTimer::timeout, this, [this] { store.save(); });

	/*
	 * New docks are found through the main window rather than by a one-off
	 * search, so docks registered later -- by plugins whose post_load runs
	 * after ours, or browser docks added from the Tools menu while OBS is
	 * running -- are picked up too.
	 *
	 * ChildPolished rather than ChildAdded: ChildAdded is sent from inside
	 * the child's QWidget constructor, before it is a QDockWidget, so the
	 * cast would fail. Polish happens on the first show, which is the first
	 * moment a dock can need releasing anyway.
	 */
	main->installEventFilter(this);
	sweep();
}

DockBreak::~DockBreak()
{
	/* Deliberately touches no widget: by the time this runs from
	 * obs_module_unload() the main window may already be gone. Everything
	 * that needs the frontend happens in shutdown(). Writing the file needs
	 * nothing but the file, and does nothing if shutdown() already did it. */
	store.save();
}

void DockBreak::finishedLoading()
{
	loaded = true;

	/* Catches anything restored floating before it was ever polished, and
	 * records where OBS put each floating dock as the starting point. */
	sweep();

	for (QDockWidget *dock : std::as_const(tracked)) {
		if (dock->isFloating() && dock->isVisible())
			rememberGeometry(dock);
	}

	diagAll("finished-loading");
}

void DockBreak::themeChanged()
{
	for (QDockWidget *dock : std::as_const(tracked))
		applyTitleBar(dock);
}

void DockBreak::shutdown()
{
	saveTimer.stop();
	store.save();

	if (main)
		main->removeEventFilter(this);

	for (QDockWidget *dock : std::as_const(tracked)) {
		dock->removeEventFilter(this);
		disconnect(dock, nullptr, this, nullptr);
	}
	tracked.clear();
}

void DockBreak::sweep()
{
	if (!main)
		return;

	const auto docks = main->findChildren<QDockWidget *>(Qt::FindDirectChildrenOnly);
	for (QDockWidget *dock : docks)
		track(dock);
}

void DockBreak::track(QDockWidget *dock)
{
	if (!dock || tracked.contains(dock))
		return;

	tracked.insert(dock);
	dock->installEventFilter(this);

	connect(dock, &QDockWidget::topLevelChanged, this,
		[this, guarded = QPointer<QDockWidget>(dock)](bool floating) { onTopLevelChanged(guarded, floating); });

	connect(dock, &QObject::destroyed, this, [this, dock] { tracked.remove(dock); });

	diag(dock, "track");

	if (dock->isFloating()) {
		release(dock);
		applyTitleBar(dock);
	}
}

bool DockBreak::eventFilter(QObject *watched, QEvent *event)
{
	if (watched == main) {
		if (event->type() == QEvent::ChildPolished) {
			auto *child = static_cast<QChildEvent *>(event)->child();
			if (auto *dock = qobject_cast<QDockWidget *>(child))
				track(dock);
		}

		switch (event->type()) {
		case QEvent::WindowStateChange:
		case QEvent::Show:
		case QEvent::Hide: {
			const char *what = event->type() == QEvent::Show   ? "main-show"
					   : event->type() == QEvent::Hide ? "main-hide"
					   : main->isMinimized()           ? "main-minimized"
									   : "main-state";
			obs_log(LOG_INFO, "diag %s (spontaneous=%d) main hwnd=%p", what, int(event->spontaneous()),
				reinterpret_cast<void *>(main->internalWinId()));
			diagAll(what);
			/* Again once Windows has finished minimising or restoring. */
			QTimer::singleShot(500, this, [this, what] { diagAll(what); });
			break;
		}
		default:
			break;
		}
		return false;
	}

	auto *dock = qobject_cast<QDockWidget *>(watched);
	if (!dock || !tracked.contains(dock))
		return false;

	switch (event->type()) {
	case QEvent::Show:
		/* Before show_sys(): the native window never appears owned,
		 * without a taskbar button, or with the wrong title bar. */
		diag(dock, event->spontaneous() ? "show (spontaneous)" : "show");
		if (dock->isFloating()) {
			release(dock);
			applyTitleBar(dock);
		}
		break;

	case QEvent::Hide:
		diag(dock, event->spontaneous() ? "hide (spontaneous)" : "hide");
		break;

	case QEvent::WindowStateChange:
		diag(dock, "window-state");
		break;

	case QEvent::ApplicationPaletteChange:
	case QEvent::PaletteChange:
	case QEvent::ThemeChange:
		/*
		 * Qt decides the title bar again on these, by its own rule
		 * (QWindowsWindow::windowEvent -> setDarkBorder), which follows
		 * the Windows colour mode rather than OBS's theme. Ours goes back
		 * on once Qt has finished.
		 */
		if (dock->isFloating())
			scheduleTitleBar(dock);
		break;

	case QEvent::Move:
	case QEvent::Resize:
		if (loaded && dock->isFloating() && dock->isVisible())
			rememberGeometry(dock);
		break;

	default:
		break;
	}

	return false;
}

void DockBreak::onTopLevelChanged(QPointer<QDockWidget> dock, bool floating)
{
	/*
	 * Qt is in the middle of QDockWidgetPrivate::setWindowState() when this
	 * signal fires, and possibly in the middle of QMainWindowLayout's unplug
	 * as well. Nothing is changed from inside it; the work runs once control
	 * is back in the event loop.
	 */
	if (dock)
		diag(dock, floating ? "floated (signal)" : "docked (signal)");

	QTimer::singleShot(0, this, [this, dock, floating] {
		if (!dock || !tracked.contains(dock.data()))
			return;

		diag(dock, floating ? "floated (deferred)" : "docked (deferred)");

		/* Floated and docked again (or the reverse) before we got here:
		 * the later signal has its own turn queued. */
		if (dock->isFloating() != floating)
			return;

		if (!floating) {
			/* Back in the main window. Its saved geometry is kept, so
			 * the next float puts it back where it was. */
			return;
		}

		release(dock);
		applyTitleBar(dock);

		/*
		 * Dragging a dock out of the main window floats it under the
		 * cursor and then grabs the mouse to carry it
		 * (QDockWidgetPrivate::mouseMoveEvent: startDrag(), grabMouse()).
		 * Moving it then would snatch it out from under the pointer. Every
		 * other way of floating -- the float button, a double-click, the
		 * Docks menu -- gets the saved position.
		 */
		if (loaded && dock->isVisible() && QWidget::mouseGrabber() != dock)
			restoreGeometry(dock);

		diag(dock, "floated (done)");
		QTimer::singleShot(500, this, [this, dock] {
			if (dock && tracked.contains(dock.data()))
				diag(dock, "floated (+500ms)");
		});
	});
}

/*
 * Makes a floating dock a window of its own: no owner, so minimising OBS
 * leaves it alone, and a taskbar button, so it can be found again once OBS's
 * main window is in front of it. Idempotent; returns true if anything changed.
 */
bool DockBreak::release(QDockWidget *dock)
{
	QWindow *window = dock->windowHandle();
	if (!window)
		return false;

	bool changed = false;

	if (window->transientParent()) {
		window->setTransientParent(nullptr);
		changed = true;
	}

#ifdef _WIN32
	if (HWND hwnd = reinterpret_cast<HWND>(dock->internalWinId())) {
		/*
		 * Qt only pushes the transient parent to the HWND when the window
		 * is next shown (QWindowsWindow::show_sys -> updateTransientParent).
		 * Inside the Show filter that is about to happen anyway; for a dock
		 * that was already on screen when we found it, it would not happen
		 * until the dock was hidden and shown again. This is the same call
		 * Qt makes, applied now, and it agrees with the state Qt has just
		 * been given.
		 */
		if (GetWindow(hwnd, GW_OWNER))
			SetWindowLongPtr(hwnd, GWLP_HWNDPARENT, 0);

		/*
		 * A floating dock is a Qt::Tool window, which Qt creates with
		 * WS_EX_TOOLWINDOW: no taskbar button. That was harmless while OBS
		 * owned the dock, since an owned window always stays above its
		 * owner. Unowned, clicking OBS can bury the dock, so it gets a
		 * button of its own. WS_EX_APPWINDOW puts a visible top-level window
		 * on the taskbar even when it is a tool window.
		 *
		 * Why not change the dock to Qt::Window instead: that swaps the
		 * small tool caption for a full one, and Qt keeps the OUTER frame
		 * when the frame changes. Every switch would then shift and resize
		 * the dock's contents by the caption difference, and the dock would
		 * creep a few pixels on each restart or Lock UI toggle. This bit
		 * changes nothing about the frame, so Qt's geometry stays exact.
		 *
		 * Qt rewrites GWL_EXSTYLE whenever the dock's window flags change
		 * (WindowCreationData::applyWindowFlags), which drops the bit. Each
		 * such path ends in show(), so the Show filter puts it back, and
		 * does so before ShowWindow() -- which is when the taskbar decides
		 * whether a window gets a button.
		 */
		const LONG_PTR exStyle = GetWindowLongPtr(hwnd, GWL_EXSTYLE);
		if (!(exStyle & WS_EX_APPWINDOW)) {
			SetWindowLongPtr(hwnd, GWL_EXSTYLE, exStyle | WS_EX_APPWINDOW);
			changed = true;

			/* The taskbar only looks at the style when a window is shown,
			 * so one already on screen gets its button the next time it
			 * is. Only a dock floating before this plugin started gets
			 * here, which a dock restored by OBS's layout does not. */
			if (IsWindowVisible(hwnd))
				obs_log(LOG_INFO,
					"dock '%s' was already on screen -- its taskbar button appears when it is next shown",
					dock_name(dock).constData());
		}
	}
#endif

	if (changed) {
		obs_log(LOG_INFO, "released floating dock '%s' from the main window", dock_name(dock).constData());
		diag(dock, "released");
	}

	return changed;
}

/*
 * Gives a floating dock the same light or dark title bar as OBS's theme.
 *
 * Qt draws a dark title bar only when Windows itself is in dark mode AND the
 * window's palette is dark (shouldApplyDarkFrame in qwindowswindow.cpp), so a
 * dark OBS theme on a light-mode Windows gets light title bars. OBS knows
 * which kind its theme is -- obs_frontend_is_theme_dark() -- and that is what
 * the dock follows. It is re-applied whenever OBS changes theme
 * (themeChanged) and whenever Qt has re-decided it (the palette and theme
 * events in eventFilter).
 */
void DockBreak::applyTitleBar(QDockWidget *dock)
{
#ifdef _WIN32
	if (!dock->isFloating())
		return;

	HWND hwnd = reinterpret_cast<HWND>(dock->internalWinId());
	if (!hwnd)
		return;

	if (!set_dark_title_bar(hwnd, obs_frontend_is_theme_dark())) {
		/* Windows 10 before 1809 has no dark title bars at all. */
		obs_log(LOG_DEBUG, "could not set the title bar colour of dock '%s'", dock_name(dock).constData());
		return;
	}

	/* A window already on screen does not repaint its title bar for the
	 * attribute alone. A frame change makes it, without moving, resizing,
	 * activating or restacking anything -- the frame metrics are the same
	 * in light and dark. */
	if (IsWindowVisible(hwnd))
		SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
			     SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER |
				     SWP_NOACTIVATE);
#else
	Q_UNUSED(dock);
#endif
}

void DockBreak::scheduleTitleBar(QDockWidget *dock)
{
	QTimer::singleShot(0, this, [this, guarded = QPointer<QDockWidget>(dock)] {
		if (guarded && tracked.contains(guarded.data()))
			applyTitleBar(guarded);
	});
}

void DockBreak::rememberGeometry(QDockWidget *dock)
{
	if (store.set(dock->objectName(), dock->geometry()))
		saveTimer.start();
}

void DockBreak::restoreGeometry(QDockWidget *dock)
{
	const QString name = dock->objectName();
	if (!store.contains(name))
		return;

	const QRect rect = store.get(name);
	if (rect == dock->geometry())
		return;

	/* A monitor may have been unplugged since the position was saved.
	 * Better to leave the dock where Qt put it than to float it off
	 * screen, where the operator cannot reach it. */
	obs_log(LOG_INFO, "diag restore-geometry '%s' -> %d,%d %dx%d", dock_name(dock).constData(), rect.x(), rect.y(),
		rect.width(), rect.height());

	if (!QGuiApplication::screenAt(rect.center())) {
		obs_log(LOG_INFO, "saved position of dock '%s' is off screen -- leaving it where it is",
			dock_name(dock).constData());
		return;
	}

	dock->setGeometry(rect);
}
