#include <doctest/doctest.h>

#include "portable/zip.hpp"
#include "portable/convert.hpp"

#include <miniz.h>

#include <cstring>
#include <fstream>
#include <string>

namespace {

class TempDir {
public:
	TempDir()
	{
		auto base = std::filesystem::temp_directory_path();
		for (int i = 0; i < 1000; ++i) {
			auto candidate = base / ("portable_zip_test_" + std::to_string(
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

// 指定したエントリ名を探し、見つかれば file_index を返す。
bool find_entry(mz_zip_archive &zip, const std::string &name, mz_uint &out_index)
{
	mz_uint count = mz_zip_reader_get_num_files(&zip);
	for (mz_uint i = 0; i < count; ++i) {
		mz_zip_archive_file_stat stat{};
		if (!mz_zip_reader_file_stat(&zip, i, &stat))
			continue;
		if (name == stat.m_filename) {
			out_index = i;
			return true;
		}
	}
	return false;
}

} // namespace

TEST_CASE("zip: 読み戻して一致")
{
	TempDir tmp;
	auto root = tmp.path() / "root";
	write_file(root / "本配信.json", "{\"name\":\"本配信\"}");
	write_file(root / "assets" / "背景.png", "background-bytes");
	write_file(root / "assets" / "a.mp4", "video-bytes");

	auto zip_path = tmp.path() / "out.zip";
	portable::write_zip(zip_path, root);

	REQUIRE(std::filesystem::exists(zip_path));

	mz_zip_archive zip{};
	REQUIRE(mz_zip_reader_init_file(&zip, zip_path.string().c_str(), 0));

	struct Expected {
		std::string name;
		std::string content;
		bool has_non_ascii;
	};
	std::vector<Expected> expected = {
		{"本配信.json", "{\"name\":\"本配信\"}", true},
		{"assets/背景.png", "background-bytes", true},
		{"assets/a.mp4", "video-bytes", false},
	};

	for (const auto &e : expected) {
		mz_uint idx = 0;
		REQUIRE(find_entry(zip, e.name, idx));

		mz_zip_archive_file_stat stat{};
		REQUIRE(mz_zip_reader_file_stat(&zip, idx, &stat));
		CHECK(stat.m_method == 0); // STORED(無圧縮)
		CHECK((stat.m_bit_flag & 0x0800) != 0);

		size_t size = 0;
		void *data = mz_zip_reader_extract_to_heap(&zip, idx, &size, 0);
		REQUIRE(data != nullptr);
		std::string content(static_cast<const char *>(data), size);
		mz_free(data);
		CHECK(content == e.content);
	}

	mz_zip_reader_end(&zip);
}
