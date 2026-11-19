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

#pragma once

#include "GeometryStore.hpp"

#include <QObject>
#include <QPointer>
#include <QSet>
#include <QTimer>

class QDockWidget;
class QMainWindow;

/*
 * Keeps floating docks on screen when OBS's main window is minimised.
 *
 * WHY A FLOATING DOCK DISAPPEARS
 *
 * A floating QDockWidget is a top-level Qt::Tool window whose QWidget parent
 * is still the main window. Qt itself does not hide it when the main window
 * minimises -- QWidgetPrivate::hideChildren() skips children that are windows.
 * What hides it is the platform: Qt gives every top-level window with a parent
 * widget a *transient parent* (QWidgetPrivate::create / reparentWidgetWindows),
 * which on Windows becomes the native OWNER of the HWND, and Windows hides
 * owned windows whenever their owner is minimised.
 *
 * WHAT THIS DOES
 *
 * It clears the transient parent of every floating dock, so the native window
 * has no owner and the OS leaves it alone. On Windows it also gives the window
 * a taskbar button (WS_EX_APPWINDOW): an owned window always stays above its
 * owner, an unowned one can end up behind OBS, and a button is the way back to
 * it. Nothing else changes: the dock keeps its QWidget parent, its Qt::Tool
 * window type and its place in QMainWindow's layout. That matters, because it
 * means:
 *
 *  - OBS's own saveState()/restoreState() still records the dock as floating,
 *    with its geometry and visibility, so a detached dock survives a restart.
 *    (OBSBasic::closeEvent saves DockState before any frontend event fires, so
 *    a dock reparented out of the main window would simply be missing from the
 *    saved layout -- that is why this plugin does not reparent.)
 *  - Re-docking is Qt's own: double-click the title bar, or drag it back in.
 *  - No native window is destroyed or recreated, so a dock hosting a CEF
 *    browser is never re-hosted.
 *
 * WHEN IT HAPPENS
 *
 * Qt resets the transient parent every time a dock floats, because floating
 * goes through setWindowFlags(), which reparents the widget's QWindow. Every
 * path to a visible floating dock ends in QWidget::show(), and show_helper()
 * sends QEvent::Show *before* show_sys() pushes the transient parent to the
 * native window. Clearing it in a Show filter therefore means the window is
 * never shown owned at all. topLevelChanged() is watched as well, both for the
 * geometry below and to catch any path that does not show the dock.
 *
 * GEOMETRY
 *
 * While a dock floats, its geometry is remembered under its object name. When
 * it is floated again -- by the float button, a double-click or the Docks menu,
 * not by dragging it out, which places it under the cursor -- it is put back
 * where it last was, even across restarts during which it stayed docked.
 */
class DockBreak : public QObject {
	Q_OBJECT

public:
	DockBreak(QMainWindow *main, const QString &storePath);
	~DockBreak() override;

	/* OBS_FRONTEND_EVENT_FINISHED_LOADING: OBS has restored its layout. */
	void finishedLoading();

	/* OBS_FRONTEND_EVENT_EXIT: last moment the frontend may be touched. */
	void shutdown();

protected:
	bool eventFilter(QObject *watched, QEvent *event) override;

private:
	void sweep();
	void track(QDockWidget *dock);
	void onTopLevelChanged(QPointer<QDockWidget> dock, bool floating);

	bool release(QDockWidget *dock);
	void rememberGeometry(QDockWidget *dock);
	void restoreGeometry(QDockWidget *dock);

	QPointer<QMainWindow> main;

	/* Raw pointers used only as keys; each entry is removed when its dock
	 * is destroyed, so none is ever dereferenced after that. */
	QSet<QDockWidget *> tracked;

	GeometryStore store;
	QTimer saveTimer;

	/* False until OBS has restored its saved layout. Until then OBS is the
	 * authority on where floating docks go, and the plugin only releases
	 * them -- it neither records nor applies geometry. */
	bool loaded = false;
};
