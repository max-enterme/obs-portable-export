#include <doctest/doctest.h>

#include "portable/convert.hpp"

#include <algorithm>

using portable::ConversionPlan;
using portable::FileKind;
using portable::Json;
using portable::MissingReason;

namespace {

// テスト用の stat: 与えられたパス集合に一致すれば RegularFile / Directory、それ以外は Missing。
portable::StatFn make_stat(const std::vector<std::string> &regular_files,
			    const std::vector<std::string> &directories = {})
{
	return [regular_files, directories](const std::string &path) {
		for (const auto &d : directories) {
			if (path == d)
				return FileKind::Directory;
		}
		for (const auto &f : regular_files) {
			if (path == f)
				return FileKind::RegularFile;
		}
		return FileKind::Missing;
	};
}

} // namespace

TEST_CASE("is_local_path: 絶対パスを判定する")
{
	CHECK(portable::is_local_path("C:/a.png"));
	CHECK(portable::is_local_path("C:\\a.png"));
	CHECK(portable::is_local_path("/home/a.png"));
	CHECK(portable::is_local_path("\\\\nas\\a.png"));
	CHECK(portable::is_local_path("//nas/a.png"));
}

TEST_CASE("is_local_path: 相対パスと URL は対象外")
{
	CHECK_FALSE(portable::is_local_path(""));
	CHECK_FALSE(portable::is_local_path("./assets/a.png"));
	CHECK_FALSE(portable::is_local_path("../a.png"));
	CHECK_FALSE(portable::is_local_path("https://example.com/a.png"));
	CHECK_FALSE(portable::is_local_path("file:///C:/a.png"));
	CHECK_FALSE(portable::is_local_path("ab"));
}

TEST_CASE("sanitize_filename: 置換と削り")
{
	CHECK(portable::sanitize_filename("背景画像") == "背景画像");
	CHECK(portable::sanitize_filename("a<>:\"/\\|?*b") == "a_________b");
	CHECK(portable::sanitize_filename("  name  ") == "name");
	CHECK(portable::sanitize_filename("name...") == "name");
	CHECK(portable::sanitize_filename("") == "asset");
	CHECK(portable::sanitize_filename("con") == "con_");
}

TEST_CASE("sanitize_filename: 80 バイトで切る")
{
	std::string input;
	for (int i = 0; i < 40; ++i)
		input += "あ";

	std::string expected;
	for (int i = 0; i < 26; ++i)
		expected += "あ";

	CHECK(portable::sanitize_filename(input) == expected);
}

TEST_CASE("same_file_key: 表記ゆれを畳む")
{
	CHECK(portable::same_file_key("C:\\Media\\BG.png") == "c:/media/bg.png");
	CHECK(portable::same_file_key("c:/media/./bg.png") == "c:/media/bg.png");
	CHECK(portable::same_file_key("C:/Media/x/../BG.png") == "c:/media/bg.png");
}

TEST_CASE("same_file_key: 日本語の別ファイルは別の値")
{
	CHECK(portable::same_file_key("C:/素材/背景.png") == "c:/素材/背景.png");
	CHECK(portable::same_file_key("C:/素材/前景.png") == "c:/素材/前景.png");
	CHECK(portable::same_file_key("C:/素材/背景.png") != portable::same_file_key("C:/素材/前景.png"));
}

TEST_CASE("same_file_key: UNC を保つ")
{
	CHECK(portable::same_file_key("\\\\nas\\a\\..\\b.png") == "//nas/b.png");
}

TEST_CASE("utf8 パス変換: 往復")
{
	std::string original = "素材/背景.png";
	std::string back = portable::utf8_from_path(portable::path_from_utf8(original));
	std::replace(back.begin(), back.end(), '\\', '/');
	CHECK(back == original);
}

TEST_CASE("plan: 単体参照 6 キー")
{
	Json collection = Json::parse(R"JSON({
		"sources": [
			{"name": "背景", "settings": {"file": "C:/m/a.png"}},
			{"name": "BGM", "settings": {"local_file": "C:/m/b.wav"}},
			{"name": "動画", "settings": {"path": "C:/m/c.mp4"}},
			{"name": "マスク", "settings": {"image_path": "C:/m/d.png"}},
			{"name": "マット", "settings": {"track_matte_path": "C:/m/e.mov"}},
			{"name": "字幕", "settings": {"text_file": "C:/m/f.txt"}}
		]
	})JSON");

	auto stat = make_stat({"C:/m/a.png", "C:/m/b.wav", "C:/m/c.mp4", "C:/m/d.png", "C:/m/e.mov", "C:/m/f.txt"});
	ConversionPlan plan = portable::plan_conversion(collection, stat);

	CHECK(plan.converted["sources"][0]["settings"]["file"] == "./assets/背景.png");
	CHECK(plan.converted["sources"][1]["settings"]["local_file"] == "./assets/BGM.wav");
	CHECK(plan.converted["sources"][2]["settings"]["path"] == "./assets/動画.mp4");
	CHECK(plan.converted["sources"][3]["settings"]["image_path"] == "./assets/マスク.png");
	CHECK(plan.converted["sources"][4]["settings"]["track_matte_path"] == "./assets/マット.mov");
	CHECK(plan.converted["sources"][5]["settings"]["text_file"] == "./assets/字幕.txt");
	CHECK(plan.copies.size() == 6);
}

