#include "portable/export.hpp"

#include "portable/zip.hpp"

#include <fstream>

namespace portable {

namespace {

bool candidate_free(const std::filesystem::path &dir_candidate, const std::filesystem::path &zip_candidate)
{
	std::error_code ec;
	return !std::filesystem::exists(dir_candidate, ec) && !std::filesystem::exists(zip_candidate, ec);
}

} // namespace

std::filesystem::path next_free_dir(const std::filesystem::path &parent, const std::string &base)
{
	std::filesystem::path dir_candidate = parent / path_from_utf8(base);
	std::filesystem::path zip_candidate = parent / path_from_utf8(base + ".zip");
	if (candidate_free(dir_candidate, zip_candidate))
		return dir_candidate;

	for (int n = 2;; ++n) {
		std::string name = base + "_" + std::to_string(n);
		dir_candidate = parent / path_from_utf8(name);
		zip_candidate = parent / path_from_utf8(name + ".zip");
		if (candidate_free(dir_candidate, zip_candidate))
			return dir_candidate;
	}
}

ExportResult export_collection(const Json &collection, const ExportOptions &opt, const ProgressFn &progress)
{
	std::string base = "scene-collection";
	if (collection.contains("name") && collection["name"].is_string())
		base = sanitize_filename(collection["name"].get<std::string>());
	else
		base = sanitize_filename(base);

	ExportResult result;
	result.out_dir = next_free_dir(opt.parent_dir, base);

	ConversionPlan plan = plan_conversion(collection, stat_file);

	std::filesystem::path assets_dir = result.out_dir / "assets";
	std::error_code mkdir_ec;
	std::filesystem::create_directories(assets_dir, mkdir_ec);
	if (mkdir_ec)
		throw ExportError("出力フォルダを作れません: " + utf8_from_path(assets_dir));

	size_t total = plan.copies.size();
	size_t done = 0;
	for (const auto &copy : plan.copies) {
		std::filesystem::path src = path_from_utf8(copy.src_path);
		std::filesystem::path dst = assets_dir / path_from_utf8(copy.dst_name);
		std::error_code copy_ec;
		std::filesystem::copy_file(src, dst, copy_ec);
		if (copy_ec)
			throw ExportError("コピーに失敗しました: " + copy.src_path);
		++done;
		if (progress)
			progress(done, total);
	}
	result.copied = done;
	result.missing = std::move(plan.missing);

	result.json_path = result.out_dir / path_from_utf8(base + ".json");
	{
		std::ofstream json_out(result.json_path, std::ios::binary | std::ios::trunc);
		if (!json_out)
			throw ExportError("JSON を書き込めません: " + utf8_from_path(result.json_path));
		std::string dumped = plan.converted.dump(2) + "\n";
		json_out.write(dumped.data(), static_cast<std::streamsize>(dumped.size()));
		json_out.flush();
		if (!json_out)
			throw ExportError("JSON を書き込めません: " + utf8_from_path(result.json_path));
	}

	if (opt.make_zip) {
		result.zip_path = opt.parent_dir / path_from_utf8(utf8_from_path(result.out_dir.filename()) + ".zip");
		write_zip(result.zip_path, result.out_dir);
	}

	return result;
}

} // namespace portable
