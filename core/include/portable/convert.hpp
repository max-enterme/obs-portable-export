#pragma once

#include <filesystem>
#include <functional>
#include <stdexcept>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace portable {

using Json = nlohmann::ordered_json;

enum class FileKind { Missing, RegularFile, Directory };
using StatFn = std::function<FileKind(const std::string &utf8_path)>;

enum class MissingReason { NotFound, IsDirectory };

struct MissingFile {
	std::string source_name;   // 要素の "name"。無ければ ""
	std::string original_path; // 元の値そのまま
	MissingReason reason;
};

struct PlannedCopy {
	std::string src_path; // 元の値そのまま(UTF-8)
	std::string dst_name; // assets/ 内のファイル名(UTF-8)
};

struct ConversionPlan {
	Json converted;
	std::vector<PlannedCopy> copies;  // 初出順。dst_name は大小無視で重複なし
	std::vector<MissingFile> missing; // 出現順
};

bool is_local_path(const std::string &value);
std::string sanitize_filename(const std::string &name);
std::string same_file_key(const std::string &utf8_path);
ConversionPlan plan_conversion(const Json &collection, const StatFn &stat);
FileKind stat_file(const std::string &utf8_path); // 実ファイルシステム版。path_from_utf8 で開く

std::filesystem::path path_from_utf8(const std::string &utf8); // = std::filesystem::u8path(utf8)
std::string utf8_from_path(const std::filesystem::path &p);    // = p.u8string()

class ExportError : public std::runtime_error {
	using std::runtime_error::runtime_error;
};

} // namespace portable
