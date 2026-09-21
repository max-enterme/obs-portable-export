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

#include "collection-file.hpp"

#include <cctype>
#include <fstream>
#include <system_error>

#include <obs-frontend-api.h>
#include <obs-module.h>
#include <util/bmem.h>
#include <util/config-file.h>

#include "portable/convert.hpp"

namespace {

std::string to_lower_ascii(std::string s)
{
	for (char &c : s)
		c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
	return s;
}

// obs_module_config_path("") = ".../obs-studio/plugin_config/obs-portable-export/" から
// ".../obs-studio" を割り出し、その下の "basic/scenes" を返す。
std::filesystem::path fallback_scenes_dir()
{
	char *cfg_path = obs_module_config_path("");
	std::string s = cfg_path ? cfg_path : "";
	bfree(cfg_path);

	while (!s.empty() && (s.back() == '/' || s.back() == '\\'))
		s.pop_back();

	std::filesystem::path trimmed = portable::path_from_utf8(s);
	std::filesystem::path obs_studio_dir = trimmed.parent_path().parent_path();
	return obs_studio_dir / "basic" / "scenes";
}

std::filesystem::path scenes_dir()
{
	config_t *app_config = obs_frontend_get_app_config();
	const char *loc = app_config ? config_get_string(app_config, "Locations", "SceneCollections") : nullptr;

	if (loc && *loc) {
		std::filesystem::path dir = portable::path_from_utf8(loc) / "obs-studio" / "basic" / "scenes";
		std::error_code ec;
		if (std::filesystem::is_directory(dir, ec))
			return dir;
	}

	return fallback_scenes_dir();
}

std::string ensure_json_ext(std::string filename)
{
	bool has_json_ext = false;
	if (filename.size() >= 5) {
		std::string ext = to_lower_ascii(filename.substr(filename.size() - 5));
		has_json_ext = ext == ".json";
	}
	if (!has_json_ext)
		filename += ".json";
	return filename;
}

// directory_iterator の operator++() / directory_entry::is_regular_file() は例外を投げる
// オーバーロードなので、ここでは非送出版(std::error_code を取るオーバーロード)だけを使う。
// この関数は Qt のイベント配送(QMetaObject::invokeMethod の中)から呼ばれるため、
// Qt6 は例外の伝播をサポートしておらず(qTerminate())、ACL 拒否や壊れたジャンクションなど
// stat できない項目が 1 つあるだけで OBS が落ちてしまう。
std::filesystem::path find_by_current_collection_name(const std::filesystem::path &dir)
{
	char *current_name_raw = obs_frontend_get_current_scene_collection();
	std::string current_name = current_name_raw ? current_name_raw : "";
	bfree(current_name_raw);

	if (current_name.empty())
		return {};

	std::error_code ec;
	if (!std::filesystem::is_directory(dir, ec))
		return {};

	std::filesystem::directory_iterator it(dir, ec);
	const std::filesystem::directory_iterator end;
	if (ec)
		return {};

	for (; it != end; it.increment(ec)) {
		if (ec)
			break;

		std::error_code file_ec;
		bool is_file = it->is_regular_file(file_ec);
		if (file_ec || !is_file)
			continue;

		std::filesystem::path p = it->path();
		if (to_lower_ascii(portable::utf8_from_path(p.extension())) != ".json")
			continue;

		std::ifstream ifs(p, std::ios::binary);
		if (!ifs)
			continue;

		try {
			portable::Json j = portable::Json::parse(ifs);
			if (j.contains("name") && j["name"].is_string() && j["name"].get<std::string>() == current_name)
				return p;
		} catch (...) {
			continue;
		}
	}

	return {};
}

} // namespace

std::filesystem::path current_collection_file()
{
	// この関数は Qt::QueuedConnection のラムダ本体(Qt のイベント配送の中)から呼ばれる。
	// Qt6 は例外の伝播をサポートしていないため、内部で想定外の例外が漏れると
	// qTerminate() で OBS ごと落ちる。空 path を返すだけの経路にする。
	try {
		std::filesystem::path dir = scenes_dir();

		config_t *user_config = obs_frontend_get_user_config();
		const char *filename_raw =
			user_config ? config_get_string(user_config, "Basic", "SceneCollectionFile") : nullptr;

		if (filename_raw && *filename_raw) {
			std::filesystem::path candidate = dir / portable::path_from_utf8(ensure_json_ext(filename_raw));
			std::error_code ec;
			if (std::filesystem::is_regular_file(candidate, ec))
				return candidate;
		}

		return find_by_current_collection_name(dir);
	} catch (...) {
		return {};
	}
}
