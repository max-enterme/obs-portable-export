#include <doctest/doctest.h>

#include "portable/export.hpp"

#include <fstream>
#include <sstream>
#include <utility>

using portable::ExportOptions;
using portable::ExportResult;
using portable::Json;

namespace {

// テスト用の一時フォルダ。破棄時に中身ごと消す。
class TempDir {
public:
	TempDir()
	{
		auto base = std::filesystem::temp_directory_path();
		for (int i = 0; i < 1000; ++i) {
			auto candidate = base / ("portable_export_test_" + std::to_string(
									 reinterpret_cast<uintptr_t>(this) + static_cast<uintptr_t>(i)));
			std::error_code ec;
			if (std::filesystem::create_directory(candidate, ec)) {
				path_ = candidate;
				return;
			}
		}
		FAIL("一時フォルダを作れなかった");
	}

	~TempDir()
	{
		std::error_code ec;
		std::filesystem::remove_all(path_, ec);
	}

	const std::filesystem::path &path() const { return path_; }

private:
	std::filesystem::path path_;
};

void write_file(const std::filesystem::path &p, const std::string &content)
{
	std::filesystem::create_directories(p.parent_path());
	std::ofstream f(p, std::ios::binary);
	f << content;
}

std::string read_file(const std::filesystem::path &p)
{
	std::ifstream f(p, std::ios::binary);
	std::ostringstream ss;
	ss << f.rdbuf();
	return ss.str();
}

} // namespace

TEST_CASE("export: 出力の形とバイト一致")
{
	TempDir tmp;
	auto src_dir = tmp.path() / "src";
	write_file(src_dir / "one.png", "png-bytes");
	write_file(src_dir / "two.mp4", "mp4-bytes");
	write_file(src_dir / "three.txt", "txt-bytes");

	Json collection;
	collection["name"] = "本配信";
	collection["sources"] = Json::array();
	collection["sources"].push_back({{"name", "Img1"}, {"settings", {{"file", portable::utf8_from_path(src_dir / "one.png")}}}});
	collection["sources"].push_back({{"name", "Vid1"}, {"settings", {{"local_file", portable::utf8_from_path(src_dir / "two.mp4")}}}});
	collection["sources"].push_back({{"name", "Txt1"}, {"settings", {{"text_file", portable::utf8_from_path(src_dir / "three.txt")}}}});

	ExportOptions opt;
	opt.parent_dir = tmp.path() / "out";
	std::filesystem::create_directories(opt.parent_dir);

	ExportResult result = portable::export_collection(collection, opt);

	CHECK(result.out_dir == opt.parent_dir / "本配信");
	CHECK(result.json_path == result.out_dir / "本配信.json");
	CHECK(result.copied == 3);
	CHECK(result.missing.empty());

	CHECK(read_file(result.out_dir / "assets" / "Img1.png") == "png-bytes");
	CHECK(read_file(result.out_dir / "assets" / "Vid1.mp4") == "mp4-bytes");
	CHECK(read_file(result.out_dir / "assets" / "Txt1.txt") == "txt-bytes");

	std::ifstream jf(result.json_path, std::ios::binary);
	Json reread = Json::parse(jf);
	CHECK(reread["sources"][0]["settings"]["file"].get<std::string>() == "./assets/Img1.png");
	CHECK(reread["sources"][1]["settings"]["local_file"].get<std::string>() == "./assets/Vid1.mp4");
	CHECK(reread["sources"][2]["settings"]["text_file"].get<std::string>() == "./assets/Txt1.txt");
}

TEST_CASE("export: フォルダ名の重複を避ける")
{
	TempDir tmp;

	{
		auto parent = tmp.path() / "out1";
		std::filesystem::create_directories(parent / "本配信");

		Json collection;
		collection["name"] = "本配信";

		ExportOptions opt;
		opt.parent_dir = parent;
		ExportResult result = portable::export_collection(collection, opt);
		CHECK(result.out_dir == parent / "本配信_2");
	}

	{
		auto parent = tmp.path() / "out2";
		std::filesystem::create_directories(parent / "本配信");
		write_file(parent / "本配信_2.zip", "zip-placeholder");

		Json collection;
		collection["name"] = "本配信";

		ExportOptions opt;
		opt.parent_dir = parent;
		ExportResult result = portable::export_collection(collection, opt);
		CHECK(result.out_dir == parent / "本配信_3");
	}
}

