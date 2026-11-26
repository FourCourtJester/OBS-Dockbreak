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

#include "GeometryStore.hpp"

#include <obs-module.h>
#include <plugin-support.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

#include <utility>

/* Bumped only if the layout of the file changes incompatibly. A file with any
 * other version is ignored, which costs nothing but remembered positions. */
static constexpr int FILE_VERSION = 1;

GeometryStore::GeometryStore(QString path_) : path(std::move(path_)) {}

void GeometryStore::load()
{
	geometry.clear();
	maximized.clear();
	dirty = false;

	if (path.isEmpty())
		return;

	QFile file(path);
	if (!file.exists())
		return;

	if (!file.open(QIODevice::ReadOnly)) {
		obs_log(LOG_WARNING, "could not open '%s' -- starting without saved geometry",
			path.toUtf8().constData());
		return;
	}

	QJsonParseError error;
	const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &error);
	if (error.error != QJsonParseError::NoError || !doc.isObject()) {
		obs_log(LOG_WARNING, "could not parse '%s' (%s) -- starting without saved geometry",
			path.toUtf8().constData(), error.errorString().toUtf8().constData());
		return;
	}

	const QJsonObject root = doc.object();
	if (root.value("version").toInt() != FILE_VERSION) {
		obs_log(LOG_WARNING, "'%s' has an unknown version -- starting without saved geometry",
			path.toUtf8().constData());
		return;
	}

	const QJsonObject docks = root.value("docks").toObject();
	for (auto it = docks.begin(); it != docks.end(); ++it) {
		const QJsonObject g = it.value().toObject();
		const QRect rect(g.value("x").toInt(), g.value("y").toInt(), g.value("width").toInt(),
				 g.value("height").toInt());

		if (!it.key().isEmpty() && rect.isValid()) {
			geometry.insert(it.key(), rect);
			if (g.value("maximized").toBool())
				maximized.insert(it.key());
		}
	}

	obs_log(LOG_INFO, "loaded saved geometry for %d dock(s)", int(geometry.size()));
}

bool GeometryStore::set(const QString &name, const QRect &rect)
{
	if (name.isEmpty() || !rect.isValid())
		return false;

	auto it = geometry.find(name);
	if (it != geometry.end() && *it == rect)
		return false;

	geometry.insert(name, rect);
	dirty = true;
	return true;
}

bool GeometryStore::setMaximized(const QString &name, bool on)
{
	if (name.isEmpty() || maximized.contains(name) == on)
		return false;

	if (on)
		maximized.insert(name);
	else
		maximized.remove(name);
	dirty = true;
	return true;
}

bool GeometryStore::save()
{
	if (!dirty || path.isEmpty())
		return true;

	/* plugin_config/<module>/ does not exist until something creates it. */
	const QFileInfo info(path);
	if (!QDir().mkpath(info.absolutePath())) {
		obs_log(LOG_WARNING, "could not create '%s'", info.absolutePath().toUtf8().constData());
		return false;
	}

	QJsonObject docks;
	for (auto it = geometry.cbegin(); it != geometry.cend(); ++it) {
		const QRect &r = it.value();
		QJsonObject entry{{"x", r.x()}, {"y", r.y()}, {"width", r.width()}, {"height", r.height()}};
		/* Only written when true, so files from before it stay valid and
		 * the common case stays small. */
		if (maximized.contains(it.key()))
			entry.insert("maximized", true);
		docks.insert(it.key(), entry);
	}

	const QJsonObject root{
		{"version", FILE_VERSION},
		{"docks", docks},
	};

	/* Written to a temporary file and renamed into place, so a crash mid-write
	 * leaves the previous file intact rather than a truncated one. */
	QSaveFile file(path);
	if (!file.open(QIODevice::WriteOnly)) {
		obs_log(LOG_WARNING, "could not write '%s'", path.toUtf8().constData());
		return false;
	}

	file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
	if (!file.commit()) {
		obs_log(LOG_WARNING, "could not save '%s'", path.toUtf8().constData());
		return false;
	}

	dirty = false;
	return true;
}
