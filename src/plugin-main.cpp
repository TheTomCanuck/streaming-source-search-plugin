/*
 * OBS Source Search Plugin
 * Copyright (C) 2024
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <https://www.gnu.org/licenses/>
 */

#include <obs-module.h>
#include <obs-frontend-api.h>

#include <QMainWindow>
#include <QAction>

#include "source-search-dock.hpp"

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("obs-source-search", "en-US")

static SourceSearchDock *searchDock = nullptr;
static obs_hotkey_id searchHotkeyId = OBS_INVALID_HOTKEY_ID;

static void OpenSearchDock(void *data, obs_hotkey_id id, obs_hotkey_t *hotkey, bool pressed)
{
	UNUSED_PARAMETER(data);
	UNUSED_PARAMETER(id);
	UNUSED_PARAMETER(hotkey);

	if (!pressed || !searchDock)
		return;

	// Find the dock widget and show/focus it
	QWidget *parent = searchDock->parentWidget();
	if (parent) {
		parent->show();
		parent->raise();
		parent->activateWindow();
	}
	searchDock->FocusSearchBox();
}

static void OnFrontendEvent(enum obs_frontend_event event, void *data)
{
	UNUSED_PARAMETER(data);

	if (event == OBS_FRONTEND_EVENT_FINISHED_LOADING) {
		// Create the dock
		QMainWindow *mainWindow = static_cast<QMainWindow *>(obs_frontend_get_main_window());
		searchDock = new SourceSearchDock(mainWindow);

		obs_frontend_add_dock_by_id("obs-source-search-dock",
					    obs_module_text("SourceSearch"),
					    searchDock);

		// Register hotkey
		searchHotkeyId = obs_hotkey_register_frontend(
			"obs_source_search.open",
			obs_module_text("OpenSourceSearch"),
			OpenSearchDock,
			nullptr);

		// Initialize the dock
		searchDock->Initialize();

	} else if (event == OBS_FRONTEND_EVENT_SCENE_COLLECTION_CLEANUP) {
		// Clean up before scene collection changes
		if (searchDock) {
			searchDock->Cleanup();
		}

	} else if (event == OBS_FRONTEND_EVENT_SCENE_COLLECTION_CHANGED) {
		// Reinitialize after scene collection changes
		if (searchDock) {
			searchDock->Initialize();
		}

	} else if (event == OBS_FRONTEND_EVENT_SCRIPTING_SHUTDOWN) {
		// Final cleanup
		if (searchDock) {
			searchDock->Cleanup();
		}
	}
}

bool obs_module_load(void)
{
	obs_frontend_add_event_callback(OnFrontendEvent, nullptr);

	// Add menu item to Tools menu
	QAction *action = static_cast<QAction *>(
		obs_frontend_add_tools_menu_qaction(obs_module_text("SourceSearch")));

	QObject::connect(action, &QAction::triggered, []() {
		if (searchDock) {
			QWidget *parent = searchDock->parentWidget();
			if (parent) {
				parent->show();
				parent->raise();
				parent->activateWindow();
			}
			searchDock->FocusSearchBox();
		}
	});

	blog(LOG_INFO, "[Source Search] Plugin loaded successfully (version %s)", "1.0.0");
	return true;
}

void obs_module_unload(void)
{
	obs_frontend_remove_event_callback(OnFrontendEvent, nullptr);

	if (searchHotkeyId != OBS_INVALID_HOTKEY_ID) {
		obs_hotkey_unregister(searchHotkeyId);
	}

	blog(LOG_INFO, "[Source Search] Plugin unloaded");
}

const char *obs_module_name(void)
{
	return "Source Search";
}

const char *obs_module_description(void)
{
	return "Search for sources across all scenes including Vertical Canvas";
}
