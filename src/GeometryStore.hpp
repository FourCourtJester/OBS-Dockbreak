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

#include <QHash>
#include <QRect>
#include <QSet>
#include <QString>

/*
 * The last floating geometry of each dock, keyed by the dock's object name.
 *
 * This is the plugin's only persisted state, and it lives in the plugin's own
 * file under plugin_config, never in OBS's user config. The file is disposable:
 * if it is missing or unreadable, docks simply float wherever Qt puts them.
 */
class GeometryStore {
public:
	explicit GeometryStore(QString path);

	void load();
	bool save();

	bool contains(const QString &name) const { return geometry.contains(name); }
	QRect get(const QString &name) const { return geometry.value(name); }

	/* Returns true when the stored value actually changed. */
	bool set(const QString &name, const QRect &rect);

	/* Whether the dock was maximised when last seen floating. Its stored
	 * geometry stays the normal one underneath. */
	bool isMaximized(const QString &name) const { return maximized.contains(name); }
	bool setMaximized(const QString &name, bool on);

	bool isDirty() const { return dirty; }

private:
	QString path;
	QHash<QString, QRect> geometry;
	QSet<QString> maximized;
	bool dirty = false;
};
