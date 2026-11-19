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
#endif

/* Window moves and resizes arrive in bursts while the operator drags. The file
 * is written once things settle, and again on exit. */
static constexpr int SAVE_DELAY_MS = 1000;

static QByteArray dock_name(const QDockWidget *dock)
{
	return dock->objectName().toUtf8();
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

	if (dock->isFloating())
		release(dock);
}

bool DockBreak::eventFilter(QObject *watched, QEvent *event)
{
	if (watched == main) {
		if (event->type() == QEvent::ChildPolished) {
			auto *child = static_cast<QChildEvent *>(event)->child();
			if (auto *dock = qobject_cast<QDockWidget *>(child))
				track(dock);
		}
		return false;
	}

	auto *dock = qobject_cast<QDockWidget *>(watched);
	if (!dock || !tracked.contains(dock))
		return false;

	switch (event->type()) {
	case QEvent::Show:
		/* Before show_sys(): the native window never appears owned. */
		if (dock->isFloating())
			release(dock);
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
	QTimer::singleShot(0, this, [this, dock, floating] {
		if (!dock || !tracked.contains(dock.data()))
			return;

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
	});
}

/*
 * Clears the dock's transient parent, so its native window has no owner.
 * Returns true if there was one to clear.
 */
bool DockBreak::release(QDockWidget *dock)
{
	QWindow *window = dock->windowHandle();
	if (!window || !window->transientParent())
		return false;

	window->setTransientParent(nullptr);

#ifdef _WIN32
	/*
	 * Qt only pushes the transient parent to the HWND when the window is
	 * next shown (QWindowsWindow::show_sys -> updateTransientParent). Inside
	 * the Show filter that is about to happen anyway; for a dock that was
	 * already on screen when we found it, it would not happen until the dock
	 * was hidden and shown again. This is the same call Qt makes, applied
	 * now, and it agrees with the state Qt has just been given.
	 */
	if (HWND hwnd = reinterpret_cast<HWND>(dock->internalWinId())) {
		if (GetWindow(hwnd, GW_OWNER))
			SetWindowLongPtr(hwnd, GWLP_HWNDPARENT, 0);
	}
#endif

	obs_log(LOG_INFO, "released floating dock '%s' from the main window", dock_name(dock).constData());
	return true;
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
	if (!QGuiApplication::screenAt(rect.center())) {
		obs_log(LOG_INFO, "saved position of dock '%s' is off screen -- leaving it where it is",
			dock_name(dock).constData());
		return;
	}

	dock->setGeometry(rect);
}
