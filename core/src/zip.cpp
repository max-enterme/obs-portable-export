#include "portable/zip.hpp"

#include "portable/convert.hpp"

#include <miniz.h>

#include <fstream>
#include <vector>

namespace portable {

namespace {

struct WriteState {
	std::ofstream out;
};

size_t zip_write_callback(void *opaque, mz_uint64 file_ofs, const void *buf, size_t n)
{
	auto *state = static_cast<WriteState *>(opaque);
	state->out.seekp(static_cast<std::streamoff>(file_ofs));
	state->out.write(static_cast<const char *>(buf), static_cast<std::streamsize>(n));
	if (!state->out)
		return 0;
	return n;
}

struct ReadState {
	std::ifstream in;
};

size_t zip_read_callback(void *opaque, mz_uint64 file_ofs, void *buf, size_t n)
{
	auto *state = static_cast<ReadState *>(opaque);
	state->in.seekg(static_cast<std::streamoff>(file_ofs));
	state->in.read(static_cast<char *>(buf), static_cast<std::streamsize>(n));
	return static_cast<size_t>(state->in.gcount());
}

// root からの相対パスを '/' 区切りの UTF-8 で返す。
std::string relative_entry_name(const std::filesystem::path &root, const std::filesystem::path &file)
{
	std::filesystem::path rel = file.lexically_relative(root);
	std::string s = utf8_from_path(rel);
	for (char &c : s) {
		if (c == '\\')
			c = '/';
	}
	return s;
}

} // namespace

void write_zip(const std::filesystem::path &zip_path, const std::filesystem::path &root_dir)
{
	std::vector<std::filesystem::path> files;
	std::error_code walk_ec;
	for (auto it = std::filesystem::recursive_directory_iterator(
		     root_dir, std::filesystem::directory_options::skip_permission_denied, walk_ec);
	     it != std::filesystem::recursive_directory_iterator(); it.increment(walk_ec)) {
		if (walk_ec)
			throw ExportError("zip: フォルダを読めません: " + utf8_from_path(root_dir));
		if (it->is_regular_file())
			files.push_back(it->path());
	}

	WriteState write_state;
	write_state.out.open(zip_path, std::ios::binary | std::ios::trunc);
	if (!write_state.out)
		throw ExportError("zip: 書き込めません: " + utf8_from_path(zip_path));

	mz_zip_archive zip{};
	zip.m_pWrite = &zip_write_callback;
	zip.m_pIO_opaque = &write_state;

	if (!mz_zip_writer_init_v2(&zip, 0, MZ_ZIP_FLAG_WRITE_ZIP64))
		throw ExportError("zip: 初期化に失敗しました: " + utf8_from_path(zip_path));

	for (const auto &file : files) {
		std::string entry_name = relative_entry_name(root_dir, file);

		ReadState read_state;
		read_state.in.open(file, std::ios::binary);
		if (!read_state.in) {
			mz_zip_writer_end(&zip);
			throw ExportError("zip: 読み込めません: " + utf8_from_path(file));
		}

		std::error_code size_ec;
		mz_uint64 size = static_cast<mz_uint64>(std::filesystem::file_size(file, size_ec));
		if (size_ec) {
			mz_zip_writer_end(&zip);
			throw ExportError("zip: サイズを取得できません: " + utf8_from_path(file));
		}

		if (!mz_zip_writer_add_read_buf_callback(&zip, entry_name.c_str(), &zip_read_callback, &read_state, size,
							  nullptr, nullptr, 0, MZ_NO_COMPRESSION, nullptr, 0, nullptr, 0)) {
			mz_zip_writer_end(&zip);
			throw ExportError("zip: 追加に失敗しました: " + utf8_from_path(file));
		}
	}

	if (!mz_zip_writer_finalize_archive(&zip)) {
		mz_zip_writer_end(&zip);
		throw ExportError("zip: 書き込みの確定に失敗しました: " + utf8_from_path(zip_path));
	}
	mz_zip_writer_end(&zip);

	if (!write_state.out)
		throw ExportError("zip: 書き込めません: " + utf8_from_path(zip_path));
}

} // namespace portable