TEST_CASE("export: 欠落を返す")
{
	TempDir tmp;
	auto src_dir = tmp.path() / "src";
	write_file(src_dir / "ok.png", "ok-bytes");

	Json collection;
	collection["name"] = "col";
	collection["sources"] = Json::array();
	collection["sources"].push_back({{"name", "Ok"}, {"settings", {{"file", portable::utf8_from_path(src_dir / "ok.png")}}}});
	collection["sources"].push_back(
		{{"name", "Missing1"}, {"settings", {{"file", portable::utf8_from_path(src_dir / "none.png")}}}});

	ExportOptions opt;
	opt.parent_dir = tmp.path() / "out";
	std::filesystem::create_directories(opt.parent_dir);

	ExportResult result = portable::export_collection(collection, opt);

	CHECK(result.copied == 1);
	REQUIRE(result.missing.size() == 1);
	CHECK(result.missing[0].source_name == "Missing1");

	std::ifstream jf(result.json_path, std::ios::binary);
	Json reread = Json::parse(jf);
	CHECK(reread["sources"][1]["settings"]["file"].get<std::string>() == portable::utf8_from_path(src_dir / "none.png"));
}

TEST_CASE("export: 進捗を呼ぶ")
{
	TempDir tmp;
	auto src_dir = tmp.path() / "src";
	write_file(src_dir / "a.png", "a");
	write_file(src_dir / "b.png", "b");
	write_file(src_dir / "c.png", "c");

	Json collection;
	collection["name"] = "col";
	collection["sources"] = Json::array();
	collection["sources"].push_back({{"name", "A"}, {"settings", {{"file", portable::utf8_from_path(src_dir / "a.png")}}}});
	collection["sources"].push_back({{"name", "B"}, {"settings", {{"file", portable::utf8_from_path(src_dir / "b.png")}}}});
	collection["sources"].push_back({{"name", "C"}, {"settings", {{"file", portable::utf8_from_path(src_dir / "c.png")}}}});

	ExportOptions opt;
	opt.parent_dir = tmp.path() / "out";
	std::filesystem::create_directories(opt.parent_dir);

	std::vector<std::pair<size_t, size_t>> calls;
	portable::export_collection(collection, opt, [&](size_t done, size_t total) { calls.emplace_back(done, total); });

	REQUIRE(calls.size() == 3);
	CHECK(calls[0] == std::make_pair<size_t, size_t>(1, 3));
	CHECK(calls[1] == std::make_pair<size_t, size_t>(2, 3));
	CHECK(calls[2] == std::make_pair<size_t, size_t>(3, 3));
}

TEST_CASE("export: 書けないと例外")
{
	TempDir tmp;
	auto blocking_file = tmp.path() / "blocking";
	write_file(blocking_file, "not-a-folder");

	Json collection;
	collection["name"] = "col";

	ExportOptions opt;
	opt.parent_dir = blocking_file;

	CHECK_THROWS_AS(portable::export_collection(collection, opt), portable::ExportError);
}

TEST_CASE("export: zip を作る")
{
	TempDir tmp;
	auto src_dir = tmp.path() / "src";
	write_file(src_dir / "a.png", "a-bytes");

	Json collection;
	collection["name"] = "col";
	collection["sources"] = Json::array();
	collection["sources"].push_back({{"name", "A"}, {"settings", {{"file", portable::utf8_from_path(src_dir / "a.png")}}}});

	ExportOptions opt;
	opt.parent_dir = tmp.path() / "out";
	std::filesystem::create_directories(opt.parent_dir);
	opt.make_zip = true;

	ExportResult result = portable::export_collection(collection, opt);

	CHECK(result.zip_path == opt.parent_dir / "col.zip");
	CHECK(std::filesystem::exists(result.zip_path));
}