TEST_CASE("plan: 入れ子の設定オブジェクト")
{
	Json collection = Json::parse(R"JSON({
		"transitions": [{"name": "フェード", "settings": {"path": "C:/m/t.mov"}}],
		"groups": [{"name": "G", "settings": {"file": "C:/m/g.png"}}],
		"sources": [
			{"name": "画像", "settings": {}, "filters": [{"name": "切り抜き", "settings": {"image_path": "C:/m/k.png"}}]},
			{"name": "シーン", "settings": {"items": [{"name": "画像", "show_transition": {"name": "登場", "transition": {"path": "C:/m/s.webm"}}}]}}
		]
	})JSON");

	auto stat = make_stat({"C:/m/t.mov", "C:/m/g.png", "C:/m/k.png", "C:/m/s.webm"});
	ConversionPlan plan = portable::plan_conversion(collection, stat);

	CHECK(plan.converted["transitions"][0]["settings"]["path"] == "./assets/フェード.mov");
	CHECK(plan.converted["groups"][0]["settings"]["file"] == "./assets/G.png");
	CHECK(plan.converted["sources"][0]["filters"][0]["settings"]["image_path"] == "./assets/切り抜き.png");
	CHECK(plan.converted["sources"][1]["settings"]["items"][0]["show_transition"]["transition"]["path"] ==
	      "./assets/登場.webm");
}

TEST_CASE("plan: 設定オブジェクトの外は触らない")
{
	Json collection = Json::parse(R"JSON({
		"modules": {
			"x": {"path": "C:/m/z.png"},
			"scripts-tool": [{"path": "C:/s/a.lua", "settings": {"path": "C:/m/z.png"}}]
		}
	})JSON");

	auto stat = make_stat({"C:/m/z.png", "C:/s/a.lua"});
	ConversionPlan plan = portable::plan_conversion(collection, stat);

	CHECK(plan.converted == collection);
	CHECK(plan.copies.empty());
	CHECK(plan.missing.empty());
}

TEST_CASE("plan: 拡張子の二重付けをしない")
{
	Json collection = Json::parse(R"JSON({
		"sources": [{"name": "bg.PNG", "settings": {"file": "C:/m/bg.png"}}]
	})JSON");

	auto stat = make_stat({"C:/m/bg.png"});
	ConversionPlan plan = portable::plan_conversion(collection, stat);

	CHECK(plan.converted["sources"][0]["settings"]["file"] == "./assets/bg.png");
}

TEST_CASE("plan: url・相対パスは触らない")
{
	Json collection = Json::parse(R"JSON({
		"sources": [{"name": "web", "settings": {"url": "https://x/y.html", "file": "./assets/a.png"}}]
	})JSON");

	auto stat = make_stat({});
	ConversionPlan plan = portable::plan_conversion(collection, stat);

	CHECK(plan.converted["sources"][0]["settings"]["url"] == "https://x/y.html");
	CHECK(plan.converted["sources"][0]["settings"]["file"] == "./assets/a.png");
	CHECK(plan.copies.empty());
	CHECK(plan.missing.empty());
}

TEST_CASE("plan: スライドショーと VLC")
{
	Json collection = Json::parse(R"JSON({
		"sources": [
			{"name": "slideshow", "settings": {"files": [{"value": "C:/d/e/a.png", "hidden": false}, {"value": "C:/o/b.png"}]}},
			{"name": "vlc", "settings": {"playlist": [{"value": "C:/v/m.mp4", "selected": true}]}}
		]
	})JSON");

	auto stat = make_stat({"C:/d/e/a.png", "C:/o/b.png", "C:/v/m.mp4"});
	ConversionPlan plan = portable::plan_conversion(collection, stat);

	auto &files = plan.converted["sources"][0]["settings"]["files"];
	CHECK(files[0]["value"] == "./assets/a.png");
	CHECK(files[0]["hidden"] == false);
	CHECK(files[1]["value"] == "./assets/b.png");

	auto &playlist = plan.converted["sources"][1]["settings"]["playlist"];
	CHECK(playlist[0]["value"] == "./assets/m.mp4");
	CHECK(playlist[0]["selected"] == true);
}

