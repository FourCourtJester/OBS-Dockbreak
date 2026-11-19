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

#include <QMainWindow>

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE(PLUGIN_NAME, "en-US")

/*
 * 32.0.0 is the first release with the plugin manager, which is the floor this
 * plugin is supported on.
 *
 * This check is load-bearing, not belt-and-braces. libobs resolves the
 * obs_module_ver export at open time but never compares it -- as of 32.0.0
 * MODULE_INCOMPATIBLE_VER is defined and never returned. Nothing rejects a
 * module built against newer headers, so we have to refuse older hosts
 * ourselves.
 */
static constexpr uint32_t MINIMUM_OBS_VERSION = MAKE_SEMANTIC_VERSION(32, 0, 0);

/* The plugin's own file under plugin_config/<module>/, never OBS's config. */
static const char *GEOMETRY_FILE = "detached-docks.json";

static DockBreak *dock_break = nullptr;
static bool callback_registered = false;

static void stop(void)
{
	if (!dock_break)
		return;

	dock_break->shutdown();
	delete dock_break;
	dock_break = nullptr;
}

static void frontend_event(enum obs_frontend_event event, void *)
{
	switch (event) {
	case OBS_FRONTEND_EVENT_FINISHED_LOADING:
		/*
		 * OBS has restored its saved layout by now (restoreState runs in
		 * OBSBasic::OBSInit well before this event), so any dock that was
		 * floating when OBS last closed is floating again. They are
		 * released here if they were not caught on the way.
		 */
		if (dock_break)
			dock_break->finishedLoading();
		break;

	case OBS_FRONTEND_EVENT_THEME_CHANGED:
		/*
		 * Raised from OBSApp::StyleChanged, after OBS has applied the new
		 * palette -- and so after Qt has re-decided every title bar for
		 * that palette by its own rule.
		 */
		if (dock_break)
			dock_break->themeChanged();
		break;

	case OBS_FRONTEND_EVENT_EXIT:
		/*
		 * The last safe moment to touch the frontend: OBSBasic calls
		 * obs_frontend_set_callbacks_internal(nullptr) on the very next
		 * line. The dock layout has already been saved by this point.
		 */
		stop();

		obs_frontend_remove_event_callback(frontend_event, nullptr);
		callback_registered = false;
		break;

	default:
		break;
	}
}

const char *obs_module_name(void)
{
	return obs_module_text("Plugin.Name");
}

const char *obs_module_description(void)
{
	return obs_module_text("Plugin.Description");
}

bool obs_module_load(void)
{
	const uint32_t version = obs_get_version();

	if (version < MINIMUM_OBS_VERSION) {
		obs_log(LOG_WARNING, "requires OBS %u.%u.%u or later, running on %s -- not loading",
			MINIMUM_OBS_VERSION >> 24, (MINIMUM_OBS_VERSION >> 16) & 0xFF, MINIMUM_OBS_VERSION & 0xFFFF,
			obs_get_version_string());
		return false;
	}

	obs_frontend_add_event_callback(frontend_event, nullptr);
	callback_registered = true;

	obs_log(LOG_INFO, "plugin loaded successfully (version %s)", PLUGIN_VERSION);
	return true;
}

void obs_module_post_load(void)
{
	if (!callback_registered || dock_break)
		return;

	/*
	 * Started here so the watcher is in place before OBSBasic::OBSInit calls
	 * restoreState(): docks restored as floating are then released as they
	 * are shown, rather than appearing owned and being fixed afterwards.
	 */
	QMainWindow *main = qobject_cast<QMainWindow *>(static_cast<QWidget *>(obs_frontend_get_main_window()));
	if (!main) {
		obs_log(LOG_WARNING, "no main window -- nothing to do");
		return;
	}

	char *path = obs_module_config_path(GEOMETRY_FILE);
	const QString storePath = path ? QString::fromUtf8(path) : QString();
	bfree(path);

	if (storePath.isEmpty())
		obs_log(LOG_WARNING, "no plugin config directory -- dock positions will not be remembered");

	dock_break = new DockBreak(main, storePath);
}

void obs_module_unload(void)
{
	/*
	 * Normally EXIT has already cleaned up. This only matters when the
	 * module is unloaded without the frontend having run its shutdown path.
	 * The frontend is not touched here: by now its calls are no-ops.
	 */
	if (callback_registered) {
		obs_frontend_remove_event_callback(frontend_event, nullptr);
		callback_registered = false;
	}

	delete dock_break;
	dock_break = nullptr;

	obs_log(LOG_INFO, "plugin unloaded");
}
