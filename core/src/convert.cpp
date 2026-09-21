#include "portable/convert.hpp"

#include <algorithm>
#include <cctype>
#include <unordered_map>
#include <unordered_set>

namespace portable {

namespace {

char to_lower_ascii(char c) {
	if (c >= 'A' && c <= 'Z')
		return static_cast<char>(c - 'A' + 'a');
	return c;
}

std::string lower_ascii(const std::string &s)
{
	std::string out = s;
	for (char &c : out)
		c = to_lower_ascii(c);
	return out;
}

bool ends_with_ci(const std::string &s, const std::string &suffix)
{
	if (suffix.size() > s.size())
		return false;
	size_t offset = s.size() - suffix.size();
	for (size_t i = 0; i < suffix.size(); ++i) {
		if (to_lower_ascii(s[offset + i]) != to_lower_ascii(suffix[i]))
			return false;
	}
	return true;
}

std::string last_path_element(const std::string &value)
{
	size_t pos = value.find_last_of("/\\");
	if (pos == std::string::npos)
		return value;
	return value.substr(pos + 1);
}

struct StemExt {
	std::string stem;
	std::string ext;
};

// 最後のパス要素の拡張子を分ける(最後の '.' 以降、'.' を含む)。
// 要素が '.' で始まり他に '.' が無いなら拡張子なし。
StemExt split_ext(const std::string &filename)
{
	size_t last_dot = filename.find_last_of('.');
	if (last_dot == std::string::npos || last_dot == 0)
		return {filename, ""};
	return {filename.substr(0, last_dot), filename.substr(last_dot)};
}

} // namespace

bool is_local_path(const std::string &value)
{
	if (value.size() >= 2 && std::isalpha(static_cast<unsigned char>(value[0])) && value[1] == ':') {
		if (value.size() >= 3 && (value[2] == '\\' || value[2] == '/'))
			return true;
	}
	if (!value.empty() && (value[0] == '/' || value[0] == '\\'))
		return true;
	return false;
}

std::string sanitize_filename(const std::string &name)
{
	std::string s = name;

	for (char &c : s) {
		unsigned char uc = static_cast<unsigned char>(c);
		if (uc < 0x20) {
			c = '_';
			continue;
		}
		switch (c) {
		case '<':
		case '>':
		case ':':
		case '"':
		case '/':
		case '\\':
		case '|':
		case '?':
		case '*':
			c = '_';
			break;
		default:
			break;
		}
	}

	size_t begin = 0;
	size_t end = s.size();
	while (begin < end && s[begin] == ' ')
		++begin;
	while (end > begin && s[end - 1] == ' ')
		--end;
	s = s.substr(begin, end - begin);

	while (!s.empty() && s.back() == '.')
		s.pop_back();

	if (s.empty())
		s = "asset";

	if (s.size() > 80) {
		size_t cut = 80;
		while (cut > 0 && (static_cast<unsigned char>(s[cut]) & 0xC0) == 0x80)
			--cut;
		s = s.substr(0, cut);
	}

	std::string upper = s;
	for (char &c : upper)
		c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
	static const std::unordered_set<std::string> reserved = {
		"CON", "PRN", "AUX", "NUL", "COM1", "COM2", "COM3", "COM4", "COM5", "COM6", "COM7", "COM8", "COM9",
		"LPT1", "LPT2", "LPT3", "LPT4", "LPT5", "LPT6", "LPT7", "LPT8", "LPT9",
	};
	if (reserved.count(upper) != 0)
		s += "_";

	return s;
}

std::string same_file_key(const std::string &utf8_path)
{
	std::string s = utf8_path;
	for (char &c : s) {
		if (c == '\\')
			c = '/';
	}

	std::string prefix;
	std::string rest = s;
	if (s.size() >= 2 && s[0] == '/' && s[1] == '/') {
		prefix = "//";
		rest = s.substr(2);
	} else if (!s.empty() && s[0] == '/') {
		prefix = "/";
		rest = s.substr(1);
	}

	std::vector<std::string> parts;
	size_t i = 0;
	while (i <= rest.size()) {
		size_t j = rest.find('/', i);
		if (j == std::string::npos)
			j = rest.size();
		std::string seg = rest.substr(i, j - i);
		if (!seg.empty() && seg != ".") {
			if (seg == "..") {
				if (!parts.empty())
					parts.pop_back();
			} else {
				parts.push_back(seg);
			}
		}
		i = j + 1;
	}

	std::string result = prefix;
	for (size_t k = 0; k < parts.size(); ++k) {
		if (k > 0)
			result += "/";
		result += parts[k];
	}

	for (char &c : result)
		c = to_lower_ascii(c);

	return result;
}

std::filesystem::path path_from_utf8(const std::string &utf8)
{
	return std::filesystem::u8path(utf8);
}

std::string utf8_from_path(const std::filesystem::path &p)
{
	return p.u8string();
}

FileKind stat_file(const std::string &utf8_path)
{
	std::error_code ec;
	auto st = std::filesystem::status(path_from_utf8(utf8_path), ec);
	if (ec || !std::filesystem::exists(st))
		return FileKind::Missing;
	if (std::filesystem::is_directory(st))
		return FileKind::Directory;
	if (std::filesystem::is_regular_file(st))
		return FileKind::RegularFile;
	return FileKind::Missing;
}

namespace {

struct PlanState {
	const StatFn &stat;
	ConversionPlan &plan;
	std::unordered_map<std::string, std::string> key_to_name;
	std::unordered_map<std::string, std::string> used_name_lower;
};

void handle_reference(Json &value_node, const std::string &raw_value, const std::string &stem_raw,
		       const std::string &ext, const std::string &owner_name, PlanState &state)
{
	std::string key = same_file_key(raw_value);

	auto cached = state.key_to_name.find(key);
	if (cached != state.key_to_name.end()) {
		value_node = "./assets/" + cached->second;
		return;
	}

	FileKind kind = state.stat(raw_value);
	if (kind == FileKind::Missing) {
		state.plan.missing.push_back({owner_name, raw_value, MissingReason::NotFound});
		return;
	}
	if (kind == FileKind::Directory) {
		state.plan.missing.push_back({owner_name, raw_value, MissingReason::IsDirectory});
		return;
	}

	std::string sanitized_stem = sanitize_filename(stem_raw);
	std::string candidate = sanitized_stem + ext;
	int n = 2;
	while (state.used_name_lower.count(lower_ascii(candidate)) != 0) {
		candidate = sanitized_stem + "_" + std::to_string(n) + ext;
		++n;
	}

	state.used_name_lower[lower_ascii(candidate)] = key;
	state.key_to_name[key] = candidate;
	state.plan.copies.push_back({raw_value, candidate});
	value_node = "./assets/" + candidate;
}

void process_single_refs(Json &conf, const std::string &owner_name, PlanState &state)
{
	static const char *keys[] = {"file", "local_file", "path", "image_path", "track_matte_path", "text_file"};
	for (const char *key : keys) {
		if (!conf.contains(key))
			continue;
		Json &v = conf[key];
		if (!v.is_string())
			continue;
		std::string value = v.get<std::string>();
		if (!is_local_path(value))
			continue;

		StemExt se = split_ext(last_path_element(value));
		std::string stem_raw = owner_name;
		if (!se.ext.empty() && ends_with_ci(stem_raw, se.ext))
			stem_raw = stem_raw.substr(0, stem_raw.size() - se.ext.size());

		handle_reference(v, value, stem_raw, se.ext, owner_name, state);
	}
}

void process_array_refs(Json &conf, const std::string &owner_name, PlanState &state)
{
	static const char *keys[] = {"files", "playlist"};
	for (const char *key : keys) {
		if (!conf.contains(key))
			continue;
		Json &arr = conf[key];
		if (!arr.is_array())
			continue;
		for (Json &elem : arr) {
			if (!elem.is_object() || !elem.contains("value"))
				continue;
			Json &vv = elem["value"];
			if (!vv.is_string())
				continue;
			std::string value = vv.get<std::string>();
			if (!is_local_path(value))
				continue;

			StemExt se = split_ext(last_path_element(value));
			handle_reference(vv, value, se.stem, se.ext, owner_name, state);
		}
	}
}

void process_config_object(Json &conf, const std::string &owner_name, PlanState &state)
{
	process_single_refs(conf, owner_name, state);
	process_array_refs(conf, owner_name, state);
}

void walk_value(Json &node, bool top_level, PlanState &state)
{
	if (node.is_object()) {
		for (auto it = node.begin(); it != node.end(); ++it) {
			const std::string &key = it.key();
			if (top_level && key == "modules")
				continue;

			Json &child = it.value();
			if ((key == "settings" || key == "transition") && child.is_object()) {
				std::string owner_name;
				if (node.contains("name") && node["name"].is_string())
					owner_name = node["name"].get<std::string>();
				process_config_object(child, owner_name, state);
			}
			walk_value(child, false, state);
		}
	} else if (node.is_array()) {
		for (Json &item : node)
			walk_value(item, false, state);
	}
}

} // namespace

ConversionPlan plan_conversion(const Json &collection, const StatFn &stat)
{
	ConversionPlan plan;
	plan.converted = collection;

	PlanState state{stat, plan};
	walk_value(plan.converted, true, state);

	return plan;
}

} // namespace portable
