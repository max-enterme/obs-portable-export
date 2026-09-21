/*
Portable Export
Copyright (C) 2026 max-enterme 112470175+max-enterme@users.noreply.github.com

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

#include <obs-frontend-api.h>
#include <obs-module.h>
#include <plugin-support.h>

#include "export-dialog.hpp"

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE(PLUGIN_NAME, "en-US")

static void on_export_menu(void *)
{
	run_export(static_cast<QWidget *>(obs_frontend_get_main_window()));
}

// OBS_FRONTEND_EVENT_EXIT はメインウィンドウと Qt のイベントループがまだ生きているうちに
// 出る。ここで export_shutdown() を呼んで書き出しスレッドの終了を待つことで、
// obs_module_unload(メインウィンドウ破棄より後)まで待つより先に、確実に join を終わらせる。
static void on_frontend_event(enum obs_frontend_event event, void *)
{
	if (event == OBS_FRONTEND_EVENT_EXIT)
		export_shutdown();
}

bool obs_module_load(void)
{
	obs_frontend_add_tools_menu_item(obs_module_text("PortableExport.Menu"), on_export_menu, nullptr);
	obs_frontend_add_event_callback(on_frontend_event, nullptr);
	return true;
}

void obs_module_unload(void)
{
	obs_frontend_remove_event_callback(on_frontend_event, nullptr);
	// OBS_FRONTEND_EVENT_EXIT で既に呼ばれているはずだが、export_shutdown() は冪等なので
	// ここでも呼んで二重に安全にする。
	export_shutdown();
}