TEST_CASE("plan: 同名別ファイルは連番")
{
	Json collection = Json::parse(R"JSON({
		"sources": [
			{"name": "画像", "settings": {"file": "C:/a/x.png"}},
			{"name": "画像", "settings": {"file": "C:/b/y.png"}}
		]
	})JSON");

	auto stat = make_stat({"C:/a/x.png", "C:/b/y.png"});
	ConversionPlan plan = portable::plan_conversion(collection, stat);

	CHECK(plan.converted["sources"][0]["settings"]["file"] == "./assets/画像.png");
	CHECK(plan.converted["sources"][1]["settings"]["file"] == "./assets/画像_2.png");
}

TEST_CASE("plan: 大小違いも衝突扱い")
{
	Json collection = Json::parse(R"JSON({
		"sources": [
			{"name": "BG", "settings": {"file": "C:/a/1.png"}},
			{"name": "bg", "settings": {"file": "C:/b/2.png"}}
		]
	})JSON");

	auto stat = make_stat({"C:/a/1.png", "C:/b/2.png"});
	ConversionPlan plan = portable::plan_conversion(collection, stat);

	CHECK(plan.converted["sources"][0]["settings"]["file"] == "./assets/BG.png");
	CHECK(plan.converted["sources"][1]["settings"]["file"] == "./assets/bg_2.png");
}

TEST_CASE("plan: 同じファイルは共有")
{
	Json collection = Json::parse(R"JSON({
		"sources": [
			{"name": "背景1", "settings": {"file": "C:/m/bg.png"}},
			{"name": "背景2", "settings": {"file": "C:/m/bg.png"}},
			{"name": "slideshow", "settings": {"files": [{"value": "C:/m/bg.png"}, {"value": "C:/m/bg.png"}]}}
		]
	})JSON");

	auto stat = make_stat({"C:/m/bg.png"});
	ConversionPlan plan = portable::plan_conversion(collection, stat);

	CHECK(plan.converted["sources"][0]["settings"]["file"] == "./assets/背景1.png");
	CHECK(plan.converted["sources"][1]["settings"]["file"] == "./assets/背景1.png");
	CHECK(plan.converted["sources"][2]["settings"]["files"][0]["value"] == "./assets/背景1.png");
	CHECK(plan.converted["sources"][2]["settings"]["files"][1]["value"] == "./assets/背景1.png");
	CHECK(plan.copies.size() == 1);
}

TEST_CASE("plan: 見つからない・フォルダ")
{
	Json collection = Json::parse(R"JSON({
		"sources": [
			{"name": "欠け", "settings": {"file": "C:/m/none.png"}},
			{"name": "フォルダ参照", "settings": {"file": "C:/m/dir"}}
		]
	})JSON");

	auto stat = make_stat({}, {"C:/m/dir"});
	ConversionPlan plan = portable::plan_conversion(collection, stat);

	CHECK(plan.converted["sources"][0]["settings"]["file"] == "C:/m/none.png");
	CHECK(plan.converted["sources"][1]["settings"]["file"] == "C:/m/dir");
	REQUIRE(plan.missing.size() == 2);
	CHECK(plan.missing[0].source_name == "欠け");
	CHECK(plan.missing[0].reason == MissingReason::NotFound);
	CHECK(plan.missing[1].source_name == "フォルダ参照");
	CHECK(plan.missing[1].reason == MissingReason::IsDirectory);
}

TEST_CASE("plan: 形が欠けていても落ちない")
{
	Json collection = Json::parse(R"JSON({
		"sources": [
			{"name": "no_settings"},
			{"settings": {"file": "C:/m/missing2.png"}},
			{"name": "weird", "settings": {"files": "C:/not/an/array.png"}}
		]
	})JSON");

	auto stat = make_stat({});
	ConversionPlan plan;
	CHECK_NOTHROW(plan = portable::plan_conversion(collection, stat));

	bool found_unnamed_missing = false;
	for (const auto &m : plan.missing) {
		if (m.source_name.empty() && m.original_path == "C:/m/missing2.png" && m.reason == MissingReason::NotFound)
			found_unnamed_missing = true;
	}
	CHECK(found_unnamed_missing);

	Json no_sources = Json::parse(R"JSON({"name": "x"})JSON");
	CHECK_NOTHROW(portable::plan_conversion(no_sources, stat));
}

TEST_CASE("plan: その他のキーと順番を保つ")
{
	Json collection = Json::parse(R"JSON({
		"name": "Coll",
		"current_scene": "Scene1",
		"sources": [],
		"scene_order": []
	})JSON");

	auto stat = make_stat({});
	ConversionPlan plan = portable::plan_conversion(collection, stat);

	std::vector<std::string> keys;
	for (auto it = plan.converted.begin(); it != plan.converted.end(); ++it)
		keys.push_back(it.key());

	REQUIRE(keys.size() == 4);
	CHECK(keys[0] == "name");
	CHECK(keys[1] == "current_scene");
	CHECK(keys[2] == "sources");
	CHECK(keys[3] == "scene_order");
	CHECK(plan.converted["name"] == "Coll");
	CHECK(plan.converted["current_scene"] == "Scene1");
}
