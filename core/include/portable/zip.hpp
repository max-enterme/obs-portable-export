#pragma once

#include <filesystem>

namespace portable {

void write_zip(const std::filesystem::path &zip_path, const std::filesystem::path &root_dir); // 失敗時 ExportError

} // namespace portable
