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

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE(PLUGIN_NAME, "en-US")

static void on_export_menu(void *)
{
	blog(LOG_INFO, "[obs-portable-export] menu clicked");
}

bool obs_module_load(void)
{
	obs_frontend_add_tools_menu_item(obs_module_text("PortableExport.Menu"), on_export_menu, nullptr);
	return true;
}

void obs_module_unload(void) {}
