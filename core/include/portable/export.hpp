#pragma once

#include <filesystem>
#include <functional>
#include <vector>

#include "portable/convert.hpp"

namespace portable {

struct ExportOptions {
	std::filesystem::path parent_dir;
	bool make_zip = false;
};

struct ExportResult {
	std::filesystem::path out_dir;
	std::filesystem::path json_path;
	std::filesystem::path zip_path; // make_zip == false なら空
	size_t copied = 0;
	std::vector<MissingFile> missing;
};

using ProgressFn = std::function<void(size_t done, size_t total)>;

std::filesystem::path next_free_dir(const std::filesystem::path &parent, const std::string &base);
ExportResult export_collection(const Json &collection, const ExportOptions &opt, const ProgressFn &progress = {});

} // namespace portable
